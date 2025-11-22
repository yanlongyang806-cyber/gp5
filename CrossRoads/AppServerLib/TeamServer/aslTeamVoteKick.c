#include "logging.h"
#include "TextParser.h"
#include "StringCache.h"
#include "StringUtil.h"

#include "objTransactions.h"
#include "objContainer.h"

#include "NotifyEnum.h"

#include "Team.h"
#include "aslTeamServer.h"
#include "aslTeamUtility.h"

#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "autogen/AppServerLib_autogen_remotefuncs.h"

#include "AutoGen/Team_h_ast.h"



#define VOTEKICK_TIME_LIMIT 30.0f

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vote management

// Notify the team members about something. Sends the command to the GameServer the entitiy is on. 
// That GameServer will format and send to the client (TeamCommands.c)
// The messageKeys are in TeamServer.ms in data/messages for NW
void aslTeam_VoteKickTeamNotify(Team* pTeam, NotifyType eNotifyType, const char* pcMessageKey)
{
	int i;
	for (i = 0; i < eaSize(&pTeam->eaMembers); i++)
	{
		U32 uEntID = pTeam->eaMembers[i]->iEntID;
	
		RemoteCommand_gslTeam_NotifyMessage(GLOBALTYPE_ENTITYPLAYER, 
											 uEntID, 
											 uEntID,
											pTeam->iContainerID,
												pcMessageKey, eNotifyType);
	}
}


// Shut down any current vote kick. If there were any unvoted people, send an interrupt notify.
void aslTeam_VoteKickInterrupt(Team* pTeam)
{
	// Interrupt if the team disappears
	// Interrupt if the target drops group.
	
	if (pTeam!=NULL && pTeam->pVoteKickData!=NULL)
	{
		// Blanket notify to the team (except the target) that the vote was interrupted.
		int i;
		for (i = 0; i < eaSize(&pTeam->eaMembers); i++)
		{
			U32 uEntID = pTeam->eaMembers[i]->iEntID;
			if (uEntID!=pTeam->pVoteKickData->iKickTargetID)
			{
				RemoteCommand_gslTeam_NotifyMessage(GLOBALTYPE_ENTITYPLAYER, 
											 uEntID, 
											 uEntID,
											pTeam->iContainerID,
										   "TeamServer_VoteKick_Interrupt",
											kNotifyType_TeamVoteKickInterrupt);
			}
		}
								   
		StructDestroy(parse_TeamVoteKickData, pTeam->pVoteKickData);
		pTeam->pVoteKickData=NULL;
	}
}



void aslTeam_VoteKickSucceed(Team *pTeam)
{
	if (pTeam!=NULL && pTeam->pVoteKickData!=NULL)
	{
		U32 uTeamID = pTeam->iContainerID;
		U32 uTargetEntID = pTeam->pVoteKickData->iKickTargetID;
		U32 uYesVotes=eaiSize(&pTeam->pVoteKickData->eaiYesVoterIDs);
		U32 uNoVotes=eaiSize(&pTeam->pVoteKickData->eaiNoVoterIDs);
	
		objLog(LOG_TEAM, GLOBALTYPE_TEAM, uTeamID, 0, NULL, NULL, NULL, "Vote Kick", NULL, "SUCCESS Inst:[%d] Target:[%d] Vote[%d/%d]",
				pTeam->pVoteKickData->iInstigatorID,
				pTeam->pVoteKickData->iKickTargetID,
			   uYesVotes, uNoVotes);

		// Notify the team members
		aslTeam_VoteKickTeamNotify(pTeam, kNotifyType_TeamVoteKickSucceed, "TeamServer_VoteKick_Succeed");
		
		// (If we are an owned team, call to the ownedteamGameServer to do the kick.
		//	Otherwise we can just call the transaction directly here on the team server
		if (pTeam->iGameServerOwnerID!=0)
		{
			// This mirrors the code in gslQueue_HandleAbandonMap in gslQueue.c. We can take shortcuts since we have more information ahead of time.

			U32 uMapID = pTeam->iGameServerOwnerID;
			U32 uPartitionID = pTeam->iGameServerOwnerPartition;

			// Force a team leave
			if (uTeamID!=0)
			{
				bool bFeedback=false;
				RemoteCommand_aslTeam_Leave(GLOBALTYPE_TEAMSERVER, 0, uTeamID, uTargetEntID, uTargetEntID, bFeedback, NULL, NULL, NULL);
			}

			{
				bool bApplyPenaltyIfNeeded=false;	// No leaver penalty for being kicked
				
				RemoteCommand_gslQueue_DoEntAbandonThisMapAndQueue(GLOBALTYPE_GAMESERVER, uMapID, uTargetEntID, uMapID, uPartitionID, bApplyPenaltyIfNeeded);
				// EntAbandonThisMapAndQueue will cause the abandoning ent to leave the LocalTeam's map if it is on it.
			}
		}
		else
		{
			bool bFeedback=true;
			RemoteCommand_aslTeam_Leave(GLOBALTYPE_TEAMSERVER, 0, uTeamID, uTargetEntID, uTargetEntID, bFeedback, NULL, NULL, NULL);
			
		}
			
		StructDestroy(parse_TeamVoteKickData, pTeam->pVoteKickData);
		pTeam->pVoteKickData=NULL;
	}
}



