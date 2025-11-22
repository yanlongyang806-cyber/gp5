/***************************************************************************



***************************************************************************/

#include "cmdServerCharacter.h"
#include "file.h"
#include "fileutil.h"
#include "timing.h"
#include "objContainer.h"
#include "objContainerIO.h"
#include "StringUtil.h"
#include "Regex.h"
#include "GlobalStateMachine.h"
#include "gslBaseStates.h"
#include "GameServerLib.h"
#include "rand.h"
#include "gslTransactions.h"

#include "AutoGen/cmdServerSyntheticLogs_c_ast.h"
#include "AutoGen/ObjectDB_autogen_remotefuncs.h"
#include "autogen/GameServerLib_autogen_RemoteFuncs.h"

// If true, synthetic replicated logs are played back in real-time using original log line timestamps. Can be toggled during replay.
static bool s_bReplaySyntheticLogDataInRealtime = false;
AUTO_CMD_INT(s_bReplaySyntheticLogDataInRealtime, ReplaySyntheticLogDataInRealtime) ACMD_CALLBACK(ReplaySyntheticLogDataInRealtimeCallback);

// If non-zero, throttles synthetic replicated log replay running at 30 FPS. This value is ignored if ReplaySyntheticLogDataInRealtime is true. Can be toggled during replay.
static U32 s_iMaxSyntheticReplayTransactionsPerFrame = 0;
AUTO_CMD_INT(s_iMaxSyntheticReplayTransactionsPerFrame, MaxSyntheticReplayTransactionsPerFrame) ACMD_CALLBACK(MaxSyntheticReplayTransactionsPerFrameCallback);

// If non-empty string, all replaying GameServers will stop at the first transaction they encounter with a timestamp greater than or equal to this value.
// Useful for running the GameServers flat out until a certain time, then playing them in realtime afterwards.
static char s_StopSyntheticReplayAt[32] = "";
AUTO_CMD_STRING(s_StopSyntheticReplayAt, StopSyntheticReplayAt) ACMD_CMDLINE ACMD_CALLBACK(StopSyntheticReplayAtCallback);

static bool s_bSimulateContainerMoves = false;
AUTO_CMD_INT(s_bSimulateContainerMoves, SimulateContainerMoves);

static U32 s_iStopSyntheticReplayAtTimestamp = 0;

void ReplaySyntheticLogDataInRealtimeCallback(CMDARGS)
{
	if(s_bReplaySyntheticLogDataInRealtime)
		s_iStopSyntheticReplayAtTimestamp = 0;
}

void MaxSyntheticReplayTransactionsPerFrameCallback(CMDARGS)
{
	if(s_iMaxSyntheticReplayTransactionsPerFrame)
		s_iStopSyntheticReplayAtTimestamp = 0;
}

void StopSyntheticReplayAtCallback(CMDARGS)
{
	if(!nullStr(s_StopSyntheticReplayAt))
	{
		int year,month,day,hour,min,sec;
		if(sscanf_s(s_StopSyntheticReplayAt, "%04u-%02u-%02u %02u:%02u:%02u", &year, &month, &day, &hour, &min, &sec) != 6)
		{
			s_StopSyntheticReplayAt[0] = '\0';
			s_iStopSyntheticReplayAtTimestamp = 0;
		}
		else
		{
			char *temp = 0;
			estrStackCreate(&temp);
			estrPrintf(&temp, "%04u-%02u-%02u %02u:%02u:%02u", year, month, day, hour, min, sec);
			s_iStopSyntheticReplayAtTimestamp = timeGetSecondsSince2000FromDateString(temp);
			estrDestroy(&temp);
		}
	}
	else
		s_iStopSyntheticReplayAtTimestamp = 0;
}

static char s_SyntheticLogDataFile[MAX_PATH] = "";
AUTO_CMD_STRING(s_SyntheticLogDataFile, SyntheticLogDataFile) ACMD_CMDLINE ACMD_CALLBACK(SyntheticLogDataFileCallback);

AUTO_ENUM;
typedef enum SyntheticLogDataFixupType
{
	SyntheticLogDataFixupType_Replace,
	SyntheticLogDataFixupType_Append,
	SyntheticLogDataFixupType_Fixup,
	SyntheticLogDataFixupType_FixupEnum,
	SyntheticLogDataFixupType_AppendOffset
} SyntheticLogDataFixupType;

AUTO_STRUCT;
typedef struct ReplaySyntheticLogData {
	// Starting GameServer ID that will be performing replay
	U32 iReplayGameServerMinID;				AST(NAME(ReplayGameServerMinID))
	// Ending GameServer ID that will be performing replay
	U32 iReplayGameServerMaxID;				AST(NAME(ReplayGameServerMaxID))

	// If zero, then no subscription simulation is performed
	U32 iFakeSubscriptionsPerSecond;		AST(NAME(FakeSubscriptionsPerSecond))
	// Starting GameServer ID that will be subscription simulation
	U32 iFakeSubscriptionsGameServerMinID;	AST(NAME(FakeSubscriptionsGameServerMinID))
	// Ending GameServer ID that will be subscription simulation
	U32 iFakeSubscriptionsGameServerMaxID;	AST(NAME(FakeSubscriptionsGameServerMaxID))
} ReplaySyntheticLogData;

AUTO_STRUCT;
typedef struct SyntheticLogDataFixup {
	bool bBreakIfDebuggerPresent;			AST(NAME(BreakIfDebuggerPresent))
	bool bBreakIfDebuggerPresentAndMatched;	AST(NAME(BreakIfDebuggerPresentAndMatched))
	bool bPrintIfMatched;					AST(NAME(PrintIfMatched))

	char *pcRegex;							AST(NAME(Regex))
	SyntheticLogDataFixupType type;			AST(NAME(Type))
	char *pcEnumName;						AST(NAME(EnumName))
} SyntheticLogDataFixup;

