#include "NotesServer.h"
#include "superassert.h"
#include "sysutil.h"
#include "MemoryMonitor.h"
#include "cmdparse.h"
#include "foldercache.h"
#include "file.h"
#include "globaltypes.h"
#include "UtilitiesLIb.h"
#include "net/net.h"
#include "serverLib.h"
#include "NameValuePair.h"
#include "NotesServer_pub.h"
#include "StructNet.h"
#include "NotesServer.h"
#include "StashTable.h"
#include "resourceInfo.h"
#include "StringUtil.h"
#include "GenericHTTPServing.h"
#include "fileUtil2.h"
#include "Alerts.h"
#include "url.h"
#include "qsortG.h"
#include "logging.h"
#include "NotesServer_h_ast.h"
#include "TextParser.h"
#include "ResourceInfo.h"
#include "NotesServer_pub_h_ast.h"
#include "sock.h"




#define MAX_LOGS_PER_NOTE 256

static NetListen *spNotesServerListen = NULL;
static StashTable sNotesByUniqueName = NULL;

typedef struct NotesServerDomain
{
	char *pDomainName;
	StashTable sNotesByName;
} NotesServerDomain;

static StashTable sDomainsByName = NULL;


typedef struct NotesServerUserData 
{
	char domainName[256];
	char productName[256];
	char systemName[256];
} NotesServerUserData;

char *pRootDirForNotes = "c:\\Notes";
AUTO_CMD_ESTRING(pRootDirForNotes, RootDirForNotes) ACMD_COMMANDLINE;


NotesServerNote *FindNoteFromDomainAndName(char *pDomainName, char *pNoteName)
{
	NotesServerDomain *pDomain;
	NotesServerNote *pNote;

	if (!stashFindPointer(sDomainsByName, pDomainName, &pDomain))
	{
		return NULL;
	}

	if (!stashFindPointer(pDomain->sNotesByName, pNoteName, &pNote))
	{
		return NULL;
	}

	return pNote;
}

void AddNoteToLists(NotesServerNote *pNote)
{
	NotesServerDomain *pDomain;

	if (!stashFindPointer(sDomainsByName, pNote->pDomainName, &pDomain))
	{
		pDomain = calloc(sizeof(NotesServerDomain), 1);
		pDomain->pDomainName = strdup(pNote->pDomainName);
		pDomain->sNotesByName = stashTableCreateWithStringKeys(16, StashDefault);
		stashAddPointer(sDomainsByName, pDomain->pDomainName, pDomain, false);
	}

	estrPrintf(&pNote->pUniqueName, "%s.%s", pNote->pDomainName, pNote->pNoteName);
	stashAddPointer(pDomain->sNotesByName, pNote->pNoteName, pNote, false);
	stashAddPointer(sNotesByUniqueName, pNote->pUniqueName, pNote, false);
}

bool VerifyNoteValidityAfterFileLoad(NotesServerNote *pNote)
{
	if (!(pNote->pDomainName && pNote->pDomainName[0]))
	{
		return false;
	}

	if (!(pNote->pNoteName && pNote->pNoteName[0]))
	{
		return false;
	}

	return true;
}

