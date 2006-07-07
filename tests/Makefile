include ../Makefile.settings

LFLAGS +=-lcheck

all: check 
	./check

clean:
	rm -f check *.o

main_objs = account.o bitlbee.o conf.o crypting.o help.o ini.o ipc.o irc.o irc_commands.o log.o nick.o query.o root_commands.o set.o storage.o storage_text.o url.o user.o util.o 

test_objs = check.o check_util.o check_nick.o

check: $(test_objs) $(addprefix ../, $(main_objs)) ../protocols/protocols.o
	@echo '*' Linking $@
	@$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS) $(EFLAGS)

%.o: %.c
	@echo '*' Compiling $<
	@$(CC) -c $(CFLAGS) $< -o $@
