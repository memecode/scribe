#include "Lgi.h"
#include "LOptionsFile.h"
#include "LDocApp.h"
#include "LMime.h"
#include "LTree.h"
#include "InetTools.h"
#include "LTextLog.h"
#include "LToken.h"

enum Id
{
	ID_SAVE = 200,
};

class MimeNode : public LTreeItem
{
	LMime *Mime;
	LStream *Data;

public:
	MimeNode(GTreeNode *parent, LMime *m, LStream *d)
	{
		Mime = m;
		Data = d;
		parent->Insert(this);
		parent->Expanded(true);

		char *Mt = Mime->GetMimeType();
		SetText(Mt ? Mt : "(none)");

		for (int k=0; k<m->Length(); k++)
		{
			new MimeNode(this, (*m)[k], d);
		}
	}

	void OnSelect()
	{
		Data->SetSize(0);

		char *h = Mime->GetHeaders();
		if (h)
			Data->Print("%s\n", h);

		LToken m(Mime->GetMimeType(), "/");
		LStreamI *s = Mime->GetData();
		if (s && s->GetSize() < 0)
			s = 0;

		if (m.Length() < 1 || !stricmp(m[0], "text"))
		{
			if (s)
			{
				char Buf[1024];
				int r;
				while ((r = s->Read(Buf, sizeof(Buf))) > 0)
				{
					Data->Write(Buf, r);
				}
			}
		}
		else if (s)
		{
			Data->Print("<<<%I64i binary bytes>>>", s->GetSize());
		}
	}

	void OnMouseClick(LMouse &m)
	{
		if (m.IsContextMenu())
		{
			LSubMenu s;
			s.AppendItem("Save Data To...", ID_SAVE, true);

			m.ToScreen();
			switch (s.Float(GetTree(), m.x, m.y))
			{
				case ID_SAVE:
				{
					LFileSelect s;
					s.Parent(GetTree());

					LAutoString FileName(Mime->GetSub("Content-Disposition", "filename"));
					if (FileName)
						s.Name(FileName);

					if (s.Save())
					{
						LFile out;
						if (out.Open(s.Name(), O_WRITE))
						{
							LStreamI *s = Mime->GetData();
							LCopyStreamer c;
							c.Copy(s, &out);
						}
					}
					break;
				}
			}
		}
	}
};

class App : public LDocApp<LOptionsFile>
{
	LTree *Tree;
	LSplitter *Split;
	LTextLog *Edit;
	LAutoPtr<LMime> Mime;

public:
	App()
	{
		SetPos(LRect(0, 0, 1200, 1024));
		MoveToCenter();
		Name("Mime Viewer");
		SetQuitOnClose(true);
		if (Attach(0))
		{
			DropTarget(true);
			if (Split = new LSplitter)
			{
				Split->Value(300);
				Split->Raised(false);
				Split->Border(false);

				Split->SetViewB(Edit = new LTextLog(101), false);
				Split->SetViewA(Tree = new LTree(100, 0, 0, 100, 100), false);
				Split->Attach(this);
			}

			Visible(true);
		}
		else LExitApp();
	}
	
	bool OpenFile(char *FileName, bool ReadOnly)
	{
		LFile f;
		if (f.Open(FileName, O_READ))
		{
			Tree->Empty();

			if (Mime.Reset(new LMime))
			{
				if (Mime->Text.Decode.Pull(&f))
				{
					MimeNode *n = new MimeNode(Tree, Mime, Edit);
				}
			}
		}

		return false;
	}

	bool SaveFile(char *FileName)
	{
		return false;
	}
};

int LgiMain(OsAppArguments &AppArgs)
{
	LApp a("application/x-mimeview", AppArgs);
	if (a.IsOk())
	{
		a.AppWnd = new App;
		a.Run();
	}
	return 0;
}
