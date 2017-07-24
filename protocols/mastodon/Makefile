###########################
## Makefile for BitlBee  ##
##                       ##
## Copyright 2002 Lintux ##
###########################

### DEFINITIONS

-include ../../Makefile.settings
ifdef _SRCDIR_
_SRCDIR_ := $(_SRCDIR_)protocols/mastodon/
endif

# [SH] Program variables
objects = mastodon.o mastodon_http.o mastodon_lib.o

LFLAGS += -r

# [SH] Phony targets
all: mastodon_mod.o
check: all
lcov: check
gcov: 
	gcov *.c

.PHONY: all clean distclean

clean:
	rm -f *.o core

distclean: clean
	rm -rf .depend

### MAIN PROGRAM

$(objects): ../../Makefile.settings Makefile

$(objects): %.o: $(_SRCDIR_)%.c
	@echo '*' Compiling $<
	$(VERBOSE) $(CC) -c $(CFLAGS) $(CFLAGS_BITLBEE) $< -o $@

mastodon_mod.o: $(objects)
	@echo '*' Linking mastodon_mod.o
	$(VERBOSE) $(LD) $(LFLAGS) $(objects) -o mastodon_mod.o

-include .depend/*.d
