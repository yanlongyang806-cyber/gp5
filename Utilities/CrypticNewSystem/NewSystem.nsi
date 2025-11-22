
!include "pathscripts.nsi"


; The name of the installer
Name "Cryptic Studios New System Setup"

; The file to write
OutFile "CrypticNewSystem.exe"

SetPluginUnload  alwaysoff

; The fluff text
ComponentText "This is an installer for various packages of Cryptic Studios stuff."

InstType "Basic"
InstType "FC Basic"
InstType "NNO Basic"
InstType "STO Basic"
InstType "STO Artist"
InstType "FC Artist"
InstType "NNO Artist"
InstType "Programmer"
InstType "Programmer Part 2"
InstType "Programmer Refresh"
InstType "None"


;--------------------------------

;THE STUFF TO INSTALL:

;GAME DATA (COMES FIRST NOW!)

SectionGroup "Game Data Installs"

;FC

	Section "FC Basic Install"
		SectionIn 2
		ExecWait 'xcopy "N:\Software\Gimme Setups\fcnosrc.bat" C:\ /Y'
		Exec 'C:\fcnosrc.bat'
		ExecWait 'del C:\fcnosrc.bat'
	SectionEnd

	Section "FC Src Install"
		SectionIn 6
		ExecWait 'xcopy "N:\Software\Gimme Setups\fcsrc.bat" C:\ /Y'
		Exec 'C:\fcsrc.bat'
		ExecWait 'del C:\fcsrc.bat'
	SectionEnd

;STO

	Section "STO Basic Install"
		SectionIn 4
		ExecWait 'xcopy "N:\Software\Gimme Setups\STOnosrc.bat" C:\ /Y'
		Exec 'C:\STOnosrc.bat'
		ExecWait 'del C:\STOnosrc.bat'
	SectionEnd

	Section "STO Src Install"
		SectionIn 5
		ExecWait 'xcopy "N:\Software\Gimme Setups\STOsrc.bat" C:\ /Y'
		Exec 'C:\STOsrc.bat'
		ExecWait 'del C:\STOsrc.bat'
	SectionEnd

;NNO

	Section "NNO Basic Install"
		SectionIn 3 
		ExecWait 'xcopy "N:\Software\Gimme Setups\NNOnosrc.bat" C:\ /Y'
		Exec 'C:\NNOnosrc.bat'
		ExecWait 'del C:\NNOnosrc.bat'
	SectionEnd

	Section "NNO Src Install"
		SectionIn 7
		ExecWait 'xcopy "N:\Software\Gimme Setups\NNOsrc.bat" C:\ /Y'
		Exec 'C:\NNOsrc.bat'
		ExecWait 'del C:\NNOsrc.bat'
	SectionEnd

;ALL

	Section "Programmer Install (After Raptor)"
		SectionIn 8
		ExecWait 'xcopy "N:\Software\Gimme Setups\ProgrammerEverything.bat" C:\ /Y'
		Exec 'C:\ProgrammerEverything.bat'
		ExecWait 'del C:\ProgrammerEverything.bat'
	SectionEnd

	Section "ALL PROJECTS Basic Install"
		;SectionIn
		ExecWait 'xcopy "N:\Software\Gimme Setups\EverythingNOsrc.bat" C:\ /Y'
		Exec 'C:\EverythingNOsrc.bat'
		ExecWait 'del C:\EverythingNOsrc.bat'
	SectionEnd

	Section "ALL PROJECTS Src Install"
		;SectionIn
		ExecWait 'xcopy "N:\Software\Gimme Setups\Everythingsrc.bat" C:\ /Y'
		Exec 'C:\Everythingsrc.bat'
		ExecWait 'del C:\Everythingsrc.bat'
	SectionEnd

SectionGroupEnd


;BASIC STUFF (any install):


Section "NOD32 (Antivirus)"

	ExecWait 'N:\Software\NOD32\einstaller.exe'
	SectionIn 1 2 3 4 5 6 7 8

SectionEnd


Section "Path Setup (c:\Cryptic\tools\bin)"

	SectionIn 1 2 3 4 5 6 7 8 10
	Push C:\Cryptic\tools\bin
	Call AddToPath  
  
SectionEnd

Section "Edit Plus"
	SectionIn 1 2 3 4 5 6 7 8
	MessageBox MB_OK "The EditPlus installation will now launch, please click OK and accept all default installation paths"
	ExecWait '"N:\Software\Editplus\epp230_en.exe" /auto'
	SetOutPath "$PROGRAMFILES\EditPlus 2\"
	File tool.ini
	SetOutPath "$APPDATA\EditPlus 2\"
	File tool.ini
	File default0_mac
	File default1_mac
	File editplus.ini

	ExecWait 'regedit /s N:\Software\Editplus\License.reg'
