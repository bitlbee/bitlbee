#!/usr/bin/env bash

start_skyped()
{
	python ../skyped.py "$@" > skypedtest.pid
	while true
	do
		[ -e skypedtest.pid ] || break
		pid=$(sed 's/.*: //' skypedtest.pid)
		if [ -n "$(ps -p $pid -o pid=)" ]; then
			sleep 5
		else
			start_skyped "$@"
			break
		fi
	done
}

BITLBEE=$1
typeset -ix PORT=`echo $2 | egrep '^[0-9]{1,5}$'`
SCRIPT=$3
shift 3

[ -n "$SCRIPT" -a -n "$BITLBEE" -a -e "$SCRIPT" -a "$PORT" -ne 0 ] || { echo Syntax: `basename "$0"` bitlbee-executable listening-port test-script test-script-args; exit 1; }

# Create or empty test dir
mkdir livetest 2>/dev/null || rm livetest/bitlbeetest*.xml bitlbeetest.pid 2>/dev/null

# Run the bee
echo Running bitlbee...
$VALGRIND $BITLBEE -n -c bitlbee.conf -d livetest/ -D -P bitlbeetest.pid -p $PORT 2>bitlbee.log &
sleep 2

# Check if it's really running
kill -0 `cat bitlbeetest.pid 2>/dev/null ` 2>/dev/null || { echo Failed to run bitlbee daemon on port $PORT; exit 1; }

if [ -z "$TUNNELED_MODE" ]; then
	# Set up skyped

	rm -rf etc
	mkdir etc
	cd etc
	cp ../../skyped.cnf .
	cp ~/.skyped/skyped.cert.pem .
	cp ~/.skyped/skyped.key.pem .
	cd ..
	echo "[skyped]" > skyped.conf
	echo "username = $TEST_SKYPE_ID" >> skyped.conf
	SHA1=`which sha1sum`
	if [ -z "$SHA1" ]; then
		SHA1=`which sha1`
	fi
	if [ -z "$SHA1" ]; then
		echo Test failed
		echo "(Can't compute password for skyped.conf)"
		exit 77
	fi
	echo "password = $(echo -n $TEST_SKYPE_PASSWORD|$SHA1|sed 's/ *-$//')" >> skyped.conf
	# we use ~ here to test that resolve that syntax works
	echo "cert = $(pwd|sed "s|$HOME|~|")/etc/skyped.cert.pem" >> skyped.conf
	echo "key = $(pwd|sed "s|$HOME|~|")/etc/skyped.key.pem" >> skyped.conf
	echo "port = 2727" >> skyped.conf

	# Run skyped
	start_skyped -c skyped.conf -l skypedtest.log &
	sleep 2
fi

if [ "$TUNNELED_MODE" = "yes" ]; then
	rm -f tunnel.pid
	if [ -n "$TUNNEL_SCRIPT" ]; then
		$TUNNEL_SCRIPT &
		echo $! > tunnel.pid
		sleep 5
	fi
fi

# Run the test
echo Running test script...
"$SCRIPT" $*
RET=$?

if [ -z "$TUNNELED_MODE" ]; then
	# skyped runs on another host: no means to kill it
	# Kill skyped
	killall -TERM skype
	if [ -f skypedtest.pid ]; then
		pid=$(sed 's/.*: //' skypedtest.pid)
		rm skypedtest.pid
		[ -n "$(ps -p $pid -o pid=)" ] && kill -TERM $pid
	fi
fi

if [ "$TUNNELED_MODE" = "yes" ]; then
	if [ -n "$TUNNEL_SCRIPT" ]; then
		cat tunnel.pid >> /tmp/tunnel.pid
		kill `cat tunnel.pid`
		rm -f tunnel.pid
	fi
fi

# Kill bee
echo Killing bitlbee...
kill `cat bitlbeetest.pid`

if [ "$TUNNELED_MODE" = "yes" ]; then
	# give the skyped a chance to timeout
	sleep 30
fi

# Return test result
[ $RET -eq 0 ] && echo Test passed
[ $RET -ne 0 ] && echo Test failed
[ $RET -eq 22 ] && echo '(timed out)'
[ $RET -eq 66 ] && echo '(environment variables missing)'
exit $RET
