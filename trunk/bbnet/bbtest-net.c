/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbtest-net.c,v 1.30 2003-04-25 16:49:09 henrik Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <netdb.h>
#include <sys/wait.h>

#include "bbgen.h"
#include "util.h"
#include "debug.h"
#include "contest.h"

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

/* toolid values */
#define TOOL_CONTEST	0
#define TOOL_NSLOOKUP	1
#define TOOL_DIG	2
#define TOOL_NTP        3
#define TOOL_FPING      4

/* dnslookup values */
#define DNS_THEN_IP     0	/* Try DNS - if it fails, use IP from bb-hosts */
#define DNS_ONLY        1	/* DNS only - if it fails, report service down */
#define IP_ONLY         2	/* IP only - dont do DNS lookups */

typedef struct {
	char *testname;		/* Name of the test = column name in BB report */
	int namelen;		/* Length of name - "testname:port" has this as strlen(testname), others 0 */
	int portnum;		/* Port number this service runs on */
	int toolid;		/* Which tool to use */
	void *items;		/* testitem_t linked list of tests for this service */
	void *next;
} service_t;

typedef struct {
	char *hostname;
	char ip[16];

	int dialup;		/* dialup flag (if set, failed tests report as clear) */
	int testip;		/* testip flag (dont do dns lookups on hostname) */
	int dnserror;		/* set internally if we cannot find the host's IP */
	int in_sla;		/* set internally if inside SLA period. If not, failed tests are blue */

	/* The following are for the connectivity test */
	int noconn;		/* noconn flag (dont report "conn" at all */
	int noping;		/* noping flag (report "conn" as clear=disabled */
	int badconn[3];		/* badconn:x:y:z flag */
	int downcount;		/* number of successive failed conn tests */
	time_t downstart;	/* time() of first conn failure */

	void *next;
} testedhost_t;

typedef struct {
	testedhost_t	*host;		/* Pointer to testedhost_t record for this host */
	service_t	*service;	/* Pointer to service_t record for the service to test */
	int		reverse;	/* "!testname" flag */
	int		dialup;		/* "?testname" flag */
	int		alwaystrue;	/* "~testname" flag */
	int		silenttest;	/* "testname:s" flag */
	int		open;		/* Is the service open ? NB: Shows true state of service, ignores flags */
	test_t		*testresult;	/* Banner and duration of test */
	char		*banner;
	void		*next;
} testitem_t;


service_t	*svchead = NULL;		/* Head of all known services */
service_t	*pingtest = NULL;		/* Identifies the pingtest within svchead list */
testedhost_t	*testhosthead = NULL;		/* Head of all hosts */
testitem_t	*testhead = NULL;		/* Head of all tests */
char		*nonetpage = NULL;		/* The "NONETPAGE" env. variable */
int		dnsmethod = DNS_THEN_IP;	/* How to do DNS lookups */


service_t *add_service(char *name, int port, int namelen, int toolid)
{
	service_t *svc;

	svc = malloc(sizeof(service_t));
	svc->portnum = port;
	svc->testname = malloc(strlen(name)+1);
	strcpy(svc->testname, name);
	svc->toolid = toolid;
	svc->namelen = namelen;
	svc->items = NULL;
	svc->next = svchead;
	svchead = svc;

	return svc;
}

int getportnumber(char *svcname)
{
	struct servent *svcinfo;

	svcinfo = getservbyname(svcname, NULL);
	return (svcinfo ? ntohs(svcinfo->s_port) : 0);
}

void load_services(void)
{
	char *netsvcs;
	char *p;

	netsvcs = malloc(strlen(getenv("BBNETSVCS"))+1);
	strcpy(netsvcs, getenv("BBNETSVCS"));

	p = strtok(netsvcs, " ");
	while (p) {
		add_service(p, getportnumber(p), 0, TOOL_CONTEST);
		p = strtok(NULL, " ");
	}
	free(netsvcs);

	/* Save NONETPAGE env. var in ",test1,test2," format for easy and safe grepping */
	nonetpage = malloc(strlen(getenv("NONETPAGE"))+3);
	sprintf(nonetpage, ",%s,", getenv("NONETPAGE"));
	for (p=nonetpage; (*p); p++) if (*p == ' ') *p = ',';
}


