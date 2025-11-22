// UpdateServer.h : main header file for the PROJECT_NAME application
//

#pragma once

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"		// main symbols

#include "UpdateServerDlg.h"

// CUpdateServerApp:
// See UpdateServer.cpp for the implementation of this class
//

class CUpdateServerApp : public CWinApp
{

private:
	CUpdateServerDlg *dlg;

public:
	CUpdateServerApp();

// Overrides
	public:
	virtual BOOL InitInstance();

// Implementation

	DECLARE_MESSAGE_MAP()
	afx_msg void OnTrayExit();
};

extern CUpdateServerApp theApp;