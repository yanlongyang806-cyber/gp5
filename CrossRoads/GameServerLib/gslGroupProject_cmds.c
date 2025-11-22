/***************************************************************************



***************************************************************************/

#include "gslGroupProject.h"
#include "GroupProjectCommon.h"
#include "Entity.h"
#include "Player.h"
#include "Guild.h"
#include "GameAccountDataCommon.h"
#include "ResourceInfo.h"
#include "StringCache.h"
#include "gslPartition.h"
#include "inventoryCommon.h"
#include "EntityLib.h"
#include "LoggedTransactions.h"
#include "GameStringFormat.h"
#include "NotifyEnum.h"
#include "NotifyCommon.h"
#include "Reward.h"
#include "SavedPetCommon.h"
#include "gslEventSend.h"
#include "strings_opt.h"

#include "AutoGen/Player_h_ast.h"
#include "AutoGen/GroupProjectCommon_h_ast.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/gslGroupProject_h_ast.h"

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void
gslGroupProject_ContributionNotify(ContributionNotifyData *notifyData)
{
    Entity *playerEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, notifyData->playerID);
    ClientCmd_gclGroupProject_ContributionNotify(playerEnt, notifyData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GroupProject_DebugGuildProjectInit(Entity *pEnt)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        RemoteCommand_aslGroupProject_DebugCreateAndInit(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, GLOBALTYPE_GUILD, pEnt->pPlayer->iGuildID);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GroupProject_ContributionReport(const char *projectName)
{
    GroupProjectDef *projectDef = RefSystem_ReferentFromString("GroupProjectDef", projectName);
    int i, j;

    if ( projectDef )
    {
        for ( i = eaSize(&projectDef->donationTaskDefs) - 1; i >= 0; i-- )
        {
            DonationTaskDef *taskDef = GET_REF(projectDef->donationTaskDefs[i]->taskDef);
            S32 contribution = 0;

            for ( j = eaSize(&taskDef->buckets) - 1; j >= 0; j-- )
            {
                GroupProjectConstant *constant;
                GroupProjectDonationRequirement *bucket = taskDef->buckets[j];

				constant = GroupProject_FindConstant(projectDef, bucket->contributionConstant);
				if ( constant )
				{
					contribution += ( bucket->count * constant->value );
				}
				else
                {
                    printf("contribution constant %s not found\n", bucket->contributionConstant);
                    continue;
                }
            }

            printf("%s, %d\n", taskDef->name, contribution);
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_GiveNumeric(Entity *pEnt, const char *projectName, const char *numericName, S32 value)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        RemoteCommand_aslGroupProject_GiveNumeric(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, projectName, numericName, value);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_GiveNumeric(Entity *pEnt, const char *projectName, const char *numericName, S32 value)
{
    if ( pEnt )
    {
        RemoteCommand_aslGroupProject_GiveNumeric(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), projectName, numericName, value);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_SetUnlock(Entity *pEnt, const char *projectName, const char *unlockName)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        RemoteCommand_aslGroupProject_SetUnlock(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, projectName, unlockName);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_SetUnlock(Entity *pEnt, const char *projectName, const char *unlockName)
{
    if ( pEnt )
    {
        RemoteCommand_aslGroupProject_SetUnlock(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), projectName, unlockName);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_ClearUnlock(Entity *pEnt, const char *projectName, const char *unlockName)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        RemoteCommand_aslGroupProject_ClearUnlock(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, projectName, unlockName);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_ClearUnlock(Entity *pEnt, const char *projectName, const char *unlockName)
{
    if ( pEnt )
    {
        RemoteCommand_aslGroupProject_ClearUnlock(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), projectName, unlockName);
    }
}

//Hey, make a wrapper before you check this in or you suck
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_SetNextTask(int iParitionIdx, Guild *pGuild, GroupProjectContainer *pProjectContainer, Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName)
{
    if ( pEnt && pGuild && guild_IsMember(pEnt) )
    {
		GuildMember *pMember = guild_FindMemberInGuild(pEnt, pGuild);
		GroupProjectState *projectState = pProjectContainer ? eaIndexedGetUsingString(&pProjectContainer->projectList, projectName) : NULL;
        DonationTaskDef *taskDef = RefSystem_ReferentFromString("DonationTaskDef", taskName);

        // Only set the task if the player has permission and the task allowed expression is true.
        if ( taskDef && pMember && 
             guild_HasPermission(pMember->iRank, pGuild, GuildPermission_GuildProjectManagement) &&
			 GuildProject_DonationTaskAllowed(iParitionIdx, pEnt, projectState, taskDef, taskSlotNum) )
        {
            RemoteCommand_aslGroupProject_SetNextTask(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, 
                GLOBALTYPE_GUILD, pEnt->pPlayer->pGuild->iGuildID, projectName, taskSlotNum, taskName);
        }
        else
        {
            //XXX - notify here
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_SetNextTask(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName)
{
    if ( pEnt )
    {
        DonationTaskDef *taskDef = RefSystem_ReferentFromString("DonationTaskDef", taskName);

        // Only set the task if the player has permission and the task allowed expression is true.
        if ( taskDef )
        {
            RemoteCommand_aslGroupProject_SetNextTask(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), 
                GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), projectName, taskSlotNum, taskName);
        }
        else
        {
            //XXX - notify here
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGuildProject_SetNextTask(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	GroupProjectContainer *pProjectContainer = GroupProject_ResolveContainer(pEnt, GroupProjectType_Guild);
	GuildProject_SetNextTask(entGetPartitionIdx(pEnt), pGuild, pProjectContainer, pEnt, projectName, taskSlotNum, taskName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_NAME(GuildProject_SetNextTask) ACMD_LIST(gGatewayCmdList);
void
gslGuildProject_SetNextTask_Gateway(Entity *pEnt, const char* guildID, const char *projectName, int taskSlotNum, const char *taskName)
{
	Guild *pGuild = RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD), guildID);
	GroupProjectContainer *pProjectContainer = RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD), guildID);
	GuildProject_SetNextTask(PARTITION_STATIC_CHECK, pGuild, pProjectContainer, pEnt, projectName, taskSlotNum, taskName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_SetNextTask(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName)
{
    PlayerProject_SetNextTask(pEnt, projectName, taskSlotNum, taskName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_NAME(PlayerProject_SetNextTask) ACMD_LIST(gGatewayCmdList);
void
gslPlayerProject_SetNextTask_Gateway(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName)
{
	PlayerProject_SetNextTask(pEnt, projectName, taskSlotNum, taskName);
}

static void
DonateSimpleItems_CB(TransactionReturnVal *returnVal, void *userData)
{

}

static void
SendDonationNoPermissionNotify(Entity *pEnt, const char *itemName, const char *projectName, const char *bucketName, S32 count, ContributionItemData **donationQueue)
{
    ContributionNotifyData notifyData = {0};
    notifyData.playerID = pEnt->myContainerID;
    notifyData.projectcontainerType = GLOBALTYPE_GROUPPROJECTCONTAINERGUILD;
    notifyData.projectContainerID = pEnt->pPlayer->pGuild->iGuildID;
    notifyData.donatedItemName = allocAddString(itemName);
    notifyData.requestedDonationCount = count;
    notifyData.donationCount = 0;
    notifyData.contributionNumericName = NULL;
    notifyData.contributionEarned = 0;
    notifyData.projectName = allocAddString(projectName);
    notifyData.taskName = NULL;
    notifyData.bucketName = allocAddString(bucketName);
    notifyData.bucketFilled = false;
    notifyData.taskFinalized = false;
    notifyData.noPermission = true;
    notifyData.requestedDonations = donationQueue;

    gslGroupProject_ContributionNotify(&notifyData);

    notifyData.requestedDonations = NULL;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_DonateSimpleItems(Entity *pEnt, Guild* pGuild, const char *projectName, int taskSlotNum, const char *bucketName, const char *itemName, S32 count)
{
    if ( guild_IsMember(pEnt) && pGuild )
    {
        GuildMember *pMember;
        if ( pGuild == NULL )
        {
            return;
        }

        pMember = guild_FindMemberInGuild(pEnt, pGuild);
        if ( pMember == NULL )
        {
            return;
        }

        if ( guild_HasPermission(pMember->iRank, pGuild, GuildPermission_DonateToProjects) )
        {
            TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValEnt("DonateSimpleItems", pEnt, DonateSimpleItems_CB, NULL);
            GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			ItemChangeReason reason = {0};

			inv_FillItemChangeReason(&reason, pEnt, "GroupProject:Donate", projectName);

            AutoTrans_GroupProject_tr_DonateSimpleItems(returnVal, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, pEnt->myContainerID, 
                GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pGuild->iContainerID, projectName, taskSlotNum, bucketName, itemName, count, &reason, pExtract);
        }
        else
        {
            SendDonationNoPermissionNotify(pEnt, itemName, projectName, bucketName, count, NULL);
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_DonateSimpleItems(Entity *pEnt, const char *projectName, int taskSlotNum, const char *bucketName, const char *itemName, S32 count)
{
    if ( pEnt )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValEnt("DonateSimpleItems", pEnt, DonateSimpleItems_CB, NULL);
        GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		ItemChangeReason reason = {0};

		inv_FillItemChangeReason(&reason, pEnt, "PlayerProject:Donate", projectName);

        AutoTrans_GroupProject_tr_DonateSimpleItems(returnVal, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, entGetContainerID(pEnt), 
            GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), projectName, taskSlotNum, bucketName, itemName, count, &reason, pExtract);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGuildProject_DonateSimpleItems(Entity *pEnt, const char *projectName, int taskSlotNum, const char *bucketName, const char *itemName, S32 count)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	GuildProject_DonateSimpleItems(pEnt, pGuild, projectName, taskSlotNum, bucketName, itemName, count);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(GuildProject_DonateSimpleItems) ACMD_LIST(gGatewayCmdList);
void
gslGuildProject_DonateSimpleItems_Gateway(Entity *pEnt, const char *guildID, const char *projectName, int taskSlotNum, const char *bucketName, const char *itemName, S32 count)
{
	Guild *pGuild = RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD), guildID);
	
	// Gateway sends down all values with the pItem->fScaleUI already applied, so undo that here. 
	ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, itemName);
	if (SAFE_MEMBER(pItemDef, fScaleUI))
		count /= pItemDef->fScaleUI;
	
	GuildProject_DonateSimpleItems(pEnt, pGuild, projectName, taskSlotNum, bucketName, itemName, count);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_DonateSimpleItems(Entity *pEnt, const char *projectName, int taskSlotNum, const char *bucketName, const char *itemName, S32 count)
{
	PlayerProject_DonateSimpleItems(pEnt, projectName, taskSlotNum, bucketName, itemName, count);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(PlayerProject_DonateSimpleItems) ACMD_LIST(gGatewayCmdList);
void
gslPlayerProject_DonateSimpleItems_Gateway(Entity *pEnt, const char *projectName, int taskSlotNum, const char *bucketName, const char *itemName, S32 count)
{
	// Gateway sends down all values with the pItem->fScaleUI already applied, so undo that here. 
	ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, itemName);
	if (SAFE_MEMBER(pItemDef, fScaleUI))
		count /= pItemDef->fScaleUI;

	PlayerProject_DonateSimpleItems(pEnt, projectName, taskSlotNum, bucketName, itemName, count);
}

static void
DonateExpressionItem_CB(TransactionReturnVal *returnVal, void *userData)
{

}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGuildProject_DonateExpressionItem(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName, const char *bucketName, const char *itemName, int bagID, int slotIdx, int count)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) && count > 0 )
    {
        Guild* pGuild;
        GuildMember *pMember;

        pGuild = guild_GetGuild(pEnt);
        if ( pGuild == NULL )
        {
            return;
        }

        pMember = guild_FindMemberInGuild(pEnt, pGuild);
        if ( pMember == NULL )
        {
            return;
        }

        if ( guild_HasPermission(pMember->iRank, pGuild, GuildPermission_DonateToProjects) )
        {
            GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
            Item *item;

            item = inv_GetItemFromBag(pEnt, bagID, slotIdx, pExtract);
            if ( item != NULL )
            {
                DonationTaskDef *taskDef;
                GroupProjectDonationRequirement *taskBucket;

                taskDef = RefSystem_ReferentFromString(g_DonationTaskDict, taskName);
                if ( taskDef != NULL )
                {
                    int donationRequireIndex = DonationTask_FindRequirement(taskDef, bucketName);
                    taskBucket = eaGet(&taskDef->buckets, donationRequireIndex);

                    if ( taskBucket != NULL )
                    {
                        if ( DonationTask_ItemMatchesExpressionRequirement(entGetPartitionIdx(pEnt), pEnt, taskBucket, item) )
                        {
                            TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValEnt("DonateExpressionItem", pEnt, DonateExpressionItem_CB, NULL);
							ItemChangeReason reason = {0};

							inv_FillItemChangeReason(&reason, pEnt, "GroupProject:Donate", projectName);
           
                            AutoTrans_GroupProject_tr_DonateExpressionItem(returnVal, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, pEnt->myContainerID, 
                                GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, projectName, taskSlotNum, taskName, bucketName, itemName, bagID, slotIdx, count, &reason, pExtract);
                        }
                    }
                }
            }
        }
        else
        {
            SendDonationNoPermissionNotify(pEnt, itemName, projectName, bucketName, count, NULL);
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_DonateExpressionItem(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName, const char *bucketName, const char *itemName, int bagID, int slotIdx, int count)
{
    if ( pEnt && count > 0 )
    {
        GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
        Item *item;

        item = inv_GetItemFromBag(pEnt, bagID, slotIdx, pExtract);
        if ( item != NULL )
        {
            DonationTaskDef *taskDef;
            GroupProjectDonationRequirement *taskBucket;

            taskDef = RefSystem_ReferentFromString(g_DonationTaskDict, taskName);
            if ( taskDef != NULL )
            {
                int donationRequireIndex = DonationTask_FindRequirement(taskDef, bucketName);
                taskBucket = eaGet(&taskDef->buckets, donationRequireIndex);

                if ( taskBucket != NULL )
                {
                    if ( DonationTask_ItemMatchesExpressionRequirement(entGetPartitionIdx(pEnt), pEnt, taskBucket, item) )
                    {
                        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValEnt("DonateExpressionItem", pEnt, DonateExpressionItem_CB, NULL);
						ItemChangeReason reason = {0};

						inv_FillItemChangeReason(&reason, pEnt, "PlayerProject:Donate", projectName);

                        AutoTrans_GroupProject_tr_DonateExpressionItem(returnVal, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, entGetContainerID(pEnt), 
                            GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), projectName, taskSlotNum, taskName, bucketName, itemName, bagID, slotIdx, count, &reason, pExtract);
                    }
                }
            }

        }
    }
}

void gslGuildProject_DonateExpressionItemList_Internal(int iPartitionIndex, Entity *pEnt, Guild* pGuild, const char *projectName, int taskSlotNum, const char *taskName, const char *bucketName, ContributionItemList *donationQueue)
{
	if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) && pGuild && donationQueue && eaSize(&donationQueue->items) > 0 )
	{
		GuildMember *pMember = guild_FindMemberInGuild(pEnt, pGuild);
		if ( pMember == NULL )
		{
			return;
		}

		if ( guild_HasPermission(pMember->iRank, pGuild, GuildPermission_DonateToProjects) )
		{
			GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			DonationTaskDef *taskDef = RefSystem_ReferentFromString(g_DonationTaskDict, taskName);
			GroupProjectDonationRequirement *taskBucket;
			int i, donationRequireIndex;
			TransactionReturnVal *returnVal;
			ItemChangeReason reason = {0};

			if ( taskDef == NULL )
			{
				return;
			}

			donationRequireIndex = DonationTask_FindRequirement(taskDef, bucketName);
			taskBucket = eaGet(&taskDef->buckets, donationRequireIndex);
			if ( taskBucket == NULL )
			{
				return;
			}

			for ( i = eaSize(&donationQueue->items) - 1; i >= 0; i-- )
			{
				Item *item;
				ContributionItemData *donation = donationQueue->items[i];

				if ( donation == NULL || donation->count <= 0 )
				{
					return;
				}

				item = inv_GetItemFromBag(pEnt, donation->bagID, donation->slotIdx, pExtract);
				if ( item == NULL )
				{
					return;
				}


				if ( !DonationTask_ItemMatchesExpressionRequirement(iPartitionIndex, pEnt, taskBucket, item) )
				{
					return;
				}
			}

			returnVal = LoggedTransactions_CreateManagedReturnValEnt("DonateExpressionItemList", pEnt, DonateExpressionItem_CB, NULL);

			inv_FillItemChangeReason(&reason, pEnt, "GroupProject:DonateItemList", projectName);

			AutoTrans_GroupProject_tr_DonateExpressionItemList(returnVal, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, pEnt->myContainerID, 
				GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, projectName, taskSlotNum, taskName, bucketName, 
				donationQueue, &reason, pExtract);
		}
		else
		{
			SendDonationNoPermissionNotify(pEnt, NULL, projectName, bucketName, 0, donationQueue->items);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGuildProject_DonateExpressionItemList(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName, const char *bucketName, ContributionItemList *donationQueue)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt))
    {
        Guild* pGuild = guild_GetGuild(pEnt);
        if (pGuild)
		{
			gslGuildProject_DonateExpressionItemList_Internal(entGetPartitionIdx(pEnt), pEnt, pGuild, projectName, taskSlotNum, taskName, bucketName, donationQueue);
        }
	}
}

void gslContributionItemList_FillQueue(ContributionItemList *pDonationQueue, const char *pchDonationString)
{
	const char *str;
	char *context = NULL;
	char *estrTemp = NULL;
	estrCopy2(&estrTemp, pchDonationString);
	str = strtok_s(estrTemp, ",", &context);
	while (str)
	{
		ContributionItemData *pData;
		ContributionItemData data;

		data.itemName = allocAddString(str);

		if ((str = strtok_s(NULL, ",", &context)) == NULL)
			break;
		data.bagID = atoi(str);

		if ((str = strtok_s(NULL, ",", &context)) == NULL)
			break;
		data.slotIdx = atoi(str);

		if ((str = strtok_s(NULL, ",", &context)) == NULL)
			break;
		data.count = atoi(str);

		pData = StructClone(parse_ContributionItemData, &data);
		eaPush(&pDonationQueue->items, pData);

		str = strtok_s(NULL, ",", &context);
	}
	estrDestroy(&estrTemp);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(GuildProject_DonateExpressionItemList) ACMD_LIST(gGatewayCmdList);
void
aslGuildProject_DonateExpressionItemList_Gateway(Entity *pEnt, const char* guildID, const char *projectName, int taskSlotNum, const char *taskName, const char *bucketName, const char *pchDonationString)
{
	Guild *pGuild = RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD), guildID);
	if (pGuild)
	{
		ContributionItemList donationQueue;
		StructInit(parse_ContributionItemList, &donationQueue);
		gslContributionItemList_FillQueue(&donationQueue, pchDonationString);
		gslGuildProject_DonateExpressionItemList_Internal(PARTITION_STATIC_CHECK, pEnt, pGuild, projectName, taskSlotNum, taskName, bucketName, &donationQueue);
		eaDestroyStruct(&donationQueue.items, parse_ContributionItemData);
	}
}

void
gslPlayerProject_DonateExpressionItemList_Internal(int iPartitionIndex, Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName, const char *bucketName, ContributionItemList *donationQueue)
{
    if ( pEnt && donationQueue && eaSize(&donationQueue->items) > 0 )
    {
        GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
        DonationTaskDef *taskDef = RefSystem_ReferentFromString(g_DonationTaskDict, taskName);
        GroupProjectDonationRequirement *taskBucket;
        int i, donationRequireIndex;
        TransactionReturnVal *returnVal;
		ItemChangeReason reason = {0};

        if ( taskDef == NULL )
        {
            return;
        }

        donationRequireIndex = DonationTask_FindRequirement(taskDef, bucketName);
        taskBucket = eaGet(&taskDef->buckets, donationRequireIndex);
        if ( taskBucket == NULL )
        {
            return;
        }

        for ( i = eaSize(&donationQueue->items) - 1; i >= 0; i-- )
        {
            Item *item;
            ContributionItemData *donation = donationQueue->items[i];

            if ( donation == NULL || donation->count <= 0 )
            {
                return;
            }

            item = inv_GetItemFromBag(pEnt, donation->bagID, donation->slotIdx, pExtract);
            if ( item == NULL )
            {
                return;
            }


            if ( !DonationTask_ItemMatchesExpressionRequirement(iPartitionIndex, pEnt, taskBucket, item) )
            {
                return;
            }
        }

        returnVal = LoggedTransactions_CreateManagedReturnValEnt("DonateExpressionItemList", pEnt, DonateExpressionItem_CB, NULL);

		inv_FillItemChangeReason(&reason, pEnt, "PlayerProject:DonateItemList", projectName);

        AutoTrans_GroupProject_tr_DonateExpressionItemList(returnVal, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, entGetContainerID(pEnt), 
            GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), projectName, taskSlotNum, taskName, bucketName, 
            donationQueue, &reason, pExtract);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_DonateExpressionItemList(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName, const char *bucketName, ContributionItemList *donationQueue)
{
	if (pEnt)
	{
		gslPlayerProject_DonateExpressionItemList_Internal(entGetPartitionIdx(pEnt), pEnt, projectName, taskSlotNum, taskName, bucketName, donationQueue);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(PlayerProject_DonateExpressionItemList) ACMD_LIST(gGatewayCmdList);
void
gslPlayerProject_DonateExpressionItemList_Gateway(Entity *pEnt, const char *projectName, int taskSlotNum, const char *taskName, const char *bucketName, const char *pchDonationString)
{
	ContributionItemList donationQueue;
	StructInit(parse_ContributionItemList, &donationQueue);
	gslContributionItemList_FillQueue(&donationQueue, pchDonationString);
	gslPlayerProject_DonateExpressionItemList_Internal(PARTITION_STATIC_CHECK, pEnt, projectName, taskSlotNum, taskName, bucketName, &donationQueue);
	eaDestroyStruct(&donationQueue.items, parse_ContributionItemData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGroupProject_RequestProjectDefsByType(Entity *pEnt, /* GroupProjectType */ int projectType)
{
    EARRAY_OF(GroupProjectDefRef) returnDefs = NULL;
    ResourceIterator iter;
    GroupProjectDef *projectDef;

    if ( !resInitIterator(g_GroupProjectDict, &iter) )
    {
        return;
    }

    // Iterate all GroupProjectDefs and find the ones that match the given type.
    while ( resIteratorGetNext(&iter, NULL, &projectDef) )
    {
        if ( projectDef->type == projectType )
        {
            GroupProjectDefRef *projectDefRef = StructCreate(parse_GroupProjectDefRef);
            SET_HANDLE_FROM_REFERENT(g_GroupProjectDict, projectDef, projectDefRef->projectDef);
            eaPush(&returnDefs, projectDefRef);
        }
    }
	resFreeIterator(&iter);

    // If any matching projects were found, send them to the client.
    if ( eaSize(&returnDefs) > 0 )
    {
        GroupProjectDefs *projectDefs = StructCreate(parse_GroupProjectDefs);
        projectDefs->projectDefs = returnDefs;
        ClientCmd_gclGroupProject_ReceiveProjectDefsByType(pEnt, projectType, projectDefs);
        StructDestroy(parse_GroupProjectDefs, projectDefs);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGroupProject_SubscribeToGuildProject(Entity *pEnt)
{
    GroupProject_SubscribeToGuildProject(pEnt);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGroupProject_SubscribeToPlayerProject(Entity *pEnt)
{
    GroupProject_SubscribeToPlayerProjectContainer(pEnt);
}

// This is a debug command that allows a developer to override the guild allegiance on a guild owned map.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_OverrideMapAllegiance(Entity *pEnt, const char *overrideAllegiance)
{
    GroupProjectMapPartitionState *pPartitionState = gslGroupProject_GetGroupProjectMapPartitionState(entGetPartitionIdx(pEnt));

    if ( pPartitionState != NULL )
    {
        pPartitionState->overrideAllegiance = allocAddString(overrideAllegiance);
    }
}

// This is a debug command that allows a designer to query the values of the Group Project Numerics associated with the guild owner of their current map.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
const char *
GuildProject_GetProjectNumeric(Entity *pEnt, const char *projectName, const char *numericName)
{
    GroupProjectState *projectState;
    GroupProjectNumericData *numericData;

    static char *estrBuf = NULL;

    estrClear(&estrBuf);

    // Find the project state.
    projectState = gslGroupProject_GetGroupProjectStateForMap(GroupProjectType_Guild, projectName, entGetPartitionIdx(pEnt));
    if ( projectState == NULL )
    {
        estrPrintf(&estrBuf, "GroupProjectState %s not found", projectName);
    }
    else
    {
        // Find the numeric.
        numericData = eaIndexedGetUsingString(&projectState->numericData, numericName);
        if ( numericData == NULL )
        {
            GroupProjectDef *projectDef = GET_REF(projectState->projectDef);
            if ( projectDef )
            {
                if ( eaIndexedGetUsingString(&projectDef->validNumerics, numericName) )
                {
                    // The numeric is valid, so return 0 which is the default value.
                    estrPrintf(&estrBuf, "0");
                    return estrBuf;
                }
            }
            estrPrintf(&estrBuf, "GroupProjectNumericData %s not found", numericName);
        }
        else
        {
            estrPrintf(&estrBuf, "%d", numericData->numericVal);
        }
    }

    return estrBuf;
}

static void
FillBucket_CB(TransactionReturnVal *returnVal, void *cbData)
{

}

// This is a debug command that that instantly fills a donation task bucket.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_FillBucket(Entity *pEnt, const char *projectName, int taskSlotNum, const char *bucketName)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("FillBucket", GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, 
            pEnt->pPlayer->pGuild->iGuildID, FillBucket_CB, NULL);

        AutoTrans_GroupProject_tr_ForceFillBucket(returnVal, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, 
            pEnt->pPlayer->pGuild->iGuildID, projectName, taskSlotNum, bucketName);
    }
}

// This is a debug command that that instantly fills a donation task bucket.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_FillBucket(Entity *pEnt, const char *projectName, int taskSlotNum, const char *bucketName)
{
    if ( pEnt )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("FillBucket", GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, 
            entGetContainerID(pEnt), FillBucket_CB, NULL);

        AutoTrans_GroupProject_tr_ForceFillBucket(returnVal, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, 
            entGetContainerID(pEnt), projectName, taskSlotNum, bucketName);
    }
}

// This is a debug command that that instantly fills a donation task bucket.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_FillAllBuckets(Entity *pEnt)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("FillAllBuckets", GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, 
            pEnt->pPlayer->pGuild->iGuildID, FillBucket_CB, NULL);

        AutoTrans_GroupProject_tr_ForceFillAllBuckets(returnVal, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, 
            pEnt->pPlayer->pGuild->iGuildID);
    }
}

// This is a debug command that that instantly fills a donation task bucket.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_FillAllBuckets(Entity *pEnt)
{
    if ( pEnt )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("FillAllBuckets", GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, 
            entGetContainerID(pEnt), FillBucket_CB, NULL);

        AutoTrans_GroupProject_tr_ForceFillAllBuckets(returnVal, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, 
            entGetContainerID(pEnt));
    }
}

// This is a debug command that will set the filled amount of a donation task bucket.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_SetBucketFill(Entity *pEnt, const char *projectName, int taskSlotNum, const char *bucketName, int count)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("SetBucketFill", GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, 
            pEnt->pPlayer->pGuild->iGuildID, FillBucket_CB, NULL);

        AutoTrans_GroupProject_tr_ForceSetBucketFill(returnVal, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, 
            pEnt->pPlayer->pGuild->iGuildID, projectName, taskSlotNum, bucketName, count);
    }
}

// This is a debug command that will set the filled amount of a donation task bucket.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_SetBucketFill(Entity *pEnt, const char *projectName, int taskSlotNum, const char *bucketName, int count)
{
    if ( pEnt )
    {
        TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValObj("SetBucketFill", GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, 
            entGetContainerID(pEnt), FillBucket_CB, NULL);

        AutoTrans_GroupProject_tr_ForceSetBucketFill(returnVal, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, 
            entGetContainerID(pEnt), projectName, taskSlotNum, bucketName, count);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(GroupProject);
void
gslGroupProject_GetMapState(Entity *pEnt)
{
    if (pEnt)
    {
        int iPartitionIdx = entGetPartitionIdx(pEnt);
        GlobalType eOwnerType = partition_OwnerTypeFromIdx(iPartitionIdx);
        ContainerID iOwnerID = partition_OwnerIDFromIdx(iPartitionIdx);
        const char *pchAllegiance = NULL;

        if (eOwnerType == GLOBALTYPE_GUILD)
            pchAllegiance = gslGroupProject_GetGuildAllegianceForGroupProjectMap(iPartitionIdx);

        ClientCmd_gclGroupProject_SetMapState(pEnt, pchAllegiance, eOwnerType, iOwnerID);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_SetProjectMessage(Entity *pEnt, const char *projectName, ACMD_SENTENCE projectMessage)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        Guild* pGuild = guild_GetGuild(pEnt);
        GuildMember *pMember = guild_FindMemberInGuild(pEnt, pGuild);

        // Only set the message if the player has permission.
        if ( guild_HasPermission(pMember->iRank, pGuild, GuildPermission_GuildProjectManagement) )
        {
            RemoteCommand_aslGroupProject_SetProjectMessage(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, 
                GLOBALTYPE_GUILD, pEnt->pPlayer->pGuild->iGuildID, projectName, projectMessage);
        }
        else
        {
            //XXX - notify here
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGuildProject_SetProjectMessage(Entity *pEnt, const char *projectName, ACMD_SENTENCE projectMessage)
{
    GuildProject_SetProjectMessage(pEnt, projectName, projectMessage);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_SetProjectPlayerName(Entity *pEnt, const char *projectName, ACMD_SENTENCE projectPlayerName)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        Guild* pGuild = guild_GetGuild(pEnt);
        GuildMember *pMember = guild_FindMemberInGuild(pEnt, pGuild);

        // Only set the name if the player has permission.
        if ( guild_HasPermission(pMember->iRank, pGuild, GuildPermission_GuildProjectManagement) )
        {
            RemoteCommand_aslGroupProject_SetProjectPlayerName(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, 
                GLOBALTYPE_GUILD, pEnt->pPlayer->pGuild->iGuildID, projectName, projectPlayerName);
        }
        else
        {
            //XXX - notify here
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGuildProject_SetProjectPlayerName(Entity *pEnt, const char *projectName, ACMD_SENTENCE projectPlayerName)
{
    GuildProject_SetProjectPlayerName(pEnt, projectName, projectPlayerName);
}

static void
DumpProjectState(char **hStrReturn, GroupProjectState *projectState)
{
    int i;
    estrAppend2(hStrReturn, "Numerics:\n");
    for ( i = eaSize(&projectState->numericData) - 1; i >= 0; i-- )
    {
        GroupProjectNumericData *numericData = projectState->numericData[i];
        GroupProjectNumericDef *numericDef = GET_REF(numericData->numericDef);

        estrConcatf(hStrReturn, "  %s: %d\n", numericDef->name, numericData->numericVal);
    }
    estrAppend2(hStrReturn, "Unlocks:\n");
    for ( i = eaSize(&projectState->unlocks) - 1; i >= 0; i-- )
    {
        GroupProjectUnlockDefRefContainer *unlockDefRef = projectState->unlocks[i];
        GroupProjectUnlockDef *unlockDef = GET_REF(unlockDefRef->unlockDef);
        estrConcatf(hStrReturn, "  %s\n", unlockDef->name);
    }
    return;
}

// This is a debug command that that prints the numerics and unlocks from the guild projects associated with the current map.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject) ACMD_NAME(DumpGuildProjectStateForMap);
char *
GroupProject_DumpGuildProjectStateForMap(Entity *pEnt, const char *projectName)
{
    static char *s_estrReturn = NULL;

    GroupProjectMapPartitionState *pPartitionState = gslGroupProject_GetGroupProjectMapPartitionState(entGetPartitionIdx(pEnt));

    if ( pPartitionState != NULL )
    {
        GroupProjectContainer *projectContainer = GET_REF(pPartitionState->guildProjectContainerRef);

        if ( projectContainer )
        {
            GroupProjectState *projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);

            estrClear(&s_estrReturn);

            if ( projectState )
            {
                DumpProjectState(&s_estrReturn, projectState);
            }
        }
    }

    return(s_estrReturn);
}

// This is a debug command that that prints the numerics and unlocks from the guild project associated with the player.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject) ACMD_NAME(DumpGuildProjectStateForPlayer);
char *
GroupProject_DumpGuildProjectStateForPlayer(Entity *pEnt, const char *projectName)
{
    static char *s_estrReturn = NULL;
    GroupProjectContainer *projectContainer;

    GroupProject_ValidateContainer(pEnt, GroupProjectType_Guild);
    projectContainer = GroupProject_ResolveContainer(pEnt, GroupProjectType_Guild);

    if ( projectContainer )
    {
        GroupProjectState *projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);

        estrClear(&s_estrReturn);

        DumpProjectState(&s_estrReturn, projectState);
    }

    return(s_estrReturn);
}

// This is a debug command that that prints the numerics and unlocks from the player project associated with the player.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject) ACMD_NAME(DumpPlayerProjectState);
char *
GroupProject_DumpPlayerProjectState(Entity *pEnt, const char *projectName)
{
    static char *s_estrReturn = NULL;
    GroupProjectContainer *projectContainer;

    GroupProject_ValidateContainer(pEnt, GroupProjectType_Player);
    projectContainer = GroupProject_ResolveContainer(pEnt, GroupProjectType_Player);

    if ( projectContainer )
    {
        GroupProjectState *projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);

        estrClear(&s_estrReturn);

        DumpProjectState(&s_estrReturn, projectState);
    }

    return(s_estrReturn);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_ClaimReward(Entity *pEnt, const char *projectName, int taskSlotNum)
{
    GroupProjectState *projectState;
    DonationTaskSlot *taskSlot;
    DonationTaskDef *taskDef;
    GroupProjectContainer *projectContainer;
    RewardTable *rewardTable;
    InventoryBag** rewardBags = NULL;
    GiveRewardBagsData Rewards = {0};
    ItemChangeReason reason = {0};
    U32* eaPets = NULL;

    if ( pEnt && pEnt->pPlayer )
    {
        projectContainer = GET_REF(pEnt->pPlayer->hGroupProjectContainer);
        if ( projectContainer == NULL )
        {
            return;
        }

        // Find the project state.
        projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
        if ( projectState == NULL )
        {
            return;
        }

        // Get the task slot.
        taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, taskSlotNum);
        if ( taskSlot == NULL )
        {
            return;
        }

        // Make sure the task is in the correct state to collect rewards.
        if ( taskSlot->state != DonationTaskState_RewardPending )
        {
            return;
        }

        // Get the task def.
        taskDef = GET_REF(taskSlot->taskDef);
        if ( taskDef == NULL )
        {
            return;
        }

        // Get reward table for task.
        rewardTable = GET_REF(taskDef->completionRewardTable);
        if ( rewardTable == NULL )
        {
            return;
        }

        gslGuildProject_ClaimRewardTable(pEnt, rewardTable, projectName, taskDef, taskSlotNum);
    }
}

AUTO_COMMAND_REMOTE ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_ClaimFinalizeRewards(ContainerID entContainerID, const char *projectName, int taskSlotNum)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	GroupProjectState *projectState;
	DonationTaskSlot *taskSlot;
	DonationTaskDef *taskDef;
	GroupProjectContainer *projectContainer;
	RewardTable *rewardTable;
	InventoryBag** rewardBags = NULL;
	GiveRewardBagsData Rewards = {0};
	ItemChangeReason reason = {0};
	U32* eaPets = NULL;

	if ( pEnt && pEnt->pPlayer )
	{
		projectContainer = GET_REF(pEnt->pPlayer->hGroupProjectContainer);
		if ( projectContainer == NULL )
		{
			return;
		}

		// Find the project state.
		projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
		if ( projectState == NULL )
		{
			return;
		}

		// Get the task slot.
		taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, taskSlotNum);
		if ( taskSlot == NULL )
		{
			return;
		}

		// Make sure the task is in the correct state to collect rewards.
		if ( taskSlot->state != DonationTaskState_FinalizedRewardPending )
		{
			return;
		}

		// Get the task def.
		taskDef = GET_REF(taskSlot->taskDef);
		if ( taskDef == NULL )
		{
			return;
		}

		// Get reward table for task.
		rewardTable = GET_REF(taskDef->finalizationRewardTable);
		if ( rewardTable == NULL )
		{
			return;
		}

		gslGuildProject_ClaimRewardTable(pEnt, rewardTable, projectName, taskDef, taskSlotNum);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_ClaimReward(Entity *pEnt, const char *projectName, int taskSlotNum)
{
    PlayerProject_ClaimReward(pEnt, projectName, taskSlotNum);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_GetRewardBags(Entity *pEnt, const char *projectName, const char *taskName)
{
    GroupProjectDef *projectDef = NULL;
    GroupProjectState *projectState;
    DonationTaskDef *taskDef;
    DonationTaskDefRef *taskDefRef;
    GroupProjectContainer *projectContainer;
    RewardTable *rewardTable;
    InventoryBag** rewardBags = NULL;
    InvRewardRequest *rewardRequest = NULL;

    if ( pEnt && pEnt->pPlayer )
    {
        projectContainer = GET_REF(pEnt->pPlayer->hGroupProjectContainer);
        if ( projectContainer != NULL )
        {
			// Find the project state.
			projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
			if ( projectState == NULL )
			{
				return;
			}

			// Get the project def
			projectDef = GET_REF(projectState->projectDef);
		}

		if ( projectDef == NULL )
		{
			// Get the project def from the dictionary
			projectDef = RefSystem_ReferentFromString("GroupProjectDef", projectName);
		}

		if ( projectDef == NULL )
		{
			// If we still can't find the projectdef, give up.
			return;
		}

		// Only allow any of this for Player projects
		if ( projectDef->type != GroupProjectType_Player )
		{
			return;
		}

		// Find the task def
		taskDefRef = eaIndexedGetUsingString(&projectDef->donationTaskDefs, taskName);
		if ( taskDefRef == NULL )
		{
			return;
		}

		// Get the task def.
		taskDef = GET_REF(taskDefRef->taskDef);

        if ( taskDef == NULL )
        {
            return;
        }

        // Get reward table for task.
        rewardTable = GET_REF(taskDef->completionRewardTable);
        if ( rewardTable != NULL )
        {
            // Generate rewards.
            reward_GenerateBagsForPersonalProject(entGetPartitionIdx(pEnt), pEnt, rewardTable, entity_GetSavedExpLevel(pEnt), &rewardBags);

            // Fill the reward request
            if ( eaSize(&rewardBags) > 0 )
            {
                rewardRequest = StructCreate(parse_InvRewardRequest);
                inv_FillRewardRequest(rewardBags, rewardRequest);
            }
        }

		// Send the rewards to the client
		ClientCmd_gclGroupProject_ReceiveRewards(pEnt, projectName, taskName, rewardRequest);

		// destroy rewardrequest otherwise we will leak memory
		if(rewardRequest)
		{
			StructDestroy(parse_InvRewardRequest, rewardRequest);
		}

		if(rewardBags)
		{
			// Destroy the reward bags as the above function never attaches them to rewardRequest. If it ever does this will crash
			eaDestroyStruct(&rewardBags, parse_InventoryBag);
		}
    }
}

// This is a command intended to be called from the GroupProject server. It's role is to fire a GameEvent when a task completes
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void
gslGroupProject_TaskCompleteCB(ContainerID entID, const char *projectName)
{
    Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entID);
    if (pEnt && projectName)
    {
        eventsend_RecordGroupProjectTaskComplete(entGetPartitionIdx(pEnt), pEnt, projectName);
    }
}

// This is a command intended to be called from the GroupProject server.  It is used to notify the gameserver that a group project unlock has
//  been granted to the player.  
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void
gslGroupProject_PlayerUnlockGrantedNotify(ContainerID entID, const char *projectName, const char *unlockName)
{
    const char *pooledProjectName = allocAddString(projectName);
    const char *pooledUnlockName = allocAddString(unlockName);
    int i;
    bool found = false;
    GroupProjectRecentUnlock *recentUnlock;

    Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entID);
    if (pEnt && pEnt->pPlayer && projectName && unlockName)
    {
        if ( gDebugUnlockNotify )
        {
            printf("Got player unlock granted notify: ContainerID=%d, ProjectName=%s, UnlockName=%s\n", entID, projectName, unlockName);
        }

        // See if the unlock is already in the recent unlocks list.
        for ( i = eaSize(&pEnt->pPlayer->eaRecentUnlocks) - 1; i >= 0; i-- )
        {
            recentUnlock = pEnt->pPlayer->eaRecentUnlocks[i];
            if ( recentUnlock->projectName == pooledProjectName && recentUnlock->unlockName == pooledUnlockName)
            {
                found = true;
                break;
            }
        }

        if ( !found )
        {
            // Unlock was not found in the recent unlock list, so add it.
            recentUnlock = StructCreate(parse_GroupProjectRecentUnlock);
            recentUnlock->projectName = pooledProjectName;
            recentUnlock->unlockName = pooledUnlockName;
            recentUnlock->time = timeSecondsSince2000();

            eaPush(&pEnt->pPlayer->eaRecentUnlocks, recentUnlock);
        }
    }
}

static void
CancelProject_CB(TransactionReturnVal *returnVal, void *userData)
{

}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_CancelProject(Entity *pEnt, const char *projectName, int taskSlotNum)
{
    GroupProjectState *projectState;
    DonationTaskSlot *taskSlot;
    DonationTaskDef *taskDef;
    GroupProjectContainer *projectContainer;
    TransactionReturnVal* pReturn;

    if ( pEnt && pEnt->pPlayer )
    {
        projectContainer = GET_REF(pEnt->pPlayer->hGroupProjectContainer);
        if ( projectContainer == NULL )
        {
            return;
        }

        // Find the project state.
        projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
        if ( projectState == NULL )
        {
            return;
        }

        // Get the task slot.
        taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, taskSlotNum);
        if ( taskSlot == NULL )
        {
            return;
        }

        // Make sure the task is in the correct state to cancel it.
        if ( taskSlot->state != DonationTaskState_AcceptingDonations )
        {
            return;
        }

        // Get the task def.
        taskDef = GET_REF(taskSlot->taskDef);
        if ( taskDef == NULL )
        {
            return;
        }

        if ( !taskDef->cancelable )
        {
            return;
        }

        pReturn = LoggedTransactions_CreateManagedReturnValObj("PlayerProject_CancelProject", GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), CancelProject_CB, NULL);

        AutoTrans_GroupProject_tr_CancelProject(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt),
            projectName, taskSlotNum);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_CancelProject(Entity *pEnt, const char *projectName, int taskSlotNum)
{
    PlayerProject_CancelProject(pEnt, projectName, taskSlotNum);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_CancelNextProject(Entity *pEnt, const char *projectName, int taskSlotNum)
{
    GroupProjectState *projectState;
    DonationTaskSlot *taskSlot;
    DonationTaskDef *taskDef;
    GroupProjectContainer *projectContainer;
    TransactionReturnVal* pReturn;

    if ( pEnt && pEnt->pPlayer )
    {
        projectContainer = GET_REF(pEnt->pPlayer->hGroupProjectContainer);
        if ( projectContainer == NULL )
        {
            return;
        }

        // Find the project state.
        projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
        if ( projectState == NULL )
        {
            return;
        }

        // Get the task slot.
        taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, taskSlotNum);
        if ( taskSlot == NULL )
        {
            return;
        }

        // Make sure the task is in the correct state to cancel it.
        if ( taskSlot->state != DonationTaskState_AcceptingDonations )
        {
            return;
        }

        // Get the task def.
        taskDef = GET_REF(taskSlot->nextTaskDef);
        if ( taskDef == NULL )
        {
            return;
        }

        pReturn = LoggedTransactions_CreateManagedReturnValObj("PlayerProject_CancelNextProject", GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), CancelProject_CB, NULL);

        AutoTrans_GroupProject_tr_CancelNextProject(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt),
            projectName, taskSlotNum);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_CancelNextProject(Entity *pEnt, const char *projectName, int taskSlotNum)
{
    PlayerProject_CancelNextProject(pEnt, projectName, taskSlotNum);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_CancelProject(Entity *pEnt, const char *projectName, int taskSlotNum)
{
    Guild *pGuild;
    GuildMember *pMember;
    TransactionReturnVal* pReturn;

    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        pGuild = guild_GetGuild(pEnt);
        if ( pGuild == NULL )
        {
            return;
        }

        pMember = guild_FindMemberInGuild(pEnt, pGuild);
        if ( pMember == NULL )
        {
            return;
        }

        if ( pMember->iRank < (eaSize(&pGuild->eaRanks) - 1) )
        {
            return;
        }

        pReturn = LoggedTransactions_CreateManagedReturnValObj("GuildProject_CancelProject", GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, CancelProject_CB, NULL);

        AutoTrans_GroupProject_tr_CancelProject(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID,
            projectName, taskSlotNum);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGuildProject_CancelProject(Entity *pEnt, const char *projectName, int taskSlotNum)
{
    GuildProject_CancelProject(pEnt, projectName, taskSlotNum);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
GuildProject_CancelNextProject(Entity *pEnt, const char *projectName, int taskSlotNum)
{
    Guild *pGuild;
    GuildMember *pMember;
    TransactionReturnVal* pReturn;

    if ( pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild && guild_IsMember(pEnt) )
    {
        pGuild = guild_GetGuild(pEnt);
        if ( pGuild == NULL )
        {
            return;
        }

        pMember = guild_FindMemberInGuild(pEnt, pGuild);
        if ( pMember == NULL )
        {
            return;
        }

        if ( pMember->iRank < (eaSize(&pGuild->eaRanks) - 1) )
        {
            return;
        }

        pReturn = LoggedTransactions_CreateManagedReturnValObj("GuildProject_CancelProject", GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID, CancelProject_CB, NULL);

        AutoTrans_GroupProject_tr_CancelNextProject(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, pEnt->pPlayer->pGuild->iGuildID,
            projectName, taskSlotNum);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslGuildProject_CancelNextProject(Entity *pEnt, const char *projectName, int taskSlotNum)
{
    GuildProject_CancelNextProject(pEnt, projectName, taskSlotNum);
}

static EARRAY_OF(GroupProjectSetTaskAndDonateAllState) s_DonateAllStates = NULL;

static void
PushDonateAllState(GroupProjectSetTaskAndDonateAllState *state)
{
    if ( s_DonateAllStates == NULL )
    {
        eaIndexedEnable(&s_DonateAllStates, parse_GroupProjectSetTaskAndDonateAllState);
    }

    eaIndexedAdd(&s_DonateAllStates, state);
}

static GroupProjectSetTaskAndDonateAllState *
GetDonateAllState(ContainerID playerID)
{
    return eaIndexedGetUsingInt(&s_DonateAllStates, playerID);
}

static void
DonateAllItems_CB(TransactionReturnVal *returnVal, void *userData)
{

}

static void
SetTaskAndDonateAllItems(Entity *pEnt, const char *projectName, const char *taskName, int taskSlotNum)
{
    TransactionReturnVal *returnVal = LoggedTransactions_CreateManagedReturnValEnt("SetTaskAndDonateAllItems", pEnt, DonateAllItems_CB, NULL);
    GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
    ItemChangeReason reason = {0};

    inv_FillItemChangeReason(&reason, pEnt, "PlayerProject:SetTaskAndDonateAllItems", projectName);

    AutoTrans_GroupProject_tr_SetTaskAndDonateAllItems(returnVal, GLOBALTYPE_GAMESERVER, pEnt->myEntityType, entGetContainerID(pEnt), 
        GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, entGetContainerID(pEnt), projectName, taskName, taskSlotNum, &reason, pExtract);
}

void
PlayerProject_ProcessPendingDonateAllStates(void)
{
    int i;
    for ( i = eaSize(&s_DonateAllStates) - 1; i >= 0; i-- )
    {
        Entity *playerEnt;
        GroupProjectSetTaskAndDonateAllState *state = s_DonateAllStates[i];
        GroupProjectContainer *projectContainer;

        playerEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, state->playerID);
        if ( playerEnt == NULL )
        {
            // Player is gone, so remove the state.
            eaRemove(&s_DonateAllStates, i);
            StructDestroy(parse_GroupProjectSetTaskAndDonateAllState, state);
            continue;
        }

        projectContainer = GET_REF(playerEnt->pPlayer->hGroupProjectContainer);
        if ( projectContainer != NULL )
        {
            // Project container now exists.  Remove it from the pending states array.
            eaRemove(&s_DonateAllStates, i);

            // Do the operation.
            SetTaskAndDonateAllItems(playerEnt, state->projectName, state->taskName, state->taskSlotNum);

            // Free the state.
            StructDestroy(parse_GroupProjectSetTaskAndDonateAllState, state);
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(GroupProject);
void
PlayerProject_SetTaskAndDonateAllItems(Entity *pEnt, const char *projectName, const char *taskName, int taskSlotNum)
{
    if ( pEnt )
    {
        GroupProjectContainer *projectContainer;
        GroupProjectSetTaskAndDonateAllState *state;

        GroupProject_SubscribeToPlayerProjectContainer(pEnt);
        projectContainer = GET_REF(pEnt->pPlayer->hGroupProjectContainer);

        if ( projectContainer == NULL )
        {
            if ( GetDonateAllState(pEnt->myContainerID) != NULL )
            {
                // Don't allow multiple pending requests.
                return;
            }

            // Ask the group project server to create the container if it does not exist.
            RemoteCommand_aslGroupProject_VerifyProjectContainerExists(GLOBALTYPE_GROUPPROJECTSERVER, 0, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, pEnt->myContainerID, GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID);

            // Save the arguments so we can do the actual "set task and donate all" operation once the container is known to exist.
            state = StructCreate(parse_GroupProjectSetTaskAndDonateAllState);
            state->playerID = pEnt->myContainerID;
            state->projectName = allocAddString(projectName);
            state->taskName = allocAddString(taskName);
            state->taskSlotNum = taskSlotNum;
            state->timeStarted = timeSecondsSince2000();
            PushDonateAllState(state);
        }
        else
        {
            // Container already exists, so just do it.
            SetTaskAndDonateAllItems(pEnt, projectName, taskName, taskSlotNum);
        }
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(GroupProject) ACMD_PRIVATE;
void
gslPlayerProject_SetTaskAndDonateAllItems(Entity *pEnt, const char *projectName, const char *taskName, int taskSlotNum)
{
    PlayerProject_SetTaskAndDonateAllItems(pEnt, projectName, taskName, taskSlotNum);
}

