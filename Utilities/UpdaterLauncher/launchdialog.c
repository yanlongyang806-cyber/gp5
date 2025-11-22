#include "launchdialog.h"
#include <shlobj.h>
#include "RegistryReader.h"
#include <stdio.h>
#include "strings_opt.h"


char updater[MAX_PATH] = {0};
char folder[MAX_PATH] = {0};
char server[MAX_PATH] = {0};
char saveProfileName[MAX_PATH] = {0};

int noself = 0;
int cov = 0;
int console = 0;
int majorpatch = 0;
int test = 0;
int nolaunch = 0;

#define NOSELF		(1<<0)
#define COV			(1<<1)
#define CONSOLE		(1<<2)
#define MAJORPATCH	(1<<3)
#define TEST		(1<<4)
#define NOLAUNCH	(1<<5)

typedef struct 
{
	char name[MAX_PATH];
	char updater[MAX_PATH];
	char folder[MAX_PATH];
	char server[MAX_PATH];
	int flags;
} Profile;


#define NUM_PROFILES 16
Profile profiles[NUM_PROFILES];

// load the profiles from the registry
void LoadProfiles( HWND hDlg, RegReader reg, int dlg_id )
{
	int i;
	char out[MAX_PATH] = {0};
	int val;

	for ( i = 0; i < NUM_PROFILES; ++i )
	{
		char regkey[256];

		sprintf( regkey, "Profile%d", i );
		if ( rrReadString(reg, regkey, out, MAX_PATH) )
		{
			LRESULT res;
			strcpy( profiles[i].name, out );
			// add the string to the combo box
			res = SendMessage(GetDlgItem(hDlg, dlg_id), CB_ADDSTRING, 0, (LPARAM)profiles[i].name);
			if ( res )
				res = res;
		}
		else
			profiles[i].name[0] = 0;

		sprintf( regkey, "Updater%d", i );
		if ( rrReadString(reg, regkey, out, MAX_PATH) )
			strcpy( profiles[i].updater, out );
		else
			profiles[i].updater[0] = 0;

		sprintf( regkey, "Folder%d", i );
		if ( rrReadString(reg, regkey, out, MAX_PATH) )
			strcpy( profiles[i].folder, out );
		else
			profiles[i].folder[0] = 0;

		sprintf( regkey, "Server%d", i );
		if ( rrReadString(reg, regkey, out, MAX_PATH) )
			strcpy( profiles[i].server, out );
		else
			profiles[i].server[0] = 0;

		sprintf( regkey, "Flags%d", i );
		if ( rrReadInt(reg, regkey, &val) )
			profiles[i].flags = val;
		else
			profiles[i].flags = 0;
	}
}

// reload existing profiles
// TODO: SEEMS TO NOT WORK
void ReloadProfiles( HWND hDlg, int dlg_id )
{
	int i;

	for ( i = 0; i < NUM_PROFILES; ++i )
	{
		// delete the string from the combo box if it exists
		SendMessage(GetDlgItem(hDlg, dlg_id), CB_DELETESTRING, i, 0);
	}
	for ( i = 0; i < NUM_PROFILES; ++i )
	{
		if ( profiles[i].name[0] )
		{
			// add the string to the combo box
			SendMessage(GetDlgItem(hDlg, dlg_id), CB_ADDSTRING, 0, (LPARAM)profiles[i].name);
		}
	}
}

// load the profiles from the registry
void WriteProfiles( HWND hDlg, RegReader reg, int dlg_id )
{
	int i;
	char out[MAX_PATH] = {0};

	for ( i = 0; i < NUM_PROFILES; ++i )
	{
		char regkey[256];

		if ( !profiles[i].name[0] )
			continue;

		sprintf( regkey, "Profile%d", i );
		rrWriteString(reg, regkey, profiles[i].name);

		sprintf( regkey, "Updater%d", i );
		rrWriteString(reg, regkey, profiles[i].updater);

		sprintf( regkey, "Folder%d", i );
		rrWriteString(reg, regkey, profiles[i].folder);

		sprintf( regkey, "Server%d", i );
		rrWriteString(reg, regkey, profiles[i].server);
		
		sprintf( regkey, "Flags%d", i );
		rrWriteInt(reg, regkey, profiles[i].flags);
	}
}

// return the index of the profile of a given name
int FindProfile( char *name )
{
	int i;

	// find the profile
	for ( i = 0; i < NUM_PROFILES; ++i )
	{
		if ( !strcmp(profiles[i].name, name) )
			return i;
	}
	return -1;
}


