/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: do_rrd.c,v 1.35 2006-06-21 08:43:58 henrik Exp $";

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>

#include <rrd.h>
#include <pcre.h>

#include "libbbgen.h"

#include "do_rrd.h"

char *rrddir = NULL;
static char *exthandler = NULL;
static char **extids = NULL;

static char rrdvalues[MAX_LINE_LEN];
static char rra1[] = "RRA:AVERAGE:0.5:1:576";
static char rra2[] = "RRA:AVERAGE:0.5:6:576";
static char rra3[] = "RRA:AVERAGE:0.5:24:576";
static char rra4[] = "RRA:AVERAGE:0.5:288:576";

static char *senderip = NULL;
static char rrdfn[PATH_MAX];	/* This one used by the modules */
static char filedir[PATH_MAX];	/* This one used here */

void setup_exthandler(char *handlerpath, char *ids)
{
	char *p;
	int idcount = 0;

	MEMDEFINE(rrdvalues);

	exthandler = strdup(handlerpath);
	idcount=1; p = ids; while ((p = strchr(p, ',')) != NULL) { p++; idcount++; }
	extids = (char **)malloc((idcount+1)*(sizeof(char *)));
	idcount = 0;
	p = strtok(ids, ",");
	while (p) {
		extids[idcount++] = strdup(p);
		p = strtok(NULL, ",");
	}
	extids[idcount] = NULL;

	MEMUNDEFINE(rrdvalues);
}

static char *setup_template(char *params[])
{
	int i;
	char *result = NULL;

	for (i = 0; (params[i]); i++) {
		if (strncasecmp(params[i], "DS:", 3) == 0) {
			char *pname, *pend;

			pname = params[i] + 3;
			pend = strchr(pname, ':');
			if (pend) {
				int plen = (pend - pname);

				if (result == NULL) {
					result = (char *)malloc(plen + 1);
					*result = '\0';
				}
				else {
					/* Hackish way of getting the colon delimiter */
					pname--; plen++;
					result = (char *)realloc(result, strlen(result) + plen + 1);
				}
				strncat(result, pname, plen);
			}
		}
	}

	return result;
}

static void setupfn(char *format, char *param)
{
	snprintf(rrdfn, sizeof(rrdfn)-1, format, param);
	rrdfn[sizeof(rrdfn)-1] = '\0';
}

static int create_and_update_rrd(char *hostname, char *fn, char *creparams[], char *template)
{
	struct stat st;
	int pcount, result;
	char *tplstr = NULL;
	char *updparams[] = { "rrdupdate", filedir, "-t", NULL, rrdvalues, NULL };

	/* ISO C90: parameters cannot be used as initializers */
	updparams[3] = template;

	if ((fn == NULL) || (strlen(fn) == 0)) {
		errprintf("RRD update for no file\n");
		return -1;
	}

	MEMDEFINE(rrdvalues);
	MEMDEFINE(filedir);

	sprintf(filedir, "%s/%s", rrddir, hostname);
	if (stat(filedir, &st) == -1) {
		if (mkdir(filedir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH) == -1) {
			errprintf("Cannot create rrd directory %s : %s\n", filedir, strerror(errno));
			MEMUNDEFINE(filedir);
			MEMUNDEFINE(rrdvalues);
			return -1;
		}
	}
	/* Watch out here - "fn" may be very large. */
	snprintf(filedir, sizeof(filedir)-1, "%s/%s/%s", rrddir, hostname, fn);
	filedir[sizeof(filedir)-1] = '\0'; /* Make sure it is null terminated */
	creparams[1] = filedir;	/* Icky */

	if (stat(filedir, &st) == -1) {
		dprintf("Creating rrd %s\n", filedir);

		for (pcount = 0; (creparams[pcount]); pcount++);
		if (debug) {
			int i;

			for (i = 0; (creparams[i]); i++) {
				dprintf("RRD create param %02d: '%s'\n", i, creparams[i]);
			}
		}

		/*
		 * Ugly! RRDtool uses getopt() for parameter parsing, so
		 * we MUST reset this before every call.
		 */
		optind = opterr = 0; rrd_clear_error();
		result = rrd_create(pcount, creparams);
		if (result != 0) {
			errprintf("RRD error creating %s: %s\n", filedir, rrd_get_error());
			MEMUNDEFINE(filedir);
			MEMUNDEFINE(rrdvalues);
			return 1;
		}
	}

	if (template) {
		updparams[3] = template;
	}
	else {
		tplstr = setup_template(creparams);
		updparams[3] = tplstr;
	}

	for (pcount = 0; (updparams[pcount]); pcount++);
	if (debug) {
		int i;

		for (i = 0; (updparams[i]); i++) {
			dprintf("RRD update param %02d: '%s'\n", i, updparams[i]);
		}
	}

	/*
	 * Ugly! RRDtool uses getopt() for parameter parsing, so
	 * we MUST reset this before every call.
	 */
	optind = opterr = 0; rrd_clear_error();
	result = rrd_update(pcount, updparams);
	if (tplstr) xfree(tplstr); 

	if (result != 0) {
		errprintf("RRD error updating %s from %s: %s\n", 
			  filedir, (senderip ? senderip : "unknown"), 
			  rrd_get_error());
		MEMUNDEFINE(filedir);
		MEMUNDEFINE(rrdvalues);
		return 2;
	}

	MEMUNDEFINE(filedir);
	MEMUNDEFINE(rrdvalues);

	return 0;
}

