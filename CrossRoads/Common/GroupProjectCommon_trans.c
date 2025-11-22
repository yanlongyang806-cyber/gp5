#include "GroupProjectCommon_trans.h"
#include "GroupProjectCommon.h"
#include "stdtypes.h"
#include "itemCommon.h"
#include "earray.h"
#include "GlobalTypeEnum.h"
#include "AutoTransDefs.h"
#include "ReferenceSystem.h"
#include "timing.h"
#include "logging.h"

#include "AutoGen/GroupProjectCommon_h_ast.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"

AUTO_TRANS_HELPER;
bool
GroupProject_trh_GetProjectConstant(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState, const char *constantName, S32 *valueOut)
{
    GroupProjectConstant *constant;
    GroupProjectDef *projectDef;

    if ( ISNULL(valueOut) )
    {
        return false;
    }

    if ( ISNULL(projectState) || ISNULL(constantName) )
    {
        *valueOut = 0;
        return false;
    }

    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        *valueOut = 0;
        return false;
    }

	constant = GroupProject_FindConstant(projectDef, constantName);
    if ( ISNULL(constant) )
    {
        *valueOut = 0;
        return false;
    }

    *valueOut = constant->value;
    return true;
}

AUTO_TRANS_HELPER;
void
GroupProject_trh_FinalizeTask(ATR_ARGS, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, GroupProjectDef *projectDef, DonationTaskDef *taskDef)
{
    if ( projectDef->type == GroupProjectType_Player && IS_HANDLE_ACTIVE(taskDef->finalizationRewardTable) )
    {
        taskSlot->state = DonationTaskState_FinalizedRewardPending;
    }
    else 
    {
        taskSlot->state = DonationTaskState_Finalized;
    }
    taskSlot->finalizedTime = timeSecondsSince2000();
    taskSlot->completionTime = taskSlot->finalizedTime + taskDef->secondsToComplete;

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FinalizeTask", 
        "ProjectName %s TaskSlot %d TaskName %s", projectDef->name, taskSlot->taskSlotNum, taskDef->name);
    return;
}

