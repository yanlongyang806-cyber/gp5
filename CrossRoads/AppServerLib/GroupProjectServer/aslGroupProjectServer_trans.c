#include "aslGroupProjectServer.h"
#include "GroupProjectCommon.h"
#include "GroupProjectCommon_trans.h"
#include "itemCommon.h"
#include "earray.h"
#include "GlobalTypeEnum.h"
#include "AutoTransDefs.h"
#include "ReferenceSystem.h"
#include "timing.h"
#include "logging.h"
#include "AutoGen/GroupProjectCommon_h_ast.h"

//
// Initialize the group project container.
//
AUTO_TRANS_HELPER;
enumTransactionOutcome 
GroupProject_trh_InitGroupProjectContainer(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, U32 containerType, U32 containerID, U32 ownerType, U32 ownerID, InitialProjectNames *initialProjectNames)
{
    int i;

    projectContainer->containerType = containerType;
    projectContainer->containerID = containerID;
    projectContainer->ownerType = ownerType;
    projectContainer->ownerID = ownerID;

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "InitGroupProjectContainer", "");

    if ( initialProjectNames )
    {
        for( i = 0; i < eaSize(&initialProjectNames->initialProjectNames); i++ )
        {
            if ( !GroupProject_trh_AddProject(ATR_PASS_ARGS, projectContainer, initialProjectNames->initialProjectNames[i]) )
            {
                return TRANSACTION_OUTCOME_FAILURE;
            }
        }
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, ".Containertype, .Containerid, .Ownertype, .Ownerid, .Projectlist");
enumTransactionOutcome 
GroupProject_tr_InitGroupProjectContainer(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, 
    U32 containerType, U32 containerID, U32 ownerType, U32 ownerID, InitialProjectNames *initialProjectNames)
{
    return GroupProject_trh_InitGroupProjectContainer(ATR_PASS_ARGS, projectContainer, containerType, containerID, ownerType, ownerID, initialProjectNames);
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, ".Containertype, projectList[]");
enumTransactionOutcome
GroupProject_tr_AddProject(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName)
{
    if ( GroupProject_trh_AddProject(ATR_PASS_ARGS, projectContainer, projectName) )
    {
        return TRANSACTION_OUTCOME_SUCCESS;
    }

    return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, ".Containertype, .Containerid, projectList[]");
enumTransactionOutcome
GroupProject_tr_ApplyNumeric(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, const char *numericName, int numericOp, S32 value)
{
    NOCONST(GroupProjectState) *projectState;
    GlobalType notifyDestinationType = GLOBALTYPE_NONE;
    ContainerID notifyDestinationID = 0;

    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( NONNULL(projectState) )
    {
        // Enable notification for unlocks on player projects.
        if ( projectContainer->containerType == GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER )
        {
            notifyDestinationType = GLOBALTYPE_ENTITYPLAYER;
            notifyDestinationID = projectContainer->containerID;
        }

        if ( GroupProject_trh_ApplyNumeric(ATR_PASS_ARGS, projectState, numericName, numericOp, value, notifyDestinationType, notifyDestinationID) )
        {
            return TRANSACTION_OUTCOME_SUCCESS;
        }
    }

    return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, ".Containertype, .Containerid, projectList[]");
enumTransactionOutcome
GroupProject_tr_SetUnlock(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, const char *unlockName)
{
    NOCONST(GroupProjectState) *projectState;
    GlobalType notifyDestinationType = GLOBALTYPE_NONE;
    ContainerID notifyDestinationID = 0;

    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( NONNULL(projectState) )
    {
        // Enable notification for unlocks on player projects.
        if ( projectContainer->containerType == GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER )
        {
            notifyDestinationType = GLOBALTYPE_ENTITYPLAYER;
            notifyDestinationID = projectContainer->containerID;
        }

        if ( GroupProject_trh_SetUnlock(ATR_PASS_ARGS, projectState, unlockName, notifyDestinationType, notifyDestinationID) )
        {
            return TRANSACTION_OUTCOME_SUCCESS;
        }
    }

    return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_ClearUnlock(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState, const char *unlockName)
{
    NOCONST(GroupProjectUnlockDefRefContainer) *unlockDefRefContainer;
    GroupProjectUnlockDef *unlockDefRef;
    GroupProjectDef *projectDef;

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

    // Check if the unlock has already been set.
    unlockDefRefContainer = eaIndexedGetUsingString(&projectState->unlocks, unlockName);
    if ( ISNULL(unlockDefRefContainer) )
    {
        // The unlock is not set.
        return false;
    }

    // Remove the unlock from the project state.
    eaFindAndRemove(&projectState->unlocks, unlockDefRefContainer);

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "ClearUnlock", 
        "ProjectName %s UnlockName %s", projectDef->name, unlockName);

    return true;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, "projectList[]");
