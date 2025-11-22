
#include "wininclude.h"

#define USE_KEYBOARD_HOOK	0
#define USE_MSG_HOOK		0

#if 0
static void printfIgnored(const char* f, ...){
	// Do nothing.
}
#endif

#if USE_KEYBOARD_HOOK
static LRESULT CALLBACK keyboardHookFunc(int code, WPARAM wParam, LPARAM lParam){
	static S32 enterCount;
	static S32 isDown;
	static S32 wDown;
	static S32 ignoreUp;
	
	KBDLLHOOKSTRUCT* h = (KBDLLHOOKSTRUCT*)lParam;
	
	#if 0
		#define PRINT printfIgnored
	#else
		#define PRINT printf
	#endif
	
	#if 0
		#define RETURN(x) { enterCount--; /*PRINT("[%d:%d] exit:  %d\n", _getpid(), GetCurrentThreadId(), enterCount);*/ return x; }
	#else
		#define RETURN(x) {return 0;}
	#endif
	
	enterCount++;
	
	//PRINT("[%d:%d] enter: %d\n", _getpid(), GetCurrentThreadId(), enterCount);
	
	//printf("Key pressed: 0x%8.8x, 0x%8.8x, 0x%8.8x\n", code, wParam, h->vkCode);
	
	if(code < 0){
		printf("WTF??  %d, %d, %d\n", code, wParam, lParam);
	}else{
		#if 0
		if(h->vkCode == 0x5b){
			isDown = wParam == WM_KEYDOWN;	
			//return 1;

			if(isDown){
				if(ignoreUp == 2){
					ignoreUp = 3;
				}else{
					ignoreUp = 0;
					RETURN(1);
				}
			}
			else if(!ignoreUp){
				// Released, but never pressed W.
				
				ignoreUp = 2;
				
				keybd_event(0x5b, 0, 0, 0);
				keybd_event(0x5b, 0, KEYEVENTF_KEYUP, 0);
				PRINT("1\n");
				RETURN(1);
			}
			else if(ignoreUp == 1){
				RETURN(1);
			}
		}
		
		if(h->vkCode == 0x57 && wParam == WM_KEYUP){
			wDown = 0;
		}

		if(isDown){
			if(h->vkCode == 0x57){
				if(wParam == WM_KEYDOWN){
					if(!wDown){
						//void suiMainWindowToggleDestY(void);
						
						wDown = 1;
						ignoreUp = 1;
						
						//suiMainWindowToggleDestY();
					}
				}
				
				//keybd_event(0x25, 0, wParam == WM_KEYDOWN ? 0 : KEYEVENTF_KEYUP, 0);
				PRINT("2\n");
				RETURN(1);
			}
		}
		#endif
		
		if(!(wParam & 1)){
			PRINT("Key pressed: 0x%8.8x, 0x%8.8x, 0x%8.8x, %c\n", code, wParam, h->vkCode, h->vkCode);
		}
	}
	
	RETURN(CallNextHookEx(0, code, wParam, lParam));
}

