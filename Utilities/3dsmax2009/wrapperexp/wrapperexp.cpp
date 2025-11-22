/**********************************************************************
 *<
	FILE: aiexp.cpp

	DESCRIPTION:  .AI file export module

	CREATED BY: Tom Hudson

	HISTORY: created 4 September 1998

 *>	Copyright (c) 1998, All Rights Reserved.
 **********************************************************************/

#include "Max.h"
#include "wrapperexpres.h"
#include "shape.h"

#include <stdarg.h>

char *wrappedDll = "C:\\Program Files\\Autodesk\\3dsMax8\\maxsdk\\plugin\\vrmlexp.dle";
//char *wrappedDll = "C:\\3dsmax7\\stdplugs\\vrmlexp.dle";

HINSTANCE hInstance;
HINSTANCE hWrappedDll = NULL;

#include <io.h>
#include <fcntl.h>
void newConsoleWindow()
{
	int hCrt,i;
	FILE *hf;

	AllocConsole();
	{
		// StdOut
		hCrt = _open_osfhandle(	(long) GetStdHandle(STD_OUTPUT_HANDLE),_O_TEXT);
		if (hCrt==-1) return;
		hf = _fdopen( hCrt, "w" );
		*stdout = *hf;
		// StdIn
		hCrt = _open_osfhandle(	(long) GetStdHandle(STD_INPUT_HANDLE),_O_TEXT);
		if (hCrt==-1) return;
		hf = _fdopen( hCrt, "r" );
		*stdin = *hf;
		i = setvbuf( stdout, NULL, _IONBF, 0 );
	}

}



int refcount=0;
void dllInit() {
	newConsoleWindow();
	refcount++;
	if (!hWrappedDll) {
		hWrappedDll = LoadLibrary( wrappedDll );
		if (!hWrappedDll)
		{
			printf("Failed to load %s\n", wrappedDll);
		} else {
			printf("Loaded ", wrappedDll);
		}
	}
	printf("%s: refcount %d\n", wrappedDll, refcount);
}

void dllDeinit() {
	refcount--;
	if (refcount==0 && hWrappedDll) {
		printf("Released ");
		FreeLibrary(hWrappedDll);
		hWrappedDll = NULL;
	}
	printf("%s: refcount %d\n", wrappedDll, refcount);
}

TCHAR *GetString(int id)
	{
	static TCHAR buf[256];
	if (hInstance)
		return LoadString(hInstance, id, buf, sizeof(buf)) ? buf : NULL;
	return NULL;
	}


class WrapperExport : public SceneExport {

public:
	SceneExport		*child;
	ClassDesc		*childDesc;
	static	int		layersFrom;					// Derive layers from...
					WrapperExport();
					~WrapperExport();
	int				ExtCount();					// Number of extensions supported
	const TCHAR *	Ext(int n);					// Extension #n (i.e. "3DS")
	const TCHAR *	LongDesc();					// Long ASCII description (i.e. "Autodesk 3D Studio File")
	const TCHAR *	ShortDesc();				// Short ASCII description (i.e. "3D Studio")
	const TCHAR *	AuthorName();				// ASCII Author name
	const TCHAR *	CopyrightMessage();			// ASCII Copyright message
	const TCHAR *	OtherMessage1();			// Other message #1
	const TCHAR *	OtherMessage2();			// Other message #2
	unsigned int	Version();					// Version number * 100 (i.e. v3.01 = 301)
	void			ShowAbout(HWND hWnd);		// Show DLL's "About..." box
	int				DoExport(const TCHAR *name,ExpInterface *ei,Interface *i, BOOL suppressPrompts=FALSE, DWORD options=0);	// Export file
	BOOL			SupportsOptions(int ext, DWORD options);
	};

// Max interface code

int controlsInit = FALSE;

BOOL WINAPI DllMain(HINSTANCE hinstDLL,ULONG fdwReason,LPVOID lpvReserved) {
	hInstance = hinstDLL;

	if ( !controlsInit ) {
		controlsInit = TRUE;
		
		// jaguar controls
		InitCustomControls(hInstance);

		// initialize Chicago controls
		InitCommonControls();
		}
	switch(fdwReason) {
		case DLL_PROCESS_ATTACH:
			//MessageBox(NULL,_T("WRAPPEREXP.DLL: DllMain"),_T("WRAPPEREXP"),MB_OK);
			break;
		case DLL_THREAD_ATTACH:
			break;
		case DLL_THREAD_DETACH:
			break;
		case DLL_PROCESS_DETACH:
			break;
		}
	return(TRUE);
}



typedef ClassDesc * ( __stdcall  *tLibClassDesc)( 
	int i);

ClassDesc *getChildDesc() {
	ClassDesc *child=NULL;
	dllInit();
	if (hWrappedDll) {
		tLibClassDesc pLibClassDesc = (tLibClassDesc) GetProcAddress(hWrappedDll, (LPCSTR)3);
		if (pLibClassDesc) {
			child = pLibClassDesc(0);
		}
	}
	return child;
}

