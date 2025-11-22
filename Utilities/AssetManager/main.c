
#include "error.h"
#include "logging.h"
#include "mathutil.h"
#include "earray.h"
#include "sysutil.h"
#include "utils.h"
#include "file.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "FolderCache.h"
#include "process_util.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "cmdparse.h"
#include "MemoryMonitor.h"
#include "winutil.h"
#include "AppRegCache.h"
#include "utilitiesLib.h"
#include "gimmeDLLWrapper.h"
#include "DebugState.h"
#include "Color.h"
#include "BitStream.h" // for bsAssertOnErrors
#include "EditLibState.h"
#include "MemoryBudget.h"
#include "EditorManager.h"
#include "WorldLib.h"
#include "WorldGrid.h"
#include "wlTime.h"
#include "GraphicsLib.h"
#include "GfxCamera.h"
#include "GfxSettings.h"
#include "GfxConsole.h"
#include "GfxDebug.h"
#include "GfxMaterials.h"
#include "inputLib.h"
#include "inputKeyBind.h"
#include "EditLib.h"
#include "MaterialEditor.h"
#include "soundLib.h"
#include "uiInternal.h"
#include "Materials.h"
#include "AutoStartupSupport.h"
#include "ResourceManager.h"
#include "ProductInfo.h"
#include "../../CrossRoads/GameClientLib/gclUtils.h"
#include "GameClientLib.h"
#include "timing_profiler_interface.h"

#define	PHYSX_SRC_FOLDER "../../3rdparty"
#include "PhysicsSDK.h"

#include "RdrStandardDevice.h"
#include "RenderLib.h"
#include "XRenderLib.h"

#ifndef _XBOX
#include "resource.h"
#endif

#define WINDOW_NAME "AssetManager"
#define WINDOW_TITLE "Asset Manager"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static DeviceDesc *primary_device;
static int no_tools;
static char *emulate_cmd = "";
static int start_mated;
static char *mated2FileToOpen;

void libTest(HINSTANCE hInstance, LPSTR lpCmdLine, const void *logo_jpg_data, int logo_data_size);
int assetManagerPreMain(HINSTANCE hInstance, LPSTR lpCmdLine);

//////////////////////////////////////////////////////////////////////////

AUTO_RUN_EARLY;
void assetManagerGlobalInit(void)
{
	if (IsDebuggerPresent())
		memBudgetSetRequirement(MemBudget_Required_PopupForProgrammers);
	pOverridePreMain = assetManagerPreMain;
	pOverrideMain = libTest;
	gGCLState.logoResource = IDR_CRYPTIC_LOGO;
}

//////////////////////////////////////////////////////////////////////////

static int switch_modes_mode, switch_modes;

static void quitProgram(void)
{
	utilitiesLibSetShouldQuit(true);
}

static void updateThreadPriority(void)
{
#if !PLATFORM_CONSOLE
	int curActive = isProductionMode() && primary_device && GetForegroundWindow() == rdrGetWindowHandle(primary_device->device);

	if(curActive)
	{
		SetPriorityClass(GetCurrentProcess(),NORMAL_PRIORITY_CLASS);
	}
	else
	{
		SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);
	}
#endif
}

static bool createPrimaryDevice(HINSTANCE hInstance)
{
	WindowCreateParams params={0};

	SAFE_FREE(primary_device);
	primary_device = calloc(sizeof(*primary_device),1);

	gfxSettingsSetAppDefault(false, 1024, 768);

	gfxGetWindowSettings(&params);

	params.threaded = 0;

	primary_device->device = rdrCreateDevice(&params, hInstance, 2);

	if (!primary_device->device)
	{
		SAFE_FREE(primary_device);
		return false;
	}

	return true;
}

static void registerPrimaryDevice(HINSTANCE hInstance)
{
	InputDevice *inpdev;
	if (!primary_device || !primary_device->device)
		return;
	inpdev = inpCreateInputDevice(primary_device->device,hInstance,keybind_ExecuteKey, false);
	rdrSetTitle(primary_device->device, WINDOW_TITLE);
	gfxRegisterDevice(primary_device->device, inpdev, true);
	gfxInitCameraController(&primary_device->freecamera, gfxFreeCamFunc, NULL);
	primary_device->activecamera = &primary_device->freecamera;
}


// Switch to edit mode
AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void EditModeAM(int on)
{
	switch_modes = 1;
	switch_modes_mode = on;
}

// Switch to edit mode
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(emulate_outsource) ACMD_CMDLINE;
void EmulateOutsource(int on)
{
	if (on)
	{
		gimmeDLLDisable(true);
		emulate_cmd = "-emulate_outsource";
	}
}

// Start in the material editor
AUTO_CMD_INT(start_mated,StartMatEd) ACMD_ACCESSLEVEL(0);

