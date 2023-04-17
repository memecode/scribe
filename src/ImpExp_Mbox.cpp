#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <sys/timeb.h>

#include "Scribe.h"
#include "lgi/common/NetTools.h"
#include "lgi/common/Edit.h"
#include "lgi/common/TextLabel.h"
#include "lgi/common/TextFile.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/FileSelect.h"

#include "FolderTask.h"
#include "resdefs.h"

///////////////////////////////////////////////////////////////////////////
ImportExportDlg::ImportExportDlg
(
	ScribeWnd *parent,
	bool IsExport,
	const char *Title,
	const char *Msg,
	LString::Array *srcFiles,
	const char *dstFolder,
	int FolderType
)
{
	Type = FolderType;
	SetParent(App = parent);
	Export = IsExport;

	if (LoadFromResource(Export ? IDD_FILES_EXPORT : IDD_FILES_IMPORT))
	{
		Name(Title);

		GetViewById(IDC_SRC, Src);
		GetViewById(IDC_DST, Dst);

		if (Src)
		{
			Src->ShowColumnHeader(false);
			if (srcFiles)
			{
				for (auto f: *srcFiles)
					InsertFile(f);
			}
		}
		else LAssert(0);

		if (Dst)
		{
			if (dstFolder)
				Dst->Name(dstFolder);
		}
		else LAssert(0);

		SetCtrlName(IDC_MSG, Msg);
	}

	MoveToCenter();
}

void ImportExportDlg::InsertFile(const char *f)
{
	if (!Src)
		return;

	bool Has = false;
	for (auto n : *Src)
	{
		auto path = n->GetText(0);
		if (Stricmp(path, f) == 0)
		{
			Has = true;
			break;
		}
	}

	if (Has)
		return;
	
	LListItem *n = new LListItem(f);
	if (!n)
		return;
	Src->Insert(n);
}

