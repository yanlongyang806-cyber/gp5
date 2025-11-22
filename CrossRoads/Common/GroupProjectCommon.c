#include "GroupProjectCommon.h"
#include "GlobalTypeEnum.h"
#include "itemCommon.h"
#include "ResourceManager.h"
#include "StringCache.h"
#include "file.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "error.h"
#include "Expression.h"
#include "Entity.h"
#include "Player.h"
#include "Guild.h"
#include "AutoTransDefs.h"
#include "ActivityCommon.h"

#ifdef GAMESERVER
#include "objTransactions.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#endif
#ifdef APPSERVER
#include "objTransactions.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"
#endif

#include "AutoGen/GlobalTypeEnum_h_ast.h"
#include "AutoGen/GroupProjectCommon_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_GroupProjectDict = NULL;
DictionaryHandle g_GroupProjectUnlockDict = NULL;
DictionaryHandle g_GroupProjectNumericDict = NULL;
DictionaryHandle g_DonationTaskDict = NULL;
DictionaryHandle g_DonationTaskDiscountDict = NULL;
DictionaryHandle g_GroupProjectBonusDict = NULL;

GroupProjectLevelTreeDef g_GroupProjectLevelTreeDef;

ExprContext *g_DonationTaskExprContext = NULL;
const char *g_ProjectStateVarName = NULL;
static int s_ProjectStateVarHandle = 0;
static const char *s_PlayerVarName = NULL;
static int s_PlayerVarHandle = 0;
static const char *s_DonationTaskDefVarName = NULL;
static int s_DonationTaskDefVarHandle = 0;

extern ParseTable parse_PlayerGuild[];

static EARRAY_OF(UnlocksForNumeric) s_UnlocksForNumerics = NULL;

U32 gDebugUnlockNotify = 0;
AUTO_CMD_INT(gDebugUnlockNotify, DebugUnlockNotify);

static void
AddUnlockToNumericMapping(GroupProjectUnlockDef *unlockDef)
{
    UnlocksForNumeric *unlocksForNumeric;
    GroupProjectNumericDef *numericDef;
    GroupProjectUnlockDefRef *unlockDefRef;

    if ( unlockDef && unlockDef->type == UnlockType_NumericValueEqualOrGreater )
    {
        if ( s_UnlocksForNumerics == NULL )
        {
            eaIndexedEnable(&s_UnlocksForNumerics, parse_UnlocksForNumeric);
        }

        numericDef = GET_REF(unlockDef->numeric);
        if ( numericDef == NULL )
        {
            Errorf("NumericValueEqualOrGreater unlock %s refers to non-existent numeric", unlockDef->name);
            return;
        }

        unlocksForNumeric = eaIndexedGetUsingString(&s_UnlocksForNumerics, numericDef->name);
        if ( unlocksForNumeric == NULL )
        {
            // There is no entry for this numeric, so create one.
            unlocksForNumeric = StructCreate(parse_UnlocksForNumeric);
            SET_HANDLE_FROM_REFERENT(g_GroupProjectNumericDict, numericDef, unlocksForNumeric->numericDef);
            eaIndexedAdd(&s_UnlocksForNumerics, unlocksForNumeric);
        }

        unlockDefRef = eaIndexedGetUsingString(&unlocksForNumeric->unlocks, unlockDef->name);
        if ( unlockDefRef == NULL )
        {
            // There is no entry for this unlock, so create one.
            unlockDefRef = StructCreate(parse_GroupProjectUnlockDefRef);
            SET_HANDLE_FROM_REFERENT(g_GroupProjectUnlockDict, unlockDef, unlockDefRef->unlockDef);
            eaIndexedAdd(&unlocksForNumeric->unlocks, unlockDefRef);
        }
    }
}

static void
BuildUnlockNumericMapping(void)
{
    RefDictIterator iter = {0};
    GroupProjectUnlockDef *unlockDef = NULL;

    RefSystem_InitRefDictIterator(g_GroupProjectUnlockDict, &iter);
    while ( unlockDef = RefSystem_GetNextReferentFromIterator(&iter) )
    {
        AddUnlockToNumericMapping(unlockDef);
    }
}

UnlocksForNumeric *
GroupProject_GetUnlocksForNumeric(GroupProjectNumericDef *numericDef)
{
    UnlocksForNumeric *unlocksForNumeric;
    unlocksForNumeric = eaIndexedGetUsingString(&s_UnlocksForNumerics, numericDef->name);

    return unlocksForNumeric;
}

void
GroupProject_SubscribeToGuildProject(Entity *playerEnt)
{
    if ( playerEnt && playerEnt->pPlayer && playerEnt->pPlayer->pGuild && guild_IsMember(playerEnt) )
    {
        GroupProjectContainer *projectContainer = GET_REF(playerEnt->pPlayer->pGuild->hGroupProjectContainer);
        if ( !IS_HANDLE_ACTIVE(playerEnt->pPlayer->pGuild->hGroupProjectContainer) || projectContainer && projectContainer->containerID != guild_GetGuildID(playerEnt) )
        {
			char idBuf[128];
            SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD), 
                ContainerIDToString(guild_GetGuildID(playerEnt), idBuf), 
                playerEnt->pPlayer->pGuild->hGroupProjectContainer);
            entity_SetDirtyBit(playerEnt, parse_PlayerGuild, playerEnt->pPlayer->pGuild, true);
			entity_SetDirtyBit(playerEnt, parse_Player, playerEnt->pPlayer, true);
        }
    }
}

void
GroupProject_SubscribeToPlayerProjectContainer(Entity *playerEnt)
{
    if ( playerEnt && playerEnt->pPlayer )
    {
        GroupProjectContainer *projectContainer = GET_REF(playerEnt->pPlayer->hGroupProjectContainer);
        if ( !IS_HANDLE_ACTIVE(playerEnt->pPlayer->hGroupProjectContainer) || projectContainer && projectContainer->containerID != entGetContainerID(playerEnt) )
        {
			char idBuf[128];
            SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER), 
                ContainerIDToString(entGetContainerID(playerEnt), idBuf), 
                playerEnt->pPlayer->hGroupProjectContainer);
            entity_SetDirtyBit(playerEnt, parse_Player, playerEnt->pPlayer, true);
        }
    }
}

// Make sure that the player has a subscription to the group project container for given project type.
void
GroupProject_ValidateContainer(Entity *playerEnt, GroupProjectType projectType)
{
    if ( projectType == GroupProjectType_Guild )
    {
        GroupProject_SubscribeToGuildProject(playerEnt);
    }
    else if ( projectType == GroupProjectType_Player )
    {
        GroupProject_SubscribeToPlayerProjectContainer(playerEnt);
    }
    else
    {
        Errorf("GroupProject_ValidateContainer: Unsupported project type: %d", projectType);
    }
}

GlobalType
GroupProject_ContainerTypeForProjectType(GroupProjectType projectType)
{
    if ( projectType == GroupProjectType_Guild )
    {
        return GLOBALTYPE_GROUPPROJECTCONTAINERGUILD;
    }
    else if ( projectType == GroupProjectType_Player )
    {
        return GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER;
    }

    return GLOBALTYPE_NONE;
}

ContainerID
GroupProject_ContainerIDForProjectType(Entity *playerEnt, GroupProjectType projectType)
{
    if ( projectType == GroupProjectType_Guild )
    {
        if ( guild_IsMember(playerEnt) )
        {
            return guild_GetGuildID(playerEnt);
        }
    }
    else if ( projectType == GroupProjectType_Player )
    {
        return entGetContainerID(playerEnt);
    }

    return 0;
}

GroupProjectContainer *
GroupProject_ResolveContainer(Entity *playerEnt, GroupProjectType projectType)
{
    switch (projectType)
    {
        case GroupProjectType_Guild:
            {
                if ( guild_IsMember(playerEnt) )
                {
                    Guild *pGuild = guild_GetGuild(playerEnt);
                    if ( pGuild )
                    {
                        return GET_REF(playerEnt->pPlayer->pGuild->hGroupProjectContainer);
                    }
                }
            }
            break;
        case GroupProjectType_Faction:
            {
                // TODO: resolve faction project container
            }
            break;
        case GroupProjectType_Shard:
            {
                // TODO: resolve shard project container
            }
            break;
        case GroupProjectType_Player:
            {
                if ( playerEnt && playerEnt->pPlayer )
                {
                    return GET_REF(playerEnt->pPlayer->hGroupProjectContainer);
                }
            }
            break;
    }

    return NULL;
}

//////////////////////////////////////////////////////////////////////////
//
// Donation task requires expression support
//
//////////////////////////////////////////////////////////////////////////

static void
DonationTaskExprEval(int iPartitionIdx, Entity *playerEnt, Expression *expression, GroupProjectState *projectState, DonationTaskDef *taskDef, MultiVal *pResult)
{
    exprContextSetSilentErrors(g_DonationTaskExprContext, false);
    exprContextSetPartition(g_DonationTaskExprContext, iPartitionIdx);
    exprContextSetSelfPtr(g_DonationTaskExprContext, playerEnt);
    exprContextSetPointerVarPooledCached(g_DonationTaskExprContext, g_ProjectStateVarName, projectState, parse_GroupProjectState, true, true, &s_ProjectStateVarHandle);
    exprContextSetPointerVarPooledCached(g_DonationTaskExprContext, s_PlayerVarName, playerEnt, parse_Entity, true, true, &s_PlayerVarHandle);
	exprContextSetPointerVarPooledCached(g_DonationTaskExprContext, s_DonationTaskDefVarName, taskDef, parse_DonationTaskDef, true, true, &s_DonationTaskDefVarHandle);

    exprEvaluate(expression, g_DonationTaskExprContext, pResult);
}

