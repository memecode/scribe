#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <sys/timeb.h>

#include "Scribe.h"
#include "lgi/common/NetTools.h"
#include "lgi/common/Edit.h"
#include "lgi/common/TextLabel.h"
#include "lgi/common/TextFile.h"
#include "resdefs.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/FileSelect.h"

///////////////////////////////////////////////////////////////////////////
ChooseFolderDlg::ChooseFolderDlg
(
	ScribeWnd *parent,
	bool IsExport,
	const char *Title,
	const char *Msg,
	char *DefFolder,
	int FolderType,
	LArray<char*> *Files
)
{
	Type = FolderType;
	SetParent(App = parent);
	DestFolder = 0;
	Export = IsExport;
	Lst = 0;

	if (LoadFromResource(Export ? IDD_FILES_EXPORT : IDD_FILES_IMPORT))
	{
		Name(Title);

		if (GetViewById(IDC_FOLDER, Folder))
		{
			Folder->Name(DefFolder ? DefFolder : (char*)"/");
			Folder->Enabled(false);
		}

		SetCtrlName(IDC_MSG, Msg);

		if (GetViewById(IDC_FILES, Lst))
		{
			if (Files)
			{
				for (unsigned i=0; i<Files->Length(); i++)
				{
					InsertFile((*Files)[i]);
				}
			}
		}
	}

	MoveToCenter();
}

ChooseFolderDlg::~ChooseFolderDlg()
{
	DeleteArray(DestFolder);
	SrcFiles.DeleteArrays();
}

void ChooseFolderDlg::InsertFile(const char *f)
{
	if (Lst)
	{
		bool Has = false;
		for (auto n : *Lst)
		{
			char Path[MAX_PATH_LEN];
			LMakePath(Path, sizeof(Path), n->GetText(0), n->GetText(1));
			if (_stricmp(Path, f) == 0)
			{
				Has = true;
				break;
			}
		}

		if (!Has)
		{
			LListItem *n = new LListItem;
			if (n)
			{
				LString file = f;
				auto parts = file.RSplit(DIR_STR, 1);
				if (parts.Length() == 2)
				{
					n->SetText(parts[0], 0);
					n->SetText(parts[1], 1);
					Lst->Insert(n);
				}
				else LAssert(0);
			}
		}
	}
}

int ChooseFolderDlg::OnNotify(LViewI *Ctrl, LNotification n)
{
	switch (Ctrl->GetId())
	{
		case IDC_FILES:
		{
			if (Lst && n.Type == LNotifyDeleteKey)
			{
				List<LListItem> Sel;
				if (Lst->GetSelection(Sel))
				{
					Sel.DeleteObjects();
				}
			}
			break;
		}
		case IDC_REMOVE_FILES:
		{
			if (Lst)
			{
				List<LListItem> Sel;
				if (Lst->GetSelection(Sel))
				{
					Sel.DeleteObjects();
				}
			}			
			break;
		}
		case IDC_PICK_FILES:
		{
			if (Lst)
			{
				LFileSelect s;

				s.Parent(this);
				s.MultiSelect(!Export);
				s.Type("All Files", LGI_ALL_FILES);
				s.Type("MBOX Files", "*.mbx;*.mbox");
				s.Type("Outlook Express Folders", "*.mbx;*.dbx");
				s.Type("Mozilla Address Book", "*.mab");
				s.Type("Eudora Address Book", "NNdbase.txt");

				if (s.Open())
				{
					if (Export)
					{
						Lst->Empty();
					}

					for (int i=0; i<s.Length(); i++)
					{
						InsertFile(s[i]);
					}
				}
			}
			break;
		}
		case IDC_SET_FOLDER:
		{
			FolderDlg Dlg(this, App, Type);
			if (Folder)
			{
				if (Dlg.DoModal())
				{
					char *f = Dlg.Get();
					if (f)
					{
						Folder->Name(f);
					}
				}
			}
			break;
		}
		case IDOK:
		{
			if (Lst)
			{
				for (auto n : *Lst)
				{
					char Path[MAX_PATH_LEN];
					LMakePath(Path, sizeof(Path), n->GetText(0), n->GetText(1));
					SrcFiles.Insert(NewStr(Path));
				}
			}

			if (Folder)
			{
				DestFolder = NewStr(Folder->Name());
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

///////////////////////////////////////////////////////////////////////////
void Import_UnixMBox(ScribeWnd *Parent)
{
	ScribeFolder *Cur = Parent->GetCurrentFolder();
	LString Path;
	if (Cur)
	    Path = Cur->GetPath();
	ChooseFolderDlg Dlg(Parent, false, LLoadString(IDS_MBOX_IMPORT), LLoadString(IDS_MBOX_SELECT_FOLDER), Path);
	if (Dlg.DoModal() && Dlg.DestFolder)
	{
		ScribeFolder *Folder = Parent->GetFolder(Dlg.DestFolder);
		if (Folder)
		{
			for (auto File: Dlg.SrcFiles)
			{
				LTextFile F;
				if (F.Open(File, O_READ))
				{
					Folder->Import(F, sMimeMbox);
				}
			}
		}
	}
}

void Export_UnixMBox(ScribeWnd *Parent)
{
	ScribeFolder *Cur = Parent->GetCurrentFolder();
	LString Path;
	if (Cur)
	    Path = Cur->GetPath();
	ChooseFolderDlg Dlg(Parent,
						true,
						LLoadString(IDS_MBOX_EXPORT),
						LLoadString(IDS_MBOX_EXPORT_FOLDER),
						Path);
	if (Dlg.DoModal() && Dlg.DestFolder)
	{
		ScribeFolder *Folder = Parent->GetFolder(Dlg.DestFolder);
		if (Folder)
		{
			for (auto File: Dlg.SrcFiles)
			{
				if (!LFileExists(File) ||
					LgiMsg(Parent, LLoadString(IDS_ERROR_FILE_EXISTS), AppName, MB_YESNO, File) == IDYES)
				{
					LFile F;
					if (F.Open(File, O_WRITE))
					{
						Folder->Export(F, sMimeMbox);
					}
				}
			}
		}
	}
}