enumTransactionOutcome
GroupProject_tr_ClearUnlock(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, const char *unlockName)
{
    NOCONST(GroupProjectState) *projectState;

    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( NONNULL(projectState) )
    {
        if ( GroupProject_trh_ClearUnlock(ATR_PASS_ARGS, projectState, unlockName) )
        {
            return TRANSACTION_OUTCOME_SUCCESS;
        }
    }

    return TRANSACTION_OUTCOME_FAILURE;
}

extern int gDebugTimeAfterFinalizeToCompleteTasks;

AUTO_TRANS_HELPER;
U32
GroupProject_trh_GetTaskCompletionTime(ATR_ARGS, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot)
{
    // Debugging option to cause task completion to happen at a specified time after task was finalized.
    if ( gDebugTimeAfterFinalizeToCompleteTasks )
    {
        return(taskSlot->finalizedTime + gDebugTimeAfterFinalizeToCompleteTasks);
    }
    else
    {
        return(taskSlot->completionTime);
    }
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_CompleteTaskInternal(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, ATH_ARG NOCONST(GroupProjectState) *projectState, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, GroupProjectDef *projectDef, DonationTaskDef *taskDef)
{
    NOCONST(DonationTaskDefRefContainer) *taskDefRef;
    bool bCanceled = false;

    // Write log.
    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "CompleteTask", 
        "ProjectName %s TaskSlot %d TaskName %s", projectDef->name, taskSlot->taskSlotNum, taskDef->name);

    // Clear buckets.
    eaClearStructNoConst(&taskSlot->buckets, parse_DonationTaskBucketData);

    // Clear times.
    taskSlot->completionTime = 0;
    taskSlot->finalizedTime = 0;
    taskSlot->startTime = 0;

    if ( taskSlot->state == DonationTaskState_Canceled )
    {
        bCanceled = true;
    }

    // Set state to completed.
    taskSlot->state = DonationTaskState_Completed;

    if ( !bCanceled )
    {
        // Grant rewards.
		if ( !GroupProject_trh_GrantTaskRewards(ATR_PASS_ARGS, projectContainer, projectState, taskSlot, false) )
        {
            return false;
        }

        // Add non-repeatable tasks to the completed task list.
        if ( !taskDef->repeatable )
        {
            taskDefRef = StructCreateNoConst(parse_DonationTaskDefRefContainer);
            SET_HANDLE_FROM_REFERENT(g_DonationTaskDict, taskDef, taskDefRef->taskDef);
            if ( !eaIndexedAdd(&projectState->completedTasks, taskDefRef) )
            {
                return false;
            }
        }
    }

    // Remove the reference to the completed task.
    REMOVE_HANDLE(taskSlot->taskDef);

    return true;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_CompleteTask(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, ATH_ARG NOCONST(GroupProjectState) *projectState, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot)
{
    U32 taskCompletionTime;
    DonationTaskDef *taskDef;
    GroupProjectDef *projectDef;

    if ( ISNULL(projectState) || ISNULL(taskSlot) )
    {
        return false;
    }

    // Make sure task is in correct state.
    devassert((taskSlot->state == DonationTaskState_Finalized) || (taskSlot->state == DonationTaskState_RewardClaimed) || (taskSlot->state == DonationTaskState_Canceled));
    if ( ( taskSlot->state != DonationTaskState_Finalized ) && (taskSlot->state != DonationTaskState_RewardClaimed) && ( taskSlot->state != DonationTaskState_Canceled ) )
    {
        return false;
    }

    // Make sure time has really expired.
    taskCompletionTime = GroupProject_trh_GetTaskCompletionTime(ATR_PASS_ARGS, taskSlot);
    devassert(taskCompletionTime <= timeSecondsSince2000());
    if ( taskCompletionTime > timeSecondsSince2000() )
    {
        return false;
    }

    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return false;
    }

    taskDef = GET_REF(taskSlot->taskDef);
    if ( ISNULL(taskDef) )
    {
        return false;
    }

    // Only tasks in finalized state need to check to see if they should go to RewardPending or do the actual completion.
    if ( taskSlot->state == DonationTaskState_Finalized )
    {
        if ( REF_STRING_FROM_HANDLE(taskDef->completionRewardTable) )
        {
            // Only player projects can have a completion reward table.
            if ( projectDef->type != GroupProjectType_Player )
            {
                return false;
            }

            // defer completion until the reward has been claimed.
            taskSlot->state = DonationTaskState_RewardPending;
            return true;
        }
    }

    return GroupProject_trh_CompleteTaskInternal(ATR_PASS_ARGS, projectContainer, projectState, taskSlot, projectDef, taskDef);
}


