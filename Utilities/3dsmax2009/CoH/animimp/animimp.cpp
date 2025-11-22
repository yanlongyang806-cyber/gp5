#define _CRT_SECURE_NO_WARNINGS

#include "AnimImp.h"

HINSTANCE hInstance;


int controlsInit = FALSE;

BOOL WINAPI DllMain(HINSTANCE hinstDLL,ULONG fdwReason,LPVOID lpvReserved) {
	hInstance = hinstDLL;

	if ( !controlsInit ) {
		controlsInit = TRUE;

		// initialize Chicago controls
		InitCommonControls();
	}
	switch(fdwReason) {
		case DLL_PROCESS_ATTACH:
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


//------------------------------------------------------

class AnimImpClassDesc:public ClassDesc {
public:
	int 			IsPublic() { return 1; }
	void *			Create(BOOL loading = FALSE) { return new AnimImport; }
	const TCHAR *	ClassName() { return "AnimImport"; }
	SClass_ID		SuperClassID() { return SCENE_IMPORT_CLASS_ID; }
	Class_ID		ClassID() { return AnimExp_CLASS_ID; }
	const TCHAR* 	Category() { return "Scene Import"; }
};

static AnimImpClassDesc AnimImpDesc;

//------------------------------------------------------
// This is the interface to Jaguar:
//------------------------------------------------------

__declspec( dllexport ) const TCHAR *
LibDescription() { return ".anim import"; }

__declspec( dllexport ) int
LibNumberClasses() { return 1; }

__declspec( dllexport ) ClassDesc *
LibClassDesc(int i) {
	switch(i) {
		case 0: return &AnimImpDesc; break;
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
// .3DS import module functions follow:
//

AnimImport::AnimImport() {
	numAnimRecords=0;
	records=0;
	numFrames=0;
}

AnimImport::~AnimImport() {
	if (records)
		delete[] records;
}

int
AnimImport::ExtCount() {
	return 1;
}

const TCHAR *
AnimImport::Ext(int n) {		// Extensions supported for import/export modules
	switch(n) {
		case 0:
			return _T("ANIM");
	}
	return _T("");
}

const TCHAR *
AnimImport::LongDesc() {			// Long ASCII description (i.e. "Targa 2.0 Image File")
	return "3ds max Animation Importer";
}

const TCHAR *
AnimImport::ShortDesc() {			// Short ASCII description (i.e. "Targa")
	return "Animation Import";
}

const TCHAR *
AnimImport::AuthorName() {			// ASCII Author name
	return "Cryptic Studios";
}

const TCHAR *
AnimImport::CopyrightMessage() {	// ASCII Copyright message
	return "Copyright 2004 Cryptic Studios";
}

const TCHAR *
AnimImport::OtherMessage1() {		// Other message #1
	return _T("");
}

const TCHAR *
AnimImport::OtherMessage2() {		// Other message #2
	return _T("");
}

unsigned int
AnimImport::Version() {				// Version number * 100 (i.e. v3.01 = 301)
	return 100;
}

void
AnimImport::ShowAbout(HWND hWnd) {			// Optional
}



//void AnimImport::dumpAnimNode(INode *node)
//{
//	Control *c;
//	char namebuf[24];
//	strncpy(namebuf, name, sizeof(namebuf)-1);
//	fwrite(namebuf, 1, sizeof(namebuf), pStream);
//	printf("Object %s:\n", name);
//	c = node->GetTMController()->GetPositionController();
//	SampleController(node, c);
//}


BOOL AnimImport::nodeEnum(INode* node) 
{
	char *name = node->GetName();
	AnimRecord *record = findAnimRecord(name);
	int tpf = GetTicksPerFrame();
	int c = ip->GetTime()/tpf;
	//printf("node: %s\n", name);

	// Stop recursing if the user pressed Cancel 
	if (ip->GetCancel())
		return FALSE;

	if (!record) {
		//printf("  (No animation found)\n");
	} else {
		for (int i=0; i<numFrames; i++) {
			TimeValue t = (c+i)*tpf;
			node->SetNodeTM(t, record->frames[i]);
		}
	}

	// For each child of this node, we recurse into ourselves 
	// until no more children are found.
	for (int c = 0; c < node->NumberOfChildren(); c++) {
		if (!nodeEnum(node->GetChildNode(c)))
			return FALSE;
	}
	return TRUE;
}


float AnimImport::readFloat(void)
{
	float f;
	fread(&f, 4, 1, pStream);
	return f;
}


int AnimImport::readAnimRecord(AnimRecord *record)
{
	fread(record->name, 24, 1, pStream);
	record->name[24] = 0;
	record->frames = new Matrix3[numFrames];
	for (int i=0; i<numFrames; i++) {
		Matrix3 &m=record->frames[i];
		for (int i=0; i<3; i++) {
			Point3 p;
			for (int j=0; j<3; j++) {
				p[j] = readFloat();
			}
			m.SetRow(i, p);
		}
		for (int i=0; i<3; i++) {
			m.SetTrans(i, readFloat());
		}
		readFloat(); // Probably 0
	}
	return 1;
}


int AnimImport::loadAnimFile(const TCHAR *name)
{
	int v;
	size_t fsize;
	pStream = _tfopen(name,_T("rb"));
	if (!pStream) {
		printf("File load failed\n");
		return 0;
	}
	fseek(pStream, 0, SEEK_END);
	fsize = ftell(pStream);
	fseek(pStream, 0, SEEK_SET);
	printf("File load succeeded (%d bytes)\n", fsize);

	numFrames=0;
	fread(&v, 4, 1, pStream);
	numFrames = v / 160;
	// Calculate number of records
	fsize/=4; // Convert to words
	fsize-=1; // Subtract header
	int blocksize = numFrames * 13 + 6;
	int remainder = fsize % blocksize;
	if (remainder) {
		MessageBox(ip->GetMAXHWnd(),"Corrupt .anim file?","Error",MB_OK);
		fclose(pStream);
		return 0;
	}
	numAnimRecords = fsize / blocksize;

	records = new AnimRecord[numAnimRecords];
	// Read it!
	for (int i=0; i<numAnimRecords; i++) {
		readAnimRecord(&records[i]);
	}

	printf("Read %d records, %d frames\n", numAnimRecords, numFrames);
	fclose(pStream);
	return 1;
}

AnimRecord *AnimImport::findAnimRecord(const char *name)
{
	for (int i=0; i<numAnimRecords; i++) {
		if (_stricmp(records[i].name, name)==0) {
			return &records[i];
		}
	}
	return NULL;
}

int AnimImport::DoImport(const TCHAR *name,ImpInterface *ii,Interface *i, BOOL suppressPrompts)
{
	ip = i;

	int tpf = GetTicksPerFrame();
	int s = ip->GetAnimRange().Start()/tpf, 
		e = ip->GetAnimRange().End()/tpf,
		c = ip->GetTime()/tpf;

	//MessageBox(NULL,"Import","Import",MB_OK);
	if (0==loadAnimFile(name))
		return IMPEXP_FAIL;

	if (numFrames == 0)
		return IMPEXP_FAIL;

	SuspendAnimate();
	AnimateOn();
#define MAX(a,b) (((a)>(b))?(a):(b))
	e = MAX(e, c+numFrames-1);
	ip->SetAnimRange(Interval(s*GetTicksPerFrame(), e*GetTicksPerFrame()));
	int numChildren = ip->GetRootNode()->NumberOfChildren();
	for (int idx=0; idx<numChildren; idx++) {
		if (ip->GetCancel())
			break;
		nodeEnum(ip->GetRootNode()->GetChildNode(idx));
	}

	ResumeAnimate();

	return IMPEXP_SUCCESS;
}
