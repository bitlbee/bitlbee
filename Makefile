###########################
## Makefile for BitlBee  ##
##                       ##
## Copyright 2002 Lintux ##
###########################

### DEFINITIONS

-include Makefile.settings

# Program variables
#objects = bitlbee.o chat.o dcc.o help.o ipc.o irc.o irc_commands.o nick.o query.o root_commands.o set.o storage.o $(STORAGE_OBJS)
objects = bitlbee.o help.o ipc.o irc.o irc_im.o irc_channel.o irc_commands.o irc_send.o irc_user.o nick.o root_commands.o set.o
headers = account.h bitlbee.h commands.h conf.h config.h help.h ipc.h irc.h log.h nick.h query.h set.h sock.h storage.h user.h lib/events.h lib/ftutil.h lib/http_client.h lib/ini.h lib/md5.h lib/misc.h lib/proxy.h lib/sha1.h lib/ssl_client.h lib/url.h protocols/ft.h protocols/nogaim.h
subdirs = lib protocols

ifeq ($(TARGET),i586-mingw32msvc)
objects += win32.o
LFLAGS+=-lws2_32
EFLAGS+=-lsecur32
OUTFILE=bitlbee.exe
else
objects += unix.o conf.o log.o
OUTFILE=bitlbee
endif

# Expansion of variables
subdirobjs = $(foreach dir,$(subdirs),$(dir)/$(dir).o)
CFLAGS += -Wall

all: $(OUTFILE)
	$(MAKE) -C doc

uninstall: uninstall-bin uninstall-doc 
	@echo -e '\nmake uninstall does not remove files in '$(DESTDIR)$(ETCDIR)', you can use make uninstall-etc to do that.\n'

install: install-bin install-doc
	@if ! [ -d $(DESTDIR)$(CONFIG) ]; then echo -e '\nThe configuration directory $(DESTDIR)$(CONFIG) does not exist yet, don'\''t forget to create it!'; fi
	@if ! [ -e $(DESTDIR)$(ETCDIR)/bitlbee.conf ]; then echo -e '\nNo files are installed in '$(DESTDIR)$(ETCDIR)' by make install. Run make install-etc to do that.'; fi
	@echo

.PHONY:   install   install-bin   install-etc   install-doc \
        uninstall uninstall-bin uninstall-etc uninstall-doc \
        all clean distclean tar $(subdirs)

Makefile.settings:
	@echo
	@echo Run ./configure to create Makefile.settings, then rerun make
	@echo

clean: $(subdirs)
	rm -f *.o $(OUTFILE) core utils/bitlbeed
	$(MAKE) -C tests clean

distclean: clean $(subdirs)
	rm -f Makefile.settings config.h bitlbee.pc
	find . -name 'DEADJOE' -o -name '*.orig' -o -name '*.rej' -o -name '*~' -exec rm -f {} \;
	$(MAKE) -C tests distclean

check: all
	$(MAKE) -C tests

gcov: check
	gcov *.c

lcov: check
	lcov --directory . --capture --output-file bitlbee.info
	genhtml -o coverage bitlbee.info

install-doc:
	$(MAKE) -C doc install

uninstall-doc:
	$(MAKE) -C doc uninstall

install-bin:
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 0755 $(OUTFILE) $(DESTDIR)$(BINDIR)/$(OUTFILE)

uninstall-bin:
	rm -f $(DESTDIR)$(BINDIR)/$(OUTFILE)

install-dev:
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	install -m 0644 $(headers) $(DESTDIR)$(INCLUDEDIR)
	mkdir -p $(DESTDIR)$(PCDIR)
	install -m 0644 bitlbee.pc $(DESTDIR)$(PCDIR)

uninstall-dev:
	rm -f $(foreach hdr,$(headers),$(DESTDIR)$(INCLUDEDIR)/$(hdr))
	-rmdir $(DESTDIR)$(INCLUDEDIR)
	rm -f $(DESTDIR)$(PCDIR)/bitlbee.pc

install-etc:
	mkdir -p $(DESTDIR)$(ETCDIR)
	install -m 0644 motd.txt $(DESTDIR)$(ETCDIR)/motd.txt
	install -m 0644 bitlbee.conf $(DESTDIR)$(ETCDIR)/bitlbee.conf

uninstall-etc:
	rm -f $(DESTDIR)$(ETCDIR)/motd.txt
	rm -f $(DESTDIR)$(ETCDIR)/bitlbee.conf
	-rmdir $(DESTDIR)$(ETCDIR)

tar:
	fakeroot debian/rules clean || make distclean
	x=$$(basename $$(pwd)); \
	cd ..; \
	tar czf $$x.tar.gz --exclude=debian --exclude=.bzr* $$x

$(subdirs):
	@$(MAKE) -C $@ $(MAKECMDGOALS)

$(objects): %.o: %.c
	@echo '*' Compiling $<
	@$(CC) -c $(CFLAGS) $< -o $@

$(objects): Makefile Makefile.settings config.h

$(OUTFILE): $(objects) $(subdirs)
	@echo '*' Linking $(OUTFILE)
	@$(CC) $(objects) $(subdirobjs) -o $(OUTFILE) $(LFLAGS) $(EFLAGS)
ifndef DEBUG
	@echo '*' Stripping $(OUTFILE)
	@-$(STRIP) $(OUTFILE)
endif

ctags: 
	ctags `find . -name "*.c"` `find . -name "*.h"`
