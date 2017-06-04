/*****************************************************************************
                    The Dark Mod GPL Source Code
 
 This file is part of the The Dark Mod Source Code, originally based 
 on the Doom 3 GPL Source Code as published in 2011.
 
 The Dark Mod Source Code is free software: you can redistribute it 
 and/or modify it under the terms of the GNU General Public License as 
 published by the Free Software Foundation, either version 3 of the License, 
 or (at your option) any later version. For details, see LICENSE.TXT.
 
 Project: The Dark Mod (http://www.thedarkmod.com/)
 
 $Revision: 5171 $ (Revision of last commit) 
 $Date: 2012-01-07 08:08:06 +0000 (Sat, 07 Jan 2012) $ (Date of last commit)
 $Author: greebo $ (Author of last commit)
 
******************************************************************************/

#include "precompiled_engine.h"
#pragma hdrstop

static bool versioned = RegisterVersionedFile("$Id: CurveDlg.cpp 5171 2012-01-07 08:08:06Z greebo $");

#include "qe3.h"
#include "Radiant.h"
#include "CurveDlg.h"


// CCurveDlg dialog

IMPLEMENT_DYNAMIC(CCurveDlg, CDialog)
CCurveDlg::CCurveDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CCurveDlg::IDD, pParent)
{
}

CCurveDlg::~CCurveDlg()
{
}

void CCurveDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_COMBO_CURVES, comboCurve);
}

void CCurveDlg::OnOK() {
	UpdateData(TRUE);
	CString str;
	comboCurve.GetWindowText( str );
	strCurveType = str;
	CDialog::OnOK();
}

BEGIN_MESSAGE_MAP(CCurveDlg, CDialog)
END_MESSAGE_MAP()


// CCurveDlg message handlers
