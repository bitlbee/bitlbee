include ../Makefile.settings

LFLAGS +=-lcheck

all: check 
	./check $(CHECKFLAGS)

clean:
	rm -f check *.o

main_objs = account.o bitlbee.o conf.o crypting.o help.o ipc.o irc.o irc_commands.o log.o nick.o query.o root_commands.o set.o storage.o storage_xml.o storage_text.o user.o 

test_objs = check.o check_util.o check_nick.o check_md5.o check_irc.o

check: $(test_objs) $(addprefix ../, $(main_objs)) ../protocols/protocols.o ../lib/lib.o
	@echo '*' Linking $@
	@$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS) $(EFLAGS)

%.o: %.c
	@echo '*' Compiling $<
	@$(CC) -c $(CFLAGS) $< -o $@
