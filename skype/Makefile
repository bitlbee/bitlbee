-include config.mak

VERSION = 0.4.2

AMVERSION = $(shell automake --version|sed 's/.* //;s/\([0-9]\+\.[0-9]\+\)\.[0-9]\+/\1/;q')

skype.$(SHARED_EXT): skype.c config.mak
	$(CC) $(CFLAGS) $(SHARED_FLAGS) -o skype.$(SHARED_EXT) skype.c $(LDFLAGS)

install: skype.$(SHARED_EXT) skyped.py
	$(INSTALL) -d $(DESTDIR)$(plugindir)
	$(INSTALL) -d $(DESTDIR)$(bindir)
	$(INSTALL) -d $(DESTDIR)$(sysconfdir)
	$(INSTALL) skype.$(SHARED_EXT) $(DESTDIR)$(plugindir)
	$(INSTALL) skyped.py $(DESTDIR)$(bindir)/skyped
	sed -i 's|/usr/local/etc/skyped|$(sysconfdir)|' $(DESTDIR)$(bindir)/skyped
	$(INSTALL) -m644 skyped.conf.dist $(DESTDIR)$(sysconfdir)/skyped.conf
	sed -i 's|$${prefix}|$(prefix)|' $(DESTDIR)$(sysconfdir)/skyped.conf
	$(INSTALL) -m644 skyped.cnf $(DESTDIR)$(sysconfdir)

client: client.c

autogen: configure.ac
	cp /usr/share/automake-$(AMVERSION)/install-sh ./
	cp /usr/share/aclocal/pkg.m4 aclocal.m4
	autoconf

clean:
	rm -f skype.$(SHARED_EXT)

distclean: clean
	rm -f config.log config.mak config.status

autoclean: distclean
	rm -rf aclocal.m4 autom4te.cache configure install-sh

dist:
	git archive --format=tar --prefix=bitlbee-skype-$(VERSION)/ HEAD | tar xf -
	mkdir -p bitlbee-skype-$(VERSION)
	git log --no-merges |git name-rev --tags --stdin > bitlbee-skype-$(VERSION)/Changelog
	make -C bitlbee-skype-$(VERSION) autogen
	tar czf bitlbee-skype-$(VERSION).tar.gz bitlbee-skype-$(VERSION)
	rm -rf bitlbee-skype-$(VERSION)

release:
	git tag $(VERSION)
	$(MAKE) dist
	gpg --comment "See http://ftp.frugalware.org/pub/README.GPG for info" \
		-ba -u 20F55619 bitlbee-skype-$(VERSION).tar.gz

doc: HEADER.html Changelog

HEADER.html: README Makefile
	ln -s README HEADER.txt
	asciidoc -a toc -a numbered -a sectids HEADER.txt
	sed -i 's|@VERSION@|$(VERSION)|g' HEADER.html
	rm HEADER.txt

Changelog: .git/refs/heads/master
	git log --no-merges |git name-rev --tags --stdin >Changelog
