###########################
## Makefile for BitlBee  ##
##                       ##
## Copyright 2002 Lintux ##
###########################

### DEFINITIONS

-include ../Makefile.settings
ifdef _SRCDIR_
_SRCDIR_ := $(_SRCDIR_)protocols/
endif

# [SH] Program variables
objects = account.o bee.o bee_chat.o bee_ft.o bee_user.o nogaim.o


# [SH] The next two lines should contain the directory name (in $(subdirs))
#      and the name of the object file, which should be linked into
#      protocols.o (in $(subdirobjs)). These need to be in order, i.e. the
#      first object file should be in the first directory.
subdirs = $(PROTOCOLS)
subdirobjs = $(PROTOOBJS)

# Expansion of variables
subdirobjs := $(join $(subdirs),$(addprefix /,$(subdirobjs)))
LFLAGS += -r

# [SH] Phony targets
all: protocols.o
check: all
lcov: check
gcov:
	gcov *.c

.PHONY: all clean distclean $(subdirs)

clean: $(subdirs)
	rm -f *.o $(OUTFILE) core

distclean: clean $(subdirs)
	rm -rf .depend

$(subdirs):
	@$(MAKE) -C $@ $(MAKECMDGOALS)

### MAIN PROGRAM

protocols.o: $(objects) $(subdirs)
	@echo '*' Linking protocols.o
	@$(LD) $(LFLAGS) $(objects) $(subdirobjs) -o protocols.o

$(objects): ../Makefile.settings Makefile

$(objects): %.o: $(_SRCDIR_)%.c
	@echo '*' Compiling $<
	@$(CC) -c $(CFLAGS) $(CFLAGS_BITLBEE) $< -o $@

-include .depend/*.d
