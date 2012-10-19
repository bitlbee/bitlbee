###########################
## Makefile for BitlBee  ##
##                       ##
## Copyright 2002 Lintux ##
###########################

### DEFINITIONS

-include ../../Makefile.settings
ifdef _SRCDIR_
_SRCDIR_ := $(_SRCDIR_)protocols/yahoo/
endif

# [SH] Program variables
objects = yahoo.o libyahoo2.o yahoo_httplib.o yahoo_util.o

CFLAGS += -DSTDC_HEADERS -DHAVE_STRING_H -DHAVE_STRCHR -DHAVE_MEMCPY -DHAVE_GLIB
LFLAGS += -r

# [SH] Phony targets
all: yahoo_mod.o
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
	@$(CC) -c $(CFLAGS) $(CFLAGS_BITLBEE) $< -o $@

yahoo_mod.o: $(objects)
	@echo '*' Linking yahoo_mod.o
	@$(LD) $(LFLAGS) $(objects) -o yahoo_mod.o

-include .depend/*.d
