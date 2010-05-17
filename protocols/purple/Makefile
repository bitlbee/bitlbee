###########################
## Makefile for BitlBee  ##
##                       ##
## Copyright 2002 Lintux ##
###########################

### DEFINITIONS

-include ../../Makefile.settings

# [SH] Program variables
objects = ft.o purple.o

CFLAGS += -Wall $(PURPLE_CFLAGS)
LFLAGS += -r

# [SH] Phony targets
all: purple_mod.o
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

purple_mod.o: $(objects)
	@echo '*' Linking purple_mod.o
	$(LD) $(LFLAGS) $(objects) -o purple_mod.o