AUTO_STRUCT;
typedef struct SyntheticLogDataReplicate {
	GlobalType eGlobalType;								AST(NAME(GlobalType))
	bool bReplicate;									AST(NAME(Replicate))
	bool bBreakIfDebuggerPresent;						AST(NAME(BreakIfDebuggerPresent))

	SyntheticLogDataFixup **eaSyntheticLogDataFixup;	AST(NAME(Fixup))

	U64 iTransactionsProcessed;							AST(NAME(TransactionsProcessed))
	U64 iTransactionsCreated;							AST(NAME(TransactionsCreated))
} SyntheticLogDataReplicate;

AUTO_STRUCT;
typedef struct SyntheticLogData {
	// To perform a replay, include the following struct
	ReplaySyntheticLogData *pReplaySyntheticLogData;			AST(NAME(Replay))

	// Input folder containing the source objectdb logs to replicate and/or replay
	char *pcInputFolder;										AST(NAME(InputFolder))

	// Output folder where replicated objectdb logs are written, after fixups are applied
	char *pcOutputFolder;										AST(NAME(OutputFolder))

	// Set to true when generating large replicated log sets so they are easier to store and transfer between machines. Use false when iterating and testing if easy
	// human reading of the generated logs is desired.
	bool bWriteCompressedLogs;									AST(NAME(WriteCompressedLogs))

	// How many separate sets of replicated objectdb log files to create. This allows replay to occur simultaneously from multiple GameServers
	U32 iLogFileMultiplier;										AST(NAME(LogFileMultiplier))

	// How many times should each log line be replicated in each set of log files. The total replication factor will end up being LogFileMultiplier * LogLineMultiplier.
	// For example, to achieve a 42 times replication factor using 6 GameServers, set LogFileMultiplier to 6 and LogLineMultiplier to 7.
	U32 iLogLineMultiplier;										AST(NAME(LogLineMultiplier))

	// Offset to use when fixing up container IDs and account IDs. In practice, the best value for this should be 10,000,000. Account ID max is somewhere around 6,000,000, so there
	// should be no conflicts using 10,000,000. This allows total replication factors to be well over 100 before we end up wrapping container and account IDs.
	U32 iOffset;												AST(NAME(Offset))

	// Determines what containers to replicate and what fixups to apply during replication of each container type.
	SyntheticLogDataReplicate **eaSyntheticLogDataReplicate;	AST(NAME(Replicate))

	// When doing replication using 1 GameServer, TransactionsProcessed and TransactionsCreated are written back out to the SyntheticLogData input struct.
	U64 iTransactionsProcessed;									AST(NAME(TransactionsProcessed))
	U64 iTransactionsCreated;									AST(NAME(TransactionsCreated))

	// Used for tracking replication process
	U32 iReplayWhileReplicatingFolderID;						NO_AST
	FileWrapper **pCurrentOutputFiles;							NO_AST
	U64 iCurrentSequenceNumber;									NO_AST
} SyntheticLogData;

static SyntheticLogData *s_pSyntheticLogData = NULL;

