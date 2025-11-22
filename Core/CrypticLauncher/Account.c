#include "Account.h"
#include "LauncherMain.h"
#include "LauncherLocale.h"
#include "Environment.h"
#include "History.h"
#include "GameDetails.h"
#include "Shards.h"
#include "registry.h"
#include "UI.h" // temp hopefully
#include "UIDefs.h"

// NewControllerTracker
#include "NewControllerTracker_Pub.h"

// UtilitiesLib
#include "net.h"
#include "sock.h"
#include "systemspecs.h"
#include "accountnet.h"
#include "earray.h"
#include "EString.h"
#include "sysutil.h"
#include "Prefs.h"
#include "hoglib.h"
#include "fileutil.h"
#include "error.h"
#include "SimpleWindowManager.h"

// Common
#include "GlobalComm.h"

static void accountLinkShutdown(void);

// For accountnet
static AccountLoginType sLoginType;
static NetLink *sAccountLink = NULL;
static U32 sAccountID = 0;
static U32 sAccountTicketID = 0;
static char *sAccountError = NULL;
// If set, ignore legacy login packets.
static bool sbAccountServerSupportsLoginFailedPacket = false;	
// If set, use forced migration retry semantics if PW login fails.
static bool sbForceMigrate = false;
// Auth info
static char sPassword[MAX_PASSWORD] = {0};
// If set, the password field is already hashed
static bool sbPasswordHashed = false;

