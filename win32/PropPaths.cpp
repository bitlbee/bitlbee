// PropPaths.cpp : implementation file
//

#define BITLBEE_CORE
#include "bitlbeewin.h"
#include "PropPaths.h"
#include "shlobj.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CPropPaths dialog


CPropPaths::CPropPaths()
	: CPropertyPage(CPropPaths::IDD)
{
	//{{AFX_DATA_INIT(CPropPaths)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}


void CPropPaths::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CPropPaths)
	DDX_Control(pDX, IDC_MOTDFILE, m_motdfile);
	DDX_Control(pDX, IDC_EDIT_MOTD, m_edit_motd);
	DDX_Control(pDX, IDC_CONFIGDIR, m_configdir);
	DDX_Control(pDX, IDC_BROWSE_MOTD, m_browse_motd);
	DDX_Control(pDX, IDC_BROWSE_CONFIG, m_browse_config);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CPropPaths, CPropertyPage)
	//{{AFX_MSG_MAP(CPropPaths)
	ON_BN_CLICKED(IDC_BROWSE_CONFIG, OnBrowseConfig)
	ON_BN_CLICKED(IDC_BROWSE_MOTD, OnBrowseMotd)
	ON_BN_CLICKED(IDC_EDIT_MOTD, OnEditMotd)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CPropPaths message handlers

void CPropPaths::OnOK() 
{
	CString tmp;
	g_free((void *)global.conf->configdir);
	m_configdir.GetWindowText(tmp);

	if (tmp.GetLength() > 0
		&& tmp.GetAt(tmp.GetLength() - 1) != '/' 
		&& tmp.GetAt(tmp.GetLength() - 1) != '\\')
	{
		global.conf->configdir = g_strdup_printf("%s\\", tmp);
	} else {
		global.conf->configdir = g_strdup(tmp);
	}

	g_free((void *)global.conf->motdfile);
	m_motdfile.GetWindowText(tmp);
	global.conf->motdfile = g_strdup(tmp);
	
	CPropertyPage::OnOK();
}

void CPropPaths::OnBrowseConfig() 
{
	BROWSEINFO bi = { 0 };
	bi.lpszTitle = _T("Choose a config directory");
	LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
	if( pidl != 0) 
	{
		TCHAR path[MAX_PATH];
		if( SHGetPathFromIDList (pidl, path) ) {
			m_configdir.SetWindowText(path);
		}

		IMalloc * imalloc = 0;
		if ( SUCCEEDED (SHGetMalloc (&imalloc)) )
		{
			imalloc->Free(pidl);
			imalloc->Release();
		}
	}
}

void CPropPaths::OnBrowseMotd() 
{
	CFileDialog f(TRUE);

	if(f.DoModal() == IDOK) {
		m_motdfile.SetWindowText(f.GetPathName());
	}
	
}

void CPropPaths::OnEditMotd() 
{
	CString loc;m_motdfile.GetWindowText(loc);
	ShellExecute(this->GetSafeHwnd(), NULL, loc, NULL, NULL, SW_SHOWNORMAL);	
}

BOOL CPropPaths::OnInitDialog() 
{
	CPropertyPage::OnInitDialog();
	
	m_configdir.SetWindowText(global.conf->configdir);
	m_motdfile.SetWindowText(global.conf->motdfile);
	
	return TRUE;  
}
