#include "LoginHammer.h"
#include "accountnet.h"
#include "accountnet_h_ast.h"
#include "chatCommonStructs.h"
#include "cmdparse.h"
#include "CrypticPorts.h"
#include "CostumeCommon.h"
#include "FolderCache.h"
#include "gimmeDLLWrapper.h"
#include "GlobalComm.h"
#include "GlobalTypes.h"
#include "LoginCommon.h"
#include "MapDescription.h"
#include "MemoryPool.h"
#include "objPath.h"
#include "net.h"
#include "netpacketutil.h"
#include "Player.h"
#include "ReferenceSystem.h"
#include "sock.h"
#include "StashTable.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "structNet.h"
#include "sysutil.h"
#include "TestServerIntegration.h"
#include "timing_profiler_interface.h"
#include "utilitiesLib.h"
#include "Login2Common.h"
#include "CharacterAttribsMinimal.h"
#include "StatPoints.h"

#include "chatCommonStructs_h_ast.h"
#include "CharacterClass_h_ast.h"
#include "CostumeCommon_h_ast.h"
#include "LoginCommon_h_ast.h"
#include "AutoGen/Login2Common_h_ast.h"
#include "AutoGen/StatPoints_h_ast.h"

#define TESTSERVER_WORKS 1

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, "LoginHammer"););

extern void setAccountServer(const char *pAccountServer);
extern bool gbDoNotSendMachineID;

int giOffset = 0;
int giConnections = 10000, giDelay = 0;
char gAccountServer[128] = "localhost";
char gLoginServer[128] = "localhost";
char gAccountPrefix[128] = "TestAccount_";
char gMapName[128] = "Mil_Tutorial";
int giAccountHammerMode = 1;
int giLoginHammerMode = 1;
int giChatAuthHammerMode = 1;
int giChatAuthNumRelays = 1;
int giServerHammerMode = 0;
float gfPercentPass = 1.0, gfPercentCreate = 0.0;
int giAccountPacketDeath = 0;
int giLoginPacketDeath = 0;
int giServerPacketDeath = 0;
float giTimeout = 10.0;
float giTransferTimeout = 120.0;
int giRepeatAt = 0;
int giIgnoreMapErrors = 0;
bool gbQuitOnSuccess = false;
bool gbQuitNonFatallyIfUnknownProduct = false;
char gFixedPassword[MAX_PASSWORD_PLAINTEXT] = "";
bool gbUsePreEncryptedPassword = true;
bool gbTestAccountGuard = false;

// "password" pre-encrypted with dev_1 encryption key
char gPreEncryptedPassword[MAX_PASSWORD_ENCRYPTED_BASE64] = "HkNuu9el+3aQSGltgPG65VQ+5By/mnoGQbfhij/itOSPLLQcEf3TLJ8sZHCoOi5jFwpONUoGXn2JfIQIzQsFDuX+cJv5NBtAr/hfJaNh2cAxuIFq1XJGobwwqbQ1Mv/GAnrExeTu9XnD4hdH/pkikjVo7GsVJkC1lfU4Ywo5cHs=";

AUTO_CMD_INT(giOffset, SetStartingOffset);
AUTO_CMD_INT(giConnections, SetConnections); // Specifies the number of connections to attempt.
AUTO_CMD_INT(giDelay, SetDelay); // Specifies the delay between connections, in milliseconds.
AUTO_CMD_STRING(gAccountServer, SetServer); // Specifies the address of the AccountServer to attack.
AUTO_CMD_STRING(gLoginServer, SetLoginServer); // Specifies the address of the LoginServer to attack.
AUTO_CMD_INT(giAccountHammerMode, AccountHammer);
AUTO_CMD_INT(giLoginHammerMode, LoginHammer);
AUTO_CMD_INT(giChatAuthHammerMode, ChatAuthHammer);
AUTO_CMD_INT(giChatAuthNumRelays, ChatAuthNumRelays); // Set to the number of relays expect, only used for initializing stash
AUTO_CMD_INT(giServerHammerMode, ServerHammer);
AUTO_CMD_FLOAT(gfPercentPass, AuthFrequency);
AUTO_CMD_FLOAT(gfPercentCreate, CreationFrequency);
AUTO_CMD_STRING(gAccountPrefix, AccountPrefix);
AUTO_CMD_STRING(gMapName, SetGameServer);
AUTO_CMD_INT(giAccountPacketDeath, AccountPacketDeath);
AUTO_CMD_INT(giLoginPacketDeath, LoginPacketDeath);
AUTO_CMD_INT(giServerPacketDeath, ServerPacketDeath);
AUTO_CMD_FLOAT(giTimeout, SetTimeout);
AUTO_CMD_FLOAT(giTransferTimeout, SetTransferTimeout);
AUTO_CMD_INT(giRepeatAt, RepeatAt);
AUTO_CMD_INT(giIgnoreMapErrors, IgnoreMapErrors);
AUTO_CMD_INT(gbQuitOnSuccess, QuitOnSuccess);
AUTO_CMD_INT(gbQuitNonFatallyIfUnknownProduct, QuitNonFatallyIfUnknownProduct);
AUTO_CMD_STRING(gFixedPassword, FixedPassword);
AUTO_CMD_STRING(gPreEncryptedPassword, PreEncryptedPassword);
AUTO_CMD_INT(gbUsePreEncryptedPassword, UsePreEncryptedPassword);
AUTO_CMD_INT(gbTestAccountGuard, TestAccountGuard);


// Global stat tracking
int giCounter = 0;
float gfMaxMainLoop = 0.0;

// Attempts/failures/timeouts for each step
int giAccountConnect[3];
int giAccountTicket[3];
int giLoginConnect[3];
int giLoginAuth[3];
int giLoginCreationData[3];
int giLoginMaps[3];
int giLoginTransfer[3];
int giServerConnect[3];

int giChatGADData[3];
int giChatConnect[3];
int giChatAuth[3];
StashTable gstChatAuthRelays = NULL;

// Timing avg/min/max
float gfAccountConnect[3];
float gfAccountTicket[3];
float gfAccount[3];
float gfLoginConnect[3];
float gfLoginAuth[3];
float gfLoginCreationData[3];
float gfLoginMaps[3];
float gfLoginTransfer[3];
float gfLogin[3];
float gfServerConnect[3];

float gfChatGADData[3];
float gfChatConnect[3];
float gfChatAuth[3];

// Other
int giAccountAuthFailure = 0;
int giAccountIntended = 0;
int giSuccess = 0;

bool gbComplete = false;
int giMainTimer;

#define PARALLEL_MODE 1

LoginAttempt **gppActiveAttempts = NULL;
LoginAttempt **gppConnectedClients = NULL;

