#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "core.lib")
#pragma comment(lib, "geom.lib")
#pragma comment(lib, "maxutil.lib")
#pragma comment(lib, "mesh.lib")

#include "AnimExp.h"
#include "SkelExp.h"

#if !IS_CRYPTIC_EXPORTER
int controlsInit = FALSE;

BOOL WINAPI DllMain(HINSTANCE hinstDLL,ULONG fdwReason,LPVOID lpvReserved) 
{
	hInstance = hinstDLL;

	// Initialize the custom controls. This should be done only once.
	if (!controlsInit) {
		controlsInit = TRUE;
		//InitCustomControls(hInstance);
		InitCommonControls();
	}

	return (TRUE);
}


__declspec( dllexport ) const TCHAR* LibDescription() 
{
#if IS_CRYPTIC_EXPORTER
	return _T("Cryptic Exporter");
#else
	return _T("Animation Exporter");
#endif
}

/// MUST CHANGE THIS NUMBER WHEN ADD NEW CLASS 
__declspec( dllexport ) int LibNumberClasses() 
{
	return 2;
}


__declspec( dllexport ) ClassDesc* LibClassDesc(int i) 
{
	switch(i) {
	case 0: return GetAnimExpDesc();
	break;
	case 1: return GetSkelExpDesc();
	default: return 0;
	}
}

__declspec( dllexport ) ULONG LibVersion() 
{
	return VERSION_3DSMAX;
}

// Let the plug-in register itself for deferred loading
__declspec( dllexport ) ULONG CanAutoDefer()
{
	return 1;
}
#endif

class AnimExpClassDesc:public ClassDesc {
public:
	int				IsPublic() { return 1; }
	void*			Create(BOOL loading = FALSE) { return new AnimExp; } 
#if IS_CRYTPIC_EXPORTER
	const TCHAR*	ClassName() { return _T"CryptExp"); }
#else
	const TCHAR*	ClassName() { return _T("AnimExp"); }
#endif
	SClass_ID		SuperClassID() { return SCENE_EXPORT_CLASS_ID; } 
	Class_ID		ClassID() { return AnimExp_CLASS_ID; }
	const TCHAR*	Category() { return _T("Standard"); }
};

static AnimExpClassDesc AnimExpDesc;

ClassDesc* GetAnimExpDesc()
{
	return &AnimExpDesc;
}

class SkelExpClassDesc:public ClassDesc {
public:
	int				IsPublic() { return 1; }
	void*			Create(BOOL loading = FALSE) { return new SkelExp; } 
	const TCHAR*	ClassName() { return _T("SkelExp"); }
	SClass_ID		SuperClassID() { return SCENE_EXPORT_CLASS_ID; } 
	Class_ID		ClassID() { return SkelExp_CLASS_ID; }
	const TCHAR*	Category() { return _T("Standard"); }
};

static SkelExpClassDesc SkelExpDesc;

ClassDesc* GetSkelExpDesc()
{
	return &SkelExpDesc;
}

#if !IS_CRYPTIC_EXPORTER
TCHAR *GetString(int id)
{
	static TCHAR buf[256];

	if (hInstance)
		return LoadString(hInstance, id, buf, sizeof(buf)) ? buf : NULL;

	return NULL;
}
#endif

int AnimExp::ExtCount()
{
	return 1;
}

const TCHAR * AnimExp::Ext(int n)
{
	switch(n) {
	case 0:
		// This cause a static string buffer overwrite
		// return GetString(IDS_EXTENSION1);
		return _T("danim");
	break;
	case 1:
		// This cause a static string buffer overwrite
		// return GetString(IDS_EXTENSION1);
		return _T("dscale");
	}
	return _T("");
}

const TCHAR * AnimExp::LongDesc()
{
#if IS_CRYTPIC_EXPORTER
	return _T("Cryptic Anim and VRML Exporter");
#else
	return _T("Cryptic Animation Exporter");
#endif
}

const TCHAR * AnimExp::ShortDesc()
{
#if IS_CRYTPIC_EXPORTER
	return _T("Anim and VRML Exporter");
#else
	return _T("Animation Export");
#endif
}

