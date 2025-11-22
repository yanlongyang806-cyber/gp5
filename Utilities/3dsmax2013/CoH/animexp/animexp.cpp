//************************************************************************** 
//* AnimExp.cpp	- Ascii File Exporter
//* 
//* By Christer Janson
//* Kinetix Development
//*
//* January 20, 1997 CCJ Initial coding
//*
//* This module contains the DLL startup functions
//*
//* Copyright (c) 1997, All Rights Reserved. 
//***************************************************************************

#define _CRT_SECURE_NO_WARNINGS

#include "AnimExp.h"

HINSTANCE hInstance;

static BOOL showPrompts;
static BOOL exportSelected;

AnimExp::AnimExp()
{
	// These are the default values that will be active when 
	// the exporter is ran the first time.
	// After the first session these options are sticky.
	bIncludeMesh = FALSE;
	bIncludeAnim = TRUE;
	bIncludeMtl =  FALSE;
	bIncludeMeshAnim =  TRUE;
	bIncludeCamLightAnim = TRUE;
	bIncludeIKJoints = TRUE;
	bIncludeNormals  =  FALSE;
	bIncludeTextureCoords = FALSE;
	bIncludeVertexColors = FALSE;
	bIncludeObjGeom = TRUE;
	bIncludeObjShape = TRUE;
	bIncludeObjCamera = TRUE;
	bIncludeObjLight = TRUE;
	bIncludeObjHelper = TRUE;
	bAlwaysSample = TRUE;
	nKeyFrameStep = 1;
	nMeshFrameStep = 1;
	nPrecision = 6;
	nStaticFrame = 0;
}

AnimExp::~AnimExp()
{
}

