# Microsoft Developer Studio Project File - Name="bitlbee" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 5.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Application" 0x0101

CFG=bitlbee - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "bitlbee.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "bitlbee.mak" CFG="bitlbee - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "bitlbee - Win32 Release" (based on "Win32 (x86) Application")
!MESSAGE "bitlbee - Win32 Debug" (based on "Win32 (x86) Application")
!MESSAGE 

# Begin Project
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "bitlbee - Win32 Release"

# PROP BASE Use_MFC 6
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 6
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MD /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_AFXDLL" /Yu"stdafx.h" /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "." /I "..\protocols" /I ".." /I "deps\include" /I "deps\include\glib-2.0" /I "deps\lib\glib-2.0\include" /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "_AFXDLL" /FD /c
# SUBTRACT CPP /YX
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /o NUL /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /o NUL /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG" /d "_AFXDLL"
# ADD RSC /l 0x409 /d "NDEBUG" /d "_AFXDLL"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 /nologo /subsystem:windows /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib iconv.lib glib-2.0.lib gmodule-2.0.lib wsock32.lib advapi32.lib /nologo /subsystem:windows /machine:I386 /libpath:"release" /libpath:"deps\lib"
# SUBTRACT LINK32 /incremental:yes /nodefaultlib

!ELSEIF  "$(CFG)" == "bitlbee - Win32 Debug"

# PROP BASE Use_MFC 6
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 6
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MDd /W3 /Gm /GX /Zi /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_AFXDLL" /Yu"stdafx.h" /FD /c
# ADD CPP /nologo /Gd /MDd /Ze /W3 /Gm /GX /Zi /Od /I "." /I "..\protocols" /I ".." /I "deps\include" /I "deps\include\glib-2.0" /I "deps\lib\glib-2.0\include" /D "_DEBUG" /D "WIN32" /D "_WINDOWS" /D "_AFXDLL" /FR /FD /c
# SUBTRACT CPP /YX
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /o NUL /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /o NUL /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG" /d "_AFXDLL"
# ADD RSC /l 0x409 /d "_DEBUG" /d "_AFXDLL"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept
# ADD LINK32 iconv.lib glib-2.0.lib gmodule-2.0.lib wsock32.lib kernel32.lib user32.lib advapi32.lib /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept /libpath:"debug" /libpath:"deps\lib"

!ENDIF 

# Begin Target

# Name "bitlbee - Win32 Release"
# Name "bitlbee - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\account.c
# End Source File
# Begin Source File

SOURCE=..\bitlbee.c
# End Source File
# Begin Source File

SOURCE=.\bitlbee.rc

!IF  "$(CFG)" == "bitlbee - Win32 Release"

!ELSEIF  "$(CFG)" == "bitlbee - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\commands.c
# End Source File
# Begin Source File

SOURCE=..\crypting.c
# End Source File
# Begin Source File

SOURCE=..\debug.c
# End Source File
# Begin Source File

SOURCE=..\help.c
# End Source File
# Begin Source File

SOURCE=..\irc.c
# End Source File
# Begin Source File

SOURCE=..\protocols\md5.c
# End Source File
# Begin Source File

SOURCE=..\nick.c
# End Source File
# Begin Source File

SOURCE=..\protocols\nogaim.c
# End Source File
# Begin Source File

SOURCE=..\protocols\proxy.c
# End Source File
# Begin Source File

SOURCE=..\query.c
# End Source File
# Begin Source File

SOURCE=..\set.c
# End Source File
# Begin Source File

SOURCE=..\protocols\sha.c
# End Source File
# Begin Source File

SOURCE=..\user.c
# End Source File
# Begin Source File

SOURCE=..\protocols\util.c

!IF  "$(CFG)" == "bitlbee - Win32 Release"

!ELSEIF  "$(CFG)" == "bitlbee - Win32 Debug"

# PROP Intermediate_Dir "Debugx"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\win32.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\account.h
# End Source File
# Begin Source File

SOURCE=..\bitlbee.h
# End Source File
# Begin Source File

SOURCE=..\commands.h
# End Source File
# Begin Source File

SOURCE=..\conf.h
# End Source File
# Begin Source File

SOURCE=.\config.h
# End Source File
# Begin Source File

SOURCE=..\crypting.h
# End Source File
# Begin Source File

SOURCE=..\help.h
# End Source File
# Begin Source File

SOURCE=..\ini.h
# End Source File
# Begin Source File

SOURCE=..\irc.h
# End Source File
# Begin Source File

SOURCE=..\log.h
# End Source File
# Begin Source File

SOURCE=..\protocols\md5.h
# End Source File
# Begin Source File

SOURCE=..\nick.h
# End Source File
# Begin Source File

SOURCE=..\protocols\nogaim.h
# End Source File
# Begin Source File

SOURCE=..\set.h
# End Source File
# Begin Source File

SOURCE=..\protocols\sha.h
# End Source File
# Begin Source File

SOURCE=..\sock.h
# End Source File
# Begin Source File

SOURCE=..\user.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;cnt;rtf;gif;jpg;jpeg;jpe"
# Begin Source File

SOURCE=.\res\bitlbee.rc2
# End Source File
# Begin Source File

SOURCE=.\res\bmp00002.bmp
# End Source File
# Begin Source File

SOURCE=.\res\icon1.ico
# End Source File
# Begin Source File

SOURCE=.\res\icon2.ico
# End Source File
# End Group
# Begin Source File

SOURCE=.\README.TXT
# End Source File
# End Target
# End Project
