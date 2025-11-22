/***************************************************************************



***************************************************************************/

#include "Entity.h"
#include "Player.h"
#include "UIGen.h"
#include "StringUtil.h"
#include "MicrotransBanner.h"

#include "AutoGen/MicrotransBanner_h_ast.h"
//#include "AutoGen/gclMicrotransBanner_c_ast.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("MicrotransBannerBlock", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("MicrotransBannerEntry", BUDGET_GameSystems););


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static MicrotransBannerBlock* s_pCurrentMicrotransBannerBlock = NULL;

static 	U32 suMicrotransBannerRequestTimestamp=0;
static 	U32 suMicrotransBannerReceiveTimestamp=0;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// At some point we should probably create the ability to request individual content blocks.
// For now we'll keep the old mechanism of getting them as a chunk in the ui


//// Get all content and pass it to the uigen
//AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicrotransBanner_GetAllContent);
//void gclMicrotransBanner_GetAllContent(SA_PARAM_NN_VALID UIGen *pGen)
//{
//	MicrotransBannerInfo ***peaContentList = ui_GenGetManagedListSafe(pGen, MicrotransBannerInfo);
//
//	S32 iCount = 0;
//
//	// Do the pass for all results
//	FOR_EACH_IN_EARRAY_FORWARDS(s_MicrotransBannerClientDatas, MicrotransBannerClientData, pClientData)
//	{
//		if (pClientData->uDataReceiveTimestamp > 0 && pClientData->pMicrotransBannerInfo!=NULL)
//		{
//			MicrotransBannerInfo *pStorageSlot = eaGetStruct(peaContentList, parse_MicrotransBannerInfo, iCount++);
//			StructCopyAll(parse_MicrotransBannerInfo, pClientData->pMicrotransBannerInfo, pStorageSlot);
//		}
//	}
//	FOR_EACH_END
//
//	eaSetSizeStruct(peaContentList, parse_MicrotransBannerInfo, iCount);
//
//	ui_GenSetManagedListSafe(pGen, peaContentList, MicrotransBannerInfo, true);
//}

//AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicrotransBanner_IsContentLoading);
// bool gclMicrotransBanner_IsContentLoading(const char *pchContentListName)
//{
//	MicrotransBannerClientData* pCurrentData = gclMicrotransBanner_FindClientData(pchContentListName);
//
//	return(pCurrentData!=NULL && pCurrentData->uDataReceiveTimestamp == 0);
//}


// Request Banners from a named list. 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicrotransBanner_Request);
void gclMicrotransBanner_RequestMicrotransBanner(const char *pchMicrotransBannerSetName)
{
	// Check for last time we asked or received to throttle things
	if ((suMicrotransBannerReceiveTimestamp==0 && timeSecondsSince2000() - suMicrotransBannerRequestTimestamp > 5) || 
		(suMicrotransBannerReceiveTimestamp>0 && timeSecondsSince2000() - suMicrotransBannerReceiveTimestamp > 5))
	{
			
		// We did not receive home page content in the last 5 seconds		
		suMicrotransBannerReceiveTimestamp=0; // Invalidate the old data
		suMicrotransBannerRequestTimestamp=timeSecondsSince2000();	// Note our request time
			
		// Ask the game server for the home page content
		ServerCmd_gslMicrotransBanner_GetBanners(pchMicrotransBannerSetName);
	}
}


AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclMicrotransBannerReceiveInfo(MicrotransBannerBlock* pBlock)
{
	suMicrotransBannerReceiveTimestamp = timeSecondsSince2000();

	if (s_pCurrentMicrotransBannerBlock != NULL)
	{
		StructDestroy(parse_MicrotransBannerBlock, s_pCurrentMicrotransBannerBlock);
	}
	s_pCurrentMicrotransBannerBlock	= StructClone(parse_MicrotransBannerBlock, pBlock);

#if 0
	{
	UIGen *pUIGen;
	const char* pchGenName = "Microtransactions_Home_Banner";

	pUIGen = ui_GenFind(pchGenName, kUIGenTypeNone);
	
	if (pUIGen!=NULL)
	{
		UIGenVarTypeGlob *pGlob = NULL;
		
		pGlob = eaIndexedGetUsingString(&pUIGen->eaVars, "TeamVoteKickTarget");
		if (pGlob)
		{
			estrCopy2(&pGlob->pchString, pTargetDisplayName);
		}		
		pGlob = eaIndexedGetUsingString(&pUIGen->eaVars, "TeamVoteKickReason");
		if (pGlob)
		{
			estrCopy2(&pGlob->pchString, pKickReason);
		}		
		ui_GenSendMessage(pUIGen, "Show");
	}
	}
#endif	
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicrotransBanner_GetCount);
int gclMicrotransBanner_GetCount(void)
{
	if (s_pCurrentMicrotransBannerBlock != NULL)
	{
		return(eaSize(&s_pCurrentMicrotransBannerBlock->ppBannerEntries));
	}
	return(0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicrotransBanner_GetImage);
const char* gclMicrotransBanner_GetImage(int iEntry)
{
	if (s_pCurrentMicrotransBannerBlock != NULL && iEntry < eaSize(&s_pCurrentMicrotransBannerBlock->ppBannerEntries))
	{
		return(s_pCurrentMicrotransBannerBlock->ppBannerEntries[iEntry]->pchImageName);
	}
	return("");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicrotransBanner_GetTitleKey);
const char* gclMicrotransBanner_GetTitleKey(int iEntry)
{
	if (s_pCurrentMicrotransBannerBlock != NULL && iEntry < eaSize(&s_pCurrentMicrotransBannerBlock->ppBannerEntries))
	{
		return(TranslateDisplayMessage(s_pCurrentMicrotransBannerBlock->ppBannerEntries[iEntry]->msgTitleKey));
	}
	return("");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicrotransBanner_GetDescKey);
const char* gclMicrotransBanner_GetDescKey(int iEntry)
{
	if (s_pCurrentMicrotransBannerBlock != NULL && iEntry < eaSize(&s_pCurrentMicrotransBannerBlock->ppBannerEntries))
	{
		return(TranslateDisplayMessage(s_pCurrentMicrotransBannerBlock->ppBannerEntries[iEntry]->msgDescKey));
	}
	return("");
}



#include "AutoGen/MicrotransBanner_h_ast.c"