bool
GroupProject_DonationTaskAllowed(int iPartitionIdx, Entity *playerEnt, GroupProjectState *projectState, DonationTaskDef *taskDef, int taskSlotNum)
{
    if ( taskDef->activityEventRequirement )
    {
        if ( !Activity_EventIsActive(taskDef->activityEventRequirement) )
            return false;
    }

    if ( playerEnt && projectState)
	{
        int i;
        MultiVal mv = {0};

        // If it is a non-repeatable task, make sure that it has not been completed already.
        if ( !taskDef->repeatable )
        {
            DonationTaskDefRefContainer *taskDefRef;

            taskDefRef = eaIndexedGetUsingString(&projectState->completedTasks, taskDef->name);
            if ( taskDefRef != NULL )
            {
                // The task has already been completed.
                return false;
            }
        }

        // Make sure the task is not already active or queued for another slot.
        for ( i = eaSize(&projectState->taskSlots) - 1; i >= 0; i-- )
        {
            DonationTaskDef *slotTaskDef;

            // Matches with the current slot are ok, unless the task is non-repeatable.
            if ( i == taskSlotNum )
            {
                if ( !taskDef->repeatable )
                {
                    // Check the current task.
                    slotTaskDef = GET_REF(projectState->taskSlots[i]->taskDef);
                    if ( taskDef == slotTaskDef )
                    {
                        return false;
                    }
                }
            }
            else
            {
                // Check the current task.
                slotTaskDef = GET_REF(projectState->taskSlots[i]->taskDef);
                if ( taskDef == slotTaskDef )
                {
                    return false;
                }

                // Check the next task.
                slotTaskDef = GET_REF(projectState->taskSlots[i]->nextTaskDef);
                if ( taskDef == slotTaskDef )
                {
                    return false;
                }
            }
        }

        // Evaluate the expression.
        if ( taskDef->taskAvailableExpr )
        {
			DonationTaskExprEval(iPartitionIdx, playerEnt, taskDef->taskAvailableExpr, projectState, taskDef, &mv);
            return MultiValToBool(&mv);
        }

        // Task is allowed if there is no expression.
        return true;
	}
    else
    {
        // Return true if the task has been flagged as being available for new group projects (meaning when no container exists).
        return taskDef->taskAvailableForNewProject;
    }
}

bool
GuildProject_DonationTaskAllowed(int iPartitionIdx, Entity *playerEnt, GroupProjectState *projectState, DonationTaskDef *taskDef, int taskSlotNum)
{
    if ( playerEnt && playerEnt->pPlayer && playerEnt->pPlayer->pGuild && guild_IsMember(playerEnt) )
    {
        return GroupProject_DonationTaskAllowed(iPartitionIdx, playerEnt, projectState, taskDef, taskSlotNum);
    }

    return false;
}

bool
PlayerProject_DonationTaskAllowed(int iPartitionIdx, Entity *playerEnt, GroupProjectState *projectState, DonationTaskDef *taskDef, int taskSlotNum)
{
    if ( playerEnt && playerEnt->pPlayer && projectState )
    {
        return GroupProject_DonationTaskAllowed(iPartitionIdx, playerEnt, projectState, taskDef, taskSlotNum);
    }

    return false;
}

static void
DonationTaskExprContextInit(void)
{
    ExprFuncTable* funcTable;

    g_ProjectStateVarName = allocAddStaticString("ProjectState");
    s_PlayerVarName = allocAddStaticString("Player");
	s_DonationTaskDefVarName = allocAddStaticString("DonationTaskDef");

    funcTable = exprContextCreateFunctionTable("DonationTask");
    exprContextAddFuncsToTableByTag(funcTable, "GroupProject");
    exprContextAddFuncsToTableByTag(funcTable, "gameutil");
    exprContextAddFuncsToTableByTag(funcTable, "util");
    exprContextAddFuncsToTableByTag(funcTable, "Player");

    g_DonationTaskExprContext = exprContextCreate();
    exprContextSetFuncTable(g_DonationTaskExprContext, funcTable);
    exprContextSetSelfPtr(g_DonationTaskExprContext, NULL);
    exprContextSetAllowRuntimeSelfPtr(g_DonationTaskExprContext);
    exprContextSetAllowRuntimePartition(g_DonationTaskExprContext);
    exprContextSetPointerVarPooledCached(g_DonationTaskExprContext, g_ProjectStateVarName, NULL, parse_GroupProjectState, true, true, &s_ProjectStateVarHandle);
	exprContextSetPointerVarPooledCached(g_DonationTaskExprContext, s_DonationTaskDefVarName, NULL, parse_DonationTaskDef, true, true, &s_DonationTaskDefVarHandle);

    return;
}

//////////////////////////////////////////////////////////////////////////
//
// Group Project Validation
//
//////////////////////////////////////////////////////////////////////////
static bool 
GroupProject_ValidateAllButRefs(GroupProjectDef* projectDef)
{
    bool bSuccess = true;

    if (!resIsValidName(projectDef->name))
    {
        ErrorFilenamef(projectDef->filename, "Group Project name is illegal: '%s'", projectDef->name);
        bSuccess = false;
    }

    if (!resIsValidScope(projectDef->scope))
    {
        ErrorFilenamef(projectDef->filename, "Group Project scope is illegal: '%s'", projectDef->scope);
        bSuccess = false;
    }

	if (eaiSize(&projectDef->slotTypes) <= 0)
    {
        ErrorFilenamef(projectDef->filename, "Group Project number of active task slots must be greater than zero");
        bSuccess = false;
    }

    return bSuccess;
}

