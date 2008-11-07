-include config.mak

VERSION = 0.6.3
# latest stable
BITLBEE_VERSION = 1.2.3

AMVERSION = $(shell automake --version|sed 's/.* //;s/\([0-9]\+\.[0-9]\+\)\.[0-9]\+/\1/;q')

ifeq ($(BITLBEE),yes)
all: skype.$(SHARED_EXT)
else
all:
endif

skype.$(SHARED_EXT): skype.c config.mak
ifeq ($(BITLBEE),yes)
	$(CC) $(CFLAGS) $(SHARED_FLAGS) -o skype.$(SHARED_EXT) skype.c $(LDFLAGS)
endif

install: all
ifeq ($(BITLBEE),yes)
	$(INSTALL) -d $(DESTDIR)$(plugindir)
	$(INSTALL) skype.$(SHARED_EXT) $(DESTDIR)$(plugindir)
endif
ifeq ($(SKYPE4PY),yes)
	$(INSTALL) -d $(DESTDIR)$(bindir)
	$(INSTALL) -d $(DESTDIR)$(sysconfdir)
	$(INSTALL) skyped.py $(DESTDIR)$(bindir)/skyped
	perl -p -i -e 's|/usr/local/etc/skyped|$(sysconfdir)|' $(DESTDIR)$(bindir)/skyped
	$(INSTALL) -m644 skyped.conf.dist $(DESTDIR)$(sysconfdir)/skyped.conf
	perl -p -i -e 's|\$${prefix}|$(prefix)|' $(DESTDIR)$(sysconfdir)/skyped.conf
	$(INSTALL) -m644 skyped.cnf $(DESTDIR)$(sysconfdir)
endif

client: client.c

autogen: configure.ac
	cp /usr/share/automake-$(AMVERSION)/install-sh ./
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
	gpg --comment "See http://vmiklos.hu/gpg/ for info" \
		-ba bitlbee-skype-$(VERSION).tar.gz

doc: HEADER.html Changelog

HEADER.html: README Makefile
	asciidoc -a toc -a numbered -a sectids -o HEADER.html README
	sed -i 's|@VERSION@|$(VERSION)|g' HEADER.html
	sed -i 's|@BITLBEE_VERSION@|$(BITLBEE_VERSION)|g' HEADER.html

Changelog: .git/refs/heads/master
	git log --no-merges |git name-rev --tags --stdin >Changelog
