/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a replacement for the "mkbb.sh" and "mkbb2.sh" scripts from the    */
/* "Big Brother" monitoring tool from BB4 Technologies.                       */
/*                                                                            */
/* Primary reason for doing this: Shell scripts perform badly, and with a     */
/* medium-sized installation (~150 hosts) it takes several minutes to         */
/* generate the webpages. This is a problem, when the pages are used for      */
/* 24x7 monitoring of the system status.                                      */
/*                                                                            */
/* Copyright (C) 2002 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: pagegen.c,v 1.86 2003-08-16 06:59:26 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "bbgen.h"
#include "util.h"
#include "loaddata.h"
#include "pagegen.h"
#include "larrdgen.h"
#include "infogen.h"
#include "bb-replog.h"
#include "reportdata.h"
#include "debug.h"

int  subpagecolumns = 1;
int  hostsbeforepages = 0;
char *includecolumns = NULL;
int  sort_grouponly_items = 0; /* Standard BB behaviour: Dont sort group-only items */
char *documentationcgi = NULL;
char *htmlextension = ".html"; /* Filename extension for generated files */
char *doctargetspec = " TARGET=\"_blank\"";
char *defaultpagetitle = NULL;

char *eventignorecolumns = NULL;

/* Format strings for htaccess files */
char *htaccess = NULL;
char *bbhtaccess = NULL;
char *bbpagehtaccess = NULL;
char *bbsubpagehtaccess = NULL;

char *hf_prefix[3];            /* header/footer prefixes for BB, BB2, BBNK pages*/


void select_headers_and_footers(char *prefix)
{
	hf_prefix[PAGE_BB]  = (char *) malloc(strlen(prefix)+1); sprintf(hf_prefix[PAGE_BB],  "%s",   prefix);
	hf_prefix[PAGE_BB2] = (char *) malloc(strlen(prefix)+2); sprintf(hf_prefix[PAGE_BB2], "%s2",  prefix);
	hf_prefix[PAGE_NK]  = (char *) malloc(strlen(prefix)+3); sprintf(hf_prefix[PAGE_NK],  "%snk", prefix);
}


int interesting_column(int pagetype, int color, int alert, char *columnname, char *onlycols)
{
	/*
	 * Decides if a given column is to be included on a page.
	 */

	if (pagetype == PAGE_BB) {
		/* Fast-path the BB page. */

		int result = 1;

		if (onlycols) {
			/* onlycols explicitly list the columns to include (for bb page only) */
			char *search;

			/* loaddata::init_group guarantees that onlycols start and end with a '|' */
			search = (char *) malloc(strlen(columnname)+3);
			sprintf(search, "|%s|", columnname);
			result = (strstr(onlycols, search) != NULL);
			free(search);
		}

		/* This is final. */
		return result;
	}

	/* pagetype is now known NOT to be PAGE_BB */

	/* LARRD and INFO columns are always included on non-BB pages */
	if (larrdcol && (strcmp(columnname, larrdcol) == 0)) return 1;
	if (infocol && (strcmp(columnname, infocol) == 0)) return 1;

	if (includecolumns) {
		/* includecolumns are other columns to include always on non-BB pages (bb2, bbnk) */
		char *col1 = (char *) malloc(strlen(columnname)+3); /* 3 = 2 commas and a NULL */
		int result;

		sprintf(col1, ",%s,", columnname);
		result = (strstr(includecolumns, col1) != NULL);
		free(col1);

		/* If included, done here. Otherwise may be included further down. */
		if (result) return result;
	}

	switch (pagetype) {
	  case PAGE_BB2:
		  /* Include all non-green tests */
		  return ((color == COL_RED) || (color == COL_YELLOW) || (color == COL_PURPLE));

	  case PAGE_NK:
		  /* Include only RED or YELLOW tests with "alert" property set. 
		   * Even then, the "conn" test is included only when RED.
		   */
		if (alert) {
			if (color == COL_RED)  return 1;
			if ( (color == COL_YELLOW) || (color == COL_CLEAR) ) return (strcmp(columnname, "conn") != 0);
		}
		break;
	}

	return 0;
}

col_list_t *gen_column_list(host_t *hostlist, int pagetype, char *onlycols)
{
	/*
	 * Build a list of all the columns that are in use by
	 * any host in the hostlist passed as parameter.
	 * The column list will be sorted by column name, except 
	 * when doing a "group-only" and the standard BB behaviour.
	 */

	col_list_t	*head;
	host_t		*h;
	entry_t		*e;
	col_list_t	*newlistitem, *collist_walk;

	/* Code de-obfuscation trick: Add a null record as the head item */
	/* Simplifies handling since head != NULL and we never have to insert at head of list */
	head = (col_list_t *) malloc(sizeof(col_list_t));
	head->column = &null_column;
	head->next = NULL;

	if (!sort_grouponly_items && (onlycols != NULL)) {

		/* 
		 * This is the original BB handling of "group-only". 
		 * All items are included, whether there are any test data
		 * for a column or not. The order given in the group-only
		 * directive is maintained.
		 * We simple convert the group-only directive to a
		 * col_list_t linked list.
		 */

		char *p1 = onlycols;
		char *p2;
		bbgen_col_t *col;

		collist_walk = head;
		do {
			if (*p1 == '|') p1++;

			p2 = strchr(p1, '|');
			if (p2) {
				*p2 = '\0';

				for (col = colhead; (col && (strcmp(p1, col->name) != 0)); col = col->next);
				if (col) {
					newlistitem = (col_list_t *) malloc(sizeof(col_list_t));
					newlistitem->column = col;
					newlistitem->next = NULL;
					collist_walk->next = newlistitem;
					collist_walk = collist_walk->next;
				}
				*p2 = '|';
			}

			p1 = p2;
		} while (p1 != NULL);

		/* Skip the dummy record */
		collist_walk = head; head = head->next; free(collist_walk);

		/* We're done - dont even look at the actual test data. */
		return (head);
	}

	for (h = hostlist; (h); h = h->next) {
		/*
		 * This is for everything except "standard group-only" handled above.
		 * So also for group-only with --sort-group-only-items.
		 * Note that in a group-only here, items may be left out if there
		 * are no test data for a column at all.
		 */
		for (e = h->entries; (e); e = e->next) {
			if (interesting_column(pagetype, e->color, e->alert, e->column->name, onlycols)) {
				/* See where e->column should go in list */
				collist_walk = head; 
				while ( (collist_walk->next && 
                               		strcmp(e->column->name, ((col_list_t *)(collist_walk->next))->column->name) > 0) ) {
					collist_walk = collist_walk->next;
				}

				if ((collist_walk->next == NULL) || ((col_list_t *)(collist_walk->next))->column != e->column) {
					/* collist_walk points to the entry before the new one */
					newlistitem = (col_list_t *) malloc(sizeof(col_list_t));
					newlistitem->column = e->column;
					newlistitem->next = collist_walk->next;
					collist_walk->next = newlistitem;
				}
			}
		}
	}

	/* Skip the dummy record */
	collist_walk = head; head = head->next; free(collist_walk);
	return (head);
}


