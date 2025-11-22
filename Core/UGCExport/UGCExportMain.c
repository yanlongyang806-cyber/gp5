#include "UGCExportImport.h"

#include "pyLib.h"
#include "sysutil.h"
#include "gimmeDLLWrapper.h"
#include "file.h"
#include "hoglib.h"
#include "errornet.h"
#include "MemoryMonitor.h"
#include "utilitiesLib.h"
#include "SharedMemory.h"
#include "Organization.h"
#include "pcl_client.h"
#include "ScratchStack.h"
#include "error.h"
#include "FolderCache.h"
#include "cmdparse.h"
#include "sock.h"
#include "StringUtil.h"

#include "UGCProjectUtils.h"
#include "UGCProjectCommon.h"
#include "UGCProjectCommon_h_ast.h"

#define PCL_DO_ERROR()																	\
{																						\
	if(patcherror != PCL_SUCCESS)														\
	{																					\
		char *msg = ScratchAlloc(MAX_PATH);												\
		char *details = NULL;															\
		pclGetErrorString(patcherror, msg, MAX_PATH);									\
		pclGetErrorDetails(patchclient, &details);										\
		strcat_s(msg + strlen(msg), MAX_PATH - strlen(msg), ", State: ");				\
		pclGetStateString(patchclient, msg + strlen(msg), MAX_PATH - strlen(msg));		\
		printfColor(COLOR_RED, "PCL error: %s%s%s. Client: %s",							\
			msg,																		\
			details ? ": " : "",														\
			details,																	\
			pclGetUsefulDebugString_Static(patchclient));								\
		ScratchFree(msg);																\
		return;																			\
	}																					\
}

#define PCL_DO(fn)		\
{						\
	patcherror = fn;	\
	PCL_DO_ERROR();		\
}

#define PCL_DO_WAIT(fn)					\
{										\
	PCL_DO(fn);							\
	patcherror = pclWait(patchclient);	\
	PCL_DO_ERROR();						\
}

static char s_hoggfilename[MAX_PATH] = "";
AUTO_CMD_STRING(s_hoggfilename, hogg) ACMD_ACCESSLEVEL(0);

static char s_machine[MAX_PATH] = "";
AUTO_CMD_STRING(s_machine, machine) ACMD_ACCESSLEVEL(0);

static char s_username[MAX_PATH] = "";
AUTO_CMD_STRING(s_username, username) ACMD_ACCESSLEVEL(0);

static char s_patchserver[MAX_PATH] = "";
AUTO_CMD_STRING(s_patchserver, patchserver) ACMD_ACCESSLEVEL(0);

static int s_patchport = 0;
AUTO_CMD_INT(s_patchport, patchport) ACMD_ACCESSLEVEL(0);

static char s_patchproject[MAX_PATH] = "";
AUTO_CMD_STRING(s_patchproject, patchproject) ACMD_ACCESSLEVEL(0);

static char s_patchshard[MAX_PATH] = "";
AUTO_CMD_STRING(s_patchshard, patchshard) ACMD_ACCESSLEVEL(0);

static U32 s_ugcprojectid = 0;
AUTO_CMD_INT(s_ugcprojectid, ugcprojectid) ACMD_ACCESSLEVEL(0);

static bool s_include_published = 1;
AUTO_CMD_INT(s_include_published, include_published) ACMD_ACCESSLEVEL(0);

static bool s_include_saved = 1;
AUTO_CMD_INT(s_include_saved, include_saved) ACMD_ACCESSLEVEL(0);

static char s_searchfilename[MAX_PATH] = "";
AUTO_CMD_STRING(s_searchfilename, search) ACMD_ACCESSLEVEL(0);

static bool s_version = 1;
AUTO_CMD_INT(s_version, version) ACMD_ACCESSLEVEL(0);

static bool s_do_patching = 1;
AUTO_CMD_INT(s_do_patching, do_patching) ACMD_ACCESSLEVEL(0);

static void InMemoryIterator(void *patchclient, const char *filename, const char *data, U32 size, U32 modtime, HogFile *hogg)
{
	if(data)
	{
		int decompressedSize;
		char *text = UGCExportImport_DecompressMemoryToText(data, size, &decompressedSize);
		if(text)
		{
			char *decompressedFilename = estrCreateFromStr(filename);
			estrTruncateAtLastOccurrence(&decompressedFilename, '.');
			hogFileModifyUpdateNamedSync(hogg, decompressedFilename, text, decompressedSize, modtime, NULL);
			hogFileModifyFlush(hogg);
			estrDestroy(&decompressedFilename);
		}
		else
			printfColor(COLOR_RED | COLOR_BRIGHT, "\nERROR: Failed to decompress UGCProjectData. Something has gone wrong!\n");
	}
}

