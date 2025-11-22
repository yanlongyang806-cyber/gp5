#include "LauncherSelfPatch.h"
#include "LauncherMain.h"
#include "LauncherLocale.h"
#include "patcher.h"
#include "Environment.h"
#include "UIDefs.h" // for CL_WINDOW_AUTOPATCH
#include "resource_CrypticLauncher.h" // only necessary for IDC_RETRIES, IDD_AUTOPATCH
#include "Account.h"
#include "UTF8.h"

// CommonLib
#include "accountCommon.h"

// UtilitiesLib
#include "sysutil.h"
#include "fileutil.h"
#include "net.h"
#include "ScratchStack.h"
#include "error.h"
#include "timing.h"
#include "utils.h"
#include "SimpleWindowManager.h" // TODO: remove this, since there should be no UI related code in here

// PatchClientLib
#include "pcl_client.h"
#include "pcl_client_struct.h"

// PATCHING DEFINES
#define LAUNCHERSELFPATCH_DEFAULT_TIMEOUT_SECONDS 60

// Force the Launcher to autoupdate, even if it ordinarily wouldn't.
static int sForceLauncherAutoPatch = 0;
AUTO_CMD_INT(sForceLauncherAutoPatch, forceAutoPatch) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

static int sAutoPatchTimeout = LAUNCHERSELFPATCH_DEFAULT_TIMEOUT_SECONDS;
AUTO_CMD_INT(sAutoPatchTimeout, autopatchtimeout) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

// this overrides ONLY the auto patch/cryptic launcher patch server.  NOT the game patch.
static char *sPatchServer = ENV_US_PATCH_SERVER_HOST;
AUTO_COMMAND ACMD_CMDLINE ACMD_ACCESSLEVEL(0); void server(char *str) { sPatchServer = strdup(str); }

static S64 sAutoPatchTimer = 0;
static bool sbAutoPatchShutdown = false;
static bool sbAutoPatchDidShutdown = false;

static int autoPatchDialogThread(PCL_Client **ppClient);


const char *getAutoPatchServer(void)
{
	bool isDefaultPatchServer = IsDefaultPatchServer(sPatchServer);

	if (isDefaultPatchServer && gPWRDMode)
	{
		return ENV_PWRD_PATCH_SERVER_HOST;
	}
	else if (isDefaultPatchServer && gPWTMode)
	{
		return ENV_PWT_PATCH_SERVER_HOST;
	}
	else
	{
		return sPatchServer;
	}
}

void LauncherSpawnNewAndQuitCurrent(const char* exeName, const char *username, const char *consoleContext)
{
	char *cmd = NULL;

	if (username)
	{
		char *input = NULL;

		estrPrintf(&cmd, "\"%s\" -readpassword 1 -passwordishashed %d %s", exeName, AccountGetPasswordHashed() ? 1 : 0, GetCommandLineWithoutExecutable());
		estrPrintf(&input, "%s\n%s\n", username, AccountGetPassword());
		printf("Restarting launcher after %s auto-patch (w/login): %s\n", consoleContext, cmd);
		system_w_input(cmd, input, estrLength(&input), true, false);
	}
	else
	{
		estrPrintf(&cmd, "\"%s\" %s", exeName, GetCommandLineWithoutExecutable());
		printf("Restarting launcher after %s auto-patch: %s\n", consoleContext, cmd);
		system_detach(cmd, 0, 0);
	}

	exit(0);
}

static void connectCallback(PCL_Client *client, bool updated, PCL_ErrorCode error, const char *error_details, void *userData)
{
	// did we autoupdate the launcher?
	if (updated)
	{
		// if STDIN or Bypass Login, then grab the username and pass it on to the spawn func
		if (gBypassPipe || gReadPasswordFromSTDIN)
		{
			char username[MAX_LOGIN_FIELD];
			assertmsg(AccountGetUsername(SAFESTR(username)), "First Chance Auto Update with STDIN or Bypass login should always have a username!");
			LauncherSpawnNewAndQuitCurrent((const char *)userData, username, "first chance");
		}
		else
		{
			LauncherSpawnNewAndQuitCurrent((const char *)userData, NULL, "first chance");
		}
	}
}


// TODO: This function needs more work:
//   -It should tell the user what the actual error is, so they have a reasonable chance of trying to troubleshoot it.
//   -PCL can't detect any sort of connection failure, due to limitations in the net code.  This should be fixed.
//   -CrypticLauncher should have some sort of a tool to help track down connectivity issues, like many other games do
//   -Any sort of way that this fails need to be reported to Error Tracker so we can keep tabs on these things.


