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
	FolderDlg *Dlg = NULL;
	ScribeWnd *App = NULL;
	bool CreateNew = false;
	int LimitTo = MAGIC_NONE;
	LString Path;
	LString DefaultNewFolderName;
	LString Filter;
	ScribeFolderTree *View = NULL;

	FolderDlgPriv(ScribeWnd *app, FolderDlg *t, bool create)
	{
		CreateNew = create;
		Dlg = t;
		App = app;
	}

	void OnFilter();
};

//////////////////////////////////////////////////////////////////////////////
struct FolderLeaf : public LTreeItem
{
	FolderDlgPriv *d;
	ScribeFolder *Folder;
	bool isRoot = false;

	FolderLeaf(FolderDlgPriv *priv, ScribeFolder *folder, bool isroot = false)
	{
		d = priv;
		Folder = folder;
		isRoot = isroot;
	}

	bool IsSelectable()
	{
		auto fType = Folder ? Folder->GetItemType() : 0;
		return	d->LimitTo == MAGIC_NONE ||
				d->LimitTo == MAGIC_ANY ||
				(Folder && d->LimitTo == fType);
	}

	const char *GetText(int i=0) override
	{
		if (!Folder)
			return "<error>";
		if (isRoot)
			return "Folders";

		return Folder->GetText(i);
	}

	int GetImage(int Flags = 0) override
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

	void OnSelect() override
	{
		LViewI *Parent = Tree->LView::GetParent();
		if (Parent)
		{
			Parent->SetCtrlEnabled(IDOK, IsSelectable());
			Parent->SetCtrlEnabled(IDC_NEW_FOLDER, IsSelectable() && d->CreateNew);
		}
	}
};

void FolderDlgPriv::OnFilter()
{
	// Initialize visible
	View->ForAllItems([vis = Filter ? LCss::DispNone : LCss::DispBlock](auto i)
	{
		FolderLeaf *l = dynamic_cast<FolderLeaf*>(i);
		if (!l) return;
		
		l->GetCss(true)->Display(vis);
	});

	// Find matching items...
	View->ForAllItems([this](auto i)
	{
		FolderLeaf *l = dynamic_cast<FolderLeaf*>(i);
		if (!l) return;

		auto nm = l->GetText();
		if (Stristr(nm, this->Filter.Get()))
		{
			l->GetCss(true)->Display(LCss::DispBlock);

			for (auto p = l->GetParent(); p; p = p->GetParent())
			{
				FolderLeaf *lp = dynamic_cast<FolderLeaf*>(p);
				if (lp) lp->GetCss(true)->Display(LCss::DispBlock);
			}
		}
	});

	View->UpdateAllItems();
	View->Invalidate();
}

//////////////////////////////////////////////////////////////////////////////
class LFolderCtrlFactory : public LViewFactory
{
	LView *NewView(const char *Class, LRect *Pos, const char *Text)
	{
		if (!Stricmp(Class, "ScribeFolderTree"))
			return new ScribeFolderTree(-1, 0, 0, 100, 100);

		return 0;
	}

}	FolderCtrlFactory;

ScribeFolderTree::ScribeFolderTree(int id, int x, int y, int cx, int cy) :
	LTree(id, x, y, cx, cy, "")
{
	SetObjectName(Res_Custom);
	Sunken(true);
	AskImage(true);
}

void ScribeFolderTree::Setup(FolderDlgPriv *d, ScribeFolder *Root, const char *InitSel)
{
	if (!Root)
	{
		LAssert(!"No root");
		return;
	}
		
	auto Leaf = new FolderLeaf(d, Root, true);
	if (!Leaf)
	{
		LAssert(!"Alloc failed.");
		return;
	}

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
		auto Path = LString(InitSel).SplitDelimit("/");
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

	UpdateAllItems();
}

void ScribeFolderTree::AddFolder(FolderDlgPriv *d, LTreeItem *i, ScribeFolder *f)
{
	if (!i || !f)
	{
		LAssert(!"Missing param");
		return;
	}

	f->LoadFolders();

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
	d->DefaultNewFolderName = DefaultNewFolderName;
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
					Root = Def->GetRoot();
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

int FolderDlg::OnNotify(LViewI *Ctrl, const LNotification &n)
{
	switch (Ctrl->GetId())
	{
		case IDC_FOLDERS:
		{
			if (n.Type == LNotifyItemDoubleClick)
			{
				if (d->View)
					d->Path = d->View->Get2();

				EndModal(1);
			}
			break;
		}
		case IDC_NEW_FOLDER:
		{
			auto Cur = d->View->Get2();
			if (!Cur)
				break;

			Store3ItemTypes Type[] = { MAGIC_MAIL, MAGIC_CONTACT, MAGIC_FILTER, MAGIC_CALENDAR, MAGIC_GROUP };
			bool Enable[] = {true, true, true, true, true};
			CreateSubFolderDlg Dlg(this, 0, Enable, d->DefaultNewFolderName);
			ScribeFolder *f = d->App->GetFolder(Cur);
			if (!f)
				break;

			ScribeFolder *Sub = f->GetSubFolder(Dlg.SubName);
			if (!Sub)
				Sub = f->CreateSubFolder(Dlg.SubName, Type[Dlg.SubType]);
			if (!Sub)
				break;

			LTreeItem *s = d->View->Selection();
			if (!s)
				break;

			FolderLeaf *NewLeaf = new FolderLeaf(d, Sub);
			s->Insert(NewLeaf);
			NewLeaf->Select(true);
			NewLeaf->ScrollTo();
			break;
		}
		case IDC_FILTER:
		{
			LString n = Ctrl->Name();
			if (d->Filter != n)
			{
				d->Filter = n;
				d->OnFilter();
			}
			break;
		}
		case IDC_CLEAR:
		{
			d->Filter.Empty();
			SetCtrlName(IDC_FILTER, NULL);
			d->OnFilter();
			break;
		}
		case IDOK:
		{
			if (d->View)
				d->Path = d->View->Get2();

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
