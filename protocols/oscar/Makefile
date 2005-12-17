###########################
## Makefile for BitlBee  ##
##                       ##
## Copyright 2002 Lintux ##
###########################

### DEFINITIONS

-include ../../Makefile.settings

# [SH] Program variables
objects = admin.o auth.o bos.o buddylist.o chat.o chatnav.o conn.o icq.o im.o info.o misc.o msgcookie.o rxhandlers.o rxqueue.o search.o service.o snac.o ssi.o stats.o tlv.o txqueue.o oscar_util.o oscar.o

CFLAGS += -Wall
LFLAGS += -r

# [SH] Phony targets
all: oscar_mod.o

.PHONY: all clean distclean

clean:
	rm -f *.o core

distclean: clean

### MAIN PROGRAM

$(objects): ../../Makefile.settings Makefile

$(objects): %.o: %.c
	@echo '*' Compiling $<
	@$(CC) -c $(CFLAGS) $< -o $@

oscar_mod.o: $(objects)
	@echo '*' Linking oscar_mod.o
	@$(LD) $(LFLAGS) $(objects) -o oscar_mod.o
