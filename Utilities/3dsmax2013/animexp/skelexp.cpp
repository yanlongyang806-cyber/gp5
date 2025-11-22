#include "SkelExp.h"

HINSTANCE hSkelInstance;

static BOOL showPrompts;
static BOOL exportSelected;

SkelExp::SkelExp()
{
	// These are the default values that will be active when 
	// the exporter is ran the first time.
	// After the first session these options are sticky.
}

SkelExp::~SkelExp()
{
}

// Dialog proc
static INT_PTR CALLBACK ExportDlgProc(HWND hWnd, UINT msg,
									  WPARAM wParam, LPARAM lParam)
{
	Interval animRange;
	ISpinnerControl  *spin;

	SkelExp *exp = (SkelExp*)GetWindowLongPtr(hWnd,GWLP_USERDATA); 
	switch (msg) {
	case WM_INITDIALOG:
		exp = (SkelExp*)lParam;
		SetWindowLongPtr(hWnd,GWLP_USERDATA,lParam); 
		CenterWindow(hWnd, GetParent(hWnd)); 
// 		CheckDlgButton(hWnd, IDC_MESHDATA, exp->GetIncludeMesh()); 
// 		CheckDlgButton(hWnd, IDC_ANIMKEYS, exp->GetIncludeAnim()); 
// 		CheckDlgButton(hWnd, IDC_MATERIAL, exp->GetIncludeMtl());
// 		CheckDlgButton(hWnd, IDC_MESHANIM, exp->GetIncludeMeshAnim()); 
// 		CheckDlgButton(hWnd, IDC_CAMLIGHTANIM, exp->GetIncludeCamLightAnim()); 
// #ifndef DESIGN_VER
// 		CheckDlgButton(hWnd, IDC_IKJOINTS, exp->GetIncludeIKJoints()); 
// #endif // !DESIGN_VER
// 		CheckDlgButton(hWnd, IDC_NORMALS,  exp->GetIncludeNormals()); 
// 		CheckDlgButton(hWnd, IDC_TEXCOORDS,exp->GetIncludeTextureCoords()); 
// 		CheckDlgButton(hWnd, IDC_VERTEXCOLORS,exp->GetIncludeVertexColors()); 
// 		CheckDlgButton(hWnd, IDC_OBJ_GEOM,exp->GetIncludeObjGeom()); 
// 		CheckDlgButton(hWnd, IDC_OBJ_SHAPE,exp->GetIncludeObjShape()); 
// 		CheckDlgButton(hWnd, IDC_OBJ_CAMERA,exp->GetIncludeObjCamera()); 
// 		CheckDlgButton(hWnd, IDC_OBJ_LIGHT,exp->GetIncludeObjLight()); 
// 		CheckDlgButton(hWnd, IDC_OBJ_HELPER,exp->GetIncludeObjHelper());
// 
// 		CheckRadioButton(hWnd, IDC_RADIO_USEKEYS, IDC_RADIO_SAMPLE, 
// 			exp->GetAlwaysSample() ? IDC_RADIO_SAMPLE : IDC_RADIO_USEKEYS);
// 
// 		// Setup the spinner controls for the controller key sample rate 
// 		spin = GetISpinner(GetDlgItem(hWnd, IDC_CONT_STEP_SPIN)); 
// 		spin->LinkToEdit(GetDlgItem(hWnd,IDC_CONT_STEP), EDITTYPE_INT ); 
// 		spin->SetLimits(1, 100, TRUE); 
// 		spin->SetScale(1.0f);
// 		spin->SetValue(exp->GetKeyFrameStep() ,FALSE);
// 		ReleaseISpinner(spin);
// 
// 		// Setup the spinner controls for the mesh definition sample rate 
// 		spin = GetISpinner(GetDlgItem(hWnd, IDC_MESH_STEP_SPIN)); 
// 		spin->LinkToEdit(GetDlgItem(hWnd,IDC_MESH_STEP), EDITTYPE_INT ); 
// 		spin->SetLimits(1, 100, TRUE); 
// 		spin->SetScale(1.0f);
// 		spin->SetValue(exp->GetMeshFrameStep() ,FALSE);
// 		ReleaseISpinner(spin);
// 
// 		// Setup the spinner controls for the floating point precision 
// 		spin = GetISpinner(GetDlgItem(hWnd, IDC_PREC_SPIN)); 
// 		spin->LinkToEdit(GetDlgItem(hWnd,IDC_PREC), EDITTYPE_INT ); 
// 		spin->SetLimits(1, 10, TRUE); 
// 		spin->SetScale(1.0f);
// 		spin->SetValue(exp->GetPrecision() ,FALSE);
// 		ReleaseISpinner(spin);
// 
// 		// Setup the spinner control for the static frame#
// 		// We take the frame 0 as the default value
// 		animRange = exp->GetInterface()->GetAnimRange();
// 		spin = GetISpinner(GetDlgItem(hWnd, IDC_STATIC_FRAME_SPIN)); 
// 		spin->LinkToEdit(GetDlgItem(hWnd,IDC_STATIC_FRAME), EDITTYPE_INT ); 
// 		spin->SetLimits(animRange.Start() / GetTicksPerFrame(), animRange.End() / GetTicksPerFrame(), TRUE); 
// 		spin->SetScale(1.0f);
// 		spin->SetValue(0, FALSE);
// 		ReleaseISpinner(spin);
// 
// 		// Enable / disable mesh options
// 		EnableWindow(GetDlgItem(hWnd, IDC_NORMALS), exp->GetIncludeMesh());
// 		EnableWindow(GetDlgItem(hWnd, IDC_TEXCOORDS), exp->GetIncludeMesh());
// 		EnableWindow(GetDlgItem(hWnd, IDC_VERTEXCOLORS), exp->GetIncludeMesh());
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
// 		exp->SetIncludeMesh(IsDlgButtonChecked(hWnd, IDC_MESHDATA)); 
// 		exp->SetIncludeAnim(IsDlgButtonChecked(hWnd, IDC_ANIMKEYS)); 
// 		exp->SetIncludeMtl(IsDlgButtonChecked(hWnd, IDC_MATERIAL)); 
// 		exp->SetIncludeMeshAnim(IsDlgButtonChecked(hWnd, IDC_MESHANIM)); 
// 		exp->SetIncludeCamLightAnim(IsDlgButtonChecked(hWnd, IDC_CAMLIGHTANIM)); 
// #ifndef DESIGN_VER
// 		exp->SetIncludeIKJoints(IsDlgButtonChecked(hWnd, IDC_IKJOINTS)); 
// #endif // !DESIGN_VER
// 		exp->SetIncludeNormals(IsDlgButtonChecked(hWnd, IDC_NORMALS));
// 		exp->SetIncludeTextureCoords(IsDlgButtonChecked(hWnd, IDC_TEXCOORDS)); 
// 		exp->SetIncludeVertexColors(IsDlgButtonChecked(hWnd, IDC_VERTEXCOLORS)); 
// 		exp->SetIncludeObjGeom(IsDlgButtonChecked(hWnd, IDC_OBJ_GEOM)); 
// 		exp->SetIncludeObjShape(IsDlgButtonChecked(hWnd, IDC_OBJ_SHAPE)); 
// 		exp->SetIncludeObjCamera(IsDlgButtonChecked(hWnd, IDC_OBJ_CAMERA)); 
// 		exp->SetIncludeObjLight(IsDlgButtonChecked(hWnd, IDC_OBJ_LIGHT)); 
// 		exp->SetIncludeObjHelper(IsDlgButtonChecked(hWnd, IDC_OBJ_HELPER));
// 		exp->SetAlwaysSample(IsDlgButtonChecked(hWnd, IDC_RADIO_SAMPLE));
// 
// 		spin = GetISpinner(GetDlgItem(hWnd, IDC_CONT_STEP_SPIN)); 
// 		exp->SetKeyFrameStep(spin->GetIVal()); 
// 		ReleaseISpinner(spin);
// 
// 		spin = GetISpinner(GetDlgItem(hWnd, IDC_MESH_STEP_SPIN)); 
// 		exp->SetMeshFrameStep(spin->GetIVal());
// 		ReleaseISpinner(spin);
// 
// 		spin = GetISpinner(GetDlgItem(hWnd, IDC_PREC_SPIN)); 
// 		exp->SetPrecision(spin->GetIVal());
// 		ReleaseISpinner(spin);
// 
// 		spin = GetISpinner(GetDlgItem(hWnd, IDC_STATIC_FRAME_SPIN)); 
// 		exp->SetStaticFrame(spin->GetIVal() * GetTicksPerFrame());
// 		ReleaseISpinner(spin);

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
DWORD WINAPI sfn(LPVOID arg)
{
	return(0);
}

static TCHAR cFirstFrame[32];

/*
static char* pcFileOutBuffer;
static char* pcFileOutCursor;
int iBufSize;
int iCurSize;
int iLenOfA;
*/

//#define OUTPUT_LINE_SKEL(a) iLenOfA = strlen(a); strncpy_s(pcFileOutCursor, iBufSize - iCurSize, a, iLenOfA); iCurSize += iLenOfA; pcFileOutCursor += iLenOfA;
#define OUTPUT_LINE_SKEL(a) fwrite(a, sizeof(TCHAR), _tcslen(a), pStream); 
//#define OUTPUT_LINE_SKEL(a)



// Start the exporter!
// This is the real entrypoint to the exporter. After the user has selected
// the filename (and he's prompted for overwrite etc.) this method is called.
int SkelExp::DoExport(const TCHAR *name,ExpInterface *ei,Interface *i, BOOL suppressPrompts, DWORD options) 
{
	// Set a global prompt display switch
	showPrompts = suppressPrompts ? FALSE : TRUE;
	exportSelected = (options & SCENE_EXPORT_SELECTED) ? TRUE : FALSE;

	// Grab the interface pointer.
	ip = i;

	// Get the options the user selected the last time
	//ReadConfig();

	if (GetAsyncKeyState(VK_SHIFT) & 0x8000000 && showPrompts) {
		// Prompt when holding shift
		// Prompt the user with our dialogbox, and get all the options.
		if (!DialogBoxParam(hSkelInstance, MAKEINTRESOURCE(IDD_ASCIIEXPORT_DLG),
			ip->GetMAXHWnd(), ExportDlgProc, (LPARAM)this))
		{
			return 1;
		}
	}
	
	// Open the stream
	pStream = _tfopen(name,_T("w"));
	if (!pStream) {
		return 0;
	}
	int tpf = GetTicksPerFrame();
	int s = ip->GetAnimRange().Start()/tpf, 
		e = ip->GetAnimRange().End()/tpf;
	int numframes = e - s + 1;
	int header = numframes * 160;
	_stprintf_s(cFirstFrame, _T("%d\n"), s);
	//const char* pcBuf = "DAnim\n{\n";
	//const char* pcBuf2 = "}";
	//fwrite(pcBuf, 1, strlen(pcBuf), pStream);
	
	// Startup the progress bar.
	ip->ProgressStart(_T("Exporting file..."), TRUE, sfn, NULL);

	// Get a total node count by traversing the scene
	// We don't really need to do this, but it doesn't take long, and
	// it is nice to have an accurate progress bar.
	nTotalNodeCount = 0;
	nCurNode = 0;
	
	int numChildren = ip->GetRootNode()->NumberOfChildren();

	// first count total nodes
	nTotalNodeCount = 0;
	for (int idx=0; idx<numChildren; idx++) {
		int iCount = 0;
		if (ip->GetCancel())
			break;
		nodeEnum(ip->GetRootNode()->GetChildNode(idx), true, &iCount);
		nTotalNodeCount += iCount;
	}

	/*
	{
		const int iSizePerNode = 131072; // this should be much bigger than normal sized nodes
		iBufSize = iSizePerNode * nTotalNodeCount;
		pcFileOutBuffer = (char*)malloc(iBufSize * sizeof(char));
		*pcFileOutBuffer = 0;
		iCurSize = 0;
		pcFileOutCursor = pcFileOutBuffer;
	}
	*/
	
	// Call our node enumerator.
	// The nodeEnum function will recurse into itself and 
	// export each object found in the scene.
	
	for (int idx=0; idx<numChildren; idx++) {
		if (ip->GetCancel())
			break;
		nodeEnum(ip->GetRootNode()->GetChildNode(idx), false, NULL);
	}

	// We're done. Finish the progress bar.
	ip->ProgressEnd();

	// Close the stream
	//fwrite(pcBuf2, 1, strlen(pcBuf2), pStream);
	//fwrite(pcFileOutBuffer, 1, strlen(pcFileOutBuffer), pStream);
	fclose(pStream);
	//free(pcFileOutBuffer);

	// Write the current options to be used next time around.
	//WriteConfig();

	return 1;
}


void SkelExp::writeFloat(float f)
{
	TCHAR fBuf[128];
	_stprintf(fBuf, _T("%.30g "), f);
	//fwrite(fBuf, 1, strlen(fBuf), pStream);
	OUTPUT_LINE_SKEL(fBuf);
}

void SkelExp::SampleController(INode *n, Control *c) {
	TimeValue t;
	Point3 trans;
	Point3 scale;
	Quat rot; // thrown away in favor of aax format
	Matrix3 pmat;
	AngAxis aax;
	Interval ivalid;
	Point3 firstFrameTrans;
	int tpf = GetTicksPerFrame();
	int s = ip->GetAnimRange().Start()/tpf, 
		e = ip->GetAnimRange().End()/tpf;

	// Sample the controller at every frame in the anim range
	for (int f = s; f <= e; f++) {
		const TCHAR* pcA = _T("\tTransform\n\t{\n");
		const TCHAR* pcBX = _T("\t\tAxis ");
		const TCHAR* pcBY = _T("\n\t\tAngle ");
		const TCHAR* pcC = _T("\n\t\tTranslation ");
		const TCHAR* pcD = _T("\n\t\tScale ");
		const TCHAR* pcE = _T("\n\t}\n\n");
		OUTPUT_LINE_SKEL(pcA);
		t = f*tpf;
		ivalid = FOREVER;
		//pmat = n->GetParentTM(t);
		c->GetValue(t, &pmat, ivalid, CTRL_RELATIVE);
		DecomposeMatrix(pmat, trans, rot, scale);
		aax.Set(pmat); // we should really be calling aax.Set(rot) here instead since pmat may have scaling in it
		OUTPUT_LINE_SKEL(pcBX);
		for (int j=0; j<3; j++) {
			writeFloat(aax.axis[j]);
		}
		OUTPUT_LINE_SKEL(pcBY);
		writeFloat(aax.angle);
		OUTPUT_LINE_SKEL(pcC);
		writeFloat(trans.x);
		writeFloat(trans.y);
		writeFloat(trans.z);
		OUTPUT_LINE_SKEL(pcD);
		writeFloat(scale.x);
		writeFloat(scale.y);
		writeFloat(scale.z);
		OUTPUT_LINE_SKEL(pcE);
	}
}


// This method is the main object exporter.
// It is called once of every node in the scene. The objects are
// exported as they are encoutered.

// Before recursing into the children of a node, we will export it.
// The benefit of this is that a nodes parent is always before the
// children in the resulting file. This is desired since a child's
// transformation matrix is optionally relative to the parent.

void SkelExp::dumpSkelNode(INode *node)
{
	const TCHAR *name = node->GetName();
	Control *c;
	TCHAR namebuf[128];
	const TCHAR* pcA = _T("Bone \"");
	const TCHAR* pcB = _T("\"\n{\n\tFirstFrame ");
	const TCHAR* pcC = _T("}\n\n");
	_tcsncpy_s(namebuf, _tcslen(namebuf), pcA, _tcslen(pcA));
	_tcsncat_s(namebuf, _tcslen(namebuf), name, _tcslen(name));
	_tcsncat_s(namebuf, _tcslen(namebuf), pcB, _tcslen(pcB));
	_tcsncat_s(namebuf, _tcslen(namebuf), cFirstFrame, _tcslen(cFirstFrame));
	OUTPUT_LINE_SKEL(namebuf);
//	printf("Object %s:\n", name);
	c = node->GetTMController(); //->GetPositionController();
	SampleController(node, c);
	_tcsncpy_s(namebuf, _tcslen(namebuf), pcC, _tcslen(pcC));
	OUTPUT_LINE_SKEL(namebuf);
}


BOOL SkelExp::nodeEnum(INode* node, bool bCount, int* piCount) 
{
//	if(exportSelected && node->Selected() == FALSE)
//		return TREE_CONTINUE;


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
			if (piCount)
				(*piCount)++;
			if (!bCount)
			{
				nCurNode++;
				ip->ProgressUpdate((int)((float)nCurNode/nTotalNodeCount*100.0f)); 

				// We look at the super class ID to determine the type of the object.
				dumpSkelNode(node); 
			}
		}
	}	
	
	// For each child of this node, we recurse into ourselves 
	// until no more children are found.
	for (int c = 0; c < node->NumberOfChildren(); c++) {
		int iCount = 0;
		if (!nodeEnum(node->GetChildNode(c), bCount, &iCount))
			return FALSE;
		if (bCount && piCount)
			*piCount += iCount;
	}

	return TRUE;
}