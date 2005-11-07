; Inno setup script for Bitlbee
; (C) 2004-2005 Jelmer Vernooij <jelmer@samba.org>

[Setup]
AppName=BitlBee
AppPublisher=The BitlBee Team
AppPublisherURL=http://www.bitlbee.org/
AppSupportURL=http://win32.bitlbee.org/
AppUpdatesURL=http://win32.bitlbee.org/
AppCopyright=Copyright © 2002-2005 The BitlBee Team
DefaultDirName={pf}\Bitlbee
DefaultGroupName=Bitlbee
LicenseFile=..\COPYING
InfoAfterFile=README.TXT
OutputDir=.
AppVerName=Bitlbee-20050516
OutputBaseFileName="BitlBee-setup"

[Components]
Name: main; Description: Main executable and files; Types: full compact custom; Flags: fixed;
Name: "yahoo"; Description: Yahoo! Messenger support; Types: full;
Name: "oscar"; Description: AIM/ICQ support; Types: full;
Name: ssl; Description: SSL Support; Types: full;
Name: "ssl\msn"; Description: MSN messenger support; Types: full;
Name: "ssl\jabber"; Description: Jabber support; Types: full;
Name: docs; Description: Documentation; Types: full;

[Tasks]
Name: startupicon; Description: "&Automatically start when the computer boots"; GroupDescription: "Other tasks:"; Flags: unchecked

[Files]
Source: "Release\bitlbee.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: main;
Source: "Release\libmsn.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: "ssl\msn"
Source: "Deps\lib\ssl3.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: "ssl"
Source: "Deps\lib\nss3.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: "ssl"
Source: "Deps\lib\nssckbi.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: "ssl"
Source: "Deps\lib\smime3.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: "ssl"
Source: "Deps\lib\softokn3.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: "ssl"
Source: "Deps\lib\libplc4.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: "ssl"
Source: "Deps\lib\libnspr4.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: "ssl"
Source: "Release\libjabber.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: "ssl\jabber"
Source: "Release\bitlbee_ssl.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: "ssl"
Source: "Deps\bin\libglib-2.0-0.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: main;
Source: "Deps\bin\libgmodule-2.0-0.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: main;
Source: "Release\liboscar.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: "oscar"
Source: "Deps\bin\intl.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: main;
Source: "Deps\bin\iconv.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: main;
Source: "Release\libyahoo.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: "yahoo"
Source: "..\motd.txt"; DestDir: "{app}"; Flags: ignoreversion; Components: main;
Source: "..\doc\help.txt"; DestDir: "{app}"; Flags: ignoreversion; Components: main;
Source: "..\COPYING"; DestDir: "{app}"; Flags: ignoreversion; Components: main;
Source: "..\doc\TODO"; DestDir: "{app}"; Flags: ignoreversion; Components: main;
Source: "..\doc\README"; DestDir: "{app}"; Flags: ignoreversion; Components: main;
Source: "..\doc\FAQ"; DestDir: "{app}"; Flags: ignoreversion; Components: docs;
Source: "..\doc\CREDITS"; DestDir: "{app}"; Flags: ignoreversion; Components: main;
; Source: "..\doc\user-guide.pdf"; DestDir: "{app}"; Flags: ignoreversion; Components: docs;
Source: "..\doc\CHANGES"; DestDir: "{app}"; Flags: ignoreversion; Components: main;
Source: "..\doc\AUTHORS"; DestDir: "{app}"; Flags: ignoreversion; Components: main;
; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Icons]
Name: "{group}\Bitlbee"; Filename: "{app}\bitlbee.exe"
Name: "{commonstartup}\Bitlbee"; Filename: "{app}\bitlbee.exe"; Tasks: startupicon


[Run]
; NOTE: The following entry contains an English phrase ("Launch"). You are free to translate it into another language if required.
Filename: "{app}\bitlbee.exe"; Description: "Launch Bitlbee"; Flags: nowait postinstall skipifsilent

[Registry]
Root: HKLM; Subkey: "SOFTWARE\Bitlbee"; ValueType: string; ValueName: "helpfile"; ValueData: "{app}\help.txt"
Root: HKLM; Subkey: "SOFTWARE\Bitlbee"; ValueType: string; ValueName: "motdfile"; ValueData: "{app}\motd.txt"
Root: HKLM; Subkey: "SOFTWARE\Bitlbee"; ValueType: string; ValueName: "configdir"; ValueData: "{userappdata}\Bitlbee"