DWORD kbHookThread(void* unused){
	assert(SetWindowsHookEx(WH_KEYBOARD_LL, keyboardHookFunc, GetModuleHandle(NULL), 0));
	
	while(1){
		MSG msg;
		
		if(GetMessage(&msg, NULL, 0, 0) > 0){
			printf("msg: %p, %d, %d, %d\n", msg.hwnd, msg.wParam, msg.lParam, msg.message);
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}
#endif

#if USE_MSG_HOOK
static LRESULT CALLBACK msgHookFunc(int code, WPARAM wParam, LPARAM lParam){
	CWPSTRUCT* cwp = (CWPSTRUCT*)lParam;
	
	if(code < 0){
		printf("WTF??  %d, %d, %d\n", code, wParam, lParam);
	}else{
		if(!(wParam & 1)){
			printf(	"Msg sent: hwnd 0x%8.8x, msg 0x%8.8x, wParam 0x%8.8x, wParam 0x%8.8x\n",
					(U32)(intptr_t)cwp->hwnd,
					cwp->message,
					cwp->wParam,
					cwp->lParam);
		}
	}
	
	RETURN(CallNextHookEx(0, code, wParam, lParam));
}

DWORD msgHookThread(void* unused){
	if(!SetWindowsHookEx(WH_CALLWNDPROC, msgHookFunc, GetModuleHandle(NULL), 0)){
		U32 error = GetLastError();
		assertmsgf(0, "Can't set msg hook (error %d)", error);
	}
	
	while(1){
		MSG msg;
		
		if(GetMessage(&msg, NULL, 0, 0) > 0){
			printf("msg: %p, %d, %d, %d\n", msg.hwnd, msg.wParam, msg.lParam, msg.message);
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}
#endif

#if 0
static HANDLE hShell;

static LRESULT CALLBACK shellHookFunc(int code, WPARAM wParam, LPARAM lParam){
	printf("shell: %d, %d, %d\n", code, wParam, lParam);
	
	return CallNextHookEx(hShell, code, wParam, lParam);
}

static DWORD shellHookThread(void* unused){
	hShell = SetWindowsHookEx(WH_SHELL, shellHookFunc, GetModuleHandle(NULL), 0);
	
	while(1){
		MSG msg;
		
		if(GetMessage(&msg, NULL, 0, 0) > 0){
			printf("msg: %p, %d, %d, %d\n", msg.hwnd, msg.wParam, msg.lParam, msg.message);
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}

static DWORD printThread(void* unused){
	while(1){
		printf("time: %d\n", time(0));
		Sleep(500);
	}
}


static void doDeviceThing(void){
	RAWINPUTDEVICELIST device[1000];
	S32 count = ARRAY_SIZE(device);
	
	count = GetRawInputDeviceList(device, &count, sizeof(device[0]));
	
	FOR_BEGIN(i, count);
		char name[1000];
		S32 nameLen = sizeof(name);
		RID_DEVICE_INFO di;
		
		nameLen = GetRawInputDeviceInfo(device[i].hDevice, RIDI_DEVICENAME, name, &nameLen);
		
		name[nameLen] = 0;
		
		printf("Device %p: %s, %d\n", device[i].hDevice, name, device[i].dwType);
		
		di.cbSize = sizeof(di);
		
		GetRawInputDeviceInfo(device[i].hDevice, RIDI_DEVICEINFO, &di, &di.cbSize);
		
		switch(di.dwType){
			xcase RIM_TYPEHID:{
				RID_DEVICE_INFO_HID* d = &di.hid;
				
				printf("  HID: %d, %d, %d, %d, %d\n", d->dwProductId, d->dwVendorId, d->dwVersionNumber, d->usUsage, d->usUsagePage);
			}
			xcase RIM_TYPEKEYBOARD:{
				RID_DEVICE_INFO_KEYBOARD* k = &di.keyboard;
				
				printf(	"  KEY: %d, %d, %d, %d, %d, %d\n",
						k->dwKeyboardMode,
						k->dwNumberOfFunctionKeys,
						k->dwNumberOfIndicators,
						k->dwNumberOfKeysTotal,
						k->dwSubType,
						k->dwType);
			}
			xcase RIM_TYPEMOUSE:{
				RID_DEVICE_INFO_MOUSE* m = &di.mouse;
				
				printf("MOUSE: %d, %d, %d\n", m->dwId, m->dwNumberOfButtons, m->dwSampleRate);
			}
			xdefault:{
				printf("?????");
			}
		}
	FOR_END;
}
#endif

void runProfilerJunk(void){
	#if USE_KEYBOARD_HOOK
		_beginthread(kbHookThread, 0, NULL);
	#endif
	
	#if USE_MSG_HOOK
		_beginthread(msgHookThread, 0, NULL);
	#endif

	//_beginthread(shellHookThread, 0, NULL);
	//_beginthread(printThread, 0, NULL);
}