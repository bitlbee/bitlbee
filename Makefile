###########################
## Makefile for BitlBee  ##
##                       ##
## Copyright 2002 Lintux ##
###########################

### DEFINITIONS

-include Makefile.settings

# Program variables
objects = bitlbee.o dcc.o help.o ipc.o irc.o irc_im.o irc_channel.o irc_commands.o irc_send.o irc_user.o irc_util.o nick.o $(OTR_BI) query.o root_commands.o set.o storage.o $(STORAGE_OBJS) unix.o conf.o log.o
headers = $(wildcard $(_SRCDIR_)*.h $(_SRCDIR_)lib/*.h $(_SRCDIR_)protocols/*.h)
subdirs = lib protocols

OUTFILE = bitlbee

# Expansion of variables
subdirobjs = $(foreach dir,$(subdirs),$(dir)/$(dir).o)

all: $(OUTFILE) $(OTR_PI) $(SKYPE_PI) doc systemd

doc:
ifdef DOC
	$(MAKE) -C doc
endif

uninstall: uninstall-bin uninstall-doc 
	@echo -e '\nmake uninstall does not remove files in '$(DESTDIR)$(ETCDIR)', you can use make uninstall-etc to do that.\n'

install: install-bin install-doc install-plugins
	@echo
	@echo Installed successfully
	@echo
	@if ! [ -d $(DESTDIR)$(CONFIG) ]; then echo -e '\nThe configuration directory $(DESTDIR)$(CONFIG) does not exist yet, don'\''t forget to create it!'; fi
	@if ! [ -e $(DESTDIR)$(ETCDIR)/bitlbee.conf ]; then echo -e '\nNo files are installed in '$(DESTDIR)$(ETCDIR)' by make install. Run make install-etc to do that.'; fi
ifdef SYSTEMDSYSTEMUNITDIR
	@echo If you want to start BitlBee using systemd, try \"make install-systemd\".
endif
	@echo To be able to compile third party plugins, run \"make install-dev\"
	@echo

.PHONY:   install   install-bin   install-etc   install-doc install-plugins install-systemd install-dev \
        uninstall uninstall-bin uninstall-etc uninstall-doc uninstall-etc \
        all clean distclean tar $(subdirs) doc

Makefile.settings:
	@echo
	@echo Run ./configure to create Makefile.settings, then rerun make
	@echo

clean: $(subdirs)
	rm -f *.o $(OUTFILE) core utils/bitlbeed init/bitlbee*.service
	$(MAKE) -C tests clean
ifdef SKYPE_PI
	$(MAKE) -C protocols/skype clean
endif

distclean: clean $(subdirs)
	rm -rf .depend
	rm -f Makefile.settings config.h bitlbee.pc
	find . -name 'DEADJOE' -o -name '*.orig' -o -name '*.rej' -o -name '*~' -exec rm -f {} \;
	@# May still be present in dirs of disabled protocols.
	-find . -name .depend -exec rm -rf {} \;
	$(MAKE) -C tests distclean

check: all
	$(MAKE) -C tests

gcov: check
	gcov *.c

lcov: check
	lcov --directory . --capture --output-file bitlbee.info
	genhtml -o coverage bitlbee.info

install-doc:
ifdef DOC
	$(MAKE) -C doc install
endif
ifdef SKYPE_PI
	$(MAKE) -C protocols/skype install-doc
endif

uninstall-doc:
ifdef DOC
	$(MAKE) -C doc uninstall
endif
ifdef SKYPE_PI
	$(MAKE) -C protocols/skype uninstall-doc
endif

install-bin:
	mkdir -p $(DESTDIR)$(SBINDIR)
	$(INSTALL) -m 0755 $(OUTFILE) $(DESTDIR)$(SBINDIR)/$(OUTFILE)

uninstall-bin:
	rm -f $(DESTDIR)$(SBINDIR)/$(OUTFILE)

install-dev:
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	$(INSTALL) -m 0644 config.h $(DESTDIR)$(INCLUDEDIR)
	for i in $(headers); do $(INSTALL) -m 0644 $$i $(DESTDIR)$(INCLUDEDIR); done
	mkdir -p $(DESTDIR)$(PCDIR)
	$(INSTALL) -m 0644 bitlbee.pc $(DESTDIR)$(PCDIR)

uninstall-dev:
	rm -f $(foreach hdr,$(headers),$(DESTDIR)$(INCLUDEDIR)/$(hdr))
	-rmdir $(DESTDIR)$(INCLUDEDIR)
	rm -f $(DESTDIR)$(PCDIR)/bitlbee.pc

install-etc:
	mkdir -p $(DESTDIR)$(ETCDIR)
	$(INSTALL) -m 0644 $(_SRCDIR_)motd.txt $(DESTDIR)$(ETCDIR)/motd.txt
	$(INSTALL) -m 0644 $(_SRCDIR_)bitlbee.conf $(DESTDIR)$(ETCDIR)/bitlbee.conf

uninstall-etc:
	rm -f $(DESTDIR)$(ETCDIR)/motd.txt
	rm -f $(DESTDIR)$(ETCDIR)/bitlbee.conf
	-rmdir $(DESTDIR)$(ETCDIR)

install-plugins: install-plugin-otr install-plugin-skype

install-plugin-otr:
ifdef OTR_PI
	mkdir -p $(DESTDIR)$(PLUGINDIR)
	$(INSTALL) -m 0755 otr.so $(DESTDIR)$(PLUGINDIR)
endif

install-plugin-skype:
ifdef SKYPE_PI
	mkdir -p $(DESTDIR)$(PLUGINDIR)
	$(INSTALL) -m 0755 skype.so $(DESTDIR)$(PLUGINDIR)
	mkdir -p $(DESTDIR)$(ETCDIR)/../skyped $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0644 $(_SRCDIR_)protocols/skype/skyped.cnf $(DESTDIR)$(ETCDIR)/../skyped/skyped.cnf
	$(INSTALL) -m 0644 $(_SRCDIR_)protocols/skype/skyped.conf.dist $(DESTDIR)$(ETCDIR)/../skyped/skyped.conf
	$(INSTALL) -m 0755 $(_SRCDIR_)protocols/skype/skyped.py $(DESTDIR)$(BINDIR)/skyped
	$(MAKE) -C protocols/skype install-doc
endif

systemd:
ifdef SYSTEMDSYSTEMUNITDIR
	mkdir -p init
	sed 's|@sbindir@|$(SBINDIR)|' $(_SRCDIR_)init/bitlbee.service.in > init/bitlbee.service
	sed 's|@sbindir@|$(SBINDIR)|' $(_SRCDIR_)init/bitlbee@.service.in > init/bitlbee@.service
endif

install-systemd: systemd
ifdef SYSTEMDSYSTEMUNITDIR
	mkdir -p $(DESTDIR)$(SYSTEMDSYSTEMUNITDIR)
	$(INSTALL) -m 0644 init/bitlbee.service $(DESTDIR)$(SYSTEMDSYSTEMUNITDIR)
	$(INSTALL) -m 0644 init/bitlbee@.service $(DESTDIR)$(SYSTEMDSYSTEMUNITDIR)
	$(INSTALL) -m 0644 $(_SRCDIR_)init/bitlbee.socket $(DESTDIR)$(SYSTEMDSYSTEMUNITDIR)
else
	@echo SYSTEMDSYSTEMUNITDIR not set, not installing systemd unit files.
endif

tar:
	fakeroot debian/rules clean || make distclean
	x=$$(basename $$(pwd)); \
	cd ..; \
	tar czf $$x.tar.gz --exclude=debian --exclude=.git* --exclude=.depend $$x

$(subdirs):
	@$(MAKE) -C $@ $(MAKECMDGOALS)

$(OTR_PI): %.so: $(_SRCDIR_)%.c
	@echo '*' Building plugin $@
	@$(CC) $(CFLAGS) -fPIC -shared $(LDFLAGS) $< -o $@ $(OTRFLAGS)

$(SKYPE_PI): $(_SRCDIR_)protocols/skype/skype.c
	@echo '*' Building plugin skype
	@$(CC) $(CFLAGS) $(LDFLAGS) $(SKYPEFLAGS) $< -o $@

$(objects): %.o: $(_SRCDIR_)%.c
	@echo '*' Compiling $<
	@$(CC) -c $(CFLAGS) $(CFLAGS_BITLBEE) $< -o $@

$(objects): Makefile Makefile.settings config.h

$(OUTFILE): $(objects) $(subdirs)
	@echo '*' Linking $(OUTFILE)
	@$(CC) $(objects) $(subdirobjs) -o $(OUTFILE) $(LDFLAGS_BITLBEE) $(LDFLAGS) $(EFLAGS)
ifndef DEBUG
	@echo '*' Stripping $(OUTFILE)
	@-$(STRIP) $(OUTFILE)
endif

ctags: 
	ctags `find . -name "*.c"` `find . -name "*.h"`

# Using this as a bogus Make target to test if a GNU-compatible version of
# make is available.
helloworld:
	@echo Hello World

# Check if we can load the helpfile. (This fails if some article is >1KB.)
# If print returns a NULL pointer, the file is unusable.
testhelp: doc
	gdb --eval-command='b main' --eval-command='r' \
	    --eval-command='print help_init(&global->helpfile, "doc/user-guide/help.txt")' \
	    $(OUTFILE) < /dev/null

-include .depend/*.d
# DO NOT DELETE
