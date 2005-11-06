-include ../Makefile.settings

install:
	mkdir -p $(DESTDIR)$(MANDIR)/man8/ $(DESTDIR)$(MANDIR)/man5/
	install -m 0644 bitlbee.8 $(DESTDIR)$(MANDIR)/man8/
	install -m 0644 bitlbee.conf.5 $(DESTDIR)$(MANDIR)/man5/
	make -C user-guide $@

uninstall:
	rm -f $(DESTDIR)$(MANDIR)/man8/bitlbee.8*
	rm -f $(DESTDIR)$(MANDIR)/man5/bitlbee.conf.5*
	make -C user-guide $@

.PHONY: install uninstall