void LoadAndInitNotes(void)
{
	char **ppFileList;
	int i;

	sNotesByUniqueName = stashTableCreateWithStringKeys(16, StashDefault);
	sDomainsByName = stashTableCreateWithStringKeys(16, StashDefault);
	resRegisterDictionaryForStashTable("Notes",  RESCATEGORY_OTHER, 0, sNotesByUniqueName, parse_NotesServerNote);

	ppFileList = fileScanDirFolders(pRootDirForNotes, FSF_FILES);

	for (i = 0; i < eaSize(&ppFileList); i++)
	{
		if (strEndsWith(ppFileList[i], ".txt"))
		{
			NotesServerNote *pNote = StructCreate(parse_NotesServerNote);
			if (!ParserReadTextFile(ppFileList[i], parse_NotesServerNote, pNote, 0))
			{
				char *pTempString = NULL;
				ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
				ParserReadTextFile(ppFileList[i], parse_NotesServerNote, pNote, 0);
				ErrorfPopCallback();

				StructDestroy(parse_NotesServerNote, pNote);
				WARNING_NETOPS_ALERT("CANT_READ_NOTE", "Failed to read note data from %s. Error: %s",
					ppFileList[i], pTempString);
				estrDestroy(&pTempString);
				StructDestroy(parse_NotesServerNote, pNote);
			}
			else if (!VerifyNoteValidityAfterFileLoad(pNote))
			{
				WARNING_NETOPS_ALERT("CORRUPT_NOTE", "Read a note from %s, but it was badly formed in some fashion",
					ppFileList[i]);
				StructDestroy(parse_NotesServerNote, pNote);
			}
			else
			{
				NotesServerNote *pOtherNote;
				if ((pOtherNote = FindNoteFromDomainAndName(pNote->pDomainName, pNote->pNoteName)))
				{
					WARNING_NETOPS_ALERT("DUPLICATE_NOTES", "Files %s and %s both contain notes named %s in domain %s",
						pOtherNote->pFileName, ppFileList[i], pNote->pNoteName, pNote->pDomainName);
					StructDestroy(parse_NotesServerNote, pNote);
				}
				else
				{
					AddNoteToLists(pNote);
				}
			}
		}
	}

	fileScanDirFreeNames(ppFileList);
}



void GetNoteFileName(NotesServerNote *pNote, char **ppFileName)
{
	static char *pTemp1 = NULL;
	static char *pTemp2 = NULL;

	estrCopy2(&pTemp1, pNote->pNoteName);
	estrCopy2(&pTemp2, pNote->pDomainName);

	estrMakeAllAlphaNumAndUnderscores(&pTemp1);
	estrMakeAllAlphaNumAndUnderscores(&pTemp2);

	estrPrintf(ppFileName, "%s\\%s_%s.txt", pRootDirForNotes, pTemp2, pTemp1);
}

void SaveNoteToDisk(NotesServerNote *pNote)
{
	char *pFileName = NULL;
	GetNoteFileName(pNote, &pFileName);

	mkdirtree_const(pFileName);
	ParserWriteTextFile(pFileName, parse_NotesServerNote, pNote, 0, 0);

	estrDestroy(&pFileName);
}

void NoteLog(NotesServerNote *pNote, FORMAT_STR const char *pFmt, ...)
{
	char *pLog = NULL;
	estrPrintf(&pLog, "%s: ", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000()));
	estrGetVarArgs(&pLog, pFmt);

	eaInsert(&pNote->ppLogs, strdup(pLog), 0);
	estrDestroy(&pLog);

	while (eaSize(&pNote->ppLogs) >= MAX_LOGS_PER_NOTE)
	{
		free(eaPop(&pNote->ppLogs));
	}
}

//should return NULL if it would be all empty
SingleNote *GetSingleNoteForProductAndSystem(NotesServerNote *pInNote, char *pProductName, char *pSystemName)
{
	NotesServerNoteWrapper *pProductWrapper = eaIndexedGetUsingString(&pInNote->ppProductNotes, pProductName);
	NotesServerNoteWrapper *pSystemWrapper = eaIndexedGetUsingString(&pInNote->ppSystemNotes, pSystemName);
	SingleNote *pOutNote;

	if (InternalNoteIsEmpty(&pInNote->globalNote) && (!pProductWrapper || InternalNoteIsEmpty(&pProductWrapper->note))
		&&  (!pSystemWrapper || InternalNoteIsEmpty(&pSystemWrapper->note)))
	{
		return NULL;
	}

	pOutNote = StructCreate(parse_SingleNote);
	estrCopy2(&pOutNote->pNoteName, pInNote->pNoteName);

	StructCopy(parse_SingleNote_Internal, &pInNote->globalNote, &pOutNote->comments[NOTESCOPE_GLOBAL], 0, 0, 0);

	if (pProductWrapper)
	{
		StructCopy(parse_SingleNote_Internal, &pProductWrapper->note,  &pOutNote->comments[NOTESCOPE_PRODUCT], 0, 0, 0);
	}
		
	if (pSystemWrapper)
	{
		StructCopy(parse_SingleNote_Internal, &pSystemWrapper->note,  &pOutNote->comments[NOTESCOPE_SYSTEM], 0, 0, 0);
	}

	return pOutNote;
}