void freeChildDesc(ClassDesc *child)
{
	dllDeinit();
}

//------------------------------------------------------

class WrapperClassDesc:public ClassDesc {
	public:
	int 			IsPublic() { return 1; }
	void *			Create(BOOL loading = FALSE) { return new WrapperExport; }
	const TCHAR *	ClassName() { 
		char *ret = _T("Failed to load wrapped DLL");
		ClassDesc* child = getChildDesc();
		if (child) {
			static char buf[1024];
			strcpy(buf, child->ClassName());
			ret = buf;
		}
		freeChildDesc(child);
		return ret;
	}
	SClass_ID		SuperClassID() { return SCENE_EXPORT_CLASS_ID; } // Only made for Scene Export wrappers
	Class_ID		ClassID() { // Does not fall through so you can have both a wrapped .dll and a normal as a plugin 
		return Class_ID(0x3fe36235,0x28493864);
	}
	const TCHAR* 	Category() {
		char *ret = _T("Failed to load wrapped DLL");
		ClassDesc* child = getChildDesc();
		if (child) {
			static char buf[1024];
			strcpy(buf, child->Category());
			ret = buf;
		}
		freeChildDesc(child);
		return ret;
	}
};

static WrapperClassDesc WrapperDesc;

//------------------------------------------------------
// This is the interface to Jaguar:
//------------------------------------------------------

__declspec( dllexport ) const TCHAR *
LibDescription() { return GetString(IDS_TH_AIEXPORTDLL); }

__declspec( dllexport ) int
LibNumberClasses() { return 1; }


__declspec( dllexport ) ClassDesc *
LibClassDesc(int i) {
	switch(i) {
		case 0: return &WrapperDesc; break;
		default: return 0; break;
		}
	}

// Return version so can detect obsolete DLLs
__declspec( dllexport ) ULONG 
LibVersion() { return VERSION_3DSMAX; }

// Let the plug-in register itself for deferred loading
__declspec( dllexport ) ULONG CanAutoDefer()
{
	return 1;
}

//
// .AI export module functions follow:
//

WrapperExport::WrapperExport() {
	//MessageBox(NULL,_T("WRAPPEREXP.DLL: WrapperExport()"),_T("WRAPPEREXP"),MB_OK);
	childDesc = getChildDesc();
	if (childDesc) {
		child = (SceneExport*)childDesc->Create();
	} else {
		child = NULL;
	}
}

WrapperExport::~WrapperExport() {
	if (child)
		delete child;
	freeChildDesc(childDesc);
}

int WrapperExport::ExtCount() {
	if (!child)
		return 0;
	return child->ExtCount();
}

const TCHAR *WrapperExport::Ext(int n) {		// Extensions supported for import/export modules
	if (!child)
		return _T("Failed to load wrapped DLL");
	return child->Ext(n);
}

const TCHAR *WrapperExport::LongDesc() {			// Long ASCII description (i.e. "Targa 2.0 Image File")
	static char ret[1024];
	if (!child)
		return _T("Failed to load wrapped DLL");
	sprintf(ret, "Wrapped(%s)", child->LongDesc());
	return ret;
}

const TCHAR *
WrapperExport::ShortDesc() {			// Short ASCII description (i.e. "Targa")
	static char ret[1024];
	if (!child)
		return _T("Failed to load wrapped DLL");
	sprintf(ret, "Wrapped(%s)", child->ShortDesc());
	return ret;
}

const TCHAR *WrapperExport::AuthorName() {			// ASCII Author name
	if (!child)
		return _T("Failed to load wrapped DLL");
	return child->AuthorName();
}

const TCHAR *WrapperExport::CopyrightMessage() {	// ASCII Copyright message
	if (!child)
		return _T("Failed to load wrapped DLL");
	return child->CopyrightMessage();
}

const TCHAR *WrapperExport::OtherMessage1() {		// Other message #1
	if (!child)
		return _T("Failed to load wrapped DLL");
	return child->OtherMessage1();
}

const TCHAR *WrapperExport::OtherMessage2() {		// Other message #2
	if (!child)
		return _T("Failed to load wrapped DLL");
	return child->OtherMessage2();
}

unsigned int WrapperExport::Version() {				// Version number * 100 (i.e. v3.01 = 301)
	if (!child)
		return 0;
	return child->Version();
}

void WrapperExport::ShowAbout(HWND hWnd) {			// Optional
	if (child)
		child->ShowAbout(hWnd);
 }


int WrapperExport::DoExport(const TCHAR *filename,ExpInterface *ei,Interface *gi, BOOL suppressPrompts, DWORD options)
{
	if (!child) {
		MessageBox(NULL, "Failed to load wrapped DLL", "Error", MB_OK);
		return 1;
	}
		
	return child->DoExport(filename, ei, gi, suppressPrompts, options);
}

BOOL WrapperExport::SupportsOptions(int ext, DWORD options) {
	if (child)
		return child->SupportsOptions(ext, options);
	return FALSE;
}
