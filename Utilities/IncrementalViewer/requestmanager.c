#include "ListView.h"
#include "globals.h"
#include "textparser.h"
#include "ListView.h"
#include "resource.h"
#include "incrementalrequest.h"

typedef struct
{
	char *name;
	char *comment;
	char *timestamp;
	StringArray files;
	int flags;
} Request;

typedef struct
{
	Request **requests;
} RequestList;

RequestList request_list;
int request_list_loaded = 0;
ListView *lvRequests;
ListView *lvFileList;

TokenizerParseInfo MasterListRequestInfo[] =
{
	{ "{",			TOKEN_START,		0	 },
	{ "WHO",		TOKEN_STRING,		offsetof(Request, name),		 },
	{ "COMMENT",	TOKEN_STRING,		offsetof(Request, comment),		 },
	{ "DATE",		TOKEN_STRING,		offsetof(Request, timestamp),	 },
	{ "FILE",		TOKEN_STRINGARRAY,	offsetof(Request, files),		 },
	{ "FLAGS",		TOKEN_INT(Request, flags),							 },
	{ "}",			TOKEN_END,			0		 },
	{ 0 }
};

TokenizerParseInfo MasterListInfo[] =
{
	{ "REQUEST",	TOKEN_STRUCT,		offsetof(RequestList, requests),	sizeof(Request), 	MasterListRequestInfo},
	{ 0 }
};

TokenizerParseInfo RequestsInfo[] =
{
	{ "Name",		TOKEN_STRING,		offsetof(Request, name),		LVMakeParam(50, 0) },
	{ "Comment",	TOKEN_STRING,		offsetof(Request, comment),		LVMakeParam(360, 0) },
	{ "Date",		TOKEN_STRING,		offsetof(Request, timestamp),	LVMakeParam(90, 0) },
	{ 0 }
};

TokenizerParseInfo FileListInfo[] =
{
	{ "File",		TOKEN_STRING,		0,		LVMakeParam(360, 0) },
	{ 0 }
};

int LoadMasterRequestList()
{
	while ( !LockMasterRequestList() )
	{
		printf("Waiting to acquire lock on Master Request List...\n");
		Sleep(1000);
	}
	if ( request_list_loaded )
	{
		ParserDestroyStruct(MasterListInfo, &request_list);
	}

	if ( ParserLoadFiles(0, masterRequestListPath, 0, 0, MasterListInfo, &request_list, 0, 0, 0) )
		request_list_loaded = 1;
	UnlockMasterRequestList();
	return 1;
}

void SaveCurrentMasterList()
{
	while ( !LockMasterRequestList() )
	{
		printf("Waiting to acquire lock on Master Request List...\n");
		Sleep(1000);
	}
	ParserWriteTextFile("N:/incremental/newmaster.txt", MasterListInfo, &request_list);
	UnlockMasterRequestList();
}

static Request *gCurSelection = NULL;

void onSelectRequest(ListView *lv, void *structptr, void *data)
{
	int i;
	HWND hDlg = (HWND)(data);
	gCurSelection = (Request*)structptr;
	
	if ( lvFileList )
		listViewDelAllItems(lvFileList, NULL);
	else
	{
		lvFileList = listViewCreate();
		listViewInit(lvFileList, FileListInfo, hDlg, GetDlgItem(hDlg, IDC_LV_FILELIST));
	}

	for ( i = 0; i < EArrayGetSize(&gCurSelection->files); ++i )
	{
		listViewAddItem(lvFileList, &(gCurSelection->files[i]));
	}
}

void onAcceptRequest(ListView *lv, void *structptr, void *data)
{
	((Request*)structptr)->flags &= ~RQ_DENIED;
	((Request*)structptr)->flags |= RQ_ACCEPTED;
	listViewItemChanged(lvRequests ,structptr);
}

void onDenyRequest(ListView *lv, void *structptr, void *data)
{
	((Request*)structptr)->flags &= ~RQ_ACCEPTED;
	((Request*)structptr)->flags |= RQ_DENIED;
	listViewItemChanged(lvRequests ,structptr);
}

void onDeleteRequest(ListView *lv, void *structptr, void *data)
{
	int i;
	for ( i = 0; i < EArrayGetSize(&request_list.requests); ++i )
	{
		if ( request_list.requests[i] == structptr )
		{
			EArrayRemove(&request_list.requests, i);
			listViewDelItem(lvRequests, listViewFindItem(lvRequests, structptr));
			return;
		}
	}
}

LRESULT CALLBACK RequestManagerDlgProc( HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam )
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			int i;
			
			lvFileList = NULL;
			lvRequests = listViewCreate();
			listViewInit(lvRequests, RequestsInfo, hDlg, GetDlgItem(hDlg, IDC_LV_ALLREQUESTS));
			for ( i = 0; i < EArrayGetSize(&request_list.requests); ++i )
			{
				listViewAddItem(lvRequests, request_list.requests[i]);
			}
			// sort by date, with most recent dates first
			listViewReverseSort(lvRequests, 2);

			SetTimer(hDlg, 0, 10, NULL);

			return FALSE;
		}
		break;
	case WM_TIMER:
		listViewDoOnSelected(lvRequests, onSelectRequest, (void*)hDlg);
		{
			int i;
			for ( i = 0; i < EArrayGetSize(&request_list.requests); ++i )
			{
				if ( request_list.requests[i]->flags & RQ_ACCEPTED )
					listViewSetItemColor(lvRequests, request_list.requests[i], LISTVIEW_DEFAULT_COLOR, RGB(123, 255, 123));
				else if ( request_list.requests[i]->flags & RQ_DENIED)
					listViewSetItemColor(lvRequests, request_list.requests[i], LISTVIEW_DEFAULT_COLOR, RGB(255, 123, 123));
			}
		}
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_ACCEPT:
			if ( lvRequests )
			{
				listViewDoOnSelected(lvRequests, onAcceptRequest, NULL);
			}
			break;
		case IDC_DENY:
			if ( lvRequests )
			{
				listViewDoOnSelected(lvRequests, onDenyRequest, NULL);
			}
			break;
		case IDC_DELETE:
			if ( lvRequests ) 
			{
				listViewDoOnSelected(lvRequests, onDeleteRequest, NULL);
			}
			break;
		case IDC_REFRESH:
			SaveCurrentMasterList();
			CompileMasterRequestList();
			LoadMasterRequestList();
			break;
		case IDC_CLOSE:
			KillTimer(hDlg, 0);
			EndDialog(hDlg, 1);
			SaveCurrentMasterList();
			listViewDestroy(lvRequests);
			if ( lvFileList )
				listViewDestroy(lvFileList);
			lvRequests = lvFileList = NULL;
			return TRUE;
		}
		break;
	case WM_NOTIFY:
		{
			if ( lvRequests && listViewOnNotify(lvRequests, wParam, lParam, NULL) )
			{
				return TRUE;
			}
		}
		break;
	case WM_QUIT:
	case WM_DESTROY:
	case WM_CLOSE:
		KillTimer(hDlg, 0);
	}
	return FALSE;
}

int RunRequestManager(void)
{
	if ( !LoadMasterRequestList() )
		return 0;

	return DialogBox(NULL, (LPCTSTR)IDD_REQUESTMANAGER, NULL, RequestManagerDlgProc);
}