static bool 
GroupProject_ValidateRefs(GroupProjectDef* projectDef)
{
    bool bSuccess = true;
	S32 i, j, k;

#ifdef GAMESERVER
    // Validate messages.
    if (!GET_REF(projectDef->displayNameMsg.hMessage) && REF_STRING_FROM_HANDLE(projectDef->displayNameMsg.hMessage))
    {
        ErrorFilenamef(projectDef->filename, "Group Project refers to non-existent display name message '%s'", REF_STRING_FROM_HANDLE(projectDef->displayNameMsg.hMessage));
        bSuccess = false;
    }
    if (!GET_REF(projectDef->descriptionMsg.hMessage) && REF_STRING_FROM_HANDLE(projectDef->descriptionMsg.hMessage))
    {
        ErrorFilenamef(projectDef->filename, "Group Project refers to non-existent description message '%s'", REF_STRING_FROM_HANDLE(projectDef->descriptionMsg.hMessage));
        bSuccess = false;
    }
#endif // GAMESERVER

    // Validate unlocks.
    for ( i = eaSize(&projectDef->unlockDefs) - 1; i >= 0; i-- )
    {
        GroupProjectUnlockDef *unlockDef = GET_REF(projectDef->unlockDefs[i]->unlockDef);
        if (!unlockDef && REF_STRING_FROM_HANDLE(projectDef->unlockDefs[i]->unlockDef))
        {
            ErrorFilenamef(projectDef->filename, "Group Project refers to non-existent Group Project Unlock '%s'", REF_STRING_FROM_HANDLE(projectDef->unlockDefs[i]->unlockDef));
            bSuccess = false;
        }

        if ( unlockDef )
        {
            GroupProjectNumericDef *numericDef = GET_REF(unlockDef->numeric);
            if ( unlockDef->type == UnlockType_NumericValueEqualOrGreater )
            {
                if ( numericDef && eaIndexedFindUsingString(&projectDef->validNumerics, numericDef->name) < 0 )
                {
                    ErrorFilenamef(projectDef->filename, "Unlock '%s' triggers on numeric '%s' not valid for Group Project '%s'", unlockDef->name, numericDef->name, projectDef->name);
                    bSuccess = false;
                }
            }
        }
    }

    // Validate donation tasks.
    for ( i = eaSize(&projectDef->donationTaskDefs) - 1; i >= 0; i-- )
    {
        DonationTaskDef *taskDef = GET_REF(projectDef->donationTaskDefs[i]->taskDef);
        if (!taskDef && REF_STRING_FROM_HANDLE(projectDef->donationTaskDefs[i]->taskDef))
        {
            ErrorFilenamef(projectDef->filename, "Group Project refers to non-existent Donation Task '%s'", REF_STRING_FROM_HANDLE(projectDef->donationTaskDefs[i]->taskDef));
            bSuccess = false;
        } 
        else if ( taskDef && ( projectDef->type != GroupProjectType_Player ) )
        {
            // Donation tasks may only grant reward tables if the group project type is GroupProjectType_Player.
            const char *rewardTableName = REF_STRING_FROM_HANDLE(taskDef->completionRewardTable);
            if ( rewardTableName )
            {
                ErrorFilenamef(projectDef->filename, "Donation task '%s' specifies reward table '%s' but is referenced by Group Project '%s' that is not type 'Player'", REF_STRING_FROM_HANDLE(projectDef->donationTaskDefs[i]->taskDef), rewardTableName, projectDef->name);
                bSuccess = false;
            }
        }

        // Ensure all constants, numerics, and unlocks referenced by the task exist on the project def
        if ( taskDef )
        {
			//Ensure that an noCost task has empty requirements
			if( taskDef->noCost )
			{
				if( eaSize(&taskDef->buckets) > 0 )
				{
					ErrorFilenamef(taskDef->filename, "Donation task '%s' has noCost set, but a nonzero set of buckets.", taskDef->name);
					bSuccess = false;
				}
			}
			else
			{
				for ( j = eaSize(&taskDef->buckets) - 1; j >= 0; j-- )
				{
					if ( GroupProject_FindConstant(projectDef, taskDef->buckets[j]->contributionConstant) == NULL )
					{
						ErrorFilenamef(projectDef->filename, "Donation task '%s' bucket '%s' uses constant '%s' not defined in Group Project '%s'", taskDef->name, taskDef->buckets[j]->name, taskDef->buckets[j]->contributionConstant, projectDef->name);
						bSuccess = false;
					}
					for ( k = eaSize(&taskDef->buckets[j]->discounts) - 1; k >= 0; k-- )
					{
						if ( !RefSystem_ReferentFromString(g_DonationTaskDiscountDict, taskDef->buckets[j]->discounts[k]) )
						{
							ErrorFilenamef(projectDef->filename, "Donation task '%s' bucket '%s' uses discount '%s' which does not exist", taskDef->name, taskDef->buckets[j]->name, taskDef->buckets[j]->discounts[k]);
							bSuccess = false;
						}
					}
				}
			}
            for ( j = eaSize(&taskDef->taskStartRewards) - 1; j >= 0; j-- )
            {
                DonationTaskReward *rewardDef = taskDef->taskStartRewards[j];
                GroupProjectUnlockDef *unlockDef = GET_REF(rewardDef->unlockDef);
                GroupProjectNumericDef *numericDef = GET_REF(rewardDef->numericDef);
                GroupProjectBonusDef *bonusDef = GET_REF(rewardDef->bonusDef);
                if ( rewardDef->rewardType == DonationTaskRewardType_NumericAdd
                    || rewardDef->rewardType == DonationTaskRewardType_NumericSet )
                {
                    if ( GroupProject_FindConstant(projectDef, rewardDef->rewardConstant) == NULL )
                    {
                        ErrorFilenamef(projectDef->filename, "Donation task '%s' start reward uses constant '%s' not defined in Group Project '%s'", taskDef->name, rewardDef->rewardConstant, projectDef->name);
                        bSuccess = false;
                    }
                    if ( numericDef && eaIndexedFindUsingString(&projectDef->validNumerics, numericDef->name) < 0 )
                    {
                        ErrorFilenamef(projectDef->filename, "Donation task '%s' start reward uses numeric '%s' not valid for Group Project '%s'", taskDef->name, numericDef->name, projectDef->name);
                        bSuccess = false;
                    }
                }
                else if ( rewardDef->rewardType == DonationTaskRewardType_Unlock )
                {
                    if ( unlockDef && eaIndexedFindUsingString(&projectDef->unlockDefs, unlockDef->name) < 0 )
                    {
                        ErrorFilenamef(projectDef->filename, "Donation task '%s' start reward uses unlock '%s' not valid for Group Project '%s'", taskDef->name, unlockDef->name, projectDef->name);
                        bSuccess = false;
                    }
                }
                else if ( rewardDef->rewardType == DonationTaskRewardType_GuildProjectDiscount
                    || rewardDef->rewardType == DonationTaskRewardType_PlayerProjectDiscount )
                {
                    if ( rewardDef->rewardType == DonationTaskRewardType_GuildProjectDiscount
                        && projectDef->type != GroupProjectType_Guild )
                    {
                        ErrorFilenamef(projectDef->filename, "Donation task '%s' start reward grants GuildProject discount for non-Guild Group Project '%s'", taskDef->name, projectDef->name);
                        bSuccess = false;
                    }
                    if ( rewardDef->rewardType == DonationTaskRewardType_PlayerProjectDiscount
                        && projectDef->type != GroupProjectType_Player )
                    {
                        ErrorFilenamef(projectDef->filename, "Donation task '%s' start reward grants PlayerProject discount for non-Player Group Project '%s'", taskDef->name, projectDef->name);
                        bSuccess = false;
                    }
                }
            }
            for ( j = eaSize(&taskDef->taskRewards) - 1; j >= 0; j-- )
            {
                DonationTaskReward *rewardDef = taskDef->taskRewards[j];
                GroupProjectUnlockDef *unlockDef = GET_REF(rewardDef->unlockDef);
                GroupProjectNumericDef *numericDef = GET_REF(rewardDef->numericDef);
                GroupProjectBonusDef *bonusDef = GET_REF(rewardDef->bonusDef);
                if ( rewardDef->rewardType == DonationTaskRewardType_NumericAdd
                    || rewardDef->rewardType == DonationTaskRewardType_NumericSet )
                {
                    if ( GroupProject_FindConstant(projectDef, rewardDef->rewardConstant) == NULL )
                    {
                        ErrorFilenamef(projectDef->filename, "Donation task '%s' reward uses constant '%s' not defined in Group Project '%s'", taskDef->name, rewardDef->rewardConstant, projectDef->name);
                        bSuccess = false;
                    }
                    if ( numericDef && eaIndexedFindUsingString(&projectDef->validNumerics, numericDef->name) < 0 )
                    {
                        ErrorFilenamef(projectDef->filename, "Donation task '%s' reward uses numeric '%s' not valid for Group Project '%s'", taskDef->name, numericDef->name, projectDef->name);
                        bSuccess = false;
                    }
                }
                else if ( rewardDef->rewardType == DonationTaskRewardType_Unlock )
                {
                    if ( unlockDef && eaIndexedFindUsingString(&projectDef->unlockDefs, unlockDef->name) < 0 )
                    {
                        ErrorFilenamef(projectDef->filename, "Donation task '%s' reward uses unlock '%s' not valid for Group Project '%s'", taskDef->name, unlockDef->name, projectDef->name);
                        bSuccess = false;
                    }
                }
                else if ( rewardDef->rewardType == DonationTaskRewardType_GuildProjectDiscount
                    || rewardDef->rewardType == DonationTaskRewardType_PlayerProjectDiscount )
                {
                    if ( rewardDef->rewardType == DonationTaskRewardType_GuildProjectDiscount
                        && projectDef->type != GroupProjectType_Guild )
                    {
                        ErrorFilenamef(projectDef->filename, "Donation task '%s' reward grants GuildProject discount for non-Guild Group Project '%s'", taskDef->name, projectDef->name);
                        bSuccess = false;
                    }
                    if ( rewardDef->rewardType == DonationTaskRewardType_PlayerProjectDiscount
                        && projectDef->type != GroupProjectType_Player )
                    {
                        ErrorFilenamef(projectDef->filename, "Donation task '%s' reward grants PlayerProject discount for non-Player Group Project '%s'", taskDef->name, projectDef->name);
                        bSuccess = false;
                    }
                }
            }
        }
    }

    // Validate group project numerics.
    for ( i = eaSize(&projectDef->validNumerics) - 1; i >= 0; i-- )
    {
        if (!GET_REF(projectDef->validNumerics[i]->numericDef) && REF_STRING_FROM_HANDLE(projectDef->validNumerics[i]->numericDef))
        {
            ErrorFilenamef(projectDef->filename, "Group Project refers to non-existent Group Project Numeric '%s'", REF_STRING_FROM_HANDLE(projectDef->validNumerics[i]->numericDef));
            bSuccess = false;
        }
    }

#ifdef GAMESERVER
    // Validate contribution numeric.
    if (!GET_REF(projectDef->contributionNumeric) && REF_STRING_FROM_HANDLE(projectDef->contributionNumeric))
    {
        ErrorFilenamef(projectDef->filename, "Group Project refers to non-existent Contribution Numeric '%s'", REF_STRING_FROM_HANDLE(projectDef->contributionNumeric));
        bSuccess = false;
    }

    // Validate lifetime contribution numeric.
    if (!GET_REF(projectDef->lifetimeContributionNumeric) && REF_STRING_FROM_HANDLE(projectDef->lifetimeContributionNumeric))
    {
        ErrorFilenamef(projectDef->filename, "Group Project refers to non-existent Lifetime Contribution Numeric '%s'", REF_STRING_FROM_HANDLE(projectDef->lifetimeContributionNumeric));
        bSuccess = false;
    }

    // Validate contact references
    for ( i = eaSize(&projectDef->remoteContacts) - 1; i >= 0; i-- )
    {
#if 0
        if ( !RefSystem_ReferentFromString("ContactDef", projectDef->remoteContacts[i]->contactDef) )
        {
            ErrorFilenamef(projectDef->filename, "Group Project refers to non-existent ContactDef '%s'", projectDef->remoteContacts[i]->contactDef);
            bSuccess = false;
        }
#endif

        // Validate unlocks for contact
        for ( j = eaSize(&projectDef->remoteContacts[i]->requiredUnlocks) - 1; j >= 0; j-- )
        {
            if (!GET_REF(projectDef->remoteContacts[i]->requiredUnlocks[j]->unlockDef) && REF_STRING_FROM_HANDLE(projectDef->remoteContacts[i]->requiredUnlocks[j]->unlockDef))
            {
                ErrorFilenamef(projectDef->filename, "Group Project refers to non-existent Group Project Unlock '%s'", REF_STRING_FROM_HANDLE(projectDef->remoteContacts[i]->requiredUnlocks[j]->unlockDef));
                bSuccess = false;
            }
        }
    }
#endif // GAMESERVER

    return bSuccess;
}

bool 
GroupProject_Validate(GroupProjectDef* projectDef)
{
    bool bSuccess = true;

    if (!GroupProject_ValidateAllButRefs(projectDef))
    {
        bSuccess = false;
    }

    if (!GroupProject_ValidateRefs(projectDef))
    {
        bSuccess = false;
    }

    return bSuccess;
}

static int 
GroupProjectResValidateCB(enumResourceValidateType eType, const char *dictName, const char *resourceName, GroupProjectDef *projectDef, U32 userID)
{
    switch (eType)
    {	
        // Called for filename check
    case RESVALIDATE_FIX_FILENAME: 
        resFixPooledFilename(&projectDef->filename, GROUP_PROJECT_BASE_DIR, projectDef->scope, projectDef->name, GROUP_PROJECT_EXT);
        return VALIDATE_HANDLED;

        // Called after load/reload but before binning
    case RESVALIDATE_POST_TEXT_READING:
        GroupProject_ValidateAllButRefs(projectDef);
        return VALIDATE_HANDLED;

        // Called when all data has been loaded
    case RESVALIDATE_CHECK_REFERENCES:
        if (IsServer() && !isProductionMode())
        {
            GroupProject_ValidateRefs(projectDef);
            return VALIDATE_HANDLED;
        }
        break;
    }
    return VALIDATE_NOT_HANDLED;
}

//////////////////////////////////////////////////////////////////////////
//
// Group Project Unlock Validation
//
//////////////////////////////////////////////////////////////////////////
static bool 
GroupProjectUnlock_ValidateAllButRefs(GroupProjectUnlockDef* unlockDef)
{
    bool bSuccess = true;

    if (!resIsValidName(unlockDef->name))
    {
        ErrorFilenamef(unlockDef->filename, "Group Project Unlock name is illegal: '%s'", unlockDef->name);
        bSuccess = false;
    }

    if (!resIsValidScope(unlockDef->scope))
    {
        ErrorFilenamef(unlockDef->filename, "Group Project Unlock scope is illegal: '%s'", unlockDef->scope);
        bSuccess = false;
    }

    if ( unlockDef->type == UnlockType_Manual )
    {
        if ( unlockDef->triggerValue != 0 )
        {
            ErrorFilenamef(unlockDef->filename, "Group Project Unlock of type Manual must have zero triggerValue");
            bSuccess = false;
        }
    }
    else if ( unlockDef->type == UnlockType_NumericValueEqualOrGreater )
    {
        if ( unlockDef->triggerValue == 0 )
        {
            ErrorFilenamef(unlockDef->filename, "Group Project Unlock of type NumericValueEqualOrGreater must have non-zero triggerValue");
            bSuccess = false;
        }
    }
    else
    {  
        ErrorFilenamef(unlockDef->filename, "Group Project Unlock has invalid type");
        bSuccess = false;
    }
    return bSuccess;
}

