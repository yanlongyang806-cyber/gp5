// UpdateClientDlg.h : header file
//

#pragma once
#include "atltime.h"
#include "afxwin.h"


// CUpdateClientDlg dialog
class CUpdateClientDlg : public CDialog
{
// Construction
public:
	CUpdateClientDlg(CWnd* pParent = NULL);	// standard constructor

// Dialog Data
	enum { IDD = IDD_UPDATECLIENT_DIALOG };

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
	afx_msg void OnDtnDatetimechangeDatetimepicker2(NMHDR *pNMHDR, LRESULT *pResult);
	CTime m_Time;
	CTime m_Date;
	CComboBox m_ServerComboBox;
	CString m_Server;
	CComboBox m_ProjectComboBox;
	CString m_Project;
	int m_cov;
	afx_msg void OnCbnSelchangeServer();
	void OnOK(void);
};
