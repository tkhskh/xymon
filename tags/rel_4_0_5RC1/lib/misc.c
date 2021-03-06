/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains miscellaneous routines.                                        */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: misc.c,v 1.38 2005-07-14 21:27:13 henrik Exp $";

#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#if !defined(HPUX)              /* HP-UX has select() and friends in sys/types.h */
#include <sys/select.h>         /* Someday I'll move to GNU Autoconf for this ... */
#endif

#include "libbbgen.h"
#include "version.h"

enum ostype_t get_ostype(char *osname)
{
	char savech;
	enum ostype_t result = OS_UNKNOWN;

	int n = strspn(osname, "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	savech = *(osname+n); *(osname+n) = '\0';

	if      (strcasecmp(osname, "solaris") == 0)     result = OS_SOLARIS;
	else if (strcasecmp(osname, "sunos") == 0)       result = OS_SOLARIS;
	else if (strcasecmp(osname, "hpux") == 0)        result = OS_HPUX;
	else if (strcasecmp(osname, "aix") == 0)         result = OS_AIX;
	else if (strcasecmp(osname, "osf") == 0)         result = OS_OSF;
	else if (strcasecmp(osname, "win32") == 0)       result = OS_WIN32;
	else if (strcasecmp(osname, "freebsd") == 0)     result = OS_FREEBSD;
	else if (strcasecmp(osname, "netbsd") == 0)      result = OS_NETBSD;
	else if (strcasecmp(osname, "openbsd") == 0)     result = OS_OPENBSD;
	else if (strcasecmp(osname, "debian3") == 0)     result = OS_LINUX22;
	else if (strcasecmp(osname, "linux22") == 0)     result = OS_LINUX22;
	else if (strcasecmp(osname, "linux") == 0)       result = OS_LINUX;
	else if (strcasecmp(osname, "redhat") == 0)      result = OS_LINUX;
	else if (strcasecmp(osname, "debian") == 0)      result = OS_LINUX;
	else if (strcasecmp(osname, "suse") == 0)        result = OS_LINUX;
	else if (strcasecmp(osname, "mandrake") == 0)    result = OS_LINUX;
	else if (strcasecmp(osname, "redhatAS") == 0)    result = OS_LINUX;
	else if (strcasecmp(osname, "redhatES") == 0)    result = OS_RHEL3;
	else if (strcasecmp(osname, "rhel3") == 0)       result = OS_RHEL3;
	else if (strcasecmp(osname, "snmp") == 0)        result = OS_SNMP;
	else if (strcasecmp(osname, "snmpnetstat") == 0) result = OS_SNMP;
	else if (strncasecmp(osname, "irix", 4) == 0)    result = OS_IRIX;

	if (result == OS_UNKNOWN) dprintf("Unknown OS: '%s'\n", osname);

	*(osname+n) = savech;
	return result;
}

int hexvalue(unsigned char c)
{
	switch (c) {
	  case '0': return 0;
	  case '1': return 1;
	  case '2': return 2;
	  case '3': return 3;
	  case '4': return 4;
	  case '5': return 5;
	  case '6': return 6;
	  case '7': return 7;
	  case '8': return 8;
	  case '9': return 9;
	  case 'a': return 10;
	  case 'A': return 10;
	  case 'b': return 11;
	  case 'B': return 11;
	  case 'c': return 12;
	  case 'C': return 12;
	  case 'd': return 13;
	  case 'D': return 13;
	  case 'e': return 14;
	  case 'E': return 14;
	  case 'f': return 15;
	  case 'F': return 15;
	}

	return -1;
}

char *commafy(char *hostname)
{
	static char *s = NULL;
	char *p;

	if (s == NULL) {
		s = strdup(hostname);
	}
	else if (strlen(hostname) > strlen(s)) {
		xfree(s);
		s = strdup(hostname);
	}
	else {
		strcpy(s, hostname);
	}

	for (p = strchr(s, '.'); (p); p = strchr(s, '.')) *p = ',';
	return s;
}


char *skipword(char *l)
{
	char *p;

	for (p=l; (*p && (!isspace((int)*p))); p++) ;
	return p;
}


char *skipwhitespace(char *l)
{
	char *p;

	for (p=l; (*p && (isspace((int)*p))); p++) ;
	return p;
}


int argnmatch(char *arg, char *match)
{
	return (strncmp(arg, match, strlen(match)) == 0);
}


void addtobuffer(char **buf, int *bufsz, char *newtext)
{
	if (*buf == NULL) {
		*bufsz = strlen(newtext) + 4096;
		*buf = (char *) malloc(*bufsz);
		**buf = '\0';
	}
	else if ((strlen(*buf) + strlen(newtext) + 1) > *bufsz) {
		*bufsz += strlen(newtext) + 4096;
		*buf = (char *) realloc(*buf, *bufsz);
	}

	strcat(*buf, newtext);
}


char *msg_data(char *msg)
{
	/* Find the start position of the data following the "status host.test " message */
	char *result;

	if (!msg || (*msg == '\0')) return msg;

	result = strchr(msg, '.');              /* Hits the '.' in "host.test" */
	if (!result) {
		dprintf("Msg was not what I expected: '%s'\n", msg);
		return msg;
	}

	result += strcspn(result, " \t\n");     /* Skip anything until we see a space, TAB or NL */
	result += strspn(result, " \t");        /* Skip all whitespace */

	return result;
}

char *gettok(char *s, char *delims)
{
	/*
	 * This works like strtok(), but can handle empty fields.
	 */

	static char *source = NULL;
	static char *whereat = NULL;
	int n;
	char *result;

	if ((delims == NULL) || (*delims == '\0')) return NULL;	/* Sanity check */
	if ((source == NULL) && (s == NULL)) return NULL;	/* Programmer goofed and called us first time with NULL */

	if (s) source = whereat = s;				/* First call */

	if (*whereat == '\0') {
		/* End of string ... clear local state and return NULL */
		source = whereat = NULL;
		return NULL;
	}

	n = strcspn(whereat, delims);
	if (n == 0) {
		/* An empty token */
		whereat++;
		result = "";
	}
	else if (n == strlen(whereat)) {
		/* Last token */
		result = whereat;
		whereat += n;
	}
	else {
		/* Mid-string token - null-teminate the token */
		*(whereat + n) = '\0';
		result = whereat;

		/* Move past this token and the delimiter */
		whereat += (n+1);
	}

	return result;
}

void grok_input(char *l)
{
	/*
	 * This routine sanitizes an input line, stripping off whitespace,
	 * removing comments and un-escaping \-escapes and quotes.
	 */
	char *p, *outp;
	int inquote, inhyphen;

	p = strchr(l, '\n'); if (p) *p = '\0';

	/* Remove quotes, comments and leading whitespace */
	p = l + strspn(l, " \t"); outp = l; inquote = inhyphen = 0;
	while (*p) {
		if (*p == '\\') {
			*outp = *(p+1);
			outp++; p += 2;
		}
		else if (*p == '"') {
			inquote = (1 - inquote);
			p++;
		}
		else if (*p == '\'') {
			inhyphen = (1 - inhyphen);
			p++;
		}
		else if ((*p == '#') && !inquote && !inhyphen) {
			*p = '\0';
		}
		else {
			if (outp != p) *outp = *p;
			outp++; p++;
		}
	}

	/* Remove trailing whitespace */
	while ((outp > l) && (isspace((int) *(outp-1)))) outp--;
	*outp = '\0';
}

unsigned int IPtou32(int ip1, int ip2, int ip3, int ip4)
{
	return ((ip1 << 24) | (ip2 << 16) | (ip3 << 8) | (ip4));
}

char *u32toIP(unsigned int ip32)
{
	int ip1, ip2, ip3, ip4;
	static char *result = NULL;

	if (result == NULL) result = (char *)malloc(16);

	ip1 = ((ip32 >> 24) & 0xFF);
	ip2 = ((ip32 >> 16) & 0xFF);
	ip3 = ((ip32 >> 8) & 0xFF);
	ip4 = (ip32 & 0xFF);

	sprintf(result, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
	return result;
}

const char *textornull(const char *text)
{
	return (text ? text : "(NULL)");
}


int get_fqdn(void)
{
	/* Get FQDN setting */
	getenv_default("FQDN", "TRUE", NULL);
	return (strcmp(xgetenv("FQDN"), "TRUE") == 0);
}

int generate_static(void)
{
	getenv_default("BBLOGSTATUS", "STATIC", NULL);
	return (strcmp(xgetenv("BBLOGSTATUS"), "STATIC") == 0);
}


int run_command(char *cmd, char *errortext, char **banner, int *bannerbytes, int showcmd, int timeout)
{
	int	result;
	int	bannersize = 0;
	char	l[1024];
	int	pfd[2];
	pid_t	childpid; 

	MEMDEFINE(l);

	result = 0;
	if (banner) { 
		bannersize = 4096;
		*banner = (char *) malloc(bannersize); 
		**banner = '\0';
		if (showcmd) sprintf(*banner, "Command: %s\n\n", cmd); 
	}

	/* Adapted from Stevens' popen()/pclose() example */
	if (pipe(pfd) > 0) {
		errprintf("Could not create pipe: %s\n", strerror(errno));
		MEMUNDEFINE(l);
		return -1;
	}

	if ((childpid = fork()) < 0) {
		errprintf("Could not fork child process: %s\n", strerror(errno));
		MEMUNDEFINE(l);
		return -1;
	}

	if (childpid == 0) {
		/* The child runs here */
		close(pfd[0]);
		if (pfd[1] != STDOUT_FILENO) {
			dup2(pfd[1], STDOUT_FILENO);
			dup2(pfd[1], STDERR_FILENO);
			close(pfd[1]);
		}

		execl("/bin/sh", "sh", "-c", cmd, NULL);
		exit(127);
	}
	else {
		/* The parent runs here */
		int done = 0, didterm = 0, n;
		struct timeval tmo, timestamp, cutoff;
		struct timezone tz;

		close(pfd[1]);

		gettimeofday(&cutoff, &tz);
		cutoff.tv_sec += timeout;

		while (!done) {
			fd_set readfds;

			gettimeofday(&timestamp, &tz);
			tvdiff(&timestamp, &cutoff, &tmo);
			if ((tmo.tv_sec < 0) || (tmo.tv_usec < 0)) {
				/* Timeout already happened */
				n = 0;
			}
			else {
				FD_ZERO(&readfds);
				FD_SET(pfd[0], &readfds);
				n = select(pfd[0]+1, &readfds, NULL, NULL, &tmo);
			}

			if (n == -1) {
				errprintf("select() error: %s\n", strerror(errno));
				result = -1;
				done = 1;
			}
			else if (n == 0) {
				/* Timeout */
				errprintf("Timeout waiting for data from child, killing it\n");
				kill(childpid, (didterm ? SIGKILL : SIGTERM));
				if (!didterm) didterm = 1; else { done = 1; result = -1; }
			}
			else if (FD_ISSET(pfd[0], &readfds)) {
				n = read(pfd[0], l, sizeof(l)-1);
				l[n] = '\0';

				if (n == 0) {
					done = 1;
				}
				else {
					if (banner) {
						if ((strlen(l) + strlen(*banner)) > bannersize) {
							bannersize += strlen(l) + 4096;
							*banner = (char *) realloc(*banner, bannersize);
						}
						strcat(*banner, l);
					}
					if (errortext && (strstr(l, errortext) != NULL)) result = 1;
				}
			}
		}

		close(pfd[0]);

		result = 0;
		while ((result == 0) && (waitpid(childpid, &result, 0) < 0)) {
			if (errno != EINTR) {
				errprintf("Error picking up child exit status: %s\n", strerror(errno));
				result = -1;
			}
		}

		if (WIFEXITED(result)) {
			result = WEXITSTATUS(result);
		}
		else if (WIFSIGNALED(result)) {
			errprintf("Child process terminated with signal %d\n", WTERMSIG(result));
			result = -1;
		}
	}

	if (bannerbytes) *bannerbytes = strlen(*banner);

	MEMUNDEFINE(l);
	return result;
}


void do_bbext(FILE *output, char *extenv, char *family)
{
	/*
	 * Extension scripts. These are ad-hoc, and implemented as a
	 * simple pipe. So we do a fork here ...
	 */

	char *bbexts, *p;
	FILE *inpipe;
	char extfn[PATH_MAX];
	char buf[4096];

	p = xgetenv(extenv);
	if (p == NULL) {
		/* No extension */
		return;
	}

	MEMDEFINE(extfn);
	MEMDEFINE(buf);

	bbexts = strdup(p);
	p = strtok(bbexts, "\t ");

	while (p) {
		/* Dont redo the eventlog or acklog things */
		if ((strcmp(p, "eventlog.sh") != 0) &&
		    (strcmp(p, "acklog.sh") != 0)) {
			sprintf(extfn, "%s/ext/%s/%s", xgetenv("BBHOME"), family, p);
			inpipe = popen(extfn, "r");
			if (inpipe) {
				while (fgets(buf, sizeof(buf), inpipe)) 
					fputs(buf, output);
				pclose(inpipe);
			}
		}
		p = strtok(NULL, "\t ");
	}

	xfree(bbexts);

	MEMUNDEFINE(extfn);
	MEMUNDEFINE(buf);
}

char **setup_commandargs(char *cmdline, char **cmd)
{
	/*
	 * Good grief - argument parsing is complex!
	 *
	 * This routine takes a command-line, picks out any environment settings
	 * that are in the commandline, and splits up the remainder into the
	 * actual command to run, and the arguments.
	 *
	 * It handles quotes, hyphens and escapes.
	 */

	char **cmdargs;
	char *cmdcp, *barg, *earg, *eqchar, *envsetting;
	int argi, argsz;
	int argdone, inquote, inhyphen;
	char savech;

	argsz = 1; cmdargs = (char **) malloc((1+argsz)*sizeof(char *)); argi = 0;
	cmdcp = strdup(expand_env(cmdline));

	barg = cmdcp;
	do {
		earg = barg; argdone = 0; inquote = inhyphen = 0;
		while (*earg && !argdone) {
			if (!inquote && !inhyphen) {
				argdone = isspace((int)*earg);
			}

			if ((*earg == '"') && !inhyphen) inquote = (1 - inquote);
			if ((*earg == '\'') && !inquote) inhyphen = (1 - inhyphen);
			if (!argdone) earg++;
		}
		savech = *earg;
		*earg = '\0';

		grok_input(barg);
		eqchar = strchr(barg, '=');
		if (eqchar && (eqchar == (barg + strspn(barg, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")))) {
			/* It's an environment definition */
			dprintf("Setting environment: %s\n", barg);
			envsetting = strdup(barg);
			putenv(envsetting);
		}
		else {
			if (argi == argsz) {
				argsz++; cmdargs = (char **) realloc(cmdargs, (1+argsz)*sizeof(char *));
			}
			cmdargs[argi++] = strdup(barg);
		}

		*earg = savech;
		barg = earg + strspn(earg, " \t\n");
	} while (*barg);
	cmdargs[argi] = NULL;

	xfree(cmdcp);

	*cmd = cmdargs[0];
	return cmdargs;
}

unsigned long long my_atoll(char *s)
{
	/*
	 * Since atoll may not exist, we do our own
	 */
	unsigned long long result = 0;
	char *p;

	p = s + strspn(s, " \t");
	while (*p && isdigit((int)*p)) {
		result = (result * 10) + (*p - '0');
		p++;
	}

	return result;
}

int checkalert(char *alertlist, char *testname)
{
	char *alist, *aname;
	int result;

	if (!alertlist) return 0;

	alist = (char *) malloc(strlen(alertlist) + 3);
	sprintf(alist, ",%s,", alertlist);
	aname = (char *) malloc(strlen(testname) + 3);
	sprintf(aname, ",%s,", testname);

	result = (strstr(alist, aname) != NULL);

	xfree(aname); xfree(alist);
	return result;
}