void aslTeam_VoteKickFail(Team *pTeam)
{
	if (pTeam!=NULL && pTeam->pVoteKickData!=NULL)
	{
		U32 uTeamID = pTeam->iContainerID;
		U32 uTargetEntID = pTeam->pVoteKickData->iKickTargetID;
		U32 uYesVotes=eaiSize(&pTeam->pVoteKickData->eaiYesVoterIDs);
		U32 uNoVotes=eaiSize(&pTeam->pVoteKickData->eaiNoVoterIDs);

		// Notify the team members
		aslTeam_VoteKickTeamNotify(pTeam, kNotifyType_TeamVoteKickFail, "TeamServer_VoteKick_Fail");
	
		objLog(LOG_TEAM, GLOBALTYPE_TEAM, uTeamID, 0, NULL, NULL, NULL, "Vote Kick", NULL, "FAIL Inst:[%d] Target:[%d] Vote[%d/%d]",
				pTeam->pVoteKickData->iInstigatorID,
				pTeam->pVoteKickData->iKickTargetID,
			   uYesVotes, uNoVotes);

		StructDestroy(parse_TeamVoteKickData, pTeam->pVoteKickData);
		pTeam->pVoteKickData=NULL;
	}
}


void aslTeam_VoteKickCountVotes(Team *pTeam)
{
	int iYesCount = 0;
	int iNoCount = 0;
	int iVoterCount = 0;
	
	if (pTeam==NULL)
	{
		return;
	}

	iYesCount = eaiSize(&pTeam->pVoteKickData->eaiYesVoterIDs);
	iNoCount = eaiSize(&pTeam->pVoteKickData->eaiNoVoterIDs);
	iVoterCount = eaiSize(&pTeam->pVoteKickData->eaiVoterIDs);
	
	// Count votes

	// If enough, then call for kick
		// (If there is a pre-formed team involved, modify the number needed?)

		//--If there is a group of 3 or more friends and someone in that group initiates a Vote Kick of a random party member, it now requires 4 votes to kick the player, instead of the standard 3 votes.	

	// (The instigator doesn't actually vote, so add 1)
	// This will come out so 2 nos defeat either for 4 or 5 people left.
	if (iYesCount+1 > iNoCount)
	{
		aslTeam_VoteKickSucceed(pTeam);
	}
	else
	{
		aslTeam_VoteKickFail(pTeam);
	}
 }


AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_VoteKickRegister(U32 iTeamID, U32 iVoterEntID, bool bKickConfirm)
{
	static char pcActionTypeVoteKick[] = "VoteKickVote";
	char *pcActionType = pcActionTypeVoteKick;
	
	Team *pTeam = aslTeam_GetTeam(iTeamID);
	if (pTeam==NULL)
	{
		aslTeam_SendError(iVoterEntID, 0, pcActionType, "TeamServer_Error_TeamNotFound");
	}
	else if (!aslTeam_IsMember(pTeam, iVoterEntID))
	{
		aslTeam_SendError(iVoterEntID, 0, pcActionType, "TeamServer_Error_SelfNotOnTeam");
	}
	else
	{
		// Check against the we-are-voting data.
		if (pTeam->pVoteKickData!=NULL)
		{
			// Make sure they were offered a vote
			if (eaiFind(&pTeam->pVoteKickData->eaiVoterIDs, iVoterEntID) < 0)
			{
				// Silent ignore
				return;
			}
			
			// See if they already voted
			if (eaiFind(&pTeam->pVoteKickData->eaiYesVoterIDs, iVoterEntID) >= 0 || eaiFind(&pTeam->pVoteKickData->eaiNoVoterIDs, iVoterEntID) >= 0)
			{
				// Silent ignore
				return;
			}

			// Register the vote.
			if (bKickConfirm)
			{
				eaiPush(&pTeam->pVoteKickData->eaiYesVoterIDs, iVoterEntID);
			}
			else
			{
				eaiPush(&pTeam->pVoteKickData->eaiNoVoterIDs, iVoterEntID);
			}

			// Apply Vote cooldown penalties

			//--When players queue as a group with a tank or a healer, and the tank or healer drops group (or is kicked) within the first 5 minutes, all of the other players who queued with them are removed from the group as well.

			//--The penalty for using the Vote Kick feature too often is not issued to a player who kicks someone with whom they queued for a dungeon (i.e. the party member being selected for removal is not a player randomly selected via the Dungeon Finder).
			//--The penalty for using the Vote Kick feature too often will be issued much more quickly to players who join as a party of 4 and kick the randomly selected 5th player.
			// Check if we're done voting
			{
				int iYesCount = eaiSize(&pTeam->pVoteKickData->eaiYesVoterIDs);
				int iNoCount = eaiSize(&pTeam->pVoteKickData->eaiNoVoterIDs);
				int iVoterCount = eaiSize(&pTeam->pVoteKickData->eaiVoterIDs);
	
				// If enough, then call for kick
				if (iYesCount+iNoCount>=iVoterCount)
				{
					aslTeam_VoteKickCountVotes(pTeam);
				}
			}
		}
	}
}


AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void aslTeam_BeginVoteKick(U32 iTeamID, U32 iEntID, U32 iSubjectID, const char* pTargetName, const char* pKickReason)
{
	static char pcActionTypeVoteKick[] = "VoteKick";
	char *pcActionType = pcActionTypeVoteKick;
	S32 i;
	
	Team *pTeam = aslTeam_GetTeam(iTeamID);

	if (pTeam==NULL)
	{
		aslTeam_SendError(iEntID, 0, pcActionType, "TeamServer_Error_TeamNotFound");
	}
	else if (!aslTeam_IsMember(pTeam, iEntID))
	{
		aslTeam_SendError(iEntID, 0, pcActionType, "TeamServer_Error_SelfNotOnTeam");
	}
	else if (!(aslTeam_IsMember(pTeam, iSubjectID) || aslTeam_IsOnDisconnecteds(pTeam, iSubjectID)))
	{
		aslTeam_SendError(iEntID, iSubjectID, pcActionType, "TeamServer_Error_NotOnTeam");
	}
	else
	{
		if (pTeam->pVoteKickData!=NULL)
		{
			// Already a vote going on. Don't start a new one.
			// (Maybe check if this one is old?)
			aslTeam_SendError(iEntID, iSubjectID, pcActionType, "TeamServer_Error_VoteKickInProgress");
		}
		else
		{
			// Set up the we are voting data
		
			pTeam->pVoteKickData = StructCreate(parse_TeamVoteKickData);
			pTeam->pVoteKickData->iInstigatorID = iEntID;
			pTeam->pVoteKickData->iKickTargetID = iSubjectID;
			pTeam->pVoteKickData->uVoteStartTime = timeSecondsSince2000();
			
			// Go through each online member and if we are not the Instigator or Subject, send a vote notification
			// (We do not worry about disconnecteds. If they come online after this call, they will not participate
			//  in the voting).
			for (i = 0; i < eaSize(&pTeam->eaMembers); i++)
			{
				U32 uMemberID = pTeam->eaMembers[i]->iEntID;
				if (uMemberID != iEntID && uMemberID != iSubjectID)
				{
					eaiPush(&pTeam->pVoteKickData->eaiVoterIDs, uMemberID);
					RemoteCommand_gslTeam_StartSingleVote(GLOBALTYPE_ENTITYPLAYER, uMemberID, uMemberID, pTargetName, pKickReason);
				}
			}

			if (eaiSize(&pTeam->pVoteKickData->eaiVoterIDs)==0)
			{
				// No actual voters. Must be a two-member situation with one person off-map (or everyone else is disconnected?)
				// No actual votes went out, so we can assume that this will immediately succeed.

				aslTeam_VoteKickSucceed(pTeam);
			}
		}
	}
}


void aslTeam_VoteKickUpdate()
{
	ContainerIterator iter;
	Team *pTeam;
	U32 currentTime = timeSecondsSince2000();
	
	objInitContainerIteratorFromType(GLOBALTYPE_TEAM, &iter);
	while (pTeam = objGetNextObjectFromIterator(&iter))
	{
		// I'm a little worried about efficiency here. But we hope that there are not that many simultaneous vote kicks happening
		if (pTeam->pVoteKickData!=NULL)
		{
			bool bInterrupted = false;

			// Check that source/target are still there
			if (!aslTeam_IsMember(pTeam,pTeam->pVoteKickData->iInstigatorID) ||
				(!aslTeam_IsMember(pTeam,pTeam->pVoteKickData->iKickTargetID) && !aslTeam_IsOnDisconnecteds(pTeam,pTeam->pVoteKickData->iKickTargetID)))
			{
				// Interrupt
				aslTeam_VoteKickInterrupt(pTeam);
				bInterrupted = true;
			}

			// Check that the voters are still present. Interrupt if someone is gone
			if (!bInterrupted)
			{
				int i;
				for (i=0; i<eaiSize(&pTeam->pVoteKickData->eaiVoterIDs); i++)
				{
					// Note that being disconnected will cause the interrupt
					if (!aslTeam_IsMember(pTeam,pTeam->pVoteKickData->eaiVoterIDs[i]))
					{
						// Interrupt
						aslTeam_VoteKickInterrupt(pTeam);
						bInterrupted=true;
						break;
					}
				}
			}
								  
			// Check timeout (30 seconds)
			if (!bInterrupted)
			{
				if (currentTime - pTeam->pVoteKickData->uVoteStartTime > VOTEKICK_TIME_LIMIT)
				{
					// Finish off vote
					aslTeam_VoteKickCountVotes(pTeam);
				}
			}
		}
	}
	objClearContainerIterator(&iter);
}