AUTO_TRANSACTION
ATR_LOCKS(projectContainer, ".Containertype, .Containerid, .Projectlist, .Discountlist, .Bonuslist");
enumTransactionOutcome
GroupProject_tr_SetNextTask(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, int slotNum, const char *taskName)
{

    if ( !GroupProject_trh_ValidateAndSetNextTask(ATR_PASS_ARGS, projectContainer, projectName, slotNum, taskName) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, ".Containertype, .Containerid, .Projectlist, .Discountlist, .Bonuslist");
enumTransactionOutcome
GroupProject_tr_CompleteTask(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, int slotNum)
{
    NOCONST(GroupProjectState) *projectState;
    GroupProjectDef *projectDef;
    NOCONST(DonationTaskSlot) *taskSlot;

    if ( ISNULL(projectContainer) || ISNULL(projectName) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( ISNULL(projectState) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the project def.
    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Validate the slot number.
    if ( ( slotNum < 0 ) || ( slotNum >= GroupProject_NumTaskSlots(projectDef) ) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the task slot.
    taskSlot = eaIndexedGetUsingInt(&projectState->taskSlots, slotNum);
    if ( ISNULL(taskSlot) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Make sure the task in in the right state.
    if ( ( taskSlot->state != DonationTaskState_Finalized ) &&
        ( taskSlot->state != DonationTaskState_RewardClaimed ) &&
        ( taskSlot->state != DonationTaskState_Canceled ) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    if ( !GroupProject_trh_CompleteTask(ATR_PASS_ARGS, projectContainer, projectState, taskSlot) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // If this task is in complete state and there is a next task, then promote it.
    if ( ( taskSlot->state == DonationTaskState_Completed ) && NONNULL(REF_STRING_FROM_HANDLE(taskSlot->nextTaskDef) ) )
    {
        if ( !GroupProject_trh_ActivateNextTask(ATR_PASS_ARGS, projectContainer, projectState, taskSlot) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, ".Containertype, projectList[]");
enumTransactionOutcome
GroupProject_tr_SetProjectMessage(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, const char *projectMessage)
{
    NOCONST(GroupProjectState) *projectState;

    if ( ISNULL(projectContainer) || ISNULL(projectName) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( ISNULL(projectState) )
    {
        // If the container doesn't contain project state for this project, then add it.
        if ( !GroupProject_trh_AddProject(ATR_PASS_ARGS, projectContainer, projectName) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }

        projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
        if ( ISNULL(projectState) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }
    }

    // Make sure the string is not too long.
    if ( strlen(projectMessage) > PROJECT_MESSAGE_MAX_LEN )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // If there is an old message, free it.
    if ( projectState->projectMessage )
    {
        free(projectState->projectMessage);
    }

    // Set the new message.
    projectState->projectMessage = strdup(projectMessage);

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, ".Containertype, projectList[]");
enumTransactionOutcome
GroupProject_tr_SetProjectPlayerName(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, const char *projectPlayerName)
{
    NOCONST(GroupProjectState) *projectState;

    if ( ISNULL(projectContainer) || ISNULL(projectName) )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // Get the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( ISNULL(projectState) )
    {
        // If the container doesn't contain project state for this project, then add it.
        if ( !GroupProject_trh_AddProject(ATR_PASS_ARGS, projectContainer, projectName) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }

        projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
        if ( ISNULL(projectState) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }
    }

    // Make sure the string is not too long.
    if ( strlen(projectPlayerName) > PROJECT_PLAYER_NAME_MAX_LEN )
    {
        return TRANSACTION_OUTCOME_FAILURE;
    }

    // If there is an old message, free it.
    if ( projectState->projectPlayerName )
    {
        free(projectState->projectPlayerName);
    }

    // Set the new message.
    projectState->projectPlayerName = strdup(projectPlayerName);

    return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_FixupTaskSlot(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, ATH_ARG NOCONST(GroupProjectState) *projectState, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, const char *projectName)
{
    // Check to see if the next DonationTask still exists.
    if ( REF_IS_SET_BUT_ABSENT(taskSlot->nextTaskDef) )
    {
        TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FixupTaskSlot", 
            "Project %s: nextTaskDef %s not found and removed.", projectName, NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(taskSlot->nextTaskDef)));

        // Remove the next task, since it doesn't exist anymore.
        REMOVE_HANDLE(taskSlot->nextTaskDef);
    }

    if ( ( taskSlot->state == DonationTaskState_AcceptingDonations ) || ( taskSlot->state == DonationTaskState_Finalized ) )
    {
        // Check to see if the active DonationTask still exists.
        if ( REF_IS_SET_BUT_ABSENT(taskSlot->taskDef) )
        {
            TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FixupTaskSlot", 
                "Project %s: active taskDef %s not found.  Slot cleared.", projectName, NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(taskSlot->taskDef)));

            // Clear out this task since it doesn't exist anymore.
            REMOVE_HANDLE(taskSlot->taskDef);

            // Clear buckets.
            eaClearStructNoConst(&taskSlot->buckets, parse_DonationTaskBucketData);

            // Clear times.
            taskSlot->completionTime = 0;
            taskSlot->finalizedTime = 0;
            taskSlot->startTime = 0;

            taskSlot->completedBuckets = 0;
            taskSlot->state = DonationTaskState_None;
        }
        else if ( IS_HANDLE_ACTIVE(taskSlot->taskDef) )
        {
            DonationTaskDef *taskDef = GET_REF(taskSlot->taskDef);
            int i;

            if ( taskSlot->state == DonationTaskState_Finalized )
            {
                // Make sure time to complete is correct.
                if ( taskSlot->completionTime != ( taskDef->secondsToComplete + taskSlot->finalizedTime ) )
                {
                    taskSlot->completionTime = taskDef->secondsToComplete + taskSlot->finalizedTime;
                    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FixupTaskSlot", 
                        "Project %s: Task %s: update completion time to %d.", projectName, taskDef->name, taskSlot->completionTime);
                }
            }
            else if ( taskSlot->state == DonationTaskState_AcceptingDonations )
            {
                int completedBucketCount = 0;

                // First iterate the persisted buckets and get rid of any that don't match buckets in the def.
                for ( i = eaSize(&taskSlot->buckets) - 1; i >= 0; i-- )
                {
                    NOCONST(DonationTaskBucketData) *bucketData = taskSlot->buckets[i];
                    GroupProjectDonationRequirement *donationRequirement;
                    int bucketIndex;

                    bucketIndex = DonationTask_FindRequirement(taskDef, bucketData->bucketName);
                    if ( bucketIndex >= 0 )
                    {
                        donationRequirement = taskDef->buckets[bucketIndex];
                    }
                    else
                    {
                        donationRequirement = NULL;
                    }

                    if ( ISNULL(donationRequirement) )
                    {
                        TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FixupTaskSlot", 
                            "Project %s: Task %s: bucket %s does not exist in def.  Current donation count %d.  Removed bucket.", 
                            projectName, taskDef->name, bucketData->bucketName, bucketData->donationCount);

                        // The bucket doesn't exist in the def anymore, so remove it.
                        eaRemove(&taskSlot->buckets, i);
                        StructDestroyNoConst(parse_DonationTaskBucketData, bucketData);
                    }
                    else
                    {
                        // Keep count of any completed buckets.
                        if ( GroupProject_trh_CheckBucketRequirementsComplete(ATR_PASS_ARGS, projectContainer, projectState, taskSlot, bucketData->bucketName) )
                        {
                            completedBucketCount++;
                        }
                    }
                }

                if ( eaSize(&taskSlot->buckets) < eaSize(&taskDef->buckets) )
                {
                    // There are some new buckets that need to be added to the persisted data.
                    for ( i = eaSize(&taskDef->buckets) - 1; i >= 0; i-- )
                    {
                        GroupProjectDonationRequirement *donationRequirement = taskDef->buckets[i];
                        NOCONST(DonationTaskBucketData) *bucketData;

                        bucketData = eaIndexedGetUsingString(&taskSlot->buckets, donationRequirement->name);
                        if ( ISNULL(bucketData) )
                        {
                            TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FixupTaskSlot", 
                                "Project %s: Task %s: bucket %s does not exist in container.  Added bucket.", 
                                projectName, taskDef->name, donationRequirement->name);

                            // The bucket doesn't exist, so create it.
                            bucketData = StructCreateNoConst(parse_DonationTaskBucketData);
                            bucketData->bucketName = donationRequirement->name;
                            bucketData->donationCount = 0;
                            eaPush(&taskSlot->buckets, bucketData);
                        }
                    }
                }

                devassert( eaSize(&taskSlot->buckets) == eaSize(&taskDef->buckets) );

				if ( taskSlot->completedBuckets != (U32)completedBucketCount )
                {
                    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FixupTaskSlot", 
                        "Project %s: Task %s: updated completed bucket count.  old value=%d, new value=%d", 
                        projectName, taskDef->name, taskSlot->completedBuckets, completedBucketCount);

                    taskSlot->completedBuckets = completedBucketCount;
                }

                // If the donations are now complete, then finalize the task.  This could happen if the required quantity of a bucket is reduced.
                if ( completedBucketCount == eaSize(&taskSlot->buckets) )
                {
                    GroupProjectDef *projectDef = GET_REF(projectState->projectDef);
                    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FixupTaskSlot", 
                        "Project %s: Task %s: finalize task.", projectName, taskDef->name);

                    GroupProject_trh_FinalizeTask(ATR_PASS_ARGS, taskSlot, projectDef, taskDef);
                }
            }
        }
        else
        {
            if ( taskSlot->state != DonationTaskState_None )
            {
                taskSlot->state = DonationTaskState_None;
                TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FixupTaskSlot", 
                    "Project %s: Slot %d: is unused and empty.", projectName, taskSlot->taskSlotNum);
            }
        }
    }

    if ( ( taskSlot->state == DonationTaskState_None ) || ( taskSlot->state == DonationTaskState_Completed ) )
    {
        if ( !IS_HANDLE_ACTIVE(taskSlot->taskDef) && IS_HANDLE_ACTIVE(taskSlot->nextTaskDef) )
        {
            if ( !GroupProject_trh_ActivateNextTask(ATR_PASS_ARGS, projectContainer, projectState, taskSlot) )
            {
                return false;
            }
        }
    }

    return true;
}

AUTO_TRANS_HELPER;
bool
GroupProject_trh_FixupProject(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, ATH_ARG NOCONST(GroupProjectState) *projectState)
{
    int i, j;
    GroupProjectDef *projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_FAILURE(LOG_GROUPPROJECT, "FixupProject", 
            "Fixup failed.  Project %s not found.", NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(projectState->projectDef)));
        return false;
    }

    for ( i = 0; i < eaiSize(&projectDef->slotTypes); i++ )
    {
        NOCONST(DonationTaskSlot) *slot = NULL;
        int bestSlotType = -1;

        for ( j = i; j < eaSize(&projectState->taskSlots); j++ )
        {
            if ( projectState->taskSlots[j]->taskSlotType ==  projectDef->slotTypes[i] )
            {
                if ( (int)projectState->taskSlots[j]->taskSlotNum == i )
                {
                    slot = projectState->taskSlots[j];
                    if ( i != j )
                    {
                        eaRemove(&projectState->taskSlots, j);
                        eaIndexedInsert(&projectState->taskSlots, slot, i);
                    }
                    break;
                }
                else
                {
                    // The existing slot is the same type of the expected slot,
                    // if there is no identically matching slot number, move the
                    // existing task slot to this new position.
                    bestSlotType = j;
                }
            }
        }

        if ( slot == NULL )
        {
            if ( bestSlotType == -1 )
            {
                slot = StructCreateNoConst(parse_DonationTaskSlot);
                slot->state = DonationTaskState_None;
                slot->taskSlotType = projectDef->slotTypes[i];
                slot->taskSlotNum = i;

                eaIndexedAdd(&projectState->taskSlots, slot);
            }
            else
            {
                slot = eaRemove(&projectState->taskSlots, bestSlotType);
                slot->taskSlotNum = i;
                eaIndexedInsert(&projectState->taskSlots, slot, i);
            }
        }
    }

    // NB(jm): I'm not going to handle removing slots here, since that will
    // likely need to be handled on a case by case basis.
    if ( eaSize(&projectState->taskSlots) != eaiSize(&projectDef->slotTypes) )
    {
        TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_FAILURE(LOG_GROUPPROJECT, "FixupProject", 
            "Fixup failed.  After adding new and reorganizing existing task slots, the project state slots don't match the project def slots.");
        return false;
    }

    for ( i = eaSize(&projectState->taskSlots) - 1; i >= 0; i-- )
    {
        NOCONST(DonationTaskSlot) *taskSlot = projectState->taskSlots[i];
        GroupProject_trh_FixupTaskSlot(ATR_PASS_ARGS, projectContainer, projectState, taskSlot, projectDef->name);
        if ( i > 0 )
        {
            if ( !devassertmsg( taskSlot->taskSlotNum > projectState->taskSlots[i-1]->taskSlotNum, "GroupProjectState code fixup code appears to have broken task slot order" ) )
            {
                TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_FAILURE(LOG_GROUPPROJECT, "FixupProject", 
                    "Fixup failed.  The task slots are out of order!");
                return false;
            }
        }
    }

    return true;
}

AUTO_TRANSACTION
ATR_LOCKS(projectContainer, ".Projectlist, .Containertype, .Containerid, .Ownertype, .Ownerid, .Discountlist, .Bonuslist");
enumTransactionOutcome
GroupProject_tr_FixupProjectContainer(ATR_ARGS, NOCONST(GroupProjectContainer) *projectContainer, U32 containerType, U32 containerID, U32 ownerType, U32 ownerID, InitialProjectNames *initialProjectNames)
{
    int i;

    if ( eaSize(&projectContainer->projectList) == 0 )
    {
        if ( GroupProject_trh_InitGroupProjectContainer(ATR_PASS_ARGS, projectContainer, containerType, containerID, ownerType, ownerID, initialProjectNames) == TRANSACTION_OUTCOME_FAILURE )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }
    }

    for ( i = eaSize(&projectContainer->projectList) - 1; i >= 0; i-- )
    {
        if ( !GroupProject_trh_FixupProject(ATR_PASS_ARGS, projectContainer, projectContainer->projectList[i]) )
        {
            return TRANSACTION_OUTCOME_FAILURE;
        }
    }
    return TRANSACTION_OUTCOME_SUCCESS;
}