void setup_htaccess(const char *pagepath)
{
	char htaccessfn[MAX_PATH];
	char htaccesscontent[1024];

	if (htaccess == NULL) return;

	htaccesscontent[0] = '\0';

	if (strlen(pagepath) == 0) {
		sprintf(htaccessfn, "%s", htaccess);
		if (bbhtaccess) strcpy(htaccesscontent, bbhtaccess);
	}
	else {
		char *pagename, *subpagename, *p;
		char *path = malcop(pagepath);

		for (p = path + strlen(path) - 1; ((p > path) && (*p == '/')); p--) *p = '\0';

		sprintf(htaccessfn, "%s/%s", path, htaccess);

		pagename = path; if (*pagename == '/') pagename++;
		p = strchr(pagename, '/'); 
		if (p) { 
			*p = '\0'; 
			subpagename = p+1;
			p = strchr(subpagename, '/');
			if (p) *p = '\0';
			if (bbsubpagehtaccess) sprintf(htaccesscontent, bbsubpagehtaccess, pagename, subpagename);
		}
		else {
			if (bbpagehtaccess) sprintf(htaccesscontent, bbpagehtaccess, pagename);
		}

		free(path);
	}

	if (strlen(htaccesscontent)) {
		FILE *fd;
		struct stat st;

		if (stat(htaccessfn, &st) == 0) {
			dprintf("htaccess file %s exists, not overwritten\n", htaccessfn);
			return;
		}

		fd = fopen(htaccessfn, "w");
		if (fd) {
			fprintf(fd, "%s\n", htaccesscontent);
			fclose(fd);
		}
		else {
			errprintf("Cannot create %s\n", htaccessfn);
		}
	}
}


static char *nameandcomment(host_t *host)
{
	static char result[1024];

	if (host->comment) {
		sprintf(result, "%s (%s)", host->displayname, host->comment);
	}
	else strcpy(result, host->displayname);

	return result;
}