void DumpStats()
{
	printf("\nTOTAL TIME ELAPSED: (%0.3f)\n", timerElapsed(giMainTimer));
	printf("NUMBER OF OUTSTANDING CONNECTIONS: %d\n", eaSize(&gppActiveAttempts));
	printf("NUMBER OF CONNECTIONS COMPLETED: %d\n", giCounter);
	printf("NUMBER OF SUCCESSES: %d\n", giSuccess);
#ifdef PARALLEL_MODE
	printf("MAX MAIN LOOP LENGTH: %0.3f\n", gfMaxMainLoop);
#endif

	if(giAccountHammerMode)
	{
		printf("\nAccount Server---------------------------------------------------\n");
		printf("Stage\t\tTries\tAvg\tMin\tMax\tTimeout\tFailure\n");
		printf("Connection\t%d\t%0.3f\t%0.3f\t%0.3f\t%d\t%d\n", giAccountConnect[0], gfAccountConnect[0]/giAccountConnect[0], gfAccountConnect[1], gfAccountConnect[2], giAccountConnect[2], giAccountConnect[1]);
		printf("Authorization\t%d\t%0.3f\t%0.3f\t%0.3f\t%d\t%d\n", giAccountTicket[0], gfAccountTicket[0]/giAccountTicket[0], gfAccountTicket[1], gfAccountTicket[2], giAccountTicket[2], giAccountTicket[1]);
		printf("Total\t\t%d\t%0.3f\t%0.3f\t%0.3f\n", giAccountConnect[0], gfAccount[0]/giAccountConnect[0], gfAccount[1], gfAccount[2]);

		printf("\nNumber of tries successfully authorized: %d\n", giLoginHammerMode?giLoginConnect[0]:giSuccess);
		printf("Number of correct authorization results: %d\n", giAccountIntended);
		printf("-----------------------------------------------------------------\n");
	}

	if (giChatAuthHammerMode)
	{
		StashTableIterator iter = {0};
		StashElement elem = NULL;
		int iRelayCount = 0;

		printf("\nChat Relay---------------------------------------------------\n");
		printf("Stage\t\tSuccess\tAvg\tMin\tMax\tTimeout\tFailure\n");
		printf("GameAccountData\t%d\t%0.3f\t%0.3f\t%0.3f\t%d\t%d\n", giChatGADData[0], 
			gfChatGADData[0]/giChatGADData[0], 
			gfChatGADData[1], gfChatGADData[2], giChatGADData[2], giChatGADData[1]);
		printf("Connection\t%d\t%0.3f\t%0.3f\t%0.3f\t%d\t%d\n", giChatConnect[0], 
			gfChatConnect[0]/giChatConnect[0], 
			gfChatConnect[1], gfChatConnect[2], giChatConnect[2], giChatConnect[1]);
		printf("Authorization\t%d\t%0.3f\t%0.3f\t%0.3f\t%d\t%d\n", giChatAuth[0], 
			gfChatAuth[0]/giChatAuth[0], 
			gfChatAuth[1], gfChatAuth[2], giChatAuth[2], giChatAuth[1]);

		stashGetIterator(gstChatAuthRelays, &iter);
		while (stashGetNextElement(&iter, &elem))
		{
			int iCount = stashElementGetInt(elem);
			char *pIP = stashElementGetStringKey(elem);
			printf("Chat Relay #%d: %d users\t[%s]\n", iRelayCount, iCount, pIP);
			iRelayCount++;
		}
		printf("%d expected relays used\n", iRelayCount);
	}

	if(giLoginConnect[0] == 0) return;

	printf("\nLogin Server-----------------------------------------------------\n");
	printf("Stage\t\tTries\tAvg\tMin\tMax\tTimeout\tFailure\n");
	printf("Connection\t%d\t%0.3f\t%0.3f\t%0.3f\t%d\t%d\n", giLoginConnect[0], gfLoginConnect[0]/giLoginConnect[0], gfLoginConnect[1], gfLoginConnect[2], giLoginConnect[2], giLoginConnect[1]);
	if(giLoginAuth[0] != 0) printf("Authorization\t%d\t%0.3f\t%0.3f\t%0.3f\t%d\t%d\n", giLoginAuth[0], gfLoginAuth[0]/giLoginAuth[0], gfLoginAuth[1], gfLoginAuth[2], giLoginAuth[2], giLoginAuth[1]);
	if(giLoginCreationData[0] != 0) printf("Creation data\t%d\t%0.3f\t%0.3f\t%0.3f\t%d\t%d\n", giLoginCreationData[0], gfLoginCreationData[0]/giLoginCreationData[0], gfLoginCreationData[1], gfLoginCreationData[2], giLoginCreationData[2], giLoginCreationData[1]);
	if(giLoginMaps[0] != 0) printf("Maps\t\t%d\t%0.3f\t%0.3f\t%0.3f\t%d\t%d\n", giLoginMaps[0], gfLoginMaps[0]/giLoginMaps[0], gfLoginMaps[1], gfLoginMaps[2], giLoginMaps[2], giLoginMaps[1]);
	if(giLoginTransfer[0] != 0) printf("Transfer\t%d\t%0.3f\t%0.3f\t%0.3f\t%d\t%d\n", giLoginTransfer[0], gfLoginTransfer[0]/giLoginTransfer[0], gfLoginTransfer[1], gfLoginTransfer[2], giLoginTransfer[2], giLoginTransfer[1]);
	printf("Total\t\t%d\t%0.3f\t%0.3f\t%0.3f\n", giLoginConnect[0], gfLogin[0]/giLoginConnect[0], gfLogin[1], gfLogin[2]);
	printf("-----------------------------------------------------------------\n");

	if(giServerConnect[0] == 0) return;

	printf("\nGame Server------------------------------------------------------\n");
	printf("Stage\t\tTries\tAvg\tMin\tMax\tTimeout\tFailure\n");
	printf("Connection\t%d\t%0.2f\t%0.2f\t%0.2f\t%d\t%d\n", giServerConnect[0], gfServerConnect[0]/giServerConnect[0], gfServerConnect[1], gfServerConnect[2], giServerConnect[2], giServerConnect[1]);
	printf("-----------------------------------------------------------------\n");
}

static BOOL consoleCtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType){ 
		case CTRL_CLOSE_EVENT: 
		case CTRL_LOGOFF_EVENT: 
		case CTRL_SHUTDOWN_EVENT: 
		case CTRL_BREAK_EVENT: 
		case CTRL_C_EVENT: 
			if(gbComplete)
			{
				return FALSE;
			}

			DumpStats();
			return TRUE;

		default: 
			return FALSE; 
	} 
}

LoginAttempt *LoginAttemptCreate(int id, char *pAccount, bool bPass, bool bCreate)
{
	LoginAttempt *pAttempt;

	pAttempt = calloc(1, sizeof(LoginAttempt));
	pAttempt->id = id;
	pAttempt->timer = timerAlloc();
	pAttempt->state = LH_INITIAL;
	pAttempt->account = NULL;
	pAttempt->error = NULL;
	pAttempt->character = NULL;

	estrCopy(&pAttempt->account, &pAccount);
	pAttempt->bPass = bPass;
	pAttempt->bCreate = bCreate;

	if(giAccountHammerMode)
	{
		pAttempt->state = LH_ACCOUNT_INITIAL;
	}
	else if(giLoginHammerMode)
	{
		pAttempt->state = LH_LOGIN_INITIAL;
	}
	else
	{
		pAttempt->state = LH_DONE;
	}

	return pAttempt;
}

void LoginAttemptDestroy(LoginAttempt *pAttempt)
{
	estrDestroy(&pAttempt->account);
	estrDestroy(&pAttempt->error);
	estrDestroy(&pAttempt->character);
	timerFree(pAttempt->timer);
	free(pAttempt);
}

void LoginAttemptAddTimeData(const char *pchName, float *data, float time)
{
	PushMetricToTestServer(NULL, pchName, time, false);
	data[0] += time;
	if(!data[1] || time < data[1]) data[1] = time;
	if(!data[2] || time > data[2]) data[2] = time;
}