// returns errorlevel 0 if success (no update)
// NOTE: with an autoupdate, the launcher quits and relaunches via LauncherSpawnNewAndQuitCurrent()
// returns errorlevel 1 if failed with single attempt
// returns errorlevel 2 if failed with multiple attempts
int LauncherSelfPatch(const char *productName)
{
	int errorlevel = 0;

	if (!gDoNotAutopatch)
	{
		char autoupdate_path[MAX_PATH];
		const char *patchServer = getAutoPatchServer();

		strcpy(autoupdate_path, getExecutableName());
		forwardSlashes(autoupdate_path);

		if (sForceLauncherAutoPatch || !PatcherIsDevelopmentPath(autoupdate_path))
		{
			PCL_Client *client = NULL;
			PCL_ErrorCode error;
			int attempts = 0;  // FIXME: I'm pretty sure this variable and what it controls is nascent, but I'm keeping it for the time being.
			NetComm *comm = NULL;
			char token[MAX_PATH];
			uintptr_t autopatch_thread = _beginthread(autoPatchDialogThread, 0, &client);

			PatcherGetToken(false /* bInitialDownload */, SAFESTR(token));

			do
			{
				if (client == NULL)
				{
					printf("Connecting to %s:%i\n", patchServer, PATCH_SERVER_PORT);
				}
				else
				{
					printf("The client was disconnected during a connect attempt\n");
					sAutoPatchTimer = 0;
					pclDisconnectAndDestroy(client);
					client = NULL;
					attempts++;
				}

				if (!comm)
				{
					comm = commCreate(0, 1);
				}

				error = pclConnectAndCreate(&client,
					patchServer,
					PATCH_SERVER_PORT,
					sAutoPatchTimeout,
					comm,
					"",
					token,
					autoupdate_path,
					connectCallback,
					autoupdate_path);

				if (error || !client || attempts >= 4)
				{
					// If we get here, we've disconnected during startup an excessive number of times.  This isn't actually the same as retries.
					errorlevel = attempts ? 2 : 1;
				}
				else
				{
					//pclSetKeepAliveAndTimeout(client, 30);
					pclSetRetryTimes(client, PATCH_RETRY_TIMEOUT, PATCH_RETRY_BACKOFF);
					sAutoPatchTimer = timerCpuTicks64();
					error = pclWait(client);
				}
			}
			while ((error == PCL_LOST_CONNECTION) && (errorlevel == 0));

			PatcherHandleError(error, true);

			if (errorlevel == 0)
			{
				sbAutoPatchShutdown = true;
				sAutoPatchTimer = 0;
				while(!sbAutoPatchDidShutdown)
				{
					WaitForSingleObject((HANDLE)autopatch_thread, 1000);
				}
				pclDisconnectAndDestroy(client);
				commDestroy(&comm);
			}
		}
		else
		{
			printf("Skipping self-patch because the client is in a known development path, and patching was not forced.\n");
		}
	}
	else
	{
		printf("Skipping self-patch because of command-line argument.\n");
	}

	return errorlevel;
}

static BOOL autoPatchDialogProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
		SetTimer(hDlg, 1, 100, NULL);
		break;
	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDCANCEL:
			// Handler for the red X in the corner
			exit(0);
			break;
		}
		break;
	case WM_TIMER:
		{
			PCL_Client **ppClient = pWindow->pUserData;
			if (!sbAutoPatchShutdown && *ppClient)
			{
				U32 autoPatchRemaining = 0;
				HWND retryWindow = GetDlgItem(hDlg, IDC_RETRIES);
				U32 retry_count = (*ppClient)->retry_count + 1;

				pclGetAutoupdateRemaining(*ppClient, &autoPatchRemaining);

				if (autoPatchRemaining)
				{
					F32 rem_num;
					char *rem_units;
					U32 rem_prec;
					humanBytes(autoPatchRemaining, &rem_num, &rem_units, &rem_prec);
					SetWindowText_UTF8(retryWindow, STACK_SPRINTF(FORMAT_OK(_("%.*f%s remaining")), rem_prec, rem_num, rem_units));
				}
				else if (retry_count > 1)
				{
					SetWindowText_UTF8(retryWindow, STACK_SPRINTF(FORMAT_OK(_("Tried connecting %d times.")), retry_count));
				}
				else
				{
					SetWindowText_UTF8(retryWindow, _("Tried connecting 1 time."));
				}
			}
		}
		break;
	}

	return FALSE;
}

static bool autoPatchTickProc(SimpleWindow *ppClient)
{
	static bool shown = false;
	if (sAutoPatchTimer && !shown && timerSeconds64(timerCpuTicks64() - sAutoPatchTimer) > 2)
	{
		shown = true;
		SimpleWindowManager_AddOrActivateWindow(CL_WINDOW_AUTOPATCH, 0, IDD_AUTOPATCH, false, autoPatchDialogProc, NULL, ppClient);
	}
	if (sbAutoPatchShutdown)
		return false;
	return true;
}

static int autoPatchDialogThread(PCL_Client **ppClient)
{
	EXCEPTION_HANDLER_BEGIN

	SimpleWindowManager_Run(autoPatchTickProc, (void*)ppClient);
	printf("AutopatchDialog thread shutting down\n");
	sbAutoPatchDidShutdown = true;

	EXCEPTION_HANDLER_END
	return 0;
}
