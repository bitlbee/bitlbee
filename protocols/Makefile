###########################
## Makefile for BitlBee  ##
##                       ##
## Copyright 2002 Lintux ##
###########################

### DEFINITIONS

-include ../Makefile.settings

# [SH] Program variables
objects = http_client.o md5.o nogaim.o proxy.o sha.o $(SSL_CLIENT)

# [SH] The next two lines should contain the directory name (in $(subdirs))
#      and the name of the object file, which should be linked into
#      protocols.o (in $(subdirobjs)). These need to be in order, i.e. the
#      first object file should be in the first directory.
subdirs = $(PROTOCOLS)
subdirobjs = $(PROTOOBJS)

# Expansion of variables
subdirobjs := $(join $(subdirs),$(addprefix /,$(subdirobjs)))
CFLAGS += -Wall
LFLAGS += -r

# [SH] Phony targets
all: protocols.o

.PHONY: all clean distclean $(subdirs)

clean: $(subdirs)
	rm -f *.o $(OUTFILE) core

distclean: clean $(subdirs)

$(subdirs):
	@$(MAKE) -C $@ $(MAKECMDGOALS)

### MAIN PROGRAM

protocols.o: $(objects) $(subdirs)
	@echo '*' Linking protocols.o
	@$(LD) $(LFLAGS) $(objects) $(subdirobjs) -o protocols.o

$(objects): ../Makefile.settings Makefile

$(objects): %.o: %.c
	@echo '*' Compiling $<
	@$(CC) -c $(CFLAGS) $< -o $@
