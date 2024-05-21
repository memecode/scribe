#include "Scribe.h"
#include "lgi/common/Db.h"
#include "lgi/common/XmlTree.h"
#include "lgi/common/FileSelect.h"
#include "resdefs.h"

//////////////////////////////////////////////////////////////////////////
class LImportEml : public LDialog
{
	ScribeWnd *App;

public:
	LString In;
	ScribeFolder *Out;
	int TotalEmail;

	LImportEml(ScribeWnd *app)
	{
		Out = 0;
		SetParent(App = app);
		if (LoadFromResource(IDD_EML_IMPORT))
		{
			MoveToCenter();
		}

		if ((Out = App->GetCurrentFolder()))
			SetCtrlName(IDC_OUT_FOLDER, Out->GetPath());
	}
	
	int Scan(LTreeNode *Parent, const char *Folder)
	{
		LDirectory d;

		int Email = 0;
		int ChildEmail = 0;
		LAutoPtr<LTreeItem> i(new LTreeItem);

		for (auto b=d.First(Folder); b; b=d.Next())
		{
			if (d.IsDir())
			{
				char c[MAX_PATH_LEN];
				d.Path(c, sizeof(c));
				ChildEmail += Scan(i, c);
			}
			else
			{
				auto ext = strrchr(d.GetName(), '.');
				if (ext && !_stricmp(ext, ".eml"))
				{
					Email++;
				}
			}
		}

		if (Email || ChildEmail)
		{
			auto Leaf = strrchr(Folder, DIR_CHAR);
			char Msg[MAX_PATH_LEN];
			sprintf_s(Msg, sizeof(Msg), "%s (%i)", Leaf + 1, Email);
			i->SetText(Msg);
			Parent->Insert(i.Release());
			Parent->Expanded(true);
		}

		return Email + ChildEmail;
	}

	int OnNotify(LViewI *c, LNotification n)
	{
		switch (c->GetId())
		{
			case IDC_SET_IN:
			{
				auto s = new LFileSelect(this);
				s->Type("Email Files", "*.eml");
				s->Type("All Files", LGI_ALL_FILES);
				s->OpenFolder([this](auto s, auto status)
				{
					if (status)
					{
						SetCtrlName(IDC_IN_FOLDER, s->Name());
					
						if (LDirExists(s->Name()))
						{
							LTree *t;
							if (GetViewById(IDC_TREE, t))
								TotalEmail = Scan(t, s->Name());
						}
					}
				});
				break;
			}
			case IDC_SET_OUT:
			{
				auto Dlg = new FolderDlg(this, App, MAGIC_MAIL);
				Dlg->DoModal([this, Dlg](auto dlg, auto id)
				{
					if (id)
					{
						Out = App->GetFolder(Dlg->Get());
						if (Out)
							SetCtrlName(IDC_OUT_FOLDER, Out->GetPath());
					}
				});
				break;
			}
			case IDOK:
			{
				In = GetCtrlName(IDC_IN_FOLDER);
				// fall through
			}
			case IDCANCEL:
			{
				EndModal(c->GetId() == IDOK);
				break;
			}
		}

		return 0;
	}
};

void ImportEmlFolders(ScribeWnd *App, LProgressPane *Prog, ScribeFolder *Out, char *In, int *Errors)
{
	LDirectory d;
	
	{
		LDataStoreI::StoreTrans Trans = Out->GetObject()->GetStore()->StartTransaction();
		for (int b=d.First(In); b && !Prog->IsCancelled(); b=d.Next())
		{
			if (!d.IsDir())
			{
				auto ext = strrchr(d.GetName(), '.');
				if (ext && !_stricmp(ext, ".eml"))
				{
					Thing *t = App->CreateItem(MAGIC_MAIL, NULL, false);
					if (t)
					{
						char c[MAX_PATH_LEN];
						d.Path(c, sizeof(c));

						LAutoPtr<LFile> Eml(new LFile);
						if (Eml->Open(c, O_READ))
						{
							if (t->Import(t->AutoCast(Eml), sMimeMessage))
							{
								t->Save(Out);
							}
							else (*Errors)++;
						}
						else (*Errors)++;
					}
					else (*Errors)++;
					
					Prog->Value(Prog->Value() + 1);
				}
			}
		}
	}

	for (int b=d.First(In); b && !Prog->IsCancelled(); b=d.Next())
	{
		if (d.IsDir())
		{
			char c[MAX_PATH_LEN];
			d.Path(c, sizeof(c));
			
			ScribeFolder *Child = Out->GetSubFolder(d.GetName());
			if (!Child)
			{
				Child = Out->CreateSubFolder(d.GetName(), MAGIC_MAIL);
			}
			if (Child)
			{			
				ImportEmlFolders(App, Prog, Child, c, Errors);
			}
		}
	}
}

void ImportEml(ScribeWnd *App)
{
	auto Dlg = new LImportEml(App);
	Dlg->DoModal([Dlg, App](auto dlg, auto id)
	{
		if (id)
		{
			int Errors = 0;
			LProgressDlg Prog(App);
			Prog.SetRange(Dlg->TotalEmail);
			Prog.SetDescription("Email");
			ImportEmlFolders(App, Prog.ItemAt(0), Dlg->Out, Dlg->In, &Errors);
			if (Errors)
			{
				Prog.Visible(false);
				LgiMsg(App, "%i email failed to import.", AppName, MB_OK, Errors);
			}
		}
	});
}
