// UpdateServer.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "UpdateServer.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CUpdateServerApp

BEGIN_MESSAGE_MAP(CUpdateServerApp, CWinApp)
	ON_COMMAND(ID_HELP, CWinApp::OnHelp)
	ON_COMMAND(ID_TRAY_EXIT, OnTrayExit)
END_MESSAGE_MAP()


// CUpdateServerApp construction

CUpdateServerApp::CUpdateServerApp()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}


// The one and only CUpdateServerApp object

CUpdateServerApp theApp;


// CUpdateServerApp initialization

BOOL CUpdateServerApp::InitInstance()
{
	// InitCommonControls() is required on Windows XP if an application
	// manifest specifies use of ComCtl32.dll version 6 or later to enable
	// visual styles.  Otherwise, any window creation will fail.
	InitCommonControls();

	CWinApp::InitInstance();

	if (!AfxSocketInit())
	{
		AfxMessageBox(IDP_SOCKETS_INIT_FAILED);
		return FALSE;
	}
	
	dlg = new CUpdateServerDlg();
	dlg->Create(IDD_UPDATESERVER_DIALOG);
	dlg->ShowWindow(SW_SHOW);
	dlg->m_trayIcon.m_pDialog = dlg;
	m_pMainWnd = dlg;

/*	INT_PTR nResponse = dlg.DoModal();
	if (nResponse == IDOK)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with OK
	}
	else if (nResponse == IDCANCEL)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with Cancel
	}
*/
	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.
//	return FALSE;
	return TRUE;
}

void CUpdateServerApp::OnTrayExit()
{
	// TODO: Add your command handler code here
	exit(0);
}