void LoginAttemptLogStats(LoginAttempt *pAttempt)
{
	if(giAccountHammerMode)
	{
		PushMetricToTestServer(NULL, "LoginHammer_AccountConnect", 0.0f, false);
		++giAccountConnect[0];

		if(pAttempt->bAccountConnectFailure)
		{
			PushMetricToTestServer(NULL, "LoginHammer_AccountConnectFailure", 0.0f, false);
			++giAccountConnect[1];
		}
		else if(pAttempt->bAccountConnectTimeout)
		{
			PushMetricToTestServer(NULL, "LoginHammer_AccountConnectTimeout", 0.0f, false);
			++giAccountConnect[2];
		}
		else
		{
			PushMetricToTestServer(NULL, "LoginHammer_AccountTicket", 0.0f, false);
			++giAccountTicket[0];

			if(pAttempt->bAccountTicketFailure)
			{
				PushMetricToTestServer(NULL, "LoginHammer_AccountTicketFailure", 0.0f, false);
				++giAccountTicket[1];
			}
			else if(pAttempt->bAccountTicketTimeout)
			{
				PushMetricToTestServer(NULL, "LoginHammer_AccountTicketTimeout", 0.0f, false);
				++giAccountTicket[2];
			}
			else
			{
				if(giLoginHammerMode && pAttempt->bAccountAuthed)
				{
					PushMetricToTestServer(NULL, "LoginHammer_LoginConnect", 0.0f, false);
					++giLoginConnect[0];
				}
				else if(!giLoginHammerMode && pAttempt->bAccountAuthed)
				{
					PushMetricToTestServer(NULL, "LoginHammer_Success", 0.0f, false);
					++giSuccess;
				}

				if(pAttempt->bAccountAuthed == pAttempt->bPass)
				{
					PushMetricToTestServer(NULL, "LoginHammer_AccountIntended", 0.0f, false);
					++giAccountIntended;
				}
			}
		}

		LoginAttemptAddTimeData("LoginHammer_AccountConnectWait", gfAccountConnect, pAttempt->fAccountConnectWait);
		LoginAttemptAddTimeData("LoginHammer_AccountTicketWait", gfAccountTicket, pAttempt->fAccountTicketWait);
		LoginAttemptAddTimeData("LoginHammer_AccountWait", gfAccount, pAttempt->fAccountWait);
	}

	if(giLoginHammerMode)
	{
		if(!giAccountHammerMode)
		{
			PushMetricToTestServer(NULL, "LoginHammer_LoginConnect", 0.0f, false);
			++giLoginConnect[0];
		}
		else if(!pAttempt->bAccountAuthed)
		{
			return;
		}

		if(pAttempt->bLoginConnectFailure)
		{
			PushMetricToTestServer(NULL, "LoginHammer_LoginConnectFailure", 0.0f, false);
			++giLoginConnect[1];
		}
		else if(pAttempt->bLoginConnectTimeout)
		{
			PushMetricToTestServer(NULL, "LoginHammer_LoginConnectTimeout", 0.0f, false);
			++giLoginConnect[2];
		}
		else
		{
			PushMetricToTestServer(NULL, "LoginHammer_LoginAuth", 0.0f, false);
			++giLoginAuth[0];

			if(pAttempt->bLoginCharactersFailure)
			{
				PushMetricToTestServer(NULL, "LoginHammer_LoginAuthFailure", 0.0f, false);
				++giLoginAuth[1];
			}
			else if(pAttempt->bLoginCharactersTimeout)
			{
				PushMetricToTestServer(NULL, "LoginHammer_LoginAuthTimeout", 0.0f, false);
				++giLoginAuth[2];
			}
			else if(pAttempt->bCreate)
			{
				PushMetricToTestServer(NULL, "LoginHammer_LoginCreationData", 0.0f, false);
				++giLoginCreationData[0];

				if(pAttempt->bLoginCreationDataFailure)
				{
					PushMetricToTestServer(NULL, "LoginHammer_LoginCreationDataFailure", 0.0f, false);
					++giLoginCreationData[1];
				}
				else if(pAttempt->bLoginCreationDataTimeout)
				{
					PushMetricToTestServer(NULL, "LoginHammer_LoginCreationDataTimeout", 0.0f, false);
					++giLoginCreationData[2];
				}
				else
				{
					PushMetricToTestServer(NULL, "LoginHammer_LoginMaps", 0.0f, false);
					++giLoginMaps[0];

					if(pAttempt->bLoginMapsFailure)
					{
						PushMetricToTestServer(NULL, "LoginHammer_LoginMapsFailure", 0.0f, false);
						++giLoginMaps[1];
					}
					else if(pAttempt->bLoginMapsTimeout)
					{
						PushMetricToTestServer(NULL, "LoginHammer_LoginMapsTimeout", 0.0f, false);
						++giLoginMaps[2];
					}
					else
					{
						PushMetricToTestServer(NULL, "LoginHammer_LoginTransfer", 0.0f, false);
						++giLoginTransfer[0];

						if(pAttempt->bLoginTransferFailure)
						{
							PushMetricToTestServer(NULL, "LoginHammer_LoginTransferFailure", 0.0f, false);
							++giLoginTransfer[1];
						}
						else if(pAttempt->bLoginTransferTimeout)
						{
							PushMetricToTestServer(NULL, "LoginHammer_LoginTransferTimeout", 0.0f, false);
							++giLoginTransfer[2];
						}
						else if(giServerHammerMode)
						{
							PushMetricToTestServer(NULL, "LoginHammer_ServerConnect", 0.0f, false);
							++giServerConnect[0];
						}
						else
						{
							PushMetricToTestServer(NULL, "LoginHammer_Success", 0.0f, false);
							++giSuccess;
						}
					}
				}
			}
			else
			{
				PushMetricToTestServer(NULL, "LoginHammer_LoginMaps", 0.0f, false);
				++giLoginMaps[0];

				if(pAttempt->bLoginMapsFailure)
				{
					PushMetricToTestServer(NULL, "LoginHammer_LoginMapsFailure", 0.0f, false);
					++giLoginMaps[1];
				}
				else if(pAttempt->bLoginMapsTimeout)
				{
					PushMetricToTestServer(NULL, "LoginHammer_LoginMapsTimeout", 0.0f, false);
					++giLoginMaps[2];
				}
				else
				{
					PushMetricToTestServer(NULL, "LoginHammer_LoginTransfer", 0.0f, false);
					++giLoginTransfer[0];

					if(pAttempt->bLoginTransferFailure)
					{
						PushMetricToTestServer(NULL, "LoginHammer_LoginTransferFailure", 0.0f, false);
						++giLoginTransfer[1];
					}
					else if(pAttempt->bLoginTransferTimeout)
					{
						PushMetricToTestServer(NULL, "LoginHammer_LoginTransferTimeout", 0.0f, false);
						++giLoginTransfer[2];
					}
					else if(giServerHammerMode)
					{
						PushMetricToTestServer(NULL, "LoginHammer_ServerConnect", 0.0f, false);
						++giServerConnect[0];
					}
					else
					{
						PushMetricToTestServer(NULL, "LoginHammer_Success", 0.0f, false);
						++giSuccess;
					}
				}
			}
		}

		LoginAttemptAddTimeData("LoginHammer_LoginConnectWait", gfLoginConnect, pAttempt->fLoginConnectWait);
		LoginAttemptAddTimeData("LoginHammer_LoginAuthWait", gfLoginAuth, pAttempt->fLoginCharactersWait);
		LoginAttemptAddTimeData("LoginHammer_LoginCreationDataWait", gfLoginCreationData, pAttempt->fLoginCreationDataWait);
		LoginAttemptAddTimeData("LoginHammer_LoginMapsWait", gfLoginMaps, pAttempt->fLoginMapsWait);
		LoginAttemptAddTimeData("LoginHammer_LoginTransferWait", gfLoginTransfer, pAttempt->fLoginTransferWait);
		LoginAttemptAddTimeData("LoginHammer_LoginWait", gfLogin, pAttempt->fLoginWait);
	}

	if(giServerHammerMode)
	{
		if(pAttempt->bServerConnectFailure)
		{
			PushMetricToTestServer(NULL, "LoginHammer_ServerConnectFailure", 0.0f, false);
			++giServerConnect[1];
		}
		else if(pAttempt->bServerConnectTimeout)
		{
			PushMetricToTestServer(NULL, "LoginHammer_ServerConnectTimeout", 0.0f, false);
			++giServerConnect[2];
		}
		else
		{
			PushMetricToTestServer(NULL, "LoginHammer_Success", 0.0f, false);
			++giSuccess;
		}

		LoginAttemptAddTimeData("LoginHammer_ServerConnectWait", gfServerConnect, pAttempt->fServerConnectWait);
	}

	if (giChatAuthHammerMode)
	{
		if (pAttempt->fGADGetWait)
		{
			LoginAttemptAddTimeData("LoginHammer_GADGet", gfChatGADData, pAttempt->fGADGetWait);
			giChatGADData[0]++;
		}
		else
			giChatGADData[1]++;
	}
}

static int siPrintErrors = 0;
AUTO_CMD_INT(siPrintErrors, PrintErrors) ACMD_CMDLINE;

void LoginAttemptFinish(LoginAttempt *pAttempt)
{
 	if(siPrintErrors && pAttempt->error)
 	{
 		fprintf(stderr, "Error with connection %d: %s\n", pAttempt->id, pAttempt->error);
 	}

	++giCounter;

	LoginAttemptLogStats(pAttempt);

	if(giCounter == giConnections)
	{
		DumpStats();
	}

	eaFindAndRemove(&gppActiveAttempts, pAttempt);

	if(pAttempt->state == LH_SERVER_CONNECTED && giServerHammerMode)
	{
		eaPush(&gppConnectedClients, pAttempt);
	}
	else
	{
		LoginAttemptDestroy(pAttempt);
	}
}

