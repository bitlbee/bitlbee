###########################
## Makefile for BitlBee  ##
##                       ##
## Copyright 2002 Lintux ##
###########################

### DEFINITIONS

-include ../../Makefile.settings
ifdef _SRCDIR_
_SRCDIR_ := $(_SRCDIR_)protocols/msn/
endif

# [SH] Program variables
objects = msn.o msn_util.o ns.o soap.o tables.o gw.o

LFLAGS += -r

# [SH] Phony targets
all: msn_mod.o
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

msn_mod.o: $(objects)
	@echo '*' Linking msn_mod.o
	@$(LD) $(LFLAGS) $(objects) -o msn_mod.o

-include .depend/*.d