// Dialog proc
static INT_PTR CALLBACK ExportDlgProc(HWND hWnd, UINT msg,
									  WPARAM wParam, LPARAM lParam)
{
	Interval animRange;
	ISpinnerControl  *spin;

	AnimExp *exp = (AnimExp*)GetWindowLongPtr(hWnd,GWLP_USERDATA); 
	switch (msg) {
	case WM_INITDIALOG:
		exp = (AnimExp*)lParam;
		SetWindowLongPtr(hWnd,GWLP_USERDATA,lParam); 
		CenterWindow(hWnd, GetParent(hWnd)); 
		CheckDlgButton(hWnd, IDC_MESHDATA, exp->GetIncludeMesh()); 
		CheckDlgButton(hWnd, IDC_ANIMKEYS, exp->GetIncludeAnim()); 
		CheckDlgButton(hWnd, IDC_MATERIAL, exp->GetIncludeMtl());
		CheckDlgButton(hWnd, IDC_MESHANIM, exp->GetIncludeMeshAnim()); 
		CheckDlgButton(hWnd, IDC_CAMLIGHTANIM, exp->GetIncludeCamLightAnim()); 
#ifndef DESIGN_VER
		CheckDlgButton(hWnd, IDC_IKJOINTS, exp->GetIncludeIKJoints()); 
#endif // !DESIGN_VER
		CheckDlgButton(hWnd, IDC_NORMALS,  exp->GetIncludeNormals()); 
		CheckDlgButton(hWnd, IDC_TEXCOORDS,exp->GetIncludeTextureCoords()); 
		CheckDlgButton(hWnd, IDC_VERTEXCOLORS,exp->GetIncludeVertexColors()); 
		CheckDlgButton(hWnd, IDC_OBJ_GEOM,exp->GetIncludeObjGeom()); 
		CheckDlgButton(hWnd, IDC_OBJ_SHAPE,exp->GetIncludeObjShape()); 
		CheckDlgButton(hWnd, IDC_OBJ_CAMERA,exp->GetIncludeObjCamera()); 
		CheckDlgButton(hWnd, IDC_OBJ_LIGHT,exp->GetIncludeObjLight()); 
		CheckDlgButton(hWnd, IDC_OBJ_HELPER,exp->GetIncludeObjHelper());

		CheckRadioButton(hWnd, IDC_RADIO_USEKEYS, IDC_RADIO_SAMPLE, 
			exp->GetAlwaysSample() ? IDC_RADIO_SAMPLE : IDC_RADIO_USEKEYS);

		// Setup the spinner controls for the controller key sample rate 
		spin = GetISpinner(GetDlgItem(hWnd, IDC_CONT_STEP_SPIN)); 
		spin->LinkToEdit(GetDlgItem(hWnd,IDC_CONT_STEP), EDITTYPE_INT ); 
		spin->SetLimits(1, 100, TRUE); 
		spin->SetScale(1.0f);
		spin->SetValue(exp->GetKeyFrameStep() ,FALSE);
		ReleaseISpinner(spin);

		// Setup the spinner controls for the mesh definition sample rate 
		spin = GetISpinner(GetDlgItem(hWnd, IDC_MESH_STEP_SPIN)); 
		spin->LinkToEdit(GetDlgItem(hWnd,IDC_MESH_STEP), EDITTYPE_INT ); 
		spin->SetLimits(1, 100, TRUE); 
		spin->SetScale(1.0f);
		spin->SetValue(exp->GetMeshFrameStep() ,FALSE);
		ReleaseISpinner(spin);

		// Setup the spinner controls for the floating point precision 
		spin = GetISpinner(GetDlgItem(hWnd, IDC_PREC_SPIN)); 
		spin->LinkToEdit(GetDlgItem(hWnd,IDC_PREC), EDITTYPE_INT ); 
		spin->SetLimits(1, 10, TRUE); 
		spin->SetScale(1.0f);
		spin->SetValue(exp->GetPrecision() ,FALSE);
		ReleaseISpinner(spin);

		// Setup the spinner control for the static frame#
		// We take the frame 0 as the default value
		animRange = exp->GetInterface()->GetAnimRange();
		spin = GetISpinner(GetDlgItem(hWnd, IDC_STATIC_FRAME_SPIN)); 
		spin->LinkToEdit(GetDlgItem(hWnd,IDC_STATIC_FRAME), EDITTYPE_INT ); 
		spin->SetLimits(animRange.Start() / GetTicksPerFrame(), animRange.End() / GetTicksPerFrame(), TRUE); 
		spin->SetScale(1.0f);
		spin->SetValue(0, FALSE);
		ReleaseISpinner(spin);

		// Enable / disable mesh options
		EnableWindow(GetDlgItem(hWnd, IDC_NORMALS), exp->GetIncludeMesh());
		EnableWindow(GetDlgItem(hWnd, IDC_TEXCOORDS), exp->GetIncludeMesh());
		EnableWindow(GetDlgItem(hWnd, IDC_VERTEXCOLORS), exp->GetIncludeMesh());
		break;

	case CC_SPINNER_CHANGE:
		spin = (ISpinnerControl*)lParam; 
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
	case IDC_MESHDATA:
		// Enable / disable mesh options
		EnableWindow(GetDlgItem(hWnd, IDC_NORMALS), IsDlgButtonChecked(hWnd,
			IDC_MESHDATA));
		EnableWindow(GetDlgItem(hWnd, IDC_TEXCOORDS), IsDlgButtonChecked(hWnd,
			IDC_MESHDATA));
		EnableWindow(GetDlgItem(hWnd, IDC_VERTEXCOLORS), IsDlgButtonChecked(hWnd,
			IDC_MESHDATA));
		break;
	case IDOK:
		exp->SetIncludeMesh(IsDlgButtonChecked(hWnd, IDC_MESHDATA)); 
		exp->SetIncludeAnim(IsDlgButtonChecked(hWnd, IDC_ANIMKEYS)); 
		exp->SetIncludeMtl(IsDlgButtonChecked(hWnd, IDC_MATERIAL)); 
		exp->SetIncludeMeshAnim(IsDlgButtonChecked(hWnd, IDC_MESHANIM)); 
		exp->SetIncludeCamLightAnim(IsDlgButtonChecked(hWnd, IDC_CAMLIGHTANIM)); 
#ifndef DESIGN_VER
		exp->SetIncludeIKJoints(IsDlgButtonChecked(hWnd, IDC_IKJOINTS)); 
#endif // !DESIGN_VER
		exp->SetIncludeNormals(IsDlgButtonChecked(hWnd, IDC_NORMALS));
		exp->SetIncludeTextureCoords(IsDlgButtonChecked(hWnd, IDC_TEXCOORDS)); 
		exp->SetIncludeVertexColors(IsDlgButtonChecked(hWnd, IDC_VERTEXCOLORS)); 
		exp->SetIncludeObjGeom(IsDlgButtonChecked(hWnd, IDC_OBJ_GEOM)); 
		exp->SetIncludeObjShape(IsDlgButtonChecked(hWnd, IDC_OBJ_SHAPE)); 
		exp->SetIncludeObjCamera(IsDlgButtonChecked(hWnd, IDC_OBJ_CAMERA)); 
		exp->SetIncludeObjLight(IsDlgButtonChecked(hWnd, IDC_OBJ_LIGHT)); 
		exp->SetIncludeObjHelper(IsDlgButtonChecked(hWnd, IDC_OBJ_HELPER));
		exp->SetAlwaysSample(IsDlgButtonChecked(hWnd, IDC_RADIO_SAMPLE));

		spin = GetISpinner(GetDlgItem(hWnd, IDC_CONT_STEP_SPIN)); 
		exp->SetKeyFrameStep(spin->GetIVal()); 
		ReleaseISpinner(spin);

		spin = GetISpinner(GetDlgItem(hWnd, IDC_MESH_STEP_SPIN)); 
		exp->SetMeshFrameStep(spin->GetIVal());
		ReleaseISpinner(spin);

		spin = GetISpinner(GetDlgItem(hWnd, IDC_PREC_SPIN)); 
		exp->SetPrecision(spin->GetIVal());
		ReleaseISpinner(spin);

		spin = GetISpinner(GetDlgItem(hWnd, IDC_STATIC_FRAME_SPIN)); 
		exp->SetStaticFrame(spin->GetIVal() * GetTicksPerFrame());
		ReleaseISpinner(spin);

		EndDialog(hWnd, 1);
		break;
	case IDCANCEL:
		EndDialog(hWnd, 0);
		break;
		}
		break;
	default:
		return FALSE;
	}
	return TRUE;
}       



