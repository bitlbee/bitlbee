#!/bin/bash
#
#  Author       geno, <geno@xenyon.com>
#  Date         2004-04-24
#  Version      0.1c
#

show_help()
{
cat << _EOF_

This script converts your CenterICQ contacts (AIM/ICQ) to BitlBee's contacts.
The use of this script is on you own risk. You agree by using this script. :-)

SYNTAX: `basename $0` <protoname> [<add_proto_tag>]

	protoname     - Choose the protocol you want to get your contacts from
			by using "aim" or "icq" here.

	add_proto_tag - This is optional and adds a suffix to each nickname.
			For an AIM contact it will look like this: geno|aim
			For an ICQ contact it will be |icq , WOW! :-D
			To enable this option use "on". 

NOTE:
	After the conversion of one protocol is done you will find a file
	called bitlbee_[protoname] in ~/.centericq . Append the content of
	this file to /var/lib/bitlbee/[username].nicks .

	[username] is your username you use to talk to the BitlBee Server.
	You will have to be root to edit this file!

CREDITS:
	This script was written by geno (geno@xenyon.com).
	I hope it will help you to make the switch to BitlBee a bit easier. :-)

_EOF_
exit 0
}

case $1 in
	"") show_help ;;
	"icq")
		nick_protocol="[1-9]*/"
		protocol_const="3"
	;;
	
	"aim")
		nick_protocol="a*/"
		protocol_const="1"
	;;
	
	*) show_help ;;
esac

# can we see CenterICQ's directory ?
if [ ! -d ~/.centericq ]; then
	echo "The directory of CenterICQ (~/.centericq) was not found!"
	echo "Maybe you are logged in with the wrong username."
	exit 1
fi

# change to the center of all evil ;)
cd ~/.centericq

# get the listing of all nicks
nick_listing=`ls -d $nick_protocol | sed 's/\ /_DuMmY_/g' | sed 's/\/_DuMmY_/\/ /g'`

echo -e "\nConverting ...\n"

# remove old conversion
rm -f ~/.centericq/bitlbee_$1

for nick_accountname in $nick_listing; do
	# get rid of the slash and replace _DuMmY_ with space
	nick_accountname=`echo "$nick_accountname" | sed 's/\/$//' | sed 's/_DuMmY_/\ /g'`
	
	# find centericq alias
	nick_cicq_alias=`cat "$nick_accountname/info" | sed '46!d'`
	
	# if the centericq alias is the same as the account's name then
	# it's not a real alias; search for account nickname
	if [ "$nick_accountname" == "$nick_cicq_alias" ]; then
		nick_accountalias=`cat "$nick_accountname/info" | sed '1!d'`
	fi

	# save the best nickname for conversion
	if [ "x$nick_accountalias" == "x" ]; then
		nick="$nick_cicq_alias"
	else
		nick="$nick_accountalias"
	fi

	# cut off the prefix 'a' of the accountname
	if [ "$1" == "aim" ]; then
		nick_accountname=`echo "$nick_accountname" | sed 's/^a//'`
	fi

	# replace each space with an underscore (spaces are not allowed in irc nicknames)
	nick=`echo "$nick" | sed 's/\ /_/g'`

	# if tags are wanted we will add them here
	if [ "$2" == "on" ]; then
		nick=`echo "$nick"\|$1`
	fi

	# print output to std
	echo "Found '$nick_accountname' with alias '$nick'"
	# save output to file
	echo "$nick_accountname" $protocol_const "$nick" >> ~/.centericq/bitlbee_$1
done

echo -e "\nYou can find this list as a file in ~/.centericq/bitlbee_$1."
echo -e "See help if you don't know what you have to do next.\n"