static int rrddatasets(char *hostname, char *fn, char ***dsnames)
{
	struct stat st;

	int result;
	char *fetch_params[] = { "rrdfetch", filedir, "AVERAGE", "-s", "-30m", NULL };
	time_t starttime, endtime;
	unsigned long steptime, dscount;
	rrd_value_t *rrddata;

	snprintf(filedir, sizeof(filedir)-1, "%s/%s/%s", rrddir, hostname, fn);
	filedir[sizeof(filedir)-1] = '\0';
	if (stat(filedir, &st) == -1) return 0;

	optind = opterr = 0; rrd_clear_error();
	result = rrd_fetch(5, fetch_params, &starttime, &endtime, &steptime, &dscount, dsnames, &rrddata);
	if (result == -1) {
		errprintf("Error while retrieving RRD dataset names from %s: %s\n",
			  filedir, rrd_get_error());
		return 0;
	}

	free(rrddata);	/* No use for the actual data */
	return dscount;
}


/* Include all of the sub-modules. */
#include "rrd/do_bbgen.c"
#include "rrd/do_bbtest.c"
#include "rrd/do_bbproxy.c"
#include "rrd/do_hobbitd.c"
#include "rrd/do_citrix.c"
#include "rrd/do_ntpstat.c"

#include "rrd/do_memory.c"	/* Must go before do_la.c */
#include "rrd/do_la.c"
#include "rrd/do_disk.c"
#include "rrd/do_netstat.c"
#include "rrd/do_vmstat.c"
#include "rrd/do_iostat.c"
#include "rrd/do_ifstat.c"

#include "rrd/do_apache.c"
#include "rrd/do_bind.c"
#include "rrd/do_sendmail.c"
#include "rrd/do_mailq.c"
#include "rrd/do_iishealth.c"
#include "rrd/do_temperature.c"

#include "rrd/do_net.c"

#include "rrd/do_ncv.c"
#include "rrd/do_external.c"
#include "rrd/do_filesizes.c"
#include "rrd/do_counts.c"

#ifdef USE_BEA2
#include "rrd/do_bea2.c"
#else
#include "rrd/do_bea.c"
#endif

#ifdef DO_ORCA
#include "rrd/do_orca.c"
#endif

void update_rrd(char *hostname, char *testname, char *msg, time_t tstamp, char *sender, hobbitrrd_t *ldef)
{
	int res = 0;
	char *id;

	MEMDEFINE(rrdvalues);

	if (ldef) id = ldef->hobbitrrdname; else id = testname;
	senderip = sender;

	if      (strcmp(id, "bbgen") == 0)       res = do_bbgen_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "bbtest") == 0)      res = do_bbtest_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "bbproxy") == 0)     res = do_bbproxy_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "hobbitd") == 0)     res = do_hobbitd_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "citrix") == 0)      res = do_citrix_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "ntpstat") == 0)     res = do_ntpstat_rrd(hostname, testname, msg, tstamp);

	else if (strcmp(id, "la") == 0)          res = do_la_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "disk") == 0)        res = do_disk_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "memory") == 0)      res = do_memory_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "netstat") == 0)     res = do_netstat_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "vmstat") == 0)      res = do_vmstat_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "iostat") == 0)      res = do_iostat_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "ifstat") == 0)      res = do_ifstat_rrd(hostname, testname, msg, tstamp);

	/* These two come from the filerstats2bb.pl script. The reports are in disk-format */
	else if (strcmp(id, "inode") == 0)       res = do_disk_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "qtree") == 0)       res = do_disk_rrd(hostname, testname, msg, tstamp);

	else if (strcmp(id, "apache") == 0)      res = do_apache_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "bind") == 0)        res = do_bind_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "sendmail") == 0)    res = do_sendmail_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "mailq") == 0)       res = do_mailq_rrd(hostname, testname, msg, tstamp);
#ifdef USE_BEA2
	else if (strcmp(id, "bea2") == 0)        res = do_bea_rrd(hostname, testname, msg, tstamp);
#else
	else if (strcmp(id, "bea") == 0)         res = do_bea_rrd(hostname, testname, msg, tstamp);
#endif
	else if (strcmp(id, "iishealth") == 0)   res = do_iishealth_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "temperature") == 0) res = do_temperature_rrd(hostname, testname, msg, tstamp);

	else if (strcmp(id, "ncv") == 0)         res = do_ncv_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "tcp") == 0)         res = do_net_rrd(hostname, testname, msg, tstamp);

	else if (strcmp(id, "filesizes") == 0)   res = do_filesizes_rrd(hostname, testname, msg, tstamp);
	else if (strcmp(id, "proccounts") == 0)  res = do_counts_rrd("processes", hostname, testname, msg, tstamp);
	else if (strcmp(id, "portcounts") == 0)  res = do_counts_rrd("ports", hostname, testname, msg, tstamp);
	else if (strcmp(id, "linecounts") == 0)  res = do_derives_rrd("lines", hostname, testname, msg, tstamp);

#ifdef DO_ORCA
	else if (strcmp(id, "orca") == 0)        res = do_orca_rrd(hostname, testname, msg, tstamp);
#endif

	else if (extids && exthandler) {
		int i;

		for (i=0; (extids[i] && strcmp(extids[i], id)); i++) ;

		if (extids[i]) res = do_external_rrd(hostname, testname, msg, tstamp);
	}

	senderip = NULL;

	MEMUNDEFINE(rrdvalues);
}

