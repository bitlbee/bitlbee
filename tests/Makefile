-include ../Makefile.settings
ifdef _SRCDIR_
_SRCDIR_ := $(_SRCDIR_)tests/
endif

LFLAGS += $(LDFLAGS_TESTS)

CFLAGS += $(shell pkg-config --cflags check)
LFLAGS += $(shell pkg-config --libs check)

all: check
	./check $(CHECKFLAGS)

clean:
	rm -f check *.o

distclean: clean

main_objs = bitlbee.o conf.o dcc.o help.o ipc.o irc.o irc_cap.o irc_channel.o irc_commands.o irc_im.o irc_send.o irc_user.o irc_util.o irc_commands.o log.o nick.o query.o root_commands.o set.o storage.o $(STORAGE_OBJS) auth.o $(AUTH_OBJS)

test_objs = check.o check_util.o check_nick.o check_md5.o check_arc.o check_irc.o check_help.o check_user.o check_set.o check_jabber_sasl.o check_jabber_util.o

check: $(test_objs) $(addprefix ../, $(main_objs)) ../protocols/protocols.o ../lib/lib.o
	@echo '*' Linking $@
	$(VERBOSE) $(CC) $(CFLAGS) -o $@ $^ $(LFLAGS) $(EFLAGS)

%.o: $(_SRCDIR_)%.c
	@echo '*' Compiling $<
	$(VERBOSE) $(CC) -c $(CFLAGS) $< -o $@