// indicates that getVrml and getTex should not be run
AUTO_CMD_INT(no_tools,NoTools) ACMD_ACCESSLEVEL(0);


int assetManagerPreMain(HINSTANCE hInstance, LPSTR lpCmdLine)
{
	loadstart_report_unaccounted(true);
	memCheckInit();
	newConsoleWindow();
	showConsoleWindow();
	consoleUpSize(200, 25);
	preloadDLLs(0);
	winSetHInstance(hInstance);
	SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);
	setDefaultAssertMode();
	gimmeDLLDisable(1);
	if (lpCmdLine && strstriConst(lpCmdLine, "-emulate_outsource")) {
		gimmeDLLDisable(true);
		setProductionClientAssertMode();
	}
	
	loadstart_printf("AssetManager Starting up...");

	gfxSettingsSetAppDefault(false, 1024, 768);

	regSetAppName("AssetManager");
	FolderCacheChooseMode();
	g_do_not_try_to_load_folder_cache = false;
	fileAutoDataDir();
	{
		LoadedProductInfo *productInfo;
		productInfo = GetProductNameFromDataFile();
		if (productInfo->productName[0])
		{
			SetProductName(productInfo->productName, productInfo->shortProductName);
		}
	}
	
	// Set to I_LIKE_PIGS for the outsource build
	if (!gimmeDLLQueryExists() || strStartsWith(gimmeDLLQueryBranchName(fileDataDir()), "Can't find source control"))
	{
		gimmeDLLDisable(true);
		emulate_cmd = "-emulate_outsource";
		FolderCacheSetMode(FOLDER_CACHE_MODE_I_LIKE_PIGS);
		setProductionClientAssertMode();
	}
	//FolderCacheEnableCallbacks(0);
	FolderCacheSetManualCallbackMode(1);
	FolderCacheStartMonitorThread();

	ErrorfSetCallback(gclErrorfCallback, NULL);
	FatalErrorfSetCallback(gclFatalErrorfCallback, NULL);
	bsAssertOnErrors(true);
	disableRtlHeapChecking(NULL);

	logSetDir("GameClient");
	errorLogStart();
	return true;
}

AUTO_STARTUP(AssetManager) ASTRT_DEPS(GraphicsLib, WorldLib, UILib, Messages, WorldLibZone);
void amStartup(void)
{

}

AUTO_COMMAND ACMD_COMMANDLINE;
void mated2Open( const char* fileName )
{
	mated2FileToOpen = strdup( fileName );
	start_mated = true;
}

//////////////////////////////////////////////////////////////////////////

void libTest(HINSTANCE hInstance, LPSTR lpCmdLine, const void *logo_jpg_data, int logo_data_size)
{
	bool okToQuit;
	FrameLockedTimer* flt;
	int argc;
	char *argv[1000];
	char buf[1000]={0};

	loadCmdline("./cmdline.txt",buf,sizeof(buf));
#ifndef _XBOX
	strcat(buf," ");
	strcat(buf,lpCmdLine);
#endif
	argv[0] = "file.exe";
	argc = 1 + tokenize_line_quoted_safe(buf,&argv[1],ARRAY_SIZE(argv)-1,0);

	updateThreadPriority();

	winSetHInstance(hInstance);
	materialShowExtraWarnings(true);

	utilitiesLibStartup();

	filePrintDataDirs();

	memBudgetStartup();

	keybind_Init(printf, NULL, quitProgram, "FreeCamera");

	rdr_state.backgroundShaderCompile = 0; // Disabling until issues fixed.

	cmdParseCommandLine(argc, argv);

	if (!createPrimaryDevice(hInstance))
		FatalErrorf("Failed to create render device!");

	gfxDisplayLogo(primary_device->device, logo_jpg_data, logo_data_size, WINDOW_TITLE);

	if (start_mated)
		// I need:
		// * Materials
		// * Models (for preview)
		// * Profiles (for physical properties)
		wlSetLoadFlags(WL_NO_LOAD_DYNFX|WL_NO_LOAD_DYNANIMATIONS|WL_NO_LOAD_COSTUMES|WL_NO_LOAD_DYNMISC);
	else
		wlSetLoadFlags(0);
	gfxSetFeatures(GFEATURE_SHADOWS|GFEATURE_POSTPROCESSING|GFEATURE_OUTLINING|GFEATURE_DOF);

	AutoStartup_SetTaskIsOn("AssetManager", 1);
	DoAutoStartup();
	resFinishLoading();

	keybind_Init(conPrintf, NULL, quitProgram, "AssetManager");

	keybind_BindKeyInUserProfile("e", "editModeAM 1");

	emSetOutsourceMode(true);

	registerPrimaryDevice(hInstance);
	gfxMaterialFlagPreloadedAllTemplates();
	gfxNoErrorOnNonPreloadedInternal(true);

#if !PSDK_DISABLED
	psdkInit(1);
#endif

#ifdef _XBOX
	FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY); // CD: folder cache does not get updated on xbox yet
