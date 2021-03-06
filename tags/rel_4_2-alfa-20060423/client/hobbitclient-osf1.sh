#!/bin/sh
#----------------------------------------------------------------------------#
# OSF1 client for Hobbit                                                     #
#                                                                            #
# Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id: hobbitclient-osf1.sh,v 1.8 2006-04-20 07:07:27 henrik Exp $

echo "[date]"
date
echo "[uname]"
uname -a
echo "[uptime]"
uptime
echo "[who]"
who
echo "[memory]"
vmstat -P
echo "[swap]"
swapon -s
echo "[df]"
df -t noprocfs | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}'
echo "[ifconfig]"
ifconfig -a
echo "[route]"
cat /etc/routes
echo "[netstat]"
netstat -s
echo "[ports]"
netstat -an|grep "^tcp"
echo "[ps]"
ps -ef
echo "[top]"
top -b -n 1 
# vmstat
nohup sh -c "vmstat 300 2 1>$BBTMP/hobbit_vmstat.$$ 2>&1; mv $BBTMP/hobbit_vmstat.$$ $BBTMP/hobbit_vmstat" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $BBTMP/hobbit_vmstat; then echo "[vmstat]"; cat $BBTMP/hobbit_vmstat; rm -f $BBTMP/hobbit_vmstat; fi
# logfiles
$BBHOME/bin/logfetch $BBTMP/logfetch.cfg $BBTMP/logfetch.status

exit

