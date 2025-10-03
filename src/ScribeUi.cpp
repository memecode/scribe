/*
**	FILE:		ScribeUi.cpp
**	AUTHOR:		Matthew Allen
**	DATE:		30/10/98
**	DESCRIPTION:	Scribe email application
**
**	Copyright (C) 1998, Matthew Allen
**		fret@memecode.com
*/


#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "Scribe.h"
#include "lgi/common/Edit.h"
#include "lgi/common/RadioGroup.h"
#include "lgi/common/Combo.h"
#include "lgi/common/Button.h"
#include "lgi/common/TextView3.h"
#include "lgi/common/TextLabel.h"
#include "resdefs.h"
#include "lgi/common/ControlTree.h"
#include "lgi/common/TabView.h"
#include "ScribeSpellCheck.h"
#include "lgi/common/TableLayout.h"

// static char AutoInBrackets[] = "(auto)";

//////////////////////////////////////////////////////////////////////////////
CreateSubFolderDlg::CreateSubFolderDlg(LView *parent, int defaultType, bool *Enable, char *defaultName)
{
	SetParent(parent);
	LRect r(0, 0, 380, 195);
	SetPos(r);
	Name("Create Sub Folder");

	if (LoadFromResource(IDD_NEWFOLDER_F))
	{
		if (GetViewById(IDC_NAME, FolderName))
		{
			if (defaultName)
				FolderName->Name(defaultName);
			FolderName->Focus(true);
		}

		GetViewById(IDC_TYPE, FolderType);
		SetCtrlValue(IDC_TYPE, defaultType);

		int Ctrls[] = { IDC_MAIL, IDC_CONTACTS, IDC_FILTERS, IDC_CALENDER, IDC_GROUP };
		for (int c=0; c<CountOf(Ctrls); c++)
		{
			bool e = Enable ? Enable[c] : true;
			SetCtrlEnabled(Ctrls[c], e);
		}

		MoveToCenter();
	}
}

int CreateSubFolderDlg::OnNotify(LViewI *Ctrl, const LNotification &n)
{
	switch (Ctrl->GetId())
	{
		case IDOK:
		{
			if (FolderName)
				SubName = FolderName->Name();

			if (FolderType)
				typeIndex = (int)FolderType->Value();

			EndModal(1);
			break;
		}
		case IDCANCEL:
		{
			EndModal(0);
			break;
		}
	}
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////
FolderNameDlg::FolderNameDlg(LView *parent, const char *Old)
{
	Name = NewStr(Old);
	SetParent(parent);

	if (LoadFromResource(IDD_FOLDER_NAME))
	{
		MoveToMouse();
		GetViewById(IDC_NAME, Ed);
	}
}

FolderNameDlg::~FolderNameDlg()
{
	DeleteArray(Name);
}

void FolderNameDlg::OnCreate()
{
	if (Ed && Name)
	{
		char16 *Txt = Utf8ToWide(Name);
		if (Txt)
		{
			Ed->NameW(Txt);
			DeleteArray(Txt);
		}
	}
}

int FolderNameDlg::OnNotify(LViewI *Ctrl, const LNotification &n)
{
	switch (Ctrl->GetId())
	{
		case IDOK:
		{
			if (Ed)
			{
				auto Txt = Ed->NameW();
				if (Txt)
				{
					DeleteArray(Name);
					Name = WideToUtf8(Txt);
				}
			}
			EndModal(1);
			break;
		}
		case IDCANCEL:
		{
			EndModal(0);
			break;
		}
	}
	return 0;
}

