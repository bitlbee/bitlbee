-include config.mak

skype.so: skype.c config.mak
	$(CC) $(CFLAGS) -shared -o skype.so skype.c $(LDFLAGS)

install: skype.so skyped.py
	$(INSTALL) -d $(DESTDIR)$(plugindir)
	$(INSTALL) -d $(DESTDIR)$(sbindir)
	$(INSTALL) skype.so $(DESTDIR)$(plugindir)
	$(INSTALL) skyped.py $(DESTDIR)$(sbindir)/skyped

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

doc: HEADER.html Changelog

HEADER.html: README
	ln -s README HEADER.txt
	asciidoc -a toc -a numbered HEADER.txt
	rm HEADER.txt

Changelog: .git/refs/heads/master
	git log --no-merges > Changelog
