#include <direct.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <conio.h>
#include <sys/types.h>
#include <time.h>
#include <fcntl.h>
#include "windefinclude.h"

#include "earray.h"
#include "stdtypes.h"
#include "file.h"
#include "error.h"
#include "logging.h"
#include "utils.h"
#include "gimmeDLLWrapper.h"
#include "piglib.h"
#include "FolderCache.h"
#include "fileutil.h"
#include "timing.h"
#include "StashTable.h"
#include "winutil.h"
#include "fileutil2.h"
#include "cmdparse.h"
#include "sharedmemory.h"
#include "utilitiesLib.h"
#include "netlinkprintf.h"
#include "net/net.h"
#include "sysutil.h"
#include "textparser.h"
#include "ScratchStack.h"
#include "ConsoleDebug.h"
#include "ControllerLink.h"
#include "systemspecs.h"

#include "tree.h"
#include "vrml.h"
#include "output.h"
#include "geo.h"
#include "procAnim.h"
#include "gettex.h"

#include "WorldLib.h"
#include "wlEditorIncludes.h"
#include "wlModel.h"
#include "wlAutoLOD.h"
#include "ObjectLibrary.h"
#include "dynSkeleton.h"

#include "GraphicsLib.h"
#include "GfxTextureTools.h"
#include "GfxSettings.h"
#include "GfxCamera.h"
#include "GfxPrimitive.h"
#include "GfxLoadScreens.h"
#include "GfxTexOpts.h"

#include "RdrDevice.h"
#include "RdrStandardDevice.h"
#include "RenderLib.h"
#include "wlTime.h"
#include "inputLib.h"
#include "inputKeyBind.h"
#include "tga.h"
#include "main.h"
#include "AutoStartupSupport.h"
#include "ResourceManager.h"
#include "ControllerLink.h"
#include "utf8.h"

#define	PHYSX_SRC_FOLDER "../../3rdparty"
#include "PhysicsSDK.h"

#define GIMME_QUIET 0

#define MAX_CHAR_LOD_FILES 5
#define CHAR_LOD_FILE_ENDING "_ConcatFile"

#include "main_c_ast.h"

typedef struct GetVrmlFolderRequest
{
	GetVrmlLibraryType targetlibrary;
	char path[MAX_PATH];
    char outputPath[MAX_PATH]; // or the null string if it should use the default
	bool is_core;
} GetVrmlFolderRequest;

RdrDevice *device = NULL;

static Reasons processFile(GetVrmlFolderRequest *request, const char *fname, bool is_reprocess);
static void reprocessOutputFileOnTheFly( FolderCache* fc, FolderNode* node, int virtual_location, const char* relpath, int when, void *userData);

int g_no_checkout = 0, g_quick_process = 0, g_force_rebuild = 0, g_dds_pause = 0, g_test = 0, g_test_count = 0, g_no_compression = 1, g_export_vrml = 0;
int g_verboseReprocessing = 1;
static int monitor, hide_console, dummy_int, need_texlib, no_prune, need_gfxlib, only_prune, need_anim, need_materials;
static char tex_info[MAX_PATH];

char nvdxt_path[MAX_PATH] = {0};

// If set, use a file's read-only status to determine if it should be processed, instead of checking for ownership with Gimme.
static bool check_writeable_only = false;

static GetVrmlFolderRequest **g_all_requests = NULL;

typedef char** (*EnumerateFileNamesFn)( const char* name, const char* src_fname, const char* dst_fname, const char* rootPath );
char** enumTexLibFiles( const char* name, const char* src_fname, const char *dst_fname, const char* root_path );

static struct
{
	char *lib_name;
	char **src_exts;
	char *dst_ext_little;
	char *dst_ext_big;
	char *data_path;
	bool flatten;
    EnumerateFileNamesFn other_dst_files;
	HANDLE hMutex[2];
} lib_data[] =
{
    //                               dest_
    //                               ext_     dest                                  	
	// lib_name			   src_exts	 little	  ext_big  	data_path			flatten 	other_dst_files	   hMutex[2]
    {  "Unspecified",      NULL,     NULL,    NULL,    	NULL,               false,  	NULL,				{NULL, NULL}},
	{  "ObjectLibrary",	   NULL,	 ".geo2", NULL, 	NULL,				false,		enumObjLibFiles,	{NULL, NULL}},
	{  "CharacterLibrary", NULL,	 ".geo2", NULL, 	NULL,				false,		enumCharLibFiles,	{NULL, NULL}},
    {  "AnimationLibrary", NULL,	 ".atrk", NULL,		NULL,				false,  	NULL,				{NULL, NULL}},
	{  "TextureLibrary",   NULL,	 ".wtex", NULL,		NULL,				false,		enumTexLibFiles,	{NULL, NULL}},
};

AUTO_RUN;
void setupOtherSrcExts( void )
{
	eaPush(&lib_data[LIB_OBJ].src_exts, ".wrl");

	eaPush(&lib_data[LIB_CHAR].src_exts, ".wrl");

	eaPush(&lib_data[LIB_ANIM].src_exts, ".danim");
	eaPush(&lib_data[LIB_ANIM].src_exts, ".wrl");

	eaPush(&lib_data[LIB_TEX].src_exts, ".tga");
	eaPush(&lib_data[LIB_TEX].src_exts, ".dds");
	eaPush(&lib_data[LIB_TEX].src_exts, ".tif");
}

char** enumTexLibFiles( const char* name, const char* src_fname, const char *dst_fname, const char* root_path )
{
	char buffer[MAX_PATH];
	char **filelist = NULL;

	changeFileExt(dst_fname, ".NinePatch", buffer);
	eaPush(&filelist, strdup(buffer));

	return filelist;
}



/// EArray of fullpaths to reprocess via REPROCESS-FULLPATH.
///
/// This array should have strings that can be FREE'd.
static char** fullpathsToReprocess = NULL;

//takes (datasrc)/object_library... for a file and converts it to (target)/object_libray...
//otherwise keeping the file structure
static void srcToData(char *src_name,char *data_name, size_t data_name_size, char *data_path, bool flatten, bool is_core)
{
	const char *s;

	if (flatten)
	{
		s = "";
	}
	else
	{
		s = strstri(src_name,"/src/");
		if (s) {
			s += strlen("/src/");
		} else {
			s = strstri(src_name,"/CoreSrc/");
			assert(s);  // One of either src or CoreSrc must exist
			s += strlen("/CoreSrc/");
		}
	}

	if (is_core)
	{
		const char *core_data_dir = fileCoreDataDir();
		assert(core_data_dir);
		strcpy_s(data_name, data_name_size, core_data_dir);
	}
	else
	{
		strcpy_s(data_name, data_name_size, fileDataDir());
	}

	if (data_path)
		strcatf_s(data_name, data_name_size, "/%s", data_path);
	strcatf_s(data_name, data_name_size, "/%s", s);

	forwardSlashes(data_name);
}

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct SkeletonFixupOverride
{
	char *vrml_filename;				AST( STRUCTPARAM )
	char *skeleton_name;				AST( STRUCTPARAM )
} SkeletonFixupOverride;

AUTO_STRUCT;
typedef struct SkeletonFixupInfo
{
	char *skeleton_name;				AST( NAME(Skeleton) )
	SkeletonFixupOverride **overrides;	AST( NAME(Override) )
} SkeletonFixupInfo;

static const DynBaseSkeleton *getSkeletonForCharLib(const char *src_fname)
{
	char filename[MAX_PATH], *s, *skeleton_name = NULL;
	const DynBaseSkeleton *skeleton = NULL;
	SkeletonFixupInfo fixup_info = {0};
	const char *sconst;
	int i;

	strcpy(filename, src_fname);
	s = getFileName(filename);
	*s = 0;
	strcat(filename, "skel_fixup.info");
	
	if (!ParserLoadFiles(NULL, filename, NULL, 0, parse_SkeletonFixupInfo, &fixup_info))
		return NULL;

	skeleton_name = fixup_info.skeleton_name;
	sconst = getFileNameConst(src_fname);

	for (i = 0; i < eaSize(&fixup_info.overrides); ++i)
	{
		if (stricmp(fixup_info.overrides[i]->vrml_filename, sconst)==0)
		{
			skeleton_name = fixup_info.overrides[i]->skeleton_name;
			break;
		}
	}

	if (skeleton_name && stricmp(skeleton_name, "NoFixup")!=0)
		skeleton = dynBaseSkeletonFind(skeleton_name);

	StructDeInit(parse_SkeletonFixupInfo, &fixup_info);

	return skeleton;
}

