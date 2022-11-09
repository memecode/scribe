/*
**	FILE:			ScribeFolderSelect.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			1/6/1999
**	DESCRIPTION:	Scribe Folder Selection
**
**	Copyright (C) 1999, Matthew Allen
**		fret@memecode.com
*/

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#include "Scribe.h"
#include "lgi/common/Button.h"
#include "resdefs.h"

class FolderDlgPriv;
class ScribeFolderTree : public LTree
{
	void AddFolder(FolderDlgPriv *d, LTreeItem *i, ScribeFolder *f);

public:
	ScribeFolderTree(int id, int x, int y, int cx, int cy);

	void Setup(FolderDlgPriv *d, ScribeFolder *Root, const char *InitSel);
	LString Get2();
};

class FolderDlgPriv
{
public:
	FolderDlg *Dlg;
	ScribeWnd *App;
	bool CreateNew;
	int LimitTo;
	LString Path;
	LAutoString DefaultNewFolderName;
	LAutoString DialogMsg;
	ScribeFolderTree *View;

	FolderDlgPriv(ScribeWnd *app, FolderDlg *t, bool create)
	{
		CreateNew = create;
		Dlg = t;
		View = 0;
		App = app;
		LimitTo = MAGIC_NONE;
	}
};

//////////////////////////////////////////////////////////////////////////////
class FolderLeaf : public LTreeItem
{
	FolderDlgPriv *d;
	ScribeFolder *Folder;

	bool IsSelectable()
	{
		auto fType = Folder ? Folder->GetItemType() : 0;
		return	d->LimitTo == MAGIC_NONE ||
				d->LimitTo == MAGIC_ANY ||
				(Folder && d->LimitTo == fType);
	}

public:
	FolderLeaf(FolderDlgPriv *priv, ScribeFolder *folder)
	{
		d = priv;
		Folder = folder;
	}

	const char *GetText(int i=0)
	{
		return (Folder) ? Folder->GetText(i) : "<error>";
	}

	int GetImage(int Flags = 0)
	{
		if (IsSelectable())
		{
			return (Flags) ? ICON_OPEN_FOLDER : ICON_CLOSED_FOLDER;
		}

		return ICON_DISABLED_FOLDER;
	}

	ScribeFolder *GetFolder()
	{
		return Folder;
	}

	void OnSelect()
	{
		LViewI *Parent = Tree->LView::GetParent();
		if (Parent)
		{
			Parent->SetCtrlEnabled(IDOK, IsSelectable());
			Parent->SetCtrlEnabled(IDC_NEW_FOLDER, IsSelectable() && d->CreateNew);
		}
	}
};

//////////////////////////////////////////////////////////////////////////////
class LFolderCtrlFactory : public LViewFactory
{
	LView *NewView(const char *Class, LRect *Pos, const char *Text)
	{
		if (Class && !_stricmp(Class, "ScribeFolderTree"))
		{
			return new ScribeFolderTree(-1, 0, 0, 100, 100);
		}

		return 0;
	}

} FolderCtrlFactory;

ScribeFolderTree::ScribeFolderTree(int id, int x, int y, int cx, int cy) :
	LTree(id, x, y, cx, cy, "")
{
	_ObjName = Res_Custom;
	Sunken(true);
	AskImage(true);
}

void ScribeFolderTree::Setup(FolderDlgPriv *d, ScribeFolder *Root, const char *InitSel)
{
	if (!Root)
		return;
		
	FolderLeaf *Leaf = new FolderLeaf(d, Root);
	if (Leaf)
	{
		Insert(Leaf);
		AddFolder(d, Leaf, Root);

		ScribeFolder *f = Root;
		while ((f = f->GetNextFolder()))
		{
			Leaf = new FolderLeaf(d, f);
			if (Leaf)
			{
				Insert(Leaf);
				AddFolder(d, Leaf, f);
			}
		}

		if (InitSel)
		{
			LToken Path(InitSel, "/");
			LTreeNode *n = this;
			for (unsigned i=0; i<Path.Length(); i++)
			{
				FolderLeaf *Match = 0;
				for (FolderLeaf *c = dynamic_cast<FolderLeaf*>(n->GetChild()); c;
								 c = dynamic_cast<FolderLeaf*>(c->GetNext()))
				{
					auto s = c->GetFolder()->GetName(true);
					if (s.Equals(Path[i]))
					{
						Match = c;
						break;
					}
				}

				if (Match)
					n = Match;
				else
					break;
			}

			LTreeItem *it = dynamic_cast<LTreeItem*>(n);
			if (it)
				it->Select(true);
		}
	}
}

