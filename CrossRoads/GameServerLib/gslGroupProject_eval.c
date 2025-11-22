/***************************************************************************



***************************************************************************/

#include "gslGroupProject.h"
#include "GroupProjectCommon.h"
#include "Expression.h"

// Name of a player in the expression context.  Defined in mission_common.c
extern const char *g_PlayerVarName;

// Returns true when the group project data for a map is all set up.
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GroupProjectMapDataReady);
ExprFuncReturnVal
exprGroupProject_GroupProjectMapDataReady(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    /* GroupProjectType */ int projectType)
{
    bool ready = gslGroupProject_GroupProjectMapDataReady(projectType, iPartitionIdx);

    *pRet = ready;
    return ExprFuncReturnFinished;
}

// Returns true when the group project data for a map is all set up.
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GuildProjectMapDataReady);
ExprFuncReturnVal
exprGroupProject_GuildProjectMapDataReady(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet)
{
    return exprGroupProject_GroupProjectMapDataReady(pContext, iPartitionIdx, pRet, GroupProjectType_Guild);
}

// Gets the value of a GroupProjectNumeric associated with the current map.
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetGroupProjectNumericValueFromMap);
ExprFuncReturnVal
exprGroupProject_GetGroupProjectNumericValueFromMap(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    /* GroupProjectType */ int projectType, const char *projectName, const char *numericName, ACMD_EXPR_ERRSTRING errString)
{
    S32 value = 0;

    if ( !gslGroupProject_GetGroupProjectNumericValueFromMap(iPartitionIdx, projectType, projectName, numericName, &value, errString) )
    {
        return ExprFuncReturnError;
    }
    else
    {
        *pRet = value;
        return ExprFuncReturnFinished;
    }
}

// Gets the value of a GroupProjectNumeric associated with the guild owning the current map.
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetGuildProjectNumericValueFromMap);
ExprFuncReturnVal
exprGuildProject_GetGuildProjectNumericValueFromMap(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    const char *projectName, const char *numericName, ACMD_EXPR_ERRSTRING errString)
{
    return exprGroupProject_GetGroupProjectNumericValueFromMap(pContext, iPartitionIdx, pRet, GroupProjectType_Guild, projectName, numericName, errString);
}

// Gets the value of a GroupProjectNumeric associated with the current map.
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetGroupProjectUnlockFromMap);
ExprFuncReturnVal
exprGroupProject_GetGroupProjectUnlockFromMap(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    /* GroupProjectType */ int projectType, const char *projectName, const char *unlockName, ACMD_EXPR_ERRSTRING errString)
{
    GroupProjectState *projectState;
    GroupProjectUnlockDefRef *unlockDefRef;

    // Find the project state.
    projectState = gslGroupProject_GetGroupProjectStateForMap(projectType, projectName, iPartitionIdx);
    if ( projectState == NULL )
    {
        // If the project state can't be found, then check and see if the named project even exists.
        if ( RefSystem_ReferentFromString(g_GroupProjectDict, projectName) == NULL )
        {
            estrPrintf(errString, "GroupProjectDef %s does not exist", projectName);
            return ExprFuncReturnError;
        }

        *pRet = false;
        return ExprFuncReturnFinished;
    }

    // Find the unlock.
    unlockDefRef = eaIndexedGetUsingString(&projectState->unlocks, unlockName);

    // Return whether the unlock is set.
    *pRet = ( unlockDefRef != NULL );

    return ExprFuncReturnFinished;
}

// Gets the value of a GroupProjectNumeric associated with the guild owning the current map.
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetGuildProjectUnlockFromMap);
ExprFuncReturnVal
exprGuildProject_GetGuildProjectUnlockFromMap(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT pRet, 
    const char *projectName, const char *unlockName, ACMD_EXPR_ERRSTRING errString)
{
    return exprGroupProject_GetGroupProjectUnlockFromMap(pContext, iPartitionIdx, pRet, GroupProjectType_Guild, projectName, unlockName, errString);
}

