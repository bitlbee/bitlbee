###########################
## Makefile for BitlBee  ##
##                       ##
## Copyright 2002 Lintux ##
###########################

### DEFINITIONS

-include ../../Makefile.settings

# [SH] Program variables
objects = iq.o jabber.o jabber_util.o message.o presence.o xmltree.o

CFLAGS += -Wall
LFLAGS += -r

# [SH] Phony targets
all: jabber_mod.o

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