// Handle a packet coming from the Account Server.
static int accountHandleInput(Packet* pak, int cmd, NetLink* link, void *user_data)
{
	static bool sent_specs = false;
	AccountLoginType eAccountType = ACCOUNTLOGINTYPE_Default;

	switch (cmd)
	{
	case FROM_ACCOUNTSERVER_LOGIN_NEW:
		sAccountID = pktGetU32(pak);
		sAccountTicketID = pktGetU32(pak);
		if (sbForceMigrate)
		{
			// Check for data on LoginType
			if (pktCheckRemaining(pak, sizeof(U32)))
				eAccountType = pktGetU32(pak);
		}

		if (!sent_specs)
		{
			char tempbuf[2048];
			Packet *pak_out = pktCreate(link, TO_ACCOUNTSERVER_LOGSPECS);
			sent_specs = true;
			systemSpecsGetNameValuePairString(SAFESTR(tempbuf));
			pktSendU32(pak_out, sAccountID);
			pktSendString(pak_out, tempbuf);
			pktSend(&pak_out);
		}


		switch (LauncherGetState())
		{
		case CL_STATE_LOGGINGIN:
		case CL_STATE_LOGGINGINAFTERLINK:
			// If we're forcing migrate...
			if (sbForceMigrate)
			{
				if (eAccountType == ACCOUNTLOGINTYPE_Default &&
					LauncherGetState() == CL_STATE_LOGGINGIN &&
					sLoginType == ACCOUNTLOGINTYPE_CrypticAndPW)
				{
					// Retry login with just PW
					AccountValidateData validateData = {0};
					AccountSetLoginType(ACCOUNTLOGINTYPE_PerfectWorld);
					AccountLinkInit(true /* bUseConnectCB */);
				}
				else if (sLoginType == ACCOUNTLOGINTYPE_Cryptic || eAccountType == ACCOUNTLOGINTYPE_Cryptic)
				{
					UI_EnterConflictFlow(sAccountTicketID, "N%3A");
				}
				else
				{
					LauncherSetState(CL_STATE_LOGGEDIN);
				}
			}
			else
			{
				LauncherSetState(CL_STATE_LOGGEDIN);
			}
			// The next step is to get the shard list from the Controller Tracker.

			break;

		case CL_STATE_GETTINGPAGETICKET:
			UI_EnterMainLauncherPage(sAccountID, sAccountTicketID);
			accountLinkShutdown();
			break;

		case CL_STATE_GETTINGGAMETICKET:
			{
				char *error = NULL;
				if (LauncherRunGame(sAccountID, sAccountTicketID, &error))
				{
					UI_RequestClose(0);
				}
				else
				{
					UI_DisplayStatusMsg(STACK_SPRINTF(FORMAT_OK(_("Unable to start game client: %s")), error), false /* bSet5SecondTimeout */);
					LauncherSetState(CL_STATE_READY);
					UI_SetPatchButtonState(CL_BUTTONSTATE_PLAY);
				}
				accountLinkShutdown();
				break;
			}
		}
		break;

	case FROM_ACCOUNTSERVER_FAILED:
		if (sbAccountServerSupportsLoginFailedPacket)
		{
			break;
		}
		// Watch for tricky-ness here - this falls through in some cases.
		// I would prefer this were clearer.

	case FROM_ACCOUNTSERVER_LOGIN_FAILED:
		if (LauncherGetState() == CL_STATE_LOGGINGIN || LauncherGetState() == CL_STATE_LOGGINGINAFTERLINK)
		{
			const char *msg = NULL;
			LoginFailureCode code = LoginFailureCode_Unknown;

			if (cmd == FROM_ACCOUNTSERVER_LOGIN_FAILED)
			{
				code = pktGetU32(pak);
				msg = accountValidatorGetFailureReasonByCode(NULL, code);
				sbAccountServerSupportsLoginFailedPacket = true;
			}
			else
			{
				msg = pktGetStringTemp(pak);
			}

			// If this login failed due to a PW account conflict, redirect the user to the conflict page.
			if (cmd == FROM_ACCOUNTSERVER_LOGIN_FAILED && code == LoginFailureCode_UnlinkedPWCommonAccount && LauncherHasConflictURL())
			{
				// Get the conflict ticket ID from the packet.
				UI_EnterConflictFlow(pktGetU32(pak), NULL); // no prefix
			}
			// Forcing migrate and user authenticated with Cryptic credentials against an AS that is rejecting Cryptic logins
			else if (LauncherGetState() == CL_STATE_LOGGINGIN && sbForceMigrate && code == LoginFailureCode_CrypticDisabled)
			{
				// Get the conflict ticket ID from the packet.
				UI_EnterConflictFlow(pktGetU32(pak), "C%3A");
			}
			// If we're forcing migrate, and we failed the initial login, try again with Cryptic.
			else if (LauncherGetState() == CL_STATE_LOGGINGIN && sbForceMigrate && sLoginType == ACCOUNTLOGINTYPE_PerfectWorld && code == LoginFailureCode_NotFound)
			{
				AccountValidateData validateData = {0};

				// Try different login type.
				AccountSetLoginType(ACCOUNTLOGINTYPE_Cryptic);

				// Initiate new validate link.
				AccountLinkInit(true /* bUseConnectCB */);
			}
			// Just a normal login failure.
			else
			{
				// Display failure.
				UI_DisplayStatusMsg(cgettext(msg), true /* bSet5SecondTimeout */);

				// Clear the password field
				if (LauncherGetState() == CL_STATE_LOGGINGIN)
				{
					UI_ClearPasswordField();
				}

				LauncherSetState(CL_STATE_LOGINPAGELOADED);
			}
		}
		break;

	default:
		break;
	}
	return 1;
}

// Handle an error on the Account Server link.
static void accountHandleError(const char *reason, void *user_data)
{
	estrPrintf(&sAccountError, "%s", reason);
}

// Specifically for when we're in CL_STATE_LOGGINGIN *or* CL_STATE_LOGGINGINAFTERLINK, but only when
// forceMigrate on in launcher, but off in account server.  This situation at the moment (1/9/2013) is never hit
// TODO CODE COVERAGE: remove this code path and levels above when really sure this is never called (look for callers to AccountLinkInit() with bUseConnectCB=true)
static int accountHandleConnect(NetLink* link, void *user_data)
{
	if (!AccountLogin())
	{
		UI_RestartAtLoginPage(STACK_SPRINTF(FORMAT_OK(_("Unable to authenticate: %s")), NULL_TO_EMPTY(sAccountError)));
	}
	else
	{
		// successfully started login process
		// launcher state remains CL_STATE_LOGGINGIN or CL_STATE_LOGGINGINAFTERLINK for now, and will advance to next state later in the call chain
		// (would like to understand more of the state transition here)
	}

	return 0;
}