static void updateThreadPriority(void)
{
#ifndef _XBOX
	int curActive = isProductionMode() && device && GetForegroundWindow() == rdrGetWindowHandle(device);

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

/// Create a rendering window for the current test library.
static bool createPrimaryDevice(HINSTANCE hInstance)
{
	WindowCreateParams params={0};

    params.display.width = 256;
    params.display.height = 256;
    params.display.hide = true;
    params.display.allow_any_size = true;
	params.threaded = false;
	params.device_type = "Direct3D9";

	device = rdrCreateDevice(&params, hInstance, 2);

	if (!device)
		return false;

	return true;
}

static void registerPrimaryDevice(HINSTANCE hInstance)
{
	InputDevice *inpdev;
	if (!device)
		return;
	inpdev = inpCreateInputDevice(device,hInstance,keybind_ExecuteKey, false);
	//rdrSetTitle(device, WINDOW_TITLE);
	gfxRegisterDevice(device, inpdev, false);
}

GimmeErrorValue checkoutSingleFile(const char *fname, const char *vrml_name)
{
	GimmeErrorValue ret;
	if (!g_no_checkout) {
		if (!gimmeDLLQueryIsFileLockedByMeOrNew(fname))
			ret=gimmeDLLDoOperation(fname, GIMME_CHECKOUT, GIMME_QUIET);
		else
			ret = GIMME_NO_ERROR;
	} else {
		int dummy = _chmod(fname, _S_IWRITE | _S_IREAD);
		ret = GIMME_NO_ERROR;
	}
	if (ret!=GIMME_NO_ERROR && ret!=GIMME_ERROR_NOT_IN_DB && ret!=GIMME_ERROR_NO_SC && ret!=GIMME_ERROR_NO_DLL)
	{
		filelog_printf("failed_checkouts", "Can't checkout %s. (%s)\n",fname,gimmeDLLGetErrorString(ret));
		Alertf("Can't checkout %s. (%s)\n",fname,gimmeDLLGetErrorString(ret));
		if (strstriConst(gimmeDLLGetErrorString(ret), "already deleted")) {
			printfColor(COLOR_RED|COLOR_BRIGHT, "WARNING: \"%s\" has been previously marked as deleted,\n   you will need to manually re-add this file with Gimme Checkin (Checkin All will skip it)\n", fname);
			ret = GIMME_NO_ERROR;
		} else {
			// Because of the checks above, if we get here, this file is one that should be processed!
			if (!g_force_rebuild)
				Alertf("Source file is owned by you, but output file is checked out by someone else!\n%s (%s)\n%s (%s)\n", vrml_name, gimmeDLLQueryLastAuthor(vrml_name), fname, gimmeDLLQueryIsFileLocked(fname)?gimmeDLLQueryIsFileLocked(fname):gimmeDLLQueryLastAuthor(fname));
			return ret; // Only failure case!
		}
	} else {
		int dummy = _chmod(fname, _S_IWRITE | _S_IREAD);
		ret = GIMME_NO_ERROR;
	}
	return ret;
}

GimmeErrorValue checkoutFiles(const char **fnames, const char *vrml_name)
{
	GimmeErrorValue ret;
	int i;
	if (!g_no_checkout) {
		bool needCheckout=false;
		for (i=0; !needCheckout && i<eaSize(&fnames); i++)
			if (!gimmeDLLQueryIsFileLockedByMeOrNew(fnames[i]))
				needCheckout = true;
		if (needCheckout)
			ret=gimmeDLLDoOperations(fnames, GIMME_CHECKOUT, GIMME_QUIET);
		else
			ret = GIMME_NO_ERROR;
	} else {
		for (i=0; i<eaSize(&fnames); i++) 
			chmod(fnames[i], _S_IWRITE | _S_IREAD);
		ret = GIMME_NO_ERROR;
	}
	if (ret!=GIMME_NO_ERROR && ret!=GIMME_ERROR_NOT_IN_DB && ret!=GIMME_ERROR_NO_SC && ret!=GIMME_ERROR_NO_DLL)
	{
		// Call the single file version for better error handling
		for (i=0; i<eaSize(&fnames); i++) 
			checkoutSingleFile(fnames[i], vrml_name);
	} else {
		for (i=0; i<eaSize(&fnames); i++) 
			chmod(fnames[i], _S_IWRITE | _S_IREAD);
		ret = GIMME_NO_ERROR;
	}
	return ret;
}


#ifdef TEST_OLD_PIVOTS
static bool meetsCriteria(Node *node, bool is_root)
{
	Node *next;
	bool ret = false;

	for(;node;node = next)
	{
		next = node->next;

		// recurse, just to delete empty children
		if (node->child)
			meetsCriteria(node->child, false);

		if (!node->child)
		{
			if (node->mesh.vert_count)
			{
				if (is_root)
				{
					if (memcmp(unitmat, node->mat, sizeof(Mat3))!=0 && !sameVec3(node->mat[3], zerovec3))
					{
						ret = true;
						printf("\n  %s", node->name);
						if (!node->mat[0][1] && !node->mat[0][2] && !node->mat[1][0] && !node->mat[1][2] && !node->mat[2][0] && !node->mat[2][1])
							printf("    **** POSSIBLE (scale, no rotation)");
					}
				}
			}
			else
			{
				treeDelete(node, NULL);
			}
		}
	}

	return ret;
}
#endif

static void freePtr( void* p )
{
    free( p );
}

static bool anyFileNewerAbsolute(char* refname, const char** testnames)
{
    int it;
    int size = eaSize( &testnames );

    for( it = 0; it != size; ++it )
    {
        if( fileNewerAbsolute( refname, testnames[ it ]))
        {
            return true;
        }
    }

    return false;
}

/* is refname older than testname?
*/
int fileNewerEx(const char *refname,const char *testname, __time32_t *ref_result, __time32_t *test_result)
{
	__time32_t ref, test;
	__time32_t t;

	*ref_result = ref = fileLastChanged(refname);
	*test_result = test = fileLastChanged(testname);

	t = test - ref;
	return t > 0;
}

/* is refname older than testname?
*/
int fileNewerAbsoluteEx(const char *refname,const char *testname, __time32_t *ref_result, __time32_t *test_result)
{
	__time32_t ref, test;
	__time32_t t;

	*ref_result = ref = fileLastChangedAbsolute(refname);
	*test_result = test = fileLastChangedAbsolute(testname);

	t = test - ref;
	return t > 0;
}

static bool textureNeedReprocessing(const char *out_fname_little, const char *src_fname, const char *timestampfile, Reasons *r)
{
	bool needRebuild = false;
	TexOpt *texopt = texoptFromTextureName(out_fname_little, NULL);

	*r = REASON_PROCESSED;
	if (g_verboseReprocessing)
		log_printf(LOG_GETVRML, "TNR for %s %s %s\n", out_fname_little, src_fname, timestampfile);

	if (texopt && (texopt->flags & TEXOPT_EXCLUDE))
	{
		if (g_verboseReprocessing)
			log_printf(LOG_GETVRML, "TNR false TexOpt exclude\n");
		*r = REASON_EXCLUDED;
		return false;
	}
	else if (!fileExists(timestampfile))
	{
		__time32_t ref, test;
		// No timestamp file, just check if either .tga or .texopt are newer than .wtex
		if (fileNewerAbsoluteEx(out_fname_little,src_fname, &ref, &test))
		{
			if (g_verboseReprocessing)
				log_printf(LOG_GETVRML, "TNR true No timestamp, source image timestamp %d newer than dest %d, diff %d\n", test, ref, test - ref);
			needRebuild = true;
		}
		if (texopt && fileNewerEx(out_fname_little,texopt->file_name, &ref, &test))
		{
			if (g_verboseReprocessing)
				log_printf(LOG_GETVRML, "TNR true No timestamp, source TexOpt timestamp %d newer than dest %d, diff %d\n", test, ref, test - ref);
			needRebuild = true;
		}
	} else {
		if (!fileExists(out_fname_little)) {

			if(texopt && (texopt->flags & TEXOPT_LIGHTMAP)) {

				// For lightmaps, check the existence of the output
				// textures instead of the thing with the same name.
				char intensityName[MAX_PATH];
				char avgName[MAX_PATH];
				char lowEndName[MAX_PATH];
				char baseName[MAX_PATH];

				strcpy(baseName, out_fname_little);
				
				if(strlen(baseName) > strlen("r.wtex")) {
					baseName[strlen(baseName) - strlen("r.wtex")] = 0;
				}
				
				sprintf(avgName, "%savg.wtex", baseName);
				sprintf(intensityName, "%sintensity.wtex", baseName);
				sprintf(lowEndName, "%slowend.wtex", baseName);

				// If outputs don't exist at all, rebuild.
				if(!fileExists(avgName) || !fileExists(intensityName) || !fileExists(lowEndName)) {
					needRebuild = true;
				}

				if(fileNewerAbsolute(timestampfile, src_fname)) {
					needRebuild = true;
				}

			} else {
				if (g_verboseReprocessing)
					log_printf(LOG_GETVRML, "TNR true Timestamp exists, but no output .wtex\n");
				// Timestamp file exists, but .wtex doesn't!  Need to reprocess
				needRebuild = true;
			}
		} else if (fileSize(timestampfile)==10) {
			__time32_t ref, test;
			// Old-style timestamp, reprocess if either .tga or .texopt are newer than .timestamp
			if (fileNewerAbsoluteEx(timestampfile,src_fname, &ref, &test))
			{
				if (g_verboseReprocessing)
					log_printf(LOG_GETVRML, "TNR true Old Timestamp, source image timestamp %d newer than (old format) dest timestamp file %d, diff %d\n", test, ref, test - ref);
				needRebuild = true;
			}
			if (texopt && fileNewerEx(timestampfile,texopt->file_name, &ref, &test))
			{
				if (g_verboseReprocessing)
					log_printf(LOG_GETVRML, "TNR true Old Timestamp, source TexOpt timestamp %d newer than (old format) dest timestamp file %d, diff %d\n", test, ref, test - ref);
				needRebuild = true;
			}
		} else {
			// New-style timestamp, open, check all deps
			char *data = fileAlloc(timestampfile, NULL);
			char *toks[4];
			__time32_t src_timestamp = 0, texopt_timestamp = 0;

			int numtoks = tokenize_line(data, toks, NULL);
			if (numtoks != 3 || stricmp(toks[0], "Version1")!=0)
				assertmsgf(0, "Error parsing timestamp file %s", timestampfile);

			src_timestamp = fileLastChangedAbsolute(src_fname);
			if (src_timestamp != atoi(toks[1]))
			{
				if (g_verboseReprocessing)
					log_printf(LOG_GETVRML, "TNR true New Timestamp, source image timestamp %d differs from saved wtex .timestamp %d, diff %d\n", src_timestamp, atoi(toks[1]), src_timestamp - atoi(toks[1]));
				needRebuild = true;
			}
			if (texopt) {
				texopt_timestamp = fileLastChanged(texopt->file_name);
				if (texopt_timestamp != atoi(toks[2]))
				{
					if (g_verboseReprocessing)
						log_printf(LOG_GETVRML, "TNR true New Timestamp, dest TexOpt timestamp %d differs from saved texopt .timestamp %d, diff %d\n", texopt_timestamp, atoi(toks[2]), texopt_timestamp - atoi(toks[2]));
					needRebuild = true;
				}
			} else {
				if (-1 != atoi(toks[2]))
				{
					if (g_verboseReprocessing)
						log_printf(LOG_GETVRML, "TNR true New Timestamp, but no dest TexOpt exists, and saved texopt timestamp is not -1; TexOpt may have been deleted!\n");
					needRebuild = true;
				}
			}
			fileFree(data);
		}
	}

	if (strEndsWith(src_fname, "_posx.tga"))
	{
		char spheremappath[MAX_PATH];
		char *s;
		strcpy(spheremappath, src_fname);
		s = strrchr(spheremappath, '_');
		strcpy_s(s, ARRAY_SIZE(spheremappath) - (s - spheremappath), "_spheremap.tga");
		if (!fileExists(spheremappath) || 
			fileNewer(spheremappath, src_fname))
		{
			needRebuild = true;
		}
	}

	return needRebuild;
}

static Reasons processFile2(GetVrmlFolderRequest *request, char *name, char *out_fname_little, char *out_fname_big, char *src_fname, char **other_src_fnames, char *out_group_fname, char *out_fname_deps, char *out_root, bool is_reprocess)
{
	char timestampfile[MAX_PATH];
	char htexfile[MAX_PATH];
	bool needRebuild=false;

	PERFINFO_AUTO_START_FUNC();

	if (request->targetlibrary == LIB_OBJ || request->targetlibrary == LIB_CHAR)
	{
		char headerpath[MAX_PATH];
		changeFileExt(out_fname_little, ".ModelHeader", headerpath);
		if (!fileExists(headerpath))
			needRebuild = true;
	}

	changeFileExt(out_fname_little, ".timestamp", timestampfile);
	changeFileExt(out_fname_little, ".htex", htexfile);

	if (request->targetlibrary == LIB_TEX)
	{
		Reasons r=0;
		if (textureNeedReprocessing(out_fname_little, src_fname, timestampfile, &r))
			needRebuild = true;
		if (r == REASON_EXCLUDED)
			return r;
		assert(!eaSize(&other_src_fnames));
		// When would other_src_filenames ever be true?
		//if (eaSize(&other_src_fnames) && other_src_fnames[0][0] && texOptDifferent(out_fname_little, other_src_fnames[0]))
		//	needRebuild = true;
	}

	if (!needRebuild)
	{
		if (fileNewerAbsolute(out_fname_little,src_fname))
		{
			// Texture checking moved above
			if ((request->targetlibrary == LIB_OBJ || request->targetlibrary == LIB_CHAR))
			{
				if (!fileExists(timestampfile) || fileNewerAbsolute(timestampfile, src_fname))
					needRebuild = true;
			} else if (request->targetlibrary != LIB_TEX)
				needRebuild = true;
		}
		else if (anyFileNewerAbsolute(out_fname_little, other_src_fnames))
		{
			needRebuild = true;
		}
		else if (out_fname_big && out_fname_big[0] && (fileNewerAbsolute(out_fname_big,src_fname) || anyFileNewerAbsolute(out_fname_big, other_src_fnames)))
		{
			needRebuild = true;
		}
	}

	if  (needRebuild ||
		g_force_rebuild==1 ||
		gimmeDLLQueryIsFileBlocked(out_fname_little))
	{
		int	ret;

		/* rules for processing:
		When GetVrml sees a source file that is newer than its corresponding output file, it sees that this
		file needs to be processed.  It will NOT process the file if it is checked out by someone else.
		If no one has the file checked out, then it will only process it if you were the last person to check it in.
		There should no longer be any issues with people getting their output files checked out by someone else.

		The correct procedure to things is :
		1. Make your changes (Check out source file)
		2. Process the geometry (run GetVRML)
		3. TEST (Run the game)
		4. Check-in (or check-point) your files so other people can get them
		*/


		// We're going to cancel if the source is not owned by me
		if (!g_force_rebuild
			&& !(check_writeable_only && !fileIsReadOnly(src_fname)
				|| gimmeDLLQueryIsFileMine(src_fname))) { 
			if (g_verboseReprocessing && needRebuild && request->targetlibrary == LIB_TEX)
			{
				log_printf(LOG_GETVRML, "TNR true But not reprocessing because file is %s\n", check_writeable_only && !fileIsReadOnly(src_fname) ? "not read-only" : "not mine");
			}
			//printf("\r Not yours (%s)    \n", gimmeDLLQueryLastAuthor(src_fname));
			PERFINFO_AUTO_STOP_FUNC();
			return REASON_NOTYOURS;
		}

		if (g_test)
		{
#ifdef TEST_OLD_PIVOTS
			if (request->targetlibrary == LIB_OBJ)
			{
				const char **file_names;
				Node	*root;

				if (fileNewerAbsolute("C:/fightclub/src/object_library/Missions/Generic_Mission_Assets/Signage/Signage.wrl", src_fname))
				{
					PERFINFO_AUTO_STOP_FUNC();
					return REASON_NOTNEWER;
				}

				eaStackCreate(&file_names, 1);
				eaPush(&file_names, src_fname);
				root = readVrmlFiles(file_names);
				eaDestroy(&file_names);
				if (!root)
				{
					texNameClear(1);
					PERFINFO_AUTO_STOP_FUNC();
					return REASON_NOTNEWER;
				}

				if (!meetsCriteria(root, true))
				{
					texNameClear(1);
					treeFree();
					PERFINFO_AUTO_STOP_FUNC();
					return REASON_NOTNEWER;
				}

				texNameClear(1);
				treeFree();
			}
#endif
			g_test_count++;

			printf("\n\n");
			PERFINFO_AUTO_STOP_FUNC();
			return REASON_NOTNEWER;
		}

		printf("\n");
		mkdirtree(out_fname_little);

        // attempt to check out all the files necesarry
        {
            char** accum = NULL;

			assert( request->targetlibrary < ARRAY_SIZE(lib_data) );

			if(lib_data[ request->targetlibrary ].other_dst_files) {
                accum = lib_data[ request->targetlibrary ].other_dst_files( name, src_fname, out_fname_little, out_root );
            }

			// This one first because it gets printed by Gimme
			eaInsert( &accum, strdup( out_fname_little ), 0);

            if (out_fname_big && out_fname_big[0])
            {
                eaPush( &accum, strdup( out_fname_big ));
            }
			if (out_fname_deps && out_fname_deps[0])
			{
				eaPush( &accum, strdup( out_fname_deps ));
			}
            if (out_group_fname && out_group_fname[0])
            {
                eaPush( &accum, strdup( out_group_fname ));
            }
			if ((request->targetlibrary == LIB_TEX || request->targetlibrary == LIB_OBJ || request->targetlibrary == LIB_CHAR)
					&& timestampfile[0])
				eaPush(&accum, strdup(timestampfile));
			if (request->targetlibrary == LIB_TEX) {
				eaPush(&accum, strdup(htexfile));
			}

            // checkout each file...
			ret = checkoutFiles(accum, src_fname);
			if( ret != GIMME_NO_ERROR ) {
				eaDestroyEx( &accum, freePtr );
				return REASON_CHECKOUTFAILED;
			}

            eaDestroyEx( &accum, freePtr );
        }

		if (!g_no_checkout)
		{
			if (g_quick_process)
			{
				gimmeDLLBlockFile(out_fname_little, "File processed by GetVrml with the quick_process flag, you must reprocess the file before checking it in.");
				if (out_fname_big && out_fname_big[0])
					gimmeDLLBlockFile(out_fname_big, "File processed by GetVrml with the quick_process flag, you must reprocess the file before checking it in.");
			}
			else
			{
				if (gimmeDLLQueryIsFileBlocked(out_fname_little))
					gimmeDLLUnblockFile(out_fname_little);
				if (out_fname_big && out_fname_big[0] && gimmeDLLQueryIsFileBlocked(out_fname_big))
					gimmeDLLUnblockFile(out_fname_big);
			}
		}

		// remove .bak file of the output file, it may have inadvertently been created from the step
		// above that created an empty file so that it could be checked out
		{
			char bakname[MAX_PATH];
			strcpy(bakname, out_fname_little);
			strcat(bakname, ".bak");
			fileForceRemove(bakname);
			strcpy(bakname, out_fname_big);
			strcat(bakname, ".bak");
			fileForceRemove(bakname);
		}

		// Do some verification
		{
			char located_path[MAX_PATH];
			char relpath[MAX_PATH];
			fileRelativePath(out_fname_little, relpath);
			fileLocateWrite(relpath, located_path);
			if (stricmp(located_path, out_fname_little)!=0 && fileExists(located_path) && !strstri(located_path, "core"))
			{
				printfColor(COLOR_RED|COLOR_BRIGHT, "\nYou are processing a file which will likely not be loaded by the client\n"
					" (e.g. a Core file which is overriden per-project).\n"
					" Actual file to be loaded by client: %s", located_path);
			}
		}

		switch (request->targetlibrary)
		{
			xcase LIB_OBJ:
			{
				const char **file_names = NULL;
				mkdirtree(out_fname_little);
				eaStackCreate(&file_names, 1);
				eaPush(&file_names, strdup(src_fname));
				geoAddFile(name, file_names, out_fname_little, out_group_fname,
					request->targetlibrary, 0, request->is_core,
					/*out_fname_big, */out_fname_deps, out_root, NULL);
				eaDestroy(&file_names);
			}

			xcase LIB_CHAR:
			{
				char **file_names = NULL;
				int i;

				eaStackCreate(&file_names, MAX_CHAR_LOD_FILES);
				eaPush(&file_names, strdup(src_fname));
				
				for (i = 1; i < MAX_CHAR_LOD_FILES; ++i)
				{
					char buffer[MAX_PATH];
					strcpy(buffer, src_fname);
					assert(strEndsWith(buffer, ".wrl"));
					buffer[strlen(buffer)-4] = 0;
					strcatf(buffer, "%s%d.wrl", CHAR_LOD_FILE_ENDING, i);
					if (!fileExists(buffer))
						break;
					eaPush(&file_names, strdup(buffer));
				}

				geoAddFile(name, file_names, out_fname_little, NULL,
					request->targetlibrary, 0, request->is_core,
					/*out_fname_big, */out_fname_deps, out_root, getSkeletonForCharLib(src_fname));

				eaDestroyEx(&file_names, NULL);
			}

			xcase LIB_TEX:
			{
				if (!processTexture(src_fname, out_fname_little, NULL)) { //write the data to .wtex
					fileForceRemove(out_fname_little);
					fileForceRemove(htexfile);
				}
			}

			xdefault:
			{
				Alertf("Unknown target library!");
				exit(1);
			}
		}

		printf("Done (finished at %s).\n", timeGetLocalTimeString());
		PERFINFO_AUTO_STOP_FUNC();
		return 0;
	} else {
		PERFINFO_AUTO_STOP_FUNC();
		return REASON_NOTNEWER;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static Reasons processFile(GetVrmlFolderRequest *request, const char *fname, bool is_reprocess)
{
	char fullname[MAX_PATH], **other_fullnames = NULL, *s, buf2[MAX_PATH], name_buf[MAX_PATH],
			out_fname_little[MAX_PATH], out_fname_big[MAX_PATH], out_group_fname[MAX_PATH],
			out_fname_deps[MAX_PATH], out_root[MAX_PATH], *name;
	char **otherDependencies;
	int i, ret;

	PERFINFO_AUTO_START_FUNC();

	mpCompactPools();

	makefullpath(fname,fullname); //add cwd to

	if (request->targetlibrary == LIB_CHAR)
	{
		for (i = 1; i < MAX_CHAR_LOD_FILES; ++i)
		{
			char buffer[MAX_PATH];
			sprintf(buffer, "%s%d.wrl", CHAR_LOD_FILE_ENDING, i);
			if (strEndsWith(fullname, buffer))
			{
				fullname[strlen(fullname)-strlen(buffer)] = 0;
				strcat(fullname, ".wrl");
				break;
			}
		}
	}

	strcpy(name_buf,fullname);
	name = strrchr(name_buf,'/');
	name++;
	s = strrchr(name,'.');
	if (s)
		*s = 0;
    if( request->outputPath[ 0 ]) {
        strcpy( buf2, request->outputPath );
    } else {
        srcToData(fullname,SAFESTR(buf2),lib_data[request->targetlibrary].data_path,lib_data[request->targetlibrary].flatten,request->is_core);
    }
    s = strrchr(buf2,'/');
	if (s)
		s[1] = 0;

    if( request->outputPath[ 0 ])
    {
        strcpy( out_root, request->outputPath );
    }
    else
    {
        srcToData(fullname,SAFESTR(out_root),NULL,1,request->is_core);
    }
	s = strrchr(out_root,'/');
	if (s)
		s[1] = 0;


    #define ErrorContinuef(condition, ...) if (!(condition)) { Errorf(__VA_ARGS__); continue; } else

	otherDependencies = getSourceFileDependencies( fullname );
    for (i = 0; i < eaSize(&otherDependencies); ++i)
	{
        char* otherDependency = otherDependencies[i];
        char buffer[ MAX_PATH ];

        if( !fileIsAbsolutePath( otherDependency ))
		{
            if( strEndsWith( otherDependency, ".wtex" ))
			{
                BasicTexture* tex = texFind( otherDependency, false );
                char buffer2[ MAX_PATH ];

                ErrorContinuef( tex, "Could not find dependant texture \"%s\".", otherDependency );

                texFindFullName( texFind( otherDependency, false ), SAFESTR( buffer2 ));
                fileLocateWrite( buffer2, buffer );
            }
			else
			{
                ErrorContinuef( false, "Could not find dependancy \"%s\".  Update GetVRML.  If this doesn't go away, bug the programming team.", otherDependency );
            }
        }
		else
		{
            strcpy( buffer, otherDependency );
        }

        eaPush(&other_fullnames, strdup(buffer));
    }    

	if (request->targetlibrary == LIB_CHAR)
	{
		for (i = 1; i < MAX_CHAR_LOD_FILES; ++i)
		{
			char buffer[MAX_PATH];
			strcpy(buffer, fullname);
			assert(strEndsWith(buffer, ".wrl"));
			buffer[strlen(buffer)-4] = 0;
			strcatf(buffer, "%s%d.wrl", CHAR_LOD_FILE_ENDING, i);
			if (!fileExists(buffer))
				break;
			eaPush(&other_fullnames, strdup(buffer));
		}
	}

    if (!request->outputPath[0])
    {
        switch (request->targetlibrary)
        {
            xcase LIB_OBJ:
            {
                if (!strstri(fullname, "/object_library/"))
                {
                    Alertf("Trying to process file \"%s\" as object library, but it is not in an object library path!", fullname);
					PERFINFO_AUTO_STOP_FUNC();
                    return REASON_NOTNEWER;
                }
            }

            xcase LIB_CHAR:
            {
                if (!strstri(fullname, "/character_library/"))
                {
                    Alertf("Trying to process file \"%s\" as character library, but it is not in a character library path!", fullname);
					PERFINFO_AUTO_STOP_FUNC();
                    return REASON_NOTNEWER;
                }
            }

            xcase LIB_TEX:
            {
                TexOptFlags texopt_flags=0;

                if (!strstri(fullname, "/texture_library/"))
                {
                    Alertf("Trying to process file \"%s\" as texture library, but it is not in a texture library path!", fullname);
					PERFINFO_AUTO_STOP_FUNC();
                    return REASON_NOTNEWER;
                }

                texoptFromTextureName(fname, &texopt_flags);

				// skip cubemap files that are going to be reprocessed anyway because the base texture will be reprocessed
                if (!is_reprocess && (texopt_flags & TEXOPT_CUBEMAP) && !strEndsWith(name, "_posx") && !strEndsWith(name, "_cube"))
                {
					PERFINFO_AUTO_STOP_FUNC();
                    return REASON_NOTNEWER;
                }

				// skip volume files that are going to be reprocessed anyway because the base texture will be reprocessed
				if (!is_reprocess && (texopt_flags & TEXOPT_VOLUMEMAP) && !strEndsWith(name, "_slice0"))
				{
					PERFINFO_AUTO_STOP_FUNC();
					return REASON_NOTNEWER;
				}

				if (strStartsWith(name, "x_"))
                {
                    printfColor(COLOR_RED|COLOR_BRIGHT,
                                "Warning: File name (%s) begins with \"x_\".\n"
                                "  This file will not be processed.\n"
                                "  Texture/Material names beginning with X_ exported from Max get the X_ stripped,\n"
                                "  so this is probably not going to do what you want it to.\n"
                                "(Note: It's perfectly valid to have a texture named x_blah which is only referenced in\n"
                                "  Max for the purpose of referring to a complicated material named Blah.)\n", fullname);
					PERFINFO_AUTO_STOP_FUNC();
                    return REASON_NOTNEWER;
                }

				if (strEndsWith(name, ".9"))
					name[strlen(name)-2] = '\0'; // remove .9

                if (texopt_flags & TEXOPT_CUBEMAP)
                {
                    // Change name
                    int offset = strlen(name)-strlen("_negx");
                    assert(name[offset] == '_');
                    strcpy_s(name + offset, ARRAY_SIZE(name_buf) - offset - (name - name_buf), "_cube");
                }

				if (texopt_flags & TEXOPT_VOLUMEMAP)
				{
					// Change name
					int offset = strlen(name)-strlen("_slice0");
					assert(name[offset] == '_');
					strcpy_s(name + offset, ARRAY_SIZE(name_buf) - offset - (name - name_buf), "_voltex");
				}

               // fill in spaces in texture name
				{
					char* sn;
	                for (sn = name; *sn; ++sn)
	                {
	                    if (*sn == ' ')
	                        *sn = '_';
	                }
				}
            }

            xdefault:
            {
                Alertf("Unknown target library!");
                exit(1);
            }
        }
    }

	sprintf(out_fname_little, "%s%s%s", buf2, name, lib_data[request->targetlibrary].dst_ext_little);
	if (request->targetlibrary == LIB_OBJ || request->targetlibrary == LIB_CHAR)
	{
		changeFileExt(out_fname_little, ".modelnames", out_group_fname);
		changeFileExt(out_fname_little, ".MaterialDeps", out_fname_deps);
	} else {
		out_group_fname[0] = 0;
		out_fname_deps[0] = 0;
	}
	if (lib_data[request->targetlibrary].dst_ext_big)
		changeFileExt(out_fname_little, lib_data[request->targetlibrary].dst_ext_big, out_fname_big);
	else
		out_fname_big[0] = 0;

	ret = processFile2(
            request, name, out_fname_little, out_fname_big, fullname,
            other_fullnames, out_group_fname, out_fname_deps, out_root,
            is_reprocess );

    eaDestroyEx( &other_fullnames, freePtr );

	PERFINFO_AUTO_STOP_FUNC();

	return ret;
}

static void reprocessFile(GetVrmlFolderRequest *request, const char *fullpath, int print_errors)
{
	static char lastpath[MAX_PATH];
	static U32 lasttime=0;
	Reasons reason;

	fileWaitForExclusiveAccess(fullpath);

	waitForGetVrmlLock(true);
	if (request->targetlibrary == LIB_ANIM)
	{
		if (strEndsWith(fullpath, lib_data[request->targetlibrary].src_exts[0]))
			reason = processAnim(fullpath, request->is_core, true);
		else
			reason = processSkeleton(fullpath, request->is_core, true);
	}
	else
	{
		fileWaitForExclusiveAccess(fullpath);
		reason = processFile(request, fullpath, true);
	}

	releaseGetVrmlLock();

	if (reason == REASON_NOTNEWER && stricmp(lastpath, fullpath)==0 && timerCpuSeconds() - lasttime  < 20) {
		// print nothing
	} else if (reason != REASON_PROCESSED && print_errors) {
		printf("\nDidn't reprocess '%s' because either it (or it's associated files) isn't newer, is not yours, or an error occurred.\n", fullpath);
		if (reason == REASON_NOTNEWER) {
			printf("  Reason:  File is not newer\n");
		}
		else if (reason == REASON_NOTYOURS) {
			printf("  Reason:  Gimme thinks it's not yours\n");
		}
		else if (reason == REASON_CHECKOUTFAILED) {
			printf("  Reason:  Checkout failed\n");
		}
		else if (reason == REASON_INVALID_SRC_DIR) {
			printf("  Reason:  Invalid source dir\n");
		}
		else if (reason == REASON_INVALID_SRC_DIR) {
			printf("  Reason:  Bad data\n");
		}
		else if (reason == REASON_EXCLUDED) {
			printf("  Reason:  Excluded\n");
		}
	}

	strcpy(lastpath, fullpath);
	lasttime = timerCpuSeconds();

	printf("\r%-200c\r", ' ');
}

static void reprocessFullpath(const char *fullpath)
{
	int folder;

	for (folder=0; folder<eaSize(&g_all_requests); ++folder)
	{
		if (strStartsWith(fullpath, g_all_requests[folder]->path) && strEndsWithAny(fullpath, lib_data[g_all_requests[folder]->targetlibrary].src_exts))
		{
			reprocessFile(g_all_requests[folder], fullpath, 1);
			break;
		}
	}

	printf("\n");
}

/// Process all files in FULLPATHS-TO-REPROCESS
static void processQueuedFullpaths( void )
{
	int i;
	for (i = 0; i < eaSize(&fullpathsToReprocess); ++i )
		reprocessFullpath(fullpathsToReprocess[i]);
	eaClearEx(&fullpathsToReprocess, freePtr);
}

static void reprocessOnTheFly(FolderCache* fc, FolderNode* node, int virtual_location, const char *relpath, int when, void *userData)
{
	char temp[MAX_PATH];
	char fullpath[MAX_PATH];

	printf("\n");

	if (strstr(relpath, "/_"))
	{
		printfColor(COLOR_RED|COLOR_BRIGHT, "File '%s' begins with an underscore, not processing.\n", relpath);
		return;
	}

	strcpy(fullpath, FolderCache_GetFromVirtualLocation(fc, virtual_location));
	if (strEndsWith(fullpath, "/")) {
		fullpath[strlen(fullpath)-1]=0;
	}
	strcat(fullpath, FolderNodeGetFullPath(node, temp));

	FolderCacheForceUpdate(fc, relpath);
	node = FolderCacheQuery(fc, relpath); // Get the right virtual_location on the node for us

	if (node && node->virtual_location != virtual_location)
	{
		FolderCacheGetRealPath(fc, node, SAFESTR(temp));
		printfColor(COLOR_RED|COLOR_BRIGHT, "Change detected on %s,\n  but a project-specific override exists at %s.\n  Will process, but you will not be able to see the results unless you run a Core application.\n",
			fullpath, temp);

	}
	FolderNodeLeaveCriticalSection();

    eaPush( &fullpathsToReprocess, strdup( fullpath ));
}

static void texoptReloaded(const char *relpath)
{
	char fullpath[MAX_PATH];

	// Find the texture that this texopt is associated with and update it
	sprintf(fullpath, "%s/%s", fileSrcDir(), relpath);
	changeFileExt(fullpath, ".tga", fullpath);
	if (fileExists(fullpath))
        eaPush( &fullpathsToReprocess, strdup( fullpath ));

	changeFileExt(fullpath, ".dds", fullpath);
	if (fileExists(fullpath))
		eaPush( &fullpathsToReprocess, strdup( fullpath ));

	changeFileExt(fullpath, ".9.tga", fullpath); // Must be last
	if (fileExists(fullpath))
		eaPush( &fullpathsToReprocess, strdup( fullpath ));

	if (fileCoreSrcDir())
	{
		sprintf(fullpath, "%s/%s", fileCoreSrcDir(), relpath);
		changeFileExt(fullpath, ".tga", fullpath);
		if (fileExists(fullpath))
			eaPush( &fullpathsToReprocess, strdup( fullpath ));
		changeFileExt(fullpath, ".dds", fullpath);
		if (fileExists(fullpath))
			eaPush( &fullpathsToReprocess, strdup( fullpath ));
		changeFileExt(fullpath, ".9.tga", fullpath); // Must be last
		if (fileExists(fullpath))
			eaPush( &fullpathsToReprocess, strdup( fullpath ));
	}
}

/// Copy the pixels of the active render surface into TGA, ignoring
/// any pixels with a zero depth.  The pixels are blitted with
/// top-left at X, Y
void tgaCopyActiveSurfaceIntoAt( U8* tga, int tgaWidth, int tgaHeight, int x, int y )
{
    int tgaLinePitch = tgaWidth * 4;
    U8* tgaTopLeftPtr = tga + tgaLinePitch * y + 4 * x;
    
    RdrSurface* rdrSurface = rdrGetPrimarySurface( device );
    int rdrWidth;
    int rdrHeight;
    int rdrLinePitch;
    U8* rdrTopLeftPtr;
	rdrWidth = rdrGetPrimarySurface( device )->width_nonthread;
	rdrHeight = rdrGetPrimarySurface( device )->height_nonthread;
    rdrLinePitch = rdrWidth * 4;
    rdrTopLeftPtr = rdrGetActiveSurfaceData( device, SURFDATA_RGBA, rdrWidth, rdrHeight );
    
    assert( x + rdrWidth <= tgaWidth && y + rdrHeight <= tgaHeight );

    {
        int xIt;
        int yIt;

        for( yIt = 0; yIt != rdrHeight; ++yIt ) {
            for( xIt = 0; xIt != rdrWidth; ++xIt ) {
                U8* tgaPixel = tgaTopLeftPtr + tgaLinePitch * yIt + 4 * xIt; 
                U8* rdrPixel = rdrTopLeftPtr + rdrLinePitch * (rdrHeight - yIt - 1) + 4 * xIt;

                tgaPixel[ 0 ] = rdrPixel[ 0 ];
                tgaPixel[ 1 ] = rdrPixel[ 1 ];
                tgaPixel[ 2 ] = rdrPixel[ 2 ];
                tgaPixel[ 3 ] = 255;
            }
        }
    }
}

/// Write out NUM-ANGLES by NUM-SEEDS billboards for GROUP-NAME into the files
/// DIFFUSE-MAP-FNAME and NORMAL-MAP-FNAME.
///
/// Each billboard will be BILLBOARD-SIZE pixels (each dimension should be two
/// less than a power of two).  The group should all be visible in
/// BILLBOARD-VIEW-SIZE.
void generateGroupBillboards(
        GroupDef* group,
        const char* diffuseMapFname,
        const char* normalMapFname,
        int numAngles, int numSeeds,
        Vec2 billboardSize, Vec2 billboardViewSize,
        float normalMapScale )
{
    Vec2 effectiveBillboardSize;
    float effectiveDiffuseMapScale;
    float effectiveNormalMapScale;

    if( billboardSize[ 0 ] == -1 ) {
        effectiveBillboardSize[ 0 ] = ceil( billboardViewSize[ 0 ] / 150 * 256 );
    } else {
        effectiveBillboardSize[ 0 ] = billboardSize [ 0 ];
    }
    if( billboardSize[ 1 ] == -1 ) {
        effectiveBillboardSize[ 1 ] = ceil( billboardViewSize[ 1 ] / 150 * 256 );
    } else {
        effectiveBillboardSize[ 1 ] = billboardSize [ 1 ];
    }
    
    // Since all textures have to scaled up to the nearest power of two, for
    // accuracy it's best if the billboarding is done as close to that power of
    // two as possible.
    effectiveBillboardSize[ 0 ] = floor( ceilPower2( effectiveBillboardSize[ 0 ] * numAngles ) / numAngles );
    effectiveBillboardSize[ 1 ] = floor( ceilPower2( effectiveBillboardSize[ 1 ] * numSeeds ) / numSeeds );
    
    effectiveNormalMapScale = pow( 2, ceil( log( normalMapScale ) / log( 2 )));
    if( effectiveNormalMapScale > 1 ) {
        effectiveBillboardSize[ 0 ] = effectiveBillboardSize[ 0 ] * effectiveNormalMapScale;
        effectiveBillboardSize[ 1 ] = effectiveBillboardSize[ 1 ] * effectiveNormalMapScale;
        effectiveDiffuseMapScale = 1 / effectiveNormalMapScale;
        effectiveNormalMapScale = 1;
    } else {
        effectiveBillboardSize[ 0 ] = effectiveBillboardSize[ 0 ];
        effectiveBillboardSize[ 1 ] = effectiveBillboardSize[ 1 ];
        effectiveDiffuseMapScale = 1;
        effectiveNormalMapScale = effectiveNormalMapScale;
    }

    {
        int tgaWidth = effectiveBillboardSize[ 0 ] * numAngles;
        int tgaHeight = effectiveBillboardSize[ 1 ] * numSeeds;
        float aspectRatio = (float)tgaWidth / tgaHeight;

        if( aspectRatio > 8 ) {
            effectiveBillboardSize[ 1 ] = tgaWidth / 8 / numSeeds;
        } else if( aspectRatio < 1.0f / 8 ) {
            effectiveBillboardSize[ 0 ] = tgaHeight / 8 / numAngles;
        }
    }

    effectiveBillboardSize[ 0 ] = max( 16, effectiveBillboardSize[ 0 ]);
    effectiveBillboardSize[ 1 ] = max( 16, effectiveBillboardSize[ 1 ]);

    {
        int tgaWidth = effectiveBillboardSize[ 0 ] * numAngles;
        int tgaHeight = effectiveBillboardSize[ 1 ] * numSeeds;
        U8* tgaData = tgaCreateScratchData( tgaWidth, tgaHeight );
        U8* tgaAlphaData = tgaCreateScratchData( tgaWidth, tgaHeight );
        bool debugMode = false;

        assert( need_gfxlib );

        rdrShowWindow( device, SW_SHOW );

        {
            int need_lock = !device->is_locked_nonthread;
			DisplayParams display_settings = device->display_nonthread;
            if (need_lock)
				rdrLockActiveDevice(device, false);
            
			display_settings.width = effectiveBillboardSize[ 0 ];
			display_settings.height = effectiveBillboardSize[ 1 ];
			rdrSetSize(device, &display_settings);
			gfxActiveCameraControllerOverrideClearColor(zerovec4);
            if (need_lock)
				rdrUnlockActiveDevice(device, false, false, false);
        }
  
        // setup billboarding envirnomnt
        globCmdParse( "nosky 1" );
        globCmdParse( "wlTimeSet 10" );
        globCmdParse( "comicShading 0" );
        globCmdParse( "maxInactiveFps 0" );
        globCmdParse( "unlit 1" );
        globCmdParse( "showcampos 0" );
		globCmdParse( "showfps 0" );
        globCmdParse( "shaderTestN 0 NOWIND" );
    
        {
            int angleIt;
            int seedIt;
            int billboardModeIt;
            int timesIt;        //< There's a one frame delay on the camera
                                //< getting set, so each angle has to be
                                //< rendered twice. -- MJF
            float outputScale;

            enum { GEN_ALPHA, GEN_DIFFUSE, GEN_NORMAL, GEN_END };
            for( billboardModeIt = 0; billboardModeIt != GEN_END; ++billboardModeIt ) {
                char fnameBuffer[ CRYPTIC_MAX_PATH ];
            
                switch( billboardModeIt ) {
                    xcase GEN_ALPHA:
                    globCmdParse( "shaderTestN 1 ShowTouched" );
                    fnameBuffer[ 0 ] = '\0';
                    outputScale = 1;
                
                    xcase GEN_DIFFUSE:
                    globCmdParse( "shaderTestN 1 NoHDR" );
                    strcpy( fnameBuffer, diffuseMapFname );
                    outputScale = effectiveDiffuseMapScale;
                
                    xcase GEN_NORMAL:
                    globCmdParse( "shaderTestN 1 ShowNormals" );
                    strcpy( fnameBuffer, normalMapFname );
                    outputScale = effectiveNormalMapScale;
                }

                tgaClear( tgaData, tgaWidth, tgaHeight );
            
                for( seedIt = 0; seedIt != numSeeds; ++seedIt ) {
                    bool loadedResourcesIt = false;
                
                    for( angleIt = 0; angleIt != numAngles; ++angleIt ) {
                        for( timesIt = 0; timesIt != 2; ++timesIt ) {
                            F32 elapsed = 0.1;
        
                            // graphics window updating
                            updateThreadPriority();

                            // update libs
                            {
                                utilitiesLibOncePerFrame( elapsed, 1.0f );
								gfxOncePerFrame( elapsed, elapsed, false, true );
								worldLibOncePerFrame( elapsed );
                            }
                    
                            // update camera position
                            {
                                Vec3 center;
                                Vec3 camPos;

                                center[ 0 ] = 0;
                                center[ 1 ] = (group->bounds.min[ 1 ] + group->bounds.max[ 1 ]) / 2;
                                center[ 2 ] = 0;
                
                                copyVec3( center, camPos );
                                camPos[ 2 ] += 100;

                                gfxCameraControllerLookAt( camPos, center, upvec );
                            }

                            gfxRunActiveCameraController(-1, NULL);
                            {
                                float scale = (debugMode ? .6 : .5);
                            
                                rdrSetupOrthoDX(gfxGetActiveCameraView()->projection_matrix,
                                                -billboardViewSize[ 0 ] * scale, billboardViewSize[ 0 ] * scale,
                                                -billboardViewSize[ 1 ] * scale, billboardViewSize[ 1 ] * scale,
                                                50000, -50000);
                                frustumSetOrtho(&gfxGetActiveCameraView()->new_frustum, (billboardViewSize[ 0 ] * scale) / (billboardViewSize[ 1 ] * scale), 1, 0, 50000);
                            }
                            gfxTellWorldLibCameraPosition();
                            inpUpdateEarly(gfxGetActiveInputDevice());
                            inpUpdateLate(gfxGetActiveInputDevice());

                            // we're loading new stuff here, so wait for
                            // everything to finish loading AND render a
                            // successful frame before moving on.
                            if( !loadedResourcesIt )
                            {
                                gfxLoadingStartWaiting();
                                do
                                {
									gfxStartMainFrameAction(false, false, false, false, false);
                                    {
                                        float angle = -(TWOPI * angleIt / numAngles - HALFPI);
                                        TempGroupParams params = { 0 };
                                        Mat4 mat;

                                        copyMat4( unitmat, mat );
                                        yawMat3( angle, mat );
                                    
                                        params.seed = seedIt;
                                        worldAddTempGroup( group, mat, &params, true );
                                    }

                                    if( debugMode ) {
                                        Vec3 bl = { -billboardViewSize[ 0 ] / 2, 0, 0 };
                                        Vec3 br = { +billboardViewSize[ 0 ] / 2, 0, 0 };
                                        Vec3 tl = { -billboardViewSize[ 0 ] / 2, billboardViewSize[ 1 ], 0 };
                                        Vec3 tr = { +billboardViewSize[ 0 ] / 2, billboardViewSize[ 1 ], 0 };
                                        Color c;
                                        c.r = 255; c.g = 0; c.b = 255; c.a = 255;

                                        gfxDrawBox3D( bl, tr, unitmat, c, 1 );
                                    }
                                
									gfxFillDrawList(true, NULL);
                                    gfxDrawFrame();

                                    geoForceBackgroundLoaderToFinish();
                                    texForceTexLoaderToComplete( 1 );
                                } while( gfxLoadingIsStillLoading());
                                gfxLoadingFinishWaiting();

                                loadedResourcesIt = true;
                            }

							gfxStartMainFrameAction(false, false, false, false, false);
                            {
                                float angle = -(TWOPI * angleIt / numAngles - HALFPI);
                                TempGroupParams params = { 0 };
                                Mat4 mat;

                                copyMat4( unitmat, mat );
                                yawMat3( angle, mat );
                                    
                                params.seed = seedIt;
                                worldAddTempGroup( group, mat, &params, true );
                            }
                            if( debugMode ) {
                                Vec3 bl = { -billboardViewSize[ 0 ] / 2, 0, 0 };
                                Vec3 br = { +billboardViewSize[ 0 ] / 2, 0, 0 };
                                Vec3 tl = { -billboardViewSize[ 0 ] / 2, billboardViewSize[ 1 ], 0 };
                                Vec3 tr = { +billboardViewSize[ 0 ] / 2, billboardViewSize[ 1 ], 0 };
                                Color c;
                                c.r = 255; c.g = 0; c.b = 255; c.a = 255;

                                gfxDrawBox3D( bl, tr, unitmat, c, 1 );
                            }

							gfxFillDrawList(true, NULL);
							gfxDrawFrame();
                            gfxOncePerFrameEnd(false);
                        }
            
                        rdrLockActiveDevice(device, false);
                        tgaCopyActiveSurfaceIntoAt( (billboardModeIt == GEN_ALPHA ? tgaAlphaData: tgaData),
                                                    tgaWidth, tgaHeight,
                                                    angleIt * effectiveBillboardSize[ 0 ],
                                                    seedIt * effectiveBillboardSize[ 1 ]);
                        rdrUnlockActiveDevice(device, false, false, false);
                    }
                }

                if( billboardModeIt == GEN_ALPHA ) {
                    // These lines are drawn so that texcoords at the borders of
                    // the billboard do not end up sampling opaque pixels.
                    {
                        int it;
                    
                        for( it = 0; it < tgaWidth; it += effectiveBillboardSize[ 0 ]) {
                            tgaFillRect( tgaAlphaData, tgaWidth, tgaHeight, it, 0, 1, tgaHeight,
                                         0x00000000 );
                            tgaFillRect( tgaAlphaData, tgaWidth, tgaHeight, it + effectiveBillboardSize[ 0 ] - 1, 0, 1, tgaHeight,
                                         0x00000000 );
                        }
                        for( it = 0; it < tgaHeight; it += effectiveBillboardSize[ 1 ]) {
                            tgaFillRect( tgaAlphaData, tgaWidth, tgaHeight, 0, it, tgaWidth, 1,
                                         0x00000000 );
                            tgaFillRect( tgaAlphaData, tgaWidth, tgaHeight, 0, it + effectiveBillboardSize[ 1 ] - 1, tgaWidth, 1,
                                         0x00000000 );
                        }
                    }
                } else {
                    tgaCopyTgaAlphaMap( tgaData, tgaWidth, tgaHeight, tgaAlphaData );
                    {
                        int xIt, yIt;
                        for( yIt = 0; yIt < tgaHeight; yIt += effectiveBillboardSize[ 1 ]) {
                            for( xIt = 0; xIt < tgaWidth; xIt += effectiveBillboardSize[ 0 ]) {
                                srand( 0 ); //< The alpha noise should be the
                                            //< same for each billboard image.
                            
                                tgaAlphaPad( tgaData, tgaWidth, tgaHeight, xIt, yIt, effectiveBillboardSize[ 0 ], effectiveBillboardSize[ 1 ],
                                             max( effectiveBillboardSize[ 0 ], effectiveBillboardSize[ 1 ]));
                                tgaMultAlphaNoise( tgaData, tgaWidth, tgaHeight, xIt, yIt, effectiveBillboardSize[ 0 ], effectiveBillboardSize[ 1 ]);
                            }
                        }
                    }

                    {
                        int tgaOutputWidth = outputScale * tgaWidth;
                        int tgaOutputHeight = outputScale * tgaHeight;
                        U8* tgaOutputData = tgaCreateScratchData( tgaOutputWidth, tgaOutputHeight );
                        tgaScale( tgaOutputData, tgaOutputWidth, tgaOutputHeight,
                                  tgaData, tgaWidth, tgaHeight, true );
                        tgaSave( fnameBuffer, tgaOutputData, tgaOutputWidth, tgaOutputHeight, 4 );
                        free( tgaOutputData );
                    }
                }
            }
        }

        free( tgaAlphaData );
        free( tgaData );

        rdrShowWindow( device, SW_HIDE );
    }
}

void monitorAllFiles(void)
{
	char monitor_string[10000];
	char path[MAX_PATH];
	int i, folder=0;
	char extension[64];

	//folder_cache_debug = 2;

	//FolderCacheSetMode(FOLDER_CACHE_MODE_I_LIKE_PIGS);
	for (folder=0; folder<eaSize(&g_all_requests); ++folder)
	{
		FolderCache *fcvrmllib;
		strcpy(path, g_all_requests[folder]->path);
		forwardSlashes(path);
		if (!strEndsWith(path, "/"))
			strcat(path, "/");
		loadstart_printf("\nCaching %s...", path);
		fcvrmllib = FolderCacheCreate();
		FolderCacheAddFolder(fcvrmllib, path, folder, NULL, false);
		FolderCacheQueryEx(fcvrmllib, NULL, true, false);
		loadend_printf("");

		monitor_string[0] = 0;
		for (i = 0; i < eaSize(&lib_data[g_all_requests[folder]->targetlibrary].src_exts); ++i)
		{
			strcatf(monitor_string, "%s files", lib_data[g_all_requests[folder]->targetlibrary].src_exts[i]);
			if (i < eaSize(&lib_data[g_all_requests[folder]->targetlibrary].src_exts) - 2 || i == eaSize(&lib_data[g_all_requests[folder]->targetlibrary].src_exts) - 2 && eaSize(&lib_data[g_all_requests[folder]->targetlibrary].src_exts) > 2)
				strcat(monitor_string, ", ");
			if (i == eaSize(&lib_data[g_all_requests[folder]->targetlibrary].src_exts) - 2)
				strcat(monitor_string, "and ");

			sprintf(extension, "*%s", lib_data[g_all_requests[folder]->targetlibrary].src_exts[i]);
			FolderCacheSetCallbackEx(FOLDER_CACHE_CALLBACK_UPDATE, extension, reprocessOnTheFly, NULL);
		}

		consoleSetFGColor(COLOR_GREEN | COLOR_BRIGHT);
		printf("Now monitoring for changes in %s.\n", monitor_string);
		consoleSetDefaultColor();
	}

	if (need_texlib)
		texoptSetReloadCallback(texoptReloaded);

	printf("\n");

	// From now on, use file writability rather than Gimme to determine if a file should be written to.
	// This allows files to be processed quickly without having to check AssetMaster for each one.
	// This is supposed to be temporary until the permanent fix, which is to allow file ownership to be safely looked up
	// locally without having to consult AssetMaster.
	check_writeable_only = true;

	while (true) {
		FolderCacheDoCallbacks();
        processQueuedFullpaths();
		Sleep(200);
		commMonitor(commDefault());
		UpdateControllerConnection();
		DoConsoleDebugMenu(GetDefaultConsoleDebugMenu());
	}
}

static bool isSameRelativePath(const char *core_filename, const char *project_filename)
{
	const char *core = strstriConst(core_filename, "src/"); // /src/ or /CoreSrc/
	const char *project = strstriConst(project_filename, "src/");
	if (!core || !project)
		return false;
	return stricmp(core, project)==0;
}

void processAllFiles(GetVrmlFolderRequest *request)
{
	char** full_list = NULL;
	int full_count;
	bool had_dups=false;
	int i;
	bool check_dups = (request->targetlibrary==LIB_CHAR || request->targetlibrary==LIB_OBJ || request->targetlibrary==LIB_TEX);
	StashTable htDupNamesCheck = check_dups?stashTableCreateWithStringKeys(256, StashDeepCopyKeys_NeverRelease):NULL;
	U32 startTime = timerCpuMs();

	if (strchr(request->path, '*'))
	{
		char directory[MAX_PATH];
		strcpy(directory, request->path);
		getDirectoryName(directory);
		full_list = fileScanDir(directory);
		for (i = eaSize(&full_list) - 1; i >= 0; --i)
		{
			if (!simpleMatchExact(request->path, full_list[i]))
			{
				free(full_list[i]);
				eaRemove(&full_list, i);
			}
		}
	}
	else if (fileExists(request->path))
	{
        eaPush( &full_list, strdup( request->path ));
	}
	else
	{
		full_list = fileScanDir(request->path);
	}
    full_count = eaSize( &full_list );

	// now we have a list of all the files to process.. walk through them and process
	waitForGetVrmlLock(true);

	// If it's the animation lib, first look at the second_src_ext, .skel's, which must be processed first
	if (request->targetlibrary == LIB_ANIM)
	{
		for (i=0; i< full_count; ++i)
		{
			bool ok_to_process = true;
			if (check_dups)
			{
				char *old_name;
				char *file_name = getFileName(full_list[i]);

				if (stashFindPointer(htDupNamesCheck, file_name, &old_name)) {
					if (!isSameRelativePath(full_list[i], old_name))
					{
						filelog_printf("duplicate_geometry", "Duplicate geometry file named %s in \"%s\" and \"%s\"", file_name, full_list[i], old_name);
						printfColor(COLOR_RED|COLOR_BRIGHT, "\rDuplicate geometry file named %s in                                   \n      %s\n      %s\n", file_name, full_list[i], old_name);
						had_dups = true;
					}
					ok_to_process = false;
				} else {
					stashAddPointer(htDupNamesCheck, file_name, full_list[i], false);
				}
			}
			if (ok_to_process && strEndsWithAny(full_list[i], lib_data[request->targetlibrary].src_exts) && !strEndsWith(full_list[i], lib_data[request->targetlibrary].src_exts[0]))
			{
				processSkeleton(full_list[i], request->is_core, false);
				FolderCacheDoCallbacks(); // need to reload skeletons immediately!
			}
		}
	}

	for (i=0; i< full_count; ++i)
	{
		bool ok_to_process = true;
		if (strEndsWithAny(full_list[i], lib_data[request->targetlibrary].src_exts))
		{
			if (check_dups)
			{
				char *old_name;
				char file_name[MAX_PATH];
				getFileNameNoExt(file_name, full_list[i]);

				if (strEndsWith(file_name, ".9"))
					file_name[strlen(file_name)-2] = '\0'; // Remove .9.ext

				if (stashFindPointer(htDupNamesCheck, file_name, &old_name)) {
					if (!isSameRelativePath(full_list[i], old_name))
					{
						const char *author1 = gimmeDLLQueryLastAuthor(full_list[i]);
						const char *author2 = gimmeDLLQueryLastAuthor(old_name);
						filelog_printf("duplicate_file", "Duplicate file named %s in \"%s\" and \"%s\"", file_name, full_list[i], old_name);
						printfColor(COLOR_RED|COLOR_BRIGHT, "\rDuplicate file named %s in                                   \n      %s (%s)\n      %s (%s)\n", file_name,
							full_list[i], author1,
							old_name, author2);
						had_dups = true;
					}
					ok_to_process = false;
				} else {
					stashAddPointer(htDupNamesCheck, file_name, full_list[i], false);
				}
			}
			if (ok_to_process)
			{
				if (request->targetlibrary == LIB_ANIM)
				{
					if (strEndsWith(full_list[i], lib_data[request->targetlibrary].src_exts[0]))
						processAnim(full_list[i], request->is_core, false);
				}
				else
				{
					processFile(request, full_list[i], false);
				}
			}
		}
		if (timerCpuMs() - startTime > 1000 || i==full_count-1)
		{
			startTime = timerCpuMs();
			printf("\r%-200c\r  %d/%d (%1.0f%%)", ' ', i+1, full_count, (i+1)*100.f/full_count);
		}
	}

	releaseGetVrmlLock();

	printf("\r%-200c\n", ' ');

	if (had_dups) {
		char dummy;
		printfColor(COLOR_RED|COLOR_BRIGHT, "Duplicate file names detected, currently probably using one at random, or\n  whichever was processed first, please delete the ones that are not needed.\n");
		printf("Press any key to continue...                                                             \n");
		printf("(You may need to click Show Console in the MCP to get at this window to press a key)     \n");
		dummy = _getch();
	}


	if ( htDupNamesCheck )
		stashTableDestroy(htDupNamesCheck);

	fileScanDirFreeNames(full_list);
}

void checkDataDirOutputDirConsistency(const char *src)
{
	char *s;
	char data_dir[MAX_PATH];
	char src_dir[MAX_PATH];
	strcpy(data_dir, fileDataDir());
	fileLocateWrite(src, src_dir);
	forwardSlashes(data_dir);
	forwardSlashes(src_dir);
	s = strchr(data_dir, '/'); // c:
	if (s && strchr(s+1, '/'))
		s = strchr(s+1, '/'); // c:/game
	if (s)
		*s = 0;
	s = strchr(src_dir, '/'); // c:
	if (s && strchr(s+1, '/'))
		s = strchr(s+1, '/'); // c:/game
	if (s)
		*s = 0;
	if (stricmp(data_dir, src_dir)!=0) {
		Alertf("Trying to process from %s into %s, this is not allowed!", src_dir, data_dir);
		exit(1);
	}
}

static void printUsage(void)
{
	char dummy;
	printf("\nUsage:\n");
	printf("   'getvrml <geometry file.geo>' = print geo information\n");
	printf("   'getvrml -objlib <geometry folder>' = all vrml in folder to object library\n");
	printf("   'getvrml -charlib <character geometry folder>' = all vrml in folder to character library\n");
	printf("   'getvrml -animlib <anim folder>' = all danim and vrml in folder to animation library  \n");
	printf("   'getvrml -texlib <tex folder>' = all tga in folder to texture library  \n");
	printf("\t-monitor = stay open and monitor for updates after processing all files\n");
	printf("\t-force = force rebuild\n");
	printf("\t-force 2 = override gimme (rebuilds other people's files)\n");
	printf("\t-nocheckout = don't check anything out (used for testing changes to getvrml)\n");
	printf("\t-nolod = don't generate lod information (objlib and charlib only)\n");
	printf("\t-nvdxt <path> = set path to nvdxt executable (texlib only)\n");
	printf("\t-noprune = don't delete processed textures that have been deleted from src (texlib only)\n");
	printf("\t-onlyprune = only prune textures, don't process new ones (texlib only)\n");
	printf("\t-ddspause = pause between calling nvdxt and writing final texture file (texlib only)\n");
	dummy = getch();
}

//look for an argument in command line params
static int checkForArg(int argc, char **argv, char * str)
{
	int i;
	for(i = 0 ; i < argc ; i++)
	{
		if (strstr(argv[i], str))
			return i+1;
	}
	return 0;
}

AUTO_CMD_INT(g_no_checkout, nocheckout);
AUTO_CMD_INT(monitor, monitor);
AUTO_CMD_INT(hide_console, hide);
AUTO_CMD_INT(g_force_rebuild, f);
AUTO_CMD_INT(g_force_rebuild, force);
AUTO_CMD_INT(g_no_compression, nocompression);
AUTO_CMD_INT(g_quick_process, nolod);
AUTO_CMD_INT(g_quick_process, nolods);
AUTO_CMD_INT(g_quick_process, quick);
AUTO_CMD_INT(g_quick_process, quickprocess);
AUTO_CMD_INT(g_export_vrml, savevrml);
AUTO_CMD_INT(g_verboseReprocessing, verboseReprocessing);

AUTO_CMD_INT(dummy_int, emulate_outsource);
AUTO_CMD_INT(dummy_int, nopig);

AUTO_CMD_INT(g_test, test);

// gettex specific parameters
AUTO_CMD_STRING(tex_info, texinfo);
AUTO_CMD_STRING(nvdxt_path, nvdxt);
AUTO_CMD_INT(no_prune, noprune);
AUTO_CMD_INT(only_prune, onlyprune);
AUTO_CMD_INT(g_dds_pause, ddspause);

static void setOutputFolder(char* requestPath, int requestPath_size, const char *path, const char *default_folder)
{
	char cwd[256];

	char* dummy = fileGetcwd(cwd, ARRAY_SIZE(cwd));

	if (path[1]==':')
        strcpy_s(SAFESTR2( requestPath ), path); // absolute path
	else
        sprintf_s(SAFESTR2( requestPath ), "%s\\%s", cwd, path); // relative path
	forwardSlashes(requestPath);
	if (!strEndsWith(requestPath, "/"))
		strcat_s(SAFESTR2(requestPath), "/");
	if (strEndsWith(requestPath, "/src/"))
	{
		strcat_s(SAFESTR2(requestPath), default_folder);
	}
}

AUTO_COMMAND ACMD_NAME(objlib);
void processObjLib(const char *path)
{
	GetVrmlFolderRequest *request = calloc(1, sizeof(*request));
	eaPush(&g_all_requests, request);
	request->targetlibrary = LIB_OBJ;
	setOutputFolder(SAFESTR(request->path), path, "object_library/");
}

AUTO_COMMAND ACMD_NAME(objlib2);
void processObjLib2( const char* inputPath, const char* outputPath )
{
    GetVrmlFolderRequest *request = calloc(1, sizeof(*request));
    eaPush(&g_all_requests, request);
    request->targetlibrary = LIB_OBJ;
    setOutputFolder(SAFESTR(request->path), inputPath, "object_library/");
    setOutputFolder(SAFESTR(request->outputPath), outputPath, "object_library/");
}

AUTO_COMMAND ACMD_NAME(charlib);
void processCharLib(const char *path)
{
	GetVrmlFolderRequest *request = calloc(1, sizeof(*request));
	eaPush(&g_all_requests, request);
	request->targetlibrary = LIB_CHAR;
	setOutputFolder(SAFESTR(request->path), path, "character_library/");
	need_anim = 1; // Maybe?  Just setting this to be safe
}

AUTO_COMMAND ACMD_NAME(animlib);
void processAnimLib(const char *path)
{
	GetVrmlFolderRequest *request = calloc(1, sizeof(*request));
	eaPush(&g_all_requests, request);
	request->targetlibrary = LIB_ANIM;
	setOutputFolder(SAFESTR(request->path), path, "animation_library/");
	need_anim = 1;
}

//this command resets all previous charlib/animlib/texlib commands on the command line. The reason for this is that the
//MCP always adds a default one, so if you want to override that via the MCP command line, you need to do
//"-reset -texLib foo", because if you just do "-texlib foo", it will still process the default texlib first, which will
//take potentially a long time
AUTO_COMMAND ACMD_NAME(reset);
void resetLibRequests(void)
{
	eaClear(&g_all_requests);
}

AUTO_COMMAND ACMD_NAME(texlib);
void processTextureLib(const char *path)
{
	GetVrmlFolderRequest *request = calloc(1, sizeof(*request));
	eaPush(&g_all_requests, request);
	request->targetlibrary = LIB_TEX;
	setOutputFolder(SAFESTR(request->path), path, "texture_library/");
	need_texlib = 1;
    need_gfxlib = 1;
	need_materials = 1;
}

/// Maps a source file name to an earray of output file names
/// that file's generation is dependant on.
StashTable sourceDependanciesTable = NULL;

/// Maps an output file name to an earray of source file names that
/// depend on the output file to be processed.
StashTable outputDependantsTable = NULL;

typedef bool (*PredFn)( const void* elem );
typedef bool (*CompFn)( const void* elem1, const void* elem2 );
typedef void* (*CopyFn)( const void* elem );

/// Remove all members of EARRAY that satisfy PRED-FN.
///
/// If DESTRUCTOR is non-null, then those members are destroyed with
/// it.
void eaRemoveIf( EArrayHandle* earray, PredFn predFn, Destructor destructor )
{
    int it;

    for( it = 0; it != eaSize( earray ); ++it ) {
        if( predFn( (*earray)[ it ])) {
            if( destructor ) {
                destructor( (*earray)[ it ]);
            }
            eaRemove( earray, it );
            --it;
        }
    }
}

/// Add ELEM to EARRAY, but only if no other member compares equal to
/// it with COMP-FN.
///
/// If COPY-FN is non-null, it is used to copy ELEM.
void eaPushUniqueIf( cEArrayHandle* earray, const void* elem, CompFn compFn, CopyFn copyFn )
{
    int it;

    for( it = 0; it != eaSize( earray ); ++it ) {
        if( compFn( elem, (*earray)[ it ])) {
            return;
        }
    }

    eaPush( earray,
            (copyFn ? copyFn( elem ) : elem) );
}

static const char* cmpStr = NULL;

static bool StrEqual( const char* str1, const char* str2 )
{
    return stricmp( str1, str2 ) == 0;
}

static bool StrEqualToSaved( const char* str )
{
    return StrEqual( str, cmpStr );
}

static void freeFunc( void * p )
{
    free( p );
}
static char* strdupFunc( const char* c )
{
    return strdup( c );
}

/// Update the dependancy tables so that SOURCE-FILE depends on
/// OUTPUT-FILES (output by some other step) as dependancies.
void setOutputFileDependancy( const char* sourceFile, const char** outputFiles )
{
    if( sourceDependanciesTable == NULL ) {
        sourceDependanciesTable = stashTableCreateWithStringKeys( 256, StashDeepCopyKeys_NeverRelease );
    }
    if( outputDependantsTable == NULL ) {
        outputDependantsTable = stashTableCreateWithStringKeys( 256, StashDeepCopyKeys_NeverRelease );
    }

    // Sanity check!
    {
        int it;

        for( it = 0; it != eaSize( &outputFiles ); ++it ) {
			char buffer[ CRYPTIC_MAX_PATH ];
			strcpy( buffer, outputFiles[ it ]);
			forwardSlashes( buffer );

			if( fileIsAbsolutePath( outputFiles[ it ]) != (strstr( buffer, "/src/" ) != NULL)) {
				Errorf( "File <%s> depends on a file outside the project,\nnamely <%s>.\nDependancies will not be managed.",
						sourceFile, outputFiles[ it ]);
				return;
			} 
        }
    }
    
    // remove existing dependancies
    {
        char*** prevOutputFiles;

        stashFindPointer( sourceDependanciesTable, sourceFile, (void**)&prevOutputFiles );
        if( prevOutputFiles ) {
            int it;
            for( it = 0; it != eaSize( prevOutputFiles ); ++it ) {
                char*** outputDependants;

                stashFindPointer( outputDependantsTable, (*prevOutputFiles)[ it ], (void**)&outputDependants );

                cmpStr = sourceFile;
                eaRemoveIf( outputDependants, StrEqualToSaved, freeFunc );
            }

            stashRemovePointer( sourceDependanciesTable, sourceFile, NULL );
            eaDestroyEx( prevOutputFiles, freeFunc );
            free( prevOutputFiles );
        }
    }

    // add new dependancies
    {
        char*** newOutputFiles = calloc( 1, sizeof( *newOutputFiles ));

        eaCreate( newOutputFiles );
        eaCopyEx( &(char**)outputFiles, newOutputFiles, strdupFunc, freeFunc );

        assert( !stashFindPointer( sourceDependanciesTable, sourceFile, NULL ));
        stashAddPointer( sourceDependanciesTable, sourceFile, newOutputFiles, true );
        
        {
            int it;
            for( it = 0; it != eaSize( &outputFiles ); ++it ) {
                char* outputFile = (*newOutputFiles)[ it ];
                char*** outputFileDependants;

                if( !stashFindPointer( outputDependantsTable, outputFile, (void**)&outputFileDependants )) {
                    outputFileDependants = calloc( 1, sizeof( *outputFileDependants ));
                    eaCreate( outputFileDependants );
                    stashAddPointer( outputDependantsTable, outputFile, outputFileDependants, true );
                }

                eaPushUniqueIf( outputFileDependants, sourceFile, StrEqual, strdupFunc );
            }
        }
    }
}

/// Get an EArray of all files dependant on OUTPUT-FNAME. 
char** getOutputFileDependants( const char* outputFname )
{
    char*** accum;

    stashFindPointer( outputDependantsTable, outputFname, (void**)&accum ); 
    return accum ? *accum : NULL;
}

/// Get an EArray of all files SRC-FNAME has a dependency on.
char** getSourceFileDependencies( char* srcFname )
{
    char*** accum;

    stashFindPointer( sourceDependanciesTable, srcFname, (void**)&accum );
    return accum ? *accum : NULL;
}

/// Queue up any dependant files to be processed at a good time.
///
/// I can't process files immediately, since this is called in a
/// FolderCacheDoCallbacks call, but this needs to call
/// FolderCacheDoCallbacks.
static void reprocessOutputFileOnTheFly( FolderCache* fc, FolderNode* node, int virtual_location, const char* relpath, int when, void *userData)
{
    char fullPath[ CRYPTIC_MAX_PATH ];
    const char* fileName;
    char** files;

    FolderCacheGetRealPath( fc, node, SAFESTR( fullPath ));

    if( strstr( fullPath, "/src/" )) {
        fileName = fullPath;
    } else {
        fileName = getFileName( (char*)relpath );
    }
    files = getOutputFileDependants( fileName );

    {
        int it;
        for( it = 0; it != eaSize( &files ); ++it ) {
            eaPush( &fullpathsToReprocess, strdup( files[ it ]));
        }
    }
}

static bool sbConnectToControllerAndSendErrors = false;
AUTO_CMD_INT(sbConnectToControllerAndSendErrors, ConnectToControllerAndSendErrors) ACMD_COMMANDLINE;

U32 OVERRIDE_LATELINK_GetAppGlobalID(void)
{
	return getpid();
}

int wmain(int argc, WCHAR** argv_wide)
{
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	

	// Stuff to change console size
	HANDLE	console_handle = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO consoleScreenBufferInfo;
	SMALL_RECT	rect;

	int oldargc, argc_in;
	char *args[1000];

	char buf[1000]={0};

	int i;
	const char *core_src_dir;

	g_ccase_string_cache = true; // To generate .bins that match what the game expects

	SetAppGlobalType(GLOBALTYPE_GETVRML);
	ARGV_WIDE_TO_ARGV
	argc_in = argc;
	DO_AUTO_RUNS

	gimmeDLLDisable(1);

	memCheckInit();
	sharedMemorySetMode(SMM_DISABLED);

	gbCanAlwaysSurviveControllerDisconnect = true;

	loadCmdline("./cmdline.txt",buf,sizeof(buf));
	args[0] = getExecutableName();
	oldargc = 1 + tokenize_line_quoted_safe(buf,&args[1],ARRAY_SIZE(args)-1,0);

	argc = oldargc + argc_in - 1;
	memcpy(&args[oldargc], argv+1, (argc_in-1)*sizeof(char*));

	argv = args;

	if (argc >= 2 && checkForArg(argc, argv, "-emulate_outsource"))
		gimmeDLLDisable(true);

	if (!gimmeDLLQueryExists())
		FolderCacheSetMode(FOLDER_CACHE_MODE_I_LIKE_PIGS);
	else if (argc >= 2 && checkForArg(argc, argv, "-nopig"))
		FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY);
	else
		FolderCacheSetMode(FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC);

	setDefaultAssertMode();
	FolderCacheSetManualCallbackMode(1);
	FolderCacheStartMonitorThread();
	setNearSameVec3Tolerance(0.0005); // Default (0.03) is way too high!
	SetRefSystemSuppressUnknownDicitonaryWarning_All(true);

	consoleUpSize(220, 500);
	GetConsoleScreenBufferInfo(console_handle, &consoleScreenBufferInfo);
	//coord = GetLargestConsoleWindowSize(console_handle);
	rect = consoleScreenBufferInfo.srWindow;
	rect.Right=rect.Right>120?rect.Right:120;
	SetConsoleWindowInfo(console_handle, TRUE, &rect);
	printf("%s\n", argv[0]);
	setConsoleTitle("GetVrml");

	winRegisterMe("Open", ".geo2");
	winRegisterMe("Open", ".mset");
	winRegisterMe("Process", ".atmosphere");
	winRegisterMe("TexInfo", ".wtex");

	if (argc < 2)
	{
		printUsage();
		exit(0);
	}

	{
		char tempc;
		if (argc>=2)
		{
			tempc = argv[1][0];
			if (tempc != '-') {
				argv[1][0] = '\0';
			}
		}
		cmdParseCommandLine(argc, argv);
		if (argc>=2)
			argv[1][0] = tempc;
	}

	if (strEndsWith(argv[1], ".wtex") || strEndsWith(tex_info, ".wtex"))
    {
		need_texlib = 1;
        need_gfxlib = 1;
    }

	if (strEndsWith(argv[1], ".mset"))
	{
		setCavemanMode();
	}

	if (strEndsWith(argv[1], ".atmosphere"))
	{
		need_gfxlib = 1;
	}

	if (hide_console)
		hideConsoleWindow();

	fileAutoDataDir();

	utilitiesLibStartup();

	if (sbConnectToControllerAndSendErrors)
	{
		AttemptToConnectToController(false, NULL, false);
		SendErrorsToController();
	}

	if (strEndsWith(argv[1], ".geo2"))
	{
		geo2PrintFileInfo(argv[1]);
		_getch();
		return 0;
	}

	if (strEndsWith(argv[1], ".mset"))
	{
		geo2PrintBinFileInfo(argv[1], true, false);
		_getch();
		return 0;
	}

    keybind_Init( printf, NULL, NULL, "FreeCamera" );

    globCmdParse( "windowed 1" );

//     if( !createPrimaryDevice(GetModuleHandle( NULL ))) {
//         FatalErrorf( "Failed to create render device!" );
//     }
    //gfxDisplayLogo( device, logo_jpg_data, logo_data_size, WINDOW_TITLE );

	
	wlSetLoadFlags(WL_NO_LOAD_ZONEMAPS|WL_NO_LOAD_DYNFX|WL_NO_LOAD_COSTUMES|(need_anim?0:WL_NO_LOAD_DYNANIMATIONS)|(need_materials?0:WL_NO_LOAD_MATERIALS)|WL_NO_LOAD_DYNMISC);
	systemSpecsInit();
	system_specs.material_hardware_supported_features = SGFEAT_ALL; // To Test valid fallbacks: SGFEAT_SM20;

	AutoStartup_SetTaskIsOn("WorldLib", 1);
	if (need_gfxlib)
	{
		AutoStartup_SetTaskIsOn("GraphicsLib", 1);
	}
	else
	{
		gfxPretendLibIsNotHere();
		AutoStartup_RemoveAllDependenciesOn("GraphicsLib");
		AutoStartup_RemoveAllDependenciesOn("GraphicsLibEarly");
	}

	DoAutoStartup();
	resFinishLoading();



	if (need_gfxlib)
	{
		int		nvdxt_version_8=0;

		// Detect appropriate NVDXT.EXE
		if (!nvdxt_path[0]) {
			fileSpecialDir("tools", SAFESTR(nvdxt_path));
			Strcat(nvdxt_path, "/bin/nvdxt.exe");
		}
		if (!fileExists(nvdxt_path) && fileCoreToolsBinDir()) {
			sprintf(nvdxt_path, "%s/nvdxt.exe", fileCoreToolsBinDir());
		}
		if (!fileExists(nvdxt_path)) {
			printfColor(COLOR_RED|COLOR_BRIGHT, "Warning: unable to find appropriate NVDXT.EXE\n");
			Strcpy(nvdxt_path, "nvdxt"); // Hope it's in the path
		} else {
			nvdxt_version_8 = fileSize(nvdxt_path) > 500*1024;
		}
		backSlashes(nvdxt_path);

		if (!nvdxt_version_8) {
			Alertf("Could not find NVDXT v8");
			exit(1);
		}

//         registerPrimaryDevice( GetModuleHandle( NULL ));
//         gfxSetActiveDevice( device );
//         gfxSetActiveSurface(rdrGetPrimarySurface( device ));
	}

	if (strEndsWith(argv[1], ".mset") || strEndsWith(argv[1], ".geo2"))
	{
		if (strEndsWith(argv[1], ".geo2"))
		{
			geo2PrintFileInfo(argv[1]);
		}
		else if (strEndsWith(argv[1], ".mset"))
		{
		}
		_getch();
		return 0;
	}

	if (strEndsWith(argv[1], ".atmosphere"))
	{
		gfxCreateAtmosphereLookupTexture(argv[1]);
		printf("\nPress a key to exit.\n");
		_getch();
		return 0;
	}

	if (strEndsWith(argv[1], ".wtex"))
	{
		texPrintInfo(argv[1]);
		_getch();
		return 0;
	}

	if (strEndsWith(tex_info, ".wtex"))
	{
		texPrintInfo(tex_info);
		_getch();
		return 0;
	}

	if (!eaSize(&g_all_requests))
	{
		printUsage();
		return 0;
	}

	printf("\n");

	core_src_dir = fileCoreSrcDir();
	for (i = 0; i < eaSize(&g_all_requests); ++i)
	{
		GetVrmlFolderRequest *request = g_all_requests[i];

		assert(request->path[1]==':');

		if (core_src_dir && strStartsWith(request->path, core_src_dir))
			request->is_core = true;

		if (!request->is_core)
			checkDataDirOutputDirConsistency(request->path);

		// Only allow one copy of GetVrml to run per target type
        if( request->outputPath[ 0 ] == 0 ) //< If they're specifying an output path on the command
                                            //< line, it's either a recursive call (safe!) or an
                                            //< expert user.
        {
            if (!lib_data[request->targetlibrary].hMutex[!!request->is_core])
            {
                DWORD result;
                char mutex_name[1024];
                char path[1024];
                const char *process_type;
                strcpy(path, request->path+3);
                backSlashes(path);
                if (strchr(path, '\\'))
                    *strchr(path, '\\') = '\0';
                process_type = lib_data[request->targetlibrary].lib_name;
                sprintf(mutex_name, "Global\\CrypticGetVrml_%s_%s", path, process_type);
                lib_data[request->targetlibrary].hMutex[!!request->is_core] = CreateMutex_UTF8(NULL, 0, mutex_name);
                WaitForSingleObjectWithReturn(lib_data[request->targetlibrary].hMutex[!!request->is_core], 0, result);
                if (!(result == WAIT_OBJECT_0 || result == WAIT_ABANDONED))
                {
                    // mutex locked
                    ReleaseMutex(lib_data[request->targetlibrary].hMutex[!!request->is_core]);
                    CloseHandle(lib_data[request->targetlibrary].hMutex[!!request->is_core]);
                    Alertf("GetVrml is already running processing %s %s.", path, process_type);
                    exit(0);
                }
            }
        }
	}

	for (i = 0; i < eaSize(&g_all_requests); ++i)
	{
		if (!only_prune)
		{
			printf("Scanning %s...\n", g_all_requests[i]->path);
			processAllFiles(g_all_requests[i]);
			printf("\r%-200c\r", ' ');
		}

		if (g_all_requests[i]->targetlibrary == LIB_TEX && !no_prune && (strEndsWith(g_all_requests[i]->path, "/texture_library") || strEndsWith(g_all_requests[i]->path, "/texture_library/")))
		{
			loadstart_printf("Pruning textures for %s\n", g_all_requests[i]->path);
			pruneTextures(g_all_requests[i]->path);
			loadend_printf("\ndone pruning textures");
		}
	}

	_flushall();
	Sleep(250);

	if (g_test)
	{
		printf("\nCount: %d\nPress a key to exit...", g_test_count);
		getch();
	}

    processQueuedFullpaths();

	if (monitor)
		monitorAllFiles();

	worldLibShutdown();

    if (device)
	{
		if (device)
		{
			gfxUnregisterDevice(device);
			rdrDestroyDevice(device);
		}
    }

    return 0;

	EXCEPTION_HANDLER_END
}

/// Calculate the next lowest power of two for VALUE. 
int floorPower2( int value )
{
    return 1 << highBitIndex( value );
}

/// Calculate the next highest power of two for VALUE.
int ceilPower2( int value )
{
    return floorPower2( (value << 1) - 1 );
}

#include "main_c_ast.c"


#include "MapDescription.h"
#include "GlobalEnums.h"
#include "GlobalEnums_h_ast.c"
#include "MapDescription_h_ast.c"