static void pclGetFileList_CB(PCL_Client *patchclient, PCL_ErrorCode error, const char *error_details, HogFile *hogg, const char *const *fileNames)
{
	if(PCL_SUCCESS == error)
	{
		PCL_ErrorCode patcherror;
		PCL_DO(pclForEachInMemory(patchclient, InMemoryIterator, hogg));
	}
}

static UGCSearchResult *UGCExport_SearchStage()
{
	UGCSearchResult *pUGCSearchResult = NULL;

	if(s_ugcprojectid > 0)
	{
		UGCContentInfo *pUGCContentInfo = StructCreate(parse_UGCContentInfo);
		pUGCSearchResult = StructCreate(parse_UGCSearchResult);
		pUGCContentInfo->iUGCProjectID = s_ugcprojectid;
		eaPush(&pUGCSearchResult->eaResults, pUGCContentInfo);
	}
	else
	{
		char *searchKey = NULL;
		UGCSearchResult *pPartialResult = NULL;
		bool bDoneSearching = false;
		UGCProjectSearchInfo *pUGCProjectSearchInfo = NULL;

		if(s_searchfilename[0])
		{
			pUGCProjectSearchInfo = StructCreate(parse_UGCProjectSearchInfo);
			if(PARSERESULT_ERROR == ParserReadTextFile(s_searchfilename, parse_UGCProjectSearchInfo, pUGCProjectSearchInfo, 0))
			{
				printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Couldn't parse UGCProjectSearchInfo from %s!\n", s_searchfilename);
				StructDestroy(parse_UGCProjectSearchInfo, pUGCProjectSearchInfo);
				return NULL;
			}
		}

		searchKey = UGCExportImport_SearchInit(s_include_saved, s_include_published, pUGCProjectSearchInfo);
		if(!searchKey)
		{
			printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Python script \"UGCExportImport\" SearchInit function - did not return a string for the search key!\n");
			return NULL;
		}

		pUGCSearchResult = StructCreate(parse_UGCSearchResult);

		do
		{
			int index;
			pPartialResult = UGCExportImport_SearchNext(searchKey);
			if(pPartialResult)
			{
				for(index = 0; index < eaSize(&pPartialResult->eaResults); index++)
					eaPush(&pUGCSearchResult->eaResults, StructClone(parse_UGCContentInfo, pPartialResult->eaResults[index]));
				StructDestroySafe(parse_UGCSearchResult, &pPartialResult);
			}
			else
				bDoneSearching = true;
		} while(!bDoneSearching);

		PyMem_Free(searchKey);
	}

	return pUGCSearchResult;
}

static char *estrCreateProjectFileName(UGCProject *pUGCProject, UGCProjectVersion *pUGCProjectVersion)
{
	char *fileName = NULL;
	estrPrintf(&fileName, "C:\\UGCExportTemp\\data\\ns\\%s\\project\\%s.gz", pUGCProjectVersion->pNameSpace, pUGCProject->pIDString);
	return fileName;
}

static bool hoggProjectFileExists(HogFile *hogg, UGCProject *pUGCProject, UGCProjectVersion *pUGCProjectVersion)
{
	HogFileIndex hfIndex;
	char hoggFileName[MAX_PATH];
	sprintf(hoggFileName, "Data/ns/%s/project/%s", pUGCProjectVersion->pNameSpace, pUGCProject->pIDString);
	hfIndex = hogFileFind(hogg, hoggFileName);
	return ((U32)-1 != hfIndex);
}

