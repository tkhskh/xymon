#!/bin/sh
#
#----------------------------------------------------------------------------#
# HP-UX client for Hobbit                                                    #
#                                                                            #
# Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id: hobbitclient-hp-ux.sh,v 1.4 2005-07-24 11:32:51 henrik Exp $

echo "[date]"
date
echo "[uname]"
uname -a
echo "[uptime]"
uptime
echo "[who]"
who
echo "[df]"
df -Pk
echo "[memory]"
echo "Total:`bb-hp-memsz -p`"
echo "Free:`bb-hp-memsz -f`"
echo "[swapinfo]"
/usr/sbin/swapinfo -tm
echo "[netstat]"
netstat -s
echo "[ps]"
ps -ef
echo "[top]"
top -d 20
# vmstat
nohup sh -c "vmstat 300 2 1>$BBTMP/hobbit_vmstat.$$ 2>&1; mv $BBTMP/hobbit_vmstat.$$ $BBTMP/hobbit_vmstat" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $BBTMP/hobbit_vmstat; then echo "[vmstat]"; cat $BBTMP/hobbit_vmstat; rm -f $BBTMP/hobbit_vmstat; fi

exit

