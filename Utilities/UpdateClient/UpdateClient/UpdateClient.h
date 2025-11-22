// UpdateClient.h : main header file for the PROJECT_NAME application
//

#pragma once

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"		// main symbols


// CUpdateClientApp:
// See UpdateClient.cpp for the implementation of this class
//

class CUpdateClientApp : public CWinApp
{
public:
	CUpdateClientApp();

// Overrides
	public:
	virtual BOOL InitInstance();

// Implementation

	DECLARE_MESSAGE_MAP()
};

extern CUpdateClientApp theApp;