#include "utilitiesLib.h"

#include "cmdparse.h"
#include "gimmeDLLWrapper.h"
#include "GlobalTypes.h"
#include "SharedMemory.h"
#include "textparser.h"
#include "serialize.h"

#include "utils.h"
#include "sysutil.h"
#include "hoglib.h"
#include "fileutil.h"
#include "fileWatch.h"
#include "ImageTypes.h"

#include "GenericMesh.h"
#include "GenericMeshRemesh.h"
#include "logging.h"

// TODO SIMPLYGON reenable symstore

// Enable this option to keep the temp data folder after the remesh process, for debugging.
int debug_preserve_temp_data = 0;
AUTO_CMD_INT(debug_preserve_temp_data, debug_preserve_temp_data) ACMD_CATEGORY(DEBUG) ACMD_CMDLINEORPUBLIC;

// Enable this option to emit .OBJ files for all input models, for inspection.
int debug_dump_model_objs = 0;
AUTO_CMD_INT(debug_dump_model_objs, debug_dump_model_objs) ACMD_CATEGORY(DEBUG) ACMD_CMDLINEORPUBLIC;

// This flag overrides the path to the location of the nvdxt application. Specify without a trailing backslash.
// The default is the same folder as the remesh executable itself.
char nvdxtFullPath[MAX_PATH];
AUTO_CMD_STRING(nvdxtFullPath, nvdxtFullPath) ACMD_CMDLINEORPUBLIC;

char inputClusterDataFile[1024] = "";
AUTO_CMD_STRING(inputClusterDataFile, inputClusterDataFile) ACMD_CMDLINEORPUBLIC;
char outputRemeshDataFile[1024] = "";
AUTO_CMD_STRING(outputRemeshDataFile, outputRemeshDataFile) ACMD_CMDLINEORPUBLIC;

int overrideTextureResolution = -1;
AUTO_CMD_INT(overrideTextureResolution, overrideTextureResolution) ACMD_CATEGORY(DEBUG) ACMD_CMDLINEORPUBLIC;
int overrideGeometryResolution = -1;
AUTO_CMD_INT(overrideGeometryResolution, overrideGeometryResolution) ACMD_CATEGORY(DEBUG) ACMD_CMDLINEORPUBLIC;

enum RemeshStatusCodes
{
	INPUT_FILE_NOTSPECIFIED		= -1,
	INPUT_FILE_READ_FAILURE		= -2,
	INPUT_FILE_MISSING_IN_HOGG	= -3,
	REMESH_FAILED				= -4,
	DXT_COMPRESS_FAILED			= -5,
};

#define TEMP_CLUSTER_DATA_DIRECTORY "cluster_temp_data"

static void hogFileAddGMesh(HogFile * hogFile, GMesh * gmesh, const char * filename)
{
	SimpleBufHandle meshBuf = SimpleBufOpenWrite(filename, true, hogFile, false, false);
	gmeshWriteBinData(gmesh, meshBuf);
	SimpleBufClose(meshBuf);
}

