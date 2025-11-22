# Microsoft Developer Studio Project File - Name="getvrml" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=getvrml - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "getvrml.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "getvrml.mak" CFG="getvrml - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "getvrml - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "getvrml - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""$/getvrml", BAAAAAAA"
# PROP Scc_LocalPath "."
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "getvrml - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "..\..\util\"
# PROP Intermediate_Dir "..\..\util\"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /W3 /GX /Zi /Ot /Oa /Og /Oi /I "..\common" /I "." /I "../view" /I "../server" /I "../tricube" /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /D "OFFLINE" /YX /FD /c
# SUBTRACT CPP /Ox
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /profile /map /debug /machine:I386 /out:"..\..\util\getseq.exe"

!ELSEIF  "$(CFG)" == "getvrml - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /W3 /Gm /GX /ZI /Od /I "..\common" /I "." /I "../game" /I "../mapserver" /I "../tricube" /I "../game/render" /I "../glh" /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /D "OFFLINE" /FR /YX /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /out:"..\..\util\getseq.exe" /pdbtype:sept

!ENDIF 

# Begin Target

# Name "getvrml - Win32 Release"
# Name "getvrml - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\Common\Array.c
# End Source File
# Begin Source File

SOURCE=.\clip.c
# End Source File
# Begin Source File

SOURCE=..\common\error.c
# End Source File
# Begin Source File

SOURCE=..\common\file.c
# End Source File
# Begin Source File

SOURCE=..\tricube\fpcube.c
# End Source File
# Begin Source File

SOURCE=.\geo.c
# End Source File
# Begin Source File

SOURCE=..\common\grid.c
# End Source File
# Begin Source File

SOURCE=..\common\gridpoly.c
# End Source File
# Begin Source File

SOURCE=.\groupwrite.c
# End Source File
# Begin Source File

SOURCE=.\main.c
# End Source File
# Begin Source File

SOURCE=..\common\mathutil.c
# End Source File
# Begin Source File

SOURCE=..\common\mem.c
# End Source File
# Begin Source File

SOURCE=..\common\memcheck.c
# End Source File
# Begin Source File

SOURCE=..\COMMON\MemoryPool.c
# End Source File
# Begin Source File

SOURCE=.\output.c
# End Source File
# Begin Source File

SOURCE=..\tricube\pcube.c
# End Source File
# Begin Source File

SOURCE=.\poly.c
# End Source File
# Begin Source File

SOURCE=.\seq.c
# End Source File
# Begin Source File

SOURCE=..\Common\StringTable.c
# End Source File
# Begin Source File

SOURCE=.\texsort.c
# End Source File
# Begin Source File

SOURCE=..\common\token.c
# End Source File
# Begin Source File

SOURCE=.\tree.c
# End Source File
# Begin Source File

SOURCE=..\common\tricks.c
# End Source File
# Begin Source File

SOURCE=..\common\utils.c
# End Source File
# Begin Source File

SOURCE=.\vrml.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\Common\Array.h
# End Source File
# Begin Source File

SOURCE=.\clip.h
# End Source File
# Begin Source File

SOURCE=..\common\ctri.h
# End Source File
# Begin Source File

SOURCE=..\common\error.h
# End Source File
# Begin Source File

SOURCE=..\common\file.h
# End Source File
# Begin Source File

SOURCE=..\tricube\fpcube.h
# End Source File
# Begin Source File

SOURCE=.\geo.h
# End Source File
# Begin Source File

SOURCE=..\common\gfxtree.h
# End Source File
# Begin Source File

SOURCE=..\common\grid.h
# End Source File
# Begin Source File

SOURCE=..\common\gridcoll.h
# End Source File
# Begin Source File

SOURCE=..\common\gridpoly.h
# End Source File
# Begin Source File

SOURCE=..\common\mathutil.h
# End Source File
# Begin Source File

SOURCE=..\common\mem.h
# End Source File
# Begin Source File

SOURCE=..\common\memcheck.h
# End Source File
# Begin Source File

SOURCE=..\COMMON\MemoryPool.h
# End Source File
# Begin Source File

SOURCE=.\output.h
# End Source File
# Begin Source File

SOURCE=..\tricube\pcube.h
# End Source File
# Begin Source File

SOURCE=.\poly.h
# End Source File
# Begin Source File

SOURCE=.\seq.h
# End Source File
# Begin Source File

SOURCE=..\common\stdtypes.h
# End Source File
# Begin Source File

SOURCE=..\Common\StringTable.h
# End Source File
# Begin Source File

SOURCE=.\texsort.h
# End Source File
# Begin Source File

SOURCE=..\common\token.h
# End Source File
# Begin Source File

SOURCE=.\tree.h
# End Source File
# Begin Source File

SOURCE=..\common\tricks.h
# End Source File
# Begin Source File

SOURCE=..\common\utils.h
# End Source File
# Begin Source File

SOURCE=..\tricube\vec.h
# End Source File
# Begin Source File

SOURCE=.\vrml.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