void do_hosts(host_t *head, char *onlycols, FILE *output, char *grouptitle, int pagetype, char *pagepath)
{
	/*
	 * This routine outputs the host part of a page or a group.
	 * I.e. it generates buttons and links to all the tests for
	 * a host, and the host docs.
	 */

	host_t	*h;
	entry_t	*e;
	col_list_t *groupcols, *gc;
	int	genstatic;
	int	columncount;
	char	*bbskin;

	if (head == NULL)
		return;

	bbskin = malcop(getenv("BBSKIN"));

	/* Generate static or dynamic links (from BBLOGSTATUS) ? */
	genstatic = generate_static();

	fprintf(output, "<A NAME=hosts-blk>&nbsp;</A>\n\n");

	groupcols = gen_column_list(head, pagetype, onlycols);
	for (columncount=0, gc=groupcols; (gc); gc = gc->next, columncount++) ;

	if (groupcols) {

		/* Start the table ... */
		fprintf(output, "<CENTER><TABLE SUMMARY=\"Group Block\" BORDER=0 CELLPADDING=2>\n");

		/* Generate the host rows */
		for (h = head; (h); h = h->next) {
			/* If there is a host pretitle, show it. */
			dprintf("Host:%s, pretitle:%s\n", h->hostname, textornull(h->pretitle));

			if (h->pretitle) {
				fprintf(output, "<tr><td colspan=%d align=center valign=middle><br><font %s>%s</font></td></tr>\n", 
						columncount+1, getenv("MKBBTITLE"), h->pretitle);
			}

			if (h->pretitle || (h == head)) {
				/* output group title and column headings */
				fprintf(output, "<TR><TD VALIGN=MIDDLE ROWSPAN=2><CENTER><FONT %s>%s</FONT></CENTER></TD>\n", 
					getenv("MKBBTITLE"), grouptitle);
				for (gc=groupcols; (gc); gc = gc->next) {
					fprintf(output, " <TD ALIGN=CENTER VALIGN=BOTTOM WIDTH=45>\n");
					fprintf(output, " <A HREF=\"%s\"><FONT %s><B>%s</B></FONT></A> </TD>\n", 
						columnlink(gc->column->link, gc->column->name), 
						getenv("MKBBCOLFONT"), gc->column->name);
				}
				fprintf(output, "</TR> \n<TR><TD COLSPAN=%d><HR WIDTH=\"100%%\"></TD></TR>\n\n", columncount);
			}

			fprintf(output, "<TR>\n <TD NOWRAP><A NAME=\"%s\">&nbsp;</A>\n", h->hostname);

			/* First the hostname and a notes-link.
			 *
			 * If a documentation CGI is defined, use that.
			 *
			 * else if a host has a direct notes-link, use that.
			 *
			 * else if no direct link and we are doing a BB2/BBNK page, 
			 * provide a link to the main page with this host (there
			 * may be links to documentation in some page-title).
			 *
			 * else just put the hostname there.
			 */
			if (documentationcgi) {
				fprintf(output, "<A HREF=\"%s/%s\" %s><FONT %s>%s</FONT></A>\n </TD>",
					getenv("CGIBINURL"), cgidoclink(documentationcgi, h->hostname),
					doctargetspec, getenv("MKBBROWFONT"), nameandcomment(h));
			}
			else if (h->link != &null_link) {
				fprintf(output, "<A HREF=\"%s\" %s><FONT %s>%s</FONT></A>\n </TD>",
					hostlink(h->link), doctargetspec, getenv("MKBBROWFONT"), nameandcomment(h));
			}
			else if (pagetype != PAGE_BB) {
				/* Provide a link to the page where this host lives */
				fprintf(output, "<A HREF=\"%s/%s\" %s><FONT %s>%s</FONT></A>\n </TD>",
					getenv("BBWEB"), hostpage_link(h), doctargetspec,
					getenv("MKBBROWFONT"), nameandcomment(h));
			}
			else {
				fprintf(output, "<FONT %s>%s</FONT>\n </TD>",
					getenv("MKBBROWFONT"), nameandcomment(h));
			}

			/* Then the columns. */
			for (gc = groupcols; (gc); gc = gc->next) {
				fprintf(output, "<TD ALIGN=CENTER>");

				/* Any column entry for this host ? */
				for (e = h->entries; (e && (e->column != gc->column)); e = e->next) ;
				if (e == NULL) {
					fprintf(output, "-");
				}
				else if (e->histlogname) {
					/* Snapshot points to historical logfile */
					fprintf(output, "<A HREF=\"%s/bb-histlog.sh?HOST=%s&amp;SERVICE=%s&amp;TIMEBUF=%s\">", 
						getenv("CGIBINURL"), h->hostname, e->column->name, e->histlogname);

					fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0></A>",
						bbskin, dotgiffilename(e->color, e->acked, e->oldage),
						alttag(e),
						getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
				}
				else if (reportstart == 0) {
					/* Standard webpage */
					char *skin;

					skin = (e->skin ? e->skin : bbskin);

					if (e->sumurl) {
						/* A summary host. */
						fprintf(output, "<A HREF=\"%s\">", e->sumurl);
					}
					else if (genstatic) {
						/*
						 * Dont use htmlextension here - it's for the
						 * pages generated by bbd.
						 */
						fprintf(output, "<A HREF=\"%s/html/%s.%s.html\">",
							getenv("BBWEB"), h->hostname, e->column->name);
					}
					else {
						fprintf(output, "<A HREF=\"%s/bb-hostsvc.sh?HOSTSVC=%s.%s\">",
							getenv("CGIBINURL"), commafy(h->hostname), e->column->name);
					}

					fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0></A>",
						skin, dotgiffilename(e->color, e->acked, e->oldage),
						alttag(e),
						getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
				}
				else {
					/* Report format output */
					if ((e->color == COL_GREEN) || (e->color == COL_CLEAR)) {
						fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0>",
							bbskin, dotgiffilename(e->color, e->acked, e->oldage),
							colorname(e->color),
							getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
					}
					else {
						if (dynamicreport) {
							fprintf(output, "<A HREF=\"%s/bb-replog.sh?HOSTSVC=%s.%s&amp;IP=%s",
								getenv("CGIBINURL"), commafy(h->hostname), e->column->name, h->ip);
							fprintf(output, "&amp;COLOR=%s&amp;PCT=%.2f&amp;ST=%u&amp;END=%u",
								colorname(e->color), e->repinfo->fullavailability, 
								(unsigned int)e->repinfo->reportstart, (unsigned int)reportend);
							fprintf(output, "&amp;RED=%.2f&amp;YEL=%.2f&amp;GRE=%.2f&amp;PUR=%.2f&amp;CLE=%.2f&amp;BLU=%.2f",
								e->repinfo->fullpct[COL_RED], e->repinfo->fullpct[COL_YELLOW], 
								e->repinfo->fullpct[COL_GREEN], e->repinfo->fullpct[COL_PURPLE], 
								e->repinfo->fullpct[COL_CLEAR], e->repinfo->fullpct[COL_BLUE]);
							fprintf(output, "&amp;STYLE=%s&amp;FSTATE=%s",
								stylenames[reportstyle], e->repinfo->fstate);
							fprintf(output, "&amp;REDCNT=%d&amp;YELCNT=%d&amp;GRECNT=%d&amp;PURCNT=%d&amp;CLECNT=%d&amp;BLUCNT=%d",
								e->repinfo->count[COL_RED], e->repinfo->count[COL_YELLOW], 
								e->repinfo->count[COL_GREEN], e->repinfo->count[COL_PURPLE], 
								e->repinfo->count[COL_CLEAR], e->repinfo->count[COL_BLUE]);
							if (h->reporttime) fprintf(output, "&amp;REPORTTIME=%s", h->reporttime);
							fprintf(output, "&amp;WARNPCT=%.2f", h->reportwarnlevel);
							fprintf(output, "&amp;RECENTGIFS=%d", use_recentgifs);
							fprintf(output, "\">\n");
						}
						else {
							FILE *htmlrep, *textrep;
							char htmlrepfn[MAX_PATH];
							char textrepfn[MAX_PATH];
							char textrepurl[MAX_PATH];

							/* File names are relative - current directory is the output dir */
							/* pagepath is either empty, or it ends with a '/' */
							sprintf(htmlrepfn, "%s%s-%s%s", 
								pagepath, h->hostname, e->column->name, htmlextension);
							sprintf(textrepfn, "%savail-%s-%s.txt",
								pagepath, h->hostname, e->column->name);
							sprintf(textrepurl, "%s/%s", 
								getenv("BBWEB"), textrepfn);

							htmlrep = fopen(htmlrepfn, "w");
							textrep = fopen(textrepfn, "w");

							/* Pre-build the test-specific report */
							restore_replogs(e->causes);
							generate_replog(htmlrep, textrep, textrepurl,
									h->hostname, h->ip, e->column->name, e->color, 
									reportstyle, reportstart, reportend,
									reportwarnlevel, reportgreenlevel, e->repinfo);

							if (textrep) fclose(textrep);
							if (htmlrep) fclose(htmlrep);

							fprintf(output, "<A HREF=\"%s-%s%s\">\n", 
								h->hostname, e->column->name, htmlextension);
						}
						fprintf(output, "<FONT SIZE=-1 COLOR=%s><B>%.2f</B></FONT></A>\n",
							colorname(e->color), e->repinfo->fullavailability);
					}
				}
				fprintf(output, "</TD>\n");
			}

			fprintf(output, "</TR>\n\n");
		}

		fprintf(output, "</TABLE></CENTER><BR><BR>\n");
	}

	/* Free the columnlist allocated by gen_column_list() */
	while (groupcols) {
		gc = groupcols;
		groupcols = groupcols->next;
		free(gc);
	}

	free(bbskin);
}