static bool 
GroupProjectUnlock_ValidateRefs(GroupProjectUnlockDef* unlockDef)
{
    bool bSuccess = true;

#ifdef GAMESERVER
    // Validate messages.
    if (!GET_REF(unlockDef->displayNameMsg.hMessage) && REF_STRING_FROM_HANDLE(unlockDef->displayNameMsg.hMessage))
    {
        ErrorFilenamef(unlockDef->filename, "Group Project Unlock refers to non-existent display name message '%s'", REF_STRING_FROM_HANDLE(unlockDef->displayNameMsg.hMessage));
        bSuccess = false;
    }
    if (!GET_REF(unlockDef->descriptionMsg.hMessage) && REF_STRING_FROM_HANDLE(unlockDef->descriptionMsg.hMessage))
    {
        ErrorFilenamef(unlockDef->filename, "Group Project Unlock refers to non-existent description message '%s'", REF_STRING_FROM_HANDLE(unlockDef->descriptionMsg.hMessage));
        bSuccess = false;
    }
    if (!GET_REF(unlockDef->tooltipMsg.hMessage) && REF_STRING_FROM_HANDLE(unlockDef->tooltipMsg.hMessage))
    {
        ErrorFilenamef(unlockDef->filename, "Group Project Unlock refers to non-existent tooltip message '%s'", REF_STRING_FROM_HANDLE(unlockDef->tooltipMsg.hMessage));
        bSuccess = false;
    }
#endif // GAMESERVER

    // Validate type and trigger numeric.
    if ( unlockDef->type == UnlockType_Manual )
    {
        if ( REF_STRING_FROM_HANDLE(unlockDef->numeric) )
        {
            ErrorFilenamef(unlockDef->filename, "Group Project Unlock of type Manual must not have a numeric specified");
            bSuccess = false;
        }
    }
    else if ( unlockDef->type == UnlockType_NumericValueEqualOrGreater )
    {
        if ( !GET_REF(unlockDef->numeric) && REF_STRING_FROM_HANDLE(unlockDef->numeric) )
        {
            ErrorFilenamef(unlockDef->filename, "Group Project Unlock of type NumericValueEqualOrGreater has an invalid numeric");
            bSuccess = false;
        }
    }

    return bSuccess;
}

bool 
GroupProjectUnlock_Validate(GroupProjectUnlockDef* unlockDef)
{
    bool bSuccess = true;

    if (!GroupProjectUnlock_ValidateAllButRefs(unlockDef))
    {
        bSuccess = false;
    }

    if (!GroupProjectUnlock_ValidateRefs(unlockDef))
    {
        bSuccess = false;
    }

    return bSuccess;
}

static int 
GroupProjectUnlockResValidateCB(enumResourceValidateType eType, const char *dictName, const char *resourceName, GroupProjectUnlockDef *unlockDef, U32 userID)
{
    switch (eType)
    {	
        // Called for filename check
    case RESVALIDATE_FIX_FILENAME: 
        resFixPooledFilename(&unlockDef->filename, GROUP_PROJECT_UNLOCK_BASE_DIR, unlockDef->scope, unlockDef->name, GROUP_PROJECT_UNLOCK_EXT);
        return VALIDATE_HANDLED;

        // Called after load/reload but before binning
    case RESVALIDATE_POST_TEXT_READING:
        GroupProjectUnlock_ValidateAllButRefs(unlockDef);
        return VALIDATE_HANDLED;

        // Called when all data has been loaded
    case RESVALIDATE_CHECK_REFERENCES:
        if (IsServer() && !isProductionMode())
        {
            GroupProjectUnlock_ValidateRefs(unlockDef);
            return VALIDATE_HANDLED;
        }
        break;
    }
    return VALIDATE_NOT_HANDLED;
}

//////////////////////////////////////////////////////////////////////////
//
// Group Project Numeric Validation
//
//////////////////////////////////////////////////////////////////////////
static bool 
GroupProjectNumeric_ValidateAllButRefs(GroupProjectNumericDef* numericDef)
{
    bool bSuccess = true;

    if (!resIsValidName(numericDef->name))
    {
        ErrorFilenamef(numericDef->filename, "Group Project Numeric name is illegal: '%s'", numericDef->name);
        bSuccess = false;
    }

    if (!resIsValidScope(numericDef->scope))
    {
        ErrorFilenamef(numericDef->filename, "Group Project Numeric scope is illegal: '%s'", numericDef->scope);
        bSuccess = false;
    }

    return bSuccess;
}

static bool 
GroupProjectNumeric_ValidateRefs(GroupProjectNumericDef* numericDef)
{
    bool bSuccess = true;

#ifdef GAMESERVER
    // Validate messages.
    if (!GET_REF(numericDef->displayNameMsg.hMessage) && REF_STRING_FROM_HANDLE(numericDef->displayNameMsg.hMessage))
    {
        ErrorFilenamef(numericDef->filename, "Group Project Numeric refers to non-existent display name message '%s'", REF_STRING_FROM_HANDLE(numericDef->displayNameMsg.hMessage));
        bSuccess = false;
    }
    if (!GET_REF(numericDef->tooltipMsg.hMessage) && REF_STRING_FROM_HANDLE(numericDef->tooltipMsg.hMessage))
    {
        ErrorFilenamef(numericDef->filename, "Group Project Numeric refers to non-existent tooltip message '%s'", REF_STRING_FROM_HANDLE(numericDef->tooltipMsg.hMessage));
        bSuccess = false;
    }
#endif // GAMESERVER

    return bSuccess;
}

bool 
GroupProjectNumeric_Validate(GroupProjectNumericDef* numericDef)
{
    bool bSuccess = true;

    if (!GroupProjectNumeric_ValidateAllButRefs(numericDef))
    {
        bSuccess = false;
    }

    if (!GroupProjectNumeric_ValidateRefs(numericDef))
    {
        bSuccess = false;
    }

    return bSuccess;
}

static int 
GroupProjectNumericResValidateCB(enumResourceValidateType eType, const char *dictName, const char *resourceName, GroupProjectNumericDef *numericDef, U32 userID)
{
    switch (eType)
    {	
        // Called for filename check
    case RESVALIDATE_FIX_FILENAME: 
        resFixPooledFilename(&numericDef->filename, GROUP_PROJECT_NUMERIC_BASE_DIR, numericDef->scope, numericDef->name, GROUP_PROJECT_NUMERIC_EXT);
        return VALIDATE_HANDLED;

        // Called after load/reload but before binning
    case RESVALIDATE_POST_TEXT_READING:
        GroupProjectNumeric_ValidateAllButRefs(numericDef);
        return VALIDATE_HANDLED;

        // Called when all data has been loaded
    case RESVALIDATE_CHECK_REFERENCES:
        if (IsServer() && !isProductionMode())
        {
            GroupProjectNumeric_ValidateRefs(numericDef);
            return VALIDATE_HANDLED;
        }
        break;
    }
    return VALIDATE_NOT_HANDLED;
}

//////////////////////////////////////////////////////////////////////////
//
// Donation Task Validation
//
//////////////////////////////////////////////////////////////////////////
static bool 
DonationTask_ValidateAllButRefs(DonationTaskDef* taskDef)
{
    bool bSuccess = true;
    S32 i;

    if (!resIsValidName(taskDef->name))
    {
        ErrorFilenamef(taskDef->filename, "Donation Task name is illegal: '%s'", taskDef->name);
        bSuccess = false;
    }

    if (!resIsValidScope(taskDef->scope))
    {
        ErrorFilenamef(taskDef->filename, "Donation Task scope is illegal: '%s'", taskDef->scope);
        bSuccess = false;
    }

    // Make sure a completion time is specified.
    if ( taskDef->secondsToComplete == 0 )
    {
        ErrorFilenamef(taskDef->filename, "Donation Task does not specify completion time");
        bSuccess = false;
    }

    // Generate the task available expression.
    exprGenerate(taskDef->taskAvailableExpr, g_DonationTaskExprContext);

    // Validate buckets
    if ( eaSize(&taskDef->buckets) <= 0 && !taskDef->noCost )
    {
        ErrorFilenamef(taskDef->filename, "Donation Task has no donation buckets");
        bSuccess = false;
    }
	else if ( eaSize(&taskDef->buckets) > 0 && taskDef->noCost )
	{
		ErrorFilenamef(taskDef->filename, "Donation Tasks should not exist when NoCost is true.");
		bSuccess = false;
	}
    else
    {
        for ( i = eaSize(&taskDef->buckets) - 1; i >= 0; i-- )
        {
            GroupProjectDonationRequirement *bucket = taskDef->buckets[i];
            if ( bucket->name == NULL || ( strlen(bucket->name) == 0 ) )
            {
                ErrorFilenamef(taskDef->filename, "Donation Task bucket name is empty string");
                bSuccess = false;
            }

            if ( bucket->count == 0 )
            {
                ErrorFilenamef(taskDef->filename, "Donation Task bucket required item count is zero");
                bSuccess = false;
            }

            if ( bucket->count == GROUP_PROJECT_INVALID_DISCOUNTED_BUCKET_REQUIREMENT )
            {
                ErrorFilenamef(taskDef->filename, "Donation Task bucket required item count uses special value, talk to Software if you really need a requirement count of %d", bucket->count);
                bSuccess = false;
            }

            if ( bucket->contributionConstant == NULL || ( strlen(bucket->contributionConstant) == 0 ) )
            {
                ErrorFilenamef(taskDef->filename, "Donation Task bucket contribution constant not specified");
                bSuccess = false;
            }

            if ( bucket->specType == DonationSpecType_Item )
            {
                // XXX - something needed here?
            }
            else if ( bucket->specType == DonationSpecType_Expression )
            {
                // XXX - something needed here?
                extern ExprContext *g_pItemContext;
                exprGenerate(bucket->allowedItemExpr, g_pItemContext);
                if ( bucket->donationIncrement > 1 )
                {
                    ErrorFilenamef(taskDef->filename, "Donation Task bucket spec type 'Expression' requires Donation Chunk Size of 0 or 1");
                    bSuccess = false;
                }
            }
            else
            {
                ErrorFilenamef(taskDef->filename, "Donation Task bucket item specification type is invalid.  Must be 'Item' or 'Expression'");
                bSuccess = false;
            }
        }
    }

    if ( eaSize(&taskDef->taskRewards) <= 0 )
    {
        ErrorFilenamef(taskDef->filename, "Donation Task has no reward specified");
        bSuccess = false;
    }
    else
    {
        for ( i = eaSize(&taskDef->taskRewards) - 1; i >= 0; i-- )
        {
            DonationTaskReward *taskReward = taskDef->taskRewards[i];

            if ( taskReward->rewardType == DonationTaskRewardType_NumericAdd )
            {
                if ( taskReward->rewardConstant == NULL || ( strlen(taskReward->rewardConstant) == 0 ) )
                {
                    ErrorFilenamef(taskDef->filename, "Donation Task reward constant not specified");
                    bSuccess = false;
                }
            }
            else if ( taskReward->rewardType == DonationTaskRewardType_NumericSet )
            {
                if ( taskReward->rewardConstant == NULL || ( strlen(taskReward->rewardConstant) == 0 ) )
                {
                    ErrorFilenamef(taskDef->filename, "Donation Task reward constant not specified");
                    bSuccess = false;
                }
            }
            else if ( taskReward->rewardType == DonationTaskRewardType_Unlock )
            {
                // Do nothing here.
            }
            else if ( taskReward->rewardType == DonationTaskRewardType_GuildProjectDiscount
                || taskReward->rewardType == DonationTaskRewardType_PlayerProjectDiscount )
            {
                if ( REF_IS_SET_BUT_ABSENT(taskReward->discountDef) )
                {
                    ErrorFilenamef(taskDef->filename, "Donation Task reward discount '%s' not found", REF_STRING_FROM_HANDLE(taskReward->discountDef));
                    bSuccess = false;
                }
                else if ( !IS_HANDLE_ACTIVE(taskReward->discountDef) )
                {
                    ErrorFilenamef(taskDef->filename, "Donation Task reward discount not specified");
                    bSuccess = false;
                }
            }
            else if ( taskReward->rewardType == DonationTaskRewardType_Bonus )
            {
                if ( REF_IS_SET_BUT_ABSENT(taskReward->bonusDef) )
                {
                    ErrorFilenamef(taskDef->filename, "Donation Task reward bonus '%s' not found", REF_STRING_FROM_HANDLE(taskReward->bonusDef));
                    bSuccess = false;
                }
                else if ( !IS_HANDLE_ACTIVE(taskReward->bonusDef) )
                {
                    ErrorFilenamef(taskDef->filename, "Donation Task reward bonus not specified");
                    bSuccess = false;
                }
            }
            else
            {
                ErrorFilenamef(taskDef->filename, "Donation Task bucket reward type is invalid.  Must be 'Unlock' or 'Numeric'");
                bSuccess = false;
            }
        }
    }

    return bSuccess;
}

