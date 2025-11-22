/***************************************************************************



***************************************************************************/
#include "ItemAssignmentsUICommon.h"
#include "gslItemAssignments.h"
#include "ItemAssignments.h"

#include "gslGatewaySession.h"
#include "Entity.h"
#include "Player.h"
#include "GameAccountDataCommon.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "itemEnums.h"

#include "textparser.h"

#include "ItemAssignmentsUICommon_h_ast.h"
#include "itemAssignments_h_ast.h"
#include "itemEnums_h_ast.h"
#include "itemCommon_h_ast.h"

#include "gslGatewayContainerMapping.h"

static void UseSessionItemAssignmentsCachedStruct(Entity *pEnt)
{
	GatewaySession *pSession = wgsFindSessionForAccountId(pEnt->pPlayer->accountID);

	if(pSession)
	{
		if(!pSession->pItemAssignmentsCache)
		{
			pSession->pItemAssignmentsCache = StructCreate(parse_ItemAssignmentCachedStruct);
		}

		pIACache = pSession->pItemAssignmentsCache;
	}
}

static void ClearSlottedItems(Entity *pEnt)
{
	S32 i;

	UseSessionItemAssignmentsCachedStruct(pEnt);

	for (i = eaSize(&pIACache->eaSlots)-1; i >= 0; i--)
	{
		ItemAssignmentsClearSlottedItem(pIACache->eaSlots[i]);
	}

	eaSetSizeStruct(&pIACache->eaSlots, parse_ItemAssignmentSlotUI, 0);
}

AUTO_COMMAND ACMD_NAME(GatewayItemAssignments_CollectRewards) ACMD_ACCESSLEVEL(0) ACMD_LIST(gGatewayCmdList);
void GatewayItemAssignments_CollectRewards(Entity *pEnt, U32 ItemAssignmentID)
{
	UseSessionItemAssignmentsCachedStruct(pEnt);

	gslItemAssignments_CollectRewards(pEnt,ItemAssignmentID);
}

AUTO_COMMAND ACMD_NAME(GatewayItemAssignments_StartAssignment) ACMD_ACCESSLEVEL(0) ACMD_LIST(gGatewayCmdList);
bool GatewayItemAssignments_StartAssignmentAtSlot(Entity *pEnt, const char* pchAssignmentDef)
{
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchAssignmentDef);
	GatewaySession *pSession =  wgsFindSessionForAccountId(pEnt->pPlayer->accountID);
	int iSlot = 0;

	UseSessionItemAssignmentsCachedStruct(pEnt);

	if (pDef && pDef == SAFE_GET_REF2(pSession,pItemAssignmentsCache,hCurrentDef))
	{
		ItemAssignmentSlots Slots = {0};
		S32 i, iSize = eaSize(&pIACache->eaSlots);

		// do a check to make sure this slot is valid
		if (g_ItemAssignmentSettings.pStrictAssignmentSlots)
		{
			if (pEnt)
			{	
				for(i=0;iSlot<g_ItemAssignmentSettings.pStrictAssignmentSlots->iMaxActiveAssignmentSlots;iSlot++)
				{
					if (ItemAssignments_IsValidNewItemAssignmentSlot(pEnt, g_ItemAssignmentSettings.pStrictAssignmentSlots, iSlot))
					{	
						break;
					}
				}
				if(iSlot==g_ItemAssignmentSettings.pStrictAssignmentSlots->iMaxActiveAssignmentSlots)
				{
					// todo: say that we failed because the assignment slot was invalid
					return false;
				}
			}
		}

		for (i = 0; i < iSize; i++)
		{
			ItemAssignmentSlotUI* pSlotUI = pIACache->eaSlots[i];
			if (pSlotUI->uItemID)
			{
				NOCONST(ItemAssignmentSlottedItem)* pSlot = StructCreateNoConst(parse_ItemAssignmentSlottedItem);
				pSlot->uItemID = pSlotUI->uItemID;
				pSlot->iAssignmentSlot = pSlotUI->iAssignmentSlot;
				eaPush(&Slots.eaSlots, (ItemAssignmentSlottedItem*)pSlot);
			}
		}

		gslItemAssignments_StartNewAssignment(pEnt,pchAssignmentDef, iSlot, &Slots);
		StructDeInit(parse_ItemAssignmentSlots, &Slots);
		ClearSlottedItems(pEnt);
		session_ReleaseContainer(pSession,GW_GLOBALTYPE_CRAFTING_DETAIL,REF_STRING_FROM_HANDLE(pSession->pItemAssignmentsCache->hCurrentDef));
		//session_ContainerModified(pSession, GW_GLOBALTYPE_CRAFTING_DETAIL, REF_STRING_FROM_HANDLE(pSession->pItemAssignmentsCache->hCurrentDef));
		REMOVE_HANDLE(pSession->pItemAssignmentsCache->hCurrentDef);
		return true;
	}
	return false;
}

