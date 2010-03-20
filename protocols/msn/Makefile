###########################
## Makefile for BitlBee  ##
##                       ##
## Copyright 2002 Lintux ##
###########################

### DEFINITIONS

-include ../../Makefile.settings

# [SH] Program variables
objects = msn.o msn_util.o ns.o passport.o sb.o soap.o tables.o

CFLAGS += -Wall
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

### MAIN PROGRAM

$(objects): ../../Makefile.settings Makefile

$(objects): %.o: %.c
	@echo '*' Compiling $<
	@$(CC) -c $(CFLAGS) $< -o $@

msn_mod.o: $(objects)
	@echo '*' Linking msn_mod.o
	@$(LD) $(LFLAGS) $(objects) -o msn_mod.o
	

