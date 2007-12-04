###########################
## Makefile for BitlBee  ##
##                       ##
## Copyright 2002 Lintux ##
###########################

### DEFINITIONS

-include ../../Makefile.settings

# [SH] Program variables
objects = conference.o io.o iq.o jabber.o jabber_util.o message.o presence.o sasl.o xmltree.o si.o s5bytestream.o

CFLAGS += -Wall
LFLAGS += -r

# [SH] Phony targets
all: jabber_mod.o
check: all
lcov:
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

jabber_mod.o: $(objects)
	@echo '*' Linking jabber_mod.o
	@$(LD) $(LFLAGS) $(objects) -o jabber_mod.o