void do_groups(group_t *head, FILE *output, char *pagepath)
{
	/*
	 * This routine generates all the groups on a given page.
	 * It also triggers generating host output for hosts
	 * within the groups.
	 */

	group_t *g;

	if (head == NULL)
		return;

	fprintf(output, "<CENTER> \n\n<A NAME=begindata>&nbsp;</A>\n");

	for (g = head; (g); g = g->next) {
		if (g->hosts && g->pretitle) {
			fprintf(output, "<CENTER><TABLE BORDER=0>\n");
			fprintf(output, "  <TR><TD><CENTER><FONT %s>%s</FONT></CENTER></TD></TR>\n", getenv("MKBBTITLE"), g->pretitle);
			fprintf(output, "  <TR><TD><HR WIDTH=\"100%%\"></TD></TR>\n");
			fprintf(output, "</TABLE></CENTER>\n");
		}

		do_hosts(g->hosts, g->onlycols, output, g->title, PAGE_BB, pagepath);
	}
	fprintf(output, "\n</CENTER>\n");
}

void do_summaries(dispsummary_t *sums, FILE *output)
{
	/*
	 * Generates output for summary statuses received from others.
	 */

	dispsummary_t *s;
	host_t *sumhosts = NULL;
	host_t *walk;

	if (sums == NULL) {
		/* No summary items */
		return;
	}

	for (s=sums; (s); s = s->next) {
		/* Generate host records out of all unique s->row values */

		host_t *newhost;
		entry_t *newentry;
		dispsummary_t *s2;

		/* Do we already have it ? */
		for (newhost = sumhosts; (newhost && (strcmp(s->row, newhost->hostname) != 0) ); newhost = newhost->next);

		if (newhost == NULL) {
			/* New summary "host" */
			newhost = init_host(s->row, NULL, NULL, 0,0,0,0, 0, 0, 0.0, NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL);

			/*
			 * Cannot have the pseudo host in the official hostlist,
			 * it messes up the WML generator later.
			 */
			if (hosthead->hostentry == newhost) hosthead = hosthead->next;

			/* Insert into sorted host list */
			if ((!sumhosts) || (strcmp(newhost->hostname, sumhosts->hostname) < 0)) {
				/* Empty list, or new entry goes before list head item */
				newhost->next = sumhosts;
				sumhosts = newhost;
			}
			else {
				/* Walk list until we find element that goes after new item */
				for (walk = sumhosts; 
			      	(walk->next && (strcmp(newhost->hostname, ((host_t *)walk->next)->hostname) > 0)); 
			      	walk = walk->next) ;

				/* "walk" points to element before the new item */
				newhost->next = walk->next;
				walk->next = newhost;
			}


			/* Setup the "event" records from the column records */
			for (s2 = sums; (s2); s2 = s2->next) {
				
				if (strcmp(s2->row, s->row) == 0) {
					newentry = (entry_t *) malloc(sizeof(entry_t));

					newentry->column = find_or_create_column(s2->column);
					newentry->color = s2->color;
					strcpy(newentry->age, "");
					newentry->oldage = 1; /* Use standard gifs */
					newentry->acked = 0;
					newentry->alert = 0;
					newentry->onwap = 0;
					newentry->propagate = 1;
					newentry->sumurl = s2->url;
					newentry->skin = NULL;
					newentry->testflags = NULL;
					newentry->repinfo = NULL;
					newentry->causes = NULL;
					newentry->histlogname = NULL;
					newentry->next = newhost->entries;
					newhost->entries = newentry;
				}
			}
		}
	}

	fprintf(output, "<A NAME=\"summaries-blk\">&nbsp;</A>\n");
	fprintf(output, "<CENTER>\n");
	fprintf(output, "<TABLE SUMMARY=\"Summary Block\" BORDER=0><TR><TD>\n");
	fprintf(output, "<CENTER><FONT %s>\n", getenv("MKBBTITLE"));
	fprintf(output, "%s\n", getenv("MKBBREMOTE"));
	fprintf(output, "</FONT></CENTER></TD></TR><TR><TD>\n");
	fprintf(output, "<HR WIDTH=\"100%%\"></TD></TR>\n");
	fprintf(output, "<TR><TD>\n");

	do_hosts(sumhosts, NULL, output, "", 0, NULL);

	fprintf(output, "</TD></TR></TABLE>\n");
	fprintf(output, "</CENTER>\n");
}

