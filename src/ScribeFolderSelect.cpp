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

	/// \returns the path of the selected folder...
	LString GetSelectedPath();
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
		if (auto l = dynamic_cast<FolderLeaf*>(i))		
			l->GetCss(true)->Display(vis);
		return true;
	});

	// Find matching items...
	View->ForAllItems([this](auto i)
	{
		if (auto l = dynamic_cast<FolderLeaf*>(i))
		{
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
		}
		return true;
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

LString ScribeFolderTree::GetSelectedPath()
{
	LString a;
	if (auto Leaf = dynamic_cast<FolderLeaf*>(Selection()))
	{
		if (auto Folder = Leaf->GetFolder())
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
				}			}
			
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
					d->Path = d->View->GetSelectedPath();

				EndModal(1);
			}
			break;
		}
		case IDC_NEW_FOLDER:
		{
			auto selectedPath = d->View->GetSelectedPath();
			if (!selectedPath)
				break;

			bool enable[] = {true, true, true, true, true};
			auto createDlg = new CreateSubFolderDlg(this, 0, enable, d->DefaultNewFolderName);
			createDlg->DoModal([this, selectedPath, createDlg](auto dlg, auto code)
				{
					if (!code)
						return; // user cancelled

					auto folder = d->App->GetFolder(selectedPath);
					if (!folder)
						return;

					auto newFolderType = createDlg->getType();
					if (!newFolderType)
						return;

					auto Sub = folder->GetSubFolder(createDlg->SubName);
					if (!Sub)
						Sub = folder->CreateSubFolder(createDlg->SubName, newFolderType);
					if (!Sub)
						return;

					auto s = d->View->Selection();
					if (!s)
						return;

					auto NewLeaf = new FolderLeaf(d, Sub);
					s->Insert(NewLeaf);
					NewLeaf->Select(true);
					NewLeaf->ScrollTo();
				});
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
				d->Path = d->View->GetSelectedPath();

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
