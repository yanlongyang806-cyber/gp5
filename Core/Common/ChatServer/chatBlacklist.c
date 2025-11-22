#include "chatBlacklist.h"

#include "GlobalComm.h"
#include "error.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "ResourceInfo.h"
#include "StringUtil.h"
#include "TextFilter.h"
#include "timing_profiler.h"

#include "AutoGen/chatBlacklist_h_ast.h"

static bool sbDisableBlacklist = false;
AUTO_CMD_INT(sbDisableBlacklist, DisableBlacklist) ACMD_CMDLINE;

// Shared Common File for Chat Blacklist - Global Chat Server, Shard Chat Server, Chat Relay
static ChatBlacklist sChatBlacklist = {0};
// Chat Blacklist TextFilter strings are not scrambled since the client does not see them
static FilterTrieNode* sChatBlacklistTrie = NULL;

AUTO_COMMAND ACMD_CATEGORY(ChatDebug);
void PrintChatBlacklistTrie(void)
{
	tf_DebugPrintTrie(sChatBlacklistTrie);
}

static void ChatBlacklist_AddStringToTrie(ChatBlacklistString* blString)
{
	FilterTrieNode* node;
	if (!sChatBlacklistTrie)
		sChatBlacklistTrie = tf_Create();
	node = tf_AddStringAlreadyNormalizedAndReduced(sChatBlacklistTrie, blString->string, false);
	node->ignoreSplitTokens = true;
	//node->ignore1337Coalesce = true;
}

static void ChatBlacklist_RemoveStringFromTrie(const char *string)
{
	if (!sChatBlacklistTrie)
		return;
	tf_RemoveStringAlreadyNormalizedAndReduced(sChatBlacklistTrie, string, false);
}

// Reset and initialized the Chat Blacklist TextFilter Trie
static void ChatBlacklist_ResetTrie(void)
{
	FilterTrieNode* node;
	if (sChatBlacklistTrie)
		tf_Free( sChatBlacklistTrie );
	sChatBlacklistTrie = tf_Create();
	EARRAY_CONST_FOREACH_BEGIN(sChatBlacklist.eaStrings, i, n);
	{
		node = tf_AddStringAlreadyNormalizedAndReduced(sChatBlacklistTrie, sChatBlacklist.eaStrings[i]->string, false);
		node->ignoreSplitTokens = true;
		//node->ignore1337Coalesce = true;
	}
	EARRAY_FOREACH_END;
}

const ChatBlacklist *blacklist_GetList(void)
{
	return &sChatBlacklist;
}

// Does not check if blacklist string is already in list
void blacklist_AddStringStruct(ChatBlacklistString *blString)
{
	if (!sChatBlacklist.eaStrings)
		eaIndexedEnable(&sChatBlacklist.eaStrings, parse_ChatBlacklistString);
	eaIndexedAdd(&sChatBlacklist.eaStrings, blString);
	ChatBlacklist_AddStringToTrie(blString);
}

ChatBlacklistString *blacklist_RemoveString_Internal(const char *string, bool bDestroy)
{
	ChatBlacklistString *blString = NULL;
	if (nullStr(string) || !sChatBlacklist.eaStrings)
		return NULL;
	blString = eaIndexedRemoveUsingString(&sChatBlacklist.eaStrings, string);
	if (!blString)
		return NULL;
	ChatBlacklist_RemoveStringFromTrie(string);
	if (bDestroy)
	{
		StructDestroy(parse_ChatBlacklistString, blString);
		return NULL;
	}
	return blString;
}

void blacklist_ReplaceBlacklist(const ChatBlacklist *blacklist)
{
	StructDeInit(parse_ChatBlacklist, &sChatBlacklist);
	StructCopy(parse_ChatBlacklist, blacklist, &sChatBlacklist, 0, 0, 0);
	ChatBlacklist_ResetTrie();
}

bool blacklist_CheckForViolations(const char *string, const ChatBlacklistString **ppViolation)
{
	return false;
}

ChatBlacklistString *blacklist_LookupString(const char *string)
{
	if (sChatBlacklist.eaStrings)
		return eaIndexedGetUsingString(&sChatBlacklist.eaStrings, string);
	return NULL;
}

void blacklist_HandleUpdate(const ChatBlacklist *blacklist, ChatBlacklistUpdate eUpdateType)
{
#ifndef GLOBALCHATSERVER
	switch (eUpdateType)
	{
	case CHATBLACKLIST_REPLACE:
		blacklist_ReplaceBlacklist(blacklist);
		break;
	case CHATBLACKLIST_ADD:
		EARRAY_CONST_FOREACH_BEGIN(blacklist->eaStrings, i, s);
		{
			ChatBlacklistString *blString = NULL;
			if (nullStr(blacklist->eaStrings[i]->string))
				continue;
			blString = blacklist_LookupString(blacklist->eaStrings[i]->string);
			if (!blString)
				blacklist_AddStringStruct(StructClone(parse_ChatBlacklistString, blacklist->eaStrings[i]));
		}
		EARRAY_FOREACH_END;
		break;
	case CHATBLACKLIST_REMOVE:
		EARRAY_CONST_FOREACH_BEGIN(blacklist->eaStrings, i, s);
		{
			blacklist_RemoveString_Internal(blacklist->eaStrings[i]->string, true);
		}
		EARRAY_FOREACH_END;
		break;
	}
#endif
}

// Blacklist Initialization and Persistence - Only Occurs on GCS
#ifdef GLOBALCHATSERVER
#define CHAT_BLACKLIST_FILENAME "server/ChatBlacklist.txt"
void saveBlacklistFile(void)
{
	char fileAbsoluteLoc[MAX_PATH];	
	sprintf(fileAbsoluteLoc, "%s/%s", fileLocalDataDir(), CHAT_BLACKLIST_FILENAME);
	ParserWriteTextFile(fileAbsoluteLoc, parse_ChatBlacklist, &sChatBlacklist, 0, 0);
}

static void blacklistReloadCallback(const char *relpath, int when)
{
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	if (!fileExists(relpath))
		; // File was deleted, do we care here?
	
	StructReset(parse_ChatBlacklist, &sChatBlacklist);
	if(!ParserReadTextFile(relpath, parse_ChatBlacklist, &sChatBlacklist, PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE | PARSER_OPTIONALFLAG));
	ChatBlacklist_ResetTrie();
}
#endif

void initChatBlacklist(void)
{
#ifdef GLOBALCHATSERVER
	char fileAbsoluteLoc[MAX_PATH];	
	sprintf(fileAbsoluteLoc, "%s/%s", fileLocalDataDir(), CHAT_BLACKLIST_FILENAME);
	blacklistReloadCallback(fileAbsoluteLoc, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, CHAT_BLACKLIST_FILENAME, blacklistReloadCallback);
#endif	
	resRegisterDictionaryForEArray("Chat Blacklist", RESCATEGORY_OTHER, 0, &sChatBlacklist.eaStrings, parse_ChatBlacklistString);
}

#include "AutoGen/chatBlacklist_h_ast.c"
