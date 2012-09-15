###########################
## Makefile for BitlBee  ##
##                       ##
## Copyright 2002 Lintux ##
###########################

### DEFINITIONS

-include ../../Makefile.settings
ifdef _SRCDIR_
_SRCDIR_ := $(_SRCDIR_)protocols/twitter/
endif

# [SH] Program variables
objects = twitter.o twitter_http.o twitter_lib.o

LFLAGS += -r

# [SH] Phony targets
all: twitter_mod.o
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

twitter_mod.o: $(objects)
	@echo '*' Linking twitter_mod.o
	@$(LD) $(LFLAGS) $(objects) -o twitter_mod.o
	
-include .depend/*.d