void ScribeFolderTree::AddFolder(FolderDlgPriv *d, LTreeItem *i, ScribeFolder *f)
{
	if (i && f)
	{
		for (ScribeFolder *Folder = f->GetChildFolder(); Folder; Folder = Folder->GetNextFolder())
		{
			FolderLeaf *Leaf = new FolderLeaf(d, Folder);
			if (Leaf)
			{
				i->Insert(Leaf);
				AddFolder(d, Leaf, Folder);
				i->Expanded(true);
			}
		}
	}
}

LString ScribeFolderTree::Get2()
{
	LString a;
	FolderLeaf *Leaf = dynamic_cast<FolderLeaf*>(Selection());
	if (Leaf)
	{
		ScribeFolder *Folder = Leaf->GetFolder();
		if (Folder)
			a = Folder->GetPath();
	}

	return a;
}

//////////////////////////////////////////////////////////////////////////////
FolderDlg::FolderDlg(	LViewI *parent,
						ScribeWnd *app,
						int LimitToType,
						ScribeFolder *Root,
						const char *InitialSelect,
						bool AllowCreate,
						char *DefaultNewFolderName,
						char *DialogMsg)
{
	d = new FolderDlgPriv(app, this, AllowCreate);
	d->DefaultNewFolderName.Reset(NewStr(DefaultNewFolderName));
	d->LimitTo = LimitToType;
	SetParent(parent);

	if (LoadFromResource(IDD_FOLDER_SELECT))
	{
		MoveToCenter();

		if (GetViewById(IDC_FOLDERS, d->View))
		{
			if (!Root)
			{
				LMailStore *Def = d->App->GetDefaultMailStore();
				if (Def)
				{
					Root = Def->Root;
					while (Root->GetPrev())
					{
						Root = dynamic_cast<ScribeFolder*>(Root->GetPrev());
					}
				}
			}
			
			d->View->Setup(d, Root, InitialSelect);
			d->View->SetImageList(d->App->GetIconImgList(), false);
		}

		LViewI *msg;
		if (DialogMsg && GetViewById(IDC_FOLDER_MSG, msg))
		{
			msg->Name(DialogMsg);
		}
	}

	SetCtrlEnabled(IDOK, false);
	SetCtrlEnabled(IDC_NEW_FOLDER, false);
}

FolderDlg::~FolderDlg()
{
	DeleteObj(d);
}

char *FolderDlg::Get()
{
	return d->Path;
}

int FolderDlg::OnNotify(LViewI *Ctrl, LNotification n)
{
	switch (Ctrl->GetId())
	{
		case IDC_NEW_FOLDER:
		{
			auto Cur = d->View->Get2();
			if (Cur)
			{
				Store3ItemTypes Type[] = { MAGIC_MAIL, MAGIC_CONTACT, MAGIC_FILTER, MAGIC_CALENDAR, MAGIC_GROUP };
				bool Enable[] = {true, true, true, true, true};
				CreateSubFolderDlg Dlg(this, 0, Enable, d->DefaultNewFolderName);
				ScribeFolder *f = d->App->GetFolder(Cur);
				if (f)
				{
					ScribeFolder *Sub = f->GetSubFolder(Dlg.SubName);
					if (!Sub)
						Sub = f->CreateSubDirectory(Dlg.SubName, Type[Dlg.SubType]);
					if (!Sub)
						break;

					LTreeItem *s = d->View->Selection();
					if (!s)
						break;

					FolderLeaf *NewLeaf = new FolderLeaf(d, Sub);
					s->Insert(NewLeaf);
					NewLeaf->Select(true);
					NewLeaf->ScrollTo();
				}
			}
			break;
		}
		case IDOK:
		{
			if (d->View)
			{
				d->Path = d->View->Get2();
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