// save a profile into the array of profiles
// returns the position the profile was saved to
int SaveProfile( Profile *p )
{
	int i;

	i = FindProfile(p->name);

	if ( i == -1 )
	{
		// move all profiles up 1
		for ( i = NUM_PROFILES - 1; i >= 1 ; --i )
		{
			if ( profiles[i-1].name[0] && profiles[i-1].updater[0] && 
				profiles[i-1].folder[0] && profiles[i-1].server[0] )
			{
				strcpy(profiles[i].name, profiles[i-1].name);
				strcpy(profiles[i].updater, profiles[i-1].updater);
				strcpy(profiles[i].folder, profiles[i-1].folder);
				strcpy(profiles[i].server, profiles[i-1].server);
				profiles[i].flags = profiles[i-1].flags;
			}
		}

		strcpy(profiles[0].name, p->name);
		strcpy(profiles[0].updater, p->updater);
		strcpy(profiles[0].folder, p->folder);
		strcpy(profiles[0].server, p->server);
		profiles[0].flags = p->flags;

		return 0;
	}
	else
	{
		strcpy(profiles[i].name, p->name);
		strcpy(profiles[i].updater, p->updater);
		strcpy(profiles[i].folder, p->folder);
		strcpy(profiles[i].server, p->server);
		profiles[i].flags = p->flags;

		return i;
	}
}

// delete a profile of a given name
void DeleteProfile( HWND hDlg, int dlg_id, char *name )
{
	int i;

	// find the profile
	i = FindProfile(name);
	if ( i == -1 )
		return;
	++i;

	// copy all of the profiles after the found one back one position
	for ( ; i < NUM_PROFILES; ++i )
	{
		strcpy(profiles[i-1].name, profiles[i].name);
		strcpy(profiles[i-1].updater, profiles[i].updater);
		strcpy(profiles[i-1].folder, profiles[i].folder);
		strcpy(profiles[i-1].server, profiles[i].server);
		profiles[i-1].flags = profiles[i].flags;
	}

	// delete it from the list
	i = (int)SendMessage(GetDlgItem(hDlg, dlg_id), CB_FINDSTRING, -1, (LPARAM)name);
	SendMessage(GetDlgItem(hDlg, dlg_id), CB_DELETESTRING, i, 0);
}


// read a registry string and both store it in the given string and set the dialog window text to the string
void InitStrFromReg( HWND hDlg, RegReader reg, char *regkey, char * str, int dlg_id )
{
	char out[MAX_PATH] = {0};
	if ( rrReadString(reg, regkey, out, MAX_PATH) )
	{
		strcpy(str, out);
		SetWindowText(GetDlgItem(hDlg, dlg_id), str);
	}
}

// read a registry int and both store it in the given val and check/uncheck the corresponding checkbox
void InitIntFromReg( HWND hDlg, RegReader reg, char *regkey, int *val, int dlg_id )
{
	int out;
	if ( rrReadInt(reg, regkey, &out) )
	{
		*val = out;
		CheckDlgButton(hDlg, dlg_id, *val ? BST_CHECKED : BST_UNCHECKED);
	}
}


// get the string of the current selection in the combo box
void GetCurComboBoxSel( HWND hDlg, int dlg_id, char *out )
{
	LRESULT sel = SendMessage(GetDlgItem(hDlg, dlg_id), CB_GETCURSEL, 0, 0);
	SendMessage(GetDlgItem(hDlg, dlg_id), CB_GETLBTEXT, sel, (LPARAM)out);
}

// save profile dialog box procedure
LRESULT CALLBACK SaveProfileProc( HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	switch ( uMsg )
	{
		case WM_INITDIALOG:
			{
				// initialize the profile name edit box to the current server name
				SetWindowText(GetDlgItem(hDlg, IDC_EDIT_PROFILENAME), server);
			}
		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDOK:
					{
						GetWindowText(GetDlgItem(hDlg, IDC_EDIT_PROFILENAME), saveProfileName, MAX_PATH);
						if ( !saveProfileName[0] )
						{
							MessageBox(NULL, "You must enter a profile name", "", MB_OK);
							break;
						}
						EndDialog(hDlg, IDOK);
						return TRUE;
					}
				case IDCANCEL:
					{
						EndDialog(hDlg, IDCANCEL);
						return TRUE;
					}
			}
	}

	return FALSE;
}