int wmain(int argc, WCHAR** argv_wide)
{
	char **argv;

	EXCEPTION_HANDLER_BEGIN;
	ARGV_WIDE_TO_ARGV

	WAIT_FOR_DEBUGGER;

	setDefaultProductionMode(1);

	DO_AUTO_RUNS

	setDefaultAssertMode();
	gimmeDLLDisable(1);
	hogSetAllowUpgrade(true);

	errorTrackerEnableErrorThreading(false);
	FolderCacheChooseMode();
	memMonitorInit();
	utilitiesLibStartup();
	sharedMemorySetMode(SMM_DISABLED);
	sockStart();

	cmdParseCommandLine(argc, argv);

	if(UGCExportImport_InitPython())
	{
		bool bHoggFileNameProvided = false;
		bool bSuccess = true;
		bool bResume = false;

		bool bCreated = false;
		int err_return = 0;
		HogFile *hogg = NULL;

		if(0 == strlen(s_hoggfilename) && 0 == strlen(s_machine))
		{
			printf("UGCExport Usage:\n");
			printf("\t-hogg filename\n");
			printf("\t   Specifies the hogg file name to export UGC content into, or a previous hogg file that UGCExport should resume\n");
			printf("\t   exporting to\n");
			printf("\t-machine machinename\n");
			printf("\t   Specifies the machine name running the shard's Server Monitor that should have its UGC projects exported from\n");
			printf("\t-username username\n");
			printf("\t   Specifies the username to use for logging into the machine's Server Monitor\n");
			printf("\t-patchserver server\n");
			printf("\t   Specifies the an override Patch Server name to use for downloading the UGC project source data, instead of the one\n");
			printf("\t   configured for the shard\n");
			printf("\t-patchport number\n");
			printf("\t   Specifies the an override Patch Server port to use for downloading the UGC project source data, instead of the one\n");
			printf("\t   configured for the shard\n");
			printf("\t-patchproject projectname\n");
			printf("\t   Specifies the an override Patch Server project name to use for downloading the UGC project source data,\n");
			printf("\t   instead of the one configured for the shard\n");
			printf("\t-patchshard shardname\n");
			printf("\t   Specifies the an override shard name to use for later import of the UGC projects,\n");
			printf("\t   instead of the shard name returned for the shard\n");
			printf("\t-ugcprojectid id\n");
			printf("\t   Specifies that only the UGC project with the ID specified is to be exported.\n");
			printf("\t-include_published 1|0\n");
			printf("\t   Determines whether or not to include published UGC Project versions in the search result. If not set, published version is still patched if present. Ignored when using -search. Default is 1 (true).\n");
			printf("\t-include_saved 1|0\n");
			printf("\t  Determines whether or not to include saved UGC Project versions in the search result. If not set, saved version is still patched if present. Ignored when using -search. Default is 1 (true).\n");
			printf("\t-search filename\n");
			printf("\t   Specifies a UGCProjectSearchInfo file name to use to filter list of exported projects and series.\n");
			printf("\t-version 1|0\n");
			printf("\t   Displays the patch version of the shard being exported from. Turning this off can speed up a resume that only needs access to\n");
			printf("\t   the patchserver. Default is 1 (true).\n");
			printf("\t-do_patching 1|0\n");
			printf("\t   Determines whether or not to patch the UGC Project data. Default is 1 (true).\n");
			printf("\n");
			printf("There are 2 typical use cases. The first is for starting the export,\n");
			printf("the second is for resuming the export in the case that the Patch Server failed to patch all of the projects.\n");
			printf("\n");
			printf("\t1: UGCExport.exe -machine mimicexternal -username andrewa -hogg UGCExportForMimicToLive.hogg\n");
			printf("\t2: UGCExport.exe -hogg UGCExportForMimicToLive.hogg\n");
			printf("\n");
			printf("In the second case, since the hogg file already exists,\n");
			printf("you will be prompted whether or not to start over and overwrite the hogg file or to resume patching.\n");
			printf("The command line arguments provided when building the expport hogg was started are journaled in the hogg. Therefore,\n");
			printf("resuming will work.\n");
			printf("\n");
			printf("Another, less common, use case is to run UGCExport against a local shard using an objectdb from a live environment. This\n");
			printf("requires you to provide the original Patch Project for the uploaded, live projects:\n");
			printf("\n");
			printf("\tUGCExport.exe -machine localhost -patchproject Mimic_Nightugc -patchshard Mimic -hogg UGCExport.hogg\n");
			return 0;
		}

		if(0 == strlen(s_hoggfilename))
		{
			char *datetimestr = timeGetDateStringFromSecondsSince2000(timeSecondsSince2000());
			char *chr = NULL;
			char buffer[1024];
			sprintf(buffer, "UGCExport_%s_%s.hogg", s_machine, datetimestr);
			while(chr = strchr(buffer, ' '))
				*chr = '_';
			while(chr = strchr(buffer, ':'))
				*chr = '-';
			sprintf(s_hoggfilename, "C:\\%s", buffer);
		}
		else
			bHoggFileNameProvided = true;

		hogg = hogFileRead(s_hoggfilename, &bCreated, PIGERR_PRINTF, &err_return, HOG_MUST_BE_WRITABLE);
		if(!hogg)
		{
			printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Couldn't open \"%s\" for reading/writing: %s!\n", s_hoggfilename, hogFileGetArchiveFileName(hogg));
			return 1;
		}

		if(!bCreated && bHoggFileNameProvided)
		{
			char prompt[1024] = "";

			sprintf(prompt, "Hogg file for UGCExport already exists: %s. Do you want to overwrite it? Type 'yes' to overwrite (anything else causes download and patching to resume)", hogFileGetArchiveFileName(hogg));

			if(!UGCExportImport_InputTest(prompt, "yes"))
				bResume = true;
		}
		else
			printf("\nHogg file for UGCExport created: %s.\n\n", hogFileGetArchiveFileName(hogg));

		if(bResume)
		{
			HogFileIndex hfIndex = hogFileFind(hogg, "machine");
			if((U32)-1 != hfIndex)
			{
				hogFileExtractBytes(hogg, hfIndex, s_machine, 0, sizeof(s_machine));
				s_machine[hogFileGetFileSize(hogg, hfIndex)] = '\0';
			}
			if(nullStr(s_username))
			{
				hfIndex = hogFileFind(hogg, "username");
				if((U32)-1 != hfIndex)
				{
					hogFileExtractBytes(hogg, hfIndex, s_username, 0, sizeof(s_username));
					s_username[hogFileGetFileSize(hogg, hfIndex)] = '\0';
				}
			}
			hfIndex = hogFileFind(hogg, "patchserver");
			if((U32)-1 != hfIndex)
			{
				hogFileExtractBytes(hogg, hfIndex, s_patchserver, 0, sizeof(s_patchserver));
				s_patchserver[hogFileGetFileSize(hogg, hfIndex)] = '\0';
			}
			hfIndex = hogFileFind(hogg, "patchport");
			if((U32)-1 != hfIndex)
			{
				char buffer[32];
				hogFileExtractBytes(hogg, hfIndex, buffer, 0, sizeof(buffer));
				buffer[hogFileGetFileSize(hogg, hfIndex)] = '\0';
				s_patchport = atoi(buffer);
			}
			hfIndex = hogFileFind(hogg, "patchproject");
			if((U32)-1 != hfIndex)
			{
				hogFileExtractBytes(hogg, hfIndex, s_patchproject, 0, sizeof(s_patchproject));
				s_patchproject[hogFileGetFileSize(hogg, hfIndex)] = '\0';
			}
			hfIndex = hogFileFind(hogg, "patchshard");
			if((U32)-1 != hfIndex)
			{
				hogFileExtractBytes(hogg, hfIndex, s_patchshard, 0, sizeof(s_patchshard));
				s_patchshard[hogFileGetFileSize(hogg, hfIndex)] = '\0';
			}
			hfIndex = hogFileFind(hogg, "ugcprojectid");
			if((U32)-1 != hfIndex)
			{
				char buffer[32];
				hogFileExtractBytes(hogg, hfIndex, buffer, 0, sizeof(buffer));
				buffer[hogFileGetFileSize(hogg, hfIndex)] = '\0';
				s_ugcprojectid = atol(buffer);
			}
			hfIndex = hogFileFind(hogg, "ugcprojectid");
			if((U32)-1 != hfIndex)
			{
				char buffer[32];
				hogFileExtractBytes(hogg, hfIndex, buffer, 0, sizeof(buffer));
				buffer[hogFileGetFileSize(hogg, hfIndex)] = '\0';
				s_ugcprojectid = atol(buffer);
			}
			hfIndex = hogFileFind(hogg, "include_published");
			if((U32)-1 != hfIndex)
			{
				char buffer[32];
				hogFileExtractBytes(hogg, hfIndex, buffer, 0, sizeof(buffer));
				buffer[hogFileGetFileSize(hogg, hfIndex)] = '\0';
				s_include_published = atoi(buffer);
			}
			hfIndex = hogFileFind(hogg, "include_saved");
			if((U32)-1 != hfIndex)
			{
				char buffer[32];
				hogFileExtractBytes(hogg, hfIndex, buffer, 0, sizeof(buffer));
				buffer[hogFileGetFileSize(hogg, hfIndex)] = '\0';
				s_include_saved = atoi(buffer);
			}
			hfIndex = hogFileFind(hogg, "search");
			if((U32)-1 != hfIndex)
			{
				hogFileExtractBytes(hogg, hfIndex, s_searchfilename, 0, sizeof(s_searchfilename));
				s_searchfilename[hogFileGetFileSize(hogg, hfIndex)] = '\0';
			}
			hfIndex = hogFileFind(hogg, "do_patching");
			if((U32)-1 != hfIndex)
			{
				char buffer[32];
				hogFileExtractBytes(hogg, hfIndex, buffer, 0, sizeof(buffer));
				buffer[hogFileGetFileSize(hogg, hfIndex)] = '\0';
				s_do_patching = atoi(buffer);
			}
		}
		else
			hogDeleteAllFiles(hogg);

		if(strlen(s_machine))
			hogFileModifyUpdateNamedSync(hogg, "machine", strdup(s_machine), (U32)strlen(s_machine), time(NULL), NULL);
		if(strlen(s_username))
			hogFileModifyUpdateNamedSync(hogg, "username", strdup(s_username), (U32)strlen(s_username), time(NULL), NULL);
		if(strlen(s_patchserver))
			hogFileModifyUpdateNamedSync(hogg, "patchserver", strdup(s_patchserver), (U32)strlen(s_patchserver), time(NULL), NULL);
		if(s_patchport > 0)
		{
			char buffer[32];
			sprintf(buffer, "%d", s_patchport);
			hogFileModifyUpdateNamedSync(hogg, "patchport", strdup(buffer), (U32)strlen(buffer), time(NULL), NULL);
		}
		if(strlen(s_patchproject))
			hogFileModifyUpdateNamedSync(hogg, "patchproject", strdup(s_patchproject), (U32)strlen(s_patchproject), time(NULL), NULL);
		if(strlen(s_patchshard))
			hogFileModifyUpdateNamedSync(hogg, "patchshard", strdup(s_patchshard), (U32)strlen(s_patchshard), time(NULL), NULL);
		if(s_ugcprojectid > 0)
		{
			char buffer[32];
			sprintf(buffer, "%d", s_ugcprojectid);
			hogFileModifyUpdateNamedSync(hogg, "ugcprojectid", strdup(buffer), (U32)strlen(buffer), time(NULL), NULL);
		}
		if(s_include_published > 0)
		{
			char buffer[32];
			sprintf(buffer, "%d", s_include_published);
			hogFileModifyUpdateNamedSync(hogg, "include_published", strdup(buffer), (U32)strlen(buffer), time(NULL), NULL);
		}
		if(s_include_saved > 0)
		{
			char buffer[32];
			sprintf(buffer, "%d", s_include_saved);
			hogFileModifyUpdateNamedSync(hogg, "include_saved", strdup(buffer), (U32)strlen(buffer), time(NULL), NULL);
		}
		if(strlen(s_searchfilename))
			hogFileModifyUpdateNamedSync(hogg, "search", strdup(s_searchfilename), (U32)strlen(s_searchfilename), time(NULL), NULL);
		if(s_do_patching > 0)
		{
			char buffer[32];
			sprintf(buffer, "%d", s_do_patching);
			hogFileModifyUpdateNamedSync(hogg, "do_patching", strdup(buffer), (U32)strlen(buffer), time(NULL), NULL);
		}
		hogFileModifyFlush(hogg);

		if(!UGCExportImport_ProxyInit(s_machine, s_username))
			return 1;

		if(s_version && !UGCExportImport_Version())
			return 1;

		{
			UGCSearchResult *pUGCSearchResult = NULL;
			if(bResume)
			{
				pUGCSearchResult = StructCreate(parse_UGCSearchResult);
				if(PARSERESULT_ERROR == ParserReadTextFileFromHogg("UGCSearchResult", parse_UGCSearchResult, pUGCSearchResult, hogg))
				{
					char prompt[1024] = "";

					StructDestroySafe(parse_UGCSearchResult, &pUGCSearchResult);

					sprintf(prompt, "Could not find UGCSearchResult in hogg %s. Disable resume? Type 'yes' to disable resume and completely overrite hogg file (anything else exits UGCExport): ", hogFileGetArchiveFileName(hogg));

					if(!UGCExportImport_InputTest(prompt, "yes"))
						return 0;

					bResume = false;
					hogDeleteAllFiles(hogg);
					hogFileModifyFlush(hogg);
				}
			}

			if(!pUGCSearchResult)
				pUGCSearchResult = UGCExport_SearchStage();

			if(!pUGCSearchResult)
				bSuccess = 0; // must have been an error
			else
			{
				if(0 == eaSize(&pUGCSearchResult->eaResults))
				{
					printfColor(COLOR_GREEN, "No search results returned for search criteria.\n");
					bSuccess = 1;
				}
				else
				{
					UGCPatchInfo *pUGCPatchInfo = NULL;
					char **fileNames = NULL;
					int *recurses = NULL;

					if(!bResume)
					{
						printf("Journaling UGCSearchResult with %d content elements into hogg\n\n", eaSize(&pUGCSearchResult->eaResults));

						ParserWriteTextFileToHogg("UGCSearchResult", parse_UGCSearchResult, pUGCSearchResult, hogg);
						hogFileModifyFlush(hogg);
					}

					printf("Journaling UGCProject and UGCProjectSeries containers into hogg\n");

					{
						int projectsJournaled = 0;
						int projectsSkipped = 0;
						int projectsErrored = 0;
						int projectSeriesJournaled = 0;
						int projectSeriesSkipped = 0;
						int projectSeriesErrored = 0;
						int index;
						int count = eaSize(&pUGCSearchResult->eaResults);
						int *projectCountByAuthor = NULL;
						int *seriesCountByAuthor = NULL;
						int projectAuthorCount = 0;
						int multipleProjectAuthorCount = 0;
						int seriesAuthorCount = 0;
						int multipleSeriesAuthorCount = 0;
						int totalProjectPlays = 0;
						int totalProjectTips = 0;
						int projectsPlayedCount = 0;
						int projectsOutOfReviewCount = 0;

						eaiCreate(&projectCountByAuthor);
						eaiCreate(&seriesCountByAuthor);

						for(index = 0; index < count; index++)
						{
							char filename[256];

							UGCExportImport_ProgressBar(index, count, 100, 50);

							if(pUGCSearchResult->eaResults[index]->iUGCProjectID)
							{
								UGCProject *pUGCProject = NULL;
								bool bSkipHoggCheck = false;

								sprintf(filename, "UGCProject/%d/UGCProject.con", pUGCSearchResult->eaResults[index]->iUGCProjectID);

								if(bResume)
								{
									pUGCProject = StructCreate(parse_UGCProject);
									if(PARSERESULT_ERROR == ParserReadTextFileFromHogg(filename, parse_UGCProject, pUGCProject, hogg))
										StructDestroySafe(parse_UGCProject, &pUGCProject);
								}

								if(!pUGCProject)
								{
									bSkipHoggCheck = true;
									pUGCProject = UGCExportImport_GetUGCProjectContainer(pUGCSearchResult->eaResults[index]->iUGCProjectID);
									if(!pUGCProject)
									{
										projectsErrored++;
										printfColor(COLOR_RED | COLOR_BRIGHT, "\nERROR: Failed to get UGC Project container: ID = %d!\n", pUGCSearchResult->eaResults[index]->iUGCProjectID);
									}
									else
									{
										projectsJournaled++;
										ParserWriteTextFileToHogg(filename, parse_UGCProject, pUGCProject, hogg);
										hogFileModifyFlush(hogg);
									}
								}
								else
									projectsSkipped++;

								if(pUGCProject)
								{
									if(!nullStr(pUGCProject->pIDString))
									{
										UGCProjectVersion *pUGCProjectVersionPublished = NULL;
										UGCProjectVersion *pUGCProjectVersionSaved = NULL;
										int totalPlays = 0;

										if(eaiSize(&projectCountByAuthor) <= (int)pUGCProject->iOwnerAccountID)
											eaiSetSize(&projectCountByAuthor, pUGCProject->iOwnerAccountID + 1);
										if(0 == projectCountByAuthor[pUGCProject->iOwnerAccountID])
											projectAuthorCount++;
										else if(1 == projectCountByAuthor[pUGCProject->iOwnerAccountID])
											multipleProjectAuthorCount++;
										projectCountByAuthor[pUGCProject->iOwnerAccountID]++;

										FOR_EACH_IN_EARRAY_INT(pUGCProject->ugcStats.completionStats.eaiCompletedCountByDay, int, c)
										{
											totalPlays += c;
										}
										FOR_EACH_END;
										totalPlays += pUGCProject->ugcStats.completionStats.uRemainingCompletedCount;

										if(0 < totalPlays)
											projectsPlayedCount++;
										if(4 < totalPlays)
											projectsOutOfReviewCount++;

										totalProjectPlays += totalPlays;
										totalProjectTips += pUGCProject->ugcLifetimeTips;

										FOR_EACH_IN_EARRAY(pUGCProject->ppProjectVersions, UGCProjectVersion, pUGCProjectVersion)
										{
											UGCProjectVersionState eState = ugcProjectGetVersionStateConst(pUGCProjectVersion);
											if(!pUGCProjectVersionPublished && (UGC_PUBLISHED == eState || UGC_REPUBLISHING == eState || UGC_NEEDS_REPUBLISHING == eState))
												pUGCProjectVersionPublished = pUGCProjectVersion;

											if(!pUGCProjectVersionSaved && eState == UGC_SAVED)
												pUGCProjectVersionSaved = pUGCProjectVersion;

											if(pUGCProjectVersionPublished && pUGCProjectVersionSaved)
												break;
										}
										FOR_EACH_END;

										if(pUGCProjectVersionPublished)
										{
											if(bSkipHoggCheck || !hoggProjectFileExists(hogg, pUGCProject, pUGCProjectVersionPublished))
											{
												eaPush(&fileNames, estrCreateProjectFileName(pUGCProject, pUGCProjectVersionPublished));
												eaiPush(&recurses, 0);
											}
										}

										if(pUGCProjectVersionSaved)
										{
											if(bSkipHoggCheck || !hoggProjectFileExists(hogg, pUGCProject, pUGCProjectVersionSaved))
											{
												eaPush(&fileNames, estrCreateProjectFileName(pUGCProject, pUGCProjectVersionSaved));
												eaiPush(&recurses, 0);
											}
										}
									}
									else
									{
										projectsErrored++;
										printfColor(COLOR_RED | COLOR_BRIGHT, "\nERROR: UGC Project container is missing its ID string: ID = %d!\n", pUGCProject->id);
									}
								}

								StructDestroySafe(parse_UGCProject, &pUGCProject);
							}
							else if(pUGCSearchResult->eaResults[index]->iUGCProjectSeriesID)
							{
								UGCProjectSeries *pUGCProjectSeries = NULL;

								sprintf(filename, "UGCProjectSeries/%d/UGCProjectSeries.con", pUGCSearchResult->eaResults[index]->iUGCProjectSeriesID);

								if(bResume)
								{
									pUGCProjectSeries = StructCreate(parse_UGCProjectSeries);
									if(PARSERESULT_ERROR == ParserReadTextFileFromHogg(filename, parse_UGCProjectSeries, pUGCProjectSeries, hogg))
										StructDestroySafe(parse_UGCProjectSeries, &pUGCProjectSeries);
								}

								if(!pUGCProjectSeries)
								{
									pUGCProjectSeries = UGCExportImport_GetUGCProjectSeriesContainer(pUGCSearchResult->eaResults[index]->iUGCProjectSeriesID);
									if(!pUGCProjectSeries)
									{
										projectSeriesErrored++;
										printfColor(COLOR_RED | COLOR_BRIGHT, "\nERROR: Failed to get UGC Project Series container: ID = %d!\n", pUGCSearchResult->eaResults[index]->iUGCProjectSeriesID);
									}
									else
									{
										projectSeriesJournaled++;
										ParserWriteTextFileToHogg(filename, parse_UGCProjectSeries, pUGCProjectSeries, hogg);
										hogFileModifyFlush(hogg);
									}
								}
								else
									projectSeriesSkipped++;

								if(pUGCProjectSeries)
								{
									if(eaiSize(&seriesCountByAuthor) <= (int)pUGCProjectSeries->iOwnerAccountID)
										eaiSetSize(&seriesCountByAuthor, pUGCProjectSeries->iOwnerAccountID + 1);
									if(0 == seriesCountByAuthor[pUGCProjectSeries->iOwnerAccountID])
										seriesAuthorCount++;
									else if(1 == seriesCountByAuthor[pUGCProjectSeries->iOwnerAccountID])
										multipleSeriesAuthorCount++;
									seriesCountByAuthor[pUGCProjectSeries->iOwnerAccountID]++;

									StructDestroy(parse_UGCProjectSeries, pUGCProjectSeries);
								}
							}
						}

						UGCExportImport_ProgressBar(count, count, 100, 50);

						printf("\nUGCProject journaling results: %d journaled, %d skipped, %d errored, %d unique authors, %d multiple project authors, %d total plays, %d total tips, %d projects played, %d projects out of review\n", projectsJournaled, projectsSkipped, projectsErrored, projectAuthorCount, multipleProjectAuthorCount, totalProjectPlays, totalProjectTips, projectsPlayedCount, projectsOutOfReviewCount);
						printf("UGCProjectSeries journaling results: %d journaled, %d skipped, %d errored, %d unique authors, %d multiple series authors\n\n", projectSeriesJournaled, projectSeriesSkipped, projectSeriesErrored, seriesAuthorCount, multipleSeriesAuthorCount);

						StructDestroySafe(parse_UGCSearchResult, &pUGCSearchResult); // no longer needed, so destroy to free up memory

						eaiDestroy(&projectCountByAuthor);
						eaiDestroy(&seriesCountByAuthor);
					}

					if(bResume)
					{
						pUGCPatchInfo = StructCreate(parse_UGCPatchInfo);
						if(PARSERESULT_ERROR == ParserReadTextFileFromHogg("UGCPatchInfo", parse_UGCPatchInfo, pUGCPatchInfo, hogg))
							StructDestroySafe(parse_UGCPatchInfo, &pUGCPatchInfo);
					}

					if(!pUGCPatchInfo)
					{
						pUGCPatchInfo = UGCExportImport_GetUGCPatchInfo();
						if(!pUGCPatchInfo)
						{
							printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Failed to get UGC Patch Info!\n");
						}
						else
						{
							printf("Journaling UGCPatchInfo into hogg, with overrides\n\n");

							if(s_patchserver && s_patchserver[0])
								StructCopyString(&pUGCPatchInfo->server, s_patchserver);
							if(s_patchport > 0)
								pUGCPatchInfo->port = s_patchport;
							if(s_patchproject && s_patchproject[0])
								StructCopyString(&pUGCPatchInfo->project, s_patchproject);
							if(s_patchshard && s_patchshard[0])
								StructCopyString(&pUGCPatchInfo->shard, s_patchshard);

							ParserWriteTextFileToHogg("UGCPatchInfo", parse_UGCPatchInfo, pUGCPatchInfo, hogg);
							hogFileModifyFlush(hogg);
						}
					}

					if(s_do_patching && pUGCPatchInfo && eaSize(&fileNames) > 0)
					{
						int file;
						
						PCL_ErrorCode patcherror;
						PCL_Client *patchclient = NULL;

						PCL_DO_WAIT(pclConnectAndCreate(&patchclient, pUGCPatchInfo->server, pUGCPatchInfo->port, 60, commDefault(), "", "UGCExport", "", NULL, NULL));

						pclAddFileFlags(patchclient, PCL_IN_MEMORY | PCL_METADATA_IN_MEMORY | PCL_NO_DELETEME_CLEANUP | PCL_NO_MIRROR | PCL_NO_DELETE);
						pclSetBadFilesDirectory(patchclient, fileTempDir());

						PCL_DO_WAIT(pclSetViewLatest(patchclient, pUGCPatchInfo->project, 0, NULL, true, false, NULL, NULL));

						pclResetRoot(patchclient, "C:\\UGCExportTemp");

						for(file = 0; file < eaSize(&fileNames); file += 500)
							PCL_DO_WAIT(pclGetFileList(patchclient, &fileNames[file], &recurses[file], false, MIN(500, eaSize(&fileNames) - file), pclGetFileList_CB, hogg, NULL));

						PCL_DO(pclDisconnectAndDestroy(patchclient));
					}

					eaDestroyEString(&fileNames);
					eaiDestroy(&recurses);
				}

				StructDestroySafe(parse_UGCSearchResult, &pUGCSearchResult); // we may have destroyed this already to free up memory, but maybe not
			}
		}

		hogFileDestroy(hogg, true);

		return (bSuccess != true); // 0 means no error
	}

	EXCEPTION_HANDLER_END;

	return 1; // couldn't init python
}