const TCHAR * AnimExp::AuthorName() 
{
	return _T("Cryptic Studios");
}

const TCHAR * AnimExp::CopyrightMessage() 
{
	return _T("Copyright 2000 Autodesk, Inc., 2004-2011 Cryptic Studios");
}

const TCHAR * AnimExp::OtherMessage1() 
{
	return _T("");
}

const TCHAR * AnimExp::OtherMessage2() 
{
	return _T("");
}

unsigned int AnimExp::Version()
{
	return 100;
}

int SkelExp::ExtCount()
{
	return 1;
}

const TCHAR * SkelExp::Ext(int n)
{
	switch(n) {
	case 0:
		// This cause a static string buffer overwrite
		// return GetString(IDS_EXTENSION1);
		return _T("dskel");
	}
	return _T("");
}

const TCHAR * SkelExp::LongDesc()
{
	return _T("3ds Max Skeleton/Scaling Info exporter");
}

const TCHAR * SkelExp::ShortDesc()
{
	return _T("DSkel Exporter");
}

const TCHAR * SkelExp::AuthorName() 
{
	return _T("Cryptic Studios");
}

const TCHAR * SkelExp::CopyrightMessage() 
{
	return _T("Copyright 2000 Autodesk, Inc., 2004-2011 Cryptic Studios");
}

const TCHAR * SkelExp::OtherMessage1() 
{
	return _T("");
}

const TCHAR * SkelExp::OtherMessage2() 
{
	return _T("");
}

unsigned int SkelExp::Version()
{
	return 100;
}


static INT_PTR CALLBACK AboutBoxDlgProc(HWND hWnd, UINT msg, 
										WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
case WM_INITDIALOG:
	CenterWindow(hWnd, GetParent(hWnd)); 
	break;
case WM_COMMAND:
	switch (LOWORD(wParam)) {
case IDOK:
	EndDialog(hWnd, 1);
	break;
	}
	break;
default:
	return FALSE;
	}
	return TRUE;
}       

void AnimExp::ShowAbout(HWND hWnd)
{
	DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, AboutBoxDlgProc, 0);
}

void SkelExp::ShowAbout(HWND hWnd)
{
	DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, AboutBoxDlgProc, 0);
}


/****************************************************************************

Configuration.
To make all options "sticky" across sessions, the options are read and
written to a configuration file every time the exporter is executed.

****************************************************************************/

TSTR AnimExp::GetCfgFilename()
{
	TSTR filename;

	filename += ip->GetDir(APP_PLUGCFG_DIR);
	filename += _T("\\");
	filename += CFGFILENAME;

	return filename;
}

// NOTE: Update anytime the CFG file changes
#define CFG_VERSION 0x05

BOOL AnimExp::ReadConfig()
{
	TSTR filename = GetCfgFilename();
	FILE* cfgStream;

	cfgStream = _tfopen(filename, _T("rb"));
	if (!cfgStream)
		return FALSE;

	// First item is a file version
	int fileVersion = _getw(cfgStream);

	if (fileVersion > CFG_VERSION) {
		// Unknown version
		fclose(cfgStream);
		return FALSE;
	}

	fclose(cfgStream);

	return TRUE;
}


void AnimExp::WriteConfig()
{
	TSTR filename = GetCfgFilename();
	FILE* cfgStream;

	cfgStream = _tfopen(filename, _T("wb"));
	if (!cfgStream)
		return;

	// Write CFG version
	_putw(CFG_VERSION,				cfgStream);

	fclose(cfgStream);
}


BOOL AnimExp::SupportsOptions(int ext, DWORD options) {
	assert(ext == 0);	// We only support one extension
	return(options == SCENE_EXPORT_SELECTED) ? TRUE : FALSE;
}

BOOL SkelExp::SupportsOptions(int ext, DWORD options) {
	assert(ext == 0);	// We only support one extension
	return(options == SCENE_EXPORT_SELECTED) ? TRUE : FALSE;
}

