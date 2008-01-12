AC_INIT([Skype plugin for BitlBee], 1.0, [vmiklos@frugalware.org], bitlbee-skype)
AC_PROG_CC
AC_PROG_INSTALL

AC_ARG_ENABLE([debug], AS_HELP_STRING([--enable-debug], [Enable debug support (default: disabled)]), debug=yes)
AC_MSG_CHECKING(for debug mode request)
if test x$debug = xyes ; then
	CFLAGS="-g -Wall -Werror"
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

dnl Check for bitlbee
PKG_CHECK_MODULES(BITLBEE, bitlbee)
CFLAGS="$CFLAGS $BITLBEE_CFLAGS"
LDFLAGS="$LDFLAGS $BITLBEE_LIBS"
AC_OUTPUT(config.mak)
AC_OUTPUT(skyped.conf.dist)