static bool 
DonationTask_ValidateRefs(DonationTaskDef* taskDef)
{
    bool bSuccess = true;
    S32 i;

#ifdef GAMESERVER
    // Validate messages.
    if (!GET_REF(taskDef->displayNameMsg.hMessage) && REF_STRING_FROM_HANDLE(taskDef->displayNameMsg.hMessage))
    {
        ErrorFilenamef(taskDef->filename, "Donation Task refers to non-existent display name message '%s'", REF_STRING_FROM_HANDLE(taskDef->displayNameMsg.hMessage));
        bSuccess = false;
    }
    if (!GET_REF(taskDef->descriptionMsg.hMessage) && REF_STRING_FROM_HANDLE(taskDef->descriptionMsg.hMessage))
    {
        ErrorFilenamef(taskDef->filename, "Donation Task refers to non-existent description message '%s'", REF_STRING_FROM_HANDLE(taskDef->descriptionMsg.hMessage));
        bSuccess = false;
    }
    if (!GET_REF(taskDef->tooltipMsg.hMessage) && REF_STRING_FROM_HANDLE(taskDef->tooltipMsg.hMessage))
    {
        ErrorFilenamef(taskDef->filename, "Donation Task refers to non-existent tooltip message '%s'", REF_STRING_FROM_HANDLE(taskDef->tooltipMsg.hMessage));
        bSuccess = false;
    }
#endif // GAMESERVER

	if( !taskDef->noCost )
	{
		for ( i = eaSize(&taskDef->buckets) - 1; i >= 0; i-- )
		{
			GroupProjectDonationRequirement *bucket = taskDef->buckets[i];

	#ifdef GAMESERVER
			// Validate messages on each bucket.
			if (!GET_REF(bucket->displayNameMsg.hMessage) && REF_STRING_FROM_HANDLE(bucket->displayNameMsg.hMessage))
			{
				ErrorFilenamef(taskDef->filename, "Donation Task bucket refers to non-existent display name message '%s'", REF_STRING_FROM_HANDLE(bucket->displayNameMsg.hMessage));
				bSuccess = false;
			}
			if (!GET_REF(bucket->descriptionMsg.hMessage) && REF_STRING_FROM_HANDLE(bucket->descriptionMsg.hMessage))
			{
				ErrorFilenamef(taskDef->filename, "Donation Task bucket refers to non-existent description message '%s'", REF_STRING_FROM_HANDLE(bucket->descriptionMsg.hMessage));
				bSuccess = false;
			}
			if (!GET_REF(bucket->tooltipMsg.hMessage) && REF_STRING_FROM_HANDLE(bucket->tooltipMsg.hMessage))
			{
				ErrorFilenamef(taskDef->filename, "Donation Task bucket refers to non-existent tooltip message '%s'", REF_STRING_FROM_HANDLE(bucket->tooltipMsg.hMessage));
				bSuccess = false;
			}
	#endif // GAMESERVER

			if ( bucket->specType == DonationSpecType_Item )
			{
	#ifdef GAMESERVER
				if (!GET_REF(bucket->requiredItem) && REF_STRING_FROM_HANDLE(bucket->requiredItem))
				{
					ErrorFilenamef(taskDef->filename, "Donation Task bucket refers to non-existent item '%s'", REF_STRING_FROM_HANDLE(bucket->requiredItem));
					bSuccess = false;
				}
	#endif // GAMESERVER
			}
			else if ( bucket->specType == DonationSpecType_Expression )
			{
				if (REF_STRING_FROM_HANDLE(bucket->requiredItem))
				{
					ErrorFilenamef(taskDef->filename, "Donation Task bucket with spec type of 'Expression' should not specify an item: %s", REF_STRING_FROM_HANDLE(bucket->requiredItem));
					bSuccess = false;
				}

				// XXX - something needed here?
			}
		}
	}

    for ( i = eaSize(&taskDef->taskRewards) - 1; i >= 0; i-- )
    {
        DonationTaskReward *taskReward = taskDef->taskRewards[i];

        if ( ( taskReward->rewardType == DonationTaskRewardType_NumericAdd ) || ( taskReward->rewardType == DonationTaskRewardType_NumericSet ) )
        {
            if (!GET_REF(taskReward->numericDef) && REF_STRING_FROM_HANDLE(taskReward->numericDef))
            {
                ErrorFilenamef(taskDef->filename, "Donation Task reward refers to non-existent numeric '%s'", REF_STRING_FROM_HANDLE(taskReward->numericDef));
                bSuccess = false;
            }
            if ( REF_STRING_FROM_HANDLE(taskReward->unlockDef) )
            {
                ErrorFilenamef(taskDef->filename, "Donation Task reward of type 'Numeric' should not specify an unlock '%s'", REF_STRING_FROM_HANDLE(taskReward->unlockDef));
                bSuccess = false;
            }
        }
        else if ( taskReward->rewardType == DonationTaskRewardType_Unlock )
        {
            if (!GET_REF(taskReward->unlockDef) && REF_STRING_FROM_HANDLE(taskReward->unlockDef))
            {
                ErrorFilenamef(taskDef->filename, "Donation Task reward refers to non-existent unlock '%s'", REF_STRING_FROM_HANDLE(taskReward->unlockDef));
                bSuccess = false;
            }
            if ( REF_STRING_FROM_HANDLE(taskReward->numericDef) )
            {
                ErrorFilenamef(taskDef->filename, "Donation Task reward of type 'Unlock' should not specify a numeric '%s'", REF_STRING_FROM_HANDLE(taskReward->numericDef));
                bSuccess = false;
            }
        }
    }

    // Only validate reward tables on the gameserver, since they are not loaded by the group project server.
#ifdef GAMESERVER
    if (!GET_REF(taskDef->completionRewardTable) && REF_STRING_FROM_HANDLE(taskDef->completionRewardTable))
    {
        ErrorFilenamef(taskDef->filename, "Donation Task refers to non-existent reward table '%s'", REF_STRING_FROM_HANDLE(taskDef->completionRewardTable));
        bSuccess = false;
    }

    if (taskDef->activityEventRequirement && !EventDef_Find(taskDef->activityEventRequirement))
    {
        ErrorFilenamef(taskDef->filename, "Donation Task refers to non-existent event def '%s'", taskDef->activityEventRequirement);
        bSuccess = false;
    }
#endif

    return bSuccess;
}

bool 
DonationTask_Validate(DonationTaskDef* taskDef)
{
    bool bSuccess = true;

    if (!DonationTask_ValidateAllButRefs(taskDef))
    {
        bSuccess = false;
    }

    if (!DonationTask_ValidateRefs(taskDef))
    {
        bSuccess = false;
    }

    return bSuccess;
}

static int 
DonationTaskResValidateCB(enumResourceValidateType eType, const char *dictName, const char *resourceName, DonationTaskDef *taskDef, U32 userID)
{
    switch (eType)
    {	
        // Called for filename check
    case RESVALIDATE_FIX_FILENAME: 
        resFixPooledFilename(&taskDef->filename, DONATION_TASK_BASE_DIR, taskDef->scope, taskDef->name, DONATION_TASK_EXT);
        return VALIDATE_HANDLED;

        // Called after load/reload but before binning
    case RESVALIDATE_POST_TEXT_READING:
        DonationTask_ValidateAllButRefs(taskDef);
        return VALIDATE_HANDLED;

        // Called when all data has been loaded
    case RESVALIDATE_CHECK_REFERENCES:
        if (IsServer() && !isProductionMode())
        {
            DonationTask_ValidateRefs(taskDef);
            return VALIDATE_HANDLED;
        }
        break;
    }
    return VALIDATE_NOT_HANDLED;
}

//////////////////////////////////////////////////////////////////////////
//
// Donation Task Discount Validation
//
//////////////////////////////////////////////////////////////////////////
static bool 
DonationTaskDiscount_ValidateAllButRefs(DonationTaskDiscountDef* discountDef)
{
    bool bSuccess = true;

    if (!resIsValidName(discountDef->name))
    {
        ErrorFilenamef(discountDef->filename, "Donation Task Discount name is illegal: '%s'", discountDef->name);
        bSuccess = false;
    }

    if (!resIsValidScope(discountDef->scope))
    {
        ErrorFilenamef(discountDef->filename, "Donation Task Discount scope is illegal: '%s'", discountDef->scope);
        bSuccess = false;
    }

    if (discountDef->discountPercent < 0 || discountDef->discountPercent > 1)
    {
        ErrorFilenamef(discountDef->filename, "Donation Task Discount '%s' has a discount percent out of the range [0, 1]", discountDef->name);
        bSuccess = false;
    }

    return bSuccess;
}

