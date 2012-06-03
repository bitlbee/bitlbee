-include ../Makefile.settings
ifdef _SRCDIR_
_SRCDIR_ := $(_SRCDIR_)doc/
endif

all:
	# Only build the docs if this is a bzr checkout
	test ! -d ../.bzr || $(MAKE) -C user-guide

install:
	mkdir -p $(DESTDIR)$(MANDIR)/man8/ $(DESTDIR)$(MANDIR)/man5/
	$(INSTALL) -m 0644 $(_SRCDIR_)bitlbee.8 $(DESTDIR)$(MANDIR)/man8/
	$(INSTALL) -m 0644 $(_SRCDIR_)bitlbee.conf.5 $(DESTDIR)$(MANDIR)/man5/
	$(MAKE) -C user-guide $@

uninstall:
	rm -f $(DESTDIR)$(MANDIR)/man8/bitlbee.8*
	rm -f $(DESTDIR)$(MANDIR)/man5/bitlbee.conf.5*
	$(MAKE) -C user-guide $@

.PHONY: install uninstall