void SendAllNotesFromDomainToSystem(Packet *pOutPack, NotesServerUserData *user_data)
{
	SingleNoteList *pNoteList = StructCreate(parse_SingleNoteList);
	NotesServerDomain *pDomain;

	if (stashFindPointer(sDomainsByName, user_data->domainName, &pDomain))
	{
		FOR_EACH_IN_STASHTABLE(pDomain->sNotesByName, NotesServerNote, pNote)
		{
			SingleNote *pOutNote = GetSingleNoteForProductAndSystem(pNote, user_data->productName, user_data->systemName);
			if (pOutNote)
			{
				eaPush(&pNoteList->ppNotes, pOutNote);
			}
		}
		FOR_EACH_END;
	}

	ParserSendStructSafe(parse_SingleNoteList, pOutPack, pNoteList);
	StructDestroy(parse_SingleNoteList, pNoteList);
}

NotesServerInfoStruct *GetNotesServerInfoStruct(void)
{
	static NotesServerInfoStruct *spStruct = NULL;

	if (!spStruct)
	{
		spStruct = StructCreate(parse_NotesServerInfoStruct);
		spStruct->iIP = getHostPublicIp();
		spStruct->iPort = DEFAULT_NOTESSERVER_HTML_PORT;
	}

	return spStruct;
}

bool VerifySettingStructValidity(NoteSettingStruct *pSettingStruct)
{
	if (!pSettingStruct->pRegisterStruct)
	{
		return false;
	}

	if (!(pSettingStruct->pRegisterStruct->pDomainName && pSettingStruct->pRegisterStruct->pDomainName[0]))
	{
		return false;
	}

	if (!(pSettingStruct->pRegisterStruct->pProductName && pSettingStruct->pRegisterStruct->pProductName[0]))
	{
		return false;
	}

	if (!(pSettingStruct->pRegisterStruct->pSystemName && pSettingStruct->pRegisterStruct->pSystemName[0]))
	{
		return false;
	}

	if (!(pSettingStruct->pSettingComment && pSettingStruct->pSettingComment[0]))
	{
		return false;
	}

	if (!pSettingStruct->pNote)
	{
		return false;
	}

	if (!(pSettingStruct->pNote->pNoteName && pSettingStruct->pNote->pNoteName[0]))
	{
		return false;
	}

	return true;
}

void SetNoteComment(NotesServerNote *pNote, NoteScopeType eScope, bool bCritical, char *pComment, NoteSettingStruct *pSettingStruct)
{
	NotesServerNoteWrapper *pWrapper;
	SingleNote_Internal *pInternalNote;
	switch (eScope)
	{
	xcase NOTESCOPE_GLOBAL:
		pInternalNote = &pNote->globalNote;
		
	xcase NOTESCOPE_PRODUCT:
		pWrapper = eaIndexedGetUsingString(&pNote->ppProductNotes, pSettingStruct->pRegisterStruct->pProductName);

		if (pWrapper)
		{
			pInternalNote = &pWrapper->note;
		}
		else
		{
			if (pComment && pComment[0])
			{
				pWrapper = StructCreate(parse_NotesServerNoteWrapper);
				pWrapper->pSystemOrProductName = strdup(pSettingStruct->pRegisterStruct->pProductName);
				eaPush(&pNote->ppProductNotes, pWrapper);
				pInternalNote = &pWrapper->note;
			}
			else
			{
				return;
			}
		}

	xcase NOTESCOPE_SYSTEM:
		pWrapper = eaIndexedGetUsingString(&pNote->ppSystemNotes, pSettingStruct->pRegisterStruct->pSystemName);

		if (pWrapper)
		{
			pInternalNote = &pWrapper->note;
		}
		else
		{
			if (pComment && pComment[0])
			{
				pWrapper = StructCreate(parse_NotesServerNoteWrapper);
				pWrapper->pSystemOrProductName = strdup(pSettingStruct->pRegisterStruct->pSystemName);
				eaPush(&pNote->ppSystemNotes, pWrapper);
				pInternalNote = &pWrapper->note;
			}
			else
			{
				return;
			}
		}

	xdefault:
		assertmsgf(0, "Unknown note Scope %d\n", eScope);
		return;
	}

	NoteLog(pNote, "Changing %s value (%s) from \"%s\" to \"%s\" due to request from %s %s %s. Setting comment: %s",
		StaticDefineIntRevLookup(NoteScopeTypeEnum, eScope), bCritical ? "Critical" : "Normal",
		bCritical ? pInternalNote->pCritical : pInternalNote->pNormal,
		pComment, pSettingStruct->pRegisterStruct->pProductName,
		pSettingStruct->pRegisterStruct->pDomainName,
		pSettingStruct->pRegisterStruct->pSystemName,
		pSettingStruct->pSettingComment);

	if (bCritical)
	{
		SAFE_FREE(pInternalNote->pCritical);
		pInternalNote->pCritical = strdup(pComment);
	}
	else
	{
		SAFE_FREE(pInternalNote->pNormal);
		pInternalNote->pNormal = strdup(pComment);
	}
}