AUTO_TRANS_HELPER;
void
GroupProject_trh_CancelTask(ATR_ARGS, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, DonationTaskDef *taskDef, const char *projectName)
{
    taskSlot->state = DonationTaskState_Canceled;
    taskSlot->finalizedTime = timeSecondsSince2000();
    taskSlot->completionTime = taskSlot->finalizedTime;

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "CancelTask",
        "ProjectName %s TaskSlot %d TaskName %s", projectName, taskSlot->taskSlotNum, taskDef->name);
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_AddProject(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, const char *projectName)
{
    NOCONST(GroupProjectState) *projectState;
    GroupProjectDef *projectDef;
    int i;

    // Don't re-add the project if it already exists.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( NONNULL(projectState) )
    {
        return false;
    }

    // Create the project state struct.
    projectState = StructCreateNoConst(parse_GroupProjectState);

    // Set the reference to the def for this project.
    SET_HANDLE_FROM_STRING(g_GroupProjectDict, projectName, projectState->projectDef);

    // Get the project def.  Create fails if the project def does not exist.
    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        StructDestroyNoConst(parse_GroupProjectState, projectState);
        return false;
    }

    if ( GroupProject_ContainerTypeForProjectType(projectDef->type) != projectContainer->containerType )
    {
        // The project type does not match the container type.
        StructDestroyNoConst(parse_GroupProjectState, projectState);
        return false;
    }

    // Initialize the donation task slots
    for ( i = 0; i < eaiSize(&projectDef->slotTypes); i++ )
    {
        NOCONST(DonationTaskSlot) *slot;

        slot = StructCreateNoConst(parse_DonationTaskSlot);
        slot->state = DonationTaskState_None;
        slot->taskSlotType = projectDef->slotTypes[i];
        slot->taskSlotNum = i;

        eaPush(&projectState->taskSlots, slot);
    }

    // Add the project to the container.
    eaIndexedPushUsingStringIfPossible(&projectContainer->projectList, projectName, projectState);

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "AddProject", 
        "ProjectName %s", projectName);
    return true;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_SetContainerDiscount(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, const char *discountName)
{
    DonationTaskDiscountDef *discountDef = RefSystem_ReferentFromString(g_DonationTaskDiscountDict, discountName);
    NOCONST(DonationTaskDiscountDefRefContainer) *discountDefRefContainer;
    int i, j, k, appliedDiscountChanges = 0;

    discountDefRefContainer = eaIndexedGetUsingString(&projectContainer->discountList, discountName);
    if ( NONNULL(discountDefRefContainer) )
    {
        return true;
    }

    if ( discountDef == NULL )
    {
        return false;
    }

    // Create the unlock.
    discountDefRefContainer = StructCreateNoConst(parse_DonationTaskDiscountDefRefContainer);
    SET_HANDLE_FROM_STRING(g_DonationTaskDiscountDict, discountName, discountDefRefContainer->discountDef);

    // Add the unlock to the project state.
    eaIndexedPushUsingStringIfPossible(&projectContainer->discountList, discountName, discountDefRefContainer);

    // Check if any buckets take advantage of the discount
    for ( i = eaSize(&projectContainer->projectList) - 1; i >= 0; i-- )
    {
        GroupProjectDef *projectDef = GET_REF(projectContainer->projectList[i]->projectDef);
        const char *projectName = REF_STRING_FROM_HANDLE(projectContainer->projectList[i]->projectDef);

        for ( j = eaSize(&projectContainer->projectList[i]->taskSlots) - 1; j >= 0; j-- )
        {
            DonationTaskDef *taskDef = GET_REF(projectContainer->projectList[i]->taskSlots[j]->taskDef);
            U32 complete = 0;
            bool usesDiscount = false;

            if ( taskDef == NULL || taskDef->noCost )
            {
                continue;
            }

            for ( k = eaSize(&taskDef->buckets) - 1; k >= 0; k-- )
            {
                if ( eaFindString(&taskDef->buckets[k]->discounts, discountName) >= 0 )
                {
                    usesDiscount = true;
                    break;
                }
            }

            if ( projectContainer->projectList[i]->taskSlots[j]->state != DonationTaskState_AcceptingDonations )
            {
                continue;
            }

            if ( !usesDiscount )
            {
                continue;
            }

            for ( k = eaSize(&projectContainer->projectList[i]->taskSlots[j]->buckets) - 1; k >= 0; k-- )
            {
                if ( GroupProject_trh_CheckBucketRequirementsComplete(ATR_PASS_ARGS, projectContainer, projectContainer->projectList[i], projectContainer->projectList[i]->taskSlots[j], projectContainer->projectList[i]->taskSlots[j]->buckets[k]->bucketName) )
                {
                    complete++;
                }
            }

            // If the new completed bucket count disagrees with the current completedBuckets, update
            // the completedBuckets tracker.
            if ( projectContainer->projectList[i]->taskSlots[j]->completedBuckets != complete )
            {
                projectContainer->projectList[i]->taskSlots[j]->completedBuckets = complete;
                TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "SetContainerDiscount", 
                    "ProjectName %s TaskSlot %d DiscountName %s completedBuckets=%d", projectName, j, discountName, complete);
                appliedDiscountChanges++;

                // Check to finalize project after updating buckets
                if ( eaUSize(&projectContainer->projectList[i]->taskSlots[j]->buckets) == complete )
                {
                    GroupProject_trh_FinalizeTask(ATR_PASS_ARGS, projectContainer->projectList[i]->taskSlots[j], projectDef, taskDef);
                }
            }
        }
    }

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "SetContainerDiscount", 
        "DiscountName %s appliedDiscountChecks=%d", discountName, appliedDiscountChanges);

    return true;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_SetContainerBonus(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, const char *bonusName)
{
    GroupProjectBonusDef *bonusDef = RefSystem_ReferentFromString(g_GroupProjectBonusDict, bonusName);
    NOCONST(GroupProjectBonusDefRefContainer) *bonusDefRefContainer;

    bonusDefRefContainer = eaIndexedGetUsingString(&projectContainer->bonusList, bonusName);
    if ( NONNULL(bonusDefRefContainer) )
    {
        return true;
    }

    if ( bonusDef == NULL )
    {
        return false;
    }

    // Create the unlock.
    bonusDefRefContainer = StructCreateNoConst(parse_GroupProjectBonusDefRefContainer);
    SET_HANDLE_FROM_STRING(g_GroupProjectBonusDict, bonusName, bonusDefRefContainer->bonusDef);

    // Add the unlock to the project state.
    eaIndexedPushUsingStringIfPossible(&projectContainer->bonusList, bonusName, bonusDefRefContainer);

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "SetContainerBonus", 
        "BonusName %s", bonusName);

    return true;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_SetUnlock(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState, const char *unlockName, GlobalType notifyDestinationType, ContainerID notifyDestinationID)
{
    NOCONST(GroupProjectUnlockDefRefContainer) *unlockDefRefContainer;
    GroupProjectUnlockDef *unlockDefRef;
    GroupProjectDef *projectDef;

    // Check if the unlock has already been set.
    unlockDefRefContainer = eaIndexedGetUsingString(&projectState->unlocks, unlockName);
    if ( NONNULL(unlockDefRefContainer) )
    {
        // The unlock has already been set.
        return true;
    }

    // Get the project def.
    projectDef = GET_REF(projectState->projectDef);
    if ( projectDef == NULL )
    {
        return false;
    }

    // Make sure the unlock is valid for the project.
    unlockDefRef = eaIndexedGetUsingString(&projectDef->unlockDefs, unlockName);
    if ( unlockDefRef == NULL )
    {
        return false;
    }

    // Create the unlock.
    unlockDefRefContainer = StructCreateNoConst(parse_GroupProjectUnlockDefRefContainer);
    SET_HANDLE_FROM_STRING(g_GroupProjectUnlockDict, unlockName, unlockDefRefContainer->unlockDef);

    // Add the unlock to the project state.
    eaIndexedPushUsingStringIfPossible(&projectState->unlocks, unlockName, unlockDefRefContainer);

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "SetUnlock", 
        "ProjectName %s UnlockName %s", projectDef->name, unlockName);

    // Notify the player's gameserver that an unlock has been granted.
    if ( notifyDestinationType == GLOBALTYPE_ENTITYPLAYER )
    {
        QueueRemoteCommand_gslGroupProject_PlayerUnlockGrantedNotify(ATR_RESULT_SUCCESS, notifyDestinationType, notifyDestinationID, notifyDestinationID, projectDef->name, unlockName);
    }
    return true;
}