SectionEnd

Section "VNC (Corp - For Vista)"
	SectionIn 2 3 4 5 6 7 8
	ExecWait '"N:\Software\VNC\Enterprise Edition (Corp Only)\vnc-E4_5-x86_x64_win32.exe" /silent'
	ExecWait 'regedit /s N:\Software\VNC\Options.reg'

SectionEnd

Section "Hosts file"

	SectionIn 1 2 3 4 5 6 7 8
	SetOutPath $SYSDIR\drivers\etc
	File C:\WINDOWS\SYSTEM32\DRIVERS\ETC\hosts

SectionEnd

Section "Gimme Registry hooks"
	SectionIn 1 2 3 4 5 6 7 8
	Exec "N:\revisions\gimme.exe -register"

SectionEnd

Section "Fix Windows Find"
	SectionIn 1 2 3 4 5 6 7 8 10
	WriteRegDWORD HKLM SYSTEM\CurrentControlSet\Control\ContentIndex FilterFilesWithUnknownExtensions 1

SectionEnd

Section "Show file extensions (requires logout)"

	SectionIn 1 2 3 4 5 6 7 8
	WriteRegDWORD HKCU Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced HideFileExt 0

SectionEnd

Section "Beyond Compare"
	SectionIn 1 2 3 4 5 6 7 8
	WriteRegExpandStr HKCU "Software\Scooter Software\Beyond Compare" CertKey "kxZoVDVfdxRwOuiWWzQYGgYSjEmunWStRJMB8P+qkNXMx3F8FcDrBnI9YULxVinUXYBco+RCtiJJ6MHR+sACOhoMAWlsEBvZ3d-674kwaXgzrUvMqEIKNFNGRgslit5mTocQYYqqG5KppaMeXu+FP0DcHAj1niYLePLXWavps6hcGTqJtkb+kBuZ4aLJdByinqNNgn0Flm0glDPyVR-Tomwkt6wEKKxIQOrQHSJkYlb1b-RxFnsvkr9vluMEYhHK"
	Exec "N:\Software\BeyondCompare2\beycomp_070308.exe /sp- /silent /norestart"
SectionEnd

Section "Cryptic Web Shortcuts"
	SectionIn 1 2 3 4 5 6 7 8
	CreateDirectory "$FAVORITES\Cryptic\"
	CreateShortCut "$FAVORITES\Cryptic\Confluence.lnk" "http://crypticwiki:8081/display/cs/Home"
	CreateShortCut "$FAVORITES\Cryptic\JIRA.lnk" "http://code:8080/"
	CreateShortCut "$FAVORITES\Cryptic\Cryptic Internal Forums.lnk" "http://intranet/"
	CreateShortCut "$FAVORITES\Cryptic\Cryptic Studios Webmail (External).lnk" "https://exchange2.crypticstudios.com/"
	CreateShortCut "$FAVORITES\Cryptic\Cryptic Studios Webmail (Internal).lnk" "https://email"

SectionEnd

Section "FileWatcher Config"
	SectionIn 1 2 3 4 5 6 7 8 10
	SetOutPath "C:\"
	File filewatch.txt
SectionEnd

Section "MaxCommunicator"

	ExecWait 'N:\Software\Altiview MAX software\MaxCommunicator\MaxCommunicator\setup.exe'
	SectionIn 1 2 3 4 5 6 7 8

SectionEnd 

Section "AMD Code Analyst"

	ExecWait '"N:\Software\AMD CodeAnalyst_Public_2.80.472.0332.exe" /passive /norestart'
	SectionIn 1 2 3 4 5 6 7 8

SectionEnd

Section "BelManage"
	
	ExecWait '\\vesta\Belclients\BelMonitor.exe'
	SectionIn 1 2 3 4 5 6 7 8

SectionEnd

Section "7Zip"

	ExecWait '"N:\Software\7z462.exe" /S /D="C:\Program Files\7-Zip"'
	SectionIn 1 2 3 4 5 6 7 8

SectionEnd

Section "Pidgin"

	ExecWait "N:\Software\Pidgin\pidgin-2.5.8.exe"
	SectionIn 1 2 3 4 5 6 7 8

SectionEnd


;BY PROJECT:




;PROGRAMMING:


SectionGroup "Programmer Stuff"

	Section "Subversion Config File"
		SectionIn 8 
		SetOutPath "$APPDATA\Subversion"
		File "C:\Cryptic\tools\programmers\svn\config"
	SectionEnd

