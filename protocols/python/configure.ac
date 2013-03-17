AC_INIT([Bitlbee Python plugin], 0.1, [https://github.com/flexo/minecraftswitch/issues], bitlbee-python, [https://github.com/flexo/minecraftswitch])
AC_MSG_NOTICE([Configuring bitlbee-python...])

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

dnl Check for bitlbee
AC_MSG_CHECKING(for BitlBee)
if test -d /usr/local/lib/pkgconfig; then
        PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/local/lib/pkgconfig
        export PKG_CONFIG_PATH
fi
pkg-config --exists bitlbee
if test "$?" != "0"; then
        AC_MSG_RESULT(no)
        BITLBEE="no"
else
        AC_MSG_RESULT(yes)
        BITLBEE="yes"
fi

if test "$BITLBEE" = "no"; then
        AC_ERROR([In order to use bitlbee-python you need BitlBee installed.])
fi

if test -z "$CFLAGS"; then
        CFLAGS="`pkg-config --cflags bitlbee` `python-config --cflags`"
else
        CFLAGS="$CFLAGS `pkg-config --cflags bitlbee` `python-config --cflags`"
fi
if test -z "$LDFLAGS"; then
        LDFLAGS="`pkg-config --libs bitlbee` `python-config --ldflags`"
else
        LDFLAGS="$LDFLAGS `pkg-config --libs bitlbee` `python-config --ldflags`"
fi
prefix=`pkg-config --variable=prefix bitlbee`

case "`$CC -dumpmachine`" in
        *linux*|*freebsd*|*netbsd*|*openbsd*)
                SHARED_FLAGS="-fPIC -shared"
                SHARED_EXT="so"
                ;;
        *apple-darwin*)
                SHARED_FLAGS="-dynamiclib -undefined dynamic_lookup"
                SHARED_EXT="dylib"
                ;;
        *)
                AC_MSG_ERROR([Your machine is not yet supported])
                ;;
esac
AC_SUBST(SHARED_FLAGS)
AC_SUBST(SHARED_EXT)

AC_OUTPUT(Makefile)
AC_OUTPUT(config.mak)
cp confdefs.h config.h

echo "
        BitlBee plugin:                 $BITLBEE
        prefix:                         $prefix
        install program:                $INSTALL
        compiler flags:                 $CFLAGS
        linker flags:                   $LDFLAGS
        shared object flags:            $SHARED_FLAGS
        shared object extension:        $SHARED_EXT"
echo