testitem_t *init_testitem(testedhost_t *host, service_t *service, int dialuptest, int reversetest, int alwaystruetest, int silenttest)
{
	testitem_t *newtest;

	newtest = malloc(sizeof(testitem_t));
	newtest->host = host;
	newtest->service = service;
	newtest->dialup = dialuptest;
	newtest->reverse = reversetest;
	newtest->alwaystrue = alwaystruetest;
	newtest->silenttest = silenttest;
	newtest->open = 0;
	newtest->testresult = NULL;
	newtest->banner = NULL;
	newtest->next = NULL;

	return newtest;
}


void load_tests(void)
{
	FILE 	*bbhosts;
	char 	l[MAX_LINE_LEN];	/* With multiple http tests, we may have long lines */
	int	ip1, ip2, ip3, ip4;
	char	hostname[MAX_LINE_LEN];
	char	*netstring;
	char 	*p;

	bbhosts = stackfopen(getenv("BBHOSTS"), "r");
	if (bbhosts == NULL) {
		perror("No bb-hosts file found");
		return;
	}

	/* Each network test tagged with NET:locationname */
	p = getenv("BBLOCATION");
	if (p) {
		netstring = malloc(strlen(p)+5);
		sprintf(netstring, "NET:%s", p);
	}
	else {
		netstring = NULL;
	}

	while (stackfgets(l, sizeof(l), "include")) {
		p = strchr(l, '\n'); if (p) { *p = '\0'; };

		if ((l[0] == '#') || (strlen(l) == 0)) {
			/* Do nothing - it's a comment */
		}
		else if (sscanf(l, "%3d.%3d.%3d.%3d %s", &ip1, &ip2, &ip3, &ip4, hostname) == 5) {
			char *testspec;

			if (((netstring == NULL) || (strstr(l, netstring) != NULL)) && strchr(l, '#')) {

				testedhost_t *h;
				testitem_t *newtest;
				int anytests;
				int ping_dialuptest = 0;
				int ping_reversetest = 0;

				h = malloc(sizeof(testedhost_t));
				h->hostname = malloc(strlen(hostname)+1);
				strcpy(h->hostname, hostname);

				p = strchr(l, '#'); p++;
				while (isspace((unsigned int) *p)) p++;

				h->dialup = (strstr(p, "dialup") != NULL);
				h->testip = (strstr(p, "testip") != NULL);
				h->noconn = (strstr(p, "noconn") != NULL);
				h->noping = (strstr(p, "noping") != NULL);
				h->badconn[0] = h->badconn[1] = h->badconn[2] = 0;
				h->downcount = 0;
				h->in_sla = within_sla(p);
				h->ip[0] = '\0';
				h->dnserror = 0;
				anytests = 0;

				testspec = strtok(p, "\t ");
				while (testspec) {

					service_t *s;
					int dialuptest;
					int reversetest;
					int alwaystruetest;
					char *option;

					if (strncmp(testspec, "badconn:", 8) == 0) {
						sscanf(testspec, "badconn:%d:%d:%d", &h->badconn[0], &h->badconn[1], &h->badconn[2]);
					}

					/* Remove any trailing ":s", ":q", ":Q", ":portnumber" */
					option = strchr(testspec, ':'); 
					if (option) { 
						*option = '\0'; 
						option++; 
					}

					/* Test prefixes:
					 * - '?' denotes dialup test, i.e. report failures as clear.
					 * - '|' denotes reverse test, i.e. service should be DOWN.
					 * - '~' denotes test that ignores ping result (normally,
					 *       TCP tests are reported CLEAR if ping check fails;
					 *       with this flag report their true status)
					 */
					dialuptest = reversetest = alwaystruetest = 0;
					if (*testspec == '?') { dialuptest=1;     testspec++; }
					if (*testspec == '!') { reversetest=1;    testspec++; }
					if (*testspec == '~') { alwaystruetest=1; testspec++; }

					if (pingtest && (strcmp(testspec, pingtest->testname) == 0)) {
						ping_dialuptest = dialuptest;
						ping_reversetest = reversetest;
					}

					/* Find the service */
					for (s=svchead; (s && (strcmp(s->testname, testspec) != 0)); s = s->next) ;

					if (option && s) {
						/*
						 * Check if it is a service with an explicit portnumber.
						 * If it is, then create a new service record named
						 * "SERVICE_PORT" so we can merge tests for this service+port
						 * combination for multiple hosts.
						 *
						 * According to BB docs, this type of services must be in
						 * BBNETSVCS - so it is known already.
						 */
						int specialport;
						char *specialname;

						specialport = atoi(option);
						if ((s->portnum == 0) && (specialport > 0)) {
							specialname = malloc(strlen(s->testname)+10);
							sprintf(specialname, "%s_%d", s->testname, specialport);
							s = add_service(specialname, specialport, strlen(s->testname), TOOL_CONTEST);
							free(specialname);
						}
					}

					if (s) {
						anytests = 1;
						newtest = init_testitem(h, s, dialuptest, reversetest, alwaystruetest, 0);
						newtest->next = s->items;
						s->items = newtest;
					}

					testspec = strtok(NULL, "\t ");
				}

				if (pingtest && !h->noconn) {
					/* Add the ping check */
					anytests = 1;
					newtest = init_testitem(h, pingtest, ping_dialuptest, ping_reversetest, 1, 0);
					newtest->next = pingtest->items;
					pingtest->items = newtest;
				}

				if (anytests) {
					/* 
					 * Determine the IP address to test. We do it here,
					 * to avoid multiple DNS lookups for each service 
					 * we test on a host.
					 */
					if (h->testip || (dnsmethod == IP_ONLY)) {
						sprintf(h->ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
						if (strcmp(h->ip, "0.0.0.0") == 0) {
							printf("bbtest-net: %s has IP 0.0.0.0 and testip - dropped\n", hostname);
							h->dnserror = 1;
						}
					}
					else {
						struct hostent *hent;

						hent = gethostbyname(hostname);
						if (hent) {
							sprintf(h->ip, "%d.%d.%d.%d", 
								(unsigned char) hent->h_addr_list[0][0],
								(unsigned char) hent->h_addr_list[0][1],
								(unsigned char) hent->h_addr_list[0][2],
								(unsigned char) hent->h_addr_list[0][3]);
						}
						else if (dnsmethod == DNS_THEN_IP) {
							sprintf(h->ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
						}
						else {
							/* Cannot resolve hostname */
							h->dnserror = 1;
						}

						if (strcmp(h->ip, "0.0.0.0") == 0) {
							printf("bbtest-net: IP resolver error for host %s\n", hostname);
							h->dnserror = 1;
						}
					}
					h->next = testhosthead;
					testhosthead = h;
				}
				else {
					/* No network tests for this host, so drop it */
					free(h);
				}

			}
		}
		else {
			/* Other bb-hosts line - ignored */
		};
	}

	stackfclose(bbhosts);
	return;
}


void load_fping_status(void)
{
	FILE *statusfd;
	char statusfn[MAX_PATH];
	char l[MAX_LINE_LEN];
	char host[MAX_LINE_LEN];
	int  downcount;
	time_t downstart;
	testedhost_t *h;

	sprintf(statusfn, "%s/fping.status", getenv("BBTMP"));
	statusfd = fopen(statusfn, "r");
	if (statusfd == NULL) return;

	while (fgets(l, sizeof(l), statusfd)) {
		if (sscanf(l, "%s %d %lu", host, &downcount, &downstart) == 3) {
			for (h=testhosthead; (h && (strcmp(h->hostname, host) != 0)); h = h->next) ;
			if (h && !h->noping && !h->noconn) {
				h->downcount = downcount;
				h->downstart = downstart;
			}
		}
	}

	fclose(statusfd);
}

void save_fping_status(void)
{
	FILE *statusfd;
	char statusfn[MAX_PATH];
	testitem_t *t;

	sprintf(statusfn, "%s/fping.status", getenv("BBTMP"));
	statusfd = fopen(statusfn, "w");
	if (statusfd == NULL) return;

	for (t=pingtest->items; (t); t = t->next) {
		if (t->host->downcount) {
			fprintf(statusfd, "%s %d %lu\n", t->host->hostname, t->host->downcount, t->host->downstart);
		}
	}

	fclose(statusfd);
}


int run_command(char *cmd, char *errortext, char **banner)
{
	FILE	*cmdpipe;
	char	l[1024];
	int	result;
	int	piperes;

	result = 0;
	if (banner) { *banner = malloc(1024); sprintf(*banner, "Command: %s\n\n", cmd); }
	cmdpipe = popen(cmd, "r");
	if (cmdpipe == NULL) {
		if (banner) strcat(*banner, "popen() failed to run command\n");
		return -1;
	}

	while (fgets(l, sizeof(l), cmdpipe)) {
		if (banner && ((strlen(l) + strlen(*banner)) < 1024)) strcat(*banner, l);
		if (strstr(l, errortext) != NULL) result = 1;
	}
	piperes = pclose(cmdpipe);
	if (!WIFEXITED(piperes) || (WEXITSTATUS(piperes) != 0)) {
		/* Something failed */
		result = 1;
	}

	return result;
}

void run_nslookup_service(service_t *service)
{
	testitem_t	*t;
	char		cmd[1024];
	char		*p;
	char		cmdpath[MAX_PATH];

	p = getenv("NSLOOKUP");
	strcpy(cmdpath, (p ? p : "nslookup"));
	for (t=service->items; (t); t = t->next) {
		if (!t->host->dnserror) {
			sprintf(cmd, "%s %s %s 2>&1", 
				cmdpath, t->host->hostname, t->host->ip);
			t->open = (run_command(cmd, "can't find", &t->banner) == 0);
		}
	}
}

void run_dig_service(service_t *service)
{
	testitem_t	*t;
	char		cmd[1024];
	char		*p;
	char		cmdpath[MAX_PATH];

	p = getenv("DIG");
	strcpy(cmdpath, (p ? p : "dig"));
	for (t=service->items; (t); t = t->next) {
		if (!t->host->dnserror) {
			sprintf(cmd, "%s @%s %s 2>&1", 
				cmdpath, t->host->ip, t->host->hostname);
			t->open = (run_command(cmd, "Bad server", &t->banner) == 0);
		}
	}
}

void run_ntp_service(service_t *service)
{
	testitem_t	*t;
	char		cmd[1024];
	char		*p;
	char		cmdpath[MAX_PATH];

	p = getenv("NTPDATE");
	strcpy(cmdpath, (p ? p : "ntpdate"));
	for (t=service->items; (t); t = t->next) {
		if (!t->host->dnserror) {
			sprintf(cmd, "%s -u -q -p 2 %s 2>&1", cmdpath, t->host->ip);
			t->open = (run_command(cmd, "no server suitable for synchronization", &t->banner) == 0);
		}
	}
}


int run_fping_service(service_t *service)
{
	testitem_t	*t;
	char		cmd[1024];
	char		*p;
	char		cmdpath[MAX_PATH];
	char		logfn[MAX_PATH];
	FILE		*cmdpipe;
	FILE		*logfd;
	char		l[MAX_LINE_LEN];
	char		hostname[MAX_LINE_LEN];
	int		ip1, ip2, ip3, ip4;

	/* Run "fping -Ae 2>/dev/null" and feed it all IP's to test */
	p = getenv("FPING");
	strcpy(cmdpath, (p ? p : "fping"));
	sprintf(logfn, "%s/fping.%lu", getenv("BBTMP"), (unsigned long)getpid());
	sprintf(cmd, "%s -Ae 2>/dev/null 1>%s", cmdpath, logfn);

	cmdpipe = popen(cmd, "w");
	if (cmdpipe == NULL) return -1;
	for (t=service->items; (t); t = t->next) {
		if (!t->host->dnserror && !t->host->noping) {
			fprintf(cmdpipe, "%s\n", t->host->ip);
		}
	}
	pclose(cmdpipe);

	/* Load status of previously failed tests */
	load_fping_status();

	logfd = fopen(logfn, "r");
	if (logfd == NULL) { printf("Cannot open fping output file!\n"); return -1; }
	while (fgets(l, sizeof(l), logfd)) {
		if (sscanf(l, "%d.%d.%d.%d ", &ip1, &ip2, &ip3, &ip4) == 4) {
			p = strchr(l, ' ');
			if (p) *p = '\0';
			strcpy(hostname, l);
			if (p) *p = ' ';

			/*
			 * Need to loop through all testitems - there may be multiple entries for
			 * the same IP-address.
			 */
			for (t=service->items; (t); t = t->next) {
				if (strcmp(t->host->ip, hostname) == 0) {

					t->open = (strstr(l, "is alive") != NULL);
					t->banner = malloc(strlen(l)+1);
					strcpy(t->banner, l);

					if (t->open) {
						t->host->downcount = 0;
					}
					else {
						t->host->downcount++;
						if (t->host->downcount == 1) t->host->downstart = time(NULL);
					}
				}
			}
		}
	}
	fclose(logfd);
	if (!debug) unlink(logfn);

	save_fping_status();

	return 0;
}


void send_results(service_t *service)
{
	testitem_t	*t;
	int		color;
	char		msgline[MAXMSG];
	char		*svcname;
	char		*nopagename;
	int		nopage = 0;

	svcname = malloc(strlen(service->testname)+1);
	strcpy(svcname, service->testname);
	if (service->namelen) svcname[service->namelen] = '\0';

	/* Check if this service is a NOPAGENET service. */
	nopagename = malloc(strlen(svcname)+3);
	sprintf(nopagename, ",%s,", svcname);
	nopage = (strstr(nonetpage, svcname) != NULL);
	free(nopagename);

	for (t=service->items; (t); t = t->next) {
		if ((service == pingtest) && t->host->noping) {
			color = COL_CLEAR;
		}
		else {
			color = COL_GREEN;

			/*
			 * If DNS error, it is red.
			 * If not, then either (open=0,reverse=0) or (open=1,reverse=1) is wrong.
			 */
			if ((t->host->dnserror) || ((t->open + t->reverse) != 1)) color = COL_RED;

			/* Dialup hosts and dialup tests report red as clear */
			if ((color != COL_GREEN) && (t->host->dialup || t->dialup)) color = COL_CLEAR;

			/* If not inside SLA and non-green, report as BLUE */
			if (!t->host->in_sla && (color != COL_GREEN)) color = COL_BLUE;
		}

		/* Handle the "badconn" stuff for ping checks */
		if ((service == pingtest) && (color == COL_RED) && (t->host->downcount < t->host->badconn[2])) {
			if      (t->host->downcount >= t->host->badconn[1]) color = COL_YELLOW;
			else if (t->host->downcount >= t->host->badconn[0]) color = COL_CLEAR;
			else                                                color = COL_GREEN;

		}

		/* If host ping fails, report failed TCP tests as clear unless "alwaystrue" flag set */
		if ( ((color == COL_RED) || (color == COL_YELLOW)) && /* Test failed */
		     (service != pingtest)                         && /* It's not a ping test */
		     (t->host->downcount > 0)                      && /* The ping check did fail */
		     (!t->alwaystrue)                              )  /* No "~testname" flag */ {
			color = COL_CLEAR;
		}

		/* NOPAGENET services that are down are reported as yellow */
		if (nopage && (color == COL_RED)) color = COL_YELLOW;

		init_status(color);
		sprintf(msgline, "status %s.%s %s %s %s %s\n", 
			commafy(t->host->hostname), svcname, colorname(color), timestamp,
			svcname, ( ((color == COL_RED) || (color == COL_YELLOW)) ? "NOT ok" : "ok"));
		addtostatus(msgline);

		if (t->host->dnserror) {
			sprintf(msgline, "\nUnable to resolve hostname %s\n\n", t->host->hostname);
		}
		else {
			sprintf(msgline, "\nService %s on %s is ", svcname, t->host->hostname);
			switch (color) {
			  case COL_GREEN: 
				  strcat(msgline, "OK ");
				  strcat(msgline, (t->reverse ? "(down)" : "(up)"));
				  strcat(msgline, "\n");
				  break;

			  case COL_RED:
			  case COL_YELLOW:
				  strcat(msgline, "not OK ");
				  strcat(msgline, (t->reverse ? "(up)" : "(down)"));
				  strcat(msgline, "\n");
				  break;

			  case COL_CLEAR:
				  strcat(msgline, "OK\n");
				  if (service == pingtest) {
					  if (t->host->noping) {
						  strcat(msgline, "Ping check disabled (noping)\n");
					  }
					  if (t->host->dialup) {
						  strcat(msgline, "Dialup host\n");
					  }
					  /* "clear" due to badconn: no extra text */
				  }
				  else {
					  /* Non-ping test clear: Dialup test or failed ping */
					  strcat(msgline, "Dialup host/service, or ping check failed\n");
				  }
				  break;

			  case COL_BLUE:
				  strcat(msgline, "OK\n");
				  strcat(msgline, "Host currently not monitored due to SLA setting.\n");
				  break;
			}
			strcat(msgline, "\n");
		}
		addtostatus(msgline);

		if ((service == pingtest) && t->host->downcount) {
			sprintf(msgline, "\n<p>System unreachable for %d poll periods (%lu seconds)\n</p>",
				t->host->downcount, (time(NULL) - t->host->downstart));
			addtostatus(msgline);
		}

		if (t->banner) {
			addtostatus("\n"); addtostatus(t->banner); addtostatus("\n");
		}
		if (t->testresult) {
			sprintf(msgline, "\nSeconds: %ld.%03ld\n", 
				t->testresult->duration.tv_sec, t->testresult->duration.tv_usec / 1000);
			addtostatus(msgline);
		}
		addtostatus("\n\n");
		finish_status();
	}
}

int main(int argc, char *argv[])
{
	service_t *s;
	testedhost_t *h;
	testitem_t *t;
	int argi;
	int timeout=0;
	int concurrency=0;
	char *pingcolumn = NULL;

	for (argi=1; (argi < argc); argi++) {
		if      (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strcmp(argv[argi], "--version") == 0) {
			printf("bbtest-net version %s\n", VERSION);
			return 0;
		}
		else if (strncmp(argv[argi], "--timeout=", 10) == 0) {
			char *p = strchr(argv[argi], '=');
			p++; timeout = atoi(p);
		}
		else if (strncmp(argv[argi], "--concurrency=", 14) == 0) {
			char *p = strchr(argv[argi], '=');
			p++; concurrency = atoi(p);
		}
		else if (strncmp(argv[argi], "--dns=", 6) == 0) {
			char *p = strchr(argv[argi], '=');
			p++;
			if (strcmp(p, "only") == 0)      dnsmethod = DNS_ONLY;
			else if (strcmp(p, "ip") == 0)   dnsmethod = IP_ONLY;
			else                             dnsmethod = DNS_THEN_IP;
		}
		else if (strncmp(argv[argi], "--ping", 6) == 0) {
			char *p = strchr(argv[argi], '=');
			if (p) {
				p++; pingcolumn = p;
			}
			else pingcolumn = "conn";
		}
		else if (strcmp(argv[argi], "--noping") == 0) {
			pingcolumn = NULL;
		}
	}

	init_timestamp();

	load_services();
	add_service("dns", getportnumber("domain"), 0, TOOL_NSLOOKUP);
	add_service("dig", getportnumber("domain"), 0, TOOL_DIG);
	add_service("ntp", getportnumber("ntp"), 0, TOOL_NTP);
	if (pingcolumn) pingtest = add_service(pingcolumn, 0, 0, TOOL_FPING);

	for (s = svchead; (s); s = s->next) {
		dprintf("Service %s port %d\n", s->testname, s->portnum);
	}

	load_tests();
	for (h = testhosthead; (h); h = h->next) {
		dprintf("Host %s, dnserror=%d, ip %s, dialup=%d testip=%d\n", 
			h->hostname, h->dnserror, h->ip, h->dialup, h->testip);
	}

	combo_start();
	dprintf("\nTest services\n");

	/* Ping checks first */
	if (pingtest && pingtest->items) {
		run_fping_service(pingtest); 
		send_results(pingtest);
	}

	/* First run the standard TCP/IP tests */
	for (s = svchead; (s); s = s->next) {
		if ((s->items) && (s->toolid == TOOL_CONTEST)) {
			for (t = s->items; (t); t = t->next) {
				if (!t->host->dnserror) {
					t->testresult = add_test(t->host->ip, s->portnum, s->testname, t->silenttest);
				}
				else {
					t->testresult = NULL;
				}
			}
		}
	}
	do_conn(timeout, concurrency);
	if (debug) show_conn_res();
	for (s = svchead; (s); s = s->next) {
		if ((s->items) && (s->toolid == TOOL_CONTEST)) {
			for (t = s->items; (t); t = t->next) { 
				/*
				 * If the test fails due to DNS error, t->testresult is NULL
				 */
				if (t->testresult) {
					t->open = t->testresult->open;
					t->banner = t->testresult->banner;
				}
				else {
					t->open = 0;
					t->banner = NULL;
				}
			}
			send_results(s);
		}
	}

	/* dns, dig, ntp tests */
	for (s = svchead; (s); s = s->next) {
		if (s->items) {
			switch(s->toolid) {
				case TOOL_NSLOOKUP:
					run_nslookup_service(s); send_results(s);
					break;
				case TOOL_DIG:
					run_dig_service(s); send_results(s);
					break;
				case TOOL_NTP:
					run_ntp_service(s); send_results(s);
					break;
				case TOOL_FPING:
					/* Already done */
					break;
			}
		}
	}

	combo_end();

	return 0;
}