LRESULT CALLBACK LaunchDlgProc( HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
			{	
				RegReader reg = createRegReader();
				initRegReader(reg, "HKEY_CURRENT_USER\\SOFTWARE\\Cryptic\\UpdaterLauncher");
				// read saved registry values
				InitStrFromReg(hDlg, reg, "Updater", updater, IDC_EDIT_UPDATERDIR);
				InitStrFromReg(hDlg, reg, "Folder", folder, IDC_EDIT_FOLDER);
				InitStrFromReg(hDlg, reg, "Server", server, IDC_EDIT_SERVER);
				InitIntFromReg(hDlg, reg, "Noself", &noself, IDC_NOSELF);
				InitIntFromReg(hDlg, reg, "Cov", &cov, IDC_COV);
				InitIntFromReg(hDlg, reg, "Console", &console, IDC_CONSOLE);
				InitIntFromReg(hDlg, reg, "Majorpatch", &majorpatch, IDC_MAJORPATCH);
				InitIntFromReg(hDlg, reg, "Test", &test, IDC_TEST);
				InitIntFromReg(hDlg, reg, "Nolaunch", &nolaunch, IDC_NOLAUNCH);
				LoadProfiles(hDlg, reg, IDC_PROFILES);
				destroyRegReader(reg);

				SetWindowText(GetDlgItem(hDlg, IDC_PROFILES), "Saved Profiles");

				UpdateWindow(hDlg);
				ShowWindow(hDlg, SW_SHOW);
			}
			break;
		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDOK:
					{
						RegReader reg;

						//get all of the parameters they passed
						GetWindowText(GetDlgItem(hDlg, IDC_EDIT_UPDATERDIR), updater, MAX_PATH);
						GetWindowText(GetDlgItem(hDlg, IDC_EDIT_FOLDER), folder, MAX_PATH);
						GetWindowText(GetDlgItem(hDlg, IDC_EDIT_SERVER), server, MAX_PATH);
						
						// 0 unless checkboxes are checked
						noself = cov = console = majorpatch = test = nolaunch = 0;

						// check checkboxes
						if ( IsDlgButtonChecked(hDlg, IDC_NOSELF) )
							noself = 1;
						if ( IsDlgButtonChecked(hDlg, IDC_COV) )
							cov = 1;
						if ( IsDlgButtonChecked(hDlg, IDC_CONSOLE) )
							console = 1;
						if ( IsDlgButtonChecked(hDlg, IDC_MAJORPATCH) )
							majorpatch = 1;
						if ( IsDlgButtonChecked(hDlg, IDC_TEST) )
							test = 1;
						if ( IsDlgButtonChecked(hDlg, IDC_NOLAUNCH) )
							nolaunch= 1;

						// write out current values to registry
						reg = createRegReader();
						initRegReader(reg, "HKEY_CURRENT_USER\\SOFTWARE\\Cryptic\\UpdaterLauncher");
						rrWriteString(reg, "Updater", updater);
						rrWriteString(reg, "Folder", folder);
						rrWriteString(reg, "Server", server);
						rrWriteInt(reg, "Noself", noself);
						rrWriteInt(reg, "Cov", cov);
						rrWriteInt(reg, "Console", console);
						rrWriteInt(reg, "Majorpatch", majorpatch);
						rrWriteInt(reg, "Test", test);
						rrWriteInt(reg, "Nolaunch", nolaunch);
						WriteProfiles(hDlg, reg, IDC_PROFILES);
						destroyRegReader(reg);

						EndDialog(hDlg, IDOK);
					}
					return TRUE;
				case IDCANCEL:
					EndDialog(hDlg, IDCANCEL);
					return TRUE;
				case IDC_BROWSE1:
					{
						// browse for the updater executable
						OPENFILENAME file;
						ZeroMemory(&file, sizeof(file));
						file.lStructSize = sizeof(file);
						file.hwndOwner = hDlg;
						file.lpstrFile = updater;
						file.lpstrFile[0] = '\0';
						file.nMaxFile = MAX_PATH;
						file.lpstrFilter = "All\0*.*\0Executable\0*.EXE\0";
						file.nFilterIndex = 1;
						file.lpstrFileTitle = NULL;
						file.nMaxFileTitle = 0;
						file.lpstrInitialDir = NULL;
						file.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
						if ( GetOpenFileName(&file) )
							SetWindowText(GetDlgItem(hDlg, IDC_EDIT_UPDATERDIR), updater);
					}
					break;
				case IDC_BROWSE2:
					{
						// browse for the patch folder
						BROWSEINFO bi;
						LPITEMIDLIST lst;
						ZeroMemory(&bi, sizeof(bi));
						bi.hwndOwner = hDlg;
						bi.lpszTitle = "Select a folder to apply the patch to.";
						bi.ulFlags = BIF_EDITBOX | BIF_VALIDATE;
						lst = SHBrowseForFolder(&bi);
						if ( lst )
						{
							SHGetPathFromIDList( lst, folder );
							SetWindowText(GetDlgItem(hDlg, IDC_EDIT_FOLDER), folder);
						}
					}
					break;
				case IDC_SAVEPROFILE:
					{
						Profile p;

						GetWindowText(GetDlgItem(hDlg, IDC_EDIT_SERVER), server, MAX_PATH);
						// dialog that gets a name for the profile
						if ( IDOK == DialogBox(NULL, (LPCTSTR)(IDD_SAVEPROFILE), NULL, SaveProfileProc) )
						{
							int i, profIdx;

							strcpy(p.name, saveProfileName);
							GetWindowText(GetDlgItem(hDlg, IDC_EDIT_UPDATERDIR), p.updater, MAX_PATH);
							GetWindowText(GetDlgItem(hDlg, IDC_EDIT_FOLDER), p.folder, MAX_PATH);
							GetWindowText(GetDlgItem(hDlg, IDC_EDIT_SERVER), p.server, MAX_PATH);

							p.flags = 0;

							if ( IsDlgButtonChecked(hDlg, IDC_NOSELF) )
								p.flags |= NOSELF;
							if ( IsDlgButtonChecked(hDlg, IDC_COV) )
								p.flags |= COV;
							if ( IsDlgButtonChecked(hDlg, IDC_CONSOLE) )
								p.flags |= CONSOLE;
							if ( IsDlgButtonChecked(hDlg, IDC_MAJORPATCH) )
								p.flags |= MAJORPATCH;
							if ( IsDlgButtonChecked(hDlg, IDC_TEST) )
								p.flags |= TEST;
							if ( IsDlgButtonChecked(hDlg, IDC_NOLAUNCH) )
								p.flags |= NOLAUNCH;

							profIdx = SaveProfile(&p);

							i = (int)SendMessage(GetDlgItem(hDlg, IDC_PROFILES), CB_FINDSTRING, -1, (LPARAM)profiles[profIdx].name);
							SendMessage(GetDlgItem(hDlg, IDC_PROFILES), CB_DELETESTRING, i, 0);
							SendMessage(GetDlgItem(hDlg, IDC_PROFILES), CB_ADDSTRING, 0, (LPARAM)profiles[profIdx].name);

							{
								RegReader reg = createRegReader();
								initRegReader(reg, "HKEY_CURRENT_USER\\SOFTWARE\\Cryptic\\UpdaterLauncher");
								WriteProfiles(hDlg, reg, IDC_PROFILES);
								destroyRegReader(reg);
							}
							//ReloadProfiles(hDlg, IDC_PROFILES);
						}
					}
					break;
				case IDC_DELETEPROFILE:
					{
						char curProfile[256]={0};
						GetCurComboBoxSel(hDlg, IDC_PROFILES, curProfile);
						DeleteProfile(hDlg, IDC_PROFILES, curProfile);
						//ReloadProfiles(hDlg, IDC_PROFILES);
					}
					break;
				case IDC_PROFILES:
					{
						char curProfile[256]={0};
						int i;
						GetCurComboBoxSel(hDlg, IDC_PROFILES, curProfile);
						i = FindProfile(curProfile);
						if ( curProfile[0] && i != -1 )
						{
							SetWindowText(GetDlgItem(hDlg, IDC_EDIT_UPDATERDIR), profiles[i].updater);
							SetWindowText(GetDlgItem(hDlg, IDC_EDIT_FOLDER), profiles[i].folder);
							SetWindowText(GetDlgItem(hDlg, IDC_EDIT_SERVER), profiles[i].server);

							CheckDlgButton(hDlg, IDC_NOSELF, profiles[i].flags & NOSELF ? BST_CHECKED : BST_UNCHECKED);
							CheckDlgButton(hDlg, IDC_COV, profiles[i].flags & COV ? BST_CHECKED : BST_UNCHECKED);
							CheckDlgButton(hDlg, IDC_CONSOLE, profiles[i].flags & CONSOLE ? BST_CHECKED : BST_UNCHECKED);
							CheckDlgButton(hDlg, IDC_MAJORPATCH, profiles[i].flags & MAJORPATCH ? BST_CHECKED : BST_UNCHECKED);
							CheckDlgButton(hDlg, IDC_TEST, profiles[i].flags & TEST ? BST_CHECKED : BST_UNCHECKED);
							CheckDlgButton(hDlg, IDC_NOLAUNCH, profiles[i].flags & NOLAUNCH? BST_CHECKED : BST_UNCHECKED);
						}
					}
					break;
			}
			break;
	}

	return FALSE;
}