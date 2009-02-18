PORT=9876
BITLBEE=/usr/sbin/bitlbee

export TEST_SKYPE_ID=bitlbee-skype
export TEST_SKYPE_PASSWORD=:9eQBgoh8

testfiles := $(wildcard irssi/*.test)
tests := $(patsubst %.test,%,$(testfiles))

.PHONY: checkvars $(tests)

default: $(tests)
	@echo "passed $$(echo $(testfiles)|wc -w) tests."

checkvars:
	@for test in irssi/*.test; do NVAR="$$NVAR `irssi/livetest-irssi.sh $$test checkvars`";done;\
		if echo $$NVAR|grep TEST &>/dev/null; then \
			echo Needed variables: ; \
			echo $$NVAR | tr ' ' '\n' | sort | uniq; \
			exit 1; \
		else \
			echo "--- Environment OK ---";\
		fi

$(tests): % : %.test
	@if ! NVARS=`irssi/livetest-irssi.sh $< checkvars`; then echo Need environment variables for $@: $$NVARS;\
		echo Skipping...;exit 0;fi;\
	echo "--- Running test $@ ---"; \
		if ! ./livetest-bitlbee.sh $(BITLBEE) $(PORT) irssi/livetest-irssi.sh $< >$@.log; then \
			echo Test failed, log: ;\
			cat $@.log;\
			exit 1;\
		fi;\
	echo "--- OK ---" ;\
	sleep 1
clean:
	rm -r irssi/*.log bitlbeetest.pid dotirssi livetest


