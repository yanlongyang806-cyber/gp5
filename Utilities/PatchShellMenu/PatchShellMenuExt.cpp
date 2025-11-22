// PatchShellMenuExt.cpp : Implementation of CPatchShellMenuExt

#include "stdafx.h"
#include "PatchShellMenuExt.h"

#include <sys/stat.h>
#include <string.h>
#include <string>

// CPatchShellMenuExt



int WideToUTF8StrConvert(const WCHAR* str, char* outBuffer, int outBufferMaxLength);
int UTF8ToWideStrConvert(const char *str, WCHAR *outBuffer, int outBufferMaxLength);
bool dirExists_UTF8(const char *pDirName);

#if 0
extern "C"
{
#include "../../libs/UtilitiesLib/utils/PatchDLLWrapper.h"
}


// Max H code that tries to do tricky stuff, looks quite useful, but would
//   need to get merged with the "patchme" logic, but disabled in favor
//   of hacky code below.
// This may need some of the fixes from below

/*
STDMETHODIMP CPatchShellMenuExt::Initialize(
	LPCITEMIDLIST folder,
	LPDATAOBJECT data_object,
	HKEY prog_id)
{
	HRESULT result = E_INVALIDARG;
	struct _stat32 file_status;
	PCL_ErrorCode error;
	UINT num_files = 0;

	USES_CONVERSION;

	patchDLLInitForShellExt();

	if(data_object)
	{
		FORMATETC format_etc = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
		STGMEDIUM stg_medium = { TYMED_HGLOBAL };
		HDROP drop;

		if(FAILED(data_object->GetData(&format_etc, &stg_medium)))
			return result;

		drop = (HDROP) GlobalLock(stg_medium.hGlobal);

		if(!drop)
			return result;

		num_files = DragQueryFile(drop, 0xFFFFFFFF, NULL, 0);
		if(num_files == 1 &&
		   (DragQueryFile(drop, 0, NULL, 0) < MAX_PATH) &&
		   DragQueryFile(drop, 0, dir_name, MAX_PATH))
		{
			result = S_OK;
		}
		else if(num_files > 1)
		{
			TCHAR tpath[MAX_PATH];
			char * path, link_dir[MAX_PATH], link_dir_temp[MAX_PATH];
			PCL_ErrorCode error;

			link_dir[0] = '\0';
			result = S_OK;
			for(UINT i = 0; i < num_files; i++)
			{
				if(DragQueryFile(drop, i, NULL, 0) < MAX_PATH)
				{
					DragQueryFile(drop, i, tpath, MAX_PATH);
					path = _strdup(T2CA(tpath));
					path_list.push_back(path);
					if(!link_dir[0])
					{
						error = patchDLLCheckCurrentLink(path, NULL, 0, NULL, NULL, 0, NULL, NULL, link_dir, MAX_PATH);
						if(error)
						{
							result = E_INVALIDARG;
							break;
						}
					}
					else
					{
						error = patchDLLCheckCurrentLink(path, NULL, 0, NULL, NULL, 0, NULL, NULL, link_dir_temp, MAX_PATH);
						if(error || _stricmp(link_dir, link_dir_temp) != 0)
						{
							result = E_INVALIDARG;
							break;
						}
					}
				}
				else
				{
					result = E_INVALIDARG;
					break;
				}
			}
		}

		GlobalUnlock(stg_medium.hGlobal);
		ReleaseStgMedium(&stg_medium);
	}
	else
	{
		BOOL success = SHGetPathFromIDList(folder, dir_name);
		result = E_INVALIDARG;
		if(success)
		{
			result = S_OK;
			num_files = 1;
			_stat32(T2CA(dir_name), &file_status);
			if(file_status.st_mode & _S_IFDIR)
			{
				num_files = 1;
				result = S_OK;
			}
		}
	}

	if(result != S_OK)
	{
		situation = MENU_NONE;
		dir_name[0] = 0;
		FinalRelease();
	}
	else
	{
		if(num_files == 0)
		{
			situation = MENU_NONE;
		}
		else if(num_files == 1)
		{
			bool is_dir;

			_stat32(T2CA(dir_name), &file_status);
			is_dir = !!(file_status.st_mode & _S_IFDIR);

			error = patchDLLCheckCurrentLink(T2CA(dir_name), NULL, 0, NULL, NULL, 0, NULL, NULL, NULL, 0);
			if(!error)
			{
				if(is_dir)
					situation = MENU_ONE_LINKED_DIR;
				else
					situation = MENU_ONE_LINKED_FILE;
			}
			else
			{
				if(is_dir)
					situation = MENU_ONE_UNLINKED_DIR;
				else
					situation = MENU_ONE_UNLINKED_FILE;
			}
		}
		else
		{
			situation = MENU_MANY_LINKED_PATHS;
		}
	}

	return result;
}

STDMETHODIMP CPatchShellMenuExt::QueryContextMenu(
	HMENU menu,
	UINT menu_index,
	UINT first_cmd,
	UINT last_cmd,
	UINT flags)
{
	if(flags & CMF_DEFAULTONLY)
		return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);

	if(situation != MENU_NONE && situation != MENU_ONE_UNLINKED_FILE)
	{
		HMENU popup_menu = CreatePopupMenu();
		UINT added = 0;

		if(popup_menu)
		{
			if(situation == MENU_ONE_UNLINKED_DIR ||
				situation == MENU_ONE_LINKED_DIR ||
				situation == MENU_ONE_LINKED_FILE)
			{
				if(first_cmd + added < last_cmd)
				{
					link_dialog_cmd = added++;
					AppendMenu(popup_menu, MF_STRING, first_cmd + link_dialog_cmd, _T("Link"));
				}
			}
			if(situation == MENU_ONE_LINKED_DIR)
			{
				if(first_cmd + added < last_cmd)
				{
					sync_dialog_cmd = added++;
					AppendMenu(popup_menu, MF_STRING, first_cmd + sync_dialog_cmd, _T("Sync"));
				}
			}

			InsertMenu(menu, menu_index, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
			InsertMenu(menu, menu_index, MF_BYPOSITION | MF_POPUP, (UINT_PTR)popup_menu, _T("Patcher") );
			if(situation == MENU_ONE_LINKED_DIR ||
				situation == MENU_ONE_LINKED_FILE ||
				situation == MENU_MANY_LINKED_PATHS)
			{
				if(first_cmd + added < last_cmd)
				{
					get_latest_dialog_cmd = added++;
					InsertMenu(menu, menu_index, MF_BYPOSITION, first_cmd + get_latest_dialog_cmd,
						_T("Patcher Get Latest"));
				}
			}
			InsertMenu(menu, menu_index, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

			return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, added);
		}
	}

	return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);
}

STDMETHODIMP CPatchShellMenuExt::GetCommandString(
	UINT cmd_id,
	UINT flags,
	UINT * reserved,
	LPSTR name,
	UINT max_ch)
{
	USES_CONVERSION;

	if(flags & GCS_HELPTEXT)
	{
		LPCTSTR text;

		if(link_dialog_cmd == cmd_id)
		{
			text = _T("Establishes a link between a folder and a project on the patch server");
		}
		else if(sync_dialog_cmd == cmd_id)
		{
			text = _T("Gets a named view of a project from the patch server");
		}
		else
			return E_INVALIDARG;

		if(flags & GCS_UNICODE)
		{
			if(name && text && max_ch > 0)
				lstrcpynW((LPWSTR)name, T2CW(text), max_ch);
		}
		else
		{
			if(name && text && max_ch > 0)
				lstrcpynA(name, T2CA(text), max_ch);
		}

		return S_OK;
	}
	else
	{
		return E_INVALIDARG;
	}
}

STDMETHODIMP CPatchShellMenuExt::InvokeCommand(
	LPCMINVOKECOMMANDINFO cmd_info)
{
	int verb;

	USES_CONVERSION;

	if(HIWORD(cmd_info->lpVerb) != 0)
		return E_INVALIDARG;

	verb = LOWORD(cmd_info->lpVerb);
	
	if(link_dialog_cmd == verb)
	{
		patchDLLLinkDialog(T2CA(dir_name), false);
		return S_OK;
	}
	else if(sync_dialog_cmd == verb)
	{
		patchDLLSyncDialog(T2CA(dir_name), false);
		return S_OK;
	}
	else if(get_latest_dialog_cmd == verb)
	{
		char ** dirs = NULL;
		int count = 0;

		if(situation == MENU_ONE_LINKED_FILE || situation == MENU_ONE_LINKED_DIR)
		{
			dirs = (char**)malloc(sizeof(char*) * 1);
			dirs[0] = (char*)T2CA(dir_name);
			count = 1;
		}
		else if(situation == MENU_MANY_LINKED_PATHS)
		{
			dirs = (char**)malloc(sizeof(char*) * path_list.size());
			for(list<char*>::iterator iter = path_list.begin(); iter != path_list.end(); ++iter)
			{
				dirs[count++] = *iter;
			}
		}

		patchDLLGetLatestDialog((const char **)dirs, count, false);
		if(dirs)
			free(dirs);
	}

	return E_INVALIDARG;
}
*/

