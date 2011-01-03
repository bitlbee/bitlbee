#!/usr/bin/env bash
ISCRIPT=$1
OPT=$2

[ -n "$ISCRIPT" ] || { echo Syntax: `basename "$0"` irssi-test-script; exit 1; }

# Load variables from test
eval `sed -e '1,/^###/!d;/^###/d' "$ISCRIPT"`

#if [ "$OPT" == "checkvars" ]; then echo $TESTNEEDEDVARS; fi
RET=0

# Check if we have the neccessary environment variables for this test
for var in $TESTNEEDEDVARS; do
	if [ -z `eval echo \$\{$var\}` ]; then
		if [ "$OPT" != "checkvars" ]; then 
			echo Need environment variable "$var" for this test.
			exit 66
		else
			echo $var
			RET=66
		fi
	fi
done

# if we got this far we're OK
if [ "$OPT" == "checkvars" ]; then exit $RET; fi

[ -n "$PORT" ] || { echo 'Need the bitlbee listening port as environment variable PORT'; exit 1; }

# Setup the irssi dir
(
	rm -r dotirssi
	mkdir -p dotirssi/scripts dotirssi/logs
	cp "`dirname $0`"/trigger.pl dotirssi/scripts &&
	echo 'script load trigger.pl' >dotirssi/startup
) &>/dev/null || { echo Failed to setup irssi testdir; exit 1; }

# write irssi config

echo '

aliases = { 
	runtest = "'`sed -e "1,/^###/d;s/@LOGIN@/$TESTLOGIN/;s/@PASSWORD@/$TESTPASSWORD/" "$ISCRIPT" | tr '\n' ';'`'"; 
	expectbee  = "/trigger add -publics -channels &bitlbee -regexp";
	expectjoin = "/trigger add -joins -masks *!$0@* $1-";
	expectmsg  = "/trigger add -privmsgs -masks *!$0@* $1-";
};

servers = ( { address = "localhost"; chatnet = "local"; port = "'$PORT'"; autoconnect="yes";});

settings = {
  settings_autosave = "no";
  core = { real_name = "bitlbee-test"; user_name = "bitlbee-test"; nick = "bitlbeetest"; };
  "fe-text" = { actlist_sort = "refnum"; };
};

chatnets = { local = { type = "IRC"; autosendcmd = "/runtest"; }; };

logs = {
"dotirssi/logs/status.log" = { auto_open = "yes"; level = "ALL"; items = ( { type = "window"; name = "1"; } ); };
"dotirssi/logs/control.log" = { auto_open = "yes"; level = "ALL"; items = ( { type = "target"; name = "&bitlbee"; } ); };
' >dotirssi/config

for nick in $TESTLOGNICKS; do 
	echo '
	"dotirssi/logs/'$nick'.log" = { auto_open = "yes"; level = "ALL"; items = ( { type = "target"; name = "'$nick'"; } ); };
		' >>dotirssi/config
done

echo '};' >>dotirssi/config

# Go!

echo Running irssi...
screen -D -m irssi --config=dotirssi/config --home=dotirssi/ & 

# output logs

submitlogs() {
	sed -i -e "s/$TESTLOGIN/---TESTLOGIN---/;s/$TESTPASSWORD/---TESTPASSWORD---/" dotirssi/logs/*.log

	if [ "$OPT" == "tgz" ]; then
		tar czf "`dirname $0`"/"`basename "$ISCRIPT"`".logs.tgz dotirssi/logs/*.log
	elif [ "$OPT" == "ctest" ]; then 
		echo CTEST_FULL_OUTPUT
		for log in dotirssi/logs/*.log; do
			echo -n '<DartMeasurement name="'$log'" type="text/string"><![CDATA['
			cat "$log"
			echo "]]></DartMeasurement>"
		done
	else
		echo Test logs: dotirssi/logs/*.log
	fi
}

# timeout stuff

t=$TESTDURATION
intval=1
while (( t >= intval )); do
	sleep $intval
	kill -0 $! &>/dev/null || { echo screen/irssi terminated.; submitlogs; bash -c "cd dotirssi/logs && $TESTCHECKRESULT" >/dev/null; exit $?; }
	t=$(( t - $intval ))
done
echo Killing screen/irssi...
kill $!
submitlogs
exit 22
