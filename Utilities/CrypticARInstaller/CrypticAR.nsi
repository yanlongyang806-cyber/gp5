!include util.nsh

; The name of the installer
Name "Cryptic Studios Animation Rig Installer"

; The file to write
OutFile "CrypticARSetup.exe"
ComponentText "This will set up the Cryptic Studios Animation Rig on your system."
InstallDir "$PROGRAMFILES\Cryptic AR\"
Var 3DSINSTALL
Var STARTMENU_FOLDER


Function PreDirectoryFunc
	ReadRegStr $0 HKLM "SOFTWARE\Autodesk\3dsMax\8.0" "Installdir"
	StrCmp $0 "" continue_finding_max done_looking
	continue_finding_max:
	ReadRegStr $0 HKLM "SOFTWARE\Autodesk\3dsMax\9.0" "Installdir"
	StrCmp $0 "" continue_finding_max2 done_looking
	continue_finding_max2:
	ReadRegStr $0 HKLM "SOFTWARE\Autodesk\3dsMax\9.0\MAX-1:409" "Installdir" ; this is where it was when i installed it
	StrCmp $0 "" couldnt_find_max done_looking
	couldnt_find_max:
	MessageBox MB_OK "Could not find 3dsMax.  Please select 3dsMax install directory (where 3dsmax.exe is located)."
	done_looking:
	StrCpy $3DSINSTALL "$0"
FunctionEnd

; install pages
Page directory
PageEx directory 
	DirVar $3DSINSTALL
	PageCallbacks PreDirectoryFunc
PageExEnd
Page components
Page instfiles

; uninstall pages
UninstPage uninstConfirm
UninstPage instfiles

Section "Cryptic AR"

	ReadRegStr $R1 HKCU "SOFTWARE\Cryptic\CrypticAR" "Installation Directory"
	StrCmp $R1 "" already_installed continue_installing

already_installed:

	MessageBox MB_YESNO "Cryptic AR is already installed.  Continue anyway?" IDYES continue_installing IDNO dont_install

continue_installing:

	WriteRegStr HKCU "SOFTWARE\Cryptic\CrypticAR" "Installation Directory" $INSTDIR
	WriteRegStr HKCU "SOFTWARE\Cryptic\CrypticAR" "3dsMax Directory" $3DSINSTALL

; first put everything in the install directory
	SetOutPath $INSTDIR
	File /r /x .svn "Cryptic AR\*.*" 
	WriteUninstaller "uninstall.exe"

; then put all the stuff where it belongs in the max directory

	; everything in the scripts directory
	StrCpy $1 "$3DSINSTALL\Scripts\Cryptic AR"
	SetOutPath $1
	File "Cryptic AR\Scripts\Cryptic AR\*.*"

	; everything in the ui\macroscripts directory
	StrCpy $1 "$3DSINSTALL\UI\Macroscripts"
	SetOutPath $1
	File "Cryptic AR\UI\Macroscripts\crypt_load.mcr"

dont_install:

SectionEnd

Section "Start Menu Shortcuts"
	StrCpy $STARTMENU_FOLDER "Cryptic AR"
	WriteRegStr HKCU "SOFTWARE\Cryptic\CrypticAR" "Start Menu Folder" $STARTMENU_FOLDER
	CreateDirectory "$SMPROGRAMS\$STARTMENU_FOLDER"
	CreateShortCut "$SMPROGRAMS\$STARTMENU_FOLDER\Cryptic AR.lnk" "$INSTDIR\Cryptic AR.max" "" "$3DSINSTALL\3dsmax.exe" 1
	CreateShortCut "$SMPROGRAMS\$STARTMENU_FOLDER\Cryptic AR - ID Template.lnk" "$INSTDIR\Cryptic AR - ID Template.max" "" "$3DSINSTALL\3dsmax.exe" 1
	CreateShortCut "$SMPROGRAMS\$STARTMENU_FOLDER\gpl.lnk" "$INSTDIR\gpl.txt"
	CreateShortCut "$SMPROGRAMS\$STARTMENU_FOLDER\uninstall.lnk" "$INSTDIR\uninstall.exe"
SectionEnd

Section "Add to Path"
	StrCpy $1 "$3DSINSTALL\Scripts\Cryptic AR"
	Push $1
	Call AddToPath
SectionEnd


Section "Uninstall"
	ReadRegStr $R1 HKCU "SOFTWARE\Cryptic\CrypticAR" "Installation Directory"
	StrCmp $R1 "" done_uninstalling

	Delete "$R1\uninstall.exe"
	Delete "$R1\Cryptic AR.max"
	Delete "$R1\Cryptic AR - ID Template.max"
	Delete "$R1\gpl.txt"
	RMDir /r "$R1" ; remove anything else

	Push $INSTDIR
	Call un.RemoveFromPath

	; delete stuff in 3dsMax
	ReadRegStr $R1 HKCU "SOFTWARE\Cryptic\CrypticAR" "3dsMax Directory"
	RMDir /r "$R1\Scripts\Cryptic AR"
	Delete "$R1\UI\Macroscripts\crypt_load.mcr"

	; delete startmenu shortcuts
	ReadRegStr $STARTMENU_FOLDER HKCU "SOFTWARE\Cryptic\CrypticAR" "Start Menu Folder"
	StrCmp $STARTMENU_FOLDER "" skip_uninstall_startmenu
	Delete "$SMPROGRAMS\$STARTMENU_FOLDER\uninstall.lnk"
	Delete "$SMPROGRAMS\$STARTMENU_FOLDER\Cryptic AR.lnk"
	Delete "$SMPROGRAMS\$STARTMENU_FOLDER\Cryptic AR - ID Template.lnk"
	Delete "$SMPROGRAMS\$STARTMENU_FOLDER\gpl.lnk"
	RMDir "$SMPROGRAMS\$STARTMENU_FOLDER"
	skip_uninstall_startmenu:

done_uninstalling:

SectionEnd