void AddAssignedStats(Login2CharacterCreationData *characterCreationData, AttribType type, int points, int pointpenalty)
{
    NOCONST(AssignedStats) *assignedStats;
    assignedStats = StructCreateNoConst(parse_AssignedStats);
    assignedStats->eType = type;
    assignedStats->iPoints = points;
    assignedStats->iPointPenalty = pointpenalty;

    eaPush(&characterCreationData->assignedStats, assignedStats);
}

void LoginAttemptFillCharacterChoice(LoginAttempt *pAttempt, Login2CharacterCreationData *characterCreationData)
{
	characterCreationData->name = StructAllocString(pAttempt->character);

	if(!stricmp(GetProductName(), "FightClub"))
	{
		// FightClub characters
		eaPush(&characterCreationData->costumes, StructCreate(parse_PossibleCharacterCostume));
		characterCreationData->costumes[0]->pcCostume = allocAddString("QuickPlayDefault");
		characterCreationData->className = allocAddString("Balanced");
		characterCreationData->powerTreeName = allocAddString("Classic_Fire");
		eaPush(&characterCreationData->powerNodes, strdup("Classic_Fire.Auto.ThrowFire"));
		eaPush(&characterCreationData->powerNodes, strdup("Classic_Fire.Auto.Firestrike"));
	}
	else if(!stricmp(GetProductName(), "StarTrek"))
	{
		// StarTrek characters
		int i;

		characterCreationData->className = allocAddString("Starfleet_Tactical");
		eaPush(&characterCreationData->costumes, StructCreate(parse_PossibleCharacterCostume));
		characterCreationData->costumes[0]->pcCostume = allocAddString("Species_Sf_Human_M_01");
		characterCreationData->speciesName = allocAddString("Sf_Human_Male");

		for(i = 0; i < 4; ++i)
		{
			LoginPetInfo *pInfo = StructCreate(parse_LoginPetInfo);
			char buf[260];

			pInfo->pchType = strdup("RedShirt");
			sprintf(buf, "Crewman %d", i);
			pInfo->pchName = strdup(buf);
			eaPush(&characterCreationData->petInfo, pInfo);
		}

		{
			LoginPuppetInfo *pInfo = StructCreate(parse_LoginPuppetInfo);

			pInfo->pchType = strdup("Puppet_Lt_Cruiser");
			pInfo->pchName = strdup("Lt. Cruiser");
			eaPush(&characterCreationData->puppetInfo, pInfo);
		}
	}
    else if(!stricmp(GetProductName(), "Night"))
    {

        // Neverwinter characters.  These values lifted from Fighter.quickplay.
        eaPush(&characterCreationData->costumes, StructCreate(parse_PossibleCharacterCostume));
        characterCreationData->costumes[0]->pcCostume = allocAddString("Do_Not_Delete_Me");
        characterCreationData->className = allocAddString("Player_Guardian");
        characterCreationData->powerTreeName = allocAddString("Player_Guardian");
        characterCreationData->characterPathName = allocAddString("Player_Guardian");

        AddAssignedStats(characterCreationData, kAttribType_FirstUserDefined + 0, 10, 0);
        AddAssignedStats(characterCreationData, kAttribType_FirstUserDefined + 4, 5, 0);
        AddAssignedStats(characterCreationData, kAttribType_FirstUserDefined + 8, 5, 0);
        AddAssignedStats(characterCreationData, kAttribType_FirstUserDefined + 12, 2, 0);
        AddAssignedStats(characterCreationData, kAttribType_FirstUserDefined + 16, 2, 0);
        AddAssignedStats(characterCreationData, kAttribType_FirstUserDefined + 20, 0, 0);
    }
	else
	{
		if (gbQuitNonFatallyIfUnknownProduct)
		{
			printf("Product %s not yet supported, but gbQuitNonFatallyIfUnknownProduct set, succeeding\n", GetProductName());
			exit(0);
		}
		assertmsgf(0, "Product %s not supported yet, talk to Vinay Sarpeshkar for help!", GetProductName());
	}
}

void LoginAttemptAccountConnectCallback(NetLink *link, void *user_data);
void LoginAttemptAccountDisconnectCallback(NetLink *link, void *user_data);
int LoginAttemptAccountPacketCallback(Packet *pak, int cmd, NetLink *link, void *user_data);
void LoginAttemptLoginConnectCallback(NetLink *link, void *user_data);
void LoginAttemptLoginDisconnectCallback(NetLink *link, void *user_data);
int LoginAttemptLoginPacketCallback(Packet *pak, int cmd, NetLink *link, void *user_data);
void LoginAttemptServerConnectCallback(NetLink *link, void *user_data);
void LoginAttemptServerDisconnectCallback(NetLink *link, void *user_data);
int LoginAttemptServerPacketCallback(Packet *pak, int cmd, NetLink *link, void *user_data);

void LoginAttemptStartConnection(LoginAttempt *pAttempt)
{
	switch(pAttempt->state)
	{
	case LH_ACCOUNT_INITIAL:
		pAttempt->accountLink = commConnect(accountCommDefault(), LINKTYPE_UNSPEC, LINK_NO_COMPRESS|LINK_FORCE_FLUSH, getAccountServer(),
			DEFAULT_ACCOUNTSERVER_UNENCRYPTED_PORT, LoginAttemptAccountPacketCallback, LoginAttemptAccountConnectCallback, LoginAttemptAccountDisconnectCallback, 0);

		if(!pAttempt->accountLink)
		{
			pAttempt->bAccountConnectFailure = true;
			pAttempt->state = LH_DONE;
			LoginAttemptFinish(pAttempt);
			break;
		}

		if(giAccountPacketDeath)
		{
			linkSetPacketDisconnect(pAttempt->accountLink, giAccountPacketDeath);
		}

		//linkSetConnectTimeout(pAttempt->accountLink, giTimeout);
		linkSetTimeout(pAttempt->accountLink, giTimeout);
		linkSetKeepAlive(pAttempt->accountLink);
		linkSetUserData(pAttempt->accountLink, (void *)pAttempt);

		pAttempt->state = LH_ACCOUNT_CONNECT;
		timerStart(pAttempt->timer);
	xcase LH_LOGIN_INITIAL:
		pAttempt->loginLink = commConnect(commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH, gLoginServer,
			DEFAULT_LOGINSERVER_PORT, LoginAttemptLoginPacketCallback, LoginAttemptLoginConnectCallback, LoginAttemptLoginDisconnectCallback, 0);

		if(!pAttempt->loginLink)
		{
			pAttempt->bLoginConnectFailure = true;
			pAttempt->state = LH_DONE;
			LoginAttemptFinish(pAttempt);
			break;
		}

		if(giLoginPacketDeath)
		{
			linkSetPacketDisconnect(pAttempt->loginLink, giLoginPacketDeath);
		}

		//linkSetConnectTimeout(pAttempt->loginLink, giTimeout);
		linkSetTimeout(pAttempt->loginLink, giTimeout);
		linkSetKeepAlive(pAttempt->loginLink);
		linkSetUserData(pAttempt->loginLink, (void *)pAttempt);

		pAttempt->state = LH_LOGIN_CONNECT;
		pAttempt->fLoginWait -= timerElapsed(pAttempt->timer);
		pAttempt->fLoginConnectWait -= timerElapsed(pAttempt->timer);
	xcase LH_SERVER_INITIAL:
		pAttempt->serverLink = commConnect(commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH, pAttempt->server,
			pAttempt->iServerPort, LoginAttemptServerPacketCallback, LoginAttemptServerConnectCallback, LoginAttemptServerDisconnectCallback, 0);

		if(!pAttempt->serverLink)
		{
			pAttempt->bServerConnectFailure = true;
			pAttempt->state = LH_DONE;
			LoginAttemptFinish(pAttempt);
			break;
		}

		if(giServerPacketDeath)
		{
			linkSetPacketDisconnect(pAttempt->serverLink, giServerPacketDeath);
		}

		//linkSetConnectTimeout(pAttempt->serverLink, giTimeout);
		linkSetTimeout(pAttempt->serverLink, giTimeout);
		linkAutoPing(pAttempt->serverLink, 1);
		linkSetKeepAlive(pAttempt->serverLink);
		linkSetUserData(pAttempt->serverLink, (void *)pAttempt);

		pAttempt->state = LH_SERVER_CONNECT;
		pAttempt->fServerConnectWait -= timerElapsed(pAttempt->timer);
	}
}