// Zero indicates success.
int remeshClusterData(const char *clusterDataFile, const char *outputDataFile)
{
	ModelClusterSource* modelCluster = NULL;
	bool hoggCreated = false;
	int hoggError = 0;
	HogFile * clusterHogg = NULL;
	GMesh * mesh = NULL;
	int remeshResultCode = 0;
	char baseClusterName[MAX_PATH];
	char clusterTempDataDir[MAX_PATH];
	DWORD currentProcessId = GetCurrentProcessId();
	DWORD currentThreadId = GetCurrentThreadId();

	sprintf(clusterTempDataDir, "%s-PID%u-TID%u", TEMP_CLUSTER_DATA_DIRECTORY, currentProcessId, currentThreadId);
	fileLocateWrite(clusterTempDataDir, clusterTempDataDir);

	mkdir(clusterTempDataDir);

	fileGetFilename(clusterDataFile, baseClusterName);
	fileStripFileExtension(baseClusterName);
	clusterHogg = hogFileRead(clusterDataFile, &hoggCreated, PIGERR_ASSERT, &hoggError, HOG_NOCREATE);
	if (hoggError)
	{
		remeshResultCode = hoggError;
	}
	else
	{
		modelCluster = modelClusterSourceHoggLoad(clusterHogg, clusterTempDataDir);
		if (!modelCluster)
		{
			printf("Error: No Cluster_Source.mcst, or opening it failed\n");
			remeshResultCode = INPUT_FILE_MISSING_IN_HOGG;
		}
		if (overrideGeometryResolution != -1)
			modelCluster->cluster_volumes_condensed->geo_resolution = overrideGeometryResolution;
		if (overrideTextureResolution != -1)
			modelCluster->cluster_volumes_condensed->texture_width = modelCluster->cluster_volumes_condensed->texture_height = overrideTextureResolution;

		if (debug_dump_model_objs)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(modelCluster->cluster_models, ModelClusterObject, inputModel)
			{
				if (inputModel->gmesh)
				{
					char outputObj[MAX_PATH];
					strcpy(outputObj, inputModel->mesh_filename);
					fileStripFileExtension(outputObj);
					exportGmeshToObj(inputModel->gmesh, outputObj);
				}
			}
			FOR_EACH_END;
		}
	}

	if (clusterHogg)
		hogFileDestroy(clusterHogg, true);
	if (!remeshResultCode)
	{
		U64 start, end, time_delta;
		WorldClusterStats remeshStats = { 0 };
		StructInit(parse_WorldClusterStats, &remeshStats);

		mesh = callocStruct(GMesh);
		GET_CPU_TICKS_64(start);
		if (!remeshModels(modelCluster, mesh, clusterTempDataDir, baseClusterName, &remeshStats))
			remeshResultCode = REMESH_FAILED;
		else
		{
			char remeshImageName[MAX_PATH];
			char remeshImageFilePath[MAX_PATH];
			const char * imageSuffixes[ 3 ] = { "D", "S", "N" };
			int i;
			F32 deltaSeconds;

			GET_CPU_TICKS_64(end);
			time_delta = end - start;
			deltaSeconds = timerSeconds64(time_delta) / 1000.0;
			log_printf(LOG_SIMPLYGON,"Remesh time: %f seconds\n",deltaSeconds);

			clusterHogg = hogFileRead(outputDataFile, &hoggCreated, PIGERR_ASSERT, &hoggError, HOG_MUST_BE_WRITABLE);
			hogDeleteAllFiles(clusterHogg);

			for (i = 0; i < ARRAY_SIZE_CHECKED(imageSuffixes); ++i)
			{
				float imageScale = 1.0 / modelCluster->cluster_volumes_condensed->texture_super_sample;

				if ((i == 2 && !modelCluster->cluster_volumes_condensed->include_normal) ||
					(i == 1 && !modelCluster->cluster_volumes_condensed->include_specular))
				{
					continue;
				}

				sprintf(remeshImageName, "%s_%s.png", baseClusterName, imageSuffixes[i]);
				sprintf(remeshImageFilePath, "%s/%s", clusterTempDataDir, remeshImageName);

				if (!modelClusterMoveRemeshImagesToHogg(clusterHogg, remeshImageName, remeshImageFilePath,  
					&remeshStats.wtexCompress, &remeshStats.cacheTex, imageScale, i == 0 ? RTEX_DXT1 : RTEX_DXT5))
				{
					remeshResultCode = DXT_COMPRESS_FAILED;
					break;
				}
			}
			sprintf(remeshImageName, "%s.msh", baseClusterName);
			hogFileAddGMesh(clusterHogg, mesh, remeshImageName);
			ParserWriteBinaryFile("buildStats.bin", NULL, parse_WorldClusterStats, &remeshStats, NULL, NULL, NULL, NULL, 0, 0, clusterHogg, PARSERWRITE_IGNORECRC, 0);
			hogFileDestroy(clusterHogg, true);

			if (debug_dump_model_objs)
				exportGmeshToObj(mesh, baseClusterName);
		}

		StructDeInit(parse_WorldClusterStats, &remeshStats);

		gmeshFree(mesh);
		mesh = NULL;
	}

	if (!debug_preserve_temp_data)
		rmdirtreeEx(clusterTempDataDir, 0);

	StructDestroy(parse_ModelClusterSource, modelCluster);

	return remeshResultCode;
}


int wmain(int argc, WCHAR** argv_wide)
{
	EXCEPTION_HANDLER_BEGIN
	int exitCode = 0;
	char **argv;

	ARGV_WIDE_TO_ARGV
	WAIT_FOR_DEBUGGER

	SetAppGlobalType(GLOBALTYPE_GETVRML);

	DO_AUTO_RUNS

	memCheckInit();
	sharedMemorySetMode(SMM_DISABLED);
	setDefaultAssertMode();
	fileWatchSetDisabled(true);
	dontLogErrors(true);
	gimmeDLLDisable(true);
	fileAllPathsAbsolute(true);
	utilitiesLibStartup();
	cmdParseCommandLine(argc, argv);

	if (strcmp(inputClusterDataFile, "") && strcmp(outputRemeshDataFile, ""))
		exitCode = remeshClusterData(inputClusterDataFile, outputRemeshDataFile);
	else
	{
		printf("Usage:\n\nremesh -inputClusterDataFile INPUT_CLUSTER_HOGG_FILE.HOGG -outputRemeshDataFile OUTPUT_FILE.HOGG");
		exitCode = INPUT_FILE_NOTSPECIFIED;
	}

	return exitCode;

	EXCEPTION_HANDLER_END
}
