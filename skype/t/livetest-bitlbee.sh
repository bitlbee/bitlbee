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

# Run skyped
python ../skyped.py -c ../skyped.conf > skypedtest.pid
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