void SyntheticLogDataFileCallback(CMDARGS)
{
	if(s_pSyntheticLogData)
		StructReset(parse_SyntheticLogData, s_pSyntheticLogData);
	else
		s_pSyntheticLogData = StructCreate(parse_SyntheticLogData);

	if(PARSERESULT_ERROR == ParserReadTextFile(s_SyntheticLogDataFile, parse_SyntheticLogData, s_pSyntheticLogData, 0))
	{
		Alertf("SyntheticLogData: Failed to parse input file %s.\n", s_SyntheticLogDataFile);

		StructDestroySafe(parse_SyntheticLogData, &s_pSyntheticLogData);
	}

	SetSkipPlayerCallbacksWhenReplaying(true);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(ReloadSyntheticLogData);
void cmdReloadSyntheticLogData()
{
	SyntheticLogDataFileCallback(NULL, NULL);
}

static void SyntheticLogDataCB(const char *command, U64 sequence, U32 timestamp)
{
	char *pCmd = NULL;
	U32 uType;
	U32 uID;
	const char *pStr = NULL;
	U32 file, line;
	char timestampStr[64];
	SyntheticLogDataReplicate *pSyntheticLogDataReplicate = NULL;
	char *pNewStr = NULL;
	char *pTempStr = NULL;
	char *pCmdStr = NULL;
	U32 fileFrom, fileTo;

	if(nullStr(command) || (!strStartsWith(command, "dbUpdateContainer ") && !strStartsWith(command, "dbUpdateContainerOwner ")))
		return;

	estrInsert(&pCmd, 0, command, strchr_fast(command, ' ') - command);

	pStr = strchr_fast(command, ' ');
	pStr++;
	uType = atoi(pStr);

	pStr = strchr_fast(pStr, ' ');
	pStr++;
	uID = atoi(pStr);

	pStr = strchr_fast(pStr, ' ');
	pStr++;

	FOR_EACH_IN_EARRAY_FORWARDS(s_pSyntheticLogData->eaSyntheticLogDataReplicate, SyntheticLogDataReplicate, pSyntheticLogDataReplicateIter)
	{
		if(uType == (U32)pSyntheticLogDataReplicateIter->eGlobalType)
		{
			pSyntheticLogDataReplicate = pSyntheticLogDataReplicateIter;
			break;
		}
	}
	FOR_EACH_END;

	if(!pSyntheticLogDataReplicate)
	{
		pSyntheticLogDataReplicate = StructCreate(parse_SyntheticLogDataReplicate);
		pSyntheticLogDataReplicate->eGlobalType = uType;
		eaPush(&s_pSyntheticLogData->eaSyntheticLogDataReplicate, pSyntheticLogDataReplicate);
	}

	s_pSyntheticLogData->iTransactionsProcessed++;
	pSyntheticLogDataReplicate->iTransactionsProcessed++;

	timeMakeDateStringFromSecondsSince2000(timestampStr, timestamp);

	fileFrom = s_pSyntheticLogData->pReplaySyntheticLogData ? s_pSyntheticLogData->iReplayWhileReplicatingFolderID : 0;
	fileTo = s_pSyntheticLogData->pReplaySyntheticLogData ? (s_pSyntheticLogData->iReplayWhileReplicatingFolderID+1) : s_pSyntheticLogData->iLogFileMultiplier;
	for(file = fileFrom; file < fileTo; file++)
	{
		if(!pSyntheticLogDataReplicate->bReplicate && file > 0)
			break;

		if(pSyntheticLogDataReplicate->bBreakIfDebuggerPresent && IsDebuggerPresent())
			_DbgBreak();

		for(line = 0; line < s_pSyntheticLogData->iLogLineMultiplier; line++)
		{
			U32 uNewID, uOffsetID;

			if(!pSyntheticLogDataReplicate->bReplicate && line > 0)
				break;

			uOffsetID = s_pSyntheticLogData->iOffset * (file * s_pSyntheticLogData->iLogLineMultiplier + line);
			uNewID = uID + uOffsetID;

			if(pSyntheticLogDataReplicate->eaSyntheticLogDataFixup)
			{
				bool bPrint = false;

				if(!pNewStr)
					estrStackCreateSize(&pNewStr, 512);

				estrCopy2(&pNewStr, pStr);
				FOR_EACH_IN_EARRAY_FORWARDS(pSyntheticLogDataReplicate->eaSyntheticLogDataFixup, SyntheticLogDataFixup, pSyntheticLogDataFixup)
				{
					char *pStrIter = pNewStr;
					int iNumMatches;
					int pMatches[100];

					if(pSyntheticLogDataFixup->bBreakIfDebuggerPresent && IsDebuggerPresent())
						_DbgBreak();

					do
					{
						iNumMatches = regexMatch(pSyntheticLogDataFixup->pcRegex, pStrIter, pMatches);
						if(iNumMatches >= 2)
						{
							if(pSyntheticLogDataFixup->bBreakIfDebuggerPresentAndMatched && IsDebuggerPresent())
								_DbgBreak();
							bPrint = bPrint || pSyntheticLogDataFixup->bPrintIfMatched;

							if(!pTempStr)
								estrStackCreateSize(&pTempStr, 512);

							if(SyntheticLogDataFixupType_Replace == pSyntheticLogDataFixup->type)
							{
								estrConcatf(&pTempStr, "%.*s%d%.*s", pMatches[2], pStrIter, uNewID, pMatches[1] - pMatches[3], pStrIter + pMatches[3]);
							}
							else if(SyntheticLogDataFixupType_Append == pSyntheticLogDataFixup->type)
							{
								estrConcatf(&pTempStr, "%.*s%d%.*s", pMatches[3], pStrIter, uNewID, pMatches[1] - pMatches[3], pStrIter + pMatches[3]);
							}
							else if(SyntheticLogDataFixupType_AppendOffset == pSyntheticLogDataFixup->type)
							{
								estrConcatf(&pTempStr, "%.*s%d%.*s", pMatches[3], pStrIter, uOffsetID, pMatches[1] - pMatches[3], pStrIter + pMatches[3]);
							}
							else if(SyntheticLogDataFixupType_Fixup == pSyntheticLogDataFixup->type)
							{
								char buf[64];
								U32 uMatchID;
								U32 uActualID;

								strncpy(buf, pStrIter + pMatches[2], pMatches[3] - pMatches[2]);
								uMatchID = atol(buf);
								uActualID = uMatchID + uOffsetID;

								estrConcatf(&pTempStr, "%.*s%d%.*s", pMatches[2], pStrIter, uActualID, pMatches[1] - pMatches[3], pStrIter + pMatches[3]);
							}
							else if(SyntheticLogDataFixupType_FixupEnum == pSyntheticLogDataFixup->type && !nullStr(pSyntheticLogDataFixup->pcEnumName))
							{
								char buf[128];
								U32 uMatchInt;

								StaticDefineInt *pStaticDefineInt = FindNamedStaticDefine(pSyntheticLogDataFixup->pcEnumName);
								if(pStaticDefineInt)
								{
									strncpy(buf, pStrIter + pMatches[2], pMatches[3] - pMatches[2]);
									if(sscanf(buf, "%d", &uMatchInt))
									{
									
										const char *pcEnumStr = StaticDefineIntRevLookupNonNull(pStaticDefineInt, uMatchInt);
										estrConcatf(&pTempStr, "%.*s\"%s\"%.*s", pMatches[2], pStrIter, pcEnumStr, pMatches[1] - pMatches[3], pStrIter + pMatches[3]);
									}
									else
										estrConcatf(&pTempStr, "%.*s", pMatches[1], pStrIter);
								}
								else
									estrConcatf(&pTempStr, "%.*s", pMatches[1], pStrIter);
							}
							else
								estrConcatf(&pTempStr, "%.*s", pMatches[1], pStrIter);

							pStrIter += pMatches[1];
						}
					} while(iNumMatches >= 2);

					estrAppend2(&pTempStr, pStrIter);
					estrCopy2(&pNewStr, pTempStr);
					estrClear(&pTempStr);
				}
				FOR_EACH_END;

				if(bPrint)
					fprintf(fileGetStderr(), "%s\n", pNewStr);

				fprintf(s_pSyntheticLogData->pCurrentOutputFiles[file], "%llu %s: %s %u %u %s",
					s_pSyntheticLogData->iCurrentSequenceNumber, timestampStr, pCmd, uType, uNewID, pNewStr);
				s_pSyntheticLogData->iCurrentSequenceNumber++;
				s_pSyntheticLogData->iTransactionsCreated++;
				pSyntheticLogDataReplicate->iTransactionsCreated++;

				if(s_pSyntheticLogData->pReplaySyntheticLogData)
				{
					if(!pCmdStr)
						estrStackCreateSize(&pCmdStr, 512);
					estrPrintf(&pCmdStr, "%s %u %u %s", pCmd, uType, uNewID, pNewStr);
					ReplaySyntheticLogDataCB(pCmdStr, sequence, timestamp);
				}
			}
			else
			{
				fprintf(s_pSyntheticLogData->pCurrentOutputFiles[file], "%llu %s: %s %u %u %s",
					s_pSyntheticLogData->iCurrentSequenceNumber, timestampStr, pCmd, uType, uNewID, pStr);
				s_pSyntheticLogData->iCurrentSequenceNumber++;
				s_pSyntheticLogData->iTransactionsCreated++;
				pSyntheticLogDataReplicate->iTransactionsCreated++;

				if(s_pSyntheticLogData->pReplaySyntheticLogData)
				{
					if(!pCmdStr)
						estrStackCreateSize(&pCmdStr, 512);
					estrPrintf(&pCmdStr, "%s %u %u %s", pCmd, uType, uNewID, pStr);
					ReplaySyntheticLogDataCB(pCmdStr, sequence, timestamp);
				}
			}
		}
	}

	estrDestroy(&pNewStr);
	estrDestroy(&pTempStr);
	estrDestroy(&pCmdStr);
	estrDestroy(&pCmd);
}

static bool CreateSyntheticLogFileCB(const char *filename)
{
	U32 i;
	char output_filename[CRYPTIC_MAX_PATH];

	char *base_filename = strrchr(filename, '\\');
	base_filename++;

	fprintf(fileGetStderr(), "Creating %d synthetic incremental logs for: %s\n", s_pSyntheticLogData->iLogFileMultiplier, base_filename);

	if(s_pSyntheticLogData->pReplaySyntheticLogData)
	{
		sprintf(output_filename, "%s\\%u\\%s%s", s_pSyntheticLogData->pcOutputFolder, s_pSyntheticLogData->iReplayWhileReplicatingFolderID, base_filename,
			s_pSyntheticLogData->bWriteCompressedLogs ? ".gz" : "");

		if(s_pSyntheticLogData->pCurrentOutputFiles[s_pSyntheticLogData->iReplayWhileReplicatingFolderID])
		{
			fclose(s_pSyntheticLogData->pCurrentOutputFiles[s_pSyntheticLogData->iReplayWhileReplicatingFolderID]);
			s_pSyntheticLogData->pCurrentOutputFiles[s_pSyntheticLogData->iReplayWhileReplicatingFolderID] = NULL;
		}

		s_pSyntheticLogData->pCurrentOutputFiles[s_pSyntheticLogData->iReplayWhileReplicatingFolderID] = fopen(output_filename, s_pSyntheticLogData->bWriteCompressedLogs ? "wzb" : "w");
		if(!s_pSyntheticLogData->pCurrentOutputFiles[s_pSyntheticLogData->iReplayWhileReplicatingFolderID])
		{
			fprintf(fileGetStderr(), "ERROR: Could not create synthetic incremental log file for writing: %s\n", output_filename);
			return false;
		}
	}
	else
	{
		for(i = 0; i < s_pSyntheticLogData->iLogFileMultiplier; i++)
		{
			sprintf(output_filename, "%s\\%u\\%s%s", s_pSyntheticLogData->pcOutputFolder, i, base_filename,
				s_pSyntheticLogData->bWriteCompressedLogs ? ".gz" : "");

			if(s_pSyntheticLogData->pCurrentOutputFiles[i])
			{
				fclose(s_pSyntheticLogData->pCurrentOutputFiles[i]);
				s_pSyntheticLogData->pCurrentOutputFiles[i] = NULL;
			}

			s_pSyntheticLogData->pCurrentOutputFiles[i] = fopen(output_filename, s_pSyntheticLogData->bWriteCompressedLogs ? "wzb" : "w");
			if(!s_pSyntheticLogData->pCurrentOutputFiles[i])
			{
				fprintf(fileGetStderr(), "ERROR: Could not create synthetic incremental log file for writing: %s\n", output_filename);
				return false;
			}
		}
	}

	return true;
}

static U32 s_iStartTimeSecs;
static char **s_eaFiles = NULL;
static bool s_bPrevStrictMerge;
static FileWrapper *s_FileWrapper = NULL;

static void HandleFakeSubscriptionQueueData_Tick();

void ReplaySyntheticLogs_Tick(void)
{
	HandleFakeSubscriptionQueueData_Tick();

	if(s_FileWrapper)
	{
		objReplayLogThrottled(NULL, &s_FileWrapper, s_iMaxSyntheticReplayTransactionsPerFrame, s_bReplaySyntheticLogDataInRealtime, s_iStopSyntheticReplayAtTimestamp);
	}
	else if(eaSize(&s_eaFiles))
	{
		char *file = eaRemove(&s_eaFiles, 0);
		objReplayLogThrottled(file, &s_FileWrapper, s_iMaxSyntheticReplayTransactionsPerFrame, s_bReplaySyntheticLogDataInRealtime, s_iStopSyntheticReplayAtTimestamp);
	}

	if(s_iStartTimeSecs && 0 == eaSize(&s_eaFiles))
	{
		U32 i;

		fprintf(fileGetStderr(), "Finished synthetic log processing (%u seconds).\n", timerCpuSeconds() - s_iStartTimeSecs);

		eaDestroyEx(&s_eaFiles, NULL);

		objSetCommandLogFileReplayCallback(NULL);
		objSetCommandReplayCallback(NULL);
		gContainerSource.strictMerge = s_bPrevStrictMerge;

		if(!s_pSyntheticLogData->pReplaySyntheticLogData)
			ParserWriteTextFile(s_SyntheticLogDataFile, parse_SyntheticLogData, s_pSyntheticLogData, 0, 0);

		for(i = 0; i < s_pSyntheticLogData->iLogFileMultiplier; i++)
		{
			if(s_pSyntheticLogData->pCurrentOutputFiles[i])
			{
				fclose(s_pSyntheticLogData->pCurrentOutputFiles[i]);
				s_pSyntheticLogData->pCurrentOutputFiles[i] = NULL;
			}
		}

		SAFE_FREE(s_pSyntheticLogData->pCurrentOutputFiles);

		s_iStartTimeSecs = 0;
	}
}

static FileScanAction FindOptionallyCompressedReplayLogsCallback(char* dir, struct _finddata32_t* data, void *pUserData)
{
	char ***eaFiles = (char***)pUserData;
	char fullPath[CRYPTIC_MAX_PATH] = "";

	if(strEndsWith(data->name, ".log") || strEndsWith(data->name, ".log.gz"))
	{
		sprintf(fullPath, "%s\\%s", dir, data->name);
		eaPush(eaFiles, strdup(fullPath));
	}

	return FSA_NO_EXPLORE_DIRECTORY;
}

static char **FindOptionallyCompressedReplayLogs(const char *pDir)
{
	char **eaFiles = NULL;
	fileScanAllDataDirs(pDir, FindOptionallyCompressedReplayLogsCallback, &eaFiles);
	return eaFiles;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(CreateSyntheticLogDataWithFolderID);
void cmdCreateSyntheticLogDataWithFolderID(U32 iFolderID)
{
	U32 i;

	if(!s_pSyntheticLogData)
	{
		fprintf(fileGetStderr(), "ERROR: Could not run CreateSyntheticLogData because no SyntheticLogDataFile was supplied on command line or the file failed to parse.\n");
		return;
	}

	if(!GSM_IsStateActive(GSL_RUNNING) && !GSM_IsStateActive(GSL_REPLAYSYNTHETICLOGS))
	{
		fprintf(fileGetStderr(), "ERROR: Could not run CreateSyntheticLogData because the GameServer is not in the ready state or already in the ReplaySyntheticLogs state.\n");
		return;
	}

	if(nullStr(s_pSyntheticLogData->pcInputFolder))
	{
		fprintf(fileGetStderr(), "ERROR: No InputFolder in SyntheticLogData! This command line option must refer to a folder containing inc_pending.log files to expand.\n");
		StructDestroy(parse_SyntheticLogData, s_pSyntheticLogData);
		return;
	}

	if(nullStr(s_pSyntheticLogData->pcOutputFolder))
	{
		fprintf(fileGetStderr(), "ERROR: No OutputFolder specified in SyntheticLogData! This command line option must refer to a folder where the expanded log files should be written.\n");
		StructDestroy(parse_SyntheticLogData, s_pSyntheticLogData);
		return;
	}

	if(0 == stricmp(s_pSyntheticLogData->pcInputFolder, s_pSyntheticLogData->pcOutputFolder))
	{
		fprintf(fileGetStderr(), "ERROR: InputFolder and OutputFolder must refer to separate paths!\n");
		StructDestroy(parse_SyntheticLogData, s_pSyntheticLogData);
		return;
	}

	if(0 == s_pSyntheticLogData->iLogFileMultiplier || 0 == s_pSyntheticLogData->iLogLineMultiplier)
	{
		fprintf(fileGetStderr(), "ERROR: SyntheticLogData multiplier values must both be greater than 0.\n");
		StructDestroy(parse_SyntheticLogData, s_pSyntheticLogData);
		return;
	}

	if(!s_pSyntheticLogData->pReplaySyntheticLogData && (iFolderID < 0 || iFolderID >= s_pSyntheticLogData->iLogFileMultiplier))
	{
		fprintf(fileGetStderr(), "ERROR: SyntheticLogData iFolderID %u is not in range [0,%u). Skipping this GameServer since logs are being replayed while replicating.\n", iFolderID, s_pSyntheticLogData->iLogFileMultiplier);
		StructDestroy(parse_SyntheticLogData, s_pSyntheticLogData);
		return;
	}

	fprintf(fileGetStderr(), "Using GameServer ID %u to create synthetic log data.\n", GetAppGlobalID());

	s_pSyntheticLogData->iCurrentSequenceNumber = 1;
	s_pSyntheticLogData->iTransactionsProcessed = 0;
	s_pSyntheticLogData->iTransactionsCreated = 0;

	FOR_EACH_IN_EARRAY_FORWARDS(s_pSyntheticLogData->eaSyntheticLogDataReplicate, SyntheticLogDataReplicate, pSyntheticLogDataReplicate)
	{
		pSyntheticLogDataReplicate->iTransactionsCreated = 0;
		pSyntheticLogDataReplicate->iTransactionsProcessed = 0;
	}
	FOR_EACH_END;

	if(s_pSyntheticLogData->pReplaySyntheticLogData)
		s_pSyntheticLogData->iReplayWhileReplicatingFolderID = iFolderID;

	fprintf(fileGetStderr(), "Multiplier of %u will be used to expand logs for containers marked to be replicated.\n", s_pSyntheticLogData->iLogFileMultiplier * s_pSyntheticLogData->iLogLineMultiplier);

	s_pSyntheticLogData->pCurrentOutputFiles = calloc(s_pSyntheticLogData->iLogFileMultiplier, sizeof(FileWrapper*));
	for(i = 0; i < s_pSyntheticLogData->iLogFileMultiplier; i++)
		s_pSyntheticLogData->pCurrentOutputFiles[i] = NULL;

	objSetCommandLogFileReplayCallback(CreateSyntheticLogFileCB);
	objSetCommandReplayCallback(SyntheticLogDataCB);
	s_bPrevStrictMerge = gContainerSource.strictMerge;
	gContainerSource.strictMerge = true;

	s_eaFiles = FindOptionallyCompressedReplayLogs(s_pSyntheticLogData->pcInputFolder);

	if(!GSM_IsStateActive(GSL_REPLAYSYNTHETICLOGS))
		GSM_SwitchToSibling(GSL_REPLAYSYNTHETICLOGS, false);

	s_iStartTimeSecs = timerCpuSeconds();
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(CreateSyntheticLogData);
void cmdCreateSyntheticLogData()
{
	cmdCreateSyntheticLogDataWithFolderID(0);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(CreateSyntheticLogDataUsingGameServers);
void cmdCreateSyntheticLogDataUsingGameServers()
{
	U32 gameServerID = GetAppGlobalID();

	if(!s_pSyntheticLogData)
	{
		fprintf(fileGetStderr(), "ERROR: Could not run CreateSyntheticLogDataUsingGameServers because no SyntheticLogDataFile was supplied on command line or the file failed to parse: %s.\n",
			s_SyntheticLogDataFile);
		return;
	}

	if(!s_pSyntheticLogData->pReplaySyntheticLogData)
	{
		fprintf(fileGetStderr(), "ERROR: Could not run CreateSyntheticLogData because no Replay data was provided in file: %s.\n",
			s_SyntheticLogDataFile);
		return;
	}

	if(gameServerID < s_pSyntheticLogData->pReplaySyntheticLogData->iReplayGameServerMinID || gameServerID > s_pSyntheticLogData->pReplaySyntheticLogData->iReplayGameServerMaxID)
	{
		fprintf(fileGetStderr(), "Not going to CreateSyntheticLogData because the GameServer ID %u is not in the range [%u,%u].\n",
			gameServerID, s_pSyntheticLogData->pReplaySyntheticLogData->iReplayGameServerMinID, s_pSyntheticLogData->pReplaySyntheticLogData->iReplayGameServerMaxID);
		return;
	}

	cmdCreateSyntheticLogDataWithFolderID(gameServerID - s_pSyntheticLogData->pReplaySyntheticLogData->iReplayGameServerMinID);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BEGIN FAKE SUBSCRIPTION TRACKING
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FakeSubscription
{
	GlobalType eContainerGlobalType;
	ContainerID containerID;
	REF_TO(Entity) entityRef;
} FakeSubscription;

StashTable s_FakeSubscriptionsByContainerGlobalType = NULL;

static void EnsureFakeSubscriptions(void)
{
	if(!s_FakeSubscriptionsByContainerGlobalType)
		s_FakeSubscriptionsByContainerGlobalType = stashTableCreateInt(32);
}

static bool FakeSubscribeContainerType(GlobalType eContainerGlobalType)
{
	return eContainerGlobalType == GLOBALTYPE_ENTITYPLAYER || eContainerGlobalType == GLOBALTYPE_TEAM || eContainerGlobalType == GLOBALTYPE_GUILD;
}

// Returns true if found, false if created
static FakeSubscription *s_pMostRecentFakeSubscription = NULL;
static bool FindOrCreateFakeSubscription(GlobalType eContainerGlobalType, ContainerID containerID, bool bSetRef)
{
	StashTable fakeSubscriptionsByContainerID = NULL;
	FakeSubscription *fakeSubscription = NULL;
	bool bFoundResult = true;

	EnsureFakeSubscriptions();

	if(!stashIntFindPointer(s_FakeSubscriptionsByContainerGlobalType, eContainerGlobalType, &fakeSubscriptionsByContainerID))
	{
		fakeSubscriptionsByContainerID = stashTableCreateInt(1024);
		stashIntAddPointer(s_FakeSubscriptionsByContainerGlobalType, eContainerGlobalType, fakeSubscriptionsByContainerID, false);
	}

	if(!stashIntFindPointer(fakeSubscriptionsByContainerID, containerID, &fakeSubscription))
	{
		char idBuf[32];
		fakeSubscription = calloc(1, sizeof(FakeSubscription));
		fakeSubscription->eContainerGlobalType = eContainerGlobalType;
		fakeSubscription->containerID = containerID;
		if(bSetRef)
			SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(eContainerGlobalType), ContainerIDToString(containerID, idBuf), fakeSubscription->entityRef);
		stashIntAddPointer(fakeSubscriptionsByContainerID, containerID, fakeSubscription, false);

		s_pMostRecentFakeSubscription = fakeSubscription;

		bFoundResult = false;
	}

	return bFoundResult;
}

static void DestroyFakeSubscription(GlobalType eContainerGlobalType, ContainerID containerID)
{
	StashTable fakeSubscriptionsByContainerID = NULL;
	FakeSubscription *fakeSubscription = NULL;

	EnsureFakeSubscriptions();

	if(!stashIntFindPointer(s_FakeSubscriptionsByContainerGlobalType, eContainerGlobalType, &fakeSubscriptionsByContainerID))
		return;

	if(!stashIntFindPointer(fakeSubscriptionsByContainerID, containerID, &fakeSubscription))
		return;

	REMOVE_HANDLE(fakeSubscription->entityRef);
	stashIntRemovePointer(fakeSubscriptionsByContainerID, containerID, NULL);
	free(fakeSubscription);
}

static void AddFakeSubscription(GlobalType eContainerGlobalType, ContainerID containerID)
{
	if(s_pSyntheticLogData->pReplaySyntheticLogData && s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsPerSecond > 0)
		if(FakeSubscribeContainerType(eContainerGlobalType))
			FindOrCreateFakeSubscription(eContainerGlobalType, containerID, /*bSetRef=*/true);
}

static void RemoveFakeSubscription(GlobalType eContainerGlobalType, ContainerID containerID)
{
	if(s_pSyntheticLogData->pReplaySyntheticLogData && s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsPerSecond)
		DestroyFakeSubscription(eContainerGlobalType, containerID);
}

typedef struct FakeSubscriptionQueueData
{
	bool bRemove;
	GlobalType eContainerGlobalType;
	ContainerID containerID;
} FakeSubscriptionQueueData;

FakeSubscriptionQueueData **s_eaFakeSubscriptionQueueData = NULL;

static void HandleFakeSubscriptionQueueData_Tick()
{
	U32 thisGameServerID = GetAppGlobalID();

	if(!s_pSyntheticLogData || !s_pSyntheticLogData->pReplaySyntheticLogData)
		return;

	if(s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsGameServerMinID && s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsGameServerMaxID
			&& thisGameServerID >= s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsGameServerMinID
			&& thisGameServerID <= s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsGameServerMaxID)
	{
		static U32 fakeSubscriptionCount = 0;

		FakeSubscriptionQueueData *pFakeSubscriptionQueueData = NULL;
		int count = 0;

		if(s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsPerSecond > 0)
		{
			if(s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsPerSecond >= 30)
				count = randomIntRange(s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsPerSecond / 30,
					(s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsPerSecond + 30 ) / 30);
			else
			{
				static U32 s_iTickCount = 0;
				U32 ticksBetweenSubs = randomIntRange(30 / s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsPerSecond,
					30 / s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsPerSecond + 1);
				if(s_iTickCount++ >= ticksBetweenSubs)
				{
					s_iTickCount = 0;
					count = 1;
				}
			}
		}

		while(count > 0)
		{
			if(0 == eaSize(&s_eaFakeSubscriptionQueueData))
				break;

			count--;

			pFakeSubscriptionQueueData = eaRemove(&s_eaFakeSubscriptionQueueData, 0);

			if(pFakeSubscriptionQueueData->bRemove)
			{
				fakeSubscriptionCount--;
				RemoveFakeSubscription(pFakeSubscriptionQueueData->eContainerGlobalType, pFakeSubscriptionQueueData->containerID);
			}
			else
			{
				fakeSubscriptionCount++;
				AddFakeSubscription(pFakeSubscriptionQueueData->eContainerGlobalType, pFakeSubscriptionQueueData->containerID);
			}

			SAFE_FREE(pFakeSubscriptionQueueData);
		}

		if(s_pMostRecentFakeSubscription)
		{
			while(count > 0)
			{
				char idBuf[32];

				REMOVE_HANDLE(s_pMostRecentFakeSubscription->entityRef);
				SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(s_pMostRecentFakeSubscription->eContainerGlobalType), ContainerIDToString(s_pMostRecentFakeSubscription->containerID, idBuf), s_pMostRecentFakeSubscription->entityRef);

				count--;
			}
		}

		{
			static U32 lastPrintTime = 0;
			static U32 lastFakeSubscriptionCount = 0;
			U32 thisPrintTime = timerCpuSeconds();
			if((0 == lastPrintTime || thisPrintTime >= lastPrintTime + 3) && (0 == lastFakeSubscriptionCount || fakeSubscriptionCount > lastFakeSubscriptionCount))
			{
				fprintf(fileGetStderr(), "Total fake subscriptions: %d. Queue size: %d\n", fakeSubscriptionCount, eaSize(&s_eaFakeSubscriptionQueueData));
				lastPrintTime = thisPrintTime;
				lastFakeSubscriptionCount = fakeSubscriptionCount;
			}
		}
	}
}

static void QueueAddFakeSubscription(GlobalType eContainerGlobalType, ContainerID containerID)
{
	if(FakeSubscribeContainerType(eContainerGlobalType))
	{
		U32 thisGameServerID = GetAppGlobalID();

		if(!s_pSyntheticLogData || !s_pSyntheticLogData->pReplaySyntheticLogData)
			return;

		if(s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsGameServerMinID && s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsGameServerMaxID
				&& (thisGameServerID < s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsGameServerMinID
					|| thisGameServerID > s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsGameServerMaxID))
		{
			static U32 lastGameServerID = 0;
			if(!FindOrCreateFakeSubscription(eContainerGlobalType, containerID, /*bSetRef=*/false))
			{
				U32 numGameServers = (s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsGameServerMaxID - s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsGameServerMinID + 1)
					/ (s_pSyntheticLogData->pReplaySyntheticLogData->iReplayGameServerMaxID - s_pSyntheticLogData->pReplaySyntheticLogData->iReplayGameServerMinID + 1);
				U32 startGameServerID = (thisGameServerID - s_pSyntheticLogData->pReplaySyntheticLogData->iReplayGameServerMinID)
					* numGameServers + s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsGameServerMinID;
				U32 gameServerID;

				if(0 == lastGameServerID)
					gameServerID = startGameServerID;
				else
				{
					U32 endGameServerID = startGameServerID + numGameServers - 1;
					gameServerID = lastGameServerID + 1;
					if(gameServerID > endGameServerID)
						gameServerID = startGameServerID;
				}

				RemoteCommand_RemoteQueueAddFakeSubscription(GLOBALTYPE_GAMESERVER, gameServerID, eContainerGlobalType, containerID);

				lastGameServerID = gameServerID;
			}
		}
		else if(s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsPerSecond > 0)
		{
			FakeSubscriptionQueueData *pFakeSubscriptionQueueData = calloc(1, sizeof(FakeSubscriptionQueueData));
			pFakeSubscriptionQueueData->bRemove = false;
			pFakeSubscriptionQueueData->eContainerGlobalType = eContainerGlobalType;
			pFakeSubscriptionQueueData->containerID = containerID;

			eaPush(&s_eaFakeSubscriptionQueueData, pFakeSubscriptionQueueData);
		}
	}
}

AUTO_COMMAND_REMOTE;
void RemoteQueueAddFakeSubscription(GlobalType eContainerGlobalType, ContainerID containerID)
{
	QueueAddFakeSubscription(eContainerGlobalType, containerID);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// END FAKE SUBSCRIPTION TRACKING
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ReplaySyntheticLogDataCB(const char *command, U64 sequence, U32 timestamp)
{
	U32 uContainerType = GLOBALTYPE_NONE;
	U32 uContainerID = 0;
	const char *pStr = NULL;
	bool bUpdateContainerOwner = false;
	bool bUpdateContainer = false;

	if(nullStr(command))
		return;

	bUpdateContainer = strStartsWith(command, "dbUpdateContainer ");
	bUpdateContainerOwner = bUpdateContainer ? false : strStartsWith(command, "dbUpdateContainerOwner ");

	if(!bUpdateContainer && !bUpdateContainerOwner)
		return;

	if(s_pSyntheticLogData && s_pSyntheticLogData->pReplaySyntheticLogData && s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsPerSecond > 0)
	{
		pStr = strchr_fast(command, ' ');
		pStr++;
		uContainerType = atoi(pStr);

		pStr = strchr_fast(pStr, ' ');
		pStr++;
		uContainerID = atoi(pStr);
	}

	if(bUpdateContainerOwner)
	{
		if(s_pSyntheticLogData && s_pSyntheticLogData->pReplaySyntheticLogData && s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsPerSecond > 0)
		{
			U32 uOwnerType;
			U32 uOwnerID;

			pStr = strchr_fast(pStr, ' ');
			pStr++;
			uOwnerType = atoi(pStr);

			pStr = strchr_fast(pStr, ' ');
			pStr++;
			uOwnerID = atoi(pStr);

			QueueAddFakeSubscription(uContainerType, uContainerID);
			if(s_bSimulateContainerMoves && uContainerType == GLOBALTYPE_ENTITYPLAYER)
			{
				if(uOwnerType == GLOBALTYPE_OBJECTDB)
				{
					objRequestContainerMove(NULL, uContainerType, uContainerID, GetAppGlobalType(), GetAppGlobalID(), GLOBALTYPE_OBJECTDB, 0);
				}
				else
				{
					MapPartitionSummary emptySummary = {0};
					emptySummary.uPartitionID = 1;
					HereIsPartitionInfoForUpcomingMapTransfer(0, uContainerID, 1, &emptySummary);
					objRequestContainerMove(NULL, uContainerType, uContainerID, GLOBALTYPE_OBJECTDB, 0, GetAppGlobalType(), GetAppGlobalID());
				}
			}
		}
	}
	else
	{
		if(s_pSyntheticLogData && s_pSyntheticLogData->pReplaySyntheticLogData && s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsPerSecond > 0)
			QueueAddFakeSubscription(uContainerType, uContainerID);

		RemoteCommand_RemoteHandleDatabaseSyntheticLogDataString(GLOBALTYPE_OBJECTDB, 0, command, sequence, timestamp);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(ReplaySyntheticLogDataWithFolderID);
void cmdReplaySyntheticLogDataWithFolderID(U32 iFolderID)
{
	char buf[MAX_PATH];

	if(!s_pSyntheticLogData)
	{
		fprintf(fileGetStderr(), "ERROR: Could not run ReplaySyntheticLogDataWithFolderID because no SyntheticLogDataFile was supplied on command line or the file failed to parse.\n");
		return;
	}

	if(!GSM_IsStateActive(GSL_RUNNING) && !GSM_IsStateActive(GSL_REPLAYSYNTHETICLOGS))
	{
		fprintf(fileGetStderr(), "ERROR: Could not run ReplaySyntheticLogDataWithFolderID because the GameServer is not in the ready state or already in the ReplaySyntheticLogs state.\n");
		return;
	}

	s_bPrevStrictMerge = gContainerSource.strictMerge;

	sprintf(buf, "%s\\%u", s_pSyntheticLogData->pcInputFolder, iFolderID);

	objSetCommandReplayCallback(ReplaySyntheticLogDataCB);
	gContainerSource.strictMerge = true;

	s_eaFiles = FindOptionallyCompressedReplayLogs(buf);

	if(!GSM_IsStateActive(GSL_REPLAYSYNTHETICLOGS))
		GSM_SwitchToSibling(GSL_REPLAYSYNTHETICLOGS, false);

	s_iStartTimeSecs = timerCpuSeconds();

	fprintf(fileGetStderr(), "GameServer ID %u is using folder %u in %s to replay synthetic logs", GetAppGlobalID(), iFolderID, s_pSyntheticLogData->pcInputFolder);

	if(s_bReplaySyntheticLogDataInRealtime)
		fprintf(fileGetStderr(), " in real time");
	else if(s_iMaxSyntheticReplayTransactionsPerFrame)
		fprintf(fileGetStderr(), " at no more than %u transactions per frame", s_iMaxSyntheticReplayTransactionsPerFrame);

	if(!nullStr(s_StopSyntheticReplayAt))
		fprintf(fileGetStderr(), " stopping at %s (%u)", s_StopSyntheticReplayAt, s_iStopSyntheticReplayAtTimestamp);
	else
		fprintf(fileGetStderr(), ".\n");
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(ReplaySyntheticLogData);
void cmdReplaySyntheticLogData()
{
	cmdReplaySyntheticLogDataWithFolderID(0);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(ReplaySyntheticLogDataUsingGameServers);
void cmdReplaySyntheticLogDataUsingGameServers()
{
	U32 gameServerID = GetAppGlobalID();

	if(!s_pSyntheticLogData)
	{
		fprintf(fileGetStderr(), "ERROR: Could not run ReplaySyntheticLogDataUsingGameServers because no SyntheticLogDataFile was supplied on command line or the file failed to parse: %s.\n",
			s_SyntheticLogDataFile);
		return;
	}

	if(!s_pSyntheticLogData->pReplaySyntheticLogData)
	{
		fprintf(fileGetStderr(), "ERROR: Could not run ReplaySyntheticLogDataUsingGameServers because no Replay data was provided in file: %s.\n",
			s_SyntheticLogDataFile);
		return;
	}

	if(gameServerID < s_pSyntheticLogData->pReplaySyntheticLogData->iReplayGameServerMinID || gameServerID > s_pSyntheticLogData->pReplaySyntheticLogData->iReplayGameServerMaxID)
	{
		if(s_pSyntheticLogData->pReplaySyntheticLogData->iFakeSubscriptionsPerSecond > 0)
		{
			if(!GSM_IsStateActive(GSL_RUNNING) && !GSM_IsStateActive(GSL_REPLAYSYNTHETICLOGS))
			{
				fprintf(fileGetStderr(), "ERROR: Could not run ReplaySyntheticLogDataUsingGameServers because the GameServer is not in the ready state or already in the ReplaySyntheticLogs state.\n");
				return;
			}

			fprintf(fileGetStderr(), "GameServer ID %u is not in the range [%u,%u], but is available for faking subscriptions.\n",
				gameServerID, s_pSyntheticLogData->pReplaySyntheticLogData->iReplayGameServerMinID, s_pSyntheticLogData->pReplaySyntheticLogData->iReplayGameServerMaxID);

			if(!GSM_IsStateActive(GSL_REPLAYSYNTHETICLOGS))
				GSM_SwitchToSibling(GSL_REPLAYSYNTHETICLOGS, false);
		}
		else
			fprintf(fileGetStderr(), "ERROR: Not going to ReplaySyntheticLogDataUsingGameServers because the GameServer ID %u is not in the range [%u,%u].\n",
				gameServerID, s_pSyntheticLogData->pReplaySyntheticLogData->iReplayGameServerMinID, s_pSyntheticLogData->pReplaySyntheticLogData->iReplayGameServerMaxID);
	}
	else
		cmdReplaySyntheticLogDataWithFolderID(gameServerID - s_pSyntheticLogData->pReplaySyntheticLogData->iReplayGameServerMinID);
}

#include "AutoGen/cmdServerSyntheticLogs_c_ast.c"