AUTO_COMMAND ACMD_NAME(GatewayItemAssignments_CancelAssignment) ACMD_ACCESSLEVEL(0) ACMD_LIST(gGatewayCmdList);
void GatewayItemAssignments_CancelAssignment(Entity *pEnt, U32 uAssignmentID)
{
	gslItemAssignments_CancelActiveAssignment(pEnt,uAssignmentID);
}

AUTO_COMMAND ACMD_NAME(GatewayItemAssignments_FinishEarly) ACMD_ACCESSLEVEL(0) ACMD_LIST(gGatewayCmdList);
void GatewayItemAssignments_FinishEarly(Entity *pEnt, U32 uAssignmentID)
{
	ItemAssignment* pAssignment = ItemAssignment_EntityGetActiveAssignmentByID(pEnt, uAssignmentID);

	if(pAssignment)
		if(!gslItemAssignments_CompleteAssignment(pEnt,pAssignment,NULL,true,false))
		{
			gslItemAssignments_NotifySendFailure(pEnt, GET_REF(pAssignment->hDef), "ItemAssignments_UnableToRush");
		}
}

AUTO_COMMAND ACMD_NAME(GatewayItemAssignments_SlotItem) ACMD_ACCESSLEVEL(0) ACMD_LIST(gGatewayCmdList);
void GatewayItemAssignments_SlotItem(Entity *pEnt, S32 iAssignmentSlot, U64 uiID)
{
	BagIterator *iter = NULL;
	ItemAssignmentSlotUI* pSlotUI;
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	GatewaySession *pSession = wgsFindSessionForAccountId(pEnt->pPlayer->accountID);
	bool bFound = false;

	if(!pEnt)
		return;

	UseSessionItemAssignmentsCachedStruct(pEnt);

	pSlotUI = eaGet(&pIACache->eaSlots, iAssignmentSlot);

	if(!pSlotUI)
		return;

	if(uiID == 0)
	{
		ItemAssignmentsClearSlottedItem(pSlotUI);
		session_ContainerModified(pSession,GW_GLOBALTYPE_CRAFTING_DETAIL,REF_STRING_FROM_HANDLE(pSession->pItemAssignmentsCache->hCurrentDef));
		return;
	}

	iter = bagiterator_Create();

	bFound = inv_trh_FindItemByIDEx(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity, pEnt),iter, uiID, false, true);

	if(bFound && ItemAssignmentsCanSlotItem(pEnt, iAssignmentSlot, bagiterator_GetCurrentBagID(iter),iter->i_cur,pExtract))
	{
		Item *pItem = invbag_GetItem(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity, pEnt), bagiterator_GetCurrentBagID(iter), iter->i_cur, pExtract);

		if(pItem)
		{
			ItemAssignmentsSlotItemCheckSwap(pSession->lang, pEnt, pSlotUI, bagiterator_GetCurrentBagID(iter), iter->i_cur, pItem);
			session_ContainerModified(pSession, GW_GLOBALTYPE_CRAFTING_DETAIL, REF_STRING_FROM_HANDLE(pSession->pItemAssignmentsCache->hCurrentDef));
		}
	}

	bagiterator_Destroy(iter);
}

// End of File
