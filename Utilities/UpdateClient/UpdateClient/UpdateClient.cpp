// UpdateClient.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "UpdateClient.h"
#include "UpdateClientDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CUpdateClientApp

BEGIN_MESSAGE_MAP(CUpdateClientApp, CWinApp)
	ON_COMMAND(ID_HELP, CWinApp::OnHelp)
END_MESSAGE_MAP()


// CUpdateClientApp construction

CUpdateClientApp::CUpdateClientApp()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}


// The one and only CUpdateClientApp object

CUpdateClientApp theApp;


// CUpdateClientApp initialization

BOOL CUpdateClientApp::InitInstance()
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

	AfxEnableControlContainer();


	CUpdateClientDlg dlg;
	m_pMainWnd = &dlg;
	INT_PTR nResponse = dlg.DoModal();
	if (nResponse == IDOK)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with OK

		CSocket socketClient;
		CString server = dlg.m_Server;
		CString project = dlg.m_Project;
		int cov = dlg.m_cov;

		if (!server.CompareNoCase("qacov"))
		{
			server = "qa";
		}

		socketClient.Create();
		socketClient.Connect(server, 1237);

		CSocketFile file(&socketClient);

		// construct an archive 
		CArchive arOut(&file, CArchive::store);

		// get the time string
		char buf[1024];

		sprintf(buf, "%02d%02d%02d%02d:%02d", dlg.m_Date.GetMonth(), dlg.m_Date.GetDay(), (dlg.m_Date.GetYear() - 2000),
			dlg.m_Time.GetHour(), dlg.m_Time.GetMinute());

		CString c = buf;
		arOut << c;
		arOut << server;

		// hack!!!
		if (cov)
//		if (!server.CompareNoCase("villain1") || !dlg.m_Server.CompareNoCase("qacov") || !server.CompareNoCase("covdev"))
		{
			arOut << (CString) "-cov"; 
		} else {
			arOut << (CString) ""; 
		}

		// Send which project this is
		arOut << project;
	}
	else if (nResponse == IDCANCEL)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with Cancel
	}

	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.
	return FALSE;
}
