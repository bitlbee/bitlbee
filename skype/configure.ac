AC_INIT([Skype plugin for BitlBee], 1.0, [vmiklos@vmiklos.hu], bitlbee-skype)
AC_PROG_CC
AC_PROG_INSTALL

AC_ARG_ENABLE([debug], AS_HELP_STRING([--enable-debug], [Enable debug support (default: disabled)]), debug=yes)
AC_MSG_CHECKING(for debug mode request)
if test "x$debug" = "xyes" ; then
	CFLAGS="-g -Wall -Werror"
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

case "`$CC -dumpmachine`" in
	*linux*)
		SHARED_FLAGS="-shared"
		SHARED_EXT="so"
		;;
	*apple-darwin*)
		SHARED_FLAGS="-dynamiclib -Wl,-headerpad_max_install_names,-undefined,dynamic_lookup"
		SHARED_EXT="dylib"
		;;
	*)
		AC_MSG_ERROR([Your machine is not yet supported])
		;;
esac
AC_SUBST(SHARED_FLAGS)
AC_SUBST(SHARED_EXT)

dnl Check for bitlbee
AC_MSG_CHECKING(for BitlBee)
pkg-config --exists bitlbee
if test "$?" != "0"; then
	AC_MSG_RESULT(no)
	BITLBEE="no"
else
	AC_MSG_RESULT(yes)
	BITLBEE="yes"
	if test -z "$CFLAGS"; then
		CFLAGS="`pkg-config --cflags bitlbee`"
	else
		CFLAGS="$CFLAGS `pkg-config --cflags bitlbee`"
	fi
	if test -z "$LDFLAGS"; then
		LDFLAGS="`pkg-config --libs bitlbee`"
	else
		LDFLAGS="$LDFLAGS `pkg-config --libs bitlbee`"
	fi
	prefix=`pkg-config --variable=prefix bitlbee`
fi
AC_SUBST(BITLBEE)

dnl Check for Skype4Py
AC_MSG_CHECKING(for Python module Skype4Py)
python -c "import Skype4Py" 2>&AS_MESSAGE_LOG_FD
if test "$?" != "0"; then
	AC_MSG_RESULT(no)
	SKYPE4PY="no"
else
	AC_MSG_RESULT(yes)
	SKYPE4PY="yes"
fi
AC_SUBST(SKYPE4PY)

if test "$BITLBEE" = "no" -a "$SKYPE4PY" = "no"; then
	AC_ERROR([In order to use bitlbee-skype you need at least BitlBee or Skype4Py installed.])
fi

AC_OUTPUT(config.mak)
AC_OUTPUT(skyped.conf.dist)

echo "
	BitlBee plugin:                 $BITLBEE
	skyped:                         $SKYPE4PY
	prefix:				$prefix
	install program:		$INSTALL"
if test "$BITLBEE" = "yes"; then
	echo "	compiler flags:			$CFLAGS
	linker flags:			$LDFLAGS
	shared object flags:		$SHARED_FLAGS
	shared object extension:	$SHARED_EXT"
fi
if test "$SKYPE4PY" = "yes"; then
	echo "	sysconfig dir:			$sysconfdir/skyped"
fi
echo