void ApplySettingStructToExistingNote(NotesServerNote *pNote, NoteSettingStruct *pSettingStruct)
{
	NoteScopeType eScopeType;

	for (eScopeType = 0; eScopeType < NOTESCOPE_COUNT; eScopeType++)
	{
		char *pNormalComment = pSettingStruct->pNote->comments[eScopeType].pNormal;
		char *pCriticalComment = pSettingStruct->pNote->comments[eScopeType].pCritical;

		if (stricmp_safe(pNormalComment, NOTE_SETCOMMENT_LEAVE_AS_IS) != 0)
		{
			SetNoteComment(pNote, eScopeType, false, pNormalComment, pSettingStruct);
		}

		if (stricmp_safe(pCriticalComment, NOTE_SETCOMMENT_LEAVE_AS_IS) != 0)
		{
			SetNoteComment(pNote, eScopeType, true, pCriticalComment, pSettingStruct);
		}
	}
}

NotesServerNote *CreateNewNoteFromSettingStruct(NoteSettingStruct *pSettingStruct)
{
	NotesServerNote *pNote = StructCreate(parse_NotesServerNote);
	pNote->pDomainName = strdup(pSettingStruct->pRegisterStruct->pDomainName);
	pNote->pNoteName = strdup(pSettingStruct->pNote->pNoteName);
	
	AddNoteToLists(pNote);

	NoteLog(pNote, "Created, some values being set by %s %s %s. Setting comment: %s",
		pSettingStruct->pRegisterStruct->pProductName,
		pSettingStruct->pRegisterStruct->pDomainName,
		pSettingStruct->pRegisterStruct->pSystemName,
		pSettingStruct->pSettingComment);


	ApplySettingStructToExistingNote(pNote, pSettingStruct);

	return pNote;
}

static NotesServerNote *spNoteForUpdatingSystems;

void UpdateNoteLinkCallback(NetLink* link, NotesServerUserData *pUserData)
{
	if (stricmp_safe(pUserData->domainName, spNoteForUpdatingSystems->pDomainName) == 0)
	{
		SingleNote *pOutNote = GetSingleNoteForProductAndSystem(spNoteForUpdatingSystems, pUserData->productName, pUserData->systemName);
		if (pOutNote)
		{
			SingleNoteList *pNoteList = StructCreate(parse_SingleNoteList);
			Packet *pOutPack = pktCreate(link, FROM_NOTESSERVER_HERE_ARE_NOTES);
			eaPush(&pNoteList->ppNotes, pOutNote);
			ParserSendStructSafe(parse_SingleNoteList, pOutPack, pNoteList);
			StructDestroy(parse_SingleNoteList, pNoteList);
			pktSend(&pOutPack);
		}
	}
}

void SendUpdatedNoteToRelevantSystems(NotesServerNote *pNote, NetLink *link)
{
	spNoteForUpdatingSystems = pNote;
	linkIterate(spNotesServerListen, UpdateNoteLinkCallback);

}

