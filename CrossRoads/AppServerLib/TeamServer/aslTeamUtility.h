#ifndef ASLTEAMUTILITY_H
#define ASLTEAMUTILITY_H

void aslTeam_SendError(U32 iDestPlayerID, U32 iSubjectID, SA_PARAM_NN_VALID const char *pcActionType, SA_PARAM_NN_VALID const char *pcMessageKey);

bool aslTeam_IsMember(Team *pTeam, U32 iEntID);
bool aslTeam_IsInvite(Team *pTeam, U32 iEntID);
bool aslTeam_IsRequest(Team *pTeam, U32 iEntID);
bool aslTeam_IsOnDisconnecteds(Team *pTeam, U32 iEntID);

#endif

