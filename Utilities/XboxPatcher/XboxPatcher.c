#include "sysutil.h"
#include "wininclude.h"
#include "net.h"
#include "pcl_client.h"
#include "file.h"
#include "GlobalTypes.h"

#if _PS3
int ps3_main(int argc_in, const char** argv_in)
#elif _XBOX
int main(int argc_in, const char** argv_in)
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
#if PLATFORM_CONSOLE
	HINSTANCE hInstance = 0;
	LPSTR lpCmdLine = GetCommandLine();
#endif

	EXCEPTION_HANDLER_BEGIN

	//void *logo_data = NULL;
	//U32 logo_size = 0;
	PCL_Client *client = NULL;
	PCL_ErrorCode error;

	WAIT_FOR_DEBUGGER_LPCMDLINE

	SetAppGlobalType(GLOBALTYPE_XBOXPATCHER);
	initXboxDrives();

	error = pclConnectAndCreate(&client, "patchserver.crypticstudios.com", 7255, 100000, commDefault(), "", "XboxPatcher", NULL, NULL, NULL);

	pclDisconnectAndDestroy(client);
	client = NULL;

	EXCEPTION_HANDLER_END
	return 0;
}