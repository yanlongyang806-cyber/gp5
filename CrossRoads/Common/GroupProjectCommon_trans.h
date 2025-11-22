/***************************************************************************



***************************************************************************/

#include "stdtypes.h"

typedef struct NOCONST(GroupProjectState) NOCONST(GroupProjectState);
typedef struct NOCONST(DonationTaskSlot) NOCONST(DonationTaskSlot);
typedef struct NOCONST(GroupProjectContainer) NOCONST(GroupProjectContainer);
typedef struct DonationTaskDef DonationTaskDef;
typedef struct GroupProjectDef GroupProjectDef;
typedef enum NumericOp NumericOp;
typedef enum GlobalType GlobalType;
typedef U32 ContainerID;

//
// Common transaction helpers.
//
bool GroupProject_trh_GetProjectConstant(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState, const char *constantName, S32 *valueOut);
void GroupProject_trh_FinalizeTask(ATR_ARGS, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, GroupProjectDef *projectDef, DonationTaskDef *taskDef);
void GroupProject_trh_CancelTask(ATR_ARGS, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, DonationTaskDef *taskDef, const char *projectName);
bool GroupProject_trh_ActivateNextTask(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, ATH_ARG NOCONST(GroupProjectState) *projectState, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot);
bool GroupProject_trh_SetNextTask(ATR_ARGS, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, DonationTaskDef *nextTaskDef, const char *projectName);
bool GroupProject_trh_ValidateAndSetNextTask(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, const char *projectName, int slotNum, const char *taskName);
bool GroupProject_trh_GrantTaskRewards(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, ATH_ARG NOCONST(GroupProjectState) *projectState, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, bool startRewards);
S32  GroupProject_trh_ApplyBonusToReward(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, const char *numericName, S32 value);
bool GroupProject_trh_ApplyNumeric(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState, const char *numericName, NumericOp op, S32 value, GlobalType notifyDestinationType, ContainerID notifyDestinationID);
bool GroupProject_trh_SetContainerDiscount(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, const char *discountName);
bool GroupProject_trh_SetContainerBonus(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, const char *bonusName);
bool GroupProject_trh_SetUnlock(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState, const char *unlockName, GlobalType notifyDestinationType, ContainerID notifyDestinationID);
bool GroupProject_trh_AddProject(ATR_ARGS, ATH_ARG NOCONST(GroupProjectContainer) *projectContainer, const char *projectName);


