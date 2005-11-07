# Microsoft Developer Studio Project File - Name="oscar" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 5.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=oscar - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "oscar.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "oscar.mak" CFG="oscar - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "oscar - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "oscar - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "oscar - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "oscarrel"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "..\protocols\oscar" /I "." /I "..\protocols" /I ".." /I "deps\include" /I "deps\include\glib-2.0" /I "deps\lib\glib-2.0\include" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /FD /c
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
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib iconv.lib ws2_32.lib glib-2.0.lib /nologo /subsystem:windows /dll /machine:I386 /out:"Release/liboscar.dll" /libpath:"release" /libpath:"deps\lib"

!ELSEIF  "$(CFG)" == "oscar - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "oscar___"
# PROP BASE Intermediate_Dir "oscar___"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "oscdeb"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /Zi /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /YX /FD /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /Zi /Od /I "..\protocols\oscar" /I "." /I "..\protocols" /I ".." /I "deps\include" /I "deps\include\glib-2.0" /I "deps\lib\glib-2.0\include" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /FD /c
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
# ADD LINK32 gmodule-2.0.lib ws2_32.lib glib-2.0.lib iconv.lib /nologo /subsystem:windows /dll /debug /machine:I386 /out:"Debug/liboscar.dll" /pdbtype:sept /libpath:"debug" /libpath:"deps\lib"

!ENDIF 

# Begin Target

# Name "oscar - Win32 Release"
# Name "oscar - Win32 Debug"
# Begin Source File

SOURCE=..\protocols\oscar\admin.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\aim.h
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\aim_cbtypes.h
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\aim_internal.h
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\auth.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\bos.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\buddylist.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\chat.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\chatnav.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\conn.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\faimconfig.h
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\ft.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\icq.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\im.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\info.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\misc.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\msgcookie.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\oscar.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\oscar_util.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\rxhandlers.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\rxqueue.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\search.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\service.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\snac.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\ssi.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\stats.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\tlv.c
# End Source File
# Begin Source File

SOURCE=..\protocols\oscar\txqueue.c
# End Source File
# End Target
# End Project