void HandleSettingNote(Packet *pkt,NetLink* link, NotesServerUserData *user_data)
{
	NoteSettingStruct *pSettingStruct = StructCreate(parse_NoteSettingStruct);
	NotesServerNote *pNote;

	ParserRecvStructSafe(parse_NoteSettingStruct, pkt, pSettingStruct);

	if (!VerifySettingStructValidity(pSettingStruct))
	{
		char *pTempString = NULL;
		ParserWriteTextEscaped(&pTempString, parse_NoteSettingStruct, pSettingStruct, 0, 0, 0);

		WARNING_NETOPS_ALERT("BAD_NOTE_SETTING", "Got an invalid note setting attempt, presumably from %s %s. Setting Struct: %s",
			user_data->domainName, user_data->systemName, pTempString);
		StructDestroy(parse_NoteSettingStruct, pSettingStruct);
		estrDestroy(&pTempString);
		return;
	}

	pNote = FindNoteFromDomainAndName(pSettingStruct->pRegisterStruct->pDomainName, pSettingStruct->pNote->pNoteName);

	if (pNote)
	{
		ApplySettingStructToExistingNote(pNote, pSettingStruct);
	}
	else
	{
		pNote = CreateNewNoteFromSettingStruct(pSettingStruct);
	}

	SendUpdatedNoteToRelevantSystems(pNote, link);
	SaveNoteToDisk(pNote);



}

void NotesServerHandleMsg(Packet *pkt,int cmd,NetLink* link,NotesServerUserData *user_data)
{
	switch (cmd)
	{
	xcase TO_NOTESSERVER_REQUESTING_NOTES:
	{
		Packet *pOutPack;
		NotesRegisterStruct *pRegisterStruct;

		pRegisterStruct = StructCreate(parse_NotesRegisterStruct);
		ParserRecvStructSafe(parse_NotesRegisterStruct, pkt, pRegisterStruct);

		strcpy(user_data->domainName, pRegisterStruct->pDomainName);

		if (!pRegisterStruct->pSystemName)
		{
			strcpy(user_data->systemName, makeIpStr(linkGetIp(link)));
		}
		else
		{
			strcpy(user_data->systemName, pRegisterStruct->pSystemName);
		}

		strcpy(user_data->productName, pRegisterStruct->pProductName);

		StructDestroy(parse_NotesRegisterStruct, pRegisterStruct);

		printf("Got connection from %s %s %s\n", user_data->productName, user_data->domainName, user_data->systemName);

		pOutPack = pktCreate(link, FROM_NOTESSERVER_HERE_IS_SERVER_INFO);
		ParserSendStructSafe(parse_NotesServerInfoStruct, pOutPack, GetNotesServerInfoStruct());
		pktSend(&pOutPack);

		pOutPack = pktCreate(link, FROM_NOTESSERVER_HERE_ARE_NOTES);
		SendAllNotesFromDomainToSystem(pOutPack, user_data);
		pktSend(&pOutPack);
	}
	

	xcase TO_NOTESSERVER_SETTING_NOTE:
		HandleSettingNote(pkt, link, user_data);

/*		char *pNoteName = pktGetStringTemp(pkt);
		char *pGlobalComment = pktGetStringTemp(pkt);
		char *pProductName = pktGetStringTemp(pkt);
		char *pProductComment = pktGetStringTemp(pkt);
		char *pShardName = pktGetStringTemp(pkt);
		char *pShardComment = pktGetStringTemp(pkt);

		char *pPrevProductComment = NULL;
		char *pPrevShardComment = NULL;

		NotesServerNote *pNote;



		if (!stashFindPointer(sNotesByName, pNoteName, &pNote))
		{
			pNote = StructCreate(parse_NotesServerNote);
			estrCopy2(&pNote->pName, pNoteName);
			stashAddPointer(sNotesByName, pNote->pName, pNote, false);
			NoteLog(pNote, "Created during request from %s", pShardName);
		}


		pPrevProductComment = GetValueFromNameValuePairs(&pNote->ppProductComments, pProductName);
		pPrevShardComment = GetValueFromNameValuePairs(&pNote->ppShardComments, pShardName);

		if (stricmp_safe(pGlobalComment, NOTE_SETCOMMENT_LEAVE_AS_IS) != 0)
		{
			if (stricmp_safe(pNote->pGlobalComment, pGlobalComment) != 0)
			{
				NoteLog(pNote, "Shard %s requests changing global comment from \"%s\" to \"%s\"",
					pShardName, pNote->pGlobalComment ? pNote->pGlobalComment : "", pGlobalComment ? pGlobalComment : "");
			}
			estrCopy2(&pNote->pGlobalComment, pGlobalComment);
		}

		if (stricmp_safe(pProductComment, NOTE_SETCOMMENT_LEAVE_AS_IS) != 0)
		{
			if (stricmp_safe(pPrevProductComment, pProductComment) != 0)
			{
				NoteLog(pNote, "Shard %s requests changing %s product comment from \"%s\" to \"%s\"",
					pShardName, pProductName, pPrevProductComment ? pPrevProductComment : "", pProductComment ? pProductComment : "");
			}
			if (pProductComment && pProductComment[0])
			{
				UpdateOrSetValueInNameValuePairList(&pNote->ppProductComments, pProductName, pProductComment);
			}
			else
			{
				RemovePairFromNameValuePairList(&pNote->ppProductComments, pProductName);
			}
		}

		if (stricmp_safe(pShardComment, NOTE_SETCOMMENT_LEAVE_AS_IS) != 0)
		{
			if (stricmp_safe(pPrevShardComment, pShardComment) != 0)
			{
				NoteLog(pNote, "Shard %s requests changing %s Shard comment from \"%s\" to \"%s\"",
					pShardName, pShardName, pPrevShardComment ? pPrevShardComment : "", pShardComment ? pShardComment : "");
			}
			if (pShardComment && pShardComment[0])
			{
				UpdateOrSetValueInNameValuePairList(&pNote->ppShardComments, pShardName, pShardComment);
			}
			else
			{
				RemovePairFromNameValuePairList(&pNote->ppShardComments, pShardName);
			}
		}

		SaveNoteToDisk(pNote);

		sSettingNoteContext.pInLink = link;
		sSettingNoteContext.pNote = pNote;

		linkIterate(spNotesServerListen, SettingNoteLinkCallback);*/
	}

}

