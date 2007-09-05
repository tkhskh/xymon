/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* This module handles "paging" messages.                                     */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@hswn.dk>                         */
/* Copyright (C) 2007 Rich Smrcina                                            */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char paging_rcsid[] = "$Id: do_paging.c,v 1.2 2007-07-24 08:45:01 henrik Exp $";

static char *paging_params[] = { "DS:paging:GAUGE:600:0:U", NULL };
static char *paging_tpl      = NULL;

int do_paging_rrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	char *p1, *p2, *p3, *pr;
	char *fn = NULL;
	int pagerate;

	p1=(strstr(msg, "z/VM"));
	p2=(strstr(msg, "z/VSE"));

	if (p1 || p2) {
		pr=(strstr(msg, "Rate"));
		if (pr) {
			pr += 5;
			sscanf(pr, "%d per", &pagerate);
			setupfn("paging.rrd", fn);

			sprintf(rrdvalues, "%d:%d", (int)tstamp, pagerate);
			create_and_update_rrd(hostname, testname, paging_params, paging_tpl);
		}

	}
	return 0;
}
