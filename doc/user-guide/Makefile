-include ../../Makefile.settings
ifdef _SRCDIR_
_SRCDIR_ := $(_SRCDIR_)doc/user-guide/
endif

EXTRAPARANEWLINE = 1
# EXTRAPARANEWLINE = 0

all: user-guide.txt user-guide.html help.txt # user-guide.pdf user-guide.ps user-guide.rtf

%.tex: %.db.xml
	xsltproc --stringparam l10n.gentext.default.language "en" --stringparam latex.documentclass.common "" --stringparam latex.babel.language "" --output $@ http://db2latex.sourceforge.net/xsl/docbook.xsl $<

%.txt: %.db.xml
	xmlto --skip-validation txt $<
	mv $*.db.txt $@

%.html: %.db.xml
	xsltproc --output $@ http://docbook.sourceforge.net/release/xsl/current/xhtml/docbook.xsl $<

%.pdf: %.db.xml
	xmlto --skip-validation pdf $<
	mv $*.db.pdf $@

%.ps: %.db.xml
	xmlto --skip-validation ps $<
	mv $*.db.ps $@

help.xml: commands.xml

%.db.xml: %.xml docbook.xsl
	xsltproc --xinclude --output $@ docbook.xsl $< 

help.txt: help.xml help.xsl commands.xml misc.xml quickstart.xml
	xsltproc --stringparam extraparanewline "$(EXTRAPARANEWLINE)" --xinclude help.xsl $< | perl -0077 -pe 's/\n\n%/\n%/s; s/_b_/\002/g;' > $@

clean: 
	rm -f *.html *.pdf *.ps *.rtf *.txt *.db.xml

install:
	mkdir -p $(DESTDIR)$(DATADIR)
	chmod 0755 $(DESTDIR)$(DATADIR)
	rm -f $(DESTDIR)$(DATADIR)/help.txt # Prevent help function from breaking in running sessions
	install -m 0644 $(_SRCDIR_)help.txt $(DESTDIR)$(DATADIR)/help.txt

uninstall:
	rm -f $(DESTDIR)$(DATADIR)/help.txt
	-rmdir $(DESTDIR)$(DATADIR)

.PHONY: clean install uninstall