void NotesServerDisconnect(NetLink* link,NotesServerUserData *user_data)
{

}

int wmain(int argc, WCHAR** argv_wide)
{
	int i;
	char **argv;

	EXCEPTION_HANDLER_BEGIN
	ARGV_WIDE_TO_ARGV
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	FolderCacheChooseMode();


	preloadDLLs(0);


	SetAppGlobalType(GLOBALTYPE_ERRORTRACKER);
	utilitiesLibStartup();


	cmdParseCommandLine(argc, argv);


	//set it so that gimme never pauses
	_putenv_s("GIMME_NO_PAUSE", "1");



	srand((unsigned int)time(NULL));

	sprintf(gServerLibState.logServerHost, "NONE");
	sprintf(gServerLibState.controllerHost, "NONE");

	serverLibStartup(argc, argv);

	LoadAndInitNotes();

	while (!(spNotesServerListen = commListen(commDefault(),LINKTYPE_SHARD_NONCRITICAL_20MEG, LINK_FORCE_FLUSH,DEFAULT_NOTESSERVER_PORT,
		NotesServerHandleMsg,NULL,NotesServerDisconnect,sizeof(NotesServerUserData))))
	{
		Sleep(1);
	}

	SetAppGlobalType(GLOBALTYPE_NOTESSERVER);

	GenericHttpServing_Begin(DEFAULT_NOTESSERVER_HTML_PORT, "CB", NULL, 0);


	while (1)
	{
		utilitiesLibOncePerFrame(REAL_TIME);
		serverLibOncePerFrame();
		Sleep(1);
		commMonitor(commDefault());
		GenericHttpServing_Tick();
	}

	EXCEPTION_HANDLER_END
}



#include "NotesServer_h_ast.c"