int ImportExportDlg::OnNotify(LViewI *Ctrl, LNotification n)
{
	if (!Src || !Dst)
		return 0;

	switch (Ctrl->GetId())
	{
		case IDC_ADD:
		{
			if (Export)
			{
				// Pick scribe folders to export:
				auto Dlg = new FolderDlg(this, App, Type);
				Dlg->DoModal([this, Dlg](auto dlg, auto ctrlId)
					{
						if (ctrlId)
						{
							auto f = Dlg->Get();
							if (f)
								InsertFile(f);
						}
						delete dlg;
					});
			}
			else
			{
				// Pick external MBOX files to import:
				auto s = new LFileSelect(this);

				s->MultiSelect(true);
				s->Type("All Files", LGI_ALL_FILES);
				s->Type("MBOX Files", "*.mbx;*.mbox");
				s->Type("Outlook Express Folders", "*.mbx;*.dbx");
				s->Type("Mozilla Address Book", "*.mab");
				s->Type("Eudora Address Book", "NNdbase.txt");
				s->Open([this](auto dlg, auto status)
				{
					if (status)
					{
						for (int i=0; i<dlg->Length(); i++)
							InsertFile((*dlg)[i]);
					}
					delete dlg;
				});
			}
			break;
		}
		case IDC_SRC:
		{
			if (n.Type == LNotifyDeleteKey)
			{
				List<LListItem> Sel;
				if (Src->GetSelection(Sel))
					Sel.DeleteObjects();
			}
			break;
		}
		case IDC_DEL:
		{
			List<LListItem> Sel;
			if (Src->GetSelection(Sel))
				Sel.DeleteObjects();
			break;
		}
		case IDC_SET_DEST:
		{
			if (Export)
			{
				// Pick external folder for writing to:
				auto s = new LFileSelect(this);

				s->MultiSelect(!Export);
				s->OpenFolder(	[this](auto s, auto status)
								{
									if (status)
										SetCtrlName(IDC_DST, s->Name());
									delete s;
								});
			}
			else
			{
				// Pick scribe folder for writing to:
				auto Dlg = new FolderDlg(this, App, Type);
				Dlg->DoModal([this, Dlg](auto dlg, auto ctrlId)
				{
					if (ctrlId)
					{
						auto f = Dlg->Get();
						if (f && Dst)
							Dst->Name(f);
					}
					delete dlg;
				});
			}
			break;
		}
		case IDOK:
		{
			for (auto n : *Src)
				SrcFiles.Add(n->GetText());
			DestFolder = Dst->Name();
			IncSubFolders = GetCtrlValue(IDC_SUB_FOLDERS) > 0;

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
	auto Cur = Parent->GetCurrentFolder();
	LString DstPath;
	if (Cur)
		DstPath = Cur->GetPath();
	
	auto Dlg = new ImportExportDlg(	Parent,
									false,
									LLoadString(IDS_MBOX_IMPORT),
									LLoadString(IDS_MBOX_SELECT_FOLDER),
									NULL, // Src
									DstPath);
	Dlg->DoModal([Dlg, Parent](auto dlg, auto ok)
	{
		if (ok && Dlg->DestFolder)
		{
			auto Folder = Parent->GetFolder(Dlg->DestFolder);
			if (Folder)
			{
				for (auto File: Dlg->SrcFiles)
				{
					LAutoPtr<LTextFile> F(new LTextFile);
					if (F->Open(File, O_READ))
						Folder->Import(Folder->AutoCast(F), sMimeMbox);
				}
			}
		}
		delete dlg;
	});
}

struct MboxExportTask : public LView::ViewEventTarget
{
	ScribeWnd *App = NULL;
	LString DestFolder;
	LString::Array SrcFiles;
	bool IncSubFolders = false;

	// Chain together enough ExportFolderTask instances to
	// get the SrcFiles written....
	MboxExportTask(ScribeWnd *Parent) :
		LView::ViewEventTarget(Parent, M_EXPORT_NEXT)
	{
		// Kick it off....
		App = Parent;
	}

	void Next()
	{
		PostEvent(M_EXPORT_NEXT);
	}

	LMessage::Result OnEvent(LMessage *Msg)
	{
		if (Msg->Msg() == M_EXPORT_NEXT)
		{
			if (SrcFiles.Length() == 0)
				return OnFinished();

			// Pop next folder path
			auto inPath = SrcFiles[0];
			SrcFiles.DeleteAt(0, true);
			auto folder = App->GetFolder(inPath);
			if (folder)
			{	
				LFile::Path outPath = DestFolder;
				outPath += LGetLeaf( folder->GetDropFileName() );
				auto p = outPath.GetFull();
				LAutoPtr<LStreamI> outFile;
				outFile.Reset(new LFile(p, O_WRITE));

				new ExportFolderTask(folder,
									outFile,
									sMimeMbox, 
									[this](auto prog, auto stream)
									{
										Next();
									});
				return 1;
			}

			Next();
		}

		return 0;
	}

	int OnFinished()
	{
		delete this;
		return 0;
	}
};

void Export_UnixMBox(ScribeWnd *Parent)
{
	ScribeFolder *Cur = Parent->GetCurrentFolder();
	LString::Array SrcPath;
	if (Cur)
		SrcPath.Add(Cur->GetPath());
	
	auto Dlg = new ImportExportDlg(	Parent,
									true,
									LLoadString(IDS_MBOX_EXPORT),
									LLoadString(IDS_MBOX_EXPORT_FOLDER),
									&SrcPath);
	Dlg->DoModal([Dlg, Parent](auto dlg, auto ok)
	{
		if (ok &&
			Dlg->SrcFiles.Length() > 0 &&
			Dlg->DestFolder)
		{
			auto task = new MboxExportTask(Parent);
			task->DestFolder = Dlg->DestFolder;
			task->SrcFiles = Dlg->SrcFiles;
			task->IncSubFolders = Dlg->IncSubFolders;
			task->Next();
		}
		delete dlg;
	});
}


/*

	ScribeFolder *Folder = Parent->GetFolder(Dlg->DestFolder);
	if (Folder)
	{
		for (auto File: Dlg->SrcFiles)
		{
			if (!LFileExists(File) ||
				LgiMsg(	Parent,
						LLoadString(IDS_ERROR_FILE_EXISTS),
						AppName,
						MB_YESNO,
						File.Get()) == IDYES)
			{
				LAutoPtr<LFile> F(new LFile);
				if (F->Open(File, O_WRITE))
					Folder->Export(Folder->AutoCast(F), sMimeMbox);
			}
		}
	}

*/