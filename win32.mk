!INCLUDE Makefile.settings

GLIB_CFLAGS = /I "$(GLIB_DIR)\include" \
	      /I "$(GLIB_DIR)\include\glib-2.0" \
	      /I "$(GLIB_DIR)\lib\glib-2.0\include"

GLIB_LFLAGS = /libpath:"$(GLIB_DIR)\lib" 

NSS_CFLAGS = /I "$(NSS_DIR)\include" /I "$(NSPR_DIR)\include"
NSS_LFLAGS = /libpath:"$(NSS_DIR)\lib" /libpath:"$(NSPR_DIR)\lib" 
NSS_LIBS = nss3.lib ssl3.lib libnspr4.lib

COMMON_LIBS = kernel32.lib user32.lib advapi32.lib shell32.lib iconv.lib \
			glib-2.0.lib gmodule-2.0.lib wsock32.lib advapi32.lib 


MAIN_OBJS = account.obj bitlbee.obj commands.obj crypting.obj \
	help.obj irc.obj protocols\md5.obj protocols\nogaim.obj \
	protocols\sha.obj protocols\proxy.obj query.obj nick.obj set.obj \
	user.obj protocols\util.obj win32.obj 

MAIN_LIBS = $(COMMON_LIBS)

SSL_OBJS = protocols\ssl_nss.obj
SSL_LIBS = $(NSS_LFLAGS) $(NSS_LIBS)

MSN_OBJS = \
	protocols\msn\msn.obj \
	protocols\msn\msn_util.obj \
	protocols\msn\ns.obj \
	protocols\msn\passport.obj \
	protocols\msn\sb.obj \
	protocols\msn\tables.obj \
	$(SSL_OBJS)

MSN_LIBS = $(COMMON_LIBS) $(SSL_LIBS)

OSCAR_OBJS = \
	protocols\oscar\admin.obj \
	protocols\oscar\auth.obj \
	protocols\oscar\bos.obj \
	protocols\oscar\buddylist.obj \
	protocols\oscar\chat.obj \
	protocols\oscar\chatnav.obj \
	protocols\oscar\conn.obj \
	protocols\oscar\icq.obj \
	protocols\oscar\im.obj \
	protocols\oscar\info.obj \
	protocols\oscar\misc.obj \
	protocols\oscar\msgcookie.obj \
	protocols\oscar\oscar.obj \
	protocols\oscar\oscar_util.obj \
	protocols\oscar\rxhandlers.obj \
	protocols\oscar\rxqueue.obj \
	protocols\oscar\search.obj \
	protocols\oscar\service.obj \
	protocols\oscar\snac.obj \
	protocols\oscar\ssi.obj \
	protocols\oscar\stats.obj \
	protocols\oscar\tlv.obj \
	protocols\oscar\txqueue.obj

OSCAR_LIBS = $(COMMON_LIBS)

JABBER_OBJS = \
	protocols\jabber\expat.obj \
	protocols\jabber\genhash.obj \
	protocols\jabber\hashtable.obj \
	protocols\jabber\jabber.obj \
	protocols\jabber\jconn.obj \
	protocols\jabber\jid.obj \
	protocols\jabber\jpacket.obj \
	protocols\jabber\jutil.obj \
	protocols\jabber\karma.obj \
	protocols\jabber\log.obj \
	protocols\jabber\pool.obj \
	protocols\jabber\pproxy.obj \
	protocols\jabber\rate.obj \
	protocols\jabber\str.obj \
	protocols\jabber\xhash.obj \
	protocols\jabber\xmlnode.obj \
	protocols\jabber\xmlparse.obj \
	protocols\jabber\xmlrole.obj \
	protocols\jabber\xmltok.obj \
	protocols\jabber\xstream.obj \
	$(SSL_OBJS)

JABBER_LIBS = $(COMMON_LIBS) $(SSL_LIBS)

YAHOO_OBJS = \
	protocols\yahoo\crypt.obj \
	protocols\yahoo\libyahoo2.obj \
	protocols\yahoo\vc50.idb \
	protocols\yahoo\yahoo.obj \
	protocols\yahoo\yahoo_fn.obj \
	protocols\yahoo\yahoo_httplib.obj \
	protocols\yahoo\yahoo_list.obj \
	protocols\yahoo\yahoo_util.obj

YAHOO_LIBS = $(COMMON_LIBS)

CC=cl.exe
CFLAGS=$(GLIB_CFLAGS) $(NSS_CFLAGS) /D NDEBUG /D WIN32 /D _WINDOWS \
       /I . /I protocols /I protocols\oscar /nologo \
       /D GLIB2 /D ARCH="\"Windows\"" /D CPU="\"x86\"" \
       /D PLUGINDIR="plugins"

.c.obj:
	$(CC) $(CFLAGS) /c /Fo$@ $<

ALL: bitlbee.exe libmsn.dll liboscar.dll libjabber.dll libyahoo.dll
	
LINK32=link.exe
LINK32_FLAGS=/nologo $(GLIB_LFLAGS)

bitlbee.exe: $(DEF_FILE) $(MAIN_OBJS)
    $(LINK32) $(MAIN_LIBS) $(LINK32_FLAGS) /out:bitlbee.exe $(MAIN_OBJS)

libmsn.dll: $(MSN_OBJS) 
	$(LINK32) /DLL /SUBSYSTEM:WINDOWS /ENTRY:msn_init $(MSN_LIBS) $(LINK32_FLAGS) /out:libmsn.dll $(MSN_OBJS)

libyahoo.dll: $(YAHOO_OBJS)
	$(LINK32) /DLL /SUBSYSTEM:WINDOWS /ENTRY:yahoo_init $(YAHOO_LIBS) $(LINK32_FLAGS) /out:libyahoo.dll $(YAHOO_OBJS)

liboscar.dll: $(OSCAR_OBJS)
	$(LINK32) /DLL /SUBSYSTEM:WINDOWS /ENTRY:oscar_init $(OSCAR_LIBS) $(LINK32_FLAGS) /out:liboscar.dll $(OSCAR_OBJS)

libjabber.dll: $(JABBER_OBJS)
	$(LINK32) /DLL /SUBSYSTEM:WINDOWS /ENTRY:jabber_init $(JABBER_LIBS) $(LINK32_FLAGS) /out:libjabber.dll $(JABBER_OBJS)

clean:
	@-erase $(MAIN_OBJS)
	@-erase $(MSN_OBJS)
	@-erase $(JABBER_OBJS)
	@-erase $(OSCAR_OBJS)
	@-erase $(YAHOO_OBJS)
