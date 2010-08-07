###########################
## Makefile for BitlBee  ##
##                       ##
## Copyright 2002 Lintux ##
###########################

### DEFINITIONS

-include ../../Makefile.settings
ifdef SRCDIR
SRCDIR := $(SRCDIR)protocols/jabber/
endif

# [SH] Program variables
objects = conference.o io.o iq.o jabber.o jabber_util.o message.o presence.o s5bytestream.o sasl.o si.o

LFLAGS += -r

# [SH] Phony targets
all: jabber_mod.o
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

$(objects): %.o: $(SRCDIR)%.c
	@echo '*' Compiling $<
	@$(CC) -c $(CFLAGS) $< -o $@

jabber_mod.o: $(objects)
	@echo '*' Linking jabber_mod.o
	@$(LD) $(LFLAGS) $(objects) -o jabber_mod.o
