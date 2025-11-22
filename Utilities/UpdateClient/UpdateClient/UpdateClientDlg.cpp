// UpdateClientDlg.cpp : implementation file
//

#include "stdafx.h"
#include "UpdateClient.h"
#include "UpdateClientDlg.h"
#include ".\updateclientdlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CAboutDlg dialog used for App About

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	enum { IDD = IDD_ABOUTBOX };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

// Implementation
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
END_MESSAGE_MAP()


// CUpdateClientDlg dialog



CUpdateClientDlg::CUpdateClientDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CUpdateClientDlg::IDD, pParent)
	, m_Time(0)
	, m_Date(0)
	, m_Server(_T(""))
	, m_Project(_T(""))
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_Time = CTime::GetCurrentTime();
	m_Date = CTime::GetCurrentTime();
}

void CUpdateClientDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_DateTimeCtrl(pDX, IDC_TIME, m_Time);
	DDX_DateTimeCtrl(pDX, IDC_DATE, m_Date);
	DDX_Control(pDX, IDC_SERVER, m_ServerComboBox);
	DDX_CBString(pDX, IDC_SERVER, m_Server);
	DDX_Control(pDX, IDC_PROJECT, m_ProjectComboBox);
	DDX_CBString(pDX, IDC_PROJECT, m_Project);
}

BEGIN_MESSAGE_MAP(CUpdateClientDlg, CDialog)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	//}}AFX_MSG_MAP
	ON_CBN_SELCHANGE(IDC_SERVER, OnCbnSelchangeServer)
END_MESSAGE_MAP()


// CUpdateClientDlg message handlers

BOOL CUpdateClientDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	// TODO: Add extra initialization here

	// read in server list otherwise, just use the default servers
	FILE *pfile = fopen("n:\\users\\vince\\servers.txt", "r");
	if (pfile)
	{
		char buf[256];
		int cov;
		
		m_ServerComboBox.ResetContent();
		m_ProjectComboBox.ResetContent();

//		m_ServerComboBox.AddString("localhost");
		while (fscanf(pfile, "%s %d\n", buf, &cov) != EOF)
		{
			int idx = m_ServerComboBox.AddString(buf);
			m_ServerComboBox.SetItemData(idx, cov);
		}
	}

	m_ProjectComboBox.AddString("CoH");
	m_ProjectComboBox.AddString("FC");
	m_ProjectComboBox.SetCurSel(0);

	// show default server
	m_ServerComboBox.SetCurSel(0);
	m_cov = 0;

	// 
	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CUpdateClientDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CUpdateClientDlg::OnPaint() 
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

// The system calls this function to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CUpdateClientDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}


void CUpdateClientDlg::OnCbnSelchangeServer()
{
	// TODO: Add your control notification handler code here
}

void CUpdateClientDlg::OnOK(void)
{
	// override
	m_cov = (int) m_ServerComboBox.GetItemData(m_ServerComboBox.GetCurSel());

	CDialog::OnOK();
}