void LoginAttemptAccountConnectCallback(NetLink *link, void *user_data)
{
	LoginAttempt *pAttempt = (LoginAttempt *)user_data;

	accountSendSaltRequest(pAttempt->accountLink, pAttempt->bPass ? pAttempt->account : "failme");

	pAttempt->fAccountConnectWait += timerElapsed(pAttempt->timer);
	pAttempt->state = LH_ACCOUNT_TICKET;
	pAttempt->fAccountTicketWait -= timerElapsed(pAttempt->timer);
}

void LoginAttemptAccountDisconnectCallback(NetLink *link, void *user_data)
{
	LoginAttempt *pAttempt = (LoginAttempt *)user_data;
	char *pcDisconnectReason = NULL;
	bool bTimeout;

	linkGetDisconnectReason(link, &pcDisconnectReason);
	bTimeout = strStartsWith(pcDisconnectReason, "commCheckTimeouts timeout exceeded");

	if((pAttempt->state < LH_ACCOUNT_INITIAL || pAttempt->state > LH_ACCOUNT_TICKET) && pAttempt->state != LH_DONE)
	{
		return;
	}

	if (pAttempt->state != LH_DONE && !bTimeout)
	{
		pAttempt->error = pcDisconnectReason;
		pcDisconnectReason = NULL;
	}

	switch(pAttempt->state)
	{
	case LH_ACCOUNT_CONNECT:
		pAttempt->bAccountConnectTimeout = bTimeout;
		pAttempt->bAccountConnectFailure = !bTimeout;
		pAttempt->fAccountConnectWait += timerElapsed(pAttempt->timer);
	xcase LH_ACCOUNT_TICKET:
		pAttempt->bAccountTicketTimeout = bTimeout;
		pAttempt->bAccountTicketFailure = !bTimeout;
		pAttempt->fAccountTicketWait += timerElapsed(pAttempt->timer);
	}

	pAttempt->fAccountWait += timerElapsed(pAttempt->timer);
	pAttempt->state = LH_DONE;
	estrDestroy(&pcDisconnectReason);
	LoginAttemptFinish(pAttempt);
}

int LoginAttemptAccountPacketCallback(Packet *pak, int cmd, NetLink *link, void *user_data)
{
	LoginAttempt *pAttempt = (LoginAttempt *)user_data;

	if(pAttempt->state != LH_ACCOUNT_TICKET)
	{
		return 1;
	}

	switch(cmd)
	{
	case FROM_ACCOUNTSERVER_NVPSTRUCT_LOGIN_SALT:
		{
			AccountNetStruct_FromAccountServerLoginSalt recvStruct = {0};
			const char * pPassword = gFixedPassword;
			const char * pPreEncryptedPassword = NULL;

			if (!ParserReceiveStructAsCheckedNameValuePairs(pak, parse_AccountNetStruct_FromAccountServerLoginSalt, &recvStruct))
			{
				Errorf("Receive failure in FROM_ACCOUNTSERVER_NVPSTRUCT_LOGIN_SALT");
				return 0;
			}

			if (nullStr(pPassword))
			{
				pPassword = pAttempt->bPass ? pAttempt->account : "failme";
			}

			if (gbUsePreEncryptedPassword)
			{
				pPreEncryptedPassword = gPreEncryptedPassword;
			}

			accountSendLoginPacket(link,
				pAttempt->bPass ? pAttempt->account : "failme", // Login Field
				pPassword, // Password
				false, // Password is not already hashed
				recvStruct.iSalt, // Temporary salt
				recvStruct.pFixedSalt, // Fixed salt
				!gbDoNotSendMachineID || gbTestAccountGuard, // Whether or not to include a machine ID
				gPreEncryptedPassword); // Pre-encrypted password
		}
	xcase FROM_ACCOUNTSERVER_LOGIN_NEW:
		pAttempt->uAccountID = pktGetU32(pak);
		pAttempt->uTicketID = pktGetU32(pak);
		pAttempt->bAccountAuthed = true;

		pAttempt->fAccountTicketWait += timerElapsed(pAttempt->timer);
		pAttempt->fAccountWait += timerElapsed(pAttempt->timer);
		
		if(giLoginHammerMode)
		{
			pAttempt->state = LH_LOGIN_INITIAL;
			LoginAttemptStartConnection(pAttempt);

			// Calls LoginAttemptFinish on disconnect
			linkRemove(&link);
		}
		else
		{
			accountSendLoginValidatePacket(link, pAttempt->uAccountID, pAttempt->uTicketID);
		}
	xcase FROM_ACCOUNTSERVER_LOGIN:
		if (gbTestAccountGuard)
		{
			U32 uIP = linkGetIp(link); // Not our IP, but whatever--it doesn't matter here
			accountSendGenerateOneTimeCode(link, pAttempt->uAccountID, uIP, getMachineID());

			// The user would normally check their e-mail here, but we'll just pretend
			accountSendOneTimeCode(link, pAttempt->uAccountID, getMachineID(), "12345", "LoginHammer", uIP);
		}
		else
		{
			pAttempt->state = LH_DONE;
			// Calls LoginAttemptFinish on disconnect
			linkRemove(&link);
		}
	xcase FROM_ACCOUNTSERVER_ONETIMECODEVALIDATE_RESPONSE:
		pAttempt->state = LH_DONE;
		// Calls LoginAttemptFinish on disconnect
		linkRemove(&link);
	xcase FROM_ACCOUNTSERVER_LOGIN_FAILED:
	acase FROM_ACCOUNTSERVER_FAILED:
	acase FROM_ACCOUNTSERVER_LOGINVALIDATE_FAILED:
		pAttempt->fAccountTicketWait += timerElapsed(pAttempt->timer);
		pAttempt->fAccountWait += timerElapsed(pAttempt->timer);
		pAttempt->state = LH_DONE;
		
		// Calls LoginAttemptFinish on disconnect
		linkRemove(&link);
	xdefault:
		break;
	}

	return 1;
}

int giChatLoginAttempts = 0;
float gfChatLoginTotalTime = 0.0f;

static void ChatLoginAttemptDestroy(ChatAttempt *pAttempt)
{
	if (pAttempt->pData)
		StructDestroy(parse_ChatAuthData, pAttempt->pData);
	if (pAttempt->pLastCommand)
		free(pAttempt->pLastCommand);
	timerFree(pAttempt->timer);
	free(pAttempt);
}

static void ChatLoginAttemptConnectCallback(NetLink *link, void *user_data)
{
	ChatAttempt *pAttempt = (ChatAttempt*) user_data;
	ChatAuthData *pChatData = pAttempt->pData;
	Packet *pak = pktCreate(pChatData->pChatRelayLink, TOSERVER_GAME_MSG);
	pktSendU32(pak, CHATRELAY_AUTHENTICATE);
	pktSendU32(pak, pChatData->uAccountID);
	pktSendU32(pak, pChatData->uSecretValue);
	pktSendU32(pak, CHAT_CONFIG_SOURCE_PC_ACCOUNT);
	pktSend(&pak);
	pAttempt->eState = CHAT_ATTEMPT_AUTH;
	pAttempt->fChatRelayConnectWait += timerElapsed(pAttempt->timer);
}

