-include config.mak

VERSION = 0.3.1

skype.so: skype.c config.mak
	$(CC) $(CFLAGS) -shared -o skype.so skype.c $(LDFLAGS)

install: skype.so skyped.py
	$(INSTALL) -d $(DESTDIR)$(plugindir)
	$(INSTALL) -d $(DESTDIR)$(bindir)
	$(INSTALL) -d $(DESTDIR)$(sysconfdir)
	$(INSTALL) skype.so $(DESTDIR)$(plugindir)
	$(INSTALL) skyped.py $(DESTDIR)$(bindir)/skyped
	sed -i 's|/usr/local/etc/skyped|$(sysconfdir)|' $(DESTDIR)$(bindir)/skyped
	$(INSTALL) -m644 skyped.conf.dist $(DESTDIR)$(sysconfdir)/skyped.conf
	$(INSTALL) -m644 skyped.cnf $(DESTDIR)$(sysconfdir)

client: client.c

prepare: configure.ac
	cp /usr/share/automake/install-sh ./
	cp /usr/share/aclocal/pkg.m4 aclocal.m4
	autoconf

clean:
	rm -f skype.so

distclean: clean
	rm -rf autom4te.cache config.log config.mak config.status
	rm -f configure install-sh aclocal.m4

dist:
	git archive --format=tar --prefix=bitlbee-skype-$(VERSION)/ HEAD | tar xf -
	mkdir -p bitlbee-skype-$(VERSION)
	git log --no-merges |git name-rev --tags --stdin > bitlbee-skype-$(VERSION)/Changelog
	make -C bitlbee-skype-$(VERSION) prepare
	tar czf bitlbee-skype-$(VERSION).tar.gz bitlbee-skype-$(VERSION)
	rm -rf bitlbee-skype-$(VERSION)

release:
	git tag $(VERSION)
	$(MAKE) dist
	gpg --comment "See http://ftp.frugalware.org/pub/README.GPG for info" \
		-ba -u 20F55619 bitlbee-skype-$(VERSION).tar.gz

doc: HEADER.html Changelog

HEADER.html: README
	ln -s README HEADER.txt
	asciidoc -a toc -a numbered -a sectids HEADER.txt
	sed -i 's|@VERSION@|$(VERSION)|g' HEADER.html
	rm HEADER.txt

Changelog: .git/refs/heads/master
	git log --no-merges |git name-rev --tags --stdin >Changelog