//
// Helper function called by several wrapper expression functions to check if a personal group project task is in a particular state.
//
ExprFuncReturnVal
exprGroupProject_GetPlayerGroupProjectTaskInState(ExprContext *pContext, ACMD_EXPR_INT_OUT pRet, const char *projectName, const char *taskName, DonationTaskState state, ACMD_EXPR_ERRSTRING errString)
{
    int i;
    Entity *playerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
    GroupProjectState *projectState;
    GroupProjectContainer *projectContainer;
    const char *taskNamePooled = allocAddString(taskName);

    if ( playerEnt == NULL )
    {
        estrPrintf(errString, "Player not found");
        return ExprFuncReturnError;
    }

    // Make sure we have group project data for this player.
    GroupProject_SubscribeToPlayerProjectContainer(playerEnt);

    // Get the GroupProjectContainer for this player and project type.
    projectContainer = GroupProject_ResolveContainer(playerEnt, GroupProjectType_Player);
    if ( projectContainer == NULL )
    {
        // No container, so return false.
        *pRet = 0;
        return ExprFuncReturnFinished;
    }

    // Find the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( projectState == NULL )
    {
        estrPrintf(errString, "GroupProjectState for project %s not found", projectName);
        return ExprFuncReturnError;
    }

    // Default to not in state.
    *pRet = 0;
    for ( i = eaSize(&projectState->taskSlots) - 1; i >= 0; i-- )
    {
        DonationTaskSlot *taskSlot = projectState->taskSlots[i];
        if ( taskSlot->state == state )
        {
            // Task is in the state we are looking for.  Now check to see if it is the task we are looking for.
            DonationTaskDef *taskDef = GET_REF(taskSlot->taskDef);
            if ( taskDef && taskDef->name == taskNamePooled )
            {
                // Found the requested task.
                *pRet = 1;
                break;
            }
        }
    }

    return ExprFuncReturnFinished;
}

// Is a personal group project task in the "Accepting Donations" state.
AUTO_EXPR_FUNC(Mission) ACMD_NAME(GetPlayerGroupProjectTaskAcceptingDonations);
ExprFuncReturnVal
exprGroupProject_GetPlayerGroupProjectTaskAcceptingDonations(ExprContext *pContext, ACMD_EXPR_INT_OUT pRet, const char *projectName, const char *taskName, ACMD_EXPR_ERRSTRING errString)
{
    return exprGroupProject_GetPlayerGroupProjectTaskInState(pContext, pRet, projectName, taskName, DonationTaskState_AcceptingDonations, errString);
}

// Is a personal group project task in the "Finalized" state.
AUTO_EXPR_FUNC(Mission) ACMD_NAME(GetPlayerGroupProjectTaskFinalized);
ExprFuncReturnVal
exprGroupProject_GetPlayerGroupProjectTaskFinalized(ExprContext *pContext, ACMD_EXPR_INT_OUT pRet, const char *projectName, const char *taskName, ACMD_EXPR_ERRSTRING errString)
{
    return exprGroupProject_GetPlayerGroupProjectTaskInState(pContext, pRet, projectName, taskName, DonationTaskState_Finalized, errString);
}

// Is a personal group project task in the "Reward Pending" state.
AUTO_EXPR_FUNC(Mission) ACMD_NAME(GetPlayerGroupProjectTaskRewardPending);
ExprFuncReturnVal
exprGroupProject_GetPlayerGroupProjectTaskRewardPending(ExprContext *pContext, ACMD_EXPR_INT_OUT pRet, const char *projectName, const char *taskName, ACMD_EXPR_ERRSTRING errString)
{
    return exprGroupProject_GetPlayerGroupProjectTaskInState(pContext, pRet, projectName, taskName, DonationTaskState_RewardPending, errString);
}

// Has a personal group project task been completed?  Note that this only works for non-repeatable tasks.
AUTO_EXPR_FUNC(Mission) ACMD_NAME(GetPlayerGroupProjectTaskCompleted);
ExprFuncReturnVal
exprGroupProject_GetPlayerGroupProjectTaskCompleted(ExprContext *pContext, ACMD_EXPR_INT_OUT pRet, const char *projectName, const char *taskName, ACMD_EXPR_ERRSTRING errString)
{
    Entity *playerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
    GroupProjectState *projectState;
    GroupProjectContainer *projectContainer;

    if ( playerEnt == NULL )
    {
        estrPrintf(errString, "Player not found");
        return ExprFuncReturnError;
    }

    // Make sure we have group project data for this player.
    GroupProject_SubscribeToPlayerProjectContainer(playerEnt);

    // Get the GroupProjectContainer for this player and project type.
    projectContainer = GroupProject_ResolveContainer(playerEnt, GroupProjectType_Player);
    if ( projectContainer == NULL )
    {
        // No container, so return false.
        *pRet = 0;
        return ExprFuncReturnFinished;
    }

    // Find the project state.
    projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
    if ( projectState == NULL )
    {
        estrPrintf(errString, "GroupProjectState for project %s not found", projectName);
        return ExprFuncReturnError;
    }

    // See if the task is in the completed tasks list.
    *pRet = ( eaIndexedGetUsingString(&projectState->completedTasks, taskName) != NULL );

    return ExprFuncReturnFinished;
}