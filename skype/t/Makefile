PORT=9876
BITLBEE=/usr/sbin/bitlbee

export TEST_SKYPE_ID=user
export TEST_SKYPE_PASSWORD=pass

testfiles := $(wildcard irssi/*.test)
tests := $(patsubst %.test,%,$(testfiles))

.PHONY: $(tests)

default: $(tests)
	@echo "passed $$(echo $(testfiles)|wc -w) tests."

$(tests): % : %.test
	@echo "--- Running test $@ ---"; \
		if [ -r "$(BITLBEE)" -a -x "$(BITLBEE)" ]; then \
			bitlbee_binary="$(BITLBEE)"; \
		else \
			bitlbee_basename=`basename $(BITLBEE)`; \
			bitlbee_binary=`which $$bitlbee_basename`; \
		fi; \
		if ! ./livetest-bitlbee.sh "$$bitlbee_binary" $(PORT) irssi/livetest-irssi.sh $< >$@.log; then \
			echo Test failed, log: ;\
			cat $@.log;\
			exit 1;\
		fi;\
	echo "--- OK ---" ;\
	sleep 1
clean:
	rm -r irssi/*.log bitlbeetest.pid dotirssi livetest


