// PropUsers.cpp : implementation file
//

#define BITLBEE_CORE
#include "bitlbeewin.h"
#include "PropUsers.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CPropUsers dialog


CPropUsers::CPropUsers()
	: CPropertyPage(CPropUsers::IDD)
{
	//{{AFX_DATA_INIT(CPropUsers)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}


void CPropUsers::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CPropUsers)
	DDX_Control(pDX, IDC_KNOWN_USERS, m_known_users);
	DDX_Control(pDX, IDC_KICK, m_kick);
	DDX_Control(pDX, IDC_DEL_KNOWN_USERS, m_del_known_users);
	DDX_Control(pDX, IDC_CURRENT_USERS, m_current_users);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CPropUsers, CPropertyPage)
	//{{AFX_MSG_MAP(CPropUsers)
	ON_BN_CLICKED(IDC_KICK, OnKick)
	ON_BN_CLICKED(IDC_DEL_KNOWN_USERS, OnDelKnownUser)
	ON_LBN_SELCHANGE(IDC_CURRENT_USERS, OnSelchangeCurrentUsers)
	ON_LBN_SELCHANGE(IDC_KNOWN_USERS, OnSelchangeKnownUsers)
	ON_BN_CLICKED(IDC_REFRESH_KNOWN_USERS, OnRefreshKnownUsers)
	ON_BN_CLICKED(IDC_REFRESH_CURRENT_USERS, OnRefreshCurrentUsers)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CPropUsers message handlers

void CPropUsers::OnKick() 
{
	int idx = m_current_users.GetCurSel();
	if(idx == LB_ERR) return;
	irc_t *irc = (irc_t *)m_current_users.GetItemData(idx);
	irc_free(irc);
	m_kick.EnableWindow(FALSE);
	OnRefreshCurrentUsers();
}

void CPropUsers::OnDelKnownUser() 
{
	CString nick;
	m_known_users.GetText(m_known_users.GetCurSel(), nick);
	CString accounts; accounts.Format("%s\\%s.accounts", global.conf->configdir, nick);
	CString nicks; nicks.Format("%s\\%s.nicks", global.conf->configdir, nick);
	CFile::Remove(accounts);
	CFile::Remove(nicks);
	m_del_known_users.EnableWindow(FALSE);
	OnRefreshKnownUsers();
}

void CPropUsers::OnSelchangeCurrentUsers() 
{
	m_kick.EnableWindow(m_current_users.GetCurSel() != LB_ERR);
	
}

void CPropUsers::OnSelchangeKnownUsers() 
{
	m_del_known_users.EnableWindow(m_known_users.GetCurSel() != LB_ERR);
	
}

void CPropUsers::OnRefreshKnownUsers() 
{
	m_known_users.ResetContent();
	GError *error = NULL;
	const char *r;
	GDir *d = g_dir_open(global.conf->configdir, 0, &error);
	if(!d) return;
	while(r = g_dir_read_name(d)) {
		if(strstr(r, ".accounts")) {
			CString tmp(r, strlen(r) - strlen(".accounts"));
			m_known_users.AddString(tmp);
		}
	}
	g_dir_close(d);
}

extern "C" {
	extern GSList *irc_connection_list;
}

void CPropUsers::OnRefreshCurrentUsers() 
{
	m_current_users.ResetContent();
	GSList *gl = irc_connection_list;
	while(gl) {
		irc_t *irc = (irc_t *)gl->data;
		CString tmp;
		tmp.Format("%s@%s \"%s\"", irc->nick, irc->myhost, irc->realname);
		int idx = m_current_users.AddString(tmp);
		m_current_users.SetItemData(idx, (unsigned long)irc);
		gl = gl->next;
	}
}