void do_page_subpages(FILE *output, bbgen_page_t *subs, char *pagepath)
{
	/*
	 * This routine does NOT generate subpages!
	 * Instead, it generates the LINKS to the subpages below any given page.
	 */

	bbgen_page_t	*p;
	link_t  *link;
	int	currentcolumn;
	char	pagelink[MAX_PATH];

	if (subs) {
		fprintf(output, "<A NAME=\"pages-blk\">&nbsp;</A>\n");

		fprintf(output, "<BR>\n<CENTER>\n");
		fprintf(output, "<TABLE SUMMARY=\"Page Block\" BORDER=0>\n");

		currentcolumn = 0;
		for (p = subs; (p); p = p->next) {
			if (p->pretitle) {
				/*
				 * Output a page-link title text.
				 */
				if (currentcolumn != 0) {
					fprintf(output, "</TR>\n");
					currentcolumn = 0;
				}

				fprintf(output, "<TR><TD COLSPAN=%d><CENTER> \n<FONT %s>\n", 
						(2*subpagecolumns + (subpagecolumns - 1)), getenv("MKBBTITLE"));
				fprintf(output, "   <br>%s\n", p->pretitle);
				fprintf(output, "</FONT></CENTER></TD></TR>\n");
				fprintf(output, "<TR><TD COLSPAN=%d><HR WIDTH=\"100%%\"></TD></TR>\n", 
						(2*subpagecolumns + (subpagecolumns - 1)));
			}

			if (currentcolumn == 0) fprintf(output, "<TR>");

			link = find_link(p->name);
			if (link != &null_link) {
				fprintf(output, "<TD><FONT %s><A HREF=\"%s\">%s</A></FONT></TD>\n", 
					getenv("MKBBROWFONT"), hostlink(link), p->title);
			}
			else {
				fprintf(output, "<TD><FONT %s>%s</FONT></TD>\n", getenv("MKBBROWFONT"), p->title);
			}

			sprintf(pagelink, "%s/%s/%s/%s%s", getenv("BBWEB"), pagepath, p->name, p->name, htmlextension);
			fprintf(output, "<TD><CENTER><A HREF=\"%s\">\n", cleanurl(pagelink));

			fprintf(output, "<IMG SRC=\"%s/%s\" WIDTH=\"%s\" HEIGHT=\"%s\" BORDER=0 ALT=\"%s\"></A>\n", 
				getenv("BBSKIN"), dotgiffilename(p->color, 0, p->oldage), 
				getenv("DOTWIDTH"), getenv("DOTHEIGHT"),
				colorname(p->color));
			fprintf(output, "</CENTER></TD>\n");

			if (currentcolumn == (subpagecolumns-1)) {
				fprintf(output, "</TR>\n");
				currentcolumn = 0;
			}
			else {
				/* Need to have a little space between columns */
				fprintf(output, "<TD WIDTH=\"%s\">&nbsp;</TD>", getenv("DOTWIDTH"));
				currentcolumn++;
			}
		}

		if (currentcolumn != 0) fprintf(output, "</TR>\n");

		fprintf(output, "</TABLE><BR><BR>\n");
		fprintf(output, "</CENTER>\n");
	}
}


void do_one_page(bbgen_page_t *page, dispsummary_t *sums, int embedded)
{
	FILE	*output;
	char	pagepath[MAX_PATH];
	char	filename[MAX_PATH];
	char	tmpfilename[MAX_PATH];
	char	*dirdelim;

	pagepath[0] = '\0';
	if (embedded) {
		output = stdout;
	}
	else {
		if (page->parent == NULL) {
			char	indexfilename[MAX_PATH];

			/* top level page */
			sprintf(filename, "bb%s", htmlextension);
			sprintf(indexfilename, "index%s", htmlextension);
			symlink(filename, indexfilename);
			dprintf("Symlinking %s -> %s\n", filename, indexfilename);
		}
		else {
			char tmppath[MAX_PATH];
			bbgen_page_t *pgwalk;
	
			for (pgwalk = page; (pgwalk); pgwalk = pgwalk->parent) {
				if (strlen(pgwalk->name)) {
					sprintf(tmppath, "%s/%s/", pgwalk->name, pagepath);
					strcpy(pagepath, tmppath);
				}
			}
	
			sprintf(filename, "%s/%s%s", pagepath, page->name, htmlextension);
		}
		sprintf(tmpfilename, "%s.tmp", filename);


		/* Try creating the output file. If it fails, we may need to create the directories */
		output = fopen(tmpfilename, "w");
		if (output == NULL) {
			char indexfilename[MAX_PATH];
			char pagebasename[MAX_PATH];
			char *p;
			int res;

			/* Make sure the directories exist. */
			dirdelim = tmpfilename;
			while ((dirdelim = strchr(dirdelim, '/')) != NULL) {
				*dirdelim = '\0';
				mkdir(tmpfilename, 0755);
				*dirdelim = '/';
				dirdelim++;
			}

			/* We've created the directories. Now retry creating the file. */
			output = fopen(tmpfilename, "w");

			/* 
			 * We had to create the directory. Set up an index.html file for 
			 * the directory where we created our new file.
			 */
			strcpy(indexfilename, filename);
			p = strrchr(indexfilename, '/'); 
			if (p) p++; else p = indexfilename;
			sprintf(p, "index%s", htmlextension);
			sprintf(pagebasename, "%s%s", page->name, htmlextension);
			res = symlink(pagebasename, indexfilename);
			dprintf("Symlinking %s->%s : %d/%d\n", pagebasename, indexfilename, res, errno);

			if (output == NULL) {
				errprintf("Cannot open file %s\n", tmpfilename);
				return;
			}
		}
	}

	setup_htaccess(pagepath);

	headfoot(output, hf_prefix[PAGE_BB], pagepath, "header", page->color);

	if (page->subpages || page->pretitle || defaultpagetitle) {
		/* Print the "Pages hosted locally" header - either the defined pretitle, or the default */
		fprintf(output, "<CENTER><TABLE BORDER=0>\n");
		fprintf(output, "  <TR><TD><br><CENTER><FONT %s>%s</FONT></CENTER></TD></TR>\n", 
			getenv("MKBBTITLE"), 
			(page->pretitle ? page->pretitle : (defaultpagetitle ? defaultpagetitle : getenv("MKBBLOCAL"))));
		fprintf(output, "  <TR><TD><HR WIDTH=\"100%%\"></TD></TR>\n");
		fprintf(output, "</TABLE></CENTER>\n");
	}

	if (!embedded && !hostsbeforepages && page->subpages) do_page_subpages(output, page->subpages, pagepath);
	do_hosts(page->hosts, NULL, output, "", PAGE_BB, pagepath);
	do_groups(page->groups, output, pagepath);
	if (!embedded && hostsbeforepages && page->subpages) do_page_subpages(output, page->subpages, pagepath);

	/* Summaries and extensions on main page only */
	if (!embedded && (page->parent == NULL)) {
		do_summaries(dispsums, output);
		do_bbext(output, "BBMKBBEXT", "mkbb");
	}

	headfoot(output, hf_prefix[PAGE_BB], pagepath, "footer", page->color);

	if (!embedded) {
		fclose(output);
		if (rename(tmpfilename, filename)) {
			errprintf("Cannot rename %s to %s - error %d\n", tmpfilename, filename, errno);
		}
	}
}


