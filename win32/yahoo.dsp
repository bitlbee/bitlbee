# Microsoft Developer Studio Project File - Name="yahoo" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 5.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=yahoo - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "yahoo.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "yahoo.mak" CFG="yahoo - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "yahoo - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "yahoo - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "yahoo - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "yahrel"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "." /I "..\protocols" /I ".." /I "deps\include" /I "deps\include\glib-2.0" /I "deps\lib\glib-2.0\include" /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "HAVE_CONFIG_H" /FD /c
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
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib iconv.lib glib-2.0.lib /nologo /subsystem:windows /dll /machine:I386 /out:"Release/libyahoo.dll" /libpath:"release" /libpath:"deps\lib"

!ELSEIF  "$(CFG)" == "yahoo - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "yahoo___"
# PROP BASE Intermediate_Dir "yahoo___"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "yahdeb"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /Zi /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /YX /FD /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /Zi /Od /I "." /I "..\protocols" /I ".." /I "deps\include" /I "deps\include\glib-2.0" /I "deps\lib\glib-2.0\include" /D "_DEBUG" /D "WIN32" /D "_WINDOWS" /D "HAVE_CONFIG_H" /FD /c
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
# ADD LINK32 ws2_32.lib glib-2.0.lib gmodule-2.0.lib /nologo /subsystem:windows /dll /debug /machine:I386 /out:"Debug/libyahoo.dll" /pdbtype:sept /libpath:"debug" /libpath:"deps\lib"

!ENDIF 

# Begin Target

# Name "yahoo - Win32 Release"
# Name "yahoo - Win32 Debug"
# Begin Source File

SOURCE=..\protocols\yahoo\crypt.c
# End Source File
# Begin Source File

SOURCE=..\protocols\yahoo\libyahoo2.c
# End Source File
# Begin Source File

SOURCE=..\protocols\yahoo\yahoo.c
# End Source File
# Begin Source File

SOURCE=..\protocols\yahoo\yahoo2.h
# End Source File
# Begin Source File

SOURCE=..\protocols\yahoo\yahoo2_callbacks.h
# End Source File
# Begin Source File

SOURCE=..\protocols\yahoo\yahoo2_types.h
# End Source File
# Begin Source File

SOURCE=..\protocols\yahoo\yahoo_debug.h
# End Source File
# Begin Source File

SOURCE=..\protocols\yahoo\yahoo_fn.c
# End Source File
# Begin Source File

SOURCE=..\protocols\yahoo\yahoo_fn.h
# End Source File
# Begin Source File

SOURCE=..\protocols\yahoo\yahoo_httplib.c
# End Source File
# Begin Source File

SOURCE=..\protocols\yahoo\yahoo_httplib.h
# End Source File
# Begin Source File

SOURCE=..\protocols\yahoo\yahoo_list.c
# End Source File
# Begin Source File

SOURCE=..\protocols\yahoo\yahoo_list.h
# End Source File
# Begin Source File

SOURCE=..\protocols\yahoo\yahoo_util.c
# End Source File
# Begin Source File

SOURCE=..\protocols\yahoo\yahoo_util.h
# End Source File
# End Target
# End Project
