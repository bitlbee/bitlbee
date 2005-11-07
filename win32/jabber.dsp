# Microsoft Developer Studio Project File - Name="jabber" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 5.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=jabber - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "jabber.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "jabber.mak" CFG="jabber - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "jabber - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "jabber - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "jabber - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "jabrel"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "..\protocols\jabber" /I "." /I "..\protocols" /I ".." /I "deps\include" /I "deps\include\glib-2.0" /I "deps\lib\glib-2.0\include" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /FD /c
# SUBTRACT CPP /YX
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /o NUL /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /o NUL /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /dll /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib iconv.lib glib-2.0.lib /nologo /subsystem:windows /dll /machine:I386 /out:"Release/libjabber.dll" /libpath:"release" /libpath:"deps\lib"

!ELSEIF  "$(CFG)" == "jabber - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "jabber__"
# PROP BASE Intermediate_Dir "jabber__"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "jabdeb"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /Zi /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /YX /FD /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /Zi /Od /I "..\protocols\jabber" /I "." /I "..\protocols" /I ".." /I "deps\include" /I "deps\include\glib-2.0" /I "deps\lib\glib-2.0\include" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /FD /c
# SUBTRACT CPP /YX
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /o NUL /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /o NUL /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 odbc32.lib glib-2.0.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbccp32.lib ws2_32.lib /nologo /subsystem:windows /dll /debug /machine:I386 /out:"Debug/libjabber.dll" /pdbtype:sept /libpath:"debug" /libpath:"deps\lib"

!ENDIF 

# Begin Target

# Name "jabber - Win32 Release"
# Name "jabber - Win32 Debug"
# Begin Source File

SOURCE=..\protocols\jabber\asciitab.h
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\expat.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\genhash.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\hashtable.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\hashtable.h
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\iasciitab.h
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\jabber.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\jabber.h
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\jconn.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\jid.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\jpacket.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\jutil.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\karma.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\latin1tab.h
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\libxode.h
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\log.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\log.h
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\nametab.h
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\pool.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\pproxy.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\rate.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\str.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\utf8tab.h
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\xhash.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\xmldef.h
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\xmlnode.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\xmlparse.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\xmlparse.h
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\xmlrole.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\xmlrole.h
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\xmltok.c
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\xmltok.h
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\xmltok_impl.h
# End Source File
# Begin Source File

SOURCE=..\protocols\jabber\xstream.c
# End Source File
# End Target
# End Project
