#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "bmm.lib")
#pragma comment(lib, "core.lib")
#pragma comment(lib, "geom.lib")
#pragma comment(lib, "gfx.lib")
#pragma comment(lib, "helpsys.lib")
#pragma comment(lib, "maxutil.lib")
#pragma comment(lib, "mesh.lib")
#pragma comment(lib, "maxscrpt.lib")

#include "MAXScrpt.h" 
#include "Numbers.h"// for handling of MAXScript Integers and Floats 
#include "Arrays.h"// for returning a MAXScript array
#include "strings.h"

#include <time.h>
#include "vrml.h"
#include "simpobj.h"
#include "istdplug.h"
#include "inline.h"
#include "lod.h"
#include "inlist.h"
#include "notetrck.h"
#include "bookmark.h"
#include "stdmat.h"
#include "normtab.h"
#include "vrml_api.h"
#include "vrmlexp.h"
#include "appd.h"
#include "timer.h"
#include "navinfo.h"
#include "backgrnd.h"
#include "fog.h"
#include "sound.h"
#include "touch.h"
#include "prox.h"
#include "vrml2.h"
#include "helpsys.h"


// for danim and skel files
#include "../animexp/animexp.h"
#include "../animexp/skelexp.h"

#include "3dsmaxport.h"


void VRMLMaxScriptInit()
{
	//MessageBox(NULL, _T("Plugin Loaded"), _T("Testing!"), MB_OK);
}

// Declare C++ function and register it with MAXScript 
#include "definsfn.h" 
def_visible_primitive(ExportVRML, "ExportVRML"); 

def_visible_primitive(ExportDANIM, "ExportDANIM"); 

def_visible_primitive(ExportSKEL, "ExportSKEL"); 


///
///
///   This is the function exposed to MaxScript. It accepts two syntaxs:
///   
///   ExportVRML(string filename)
///   and
///   ExportVRML(string filename, bool exportSelected, bool exportHidden, bool exportVertexColors)
///
///
Value* ExportVRML_cf(Value **arg_list, int count) 
{ 
	String* success = new String("Export successful.");
	
	if((count == 1 && is_string(arg_list[0])) || (count == 4 && (is_string(arg_list[0]) && is_bool(arg_list[1]) && is_bool(arg_list[2]) && is_bool(arg_list[3]))))
	{
		VRBLExport* expObject = new VRBLExport();

		Interface* ip = GetCOREInterface();
		
		// initialize options for exporter...
		DWORD options = 0;

		if(count == 4)
		{
			if(arg_list[1]->to_bool())
				options = options | SCENE_EXPORT_SELECTED;
			expObject->SetExportHidden(arg_list[2]->to_bool());
			expObject->SetExportVertexColors(arg_list[3]->to_bool());
		}

		// set default digits
		expObject->SetDigits(6);

		int result = expObject->DoExport(arg_list[0]->to_string(), NULL, ip, TRUE, options);

		delete expObject;

		if(result == 1)
		{
			return success;
		}
		else
		{
			throw RuntimeError("Error: Problem during export!");
		}
	}
	else
	{
		throw RuntimeError("Invalid arguments. Syntax is \"ExportVRML (string filename)\" or \"ExportVRML (string filename) (bool exportSelected) (bool exportHidden) (bool exportVertexColors)\".");
	}
	
}

//////////////////////////////////////////////////////////////////////////

/// The animation export function accepts only a filename

Value* ExportDANIM_cf(Value **arg_list, int count) 
{ 
	String* success = new String("Export successful.");

	if(count == 1 && is_string(arg_list[0]))
	{
		AnimExp* expObject = new AnimExp();

		Interface* ip = GetCOREInterface();

		// initialize options for exporter...
		DWORD options = 0;

		int result = expObject->DoExport(arg_list[0]->to_string(), NULL, ip, TRUE, options);

		delete expObject;

		if(result == 1)
		{
			return success;
		}
		else
		{
			throw RuntimeError("Error: Problem during export!");
		}
	}
	else
	{
		throw RuntimeError("Invalid arguments. Syntax is \"ExportDANIM (string filename)\".");
	}

}

//////////////////////////////////////////////////////////////////////////

/// This is actually useless (not used), but I didn't figure that out until I was done adding it... so here it stays just in case.

Value* ExportSKEL_cf(Value **arg_list, int count) 
{ 
	String* success = new String("Export successful.");

	if(count == 1 && is_string(arg_list[0]))
	{
		SkelExp* expObject = new SkelExp();

		Interface* ip = GetCOREInterface();

		// initialize options for exporter...
		DWORD options = 0;

		int result = expObject->DoExport(arg_list[0]->to_string(), NULL, ip, TRUE, options);

		delete expObject;

		if(result == 1)
		{
			return success;
		}
		else
		{
			throw RuntimeError("Error: Problem during export!");
		}
	}
	else
	{
		throw RuntimeError("Invalid arguments. Syntax is \"ExportVRML (string filename)\".");
	}
}