static bool 
DonationTaskDiscount_ValidateRefs(DonationTaskDiscountDef* discountDef)
{
    bool bSuccess = true;

#ifdef GAMESERVER
    // Validate messages.
    if (!GET_REF(discountDef->displayNameMsg.hMessage) && REF_STRING_FROM_HANDLE(discountDef->displayNameMsg.hMessage))
    {
        ErrorFilenamef(discountDef->filename, "Donation Task Discount refers to non-existent display name message '%s'", REF_STRING_FROM_HANDLE(discountDef->displayNameMsg.hMessage));
        bSuccess = false;
    }
    if (!GET_REF(discountDef->descriptionMsg.hMessage) && REF_STRING_FROM_HANDLE(discountDef->descriptionMsg.hMessage))
    {
        ErrorFilenamef(discountDef->filename, "Donation Task Discount refers to non-existent description message '%s'", REF_STRING_FROM_HANDLE(discountDef->descriptionMsg.hMessage));
        bSuccess = false;
    }
#endif // GAMESERVER

    return bSuccess;
}

bool 
DonationTaskDiscount_Validate(DonationTaskDiscountDef* taskDiscountDef)
{
    bool bSuccess = true;

    if (!DonationTaskDiscount_ValidateAllButRefs(taskDiscountDef))
    {
        bSuccess = false;
    }

    if (!DonationTaskDiscount_ValidateRefs(taskDiscountDef))
    {
        bSuccess = false;
    }

    return bSuccess;
}

static int 
DonationTaskDiscountResValidateCB(enumResourceValidateType eType, const char *dictName, const char *resourceName, DonationTaskDiscountDef *discountDef, U32 userID)
{
    switch (eType)
    {
        // Called for filename check
    case RESVALIDATE_FIX_FILENAME: 
        resFixPooledFilename(&discountDef->filename, DONATION_TASK_DISCOUNT_BASE_DIR, discountDef->scope, discountDef->name, DONATION_TASK_DISCOUNT_EXT);
        return VALIDATE_HANDLED;

        // Called after load/reload but before binning
    case RESVALIDATE_POST_TEXT_READING:
        DonationTaskDiscount_ValidateAllButRefs(discountDef);
        return VALIDATE_HANDLED;

        // Called when all data has been loaded
    case RESVALIDATE_CHECK_REFERENCES:
        if (IsServer() && !isProductionMode())
        {
            DonationTaskDiscount_ValidateRefs(discountDef);
            return VALIDATE_HANDLED;
        }
    }
    return VALIDATE_NOT_HANDLED;
}

//////////////////////////////////////////////////////////////////////////
//
// Group Project Bonus Validation
//
//////////////////////////////////////////////////////////////////////////
static bool 
GroupProjectBonus_ValidateAllButRefs(GroupProjectBonusDef* bonusDef)
{
    bool bSuccess = true;

    if (!resIsValidName(bonusDef->name))
    {
        ErrorFilenamef(bonusDef->filename, "Group Project Bonus name is illegal: '%s'", bonusDef->name);
        bSuccess = false;
    }

    if (!resIsValidScope(bonusDef->scope))
    {
        ErrorFilenamef(bonusDef->filename, "Group Project Bonus scope is illegal: '%s'", bonusDef->scope);
        bSuccess = false;
    }

    return bSuccess;
}

static bool 
GroupProjectBonus_ValidateRefs(GroupProjectBonusDef* bonusDef)
{
    bool bSuccess = true;

#ifdef GAMESERVER
    // Validate messages.
    if (!GET_REF(bonusDef->displayNameMsg.hMessage) && REF_STRING_FROM_HANDLE(bonusDef->displayNameMsg.hMessage))
    {
        ErrorFilenamef(bonusDef->filename, "Group Project Bonus refers to non-existent display name message '%s'", REF_STRING_FROM_HANDLE(bonusDef->displayNameMsg.hMessage));
        bSuccess = false;
    }
    if (!GET_REF(bonusDef->descriptionMsg.hMessage) && REF_STRING_FROM_HANDLE(bonusDef->descriptionMsg.hMessage))
    {
        ErrorFilenamef(bonusDef->filename, "Group Project Bonus refers to non-existent description message '%s'", REF_STRING_FROM_HANDLE(bonusDef->descriptionMsg.hMessage));
        bSuccess = false;
    }
#endif // GAMESERVER

    return bSuccess;
}

bool 
GroupProjectBonus_Validate(GroupProjectBonusDef* bonusDef)
{
    bool bSuccess = true;

    if (!GroupProjectBonus_ValidateAllButRefs(bonusDef))
    {
        bSuccess = false;
    }

    if (!GroupProjectBonus_ValidateRefs(bonusDef))
    {
        bSuccess = false;
    }

    return bSuccess;
}

static int 
GroupProjectBonusResValidateCB(enumResourceValidateType eType, const char *dictName, const char *resourceName, GroupProjectBonusDef *bonusDef, U32 userID)
{
    switch (eType)
    {
        // Called for filename check
    case RESVALIDATE_FIX_FILENAME: 
        resFixPooledFilename(&bonusDef->filename, GROUP_PROJECT_BONUS_BASE_DIR, bonusDef->scope, bonusDef->name, GROUP_PROJECT_BONUS_EXT);
        return VALIDATE_HANDLED;

        // Called after load/reload but before binning
    case RESVALIDATE_POST_TEXT_READING:
        GroupProjectBonus_ValidateAllButRefs(bonusDef);
        return VALIDATE_HANDLED;

        // Called when all data has been loaded
    case RESVALIDATE_CHECK_REFERENCES:
        if (IsServer() && !isProductionMode())
        {
            GroupProjectBonus_ValidateRefs(bonusDef);
            return VALIDATE_HANDLED;
        }
    }
    return VALIDATE_NOT_HANDLED;
}

DefineContext *s_DefineGroupProjectTaskSlotTypes = NULL;
// Item tags
AUTO_STARTUP(GroupProjectTaskSlotTypes);
void 
GroupProject_LoadTaskSlotTypes(void)
{
    s_DefineGroupProjectTaskSlotTypes = DefineCreate();
    DefineLoadFromFile(s_DefineGroupProjectTaskSlotTypes, "GroupProjectTaskSlotType", "GroupProjectTaskSlotTypes", NULL,  GROUP_PROJECT_TASK_SLOT_TYPE_FILE, "GroupProjectTaskSlotTypes.bin", TaskSlotType_MAX);
}

DefineContext *s_DefineDonationTaskCategoryTypes = NULL;
// Item tags
AUTO_STARTUP(DonationTaskCategoryTypes);
void 
GroupProject_LoadDonationTaskCategoryTypes(void)
{
    s_DefineDonationTaskCategoryTypes = DefineCreate();
    DefineLoadFromFile(s_DefineDonationTaskCategoryTypes, "DonationTaskCategoryType", "DonationTaskCategoryTypes", NULL,  DONATION_TASK_CATEGORY_TYPE_FILE, "DonationTaskCategoryTypes.bin", DonationTaskCategory_MAX);
}

static void
GroupProjectLevelDefReload(const char *pchPath, S32 iWhen)
{
	loadstart_printf("Loading GroupProjectLevelTree... ");
	if (pchPath)
		fileWaitForExclusiveAccess(pchPath);
	StructReset(parse_GroupProjectLevelTreeDef, &g_GroupProjectLevelTreeDef);
	ParserLoadFiles(NULL, "defs/config/GroupProjectLevelTree.def", "GroupProjectLevelTree.bin", PARSER_OPTIONALFLAG, parse_GroupProjectLevelTreeDef, &g_GroupProjectLevelTreeDef);
	loadend_printf("Done. (%d)", eaSize(&g_GroupProjectLevelTreeDef.eaLevelNodes));
}

