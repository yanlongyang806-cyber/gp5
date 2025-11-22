#include "chatCommonFake.h"

#include "ChatData.h"
#include "EString.h"
#include "GlobalEnums.h"
#include "SimpleParser.h"
#include "textparser.h"

#include "AutoGen/AppLocale_h_ast.h"
#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/ChatData_h_ast.h"
#include "AutoGen/GlobalEnums_h_ast.h"

extern ChatMessage *ChatCommon_CreateMsg(
		const ChatUserInfo *pFrom, const ChatUserInfo *pTo, 
		ChatLogEntryType eType, const char *pchChannel, 
		const char *pchText, const ChatData *pData)
{
	ChatMessage *pMsg;

	if (!pchText || !*pchText) {
		return NULL;
	}
	
	pMsg = StructCreate(parse_ChatMessage);

	if (pFrom) {
		pMsg->pFrom = StructClone(parse_ChatUserInfo, pFrom);
	}

	if (pTo) {
		pMsg->pTo = StructClone(parse_ChatUserInfo, pTo);
	}

	pMsg->eType = eType;

	if (pchChannel) {
		estrCopy2(&pMsg->pchChannel, pchChannel);
	}
	if (pchText) {
		const char *pchTmpText = removeLeadingWhiteSpaces(pchText);
		estrCopy2(&pMsg->pchText, pchTmpText);
		removeTrailingWhiteSpaces(pMsg->pchText);
	}

	if (pData) {
		pMsg->pData = StructClone(parse_ChatData, pData);
	}

	return pMsg;
}

#include "AutoGen/chatCommon_h_ast.c"
#include "AutoGen/chatCommonStructs_h_ast.c"
#include "AutoGen/GlobalEnums_h_ast.c"