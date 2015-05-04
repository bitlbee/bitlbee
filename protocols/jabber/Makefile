###########################
## Makefile for BitlBee  ##
##                       ##
## Copyright 2002 Lintux ##
###########################

### DEFINITIONS

-include ../../Makefile.settings
ifdef _SRCDIR_
_SRCDIR_ := $(_SRCDIR_)protocols/jabber/
endif

# [SH] Program variables
objects = conference.o io.o iq.o jabber.o jabber_util.o message.o presence.o s5bytestream.o sasl.o si.o hipchat.o

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
	rm -rf .depend

### MAIN PROGRAM

$(objects): ../../Makefile.settings Makefile

$(objects): %.o: $(_SRCDIR_)%.c
	@echo '*' Compiling $<
	@$(CC) -c $(CFLAGS) $(CFLAGS_BITLBEE) $< -o $@

jabber_mod.o: $(objects)
	@echo '*' Linking jabber_mod.o
	@$(LD) $(LFLAGS) $(objects) -o jabber_mod.o

-include .depend/*.d
