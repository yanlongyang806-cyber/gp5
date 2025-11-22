// UpdateServerDlg.h : header file
//

#pragma once

#include "trayicon.h"

// CUpdateServerDlg dialog
class CUpdateServerDlg : public CDialog
{
// Construction
public:
	CUpdateServerDlg(CWnd* pParent = NULL);	// standard constructor

// Dialog Data
	enum { IDD = IDD_UPDATESERVER_DIALOG };
	CTrayIcon	m_trayIcon;		// my tray icon

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support


// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	static UINT workerThread( LPVOID pParam );
};