AUTO_TRANS_HELPER;
S32
GroupProject_trh_ApplyBonusToReward(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, const char *numericName, S32 value)
{
    GroupProjectBonusDef *bonusDef;
    GroupProjectNumericDef *numericDef;
    F32 totalBonusPercent = 0;
    F32 totalBonusAmount = 0;
    S32 valueOut = value;
    int i;
    bool bFoundBonus = false;

    for ( i = 0; i < eaSize(&projectContainer->bonusList); i++ )
    {
        bonusDef = GET_REF(projectContainer->bonusList[i]->bonusDef);
        if ( bonusDef )
        {
            numericDef = GET_REF(bonusDef->numericDef);
            if ( numericDef && numericDef->name == numericName )
            {
                switch ( bonusDef->bonusType )
                {
                    case BonusType_Percentage:
                        totalBonusPercent += bonusDef->value;
                        break;

                    case BonusType_Absolute:
                        totalBonusAmount += bonusDef->value;
                        break;
                }

                bFoundBonus = true;
            }
        }
    }

    if (bFoundBonus)
    {
        valueOut += valueOut * totalBonusPercent;
        valueOut += totalBonusAmount;
    }

    return valueOut;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_ApplyNumeric(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState, const char *numericName, NumericOp op, S32 value, GlobalType notifyDestinationType, ContainerID notifyDestinationID)
{
    NOCONST(GroupProjectNumericData) *numericData;
    GroupProjectDef *projectDef;
    GroupProjectNumericDef *numericDef;
    GroupProjectNumericDefRef *numericDefRef;
    UnlocksForNumeric *unlocksForNumeric;
    int i;
    S32 oldValue = 0;
    S64 finalValue;

    numericData = eaIndexedGetUsingString(&projectState->numericData, numericName);

    projectDef = GET_REF(projectState->projectDef);
    if ( projectDef == NULL )
    {
        return false;
    }

    //XXX - add support for max/min values
    if ( numericData )
    {
        numericDef = GET_REF(numericData->numericDef);

        // Save the old value for logging.
        oldValue = numericData->numericVal;

        if ( op == NumericOp_Add )
        {
            finalValue = numericData->numericVal + value;
        }
        else if ( op == NumericOp_SetTo )
        {
            finalValue = value;
        }
        else
        {
            return false;
        }

    }
    else
    {
        numericDefRef = eaIndexedGetUsingString(&projectDef->validNumerics, numericName);
        if ( numericDefRef == NULL )
        {
            return false;
        }

        numericDef = GET_REF(numericDefRef->numericDef);
        if ( numericDef == NULL )
        {
            return false;
        }

        numericData = StructCreateNoConst(parse_GroupProjectNumericData);
        SET_HANDLE_FROM_STRING(g_GroupProjectNumericDict, numericName, numericData->numericDef);
        finalValue = value;

        eaIndexedPushUsingStringIfPossible(&projectState->numericData, numericName, numericData);
    }

    // NOTE - finalValue, numericDef and numericData must be valid for any case that gets here!

    // Clamp value to signed 32-bit
    if(finalValue > INT_MAX)
    {
        finalValue = INT_MAX;
    }
    else if (finalValue < INT_MIN)
    {
        finalValue = INT_MIN;
    }

    // If the numeric has a maximum value, then clamp it to the maximum.
    if ( ( numericDef->maxValue ) > 0 && ( finalValue > numericDef->maxValue ) )
    {
        finalValue = numericDef->maxValue;
    }

    numericData->numericVal = (S32)finalValue;

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "ApplyNumeric", 
        "ProjectName %s NumericName %s OldValue %d NewValue %d", 
        projectDef->name, numericName, oldValue, numericData->numericVal);

    // Process any unlocks that are triggered by this numeric.
    unlocksForNumeric = GroupProject_GetUnlocksForNumeric(numericDef);

    if ( NONNULL(unlocksForNumeric) )
    {
        for ( i = eaSize(&unlocksForNumeric->unlocks) - 1; i >= 0; i-- )
        {
            GroupProjectUnlockDefRef *unlockDefRef = unlocksForNumeric->unlocks[i];
            GroupProjectUnlockDef *unlockDef = GET_REF(unlockDefRef->unlockDef);
            if ( NONNULL(unlockDef) )
            {
                GroupProjectNumericDef *numericDefFromUnlock = GET_REF(unlockDef->numeric);
                devassert(numericDef == numericDefFromUnlock);
                devassert(unlockDef->type == UnlockType_NumericValueEqualOrGreater);

                if ( unlockDef->type == UnlockType_NumericValueEqualOrGreater )
                {
                    if ( numericData->numericVal >= unlockDef->triggerValue )
                    {
                        if ( !GroupProject_trh_SetUnlock(ATR_PASS_ARGS, projectState, unlockDef->name, notifyDestinationType, notifyDestinationID) )
                        {
                            return false;
                        }
                    }
                }
            }
        }
    }

    return true;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_GrantTaskRewards(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, ATH_ARG NOCONST(GroupProjectState) *projectState, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, bool startRewards)
{
    GroupProjectDef *projectDef;
    DonationTaskDef *taskDef;
    DonationTaskDiscountDef *discountDef;
    GroupProjectNumericDef *numericDef;
    GroupProjectUnlockDef *unlockDef;
    GroupProjectBonusDef *bonusDef;
    int i;
    S32 value;
    EARRAY_OF(DonationTaskReward) rewards;
    GlobalType notifyDestinationType = GLOBALTYPE_NONE;
    ContainerID notifyDestinationID = 0;

    if ( ISNULL(projectState) || ISNULL(taskSlot) )
    {
        return false;
    }

    // Get project def.
    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return false;
    }

    // Get donation task def.
    taskDef = GET_REF(taskSlot->taskDef);
    if ( ISNULL(taskDef) )
    {
        return false;
    }

    if ( startRewards )
    {
        rewards = taskDef->taskStartRewards;
    }
    else
    {
        rewards = taskDef->taskRewards;
    }

    // Enable notification for unlocks on player projects.
    if ( projectContainer->containerType == GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER )
    {
        notifyDestinationType = GLOBALTYPE_ENTITYPLAYER;
        notifyDestinationID = projectContainer->containerID;
    }

    for ( i = 0; i < eaSize(&rewards); i++ )
    {
        DonationTaskReward *reward;
        NumericOp numericOp;

        reward = rewards[i];

        switch ( reward->rewardType )
        {
        case DonationTaskRewardType_NumericAdd:
        case DonationTaskRewardType_NumericSet:
            // Pick the correct NumericOp based on reward type.
            if ( reward->rewardType == DonationTaskRewardType_NumericAdd )
            {
                numericOp = NumericOp_Add;
            }
            else if ( reward->rewardType == DonationTaskRewardType_NumericSet )
            {
                numericOp = NumericOp_SetTo;
            }
            else
            {
                return false;
            }

            // Get the numeric def of the reward numeric.
            numericDef = GET_REF(reward->numericDef);
            if ( ISNULL(numericDef) )
            {
                return false;
            }

            // Get the constant value to reward.
            if ( !GroupProject_trh_GetProjectConstant(ATR_PASS_ARGS, projectState, reward->rewardConstant, &value) )
            {
                return false;
            }

            // Apply the bonus to the numeric reward
            value = GroupProject_trh_ApplyBonusToReward(ATR_PASS_ARGS, projectContainer, numericDef->name, value);

            // Apply the reward value to the numeric.
            if ( !GroupProject_trh_ApplyNumeric(ATR_PASS_ARGS, projectState, numericDef->name, numericOp, value, notifyDestinationType, notifyDestinationID) )
            {
                return false;
            }
            break;

        case DonationTaskRewardType_Unlock:
            // Get the unlock def.
            unlockDef = GET_REF(reward->unlockDef);
            if ( ISNULL(unlockDef) )
            {
                return false;
            }

            // Grant the unlock.
            if ( !GroupProject_trh_SetUnlock(ATR_PASS_ARGS, projectState, unlockDef->name, notifyDestinationType, notifyDestinationID) )
            {
                return false;
            }
            break;

        case DonationTaskRewardType_GuildProjectDiscount:
        case DonationTaskRewardType_PlayerProjectDiscount:
            discountDef = GET_REF(reward->discountDef);
            if ( ISNULL(discountDef) )
            {
                return false;
            }

            // Grant the unlock.
            if ( !GroupProject_trh_SetContainerDiscount(ATR_PASS_ARGS, projectContainer, discountDef->name) )
            {
                return false;
            }
            break;

        case DonationTaskRewardType_Bonus:
            bonusDef = GET_REF(reward->bonusDef);
            if ( ISNULL(bonusDef) )
            {
                return false;
            }

            // Grant the bonus.
            if ( !GroupProject_trh_SetContainerBonus(ATR_PASS_ARGS, projectContainer, bonusDef->name) )
            {
                return false;
            }
            break;

        default:
            return false;
        }
    }

    return true;
}