#else

static char *strstri(const char *s,const char *srch)
{
	int		len = (int)strlen(srch);

	for(;*s;s++)
	{
		if (_strnicmp(s,srch,len)==0)
			return (char*)s;
	}
	return 0;
}

static int strStartsWith(const char* str, const char* start)
{
	if(!str || !start)
		return 0;

	return _strnicmp(str, start, strlen(start))==0;
}

static int strEndsWith(const char* str, const char* ending)
{
	int strLength;
	int endingLength;
	if(!str || !ending)
		return 0;

	strLength = (int)strlen(str);
	endingLength = (int)strlen(ending);

	if(endingLength > strLength)
		return 0;

	if(_stricmp(str + strLength - endingLength, ending) == 0)
		return 1;
	else
		return 0;
}

static bool dirExists(const WCHAR *dir)
{
	struct _stat32 file_status;
	if (0==_wstat32(dir, &file_status))
		if (file_status.st_mode & _S_IFDIR)
			return true;
	return false;
}

static bool fileIsReadOnly(const char *filename)
{
	struct _stat32 file_status;
	if (0==_stat32(filename, &file_status))
		if (file_status.st_mode & _S_IWRITE)
			return false;
	return true;
}

unsigned int registryReadInt(HKEY hKeyRoot, const char *keyName, const char *valueName)
{
	HKEY key;
	int result;
	DWORD valueType;
	DWORD valueSize;
	unsigned int value=0;

	result = RegCreateKeyExA(
		hKeyRoot,						// handle to open key
		keyName,						// subkey name
		0,								// reserved
		NULL,							// class string
		REG_OPTION_NON_VOLATILE,		// special options
		KEY_READ,						// desired security access
		NULL,							// inheritance
		&key,							// key handle 
		NULL							// disposition value buffer
		);

	if(ERROR_SUCCESS != result)
		return 0;

	valueSize = sizeof(value);

	result = RegQueryValueExA(
		key,	// handle to key
		valueName,		// value name
		NULL,			// reserved
		&valueType,		// type buffer
		(unsigned char*)&value,	// data buffer
		&valueSize				// size of data buffer
		);

	RegCloseKey(key);
	return value;
}

static int shellMenuGetOption(const char *option)
{
	return (int)registryReadInt(HKEY_CURRENT_USER, "SOFTWARE\\Cryptic\\Gimme", option);
}

