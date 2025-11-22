#include "logging.h"
#include "TextParser.h"
#include "StringCache.h"
#include "StringUtil.h"

//#include "Entity.h"
//#include "EntityLib.h"
//#include "EntitySavedData.h"
//#include "EntitySavedData_h_ast.h"
//#include "Player.h"

#include "NotifyEnum.h"

#include "Team.h"

#include "aslTeamUtility.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"




void aslTeam_SendError(U32 iDestPlayerID, U32 iSubjectID, SA_PARAM_NN_VALID const char *pcActionType, SA_PARAM_NN_VALID const char *pcMessageKey)
{
	if (iDestPlayerID) {
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);
		estrConcatf(&estrBuffer, "TeamServer_ErrorType_%s", pcActionType);
		RemoteCommand_gslTeam_ResultMessage(GLOBALTYPE_ENTITYPLAYER, iDestPlayerID, iDestPlayerID, iDestPlayerID, iSubjectID, estrBuffer, pcMessageKey, true, 0, 0, 0, 0, NULL);
		estrDestroy(&estrBuffer);
	}
}


///////////////////////////////////////////////////////////////////////////////////////////
// Non-Transaction Helpers
///////////////////////////////////////////////////////////////////////////////////////////
// This represents a lot of validation code that is repeated among the many remote
// commands below.

// Return whether the entity is a member of the team
bool aslTeam_IsMember(Team *pTeam, U32 iEntID) {
	S32 i;
	if(!pTeam)
	{
		return false;
	}
	for (i = eaSize(&pTeam->eaMembers)-1; i >= 0; i--) {
		if (pTeam->eaMembers[i]->iEntID == iEntID) {
			return true;
		}
	}
	return false;
}

// Return whether the entity is invited to the team
bool aslTeam_IsInvite(Team *pTeam, U32 iEntID) {
	S32 i;
	if(!pTeam)
	{
		return false;
	}
	for (i = eaSize(&pTeam->eaInvites)-1; i >= 0; i--) {
		if (pTeam->eaInvites[i]->iEntID == iEntID) {
			return true;
		}
 	}
	return false;
}

// Return whether the entity is requesting the team
bool aslTeam_IsRequest(Team *pTeam, U32 iEntID) {
	S32 i;
	if(!pTeam)
	{
		return false;
	}
	for (i = eaSize(&pTeam->eaRequests)-1; i >= 0; i--) {
		if (pTeam->eaRequests[i]->iEntID == iEntID) {
			return true;
		}
	}
	return false;
}

// Return whether the entity is in the disconnecteds
bool aslTeam_IsOnDisconnecteds(Team *pTeam, U32 iEntID)
{
	S32 i;
	if(!pTeam)
	{
		return false;
	}
	for (i = eaSize(&pTeam->eaDisconnecteds)-1; i >= 0; i--) {
		if (pTeam->eaDisconnecteds[i]->iEntID == iEntID) {
			return true;
		}
	}
	return false;
}