void do_page_with_subs(bbgen_page_t *curpage, dispsummary_t *sums)
{
	bbgen_page_t *levelpage;

	for (levelpage = curpage; (levelpage); levelpage = levelpage->next) {
		do_one_page(levelpage, sums, 0);
		do_page_with_subs(levelpage->subpages, NULL);
	}
}

int wanted_eventcolumn(char *service)
{
	char svc[100];
	int result;

	if (!eventignorecolumns || (strlen(service) > (sizeof(svc)-3))) return 1;

	sprintf(svc, ",%s,", service);
	result = (strstr(eventignorecolumns, svc) == NULL);

	return result;
}

void do_eventlog(FILE *output, int maxcount, int maxminutes)
{
	FILE *eventlog;
	char eventlogfilename[MAX_PATH];
	char newcol[3], oldcol[3];
	time_t cutoff;
	event_t	*events;
	int num, eventintime_count;
	struct stat st;
	char l[MAX_LINE_LEN];
	char title[200];


	cutoff = ( (maxminutes) ? (time(NULL) - maxminutes*60) : 0);
	if ((!maxcount) || (maxcount > 100)) maxcount = 100;

	sprintf(eventlogfilename, "%s/allevents", getenv("BBHIST"));
	eventlog = fopen(eventlogfilename, "r");
	if (!eventlog) {
		errprintf("Cannot open eventlog");
		return;
	}

	if (stat(eventlogfilename, &st) == 0) {
		time_t curtime;
		int done = 0;

		/* Find a spot in the eventlog file close to where the cutoff time is */
		fseek(eventlog, 0, SEEK_END);
		do {
			/* Go back maxcount*80 bytes - one entry is ~80 bytes */
			if (ftell(eventlog) > maxcount*80) {
				fseek(eventlog, -maxcount*80, SEEK_CUR); 
				fgets(l, sizeof(l), eventlog); /* Skip to start of line */
				fgets(l, sizeof(l), eventlog);
				sscanf(l, "%*s %*s %u %*u %*u %*s %*s %*d", (unsigned int *)&curtime);
				done = (curtime < cutoff);
			}
			else {
				rewind(eventlog);
				done = 1;
			}
		} while (!done);
	}
	
	events = (event_t *) malloc(maxcount*sizeof(event_t));
	eventintime_count = num = 0;

	while (fgets(l, sizeof(l), eventlog)) {

		sscanf(l, "%s %s %u %u %u %s %s %d",
			events[num].hostname, events[num].service,
			(unsigned int *)&events[num].eventtime, 
			(unsigned int *)&events[num].changetime, 
			(unsigned int *)&events[num].duration, 
			newcol, oldcol, &events[num].state);

		if ((events[num].eventtime > cutoff) && find_host(events[num].hostname) && wanted_eventcolumn(events[num].service)) {
			events[num].newcolor = eventcolor(newcol);
			events[num].oldcolor = eventcolor(oldcol);
			eventintime_count++;

			num = (num + 1) % maxcount;
		}
	}

	if (eventintime_count > 0) {
		int firstevent, lastevent;
		char *bgcolors[2] = { "000000", "000033" };
		int  bgcolor = 0;

		if (eventintime_count <= maxcount) {
			firstevent = 0;
			lastevent = eventintime_count-1;
		}
		else {
			firstevent = num;
			lastevent = ( (num == 0) ? maxcount : (num-1));
			eventintime_count = maxcount;
		}

		sprintf(title, "%d events received in the past %u minutes",
			eventintime_count, (unsigned int)((time(NULL)-events[firstevent].eventtime) / 60));

		fprintf(output, "<BR><BR>\n");
        	fprintf(output, "<TABLE SUMMARY=\"$EVENTSTITLE\" BORDER=0>\n");
		fprintf(output, "<TR BGCOLOR=\"333333\">\n");
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"teal\">%s</FONT></TD></TR>\n", title);

		for (num = lastevent; (eventintime_count); eventintime_count--, num = ((num == 0) ? (maxcount-1) : (num - 1)) ) {
			fprintf(output, "<TR BGCOLOR=%s>\n", bgcolors[bgcolor]);
			bgcolor = ((bgcolor + 1) % 2);

			fprintf(output, "<TD ALIGN=CENTER>%s</TD>\n", ctime(&events[num].eventtime));

			if (events[num].newcolor == COL_CLEAR) {
				fprintf(output, "<TD ALIGN=CENTER BGCOLOR=black><FONT COLOR=white>%s</FONT></TD>\n",
					events[num].hostname);
			}
			else {
				fprintf(output, "<TD ALIGN=CENTER BGCOLOR=%s><FONT COLOR=black>%s</FONT></TD>\n",
					colorname(events[num].newcolor),
					events[num].hostname);
			}

			fprintf(output, "<TD ALIGN=LEFT>%s</TD>\n", events[num].service);
			fprintf(output, "<TD><A HREF=\"%s\">\n", 
				histlogurl(events[num].hostname, events[num].service, events[num].changetime));
			fprintf(output, "<IMG SRC=\"%s/%s\"  HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0 ALT=%s></A>\n", 
				getenv("BBSKIN"), dotgiffilename(events[num].oldcolor, 0, 0), 
				getenv("DOTHEIGHT"), getenv("DOTWIDTH"), 
				colorname(events[num].oldcolor));
			fprintf(output, "<IMG SRC=\"%s/arrow.gif\" BORDER=0 ALT=\"From -&gt; To\">\n", 
				getenv("BBSKIN"));
			fprintf(output, "<TD><A HREF=\"%s\">\n", 
				histlogurl(events[num].hostname, events[num].service, events[num].eventtime));
			fprintf(output, "<IMG SRC=\"%s/%s\"  HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0 ALT=%s></A>\n", 
				getenv("BBSKIN"), dotgiffilename(events[num].newcolor, 0, 0), 
				getenv("DOTHEIGHT"), getenv("DOTWIDTH"), 
				colorname(events[num].newcolor));
		}

		fprintf(output, "</TABLE>\n");
	}
	else {
		/* No events during the past maxminutes */
		sprintf(title, "No events received in the last %d minutes", maxminutes);

		fprintf(output, "<CENTER><BR>\n");
		fprintf(output, "<TABLE SUMMARY=\"%s\" BORDER=0>\n", title);
		fprintf(output, "<TR BGCOLOR=\"333333\">\n");
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"teal\">%s</FONT></TD>\n", title);
		fprintf(output, "</TR>\n");
		fprintf(output, "</TABLE>\n");
		fprintf(output, "</CENTER>\n");
	}

	free(events);
	fclose(eventlog);
}