bool CPatchShellMenuExt::isUnderGimmeControl(const char *path)
{
	char temp_buf[MAX_PATH];
	if (strEndsWith(path, ".bak"))
		return false;
	if (GetAsyncKeyState(VK_SHIFT) & 0x8000000)
		return true;
	if (!gimme_paths.size()) {
		gimme_paths.push_back(GimmeControlledPath("C:\\Cryptic\\", true));
		gimme_paths.push_back(GimmeControlledPath("C:\\biz\\", true));
	}
	if (dirExists_UTF8(path) && path[strlen(path)-1]!='\\') {
		strcpy_s(temp_buf, MAX_PATH, path);
		strcat_s(temp_buf, MAX_PATH, "\\");
		path = temp_buf;
	}
	for (unsigned int i=0; i<gimme_paths.size(); i++) {
		if (strStartsWith(path, gimme_paths[i].first))
			return gimme_paths[i].second;
	}
	if (path[1]!=':' || path[2]!='\\')
	{
		// Network drive or something, code below assumes X:\blarg
		return false;
	}
	// Query this path, and cache it
	char patchbuf[MAX_PATH];
	strcpy_s(patchbuf, MAX_PATH, path);
	do {
		char *s = strrchr(patchbuf+3, '\\');
		if (!s)
			break;
		*s = '\0';
		strcat_s(patchbuf, MAX_PATH, "\\.patch");
		if (dirExists_UTF8(patchbuf)) {
			s[1] = '\0';
			gimme_paths.push_back(GimmeControlledPath(_strdup(patchbuf), true));
			return true;
		}
		*s = '\0';
	} while (true);
	gimme_paths.push_back(GimmeControlledPath(_strdup(path), false));
	return false;
}

