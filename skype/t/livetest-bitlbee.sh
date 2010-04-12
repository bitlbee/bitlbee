#!/bin/bash
BITLBEE=$1
typeset -ix PORT=`echo $2 | egrep '^[0-9]{1,5}$'`
SCRIPT=$3
shift 3

[ -n "$SCRIPT" -a -n "$BITLBEE" -a -e "$SCRIPT" -a "$PORT" -ne 0 ] || { echo Syntax: `basename "$0"` bitlbee-executable listening-port test-script test-script-args; exit 1; }

# Create or empty test dir
mkdir livetest 2>/dev/null || rm livetest/bitlbeetest*.xml bitlbeetest.pid 2>/dev/null

# Run the bee
echo Running bitlbee...
$VALGRIND $BITLBEE -n -c bitlbee.conf -d livetest/ -D -P bitlbeetest.pid -p $PORT & sleep 2

# Check if it's really running
kill -0 `cat bitlbeetest.pid 2>/dev/null ` 2>/dev/null || { echo Failed to run bitlbee daemon on port $PORT; exit 1; }

# Set up skyped

rm -rf etc
mkdir etc
cd etc
cp ../../skyped.cnf .
yes ""|openssl req -new -x509 -days 365 -nodes -config skyped.cnf -out skyped.cert.pem -keyout skyped.key.pem 2> openssl.log
cd ..
echo "[skyped]" > skyped.conf
echo "username = $TEST_SKYPE_ID" >> skyped.conf
echo "password = $(echo -n $TEST_SKYPE_PASSWORD|sha1sum|sed 's/ *-$//')" >> skyped.conf
# we use ~ here to test that resolve that syntax works
echo "cert = $(pwd|sed "s|$HOME|~|")/etc/skyped.cert.pem" >> skyped.conf
echo "key = $(pwd|sed "s|$HOME|~|")/etc/skyped.key.pem" >> skyped.conf
echo "port = 2727" >> skyped.conf

# Run skyped
python ../skyped.py -c skyped.conf -l skypedtest.log > skypedtest.pid
sleep 2

# Run the test
echo Running test script...
"$SCRIPT" $*
RET=$?

# Kill skyped
killall -TERM skype
kill -TERM $(sed 's/.*: //' skypedtest.pid)

# Kill bee
echo Killing bitlbee...
kill `cat bitlbeetest.pid`

# Return test result
[ $RET -eq 0 ] && echo Test passed
[ $RET -ne 0 ] && echo Test failed
[ $RET -eq 22 ] && echo '(timed out)'
[ $RET -eq 66 ] && echo '(environment variables missing)'
exit $RET
