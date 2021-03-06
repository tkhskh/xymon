/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __DO_LARRD_H__
#define __DO_LARRD_H__

#include <time.h>

#include "libbbgen.h"

extern char *rrddir;
extern void setup_exthandler(char *handlerpath, char *ids);
extern void update_larrd(char *hostname, char *testname, char *restofmsg, time_t tstamp, char *sender, larrdrrd_t *ldef);

#endif

