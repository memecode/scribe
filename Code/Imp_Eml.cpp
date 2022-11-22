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
	LAutoString In;
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
	
	int Scan(LTreeNode *Parent, char *Folder)
	{
		LDirectory d;

		int Email = 0;
		int ChildEmail = 0;
		LTreeItem *i = new LTreeItem;

		for (int b=d.First(Folder); b; b=d.Next())
		{
			if (d.IsDir())
			{
				char c[MAX_PATH_LEN];
				d.Path(c, sizeof(c));
				ChildEmail += Scan(i, c);
			}
			else
			{
				char *ext = strrchr(d.GetName(), '.');
				if (ext && !_stricmp(ext, ".eml"))
				{
					Email++;
				}
			}
		}

		if (Email || ChildEmail)
		{
			char *Leaf = strrchr(Folder, DIR_CHAR);
			char Msg[MAX_PATH_LEN];
			sprintf_s(Msg, sizeof(Msg), "%s (%i)", Leaf + 1, Email);
			i->SetText(Msg);
			Parent->Insert(i);
			Parent->Expanded(true);
		}
		else
		{
			DeleteObj(i);
		}

		return Email + ChildEmail;
	}

	int OnNotify(LViewI *c, LNotification n)
	{
		switch (c->GetId())
		{
			case IDC_SET_IN:
			{
				LFileSelect s;
				s.Parent(this);
				s.Type("Email Files", "*.eml");
				s.Type("All Files", LGI_ALL_FILES);
				if (s.Open())
				{
					char p[MAX_PATH_LEN];
					strcpy_s(p, sizeof(p), s.Name());
					LTrimDir(p);
					SetCtrlName(IDC_IN_FOLDER, p);
					
					if (LDirExists(p))
					{
						LTree *t;
						if (GetViewById(IDC_TREE, t))
						{
							TotalEmail = Scan(t, p);
						}
					}
				}
				break;
			}
			case IDC_SET_OUT:
			{
				FolderDlg d(this, App, MAGIC_MAIL);
				if (d.DoModal())
				{
					char *NewPath = d.Get();
					if (NewPath)
					{
						Out = App->GetFolder(NewPath);
						if (Out)
							SetCtrlName(IDC_OUT_FOLDER, Out->GetPath());
					}
				}
				break;
			}
			case IDOK:
			{
				In.Reset(NewStr(GetCtrlName(IDC_IN_FOLDER)));
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
		DoEvery Timer(300);
		
		LDataStoreI::StoreTrans Trans = Out->GetObject()->GetStore()->StartTransaction();
		for (int b=d.First(In); b && !Prog->IsCancelled(); b=d.Next())
		{
			if (!d.IsDir())
			{
				char *ext = strrchr(d.GetName(), '.');
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
					if (Timer.DoNow())
						LYield();
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
				Child = Out->CreateSubDirectory(d.GetName(), MAGIC_MAIL);
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
	LImportEml Dlg(App);
	if (Dlg.DoModal())
	{
		int Errors = 0;
		LProgressDlg Prog(App);
		Prog.SetRange(Dlg.TotalEmail);
		Prog.SetDescription("Email");
		ImportEmlFolders(App, Prog.ItemAt(0), Dlg.Out, Dlg.In, &Errors);
		if (Errors)
		{
			Prog.Visible(false);
			LgiMsg(App, "%i email failed to import.", AppName, MB_OK, Errors);
		}
	}
}