static void ChatLoginAttemptDisconnectCallback(NetLink *link, void *user_data)
{
	ChatAttempt *pAttempt = (ChatAttempt*) user_data;
	char *pcDisconnectReason = NULL;
	bool bTimeout;

	linkGetDisconnectReason(link, &pcDisconnectReason);
	bTimeout = strStartsWith(pcDisconnectReason, "commCheckTimeouts timeout exceeded");
	estrDestroy(&pcDisconnectReason);
	
	switch (pAttempt->eState)
	{
	case CHAT_ATTEMPT_CONNECT:
		pAttempt->bFailed = true;
		pAttempt->fChatRelayConnectWait = timerElapsed(pAttempt->timer);
		PushMetricToTestServer(NULL, "ChatRelay_ConnectFailed", 0.0f, false);
		if (bTimeout)
			giChatConnect[2]++;
		else
			giChatConnect[1]++;
	xcase CHAT_ATTEMPT_AUTH:
		pAttempt->bFailed = true;
		pAttempt->fChatRelayAuthWait = timerElapsed(pAttempt->timer);
		LoginAttemptAddTimeData("ChatRelay_Connect", gfChatConnect, pAttempt->fChatRelayConnectWait);
		giChatConnect[0]++;
		PushMetricToTestServer(NULL, "ChatRelay_AuthFailed", 0.0f, false);
		if (bTimeout)
			giChatAuth[2]++;
		else
			giChatAuth[1]++;
	xdefault:
		LoginAttemptAddTimeData("ChatRelay_Connect", gfChatConnect, pAttempt->fChatRelayConnectWait);
		LoginAttemptAddTimeData("ChatRelay_Auth", gfChatAuth, pAttempt->fChatRelayAuthWait);
		giChatConnect[0]++;
		giChatAuth[0]++;
		break;
	}
	giChatLoginAttempts++;
	ChatLoginAttemptDestroy(pAttempt);
}

static void ChatLoginAttemptPacketCallback(Packet *pak, int cmd, NetLink *link, void *user_data)
{
	ChatAttempt *pAttempt = (ChatAttempt*) user_data;
	if (cmd == TOCLIENT_GAME_MSG)
	{
		int ref = pktGetBitsPack(pak,8);
		int lib = pktGetBits(pak,1);
		int command = pktGetBitsPack(pak,GAME_MSG_SENDBITS) | (lib * LIB_MSG_BIT);
		char *msg = pktGetStringTemp(pak);
		U32 iFlags = pktGetBits(pak, 32);
		enumCmdContextHowCalled eHow = pktGetBits(pak, 32);

		if (!strStartsWith(msg, "ChatAuth"))
			return;
		if (strstri(msg, "ChatAuthFailed"))
			pAttempt->bFailed = true;
		pAttempt->fChatRelayAuthWait += timerElapsed(pAttempt->timer);
		pAttempt->pLastCommand = StructAllocString(msg);
		pAttempt->eState = CHAT_ATTEMPT_DONE;
		linkRemove(&link);
	}
}

void LoginAttemptLoginConnectCallback(NetLink *link, void *user_data)
{
	LoginAttempt *pAttempt = (LoginAttempt *)user_data;
	Packet *pkt = pktCreate(pAttempt->loginLink, TOLOGIN_LOGIN2_BEGIN_LOGIN);
	pktSendBits(pkt, 1, 0);
	pktSendU32(pkt, LANGUAGE_ENGLISH);

	if(giAccountHammerMode)
	{
		pktSendString(pkt, ACCOUNT_FASTLOGIN_LABEL);
		pktSendU32(pkt, pAttempt->uAccountID);
		pktSendU32(pkt, pAttempt->uTicketID);
		pktSendString(pkt, getMachineID()); // No reason to not send the real machine ID
	}
	else
	{
		pktSendString(pkt, pAttempt->account);
	}

	pktSendU32(pkt, 0); // CRC
	pktSendString(pkt, ""); // affiliate
	pktSend(&pkt);

	pAttempt->fLoginConnectWait += timerElapsed(pAttempt->timer);
	pAttempt->state = LH_LOGIN_CHARACTERS;
	pAttempt->fLoginCharactersWait -= timerElapsed(pAttempt->timer);
}

void LoginAttemptLoginDisconnectCallback(NetLink *link, void *user_data)
{
	LoginAttempt *pAttempt = (LoginAttempt *)user_data;
	char *pcDisconnectReason = NULL;
	bool bTimeout;

	linkGetDisconnectReason(link, &pcDisconnectReason);
	bTimeout = strStartsWith(pcDisconnectReason, "commCheckTimeouts timeout exceeded");

	if (!pAttempt)
		return;
	if((pAttempt->state < LH_LOGIN_INITIAL || pAttempt->state > LH_LOGIN_TRANSFER) && pAttempt->state != LH_DONE)
	{
		return;
	}

	if (pAttempt->state != LH_DONE && !bTimeout)
	{
		pAttempt->error = pcDisconnectReason;
		pcDisconnectReason = NULL;
	}

	switch(pAttempt->state)
	{
	case LH_LOGIN_CONNECT:
		pAttempt->bLoginConnectTimeout = bTimeout;
		pAttempt->bLoginConnectFailure = !bTimeout;
	xcase LH_LOGIN_CHARACTERS:
		pAttempt->bLoginCharactersTimeout = bTimeout;
		pAttempt->bLoginCharactersFailure = !bTimeout;
		pAttempt->fLoginCharactersWait += timerElapsed(pAttempt->timer);
	xcase LH_LOGIN_CREATIONDATA:
		pAttempt->bLoginCreationDataTimeout = bTimeout;
		pAttempt->bLoginCreationDataFailure = !bTimeout;
		pAttempt->fLoginCreationDataWait += timerElapsed(pAttempt->timer);
	xcase LH_LOGIN_MAPS:
		pAttempt->bLoginMapsTimeout = bTimeout;
		pAttempt->bLoginMapsFailure = !bTimeout;
		pAttempt->fLoginMapsWait += timerElapsed(pAttempt->timer);
	xcase LH_LOGIN_TRANSFER:
		pAttempt->bLoginTransferTimeout = bTimeout;
		pAttempt->bLoginTransferFailure = !bTimeout;
		pAttempt->fLoginTransferWait += timerElapsed(pAttempt->timer);
		break;
	}

	pAttempt->fLoginWait += timerElapsed(pAttempt->timer);
	pAttempt->state = LH_DONE;
	estrDestroy(&pcDisconnectReason);
	LoginAttemptFinish(pAttempt);
}