// Dummy function for progress bar
DWORD WINAPI fn(LPVOID arg)
{
	return(0);
}

// Start the exporter!
// This is the real entrypoint to the exporter. After the user has selected
// the filename (and he's prompted for overwrite etc.) this method is called.
int AnimExp::DoExport(const TCHAR *name,ExpInterface *ei,Interface *i, BOOL suppressPrompts, DWORD options) 
{
	// Set a global prompt display switch
	showPrompts = suppressPrompts ? FALSE : TRUE;
	exportSelected = (options & SCENE_EXPORT_SELECTED) ? TRUE : FALSE;

	// Grab the interface pointer.
	ip = i;

	// Get the options the user selected the last time
	ReadConfig();

	if (GetAsyncKeyState(VK_SHIFT) & 0x8000000 && showPrompts) {
		// Prompt when holding shift
		// Prompt the user with our dialogbox, and get all the options.
		if (!DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_ASCIIEXPORT_DLG),
			ip->GetMAXHWnd(), ExportDlgProc, (LPARAM)this))
		{
			return 1;
		}
	}
	
	// Open the stream
	pStream = _tfopen(name,_T("wb"));
	if (!pStream) {
		return 0;
	}
	int tpf = GetTicksPerFrame();
	int s = ip->GetAnimRange().Start()/tpf, 
		e = ip->GetAnimRange().End()/tpf;
	int numframes = e - s + 1;
	int header = numframes * 160;
	fwrite(&header, sizeof(header), 1, pStream);
	
	// Startup the progress bar.
	ip->ProgressStart(GetString(IDS_PROGRESS_MSG), TRUE, fn, NULL);

	// Get a total node count by traversing the scene
	// We don't really need to do this, but it doesn't take long, and
	// it is nice to have an accurate progress bar.
	nTotalNodeCount = 0;
	nCurNode = 0;
	
	int numChildren = ip->GetRootNode()->NumberOfChildren();
	
	// Call our node enumerator.
	// The nodeEnum function will recurse into itself and 
	// export each object found in the scene.
	
	for (int idx=0; idx<numChildren; idx++) {
		if (ip->GetCancel())
			break;
		nodeEnum(ip->GetRootNode()->GetChildNode(idx));
	}

	// We're done. Finish the progress bar.
	ip->ProgressEnd();

	// Close the stream
	fclose(pStream);

	// Write the current options to be used next time around.
	WriteConfig();

	return 1;
}