char *DEFAULT_LATELINK_getAccountServer(void);
char *OVERRIDE_LATELINK_getAccountServer(void)
{
	if (accountServerWasSet())
		return DEFAULT_LATELINK_getAccountServer();

	if (gQAMode)
		return ENV_QA_ACCOUNTSERVER_HOST;

	if (gDevMode)
		return ENV_DEV_ACCOUNTSERVER_HOST;

	if (gPWRDMode)
		return ENV_PWRD_ACCOUNTSERVER_HOST;

	if (gPWTMode)
		return ENV_PWT_ACCOUNTSERVER_HOST;

	return DEFAULT_LATELINK_getAccountServer();
}

void AccountLinkInit(bool bUseConnectCB)
{
	AccountValidateData validateData = {0};
	char username[MAX_LOGIN_FIELD];

	// Clear error state.
	estrDestroy(&sAccountError);

	validateData.eLoginType = sLoginType;
	validateData.login_cb = accountHandleInput;
	validateData.failed_cb = accountHandleError;
	if (bUseConnectCB)
	{
		validateData.connect_cb = accountHandleConnect;
	}
	validateData.userData = NULL;
	AccountGetUsername(SAFESTR(username));
	validateData.pLoginField = strdup(username);
	validateData.pPassword = sPassword;
	validateData.bPasswordHashed = sbPasswordHashed;

	// accountnet internally manages the lifetime of the account link
	// there may be times we come into this method where sAccountLink is not null - this is OK
	sAccountLink = accountValidateInitializeLinkEx(&validateData);
}

static void accountLinkShutdown(void)
{
	accountValidateCloseAccountServerLink();
	sAccountLink = NULL;
}

bool AccountLogin(void)
{
	char username[MAX_LOGIN_FIELD];
	AccountGetUsername(SAFESTR(username));
	return accountValidateStartLoginProcess(username);
}

bool AccountLinkWaitAndLogin(void)
{
	accountValidateWaitForLink(&sAccountLink, 5);
	return AccountLogin();
}

const char *AccountLastError(void)
{
	return cgettext(NULL_TO_EMPTY(sAccountError));
}

void AccountAppendDataToPacket(Packet* pak)
{
	pktSendString(pak, ACCOUNT_FASTLOGIN_LABEL);
	pktSendU32(pak, sAccountID);
	pktSendU32(pak, sAccountTicketID);
}

void AccountSetPassword(const char *password, bool bPasswordHashed)
{
	memset(sPassword, 0, sizeof(sPassword));
	strcpy(sPassword, password);
	sbPasswordHashed = bPasswordHashed;
}

const char *AccountGetPassword(void)
{
	return sPassword;
}

bool AccountGetPasswordHashed(void)
{
	return sbPasswordHashed;
}

void AccountSetLoginType(AccountLoginType loginType)
{
	sLoginType = loginType;
}

void AccountSetForceMigrate(bool bForceMigrate)
{
	sbForceMigrate = bForceMigrate;
}

#define REGKEY_USERNAME						"UserName"

bool AccountSetUsername(const char *username)
{
	// VAS 062113 - If we ever change where the Launcher reads/writes this username, we must also change gclLoadLoginConfig() and gclSaveLoginConfig() accordingly
	const char *launcherProductDisplayName = gdGetDisplayName(0);
	return RegistryBackedStrSet(launcherProductDisplayName, REGKEY_USERNAME, username, true /* bUseHistory */);
}

bool AccountGetUsername(char *username, int usernameMaxLength)
{
	// VAS 062113 - If we ever change where the Launcher reads/writes this username, we must also change gclLoadLoginConfig() and gclSaveLoginConfig() accordingly
	const char *launcherProductDisplayName = gdGetDisplayName(0);
	*username = 0; /* default */
	return RegistryBackedStrGet(launcherProductDisplayName, REGKEY_USERNAME, username, usernameMaxLength, true /* bUseHistory */);
}