void do_acklog(FILE *output, int maxcount, int maxminutes)
{
	FILE *acklog;
	char acklogfilename[MAX_PATH];
	time_t cutoff;
	struct stat st;
	char l[MAX_LINE_LEN];
	char title[200];
	ack_t *acks;
	int num, ackintime_count;


	cutoff = ( (maxminutes) ? (time(NULL) - maxminutes*60) : 0);
	if ((!maxcount) || (maxcount > 100)) maxcount = 100;

	sprintf(acklogfilename, "%s/acklog", getenv("BBACKS"));
	acklog = fopen(acklogfilename, "r");
	if (!acklog) {
		/* If no acklog, that is OK - some people dont use acks */
		dprintf("Cannot open acklog");
		return;
	}

	/* HACK ALERT! */
	if (stat(acklogfilename, &st) == 0) {
		char dummy[80];

		/* Assume a log entry is max 150 bytes */
		if (150*maxcount < st.st_size)
			fseek(acklog, -150*maxcount, SEEK_END);
		fgets(dummy, sizeof(dummy), acklog);
	}

	acks = (ack_t *) malloc(maxcount*sizeof(ack_t));
	ackintime_count = num = 0;

	while (fgets(l, sizeof(l), acklog)) {
		char ackedby[MAX_LINE_LEN], hosttest[MAX_LINE_LEN], color[10], ackmsg[MAX_LINE_LEN];
		char ackfn[MAX_PATH];
		char *testname;
		int ok;

		if (atol(l) >= cutoff) {
			int c_used;
			char *p, *p1;

			sscanf(l, "%u\t%d\t%d\t%d\t%s\t%s\t%s\t%n",
				(unsigned int *)&acks[num].acktime, &acks[num].acknum,
				&acks[num].duration, &acks[num].acknum2,
				ackedby, hosttest, color, &c_used);

			p1 = ackmsg;
			for (p=l+c_used, p1=ackmsg; (*p); ) {
				/*
				 * Need to de-code the ackmsg - it may have been entered
				 * via a web page that did "%asciival" encoding.
				 */
				if ((*p == '%') && (strlen(p) >= 3) && isxdigit((int)*(p+1)) && isxdigit((int)*(p+2))) {
					char hexnum[3];

					hexnum[0] = *(p+1);
					hexnum[1] = *(p+2);
					hexnum[2] = '\0';
					*p1 = (char) strtol(hexnum, NULL, 16);
					p1++;
					p += 3;
				}
				else {
					*p1 = *p;
					p1++;
					p++;
				}
			}
			/* Show only the first 30 characters in message */
			ackmsg[30] = '\0';

			sprintf(ackfn, "%s/ack.%s", getenv("BBACKS"), hosttest);

			testname = strrchr(hosttest, '.');
			if (testname) {
				*testname = '\0'; testname++; 
			}
			else testname = "unknown";

			ok = 1;

			/* Ack occurred within wanted timerange ? */
			if (ok && (acks[num].acktime < cutoff)) ok = 0;

			/* Unknown host ? */
			if (ok && (find_host(hosttest) == NULL)) ok = 0;

			if (ok) {
				char *ackerp;

				/* If ack has expired or tag file is gone, the ack is no longer valid */
				acks[num].ackvalid = 1;
				if ((acks[num].acktime + 60*acks[num].duration) < time(NULL)) acks[num].ackvalid = 0;
				if (acks[num].ackvalid && (stat(ackfn, &st) != 0)) acks[num].ackvalid = 0;

				ackerp = ackedby;
				if (strncmp(ackerp, "np_", 3) == 0) ackerp += 3;
				p = strrchr(ackerp, '_');
				if (p > ackerp) *p = '\0';
				acks[num].ackedby = malcop(ackerp);

				acks[num].hostname = malcop(hosttest);
				acks[num].testname = malcop(testname);
				strcat(color, " "); acks[num].color = parse_color(color);
				acks[num].ackmsg = malcop(ackmsg);
				ackintime_count++;

				num = (num + 1) % maxcount;
			}
		}
	}

	if (ackintime_count > 0) {
		int firstack, lastack;
		int period = maxminutes;

		if (ackintime_count <= maxcount) {
			firstack = 0;
			lastack = ackintime_count-1;
			period = maxminutes;
		}
		else {
			firstack = num;
			lastack = ( (num == 0) ? maxcount : (num-1));
			ackintime_count = maxcount;
			period = ((time(NULL)-acks[firstack].acktime) / 60);
		}

		sprintf(title, "%d events acknowledged in the past %u minutes", ackintime_count, period);

		fprintf(output, "<BR><BR>\n");
		fprintf(output, "<TABLE SUMMARY=\"%s\" BORDER=0>\n", title);
		fprintf(output, "<TR BGCOLOR=\"333333\">\n");
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"teal\">%s</FONT></TD></TR>\n", title);

		for (num = lastack; (ackintime_count); ackintime_count--, num = ((num == 0) ? (maxcount-1) : (num - 1)) ) {
			fprintf(output, "<TR BGCOLOR=#000000>\n");

			fprintf(output, "<TD ALIGN=CENTER><FONT COLOR=white>%s</FONT></TD>\n", ctime(&acks[num].acktime));
			fprintf(output, "<TD ALIGN=CENTER BGCOLOR=%s><FONT COLOR=black>%s</FONT></TD>\n", colorname(acks[num].color), acks[num].hostname);
			fprintf(output, "<TD ALIGN=CENTER><FONT COLOR=white>%s</FONT></TD>\n", acks[num].testname);

			if (acks[num].color != -1) {
   				fprintf(output, "<TD ALIGN=CENTER><IMG SRC=\"%s/%s\"></TD>\n", 
					getenv("BBSKIN"), 
					dotgiffilename(acks[num].color, acks[num].ackvalid, 1));
			}
			else
   				fprintf(output, "<TD ALIGN=CENTER><FONT COLOR=white>&nbsp;</FONT></TD>\n");

			fprintf(output, "<TD ALIGN=LEFT BGCOLOR=#000033>%s</TD>\n", acks[num].ackedby);
			fprintf(output, "<TD ALIGN=LEFT>%s</TD></TR>\n", acks[num].ackmsg);
		}

	}
	else {
		sprintf(title, "No events acknowledged in the last %u minutes", maxminutes);

		fprintf(output, "<BR><BR>\n");
		fprintf(output, "<TABLE SUMMARY=\"%s\" BORDER=0>\n", title);
		fprintf(output, "<TR BGCOLOR=\"333333\">\n");
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"teal\">%s</FONT></TD></TR>\n", title);
	}

	fprintf(output, "</TABLE>\n");

	fclose(acklog);
}