void AnimExp::writeFloat(float f)
{
	fwrite(&f, sizeof(f), 1, pStream);
}

void AnimExp::SampleController(INode *n, Control *c) {
	TimeValue t;
	Point3 trans;
	Matrix3 pmat;
	Interval ivalid;
	int tpf = GetTicksPerFrame();
	int s = ip->GetAnimRange().Start()/tpf, 
		e = ip->GetAnimRange().End()/tpf;

	// Sample the controller at every frame in the anim range
	for (int f = s; f <= e; f++) {
		t = f*tpf;
		ivalid = FOREVER;
		pmat = n->GetParentTM(t);
		c->GetValue(t, &pmat, ivalid, CTRL_RELATIVE);
		trans = pmat.GetTrans();
		for (int i=0; i<3; i++) {
			for (int j=0; j<3; j++) {
				writeFloat(pmat.GetRow(i)[j]);
			}
		}
		writeFloat(trans.x);
		writeFloat(trans.y);
		writeFloat(trans.z);
		writeFloat(0.f);
//		printf("Position at frame: %d of %d=(%.1f, %.1f, %.1f)\n", 
//			f, e, trans.x, trans.y, trans.z);
//		printf("rot= {{%.1f, %.1f, %.1f}, {%.1f, %.1f, %.1f}, {%.1f, %.1f, %.1f}}\n", pmat.GetRow(0)[0], pmat.GetRow(0)[1], pmat.GetRow(0)[2], pmat.GetRow(1)[0], pmat.GetRow(1)[1], pmat.GetRow(1)[2], pmat.GetRow(2)[0], pmat.GetRow(2)[1], pmat.GetRow(2)[2]);
	}
}


// This method is the main object exporter.
// It is called once of every node in the scene. The objects are
// exported as they are encoutered.

// Before recursing into the children of a node, we will export it.
// The benefit of this is that a nodes parent is always before the
// children in the resulting file. This is desired since a child's
// transformation matrix is optionally relative to the parent.

void AnimExp::dumpAnimNode(INode *node)
{
	const TSTR name = node->GetName();
	Control *c;
	char namebuf[24];
	strcpy_s(namebuf, sizeof(namebuf), name.ToCStr());
	fwrite(namebuf, 1, sizeof(namebuf), pStream);
	printf("Object %s:\n", name);
	c = node->GetTMController(); //->GetPositionController();
	SampleController(node, c);
}


BOOL AnimExp::nodeEnum(INode* node) 
{
//	if(exportSelected && node->Selected() == FALSE)
//		return TREE_CONTINUE;

	nCurNode++;
	ip->ProgressUpdate((int)((float)nCurNode/nTotalNodeCount*100.0f)); 

	// Stop recursing if the user pressed Cancel 
	if (ip->GetCancel())
		return FALSE;

	if(!exportSelected || node->Selected()) {

		// The ObjectState is a 'thing' that flows down the pipeline containing
		// all information about the object. By calling EvalWorldState() we tell
		// max to eveluate the object at end of the pipeline.
		ObjectState os = node->EvalWorldState(0); 

		// The obj member of ObjectState is the actual object we will export.
		if (os.obj) {

			// We look at the super class ID to determine the type of the object.
			switch(os.obj->SuperClassID()) {
			case GEOMOBJECT_CLASS_ID: 
				if (GetIncludeObjGeom()) dumpAnimNode(node); 
				break;
			case CAMERA_CLASS_ID:
				if (GetIncludeObjCamera()) dumpAnimNode(node); 
				break;
			case LIGHT_CLASS_ID:
				if (GetIncludeObjLight()) dumpAnimNode(node); 
				break;
			case SHAPE_CLASS_ID:
				if (GetIncludeObjShape()) dumpAnimNode(node); 
				break;
			case HELPER_CLASS_ID:
				if (GetIncludeObjHelper()) dumpAnimNode(node); 
				break;
			default:
				dumpAnimNode(node); 
				break;
			}
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
