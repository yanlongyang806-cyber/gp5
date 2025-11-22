// PatchShellMenuExt.h : Declaration of the CPatchShellMenuExt

#pragma once
#include "resource.h"       // main symbols

#include "PatchShellMenu.h"

#include <shlobj.h>
#include <comdef.h>
#include <utility>
#include <vector>
#include <map>
#include <malloc.h>

using std::vector;
using std::pair;
using std::map;

#if defined(_WIN32_WCE) && !defined(_CE_DCOM) && !defined(_CE_ALLOW_SINGLE_THREADED_OBJECTS_IN_MTA)
#error "Single-threaded COM objects are not properly supported on Windows CE platform, such as the Windows Mobile platforms that do not include full DCOM support. Define _CE_ALLOW_SINGLE_THREADED_OBJECTS_IN_MTA to force ATL to support creating single-thread COM object's and allow use of it's single-threaded COM object implementations. The threading model in your rgs file was set to 'Free' as that is the only threading model supported in non DCOM Windows CE platforms."
#endif



// CPatchShellMenuExt

typedef enum
{
	MENU_NONE,
	MENU_ONE_UNLINKED_DIR,
	MENU_ONE_LINKED_DIR,
	MENU_MANY_LINKED_PATHS,
	MENU_ONE_LINKED_FILE,
	MENU_ONE_UNLINKED_FILE,
} MenuSituation;

class ATL_NO_VTABLE CPatchShellMenuExt :
	public CComObjectRootEx<CComSingleThreadModel>,
	public CComCoClass<CPatchShellMenuExt, &CLSID_PatchShellMenuExt>,
	public IPatchShellMenuExt,
	public IShellExtInit,
	public IContextMenu
{
public:
	CPatchShellMenuExt()
	{
	}

DECLARE_REGISTRY_RESOURCEID(IDR_PATCHSHELLMENUEXT)

DECLARE_NOT_AGGREGATABLE(CPatchShellMenuExt)

BEGIN_COM_MAP(CPatchShellMenuExt)
	COM_INTERFACE_ENTRY(IPatchShellMenuExt)
	COM_INTERFACE_ENTRY(IShellExtInit)
	COM_INTERFACE_ENTRY_IID(IID_IContextMenu, IContextMenu)
END_COM_MAP()



	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct()
	{
		return S_OK;
	}

	void FinalRelease()
	{
		for(vector<char*>::iterator iter = path_list.begin(); iter != path_list.end(); ++iter)
		{
			free(*iter);
		}
		path_list.clear();
		ClearMenuBitmaps();
	}

protected:
	char dir_name[MAX_PATH];
	MenuSituation situation;
	bool is_readonly_file;
	bool is_writeable_file;
	bool is_folder;
	bool is_file;
#if 0 // Max H version
	UINT link_dialog_cmd, sync_dialog_cmd, get_latest_dialog_cmd;
#else
	struct {
		UINT cmd_checkin, cmd_checkout, cmd_checkout_noedit, cmd_checkpoint, cmd_diff, cmd_getlatest, cmd_remove, cmd_stat, cmd_undocheckout;
		UINT cmd_checkin_fold, cmd_checkout_fold, cmd_checkpoint_fold, cmd_getlatest_fold, cmd_remove_fold, cmd_undocheckout_fold, cmd_diff_fold;
		UINT cmd_quickrd;
		UINT cmd_switch;
		UINT cmd_backup, cmd_restore;
		struct {
			UINT data, src, texlib, objlib, core;
			UINT fc, sto, pa, night, bronze, creatures;
		} switchto;
	} commands;
#endif
	vector<char*> path_list;

	bool isUnderGimmeControl(const char *path);
	typedef pair<const char*, bool> GimmeControlledPath;
	vector<GimmeControlledPath> gimme_paths;

	void ClearMenuBitmaps();
	HBITMAP GetMenuBitmap(int resource_key);
	map<int, HBITMAP> bitmap_cache;


public:
	STDMETHODIMP Initialize(LPCITEMIDLIST, LPDATAOBJECT, HKEY);

	STDMETHODIMP QueryContextMenu(HMENU, UINT, UINT, UINT, UINT);
	STDMETHODIMP GetCommandString(UINT_PTR, UINT, UINT *, LPSTR, UINT);
	STDMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO);
};

OBJECT_ENTRY_AUTO(__uuidof(PatchShellMenuExt), CPatchShellMenuExt)