AUTO_STARTUP(GroupProjectLevelTreeDef) ASTRT_DEPS(GroupProjects);
void
GroupProjectLevelTreeDefStartup(void)
{
	GroupProjectLevelDefReload(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/GroupProjectLevelTree.def", GroupProjectLevelDefReload);
}

static void
InitReferenceDict(const char *dictName, DictionaryHandle *dict, ParseTable *parseTable, resCallback_Validate validateCB)
{
    // Set up reference dictionaries
    *dict = RefSystem_RegisterSelfDefiningDictionary(dictName, false, parseTable, true, true, NULL);

    resDictManageValidation(*dict, validateCB);

    if (IsServer())
    {
        // Servers provide missing resources to clients.
        resDictProvideMissingResources(*dict);
        if (IsGameServerBasedType() && (isDevelopmentMode() || isProductionEditMode())) 
        {
            resDictMaintainInfoIndex(*dict, ".Name", ".Scope", NULL, NULL, NULL);
        }
    } 
    else
    {
        // Clients request missing resources from servers.
        resDictRequestMissingResources(*dict, 8, false, resClientRequestSendReferentCommand);
    }
}

AUTO_RUN;
void
GroupProject_Init(void)
{
    InitReferenceDict("GroupProjectNumericDef", &g_GroupProjectNumericDict, parse_GroupProjectNumericDef, GroupProjectNumericResValidateCB);
    InitReferenceDict("GroupProjectUnlockDef", &g_GroupProjectUnlockDict, parse_GroupProjectUnlockDef, GroupProjectUnlockResValidateCB);
    InitReferenceDict("DonationTaskDef", &g_DonationTaskDict, parse_DonationTaskDef, DonationTaskResValidateCB);
    InitReferenceDict("DonationTaskDiscountDef", &g_DonationTaskDiscountDict, parse_DonationTaskDiscountDef, DonationTaskDiscountResValidateCB);
    InitReferenceDict("GroupProjectBonusDef", &g_GroupProjectBonusDict, parse_GroupProjectBonusDef, GroupProjectBonusResValidateCB);
    InitReferenceDict("GroupProjectDef", &g_GroupProjectDict, parse_GroupProjectDef, GroupProjectResValidateCB);

    DonationTaskExprContextInit();
}

AUTO_STARTUP(GroupProjects) ASTRT_DEPS(GroupProjectTaskSlotTypes, DonationTaskCategoryTypes, ItemTags, ItemEval);
void 
GroupProject_Load(void)
{
    if (!IsClient()) 
    {
        // Servers load resources from disk.
        resLoadResourcesFromDisk(g_GroupProjectNumericDict,
            GROUP_PROJECT_NUMERIC_BASE_DIR,
            "."GROUP_PROJECT_NUMERIC_EXT,
            NULL,
            RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
        resLoadResourcesFromDisk(g_GroupProjectUnlockDict,
            GROUP_PROJECT_UNLOCK_BASE_DIR,
            "."GROUP_PROJECT_UNLOCK_EXT,
            NULL,
            RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
        resLoadResourcesFromDisk(g_DonationTaskDiscountDict,
            DONATION_TASK_DISCOUNT_BASE_DIR,
            "."DONATION_TASK_DISCOUNT_EXT,
            NULL,
            RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
        resLoadResourcesFromDisk(g_GroupProjectBonusDict,
            GROUP_PROJECT_BONUS_BASE_DIR,
            "."GROUP_PROJECT_BONUS_EXT,
            NULL,
            RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
        resLoadResourcesFromDisk(g_DonationTaskDict,
            DONATION_TASK_BASE_DIR,
            "."DONATION_TASK_EXT,
            NULL,
            RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
        resLoadResourcesFromDisk(g_GroupProjectDict,
            GROUP_PROJECT_BASE_DIR,
            "."GROUP_PROJECT_EXT,
            NULL,
            RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);

        BuildUnlockNumericMapping();
    }
}

int
GroupProject_NumTaskSlots(GroupProjectDef *projectDef)
{
    return eaiSize(&projectDef->slotTypes);
}

GroupProjectConstant*
GroupProject_FindConstant(const GroupProjectDef *projectDef, const char *key)
{
	if (projectDef)
	{
		const char* keyPooled = allocFindString(key);
		int i;
		for (i = eaSize(&projectDef->constants)-1; i >= 0; i--)
		{
			if (projectDef->constants[i]->key == keyPooled)
			{
				return projectDef->constants[i];
			}
		}
	}
	return NULL;
}

int
DonationTask_FindRequirement(DonationTaskDef *taskDef, const char *requirementName)
{
	if (taskDef)
	{
		const char* requirementNamePooled = allocFindString(requirementName);
		int i;
		for (i = eaSize(&taskDef->buckets)-1; i >= 0; i--)
		{
			if (taskDef->buckets[i]->name == requirementNamePooled)
			{
				return i;
			}
		}
	}
	return -1;
}

bool
DonationTask_ItemMatchesExpressionRequirement(unsigned int iParitionIdx, Entity *pEnt, const GroupProjectDonationRequirement *taskBucket, Item *item)
{
    MultiVal mv = {0};
    ItemDef *itemDef;

    if ( ( item == NULL ) || ( taskBucket == NULL ) || !devassert(taskBucket->specType == DonationSpecType_Expression) )
    {
        return false;
    }

    itemDef = GET_REF(item->hItem);
    if ( itemDef == NULL )
    {
        return false;
    }

    // The item must have one of the required item categories
    if ( eaiSize(&taskBucket->requiredItemCategories) && !itemdef_HasItemCategory(itemDef, taskBucket->requiredItemCategories) )
    {
        return false;
    }

    // The item cannot have a restrict item category
    if (itemdef_HasItemCategory(itemDef, taskBucket->restrictItemCategories))
    {
        return false;
    }

    // If the expression exists, then the item must also match the expression.
    if ( taskBucket->allowedItemExpr )
    {
		itemeval_Eval(iParitionIdx,
            taskBucket->allowedItemExpr,
            itemDef,
            NULL,
            item,
            pEnt,
            item_GetLevel(item),
            item_GetQuality(item),
            0,
            itemDef->pchFileName,
            -1,
            &mv);
        return MultiValToBool(&mv);
    }

    return true;
}

bool
GroupProject_GetNumericFromPlayerExprHelper(Entity *playerEnt, GroupProjectType projectType, const char *projectName, const char *numericName, int *pValueOut, char **errString)
{
    GroupProjectState *projectState;
    GroupProjectNumericData *numericData;
    GroupProjectContainer *projectContainer;

    if ( playerEnt == NULL )
    {
        estrPrintf(errString, "Player not found");
        return false;
    }

    // Make sure the player is subscribed to the group project Container.
    GroupProject_ValidateContainer(playerEnt, projectType);

    // Get the GroupProjectContainer for this player and project type.
    projectContainer = GroupProject_ResolveContainer(playerEnt, projectType);
    if ( projectContainer == NULL )
    {
        // No container, so return default numeric value.
        *pValueOut = 0;
        return true;
    }

    // Find the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( projectState == NULL )
    {
        estrPrintf(errString, "GroupProjectState not found");
        return false;
    }

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
                *pValueOut = 0;
                return true;
            }
        }
        estrPrintf(errString, "GroupProjectNumericData %s not found", numericName);
        return false;
    }

    // Return the value.
    *pValueOut = numericData->numericVal;
    return true;
}

bool
GroupProject_GetUnlockFromPlayerExprHelper(Entity *playerEnt, GroupProjectType projectType, const char *projectName, const char *unlockName, int *pValueOut, char **errString)
{
    GroupProjectState *projectState;
    GroupProjectUnlockDefRef *unlockDefRef;
    GroupProjectContainer *projectContainer;

    if ( playerEnt == NULL )
    {
        estrPrintf(errString, "Player not found");
        return false;
    }

    // Make sure the player is subscribed to the group project Container.
    GroupProject_ValidateContainer(playerEnt, projectType);

    // Get the GroupProjectContainer for this player and project type.
    projectContainer = GroupProject_ResolveContainer(playerEnt, projectType);
    if ( projectContainer == NULL )
    {
        // No container, so return default numeric value.
        *pValueOut = 0;
        return true;
    }

    // Find the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( projectState == NULL )
    {
        estrPrintf(errString, "GroupProjectState not found");
        return false;
    }

    // Find the unlock.
    unlockDefRef = eaIndexedGetUsingString(&projectState->unlocks, unlockName);

    // Return whether the unlock is set.
    *pValueOut = ( unlockDefRef != NULL );

    return true;
}

static S32 
GroupProjectUnlockDef_SortByNumericUnlockValue(const GroupProjectUnlockDef **ppUnlockL, const GroupProjectUnlockDef **ppUnlockR, const void *pContext)
{
	return (*ppUnlockL)->triggerValue - (*ppUnlockR)->triggerValue;
}

S32
GroupProject_GetLevelTreeCount(const GroupProjectState *pState, const char *pchHint, GroupProjectLevelTreeCount eFlags)
{
    static GroupProjectUnlockDef **s_eaNumericUnlocks;
	static GroupProjectLevelTreeNodeDef **s_eaLevelTreeNodes;
    GroupProjectDef *pGroupProject = pState ? GET_REF(pState->projectDef) : NULL;
    GroupProjectNumericDef *pUnlockNumericDef = NULL;
    S32 i, j, iCurrentNumericValue = 0, iCount = 0;

    if (pGroupProject)
    {
        pchHint = allocAddString(pchHint);

        for (i = 0; i < eaSize(&g_GroupProjectLevelTreeDef.eaLevelNodes); i++)
        {
            if (pchHint == g_GroupProjectLevelTreeDef.eaLevelNodes[i]->pchHint)
            {
                GroupProjectLevelTreeNodeDef *pNodeDef = g_GroupProjectLevelTreeDef.eaLevelNodes[i];
                GroupProjectUnlockDefRef *pNumericUnlockRef = pNodeDef->pchNumericUnlock ? eaIndexedGetUsingString(&pGroupProject->unlockDefs, pNodeDef->pchNumericUnlock) : NULL;
                GroupProjectUnlockDef *pNumericUnlock = pNumericUnlockRef ? GET_REF(pNumericUnlockRef->unlockDef) : NULL;
                GroupProjectNumericDef *pUnlockNumeric = pNumericUnlock && pNumericUnlock->type == UnlockType_NumericValueEqualOrGreater ? GET_REF(pNumericUnlock->numeric) : NULL;

                if (pNumericUnlock && pUnlockNumeric)
                    pUnlockNumericDef = pUnlockNumeric;
            }
        }

        for (i = 0; i < eaSize(&g_GroupProjectLevelTreeDef.eaLevelNodes); i++)
        {
            GroupProjectLevelTreeNodeDef *pNodeDef = g_GroupProjectLevelTreeDef.eaLevelNodes[i];
            GroupProjectUnlockDefRef *pNumericUnlockRef = pNodeDef->pchNumericUnlock ? eaIndexedGetUsingString(&pGroupProject->unlockDefs, pNodeDef->pchNumericUnlock) : NULL;
            GroupProjectUnlockDef *pNumericUnlock = pNumericUnlockRef ? GET_REF(pNumericUnlockRef->unlockDef) : NULL;
            GroupProjectNumericDef *pUnlockNumeric = pNumericUnlock && pNumericUnlock->type == UnlockType_NumericValueEqualOrGreater ? GET_REF(pNumericUnlock->numeric) : NULL;

            if (pNumericUnlock && pUnlockNumeric)
            {
                if (pUnlockNumericDef == pUnlockNumeric)
                {
                    eaPush(&s_eaNumericUnlocks, pNumericUnlock);
                    eaPush(&s_eaLevelTreeNodes, pNodeDef);
                }
            }
        }
    }

    if (pState && pUnlockNumericDef)
    {
        GroupProjectNumericData *pData = eaIndexedGetUsingString(&pState->numericData, pUnlockNumericDef->name);
        iCurrentNumericValue = pData ? pData->numericVal : 0;
    }

    eaStableSort(s_eaNumericUnlocks, NULL, GroupProjectUnlockDef_SortByNumericUnlockValue);

    for (j = 0; j < eaSize(&s_eaNumericUnlocks); j++)
    {
        GroupProjectLevelTreeNodeDef *pNodeDef = NULL;

        if (eFlags & kGroupProjectLevelTreeCount_ManualNodes)
        {
            for (i = 0; i < eaSize(&s_eaLevelTreeNodes); i++)
            {
                if (s_eaLevelTreeNodes[i]->pchNumericUnlock == s_eaNumericUnlocks[j]->name)
                {
                    pNodeDef = s_eaLevelTreeNodes[i];
                    break;
                }
            }
        }

        if (iCurrentNumericValue >= s_eaNumericUnlocks[j]->triggerValue)
        {
            if (eFlags & kGroupProjectLevelTreeCount_ManualNodes)
            {
                GroupProjectUnlockDefRef *pManualUnlockRef = pNodeDef->pchManualUnlock ? eaIndexedGetUsingString(&pGroupProject->unlockDefs, pNodeDef->pchManualUnlock) : NULL;
                GroupProjectUnlockDef *pManualUnlock = pManualUnlockRef ? GET_REF(pManualUnlockRef->unlockDef) : NULL;
                GroupProjectUnlockDefRefContainer *pUnlock = pManualUnlock && pState ? eaIndexedGetUsingString(&pState->unlocks, pManualUnlock->name) : NULL;
                if (pUnlock)
                    iCount++;
            }
            else if (eFlags & kGroupProjectLevelTreeCount_NumericNodes)
                iCount++;
        }
    }

    eaClearFast(&s_eaNumericUnlocks);
    eaClearFast(&s_eaLevelTreeNodes);
    return iCount;
}

void 
GroupProject_UpdateBucketQuantities(const GroupProjectContainer *pContainer, const GroupProjectState *pState, const GroupProjectDef *pGroupProject, const DonationTaskDef *pDef, const DonationTaskSlot *pSlot, S32 *piCurrentBucketQuantityOut, S32 *piTotalBucketQuantityOut) 
{
	static U32 *s_ea32UnweightedDonations;
	int i, j;
	U32 iUnweightedRequirement = 0;

	for (i = eaSize(&pSlot->buckets) - 1; i >= 0; i--)
	{
		DonationTaskBucketData *pBucket = pSlot->buckets[i];
		GroupProjectDonationRequirement *pRequirement = NULL;
		S32 iDonationCount, iFillCount;
		U32 iRequirementCount;
		F32 fItemWeight = 1;

		for (j = eaSize(&pDef->buckets) - 1; j >= 0; j--)
		{
			if (pDef->buckets[j]->name == pBucket->bucketName)
			{
				pRequirement = pDef->buckets[j];
				break;
			}
		}

		if (!pRequirement || pRequirement->count < 1)
			continue;

		iRequirementCount = GroupProject_GetDiscountedBucketRequirement(pContainer, pState, pSlot, pBucket->bucketName);

		iDonationCount = pRequirement->donationIncrement > 0 ? pRequirement->donationIncrement : 1;
		iFillCount = MIN(pBucket->donationCount, iRequirementCount);

		if (pRequirement->specType == DonationSpecType_Item)
		{
			ItemDef *pItemDef = GET_REF(pRequirement->requiredItem);
			if (pItemDef && pItemDef->eType == kItemType_Numeric)
				fItemWeight = pItemDef->fScaleUI;
		}

		if (pGroupProject)
		{
			GroupProjectConstant *pWeight = GroupProject_FindConstant(pGroupProject, pRequirement->contributionConstant);
			S32 iWeight = pWeight && pWeight->value > 0 ? pWeight->value : 1;
			if (pWeight && pWeight->value > 0)
			{
				*piCurrentBucketQuantityOut += iFillCount * iWeight / iDonationCount;
				*piTotalBucketQuantityOut += iRequirementCount * iWeight / iDonationCount;
				iUnweightedRequirement += iRequirementCount;
			}
			else
			{
				ea32Push(&s_ea32UnweightedDonations, iFillCount);
				ea32Push(&s_ea32UnweightedDonations, iRequirementCount);
				iUnweightedRequirement += iRequirementCount;
			}
		}
		else
		{
			ea32Push(&s_ea32UnweightedDonations, iFillCount);
			ea32Push(&s_ea32UnweightedDonations, iRequirementCount);
			iUnweightedRequirement += iRequirementCount;
		}
	}

	if (ea32Size(&s_ea32UnweightedDonations) > 0)
	{
		// For donations that don't have a weight, treat it as an equivalent
		// amount to the average requirement.
		iUnweightedRequirement /= eaSize(&pSlot->buckets);
		for (i = 0; i < ea32Size(&s_ea32UnweightedDonations); i += 2)
		{
			U32 iDonationCount = s_ea32UnweightedDonations[i];
			U32 iRequirementCount = s_ea32UnweightedDonations[i + 1];
			F64 fMod = (F64)iUnweightedRequirement / (F64)iRequirementCount;
			if (fMod < 1)
				fMod = 1;
			*piCurrentBucketQuantityOut += (U32)(iDonationCount * fMod);
			*piTotalBucketQuantityOut += (U32)(iRequirementCount * fMod);
		}
		ea32ClearFast(&s_ea32UnweightedDonations);
	}
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_CheckForDiscount(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, ATH_ARG NOCONST(GroupProjectState) *projectState, const char *discountName)
{
    if ( ISNULL(discountName) )
    {
        return false;
    }

    if ( NONNULL(projectContainer) && eaIndexedFindUsingString(&projectContainer->discountList, discountName) >= 0 )
    {
        return true;
    }

    return false;
}

AUTO_TRANS_HELPER;
U32
GroupProject_trh_GetDiscountedTaskBucketRequirement(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, ATH_ARG NOCONST(GroupProjectState) *projectState, DonationTaskDef *taskDef, const char *bucketName)
{
    GroupProjectDonationRequirement *requirement;
    U32 count;
    int i, requirementIdx;
    F32 totalDiscountPercent = 0;

    if ( taskDef == NULL || taskDef->noCost )
    {
        return GROUP_PROJECT_INVALID_DISCOUNTED_BUCKET_REQUIREMENT;
    }

    requirementIdx = DonationTask_FindRequirement(taskDef, bucketName);
    requirement = eaGet(&taskDef->buckets, requirementIdx);
    if ( requirement == NULL )
    {
        return GROUP_PROJECT_INVALID_DISCOUNTED_BUCKET_REQUIREMENT;
    }

    count = requirement->count;

    for ( i = eaSize(&requirement->discounts) - 1; i >= 0; i-- )
    {
        if ( GroupProject_trh_CheckForDiscount(ATR_PASS_ARGS, projectContainer, projectState, requirement->discounts[i]) )
        {
            DonationTaskDiscountDef *discountDef = RefSystem_ReferentFromString(g_DonationTaskDiscountDict, requirement->discounts[i]);
            U32 discountAmount = 0;

            if ( discountDef == NULL )
            {
                continue;
            }

            switch ( discountDef->discountType )
            {
            case DonationDiscountType_Percentage:
                // a discount against a percentage of the original
                totalDiscountPercent += discountDef->discountPercent;
                break;

            case DonationDiscountType_Absolute:
                // a fixed discount
                discountAmount = discountDef->discountAmount;
                break;
            }

            count -= MIN(count, discountAmount);
        }
    }

    if ( totalDiscountPercent > 0 )
    {
        U32 discountAmount = floorf(requirement->count * totalDiscountPercent);
        count -= MIN(count, discountAmount);
    }

    return count;
}

AUTO_TRANS_HELPER;
U32
GroupProject_trh_GetDiscountedBucketRequirement(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, ATH_ARG NOCONST(GroupProjectState) *projectState, ATH_ARG NOCONST(DonationTaskSlot) *slot, const char *bucketName)
{
    DonationTaskDef *taskDef;

    if ( ISNULL(slot) )
    {
        return GROUP_PROJECT_INVALID_DISCOUNTED_BUCKET_REQUIREMENT;
    }

    taskDef = GET_REF(slot->taskDef);
    if ( taskDef == NULL )
    {
        return GROUP_PROJECT_INVALID_DISCOUNTED_BUCKET_REQUIREMENT;
    }

    return GroupProject_trh_GetDiscountedTaskBucketRequirement(ATR_PASS_ARGS, projectContainer, projectState, taskDef, bucketName);
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_CheckBucketRequirementsComplete(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, ATH_ARG NOCONST(GroupProjectState) *projectState, ATH_ARG NOCONST(DonationTaskSlot) *slot, const char *bucketName)
{
    DonationTaskBucketData *bucket;
    U32 requirementCount;

    if ( ISNULL(slot) )
    {
        return false;
    }

    bucket = eaIndexedGetUsingString(&slot->buckets, bucketName);
    if ( bucket == NULL )
    {
        return false;
    }

    requirementCount = GroupProject_trh_GetDiscountedBucketRequirement(ATR_PASS_ARGS, projectContainer, projectState, slot, bucketName);
    if ( requirementCount == GROUP_PROJECT_INVALID_DISCOUNTED_BUCKET_REQUIREMENT )
    {
        return false;
    }

    if ( bucket->donationCount >= requirementCount )
    {
        return true;
    }

    return false;
}
// Returns true if the given DonationTaskDef passes the DonationTaskFilter
bool
GroupProject_TaskFilterCheck(int iPartitionIdx, DonationTaskDef *pTask, Entity *pEnt, GroupProjectState *pState, const DonationTaskFilter *pFilter, int slotNum, bool *pbAvailableOut, DonationTaskCategoryType **ppeCategoriesOut)
{
	bool bAvailable = false;

	const GroupProjectDef *pGroupProjectDef = GET_REF(pState->projectDef);

	if (pTask == NULL || pFilter == NULL)
		return false;

	if (pFilter->bRequireAvailableForNewProject && !pTask->taskAvailableForNewProject)
		return false;

	// Evaluate whether the donation task is currently allowed.
	switch (pGroupProjectDef->type)
	{
		xcase GroupProjectType_Guild:
			bAvailable = GuildProject_DonationTaskAllowed(iPartitionIdx, pEnt, pState, pTask, slotNum);
		xcase GroupProjectType_Player:
			bAvailable = PlayerProject_DonationTaskAllowed(iPartitionIdx, pEnt, pState, pTask, slotNum);
	}

	if (pbAvailableOut)
		*pbAvailableOut = bAvailable;
		
	if(!pFilter->bIncludeUnavailableTasks && !bAvailable)
		return false;

	if (pFilter)
	{
		if (pFilter->eSlotType != TaskSlotType_None && pTask->slotType != pFilter->eSlotType)
			return false;

		if (pFilter->pchNameFilter && *pFilter->pchNameFilter)
		{
			const char *pchName = TranslateDisplayMessage(pTask->displayNameMsg);
			if (pchName && !strstri(pchName, pFilter->pchNameFilter))
				return false;
		}
		if (pFilter->pchDescriptionFilter && *pFilter->pchDescriptionFilter)
		{
			const char *pchDescription = TranslateDisplayMessage(pTask->descriptionMsg);
			if (pchDescription && !strstri(pchDescription, pFilter->pchDescriptionFilter))
				return false;
		}
	}

	if (ppeCategoriesOut)
		eaiPushUnique(ppeCategoriesOut, pTask->category);
	if (pFilter && eaiFind(&pFilter->peCategoryMask, pTask->category) >= 0)
		return false;

	return true;
}

ItemDef*
GroupProject_FindItemDef(const GroupProjectDef *pGroupProject, const char *pchNumericPattern)
{
	S32 i, j;

	if (pGroupProject)
	{
		GroupProjectDonationRequirement *pBucket = NULL;

		for (i = 0; i < eaSize(&pGroupProject->donationTaskDefs); i++)
		{
			DonationTaskDef *pTaskDef = GET_REF(pGroupProject->donationTaskDefs[i]->taskDef);
			if (!pTaskDef || pTaskDef->noCost )
				continue;

			for (j = 0; j < eaSize(&pTaskDef->buckets); j++)
			{
				ItemDef *pItemDef;

				if (pTaskDef->buckets[j]->specType != DonationSpecType_Item)
					continue;

				pItemDef = GET_REF(pTaskDef->buckets[j]->requiredItem);
				if (!pItemDef)
					continue;

				if (isWildcardMatch(pchNumericPattern, pItemDef->pchName, false, true))
					return pItemDef;
			}
		}
	}

	return NULL;
}

#include "AutoGen/GroupProjectCommon_h_ast.c"