#endif
	FolderCacheEnableCallbacks(1);

	editLibStartup(0);

#ifndef _XBOX
	if (!no_tools && !gimmeDLLQueryExists() && !ProcessCount("getvrml.exe", true))
	{
		char cwd[MAX_PATH];
		char srcdir[MAX_PATH];
		char cmdline[MAX_PATH + 256];
		char *s;

		fileGetcwd(cwd, ARRAY_SIZE_CHECKED(cwd)-1);
		strcpy(srcdir, cwd);
		backSlashes(cwd);
		forwardSlashes(srcdir);

		if (strEndsWith(srcdir, "/tools/bin"))
		{
			s = strrchr(srcdir, '/');
			*s = 0;
			s = strrchr(srcdir, '/');
			*s = 0;
		}

		strcat(srcdir, "/src");

		backSlashes(srcdir);
		assert(chdir(srcdir) == 0);
		forwardSlashes(srcdir);

		sprintf(cmdline, "%s\\getvrml.exe -objlib %s/object_library -texlib %s/texture_library -noprune -monitor %s", cwd, srcdir, srcdir, emulate_cmd);
		system_detach(cmdline, true, false);

		assert(chdir(cwd) == 0);
	}
#endif

	frameLockedTimerCreate(&flt, 3000, 3000 / 60);

	// Create a world region so that the rest of the engine is happy.
	// MJF: Maybe this should use some default zone instead?
	worldGetTempWorldRegionByName("temp");

	globCmdParse("showcampos 0");
	globCmdParse("showfps 0");

	emSetEditorMode(true);
	if (start_mated)
	{
		if( mated2FileToOpen ) {
			emQueueFunctionCallEx( (emFunc)emOpenFile, mated2FileToOpen, 2 );
		}
		else {
			emQueueFunctionCallEx( emWorkspaceOpen, "Material Editor", 2 );
		}
	}

	loadend_printf("Startup finished");

	okToQuit = utilitiesLibShouldQuit();
	while (!okToQuit)
	{
		F32			elapsed;
		bool		drawWorld = true;

		// Nothing goes above timerTickBegin()!!!!!
		timerTickBegin();

		frameLockedTimerStartNewFrame(flt, wlTimeGetStepScale());
		frameLockedTimerGetPrevTimes(flt, &elapsed, NULL, NULL);
		updateThreadPriority();

		gfxResetFrameCounters();

		FolderCacheDoCallbacks();

		PERFINFO_AUTO_START("Once Per Frame Calls",1);

			utilitiesLibOncePerFrame(elapsed, 1.0f);
			gfxOncePerFrame(elapsed, elapsed, emIsEditorActive() || start_mated, true);
			worldLibOncePerFrame(elapsed);
			editLibOncePerFrame(elapsed);
			sndLibOncePerFrame(elapsed);


		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("Primary Device Drawing",1);

			gfxSetActiveDevice(primary_device->device);
			setVec4same(primary_device->activecamera->clear_color, 0);
			emSetActiveCamera(primary_device->activecamera, NULL);
			inpUpdateEarly(gfxGetActiveInputDevice());

			emRunQueuedFunctions();

			gfxDisplayDebugInterface2D(true);
			ui_OncePerFramePerDevice(elapsed, primary_device->device);
			if (switch_modes)
			{
				emSetEditorMode(switch_modes_mode);
				if (switch_modes_mode)
					gfxSetTitle(WINDOW_TITLE);
				switch_modes = 0;
			}
			drawWorld = !emEditorHidingWorld();
			okToQuit = emEditorMain();
			inpUpdateLate(gfxGetActiveInputDevice());
			gfxRunActiveCameraController(-1, NULL);
			gfxTellWorldLibCameraPosition(); // Call this only on the primary camera

			// drawing
			gfxStartMainFrameAction(emIsEditorActive(), false, !drawWorld, false, true);
			emEditorDrawGhosts();
			gfxFillDrawList(true, NULL);
			gfxDrawFrame();

		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("Aux Device Drawing",1);
			gfxRunAuxDevices();
		PERFINFO_AUTO_STOP();
		gfxOncePerFrameEnd(true);

		gclProcessQueuedErrors(true);

		assert(isProductionMode() || heapValidateAllPeriodic(100));

		// HEY YOU: Don't put anything after timerTickEnd()!!!!!!
		timerTickEnd();
		// Nothing goes down here.
	}

	gfxSettingsSave(primary_device->device);

	gfxUnregisterDevice(primary_device->device);
	rdrDestroyDevice(primary_device->device);

	sndShutdown();

	frameLockedTimerDestroy(&flt);
}