int do_bb2_page(char *filename, int summarytype)
{
	bbgen_page_t	bb2page;
	FILE		*output;
	char		*tmpfilename = (char *) malloc(strlen(filename)+5);
	hostlist_t 	*h;

	/* Build a "page" with the hosts that should be included in bb2 page */
	bb2page.name = bb2page.title = "";
	bb2page.color = COL_GREEN;
	bb2page.subpages = NULL;
	bb2page.groups = NULL;
	bb2page.hosts = NULL;
	bb2page.next = NULL;

	for (h=hosthead; (h); h=h->next) {
		entry_t	*e;
		int	useit = 0;

		/*
		 * Why dont we use the interesting_column() * routine here ? 
		 *
		 * Well, because what we are interested in for now is
		 * to determine if this HOST should be included on the page.
		 *
		 * We dont care if individual COLUMNS are included if the 
		 * host shows up - some columns are always included, e.g.
		 * the info- and larrd-columns, but we dont want that to
		 * trigger a host being on the bb2 page!
		 */
		switch (summarytype) {
		  case PAGE_BB2:
			/* Normal BB2 page */
			useit = ((h->hostentry->color == COL_RED) || 
				 (h->hostentry->color == COL_YELLOW) || 
				 (h->hostentry->color == COL_PURPLE));
			break;

		  case PAGE_NK:
			/* The NK page */
			for (useit=0, e=h->hostentry->entries; (e && !useit); e=e->next) {
				useit = (e->alert && (!e->acked) && ((e->color == COL_RED) || ((e->color == COL_YELLOW) && (strcmp(e->column->name, "conn") != 0))));
			}
			break;
		}

		if (useit) {
			host_t *newhost, *walk;

			if (h->hostentry->color > bb2page.color) bb2page.color = h->hostentry->color;

			/* We need to create a copy of the original record, */
			/* as we will diddle with the pointers */
			newhost = (host_t *) malloc(sizeof(host_t));
			memcpy(newhost, h->hostentry, sizeof(host_t));
			newhost->next = NULL;

			/* Insert into sorted host list */
			if ((!bb2page.hosts) || (strcmp(newhost->hostname, bb2page.hosts->hostname) < 0)) {
				/* Empty list, or new entry goes before list head item */
				newhost->next = bb2page.hosts;
				bb2page.hosts = newhost;
			}
			else {
				/* Walk list until we find element that goes after new item */
				for (walk = bb2page.hosts; 
				      (walk->next && (strcmp(newhost->hostname, ((host_t *)walk->next)->hostname) > 0)); 
				      walk = walk->next) ;

				/* "walk" points to element before the new item.
				 *
		 		 * Check for duplicate hosts. We can have a host on two normal BB
		 		 * pages, but in the BB2 page we want it only once.
		 		 */
				if (strcmp(walk->hostname, newhost->hostname) == 0) {
					/* Duplicate at start of list */
					free(newhost);
				}
				else if (walk->next && (strcmp(((host_t *)walk->next)->hostname, newhost->hostname) == 0)) {
					/* Duplicate inside list */
					free(newhost);
				}
				else {
					/* New host */
					newhost->next = walk->next;
					walk->next = newhost;
				}
			}
		}
	}

	sprintf(tmpfilename, "%s.tmp", filename);
	output = fopen(tmpfilename, "w");
	if (output == NULL) {
		errprintf("Cannot open file %s", tmpfilename);
		free(tmpfilename);
		return bb2page.color;
	}

	headfoot(output, hf_prefix[summarytype], "", "header", bb2page.color);

	fprintf(output, "<center>\n");
	fprintf(output, "\n<A NAME=begindata>&nbsp;</A> \n<A NAME=\"hosts-blk\">&nbsp;</A>\n");

	if (bb2page.hosts) {
		do_hosts(bb2page.hosts, NULL, output, "", summarytype, NULL);
	}
	else {
		/* "All Monitored Systems OK */
		fprintf(output, "<FONT SIZE=+2 FACE=\"Arial, Helvetica\"><BR><BR><I>All Monitored Systems OK</I></FONT><BR><BR>");
	}

	if ((snapshot == 0) && (summarytype == PAGE_BB2)) {
		do_eventlog(output, 0, 240);
		do_acklog(output, 25, 240);
		do_bbext(output, "BBMKBB2EXT", "mkbb");
	}

	fprintf(output, "</center>\n");
	headfoot(output, hf_prefix[summarytype], "", "footer", bb2page.color);

	fclose(output);
	if (rename(tmpfilename, filename)) {
		errprintf("Cannot rename %s to %s - error %d\n", tmpfilename, filename, errno);
	}

	free(tmpfilename);

	{
		/* Free temporary hostlist */
		host_t *h1, *h2;

		h1 = bb2page.hosts;
		while (h1) {
			h2 = h1;
			h1 = h1->next;
			free(h2);
		}
	}

	return bb2page.color;
}
