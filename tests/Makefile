include ../Makefile.settings

LFLAGS +=-lcheck

all: check
	./check

check: check.o check_util.o ../util.o
	@echo '*' Linking $@
	@$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS) $(EFLAGS)

%.o: %.c
	@echo '*' Compiling $<
	@$(CC) -c $(CFLAGS) $< -o $@