int LoginAttemptLoginPacketCallback(Packet *pak, int cmd, NetLink *link, void *user_data)
{
	LoginAttempt *pAttempt = (LoginAttempt *)user_data;

	if(pAttempt->state < LH_LOGIN_CHARACTERS || pAttempt->state > LH_LOGIN_TRANSFER)
	{
		return 1;
	}

	switch(cmd)
	{
	case LOGIN2_TO_CLIENT_CHARACTER_SELECTION_DATA:
		{
			Login2CharacterSelectionData *characterSelectionData = StructCreate(parse_Login2CharacterSelectionData);
			ParserRecv(parse_Login2CharacterSelectionData, pak, characterSelectionData, 0);

			pAttempt->bLoginAuthed = true;

			if(!pAttempt->bCreate && eaSize(&characterSelectionData->characterChoices->characterChoices) > 0)
			{
				Login2CharacterChoice *pChoice;
				MapSearchInfo pInfo = {0};
				Packet *pkt = pktCreate(link, TOLOGIN_LOGIN2_CHOOSECHARACTER);

				pChoice = characterSelectionData->characterChoices->characterChoices[0];
				pAttempt->iServerContainerID = pChoice->containerID;
                pktSendU32(pkt, pChoice->containerID);
                pktSendBool(pkt, false); // NO UGC
				pktSend(&pkt);

				estrCopy2(&pAttempt->character, pChoice->savedName);

				pkt = pktCreate(pAttempt->loginLink, TOLOGIN_LOGIN2_REQUESTMAPSEARCH);
				pInfo.developerAllStatic = 0;
				pInfo.baseMapDescription.mapDescription = allocAddString(gMapName);
				ParserSend(parse_MapSearchInfo, pkt, NULL, &pInfo, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
				pktSend(&pkt);

				pAttempt->fLoginCharactersWait += timerElapsed(pAttempt->timer);
				pAttempt->state = LH_LOGIN_MAPS;
				pAttempt->fLoginMapsWait -= timerElapsed(pAttempt->timer);
			}
			else
			{
				Packet *pkt = pktCreate(link, TOLOGIN_LOGIN2_GET_CHARACTER_CREATION_DATA);
				pktSend(&pkt);

				pAttempt->bCreate = true;

				pAttempt->fLoginCharactersWait += timerElapsed(pAttempt->timer);
				pAttempt->state = LH_LOGIN_CREATIONDATA;
				pAttempt->fLoginCreationDataWait -= timerElapsed(pAttempt->timer);
			}

			StructDestroy(parse_Login2CharacterSelectionData, characterSelectionData);
		}
	xcase LOGINSERVER_TO_CLIENT_CHARACTER_CREATION_DATA:
		{
			Login2CharacterCreationData *characterCreationData = StructCreate(parse_Login2CharacterCreationData);
			MapSearchInfo pInfo = {0};
			Packet *pkt = pktCreate(link, TOLOGIN_LOGIN2_CREATECHARACTER);

			estrPrintf(&pAttempt->character, "Default_%06d", rand()%1000000);
			LoginAttemptFillCharacterChoice(pAttempt, characterCreationData);
			ParserSend(parse_Login2CharacterCreationData, pkt, NULL, characterCreationData, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
			pktSendBits(pkt, 1, 0);  // Edit UGC flag
			pktSend(&pkt);

			StructDestroy(parse_Login2CharacterCreationData, characterCreationData);

			pkt = pktCreate(pAttempt->loginLink, TOLOGIN_LOGIN2_REQUESTMAPSEARCH);
			pInfo.developerAllStatic = 0;
			pInfo.baseMapDescription.mapDescription = allocAddString(gMapName);
			ParserSend(parse_MapSearchInfo, pkt, NULL, &pInfo, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
			pktSend(&pkt);

			pAttempt->fLoginCreationDataWait += timerElapsed(pAttempt->timer);
			pAttempt->state = LH_LOGIN_MAPS;
			pAttempt->fLoginMapsWait -= timerElapsed(pAttempt->timer);
		}
	xcase LOGINSERVER_TO_CLIENT_NEWLY_CREATED_CHARACTER_ID:
		pAttempt->iServerContainerID = GetContainerIDFromPacket(pak);
	xcase LOGINSERVER_TO_CLIENT_POSSIBLE_GAMESERVERS:
		{
			PossibleMapChoices *pChoices = StructCreate(parse_PossibleMapChoices);
			PossibleMapChoice *pChosenMap = NULL;
			ParserRecv(parse_PossibleMapChoices, pak, pChoices, 0);

			//printf("Attempt %d: %d choices\n", pAttempt->id, eaSize(&pChoices->ppChoices));

			FOR_EACH_IN_EARRAY_FORWARDS(pChoices->ppChoices, PossibleMapChoice, pChoice)
			{
				if(pChosenMap || pChoice->bNotALegalChoice)
				{
					continue;
				}

				if(strstri(pChoice->baseMapDescription.mapDescription, gMapName))
				{
					pChosenMap = pChoice;
				}
				else if(!giServerHammerMode)
				{
					continue;
				}
				else if(!pChosenMap || (!pChosenMap->baseMapDescription.mapInstanceIndex && pChoice->baseMapDescription.mapInstanceIndex))
				{
					pChosenMap = pChoice;
				}
			}
			FOR_EACH_END

			if(pChosenMap)
			{
				Packet *pkt = pktCreate(link, TOLOGIN_LOGIN2_REQUESTGAMESERVERADDRESS);

				ParserSend(parse_PossibleMapChoice, pkt, NULL, pChosenMap, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
				pktSend(&pkt);

				linkSetTimeout(link, giTransferTimeout);

				pAttempt->fLoginMapsWait += timerElapsed(pAttempt->timer);
				pAttempt->state = LH_LOGIN_TRANSFER;
				pAttempt->fLoginTransferWait -= timerElapsed(pAttempt->timer);
			}
			else
			{
				pAttempt->bLoginMapsFailure = true;

				if (!giIgnoreMapErrors)
				{
					if(eaSize(&pChoices->ppChoices))
					{
						estrPrintf(&pAttempt->error, "Didn't find requested map \"%s\" in map selections.", gMapName);
					}
					else
					{
						estrPrintf(&pAttempt->error, "No map choices returned from Map Manager.");
					}
				}

				pAttempt->fLoginMapsWait += timerElapsed(pAttempt->timer);
				pAttempt->fLoginWait += timerElapsed(pAttempt->timer);
				pAttempt->state = LH_DONE;

				// Calls LoginAttemptFinish on disconnect
				linkRemove(&link);
			}

			StructDestroy(parse_PossibleMapChoices, pChoices);
		}
	xcase TOCLIENT_GAME_SERVER_ADDRESS:
		{
			ReturnedGameServerAddress *pAddress = StructCreate(parse_ReturnedGameServerAddress);
			pktGetBits(pak, 1);
			ParserRecv(parse_ReturnedGameServerAddress, pak, pAddress, 0);

			GetIpStr(ChooseIP(ipFromString(gLoginServer), pAddress->iIPs[0], pAddress->iIPs[1]), pAttempt->server, 32);
			pAttempt->iServerPort = pAddress->iPortNum;
			pAttempt->iServerID = pAddress->iContainerID;
			pAttempt->iServerCookie = pAddress->iCookie;

			pAttempt->fLoginTransferWait += timerElapsed(pAttempt->timer);
			pAttempt->fLoginWait += timerElapsed(pAttempt->timer);

			if(giServerHammerMode)
			{
				pAttempt->state = LH_SERVER_INITIAL;
				LoginAttemptStartConnection(pAttempt);
			}
			else
			{
				pAttempt->state = LH_DONE;
			}

			// Calls LoginAttemptFinish on disconnect
			linkRemove(&link);
			StructDestroy(parse_ReturnedGameServerAddress, pAddress);
		}
	xcase LOGINSERVER_TO_CLIENT_LOGIN_FAILED:
		estrCopy2(&pAttempt->error, pktGetStringTemp(pak));

		switch(pAttempt->state)
		{
		case LH_LOGIN_CHARACTERS:
			pAttempt->bLoginCharactersFailure = true;
			pAttempt->fLoginCharactersWait += timerElapsed(pAttempt->timer);
		xcase LH_LOGIN_CREATIONDATA:
			pAttempt->bLoginCreationDataFailure = true;
			pAttempt->fLoginCreationDataWait += timerElapsed(pAttempt->timer);
		xcase LH_LOGIN_MAPS:
			pAttempt->bLoginMapsFailure = true;
			pAttempt->fLoginMapsWait += timerElapsed(pAttempt->timer);
		xcase LH_LOGIN_TRANSFER:
			pAttempt->bLoginTransferFailure = true;
			pAttempt->fLoginTransferWait += timerElapsed(pAttempt->timer);
			break;
		}

		pAttempt->fLoginWait += timerElapsed(pAttempt->timer);
		pAttempt->state = LH_DONE;

		// Calls LoginAttemptFinish on disconnect
		linkRemove(&link);
		break;

	xcase LOGINSERVER_TO_CLIENT_REQUIRE_ONETIMECODE:
	case LOGINSERVER_TO_CLIENT_SAVENEXTMACHINE:
		devassertmsg(0, "Stop Running LoginHammer against machine-lock enabled accounts.");
	}

	return 1;
}

void LoginAttemptServerConnectCallback(NetLink *link, void *user_data)
{
	LoginAttempt *pAttempt = (LoginAttempt *)user_data;
	Packet *pkt = pktCreate(pAttempt->serverLink, TOSERVER_GAME_LOGIN);

	pktSendString(pkt, pAttempt->character); // name
	pktSendBitsPack(pkt, 4, pAttempt->iServerCookie); // cookie
	PutContainerIDIntoPacket(pkt, pAttempt->iServerContainerID); // container id
	pktSendBits(pkt, 1, 0); // needs file updates
	pktSendBits(pkt, 1, 0); // no timeout
	pktSendU32(pkt, LOCALE_ENGLISH); // locale
	pktSendBits(pkt, 1, 0); // client info string
	pktSendBits(pkt, 1, 0); // xbox net info
	pktSend(&pkt);

	pkt = pktCreate(pAttempt->serverLink, TOSERVER_DONE_LOADING);
	pktSend(&pkt);

	pAttempt->fServerConnectWait += timerElapsed(pAttempt->timer);
	pAttempt->state = LH_SERVER_CONNECTED;
}

void LoginAttemptServerDisconnectCallback(NetLink *link, void *user_data)
{
	LoginAttempt *pAttempt = (LoginAttempt *)user_data;
	char *pcDisconnectReason = NULL;
	bool bTimeout;

	linkGetDisconnectReason(link, &pcDisconnectReason);
	bTimeout = strStartsWith(pcDisconnectReason, "commCheckTimeouts timeout exceeded");

	if((pAttempt->state < LH_SERVER_INITIAL || pAttempt->state > LH_SERVER_CONNECTED) && pAttempt->state != LH_DONE)
	{
		return;
	}

	if (pAttempt->state != LH_DONE && !bTimeout)
	{
		pAttempt->error = pcDisconnectReason;
		pcDisconnectReason = NULL;
	}

	switch(pAttempt->state)
	{
	case LH_SERVER_CONNECT:
		pAttempt->bServerConnectTimeout = bTimeout;
		pAttempt->bServerConnectFailure = !bTimeout;
		pAttempt->fServerConnectWait += timerElapsed(pAttempt->timer);
	xcase LH_SERVER_CONNECTED:
		break;
	}

	pAttempt->state = LH_DONE;
	estrDestroy(&pcDisconnectReason);
	LoginAttemptFinish(pAttempt);
}

int LoginAttemptServerPacketCallback(Packet *pak, int cmd, NetLink *link, void *user_data)
{
	LoginAttempt *pAttempt = (LoginAttempt *)user_data;

	if(pAttempt->state < LH_SERVER_INITIAL)
	{
		return 1;
	}

	return 1;
}

int wmain(int argc, WCHAR** argv_wide)
{
	int i = 0, j = 0;
	int connTimer, mainLoopTimer;
	char **argv;

	EXCEPTION_HANDLER_BEGIN;
	ARGV_WIDE_TO_ARGV;
	WAIT_FOR_DEBUGGER;
	DO_AUTO_RUNS;

#if PARALLEL_MODE
	timerSetMaxTimers(32768);
#endif

	SetAppGlobalType(GLOBALTYPE_LOGINHAMMER);

	gimmeDLLDisable(1);
	FolderCacheChooseMode();
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleCtrlHandler, TRUE);
	cmdParseCommandLine(argc, argv);
	utilitiesLibStartup();
	setAccountServer(gAccountServer);

	SetRefSystemSuppressUnknownDicitonaryWarning_All(true);
	if(!giLoginHammerMode)
	{
		giServerHammerMode = 0;
	}

	srand(clock());

	for(i = 0; i < 3; ++i)
	{
		giAccountConnect[i] = 0;
		giAccountTicket[i] = 0;
		giLoginConnect[i] = 0;
		giLoginAuth[i] = 0;
		giLoginCreationData[i] = 0;
		giLoginMaps[i] = 0;
		giLoginTransfer[i] = 0;
		giServerConnect[i] = 0;
		giChatGADData[i] = 0;
		giChatConnect[i] = 0;
		giChatAuth[i] = 0;

		gfAccountConnect[i] = 0.0;
		gfAccountTicket[i] = 0.0;
		gfAccount[i] = 0.0;
		gfLoginConnect[i] = 0.0;
		gfLoginAuth[i] = 0.0;
		gfLoginCreationData[i] = 0.0;
		gfLoginMaps[i] = 0.0;
		gfLoginTransfer[i] = 0.0;
		gfLogin[i] = 0.0;
		gfServerConnect[i] = 0.0;
		gfChatGADData[i] = 0.0;
		gfChatConnect[i] = 0.0;
		gfChatAuth[i] = 0.0;
	}
	gstChatAuthRelays = stashTableCreateWithStringKeys(giChatAuthNumRelays, StashDeepCopyKeys_NeverRelease);

	i = 0;

#if TESTSERVER_WORKS
	ClearGlobalOnTestServer(NULL, "LoginHammer_AccountConnect");
	ClearGlobalOnTestServer(NULL, "LoginHammer_AccountConnectFailure");
	ClearGlobalOnTestServer(NULL, "LoginHammer_AccountConnectTimeout");
	ClearGlobalOnTestServer(NULL, "LoginHammer_AccountTicket");
	ClearGlobalOnTestServer(NULL, "LoginHammer_AccountTicketFailure");
	ClearGlobalOnTestServer(NULL, "LoginHammer_AccountTicketTimeout");
	ClearGlobalOnTestServer(NULL, "LoginHammer_AccountConnectWait");
	ClearGlobalOnTestServer(NULL, "LoginHammer_AccountTicketWait");
	ClearGlobalOnTestServer(NULL, "LoginHammer_AccountWait");
	ClearGlobalOnTestServer(NULL, "LoginHammer_AccountIntended");

	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginConnect");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginConnectFailure");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginConnectTimeout");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginAuth");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginAuthFailure");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginAuthTimeout");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginCreationData");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginCreationDataFailure");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginCreationDataTimeout");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginMaps");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginMapsFailure");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginMapsTimeout");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginTransfer");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginTransferFailure");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginTransferTimeout");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginConnectWait");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginAuthWait");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginCreationDataWait");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginMapsWait");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginTransferWait");
	ClearGlobalOnTestServer(NULL, "LoginHammer_LoginWait");

	ClearGlobalOnTestServer(NULL, "LoginHammer_ServerConnect");
	ClearGlobalOnTestServer(NULL, "LoginHammer_ServerConnectFailure");
	ClearGlobalOnTestServer(NULL, "LoginHammer_ServerConnectTimeout");
	ClearGlobalOnTestServer(NULL, "LoginHammer_ServerConnectWait");

	ClearGlobalOnTestServer(NULL, "LoginHammer_Success");

	ClearGlobalOnTestServer(NULL, "ChatRelay_Connect");
	ClearGlobalOnTestServer(NULL, "ChatRelay_Auth");
	ClearGlobalOnTestServer(NULL, "ChatRelay_AuthRejected");
