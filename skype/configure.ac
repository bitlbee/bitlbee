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
PKG_CHECK_MODULES(BITLBEE, bitlbee)
CFLAGS="$CFLAGS $BITLBEE_CFLAGS"
LDFLAGS="$LDFLAGS $BITLBEE_LIBS"
prefix=`$PKG_CONFIG --variable=prefix bitlbee`

dnl Check for Skype4Py
AC_MSG_CHECKING(for Python module Skype4Py)
python -c "import Skype4Py"
if test "$?" != "0"; then
	AC_MSG_RESULT(no)
	SKYPE4PY="no"
else
	AC_MSG_RESULT(yes)
	SKYPE4PY="yes"
fi
AC_SUBST(SKYPE4PY)

AC_OUTPUT(config.mak)
AC_OUTPUT(skyped.conf.dist)

echo "
	compiler flags:			$CFLAGS
	linker flags:			$LDFLAGS
	shared object flags:		$SHARED_FLAGS
	shared object extension:	$SHARED_EXT
	skyped:                         $SKYPE4PY
	install program:		$INSTALL
	prefix:				$prefix
	sysconfig dir:			$sysconfdir/skyped
"
