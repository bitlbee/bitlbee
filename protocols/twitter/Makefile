###########################
## Makefile for BitlBee  ##
##                       ##
## Copyright 2002 Lintux ##
###########################

### DEFINITIONS

-include ../../Makefile.settings

# [SH] Program variables
objects = twitter.o twitter_http.o twitter_lib.o

CFLAGS += -Wall
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

### MAIN PROGRAM

$(objects): ../../Makefile.settings Makefile

$(objects): %.o: %.c
	@echo '*' Compiling $<
	@$(CC) -c $(CFLAGS) $< -o $@

twitter_mod.o: $(objects)
	@echo '*' Linking twitter_mod.o
	@$(LD) $(LFLAGS) $(objects) -o twitter_mod.o
	

