#ifndef __SkelExp__H
#define __SkelExp__H

#define _CRT_SECURE_NO_DEPRECATE 1

#include "Max.h"
#include "resource.h"
#include "istdplug.h"
#include "stdmat.h"
#include "decomp.h"
#include "shape.h"
#include "interpik.h"

#include "asciitok.h"


extern ClassDesc* GetSkelExpDesc();
extern TCHAR *GetString(int id);
extern HINSTANCE hInstance;

#define VERSION			200			// Version number * 100
//#define FLOAT_OUTPUT	_T("%4.4f")	// Float precision for output

// This is the main class for the exporter.

class SkelExp : public SceneExport {
public:
	SkelExp();
	~SkelExp();

	// SceneExport methods
	int    ExtCount();     // Number of extensions supported 
	const TCHAR * Ext(int n);     // Extension #n (i.e. "ASC")
	const TCHAR * LongDesc();     // Long ASCII description (i.e. "Ascii Export") 
	const TCHAR * ShortDesc();    // Short ASCII description (i.e. "Ascii")
	const TCHAR * AuthorName();    // ASCII Author name
	const TCHAR * CopyrightMessage();   // ASCII Copyright message 
	const TCHAR * OtherMessage1();   // Other message #1
	const TCHAR * OtherMessage2();   // Other message #2
	unsigned int Version();     // Version number * 100 (i.e. v3.01 = 301) 
	void	ShowAbout(HWND hWnd);  // Show DLL's "About..." box
	int		DoExport(const TCHAR *name,ExpInterface *ei,Interface *i, BOOL suppressPrompts=FALSE, DWORD options=0); // Export	file
	BOOL	SupportsOptions(int ext, DWORD options);

	// Other methods
	void	SampleController(INode *n, Control *c);
	void	dumpSkelNode(INode *node);
	void	writeFloat(float f);

	// Node enumeration
	BOOL	nodeEnum(INode* node, bool bCount, int* piCount);

	// Configuration methods
	TSTR	GetCfgFilename();
	BOOL	ReadConfig();
	void	WriteConfig();
	
	// Interface to member variables
	inline Interface*	GetInterface()		{ return ip; }

private:
	Interface*	ip;
	FILE*		pStream;
	int			nTotalNodeCount;
	int			nCurNode;

	//MtlKeeper	mtlList;
	const static int	endOfField = 0x00000004;
};

// Class ID. These must be unique and randomly generated!!
// If you use this as a sample project, this is the first thing
// you should change!
#define SkelExp_CLASS_ID	Class_ID(0x19522210, 0x422111c1)


#endif // __SkelExp__H