//
// This function promotes the next task to be the new active task.  There must be no current task, or it must be complete.
//
AUTO_TRANS_HELPER;
bool
GroupProject_trh_ActivateNextTask(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, ATH_ARG NOCONST(GroupProjectState) *projectState, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot)
{
    DonationTaskDef *taskDef;
    int i;
    GroupProjectDef *projectDef;

    if ( ISNULL(taskSlot) || ISNULL(projectState) )
    {
        return false;
    }

    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return false;
    }

    // Fail if the slot is not either empty or in completed state.
    if ( ( taskSlot->state != DonationTaskState_None ) && ( taskSlot->state != DonationTaskState_Completed ) )
    {
        return false;
    }

    // Find the taskDef for the next task.
    taskDef = GET_REF(taskSlot->nextTaskDef);
    if ( ISNULL(taskDef) )
    {
        return false;
    }

    // Make sure the slot types match.
    devassert(taskDef->slotType == taskSlot->taskSlotType);

    // If there was a previous task it should have been cleaned out by now.
    devassert(REF_STRING_FROM_HANDLE(taskSlot->taskDef) == NULL);

    // Set the new task def.
    SET_HANDLE_FROM_STRING(g_DonationTaskDict, taskDef->name, taskSlot->taskDef);

    // Remove the next task, since it is being promoted to active task.
    REMOVE_HANDLE(taskSlot->nextTaskDef);

    // Set the state.
    taskSlot->state = DonationTaskState_AcceptingDonations;

    // Make sure the buckets are empty.
    devassert(eaSize(&taskSlot->buckets) == 0);

    // Initialize the buckets.
	if( !taskDef->noCost )
	{
		for ( i = 0; i < eaSize(&taskDef->buckets); i++ )
		{
			NOCONST(DonationTaskBucketData) *bucketData = StructCreateNoConst(parse_DonationTaskBucketData);
			bucketData->bucketName = taskDef->buckets[i]->name;
			bucketData->donationCount = 0;
			eaPush(&taskSlot->buckets, bucketData);
		}
	}

    // Initialize times.
    taskSlot->finalizedTime = 0;
    taskSlot->completionTime = 0;
    taskSlot->startTime = timeSecondsSince2000();

    // Reset the count of buckets that have been completed.
    taskSlot->completedBuckets = 0;

    // Grant task start rewards.
    if ( !GroupProject_trh_GrantTaskRewards(ATR_PASS_ARGS, projectContainer, projectState, taskSlot, true) )
    {
        return false;
    }

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "ActivateTask", 
        "ProjectName %s TaskSlot %d TaskName %s", projectDef->name, taskSlot->taskSlotNum, taskDef->name);

    return true;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_SetNextTask(ATR_ARGS, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, DonationTaskDef *nextTaskDef, const char *projectName)
{
    // NOTE - caller should validate that the task is valid for the current project.

    if ( ISNULL(taskSlot) || ISNULL(nextTaskDef) )
    {
        return false;
    }

    // Slot types must match.
    if ( nextTaskDef->slotType != taskSlot->taskSlotType )
    {
        return false;
    }

    // Clean up the previous "next task" reference if one exists.
    if ( NONNULL(REF_STRING_FROM_HANDLE(taskSlot->nextTaskDef)) )
    {
        REMOVE_HANDLE(taskSlot->nextTaskDef);
    }

    // Set the reference to the next task.
    SET_HANDLE_FROM_STRING(g_DonationTaskDict, nextTaskDef->name, taskSlot->nextTaskDef);

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "SetNextTask", 
        "ProjectName %s TaskSlot %d TaskName %s", projectName, taskSlot->taskSlotNum, nextTaskDef->name);

    return true;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_ValidateAndSetNextTask(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, int slotNum, const char *taskName)
{
    NOCONST(GroupProjectState) *projectState;
    GroupProjectDef *projectDef;
    DonationTaskDef *taskDef;
    DonationTaskDefRef *taskDefRef;
    NOCONST(DonationTaskSlot) *taskSlot;
    int i;

    if ( ISNULL(projectContainer) || ISNULL(projectName) || ISNULL(taskName) )
    {
        return false;
    }

    // Get the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( ISNULL(projectState) )
    {
        // If the container doesn't contain project state for this project, then add it.
        if ( !GroupProject_trh_AddProject(ATR_PASS_ARGS, projectContainer, projectName) )
        {
            return false;
        }

        projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
        if ( ISNULL(projectState) )
        {
            return false;
        }
    }

    // Get the project def.
    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return false;
    }

    // Validate the slot number.
    if ( ( slotNum < 0 ) || ( slotNum >= GroupProject_NumTaskSlots(projectDef) ) )
    {
        return false;
    }

    // Get the task def.
    taskDefRef = eaIndexedGetUsingString(&projectDef->donationTaskDefs, taskName);
    if ( ISNULL(taskDefRef) )
    {
        return false;
    }
    taskDef = GET_REF(taskDefRef->taskDef);
    if ( ISNULL(taskDef) )
    {
        return false;
    }

    // Get the task slot.
    taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, slotNum);
    if ( ISNULL(taskSlot) )
    {
        return false;
    }

    // If it is a non-repeatable task, make sure that it has not been completed already.
    if ( !taskDef->repeatable )
    {
        NOCONST(DonationTaskDefRefContainer) *taskDefRefContainer;

        taskDefRefContainer = eaIndexedGetUsingString(&projectState->completedTasks, taskDef->name);
        if ( taskDefRefContainer != NULL )
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
        if ( i == slotNum )
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

    // Set as next task.
    if ( !GroupProject_trh_SetNextTask(ATR_PASS_ARGS, taskSlot, taskDef, projectName) )
    {
        return false;
    }

    // If there is no current task, then promote this one.
    if ( ISNULL(REF_STRING_FROM_HANDLE(taskSlot->taskDef)) )
    {
        if ( !GroupProject_trh_ActivateNextTask(ATR_PASS_ARGS, projectContainer, projectState, taskSlot) )
        {
            return false;
        }
    }

    return true;
}