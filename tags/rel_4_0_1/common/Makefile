# bbgen - common tools
#

PROGRAMS = bbhostgrep bbhostshow bbdigest bb hobbitlaunch bbcmd

HOSTGREPOBJS = bbhostgrep.o
HOSTSHOWOBJS = bbhostshow.o
DIGESTOBJS = bbdigest.o
MSGOBJS = sendmsg-std.o
LAUNCHOBJS = hobbitlaunch.o
CMDOBJS = bbcmd.o

all: $(PROGRAMS)


bbhostgrep: $(HOSTGREPOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(HOSTGREPOBJS) ../lib/libbbgen.a

bbhostshow: $(HOSTSHOWOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(HOSTSHOWOBJS) ../lib/libbbgen.a

bbdigest: $(DIGESTOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(DIGESTOBJS) ../lib/libbbgen.a $(SSLLIBS) $(NETLIBS)

bb: $(MSGOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(MSGOBJS) ../lib/libbbgen.a $(NETLIBS)

hobbitlaunch: $(LAUNCHOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(LAUNCHOBJS) ../lib/libbbgen.a

bbcmd: $(CMDOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(CMDOBJS) ../lib/libbbgen.a

sendmsg-std.o: ../lib/sendmsg.c
	$(CC) $(CFLAGS) -DSTANDALONE -c -o $@ ../lib/sendmsg.c


################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a *~ $(PROGRAMS) $(CGISCRIPTS)

install: install-bin install-man

install-bin: $(PROGRAMS)
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man7 $(INSTALLROOT)$(MANROOT)/man8
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/
	cp -fp *.7 $(INSTALLROOT)$(MANROOT)/man7/
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/