#endif

	connTimer = timerAlloc();
	giMainTimer = timerAlloc();
	mainLoopTimer = timerAlloc();
	timerStart(giMainTimer);
	timerStart(connTimer);
	timerStart(mainLoopTimer);

	while(!gbComplete)
	{
		bool complete = false;
		autoTimerThreadFrameBegin("main");

#if PARALLEL_MODE
		if(i < giConnections && timerElapsed(connTimer) >= giDelay / 1000.0)
#endif
		{
			char *pAccount = NULL;
			LoginAttempt *pAttempt;
			++i;
			++j;

			estrPrintf(&pAccount, "%s%06d", gAccountPrefix, (j+giOffset)%1000000);
			pAttempt = LoginAttemptCreate(i, pAccount, ((float)rand()/RAND_MAX <= gfPercentPass), ((float)rand()/RAND_MAX <= gfPercentCreate));
			estrDestroy(&pAccount);

			eaPush(&gppActiveAttempts, pAttempt);
			timerStart(connTimer);

			LoginAttemptStartConnection(pAttempt);
		}

		if(i == giConnections)
		{
			complete = true;
		}

		if(j == giRepeatAt)
		{
			j = 0;
		}

		utilitiesLibOncePerFrame(timerElapsed(giMainTimer), 0);
//		printf(" --- FRAME BOUNDARY ---------------------------------\n");
		commMonitor(accountCommDefault());
		commMonitor(commDefault());

#if PARALLEL_MODE
		if(eaSize(&gppActiveAttempts) > 0)
		{
			complete = false;
		}
#else
		while(eaSize(&gppActiveAttempts) > 0)
		{
			commMonitor(accountCommDefault());
			commMonitor(commDefault());
		}
#endif

		if(eaSize(&gppConnectedClients) > 0)
		{
			complete = false;
		}

		gbComplete = complete;

#if PARALLEL_MODE
		if(timerElapsed(mainLoopTimer) > gfMaxMainLoop)
		{
			gfMaxMainLoop = timerElapsed(mainLoopTimer);
		}

		timerStart(mainLoopTimer);
#endif
		autoTimerThreadFrameEnd();
	}

#if TESTSERVER_WORKS
	if(gbIsTestServerHostSet)
	{
		commFlushAndCloseAllComms(5.0f);
		return 0;
	}
#endif

	if (gbQuitOnSuccess)
	{
		Sleep(5000);
		exit(0);
	}

	while(true)
	{
		Sleep(1);
	}

	EXCEPTION_HANDLER_END;

	return 0;
}