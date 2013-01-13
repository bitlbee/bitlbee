-include ../../Makefile.settings
ifdef _SRCDIR_
_SRCDIR_ := $(_SRCDIR_)protocols/skype/
endif

DATE := $(shell date +%Y-%m-%d)
INSTALL = install
ASCIIDOC = yes

ifeq ($(ASCIIDOC),yes)
MANPAGES = skyped.1
else
MANPAGES =
endif

all: $(MANPAGES)

clean:
	rm -f $(MANPAGES)

# take this from the kernel
check:
	perl checkpatch.pl --show-types --ignore LONG_LINE,CAMELCASE --no-tree --file skype.c

test: all
	./test.py

doc: $(MANPAGES)

install-doc: doc
ifeq ($(ASCIIDOC),yes)
	$(INSTALL) -d $(DESTDIR)$(MANDIR)/man1
	$(INSTALL) -m644 $(MANPAGES) $(DESTDIR)$(MANDIR)/man1
endif

uninstall-doc:
	rm -f $(DESTDIR)$(MANDIR)/man1/skyped.1*

%.1: $(_SRCDIR_)%.txt $(_SRCDIR_)asciidoc.conf
	a2x --asciidoc-opts="-f $(_SRCDIR_)asciidoc.conf" -a bee_date=$(DATE) -f manpage -D . $<