// Use this instead of __declspec(thread)
#define STATIC_THREAD_ALLOC_TYPE(var,type)			\
	static int tls_##var##_index = 0;		\
	int dummy_##var = (((!tls_##var##_index?(tls_##var##_index=TlsAlloc()):0)),	\
	((var = (type)TlsGetValue(tls_##var##_index))?0:(TlsSetValue(tls_##var##_index, var = (type) calloc(sizeof(*var), 1)))))


const char *T2CA_static(CONST TCHAR *str)
{
	USES_CONVERSION;
	char **buffer_p;
	size_t *buffer_len_p;
	STATIC_THREAD_ALLOC_TYPE(buffer_p, char**);
	STATIC_THREAD_ALLOC_TYPE(buffer_len_p, size_t*);

	const char *res = T2CA(str);
	size_t slen = strlen(res);
	if (slen >= *buffer_len_p) {
		*buffer_len_p = slen + 1;
		*buffer_p = (char*)realloc(*buffer_p, *buffer_len_p);
	}
	strcpy_s(*buffer_p, *buffer_len_p, res);
	return *buffer_p;
}

STDMETHODIMP CPatchShellMenuExt::Initialize(
	LPCITEMIDLIST folder,
	LPDATAOBJECT data_object,
	HKEY prog_id)
{
	HRESULT result = E_INVALIDARG;
	UINT num_files = 0;

	//USES_CONVERSION; // The functions that use this all call alloca, bad!

	if(data_object)
	{
		FORMATETC format_etc = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
		STGMEDIUM stg_medium = { TYMED_HGLOBAL };
		HDROP drop;

		if(FAILED(data_object->GetData(&format_etc, &stg_medium)))
			return result;

		FinalRelease(); // Free old path_list

		drop = (HDROP) GlobalLock(stg_medium.hGlobal);

		if(!drop)
			return result;

		TCHAR temp_dir_name[MAX_PATH];
		num_files = DragQueryFile(drop, 0xFFFFFFFF, NULL, 0);
		if(num_files == 1 &&
		   (DragQueryFile(drop, 0, NULL, 0) < MAX_PATH) &&
		   DragQueryFile(drop, 0, temp_dir_name, MAX_PATH))
		{
			WideToUTF8StrConvert(temp_dir_name, dir_name, sizeof(dir_name));
			result = S_OK;
		}
		else if(num_files > 1)
		{
			TCHAR tpath[MAX_PATH];

			result = S_OK;
			for(UINT i = 0; i < num_files; i++)
			{
				if(DragQueryFile(drop, i, NULL, 0) < MAX_PATH)
				{
					char * path;
					DragQueryFile(drop, i, tpath, MAX_PATH);
					path = _strdup(T2CA_static(tpath));
					path_list.push_back(path);
					strcpy_s(dir_name, sizeof(dir_name), T2CA_static(tpath));
				}
				else
				{
					result = E_INVALIDARG;
					break;
				}
			}
		}

		GlobalUnlock(stg_medium.hGlobal);
		ReleaseStgMedium(&stg_medium);
	}
	else
	{
		TCHAR temp_dir_name[MAX_PATH];
		BOOL success = SHGetPathFromIDList(folder, temp_dir_name);
		result = E_INVALIDARG;
		if(success)
		{
			WideToUTF8StrConvert(temp_dir_name, dir_name, sizeof(dir_name));
			result = S_OK;
			num_files = 1;
			if (dirExists_UTF8(dir_name))
			{
				num_files = 1;
				result = S_OK;
			}
		}
	}

	if(result != S_OK)
	{
		situation = MENU_NONE;
		dir_name[0] = 0;
		FinalRelease();
	}
	else
	{
		if(num_files == 0)
		{
			situation = MENU_NONE;
		}
		else if(num_files == 1)
		{
			bool is_dir = dirExists_UTF8(dir_name);

			if(isUnderGimmeControl(dir_name))
			{
				if(is_dir) {
					situation = MENU_ONE_LINKED_DIR;
					is_file = false;
					is_folder = true;
				} else {
					situation = MENU_ONE_LINKED_FILE;
					is_readonly_file = fileIsReadOnly(dir_name);
					is_writeable_file = !is_readonly_file;
					is_file = true;
					is_folder = false;
				}
			}
			else
			{
				if(is_dir)
					situation = MENU_ONE_UNLINKED_DIR;
				else
					situation = MENU_ONE_UNLINKED_FILE;
			}
		}
		else
		{
			situation = MENU_NONE;
			is_file = false;
			is_folder = false;
			is_writeable_file = false;
			is_readonly_file = false;
			for(int i=(int)path_list.size()-1; i>=0; i--)
			{
				char *s = path_list[i];
				if (isUnderGimmeControl(s)) {
					situation = MENU_MANY_LINKED_PATHS;
					if (dirExists_UTF8(s)) {
						is_folder = true;
					} else {
						// File
						is_file = true;
						if (fileIsReadOnly(s))
							is_readonly_file = true;
						else
							is_writeable_file = true;
					}
				} else {
					free(path_list[i]);
					if (path_list.size()>1)
						path_list[i] = path_list[path_list.size()-1];
					path_list.pop_back();
				}
			}
		}
	}

	return result;
}


// Get handle to menu bitmap
HBITMAP CPatchShellMenuExt::GetMenuBitmap(int resource_key)
{
	HDC hdc = 0;
	HDC hScreenDC = 0;
	int bmp_x, bmp_y;
	HBITMAP result = 0;
	HBITMAP hbmTemp;
	HBRUSH hBrush = 0;
	HICON hIcon = 0;
	RECT r;

	OutputDebugString(L"GetMenuBitmap()\n");
	if (bitmap_cache.find(resource_key) != bitmap_cache.end())
		return bitmap_cache[resource_key];

	// Get bitmap dimensions
	bmp_x = GetSystemMetrics(SM_CXMENUCHECK);
	bmp_y = GetSystemMetrics(SM_CYMENUCHECK);

	// Create bitmap
	hScreenDC = CreateDC(TEXT("DISPLAY"), NULL, NULL, NULL);
	hdc = CreateCompatibleDC(hScreenDC);
	if (!hdc)
		goto Cleanup;

	result = CreateCompatibleBitmap(hScreenDC, bmp_x, bmp_y);
	if (!result)
		goto Cleanup;
	OutputDebugString(L"  GetMenuBitmap() created bitmap\n");

	hbmTemp = (HBITMAP) SelectObject(hdc, result);

	// Draw bitmap background
	r.left = 0;
	r.right = bmp_x;
	r.top = 0;
	r.bottom = bmp_y;
	hBrush = CreateSolidBrush(RGB(255, 255, 255));
	FillRect(hdc, &r, hBrush);

	// Load icon
	extern HINSTANCE g_hInstance;
	hIcon = (HICON) LoadImage(g_hInstance, MAKEINTRESOURCE(resource_key), IMAGE_ICON, bmp_x, bmp_y, LR_DEFAULTCOLOR);
	if (!hIcon)
	{
		DeleteObject(result);
		result = 0;
		goto Cleanup;
	}
	DrawIconEx(hdc, (bmp_x - bmp_y) / 2, (bmp_y - bmp_y) / 2, hIcon, bmp_y, bmp_y, 0, NULL, DI_NORMAL);
	SelectObject(hdc, hbmTemp);

	bitmap_cache[resource_key] = result;

Cleanup:
	if (hScreenDC)
		DeleteDC(hScreenDC);
	if (hBrush)
		DeleteObject(hBrush);
	if (hdc)
		DeleteDC(hdc);
	if (hIcon)
		DestroyIcon(hIcon);
	OutputDebugString(L"END GetMenuBitmap()\n");

	return result;
}


void CPatchShellMenuExt::ClearMenuBitmaps()
{
	for (map<int, HBITMAP>::iterator it = bitmap_cache.begin();
		it != bitmap_cache.end(); ++it)
	{
		OutputDebugString(L"Deleting object\n");
		DeleteObject((*it).second);
	}
	bitmap_cache.clear();
}



STDMETHODIMP CPatchShellMenuExt::QueryContextMenu(
	HMENU menu,
	UINT menu_index,
	UINT first_cmd,
	UINT last_cmd,
	UINT flags)
{
	UINT added = 0;
	bool bDidCMB=false;

	if(flags & CMF_DEFAULTONLY)
		return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);

	MENUITEMINFO mii = {0};
	mii.cbSize = sizeof(mii);

	memset(&commands, 255, sizeof(commands)); // Set commands to invalid values so we don't accidentally grab a command with an old ID which is not allowed on this context

	if(situation != MENU_NONE && situation != MENU_ONE_UNLINKED_FILE && situation != MENU_ONE_UNLINKED_DIR)
	{
		if (!bDidCMB)
		{
			ClearMenuBitmaps();
			bDidCMB = true;
		}

		// Delete any existing "Gimme" submenu.  This is a workaround for an 
		// Explorer bug - it removes all normal menu items, but not submenu
		// items that we added previously.  This is on the main File menu -
		// for popup context menus, the menu must be generated afresh, so
		// everything is fine.
		char buf[_MAX_PATH];
		int count = GetMenuItemCount(menu);
		for (int j = 0; j < count; ++j)
		{
			GetMenuStringA(menu, j, buf, sizeof(buf) - 1, MF_BYPOSITION);
			if (strcmp(buf, "Gimme")==0 || strcmp(buf, "Switch To")==0 || strcmp(buf, "QuickRD")==0)
				DeleteMenu(menu, j, MF_BYPOSITION);
		}

		HBITMAP gimme_bitmap = GetMenuBitmap(IDI_GIMME);

		// Separator
		InsertMenu(menu, menu_index, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

		HMENU menu_to_add_to;
		int sub_index;
		if (shellMenuGetOption("NoSubMenu")) {
			menu_to_add_to = menu;
			sub_index = menu_index;
		} else {
			HMENU popup_menu = CreatePopupMenu();
			if (popup_menu) {
				menu_to_add_to = popup_menu;
				sub_index = 0;

				mii.fMask = MIIM_TYPE | MIIM_SUBMENU;
				mii.fType = MFT_STRING;
				mii.dwTypeData = _T("Gimme");
				mii.cch = (UINT)strlen("Gimme");
				mii.hSubMenu = popup_menu;
				if (gimme_bitmap)
				{
					mii.fMask |= MIIM_CHECKMARKS | MIIM_STATE;
					mii.fState = MFS_UNCHECKED;
					mii.hbmpChecked = 0;
					mii.hbmpUnchecked = gimme_bitmap;
				}
				InsertMenuItem(menu, menu_index, true, &mii);
				//InsertMenu(menu, menu_index, MF_BYPOSITION | MF_POPUP, (UINT_PTR)popup_menu, _T("Gimme") );

			} else {
				menu_to_add_to = menu;
				sub_index = menu_index;
			}
		}

		mii.fMask = MIIM_TYPE | MIIM_ID;
		mii.fType = MFT_STRING;

		if (gimme_bitmap && menu_to_add_to == menu) // Only add bitmap on the root for now
		{
			mii.fMask |= MIIM_CHECKMARKS | MIIM_STATE;
			mii.fState = MFS_UNCHECKED;
			mii.hbmpChecked = 0;
			mii.hbmpUnchecked = gimme_bitmap;
		}

#define ADDCMD(var, str)												\
			if(first_cmd + added < last_cmd)							\
			{															\
				commands.var = added++;									\
				mii.wID = first_cmd + commands.var;						\
				mii.dwTypeData = _T(str);								\
				mii.cch = (UINT)strlen(str);							\
				InsertMenuItem(menu_to_add_to, sub_index, true, &mii);	\
				sub_index++;											\
			}

		if (situation == MENU_ONE_LINKED_FILE || situation == MENU_MANY_LINKED_PATHS)
		{
			if (GetAsyncKeyState(VK_SHIFT) & 0x8000000)
				is_readonly_file = is_writeable_file = true; // Show all options

			ADDCMD(cmd_getlatest, "Get Latest Version");
			if (is_writeable_file || is_folder) {
				ADDCMD(cmd_checkin, "Checkin...");
				ADDCMD(cmd_checkpoint, "Checkpoint...");
				ADDCMD(cmd_undocheckout, "Undo Checkout...");
			}
			if (is_readonly_file || is_folder) {
				ADDCMD(cmd_checkout, "Checkout");
				ADDCMD(cmd_checkout_noedit, "Checkout Noedit");
			}
			if (is_file && situation != MENU_MANY_LINKED_PATHS) {
				ADDCMD(cmd_stat, "Stat...");
				ADDCMD(cmd_diff, "Diff...");
			}
			ADDCMD(cmd_remove, "Remove...");
		}
		if(situation == MENU_ONE_LINKED_DIR)
		{
			ADDCMD(cmd_getlatest_fold, "Get Latest Version");
			ADDCMD(cmd_checkout_fold, "Checkout Folder");
			ADDCMD(cmd_checkin_fold, "Checkin Folder...");
			ADDCMD(cmd_checkpoint_fold, "Checkpoint Folder...");
			ADDCMD(cmd_undocheckout_fold, "Undo Checkout Folder...");
			ADDCMD(cmd_diff_fold, "Diff Folder...");
			ADDCMD(cmd_remove_fold, "Remove Folder...");
			ADDCMD(cmd_switch, "Switch branches...");
			ADDCMD(cmd_backup, "Backup...");
			ADDCMD(cmd_restore, "Restore from Backup...");
		}
#undef ADDCMD
			
// Need to reuse the appropriate above variable
// 			if(!shellMenuGetOption("NoSubMenu") &&
//				(situation == MENU_ONE_LINKED_DIR ||
// 				situation == MENU_ONE_LINKED_FILE ||
// 				situation == MENU_MANY_LINKED_PATHS))
// 			{
// 				if(first_cmd + added < last_cmd)
// 				{
// 					cmd_getlatest = added++;
// 					InsertMenu(menu, menu_index, MF_BYPOSITION, first_cmd + cmd_getlatest,
// 						_T("Gimme Get Latest"));
// 				}
// 			}

		InsertMenu(menu, menu_index, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

		if (situation == MENU_ONE_LINKED_DIR && shellMenuGetOption("ShowSwitchTo"))
		{
			HMENU popup_menu = CreatePopupMenu();
			if (popup_menu) {
				mii.fMask = MIIM_TYPE | MIIM_SUBMENU;
				mii.fType = MFT_STRING;
				mii.dwTypeData = _T("Switch To");
				mii.cch = (UINT)strlen("Switch To");
				mii.hSubMenu = popup_menu;
				InsertMenuItem(menu, menu_index, true, &mii);
				InsertMenu(menu, menu_index, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

				mii.fMask = MIIM_TYPE | MIIM_ID;
				mii.fType = MFT_STRING;
#define ADDSWITCHTO(var, str)													\
					if(first_cmd + added < last_cmd)							\
					{															\
						commands.switchto.var = added++;						\
						mii.wID = first_cmd + commands.switchto.var;			\
						mii.dwTypeData = _T(str);								\
						mii.cch = (UINT)strlen(str);							\
						InsertMenuItem(popup_menu, 0, false, &mii);				\
					}


				if (strstri(dir_name, "\\src\\")) {
					ADDSWITCHTO(data, "Switch to DATA");
				} else if (strstri(dir_name, "\\data\\")) {
					ADDSWITCHTO(src, "Switch to SRC");
				}
				if (strstri(dir_name, "\\object_library\\")) {
					ADDSWITCHTO(texlib, "Switch to texture_library");
				} else if (strstri(dir_name, "\\texture_library\\")) {
					ADDSWITCHTO(objlib, "Switch to object_library");
				}
				if (strstri(dir_name, "\\Core\\")) {
					ADDSWITCHTO(fc, "Switch to FightClub");
					ADDSWITCHTO(night, "Switch to Night");
					//ADDSWITCHTO(pa, "Switch to PrimalAge");
					ADDSWITCHTO(sto, "Switch to StarTrek");
					ADDSWITCHTO(creatures, "Switch to Creatures");
					ADDSWITCHTO(bronze, "Switch to Bronze");
				} else {
					ADDSWITCHTO(core, "Switch to Core");
				}
			}
		}

	}

	if ((situation == MENU_ONE_LINKED_DIR || situation == MENU_ONE_UNLINKED_DIR) && shellMenuGetOption("ShowQuickRD"))
	{
		if (!bDidCMB)
		{
			ClearMenuBitmaps();
			bDidCMB = true;
		}

		HBITMAP gimme_bitmap = GetMenuBitmap(IDI_GIMME);

		if(first_cmd + added + 1 < last_cmd)
		{
			// Separator
			InsertMenu(menu, menu_index, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

			commands.cmd_quickrd = added++;
			mii.fMask = MIIM_TYPE | MIIM_ID;
			mii.fType = MFT_STRING;
			mii.wID = first_cmd + commands.cmd_quickrd;
			mii.dwTypeData = _T("QuickRD");
			mii.cch = (UINT)strlen("QuickRD");

			if (gimme_bitmap)
			{
				mii.fMask |= MIIM_CHECKMARKS | MIIM_STATE;
				mii.fState = MFS_UNCHECKED;
				mii.hbmpChecked = 0;
				mii.hbmpUnchecked = gimme_bitmap;
			}

			InsertMenuItem(menu, menu_index, true, &mii);
		}
	}

	return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, added);
}

STDMETHODIMP CPatchShellMenuExt::GetCommandString(
	UINT_PTR cmd_id,
	UINT flags,
	UINT * reserved,
	LPSTR name,
	UINT max_ch)
{
	//USES_CONVERSION;

	if(flags & GCS_HELPTEXT)
	{
		LPCTSTR text;

#define SHOWHELP(cmd, help)	if(commands.cmd == cmd_id) { text = _T(help); } else
		if (situation == MENU_ONE_LINKED_FILE)
		{
			SHOWHELP(cmd_checkin, "Check in checked out files and new files")
			SHOWHELP(cmd_checkout, "Checkout selected file or folder")
			SHOWHELP(cmd_checkout_noedit, "Checkout selected file without launching the default editor")
			SHOWHELP(cmd_checkpoint, "Check in changes to selected file or folder while leaving the files checked out")
			SHOWHELP(cmd_diff, "Compare the selected file against the latest version commited to the server")
			SHOWHELP(cmd_getlatest, "Update to the latest version")
			SHOWHELP(cmd_remove, "Check out and remove the selected file or folder")
			SHOWHELP(cmd_stat, "Get history and other useful information")
			SHOWHELP(cmd_undocheckout, "Undo a checkout on the selected file or folder")
			// ELSE:
				return E_INVALIDARG;
		} else if (situation == MENU_ONE_LINKED_DIR) {
			SHOWHELP(cmd_quickrd, "Recursively remove a directory without going to the recycle bin and without Explorer hooks")
			SHOWHELP(cmd_checkin_fold, "Check in checked out files and new files")
			SHOWHELP(cmd_checkout_fold, "Checkout selected file or folder")
			SHOWHELP(cmd_checkpoint_fold, "Check in changes to selected file or folder while leaving the files checked out")
			SHOWHELP(cmd_getlatest_fold, "Update to the latest version")
			SHOWHELP(cmd_remove_fold, "Check out and remove the selected file or folder")
			SHOWHELP(cmd_diff_fold, "Simulate a checkin to show what files you have changed locally")
			SHOWHELP(cmd_undocheckout_fold, "Undo a checkout on the selected file or folder")
			SHOWHELP(cmd_switch, "Open GimmeCtrl to change the branch");
			SHOWHELP(cmd_backup, "Backup changed files in working copy to a different location");
			SHOWHELP(cmd_restore, "Restore the working copy from a backup");
			// ELSE:
				return E_INVALIDARG;
		} else {
			SHOWHELP(cmd_quickrd, "Recursively remove a directory without going to the recycle bin and without Explorer hooks")
			// ELSE:
				return E_INVALIDARG;
		}

		if(flags & GCS_UNICODE)
		{
			if(name && text && max_ch > 0)
				lstrcpynW((LPWSTR)name, T2CW(text), max_ch);
		}
		else
		{
			if(name && text && max_ch > 0)
				lstrcpynA(name, T2CA_static(text), max_ch);
		}

		return S_OK;
	}
	else
	{
		return E_INVALIDARG;
	}

}

static int system_detach(char *cmd, int minimized, int hidden)
{
	STARTUPINFOW si = {0};
	PROCESS_INFORMATION pi = {0};

	size_t len = strlen(cmd) + 1;
	WCHAR *pWideBuf = new WCHAR[len];

	UTF8ToWideStrConvert(cmd, pWideBuf, (int)len);


	si.cb = sizeof(si);
	if(minimized || hidden)
	{
		si.dwFlags |= STARTF_USESHOWWINDOW;
		si.wShowWindow = (hidden ? SW_HIDE : SW_MINIMIZE);
	}

	if (!CreateProcessW(NULL, pWideBuf,
		NULL, // process security attributes, cannot be inherited
		NULL, // thread security attributes, cannot be inherited
		FALSE, // do NOT let this child inherit handles
		CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP,
		NULL, // inherit environment
		NULL, // inherit current directory
		&si,
		&pi))
	{
		//printf("Error creating process '%s'\n", cmd);
		return 0;
	} else {
		int pid = (int)pi.dwProcessId;
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return pid;
	}
}

static void runGimmeCmd(const char *cmd)
{
	static bool bFoundGimme=false;
	static char gimmePath[MAX_PATH];
	if (!bFoundGimme)
	{
		int regCreateResult;
		HKEY key;

		bool bFoundIt=false;

		regCreateResult = RegOpenKeyExA(
			HKEY_LOCAL_MACHINE,
			"Software\\Cryptic\\Gimme",
			0,
			KEY_READ,
			&key);
		if (ERROR_SUCCESS == regCreateResult) {
			DWORD valueType;
			int regQueryResult;
			DWORD size = sizeof(gimmePath);

			regQueryResult = RegQueryValueExA(
				key,	// handle to key
				"Registered",		// value name
				NULL,			// reserved
				&valueType,		// type buffer
				(LPBYTE)gimmePath,		// data buffer
				&size		// size of data buffer
				);

			// Does the value exist?
			if(ERROR_SUCCESS == regQueryResult){
				// If the stored value is not a string, it is not possible to retrieve it.
				if(REG_SZ == valueType || REG_EXPAND_SZ == valueType){
					bFoundIt = true;
					gimmePath[size] = '\0';
				}
			}
			RegCloseKey(key);
		}
		if (!bFoundIt)
			strcpy_s(gimmePath, sizeof(gimmePath), "C:\\Cryptic\\tools\\bin\\gimme.exe");
		bFoundGimme = true;
	}

	int count = _scprintf("%s %s", gimmePath, cmd);
	char *buf = (char*)malloc(count+1);
	if (buf) {
		sprintf_s(buf, count+1, "%s %s", gimmePath, cmd);
		//OutputDebugStringA("Running Gimme: "); OutputDebugStringA(buf);	OutputDebugStringA("\n");
		system_detach(buf, 0, 0);
		free(buf);
	}
}

static void runGimmeCmdf(const char *fmt, ...)
{
	va_list va, *__vaTemp__ = &va;
	va_start(va, fmt);
	int count = _vscprintf(fmt, va);
	char *buf = (char*)malloc(count+1);
	if (buf) {
		vsprintf_s(buf, count+1, fmt, va);
		runGimmeCmd(buf);
		free(buf);
	}
	va_end(*__vaTemp__);
}


void quickRD(HWND hWnd, const char *filename)
{
	char message[4096];
	sprintf_s(message, sizeof(message), "Are you sure you want to permanently remove the folder '%s'?",
		filename);
	if (IDYES==MessageBoxA(hWnd, message, "QuickRD", MB_YESNO|MB_ICONEXCLAMATION))
	{
		sprintf_s(message, sizeof(message), "CMD.EXE /C rd /s /q \"%s\"", filename);
		system_detach(message, 0, 0);
	}
}

STDMETHODIMP CPatchShellMenuExt::InvokeCommand(
	LPCMINVOKECOMMANDINFO cmd_info)
{
	int verb;

	//USES_CONVERSION;

	if(HIWORD(cmd_info->lpVerb) != 0)
		return E_INVALIDARG;

	// 

	verb = LOWORD(cmd_info->lpVerb);
	

#define EXECUTE(cmd, fmt, ...)					\
			if (verb == commands.cmd) {			\
				runGimmeCmdf(fmt, __VA_ARGS__);	\
				return S_OK;					\
			}

	if (situation == MENU_ONE_LINKED_FILE)
	{
		char *filename = (char*)dir_name;

		EXECUTE(cmd_checkin, "-put \"%s\"", filename);
		EXECUTE(cmd_checkout, "-checkout \"%s\"", filename);
		EXECUTE(cmd_checkout_noedit, "-editor NULL \"%s\"", filename);
		EXECUTE(cmd_checkpoint, "-leavecheckedout -put \"%s\"", filename);
		EXECUTE(cmd_diff, "-diff \"%s\"", filename);
		EXECUTE(cmd_getlatest, "-glvfile \"%s\"", filename);
		EXECUTE(cmd_remove, "-remove \"%s\"", filename);
		EXECUTE(cmd_stat, "-stat \"%s\"", filename);
		EXECUTE(cmd_undocheckout, "-undo \"%s\"", filename);
	} else if (situation == MENU_ONE_LINKED_DIR)
	{
		char *filename = (char*)dir_name;

		EXECUTE(cmd_checkin_fold, "-checkinfold \"%s\"", filename);
		EXECUTE(cmd_checkout_fold, "-checkoutfold \"%s\"", filename);
		EXECUTE(cmd_checkpoint_fold, "-leavecheckedout -checkinfold \"%s\"", filename);
		EXECUTE(cmd_getlatest_fold, "-glvfold \"%s\"", filename);
		EXECUTE(cmd_remove_fold, "-rmfold \"%s\"", filename);
		EXECUTE(cmd_diff_fold, "-simulate -notestwarn -checkinfold \"%s\"", filename);
		EXECUTE(cmd_undocheckout_fold, "-undofold \"%s\"", filename);
		EXECUTE(cmd_backup, "-backup \"%s\"", filename);
		EXECUTE(cmd_restore, "-restore_backup \"%s\"", filename);
#undef EXECUTE

		// For switch, just run Gimmectrl.
		if (verb == commands.cmd_switch)
		{
			system_detach("c:\\cryptic\\tools\\bin\\gimmectrl", false, false);
			return S_OK;
		}

		if (verb == commands.cmd_quickrd) {
			quickRD(cmd_info->hwnd, filename);
			return S_OK;
		}
	} else if (situation == MENU_MANY_LINKED_PATHS)
	{
#define EXECUTE2(cmd, gimmecmd, str)				\
			if (verb == commands.cmd) {				\
				if (cmdstr.length()) cmdstr+=" ";	\
				cmdstr+=gimmecmd;					\
				cmdstr+=" \"";						\
				cmdstr+=str;						\
				cmdstr+="\"";						\
			}
		// Using the non-folder versions of command variables, regardless of if it's a folder or not
		std::string filestr = "";
		std::string foldstr = "";
		for (int i=0; i<(int)path_list.size(); i++) {
			char *filename = path_list[i];
			bool is_dir = dirExists_UTF8(filename);
			if (is_dir) {
				if (foldstr.length())
					foldstr+=";";
				//foldstr+="\"";
				foldstr+=filename;
				//foldstr+="\"";
			} else {
				if (filestr.length())
					filestr+=";";
				//filestr+="\"";
				filestr+=filename;
				//filestr+="\"";
			}

		}
		std::string cmdstr="";
		if (foldstr.length()) {
			EXECUTE2(cmd_checkin, "-checkinfold", foldstr);
			EXECUTE2(cmd_checkout, "-checkoutfold", foldstr);
			EXECUTE2(cmd_checkpoint, "-leavecheckedout -checkinfold", foldstr);
			EXECUTE2(cmd_getlatest, "-glvfold", foldstr);
			EXECUTE2(cmd_remove, "-rmfold", foldstr);
			EXECUTE2(cmd_undocheckout, "-undofold", foldstr);
		}
		if (filestr.length()) {
			EXECUTE2(cmd_checkin, "-put", filestr);
			EXECUTE2(cmd_checkout, "-checkout", filestr);
			EXECUTE2(cmd_checkout_noedit, "-editor NULL", filestr);
			EXECUTE2(cmd_checkpoint, "-leavecheckedout -put", filestr);
			EXECUTE2(cmd_diff, "-diff", filestr);
			EXECUTE2(cmd_getlatest, "-glvfile", filestr);
			EXECUTE2(cmd_remove, "-remove", filestr);
			EXECUTE2(cmd_stat, "-stat", filestr);
			EXECUTE2(cmd_undocheckout, "-undo", filestr);
		}
		runGimmeCmd(cmdstr.c_str());
	} else if (situation == MENU_ONE_UNLINKED_DIR)
	{
		char *filename = (char*)dir_name;

		if (verb == commands.cmd_quickrd) {
			quickRD(cmd_info->hwnd, filename);
			return S_OK;
		}
	}

	/*
	if(get_latest_dialog_cmd == verb)
	{
		char ** dirs = NULL;
		int count = 0;

		if(situation == MENU_ONE_LINKED_FILE || situation == MENU_ONE_LINKED_DIR)
		{
			dirs = (char**)malloc(sizeof(char*) * 1);
			dirs[0] = (char*)dir_name;
			count = 1;
		}
		else if(situation == MENU_MANY_LINKED_PATHS)
		{
			dirs = (char**)malloc(sizeof(char*) * path_list.size());
			for(int i=0; i<(int)path_list.size(); i++)
			{
				dirs[count++] = path_list[i];
			}
		}

		//patchDLLGetLatestDialog((const char **)dirs, count, false);
		if(dirs)
			free(dirs);
	}
*/

	// Switch to
#define EXECUTE_SWITCH(var, str)				\
		if (verb == commands.switchto.var) {	\
			char buf[MAX_PATH*2];				\
			sprintf_s(buf, sizeof(buf), "C:\\Cryptic\\tools\\art\\SwitchTo\\SwitchTo" str ".bat %s", dir_name);	\
			system_detach(buf, 1, 0);			\
		} else 
	EXECUTE_SWITCH(data, "Data")
	EXECUTE_SWITCH(src, "Src")
	EXECUTE_SWITCH(texlib, "Texture")
	EXECUTE_SWITCH(objlib, "Object")
	EXECUTE_SWITCH(core, "Core")
	EXECUTE_SWITCH(fc, "FightClub")
	EXECUTE_SWITCH(pa, "PrimalAge")
	EXECUTE_SWITCH(night, "Night")
	EXECUTE_SWITCH(sto, "StarTrek")
	EXECUTE_SWITCH(creatures, "Creatures")
	EXECUTE_SWITCH(bronze, "Bronze")
	{
		// else
	}

	return E_INVALIDARG;
}



/* Function UTF8ToWideStrConvert()
 *	This function will return the given UTF-8 string
 */
int UTF8ToWideStrConvert(const char *str, WCHAR *outBuffer, int outBufferMaxLength) {
	int result;
	int bufferSize;
	int strSize;

	// If either the outBuffer or the out buffer length is 0,
	// the user is asking how long the string will be after conversion.
	if(!outBuffer || !outBufferMaxLength){
		outBuffer = NULL;
		bufferSize = 0;

		if('\0' == *str)
			return 0;
	}
	else{
		bufferSize = outBufferMaxLength;

		// If the given string is an emtpy string, pass back an emtpy string also.
		if('\0' == *str){
			outBuffer[0] = '\0';
			return 0;
		}
	}
	
	strSize = (int)strlen(str);//(bufferSize ? min(strlen(str), bufferSize) : strlen(str));

	result = MultiByteToWideChar(CP_UTF8, 0, str, strSize, outBuffer, bufferSize);

	if(!result)
		assert(0);


    if(outBuffer)
		outBuffer[result] = 0;

	// Do not count the null terminating character as part of the string.
	return result;
}




int WideToEncodingConvert(const WCHAR* str, char* outBuffer, int outBufferMaxLength, unsigned int encoding)
{
	int result;
	int bufferSize;
    int strSize;

	// If either the outBuffer or the out buffer length is 0,
	// the user is asking how long the string will be after conversion.
	if(!outBuffer || !outBufferMaxLength){
		outBuffer = NULL;
		bufferSize = 0;

		if('\0' == *str)
			return 0;
	}
	else{
		bufferSize = outBufferMaxLength;

		// If the given string is an emtpy string, pass back an emtpy string also.
		if('\0' == *str){
			outBuffer[0] = '\0';
			return 0;
		}
	}

    strSize = (int)wcslen(str);

	result = WideCharToMultiByte(encoding, 0, str, strSize, outBuffer, bufferSize, NULL, NULL);

	if(!result)
		assert(0);


	if(outBuffer)
		outBuffer[result] = 0;

	return result;
}


int WideToUTF8StrConvert(const WCHAR* str, char* outBuffer, int outBufferMaxLength)
{
	return WideToEncodingConvert(str, outBuffer, outBufferMaxLength, CP_UTF8);
}

bool dirExists_UTF8(const char *pDirName)
{
	WCHAR wideBuf[MAX_PATH];
	UTF8ToWideStrConvert(pDirName, wideBuf, MAX_PATH);
	return dirExists(wideBuf);
}

#endif


