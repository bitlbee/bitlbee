#if !defined(AFX_PROPLOG_H__F56909E1_BBCC_4163_B9C2_E8D5685A34AA__INCLUDED_)
#define AFX_PROPLOG_H__F56909E1_BBCC_4163_B9C2_E8D5685A34AA__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// PropLog.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CPropLog dialog

class CPropLog : public CPropertyPage
{
// Construction
public:
	CPropLog();   // standard constructor

// Dialog Data
	//{{AFX_DATA(CPropLog)
	enum { IDD = IDD_PROPPAGE_LOG };
	CListBox	m_log;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CPropLog)
	public:
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CPropLog)
	virtual BOOL OnInitDialog();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PROPLOG_H__F56909E1_BBCC_4163_B9C2_E8D5685A34AA__INCLUDED_)