;	Section "Nightly Source Code Backup OLD"
;		;SectionIn 8 10
;
;		SetOutPath $TEMP
;
;		push "backup_source"
;		push "Nightly Source Code Backup"
;		push "C:\Cryptic\tools\programmers\nightly_backup\backup_source.bat"
;		push "C:\Cryptic\tools\programmers\nightly_backup"
;		; 1 = DAILY  - field 15 gives interval in days (here it 1)
;		push "*(&l2, &i2 0, &i2 2003, &i2 9, &i2 3, &i2 0, &i2 0, &i2 0, &i2 5, &i2 00, i 0, i 0, i 0, i 1, &i2 1, i 0, i 0, &i2 0) i.s"
;
;		Call CreateTask
;		Pop $0
;		; MessageBox MB_OK "CreateTask result: $0"
;
;		; last plugin call must not have /NOUNLOAD so NSIS will be able to delete the temporary DLL
;		SetPluginUnload manual
;		; do nothing
;		System::Free 0
;
;	SectionEnd

	Section "Nightly Source Code Backup"

		SectionIn 8 10
		ExecWait 'schtasks /create /u "paragon\cryptic" /p "none123" /sc DAILY /tn "Nightly Source Code Backup" /ru "paragon\cryptic" /rp "none123" /tr C:\Cryptic\tools\programmers\nightly_backup\backup_source.bat /st 03:00:00'

	SectionEnd

	Section "Tortoise SVN-32"
		; SectionIn 8
		ExecWait 'msiexec /quiet /norestart /i "N:\Software\subversion\TortoiseSVN-1.6.5.16974-win32-svn-1.6.5.msi"'
	SectionEnd

	Section "Tortoise SVN-64"
		SectionIn 8
		ExecWait 'msiexec /quiet /norestart /i "N:\Software\subversion\TortoiseSVN-1.6.5.16974-x64-svn-1.6.5.msi"'
	SectionEnd

	Section "Set SVN Diff to BeyondCompare"
		SectionIn 8 10
		WriteRegStr HKCU "Software\TortoiseSVN" Diff "$PROGRAMFILES\Beyond Compare 2\BC2.EXE"
	SectionEnd

	Section "Visual Studio 2005 TeamSuite (VISTA)"
		SectionIn 8
		ExecWait '"N:\Software\MSDN Subscriptions\Visual Studio 2005 TeamSuite\vs\Setup\setup.exe" /unattendfile N:\Software\vs2005_deployment.ini'

	SectionEnd

	Section "Visual Studio 2005 TeamSuite (XP)"
		;SectionIn 8
		ExecWait '"N:\Software\MSDN Subscriptions\Visual Studio 2005 TeamSuite\vs\Setup\setup.exe" /unattendfile N:\Software\vs2005_deployment_xp.ini'

	SectionEnd

	Section "Wait for all installers to finish"
		SectionIn 8
		ExecWait "N:\bin\BatUtil.exe WaitForExit 2000 msiexec.exe setup.exe"
	SectionEnd

	Section "VS2005 SP1 patch - XP"
		SectionIn 8
		ExecWait '"N:\Software\MSDN Subscriptions\VS 2005 SP1\VS80sp1-KB926601-X86-ENU.exe" /passive /qn'
	SectionEnd

	Section "Wait for all installers to finish"
		SectionIn 8
		ExecWait "N:\bin\BatUtil.exe WaitForExit 2000 msiexec.exe setup.exe VS80sp1-KB926601-X86-ENU.exe"
	SectionEnd

	Section "VS2005 SP1 patch - VISTA"
		SectionIn 8
		ExecWait '"N:\Software\MSDN Subscriptions\VS 2005 SP1\VS80sp1-KB932232-X86-ENU(Vista).exe" /passive /qn'
	SectionEnd

	Section "Wait for all installers to finish"
		SectionIn 8
		ExecWait "N:\bin\BatUtil.exe WaitForExit 2000 msiexec.exe setup.exe VS80sp1-KB932232-X86-ENU(Vista).exe"
	SectionEnd

	Section "VS2005 SP1 patch - VISTA (Part 2)"
		SectionIn 8
		ExecWait '"N:\Software\MSDN Subscriptions\VS 2005 SP1\VS80sp1-KB932232-X86-ENU(VistaPart2).exe" /passive /qn'
	SectionEnd

	Section "Wait for all installers to finish"
		SectionIn 8
		ExecWait "N:\bin\BatUtil.exe WaitForExit 2000 msiexec.exe setup.exe VS80sp1-KB932232-X86-ENU(Vista).exe VS80sp1-KB932232-X86-ENU(VistaPart2).exe"
	SectionEnd

	; Must be after Tortoise SVN
	Section "Get C:\SRC"
		;SectionIn 8
		ExecWait 'C:\Cryptic\tools\bin\svn checkout svn://code/dev C:\src --username brogers --password "" --no-auth-cache'
	SectionEnd

	Section "Get C:\SRC (Junctioned Programmers)"
		;SectionIn 8
		ExecWait 'C:\Cryptic\tools\bin\svn checkout svn://code/dev E:\CJUNCTIONS\src --username brogers --password "" '
	SectionEnd

	; Must be after Visual Studio 2005
	Section "IncrediBuild"
		SectionIn 9
		ExecWait "N:\Software\IncrediBuild\IBforCNS-SILENT.exe /Install /Components=Agent /Coordinator=arthur"
		WriteRegStr HKLM "Software\Xoreax\IncrediBuild\Builder" Flags "All,Beep"
	SectionEnd

	; Must be after Visual Studio 2005
	Section "Visual Assist"
		SectionIn 9
		; No silent install? :(
		ExecWait '"N:\Software\VisualAssist\VA_X_ForCrypticNewSystem.exe"'
	SectionEnd

	; Silent works
	; Must be after Visual Studio 2005
	Section "Workspace Whiz"
		SectionIn 9
		ExecWait '"N:\Software\WorkspaceWhiz\WorkspaceWhiz40VSNET_LatestCryptic.exe" /silent'
	SectionEnd

	Section "VS10 SVN External Tools (VISTA64)"
		SectionIn 9
		ExecWait 'regedit /s C:\Cryptic\tools\programmers\svn\SVNVisualStudio2010-64.reg'
	SectionEnd

	; Must be after Get C:\SRC
	Section "VS8 Team Settings"
		SectionIn 9
		ExecWait 'regedit /s C:\src\Utilities\VS8Settings\InstallSettings.reg'

		CopyFiles "$PROGRAMFILES\Microsoft Visual Studio 8\Common7\IDE\Profiles\General.vssettings" "$PROGRAMFILES\Microsoft Visual Studio 8\Common7\IDE\Profiles\General.vssettings.bak"
		CopyFiles "$PROGRAMFILES\Microsoft Visual Studio 8\Common7\IDE\Profiles\VC.vssettings" "$PROGRAMFILES\Microsoft Visual Studio 8\Common7\IDE\Profiles\General.vssettings"
		WriteRegStr HKCU "Software\Whole Tomato\Visual Assist X" ShowTipOfTheDay "No"
		ExecWait '"$PROGRAMFILES\Microsoft Visual Studio 8\Common7\IDE\devenv.exe" /Command Exit /ResetSettings "C:\src\Utilities\VS8Settings\ToolbarAndKeyboard.vssettings"'
		WriteRegStr HKCU "Software\Whole Tomato\Visual Assist X" ShowTipOfTheDay "Yes"
		CopyFiles "$PROGRAMFILES\Microsoft Visual Studio 8\Common7\IDE\Profiles\General.vssettings.bak" "$PROGRAMFILES\Microsoft Visual Studio 8\Common7\IDE\Profiles\General.vssettings"

	SectionEnd

	Section "VS8 Disable Macro Balloon"
		SectionIn 9 10
		WriteRegDWORD HKCU "Software\Microsoft\VisualStudio\8.0" DontShowMacrosBalloon 6
	SectionEnd

;	Section "PS3 Dev Tools"
;		SectionIn 9
;		MessageBox MB_OK "The PS3 Dev Tools installation will ask you for the location of the PS3/Cell SDK, which is: C:\src\3rdparty\ps3\cell\"
;		Exec 'N:\Software\PS3\ProDGforCrypticNewSystem.exe'
;	SectionEnd

;	Section "Xbox XDK"
;		SectionIn 2 3 4 5 6 7 8 
;		ExecWait "N:\Software\xdk\installForCrypticNewSystem.bat"
;	SectionEnd


SectionGroupEnd


SectionGroup "Art Stuff"

	Section "ACDSee"

		ExecWait 'N:\Software\ACDSee\setup.exe'
		SectionIn 5 6 7

	SectionEnd

	Section "3dsMax 2k9"

		ExecWait '"N:\Software\3ds Max 2009\Cryptic\AdminImage\setup.exe" /I N:\Software\3ds Max 2009\Cryptic\AdminImage\Cryptic3dsm9.ini'
		ExecWait "N:\bin\BatUtil.exe WaitForExit 2000 setup.exe"
		SectionIn 5 6 7

	SectionEnd

	Section "3dsMax 2k9 TEST"

		ExecWait 'N:\Software\3ds Max 2009\Cryptic\Cryptic3dsm9.lnk'
		ExecWait "N:\bin\BatUtil.exe WaitForExit 2000 setup.exe"
		SectionIn 5 6 7

	SectionEnd

	Section "Photoshop CS4"

		ExecWait 'N:\Software\Adobe Photoshop CS4\Adobe Photoshop CS4\Setup.exe'
		SectionIn 5 6 7

	SectionEnd

	Section "Photoshop CS3"

		ExecWait 'N:\Software\Adobe Photoshop CS3\Adobe CS3\Setup.exe'
		;SectionIn 

	SectionEnd

SectionGroupEnd