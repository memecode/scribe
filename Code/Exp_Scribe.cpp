
#include "Scribe.h"

#include "lgi/common/List.h"
#include "lgi/common/ProgressDlg.h"
#include "lgi/common/Store3.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/FileSelect.h"

#include "ScribeFolderSelect.h"
#include "../Resources/resdefs.h"
#include "FolderTask.h"

/*

	int CountItems(ScribeFolder *f, bool Children)
	{
		int Status = 0;

		if (f && f->Store)
		{
			for (StorageItem *i = f->Store->GetChild(); i; i = i->GetNext())
			{
				if (i->GetType() == MAGIC_MAIL ||
					i->GetType() == MAGIC_CONTACT)
				{
					Status++;
				}
			}

			if (Children)
			{
				for (ScribeFolder *c = f->GetChildFolder(); c; c = c->GetNextFolder())
				{
					if ((!Spam || c != Spam) &&
						(!Trash || c != Trash))
					{
						Status += CountItems(c, Children);
					}
				}
			}
		}

		return Status;
	}

*/

struct ExportParams
{
	bool AllFolders = false;
	bool ExceptTrashSpam = false;
	LString DestFolders; // Mail3 path for dest folders;
	LString DestPath; // Sub folder path for export
	LString::Array SrcPaths;
};

struct Mail3Folders
{
	ScribeWnd *App = NULL;
	LAutoPtr<LDataStoreI> Store;
	LAutoPtr<ScribeFolder> Root;

	Mail3Folders(ScribeWnd *app) : App(app)
	{
		int asd=0;
	}

	Mail3Folders(Mail3Folders &src)
	{
		App = src.App;

		if (src)
		{
			Store = src.Store;
			Root = src.Root;
		}
		else LAssert(!"Src not loaded.");
	}

	operator bool()
	{
		return Store != NULL && Root != NULL;
	}

	void LoadFolders(const char *FilePath)
	{
		if (!Store)
			Store.Reset(App->CreateDataStore(FilePath, true));

		if (Store && !Root)
		{
			if (Root.Reset(new ScribeFolder))
			{
				Root->App = App;
				Root->SetObject(Store->GetRoot(), false, _FL);
			}
		}
	}

	void Unload()
	{
		Root.Reset();
		Store.Reset();
	}

	ScribeFolder *GetFolder(LString Path, Store3ItemTypes CreateItemType = MAGIC_NONE)
	{
		if (!Root)
		{
			LAssert(!"No root loaded.");
			return NULL;
		}

		auto parts = Path.SplitDelimit("/");
		ScribeFolder *f = Root;
		for (auto p: parts)
		{
			auto c = f->GetSubFolder(p);
			if (!c)
			{
				if (CreateItemType != MAGIC_NONE)
				{
					c = f->CreateSubDirectory(p, CreateItemType);
					if (!c)
						return NULL;
				}
				else return NULL;
			}

			f = c;
		}

		return f;
	}
};

struct ScribeExportTask : public FolderTask
{
	int FolderLoadErrors = 0;
	int MailCreated = 0;
	int MailSkipped = 0;
	int MailErrors = 0;
	int ContactCreated = 0;
	int ContactSkipped = 0;
	int ContactErrors = 0;

	LMailStore *SrcStore = NULL; // Source data store
	Mail3Folders Dst;
	ScribeFolder *Spam = NULL;
	ScribeFolder *Trash = NULL;
	ExportParams Params;

	// Working state, used to iterate over the exporting process while
	// being able to yield to the OS at regular intervals.
	enum ExportState
	{
		ExpNone,
		ExpGetNext,
		ExpLoadFolders,
		ExpItems,
		ExpFinished,
	}	State = ExpNone;

	LString::Array InputPaths;
	ScribeFolder *SrcFolder = NULL;
	LArray<LDataI*> SrcItems;
	ScribeFolder *DstFolder = NULL;
	LDataFolderI *DstObj = NULL;
	LDataStoreI *DstStore = NULL;
	LHashTbl<ConstStrKey<char>,LDataI*> DstMsgIds;

	ScribeExportTask(struct ScribeExportDlg *dlg);

	LString ContactKey(Contact *c);	
	bool TimeSlice();
	bool CopyAttachments(LDataI *outMail, LDataPropI *outSeg, LDataPropI *inSeg, LString &err);

	LString MakePath(LString path)
	{
		LString sep;
		auto p = Params.DestPath.SplitDelimit(sep);
		p += path.Strip(sep).SplitDelimit(sep).Slice(1);
		return sep + sep.Join(p);
	}

	void CollectPaths(ScribeFolder *f, LString::Array &paths)
	{
		paths.Add(f->GetPath());
		for (auto c = f->GetChildFolder(); c; c = c->GetNextFolder())
			CollectPaths(c, paths);
	}

	void OnComplete()
	{
		LgiMsg(	this,
				"Mail export complete.\n"
				"\n"
				"    Email: %i created, %i already exist, %i errors\n"
				"    Contacts: %i created, %i already exist, %i errors",
				"Export",
				MB_OK,
				MailCreated,
				MailSkipped,
				MailErrors,
				ContactCreated,
				ContactSkipped,
				ContactErrors);
	}
};

struct ScribeExportDlg : public LDialog, public LDataEventsI
{
	ScribeWnd *App = NULL;
	LMailStore *SrcStore = NULL;
	LList *Lst = NULL;
	ExportParams Params;
	Mail3Folders Dst;

	ScribeExportDlg(ScribeWnd *app, LMailStore *srcStore) :
		SrcStore(srcStore),
		Dst(app)
	{
		SetParent(App = app);

		if (!SrcStore)
			SrcStore = App->GetDefaultMailStore();

		if (LoadFromResource(IDD_SCRIBE_EXPORT))
		{
			MoveToCenter();
			// EnableCtrls(false);
			GetViewById(IDC_SRC_FOLDERS, Lst);

			LVariant s;
			if (Lst && App->GetOptions()->GetValue(OPT_ScribeExpSrcPaths, s) && s.Str())
			{
				Params.SrcPaths = LString(s.Str()).SplitDelimit(":");
				for (auto p: Params.SrcPaths)
					Lst->Insert(new LListItem(p));

				Lst->ResizeColumnsToContent();
			}

			if (App->GetOptions()->GetValue(OPT_ScribeExpDstPath, s) &&
				ValidStr(s.Str()))
				SetCtrlName(IDC_FOLDER, s.Str());
			else
				SetCtrlName(IDC_FOLDER, "/");

			LVariant n;
			if (App->GetOptions()->GetValue(OPT_ScribeExpAll, n))
				SetCtrlValue(IDC_ALL, n.CastInt32());

			if (App->GetOptions()->GetValue(OPT_ScribeExpExclude, n))
				SetCtrlValue(IDC_NO_SPAM_TRASH, n.CastInt32());
			else
				SetCtrlValue(IDC_NO_SPAM_TRASH, true);

			if (App->GetOptions()->GetValue(OPT_ScribeExpFolders, s) && s.Str())
				SetCtrlName(IDC_DEST, s.Str());

			OnAll();
		}
	}

	void OnNew(LDataFolderI *parent, LArray<LDataI*> &new_items, int pos, bool is_new)
	{
	}

	bool OnDelete(LDataFolderI *parent, LArray<LDataI*> &items)
	{
		return true;
	}
	
	bool OnMove(LDataFolderI *new_parent, LDataFolderI *old_parent, LArray<LDataI*> &items)
	{
		return true;
	}

	bool OnChange(LArray<LDataI*> &items, int FieldHint)
	{
		return true;
	}

	void OnAll()
	{
		SetCtrlEnabled(IDC_SRC_FOLDERS, !GetCtrlValue(IDC_ALL));
		SetCtrlEnabled(IDC_ADD_SRC_FOLDER, !GetCtrlValue(IDC_ALL));
		SetCtrlEnabled(IDC_DEL_SRC_FOLDER, !GetCtrlValue(IDC_ALL));
	}

	int OnNotify(LViewI *c, LNotification n)
	{
		switch (c->GetId())
		{
			case IDC_ALL:
			{
				OnAll();
				break;
			}
			case IDC_SET_DEST:
			{
				auto s = new LFileSelect(this);
				s->Type("Scribe Folders", "*.mail3");
				s->Type("All Files", LGI_ALL_FILES);
				s->Open([this](auto dlg, auto status)
				{
					if (status)
						SetCtrlName(IDC_DEST, dlg->Name());
					delete dlg;
				});
				break;
			}
			case IDC_ADD_SRC_FOLDER:
			{
				if (!Lst)
					break;

				auto s = new FolderDlg(this, App);
				s->DoModal([this, s](auto dlg, auto status)
				{
					if (status && ValidStr(s->Get()))
					{
						bool Has = false;

						for (auto n: *Lst)
						{
							if (Stricmp(n->GetText(), s->Get()) == 0)
							{
								Has = true;
								break;
							}
						}

						if (!Has)
						{
							Lst->Insert(new LListItem(s->Get()));
							Lst->ResizeColumnsToContent();
						}
					}

					delete dlg;
				});
				break;
			}
			case IDC_DEL_SRC_FOLDER:
			{
				if (Lst)
				{
					List<LListItem> i;
					if (Lst->GetSelection(i))
					{
						i.DeleteObjects();
					}
				}
				break;
			}
			case IDC_SET_FOLDER:
			{
				if (!Dst)
					Dst.LoadFolders(GetCtrlName(IDC_DEST));

				if (Dst.Root)
				{
					auto s = new FolderDlg(this, App, MAGIC_NONE, Dst.Root);
					s->DoModal([this, s](auto dlg, auto ctrlId)
					{
						if (ctrlId)
							SetCtrlName(IDC_FOLDER, s->Get());
						delete dlg;
					});
				}
				else LgiMsg(this, "Couldn't load mail3 store.", AppName);
				break;
			}
			case IDOK:
			{
				Params.AllFolders = GetCtrlValue(IDC_ALL) != 0;
				Params.ExceptTrashSpam = GetCtrlValue(IDC_NO_SPAM_TRASH) != 0;
				Params.DestPath = GetCtrlName(IDC_FOLDER);
				Params.DestFolders = GetCtrlName(IDC_DEST);

				if (Lst)
				{
					for (auto i: *Lst)
						Params.SrcPaths.Add(i->GetText());
				}

				if (!Dst)
					Dst.LoadFolders(Params.DestFolders);

				// fall through
			}
			case IDCANCEL:
			{
				LVariant v;
				if (Lst)
				{
					LStringPipe p;
					int n=0;
					for (auto i : *Lst)
					{
						p.Print("%s%s", n ? (char*)":" : (char*) "", i->GetText(0));
					}
					char *s = p.NewStr();
					if (s)
					{
						App->GetOptions()->SetValue(OPT_ScribeExpSrcPaths, v = s);
						DeleteArray(s);
					}
				}

				App->GetOptions()->SetValue(OPT_ScribeExpDstPath, v = GetCtrlName(IDC_FOLDER));
				App->GetOptions()->SetValue(OPT_ScribeExpAll, v = (int)GetCtrlValue(IDC_ALL));
				App->GetOptions()->SetValue(OPT_ScribeExpExclude, v = (int)GetCtrlValue(IDC_NO_SPAM_TRASH));
				App->GetOptions()->SetValue(OPT_ScribeExpFolders, v = GetCtrlName(IDC_DEST));

				EndModal(c->GetId() == IDOK);
			}
		}

		return 0;
	}
};

ScribeExportTask::ScribeExportTask(ScribeExportDlg *dlg) :
	FolderTask(dlg->Dst.Root, LAutoPtr<LStreamI>(NULL), NULL, NULL),
	Dst(dlg->Dst),
	SrcStore(dlg->SrcStore)
{
	SetDescription("Initializing...");
	SetType("Folders");

	LAssert(SrcStore);

	Params = dlg->Params;
	if (Params.ExceptTrashSpam)
	{
		Spam = App->GetFolder("/Spam");
		Trash = App->GetFolder(FOLDER_TRASH);
	}

	// Work out the folders we need to operate on:
	if (Params.AllFolders)
		CollectPaths(SrcStore->Root, InputPaths);
	else
		InputPaths = Params.SrcPaths;
	SetRange(InputPaths.Length());

	// Kick off the processing...
	State = ExpGetNext;
	SetPulse(PULSE_MS);
	SetAlwaysOnTop(true);
}

LString ScribeExportTask::ContactKey(Contact *c)
{
	LString p;
	const char *f = 0, *l = 0;
	auto e = c->GetAddrAt(0);
	c->Get(OPT_First, f);
	c->Get(OPT_Last, l);
	p.Printf("%s,%s,%s", e.Get(), f, l);
	return p;
}

#define ExportFolderStatus(b) \
	{ if (onStatus) onStatus(b); \
	return; }

/*
void ScribeExportTask::ExportFolder(LString ToPath,
									LString FromPath,
									bool Children,
									LProgressDlg *Prog,
									std::function<void(bool)> onStatus)
{
	if (!ToPath || !FromPath)
		ExportFolderStatus(false);

	ScribeFolder *From = App->GetFolder(FromPath);
	if (!From)
		ExportFolderStatus(false);

	if (!((!Spam  || From != Spam) &&
		(!Trash || From != Trash)))
		ExportFolderStatus(false);

	ScribeFolder *To = GetFolder(ToPath, From);
	if (!To)
		ExportFolderStatus(false);

	bool FromLoaded = From->IsLoaded();
	bool ToLoaded = From->IsLoaded();

	auto ProcessItem = [this, To, Prog, FromPath, From, FromLoaded, ToLoaded, Children, ToPath, onStatus]()
	{
		bool Status = true;


		if (!FromLoaded)
		{
			From->UnloadThings();
		}
		if (!ToLoaded)
		{
			To->UnloadThings();
		}

		if (Children)
		{
			char t[256];
			char f[256];
			LString n;

			for (ScribeFolder *c = From->GetChildFolder(); c && (!Prog || !Prog->IsCancelled()); c = c->GetNextFolder())
			{
				n = c->GetName(true);
				if (n)
				{
					strcpy_s(t, sizeof(t), ToPath);
					char *e = t + strlen(t) - 1;
					if (*e++ != '/') *e++ = '/';
					strcpy_s(e, sizeof(t)-(e-t), n);

					strcpy_s(f, sizeof(f), FromPath);
					e = f + strlen(f) - 1;
					if (*e++ != '/') *e++ = '/';
					strcpy_s(e, sizeof(f)-(e-f), n);
									
					ExportFolder(t, f, true, Prog, [](auto ok){});
				}
			}
		}
	};

	To->LoadThings(NULL, [From, ProcessItem](auto status)
	{
		From->LoadThings(NULL, [ProcessItem](auto status)
		{
			ProcessItem();
		});
	});
}

bool ScribeExportTask::ExportThing()
{
	switch ((uint32_t)SrcFolder->GetItemType())
	{
		case MAGIC_MAIL:
		{
			int InitMailErrors = MailErrors;
								
			for (auto t: From->Items)
			{
				if (Prog && Prog->IsCancelled())
					break;
									
				Mail *m = t->IsMail();
				if (m)
				{
					auto Id = m->GetMessageId(true);
					if (Id)
					{
						if (!ToMsgs.Find(Id))
						{
							// Create new mail...
							Mail *n = new Mail(App);
							if (n)
							{
								*n = (Thing&)*m;
								n->SetParentFolder(To);
								n->SetObject(To->GetObject()->GetStore()->Create(MAGIC_MAIL), false, _FL);
								if (n->GetObject())
								{
									MailCreated++;

									// Now create all the attachments
									List<Attachment> Att;
									if (m->GetAttachments(&Att))
									{
										for (auto OldAttachment: Att)
										{
											Attachment *NewAttachment = new Attachment(m->App, OldAttachment);
											if (NewAttachment)
											{
												n->AttachFile(NewAttachment);
												NewAttachment->SetObject(n->GetObject()->GetStore()->Create(MAGIC_ATTACHMENT), false, _FL);
											}
										}
									}
								}
								else MailErrors++;

							}
							else MailErrors++;
						}
						else MailSkipped++;
					}
					else MailErrors++;
				}

				if (Prog)
					Prog->Value(Prog->Value() + 1);
			}

			Status |= MailErrors == InitMailErrors;
			break;
		}
		case MAGIC_CONTACT:
		{
			LHashTbl<StrKey<char>,Contact*> ToContacts;
			for (auto t: To->Items)
			{
				Contact *c = t->IsContact();
				if (c)
				{
					auto k = ContactKey(c);
					if (k)
						ToContacts.Add(k, c);
				}
			}

			int InitContactErrors = ContactErrors;
			uint64 Last = LCurrentTime();
			for (auto t: From->Items)
			{
				if (Prog && Prog->IsCancelled())
					break;
										
				Contact *c = t->IsContact();
				if (c)
				{
					auto k = ContactKey(c);
					if (k)
					{
						if (!ToContacts.Find(k))
						{
							Contact *n = new Contact(App);
							if (n)
							{
								*n = (Thing&)*c;
								n->SetParentFolder(To);
							}
							else ContactErrors++;
						}
						else ContactSkipped++;
					}
					else ContactErrors++;
				}

				if (Prog)
					Prog->Value(Prog->Value() + 1);
			}

			Status |= ContactErrors == InitContactErrors;
			break;
		}
}
*/

bool ScribeExportTask::TimeSlice()
{
	if (IsCancelled())
		return false;

	switch (State)
	{
		case ExpGetNext:
		{
			if (InputPaths.Length() == 0)
			{
				State = ExpFinished;
				break;
			}

			// Load up the next SrcFolder...
			auto src = InputPaths[0];
			InputPaths.DeleteAt(0, true);

			auto dst = MakePath(src);
			SrcFolder = App->GetFolder(src, SrcStore);
			if (!SrcFolder)
			{
				FolderLoadErrors++;
				return true;
			}

			DstStore = NULL;
			DstFolder = Dst.GetFolder(dst, SrcFolder->GetItemType());
			if (!DstFolder)
			{
				return true;
			}

			State = ExpLoadFolders;
			SrcFolder->LoadThings(this, [this](auto status)
			{
				if (status == Store3Success)
				{
					// Load a list of things to process...
					SrcItems.Empty();
					auto SrcObj = dynamic_cast<LDataFolderI*>(SrcFolder->GetObject());
					if (SrcObj)
					{
						auto &c = SrcObj->Children();
						for (auto t = c.First(); t; t = c.Next())
							SrcItems.Add(t);
					}

					DstFolder->LoadThings(this, [this](auto status)
					{
						if (status == Store3Success)
						{
							State = ExpItems;
							SetDescription(DstFolder->GetPath());

							if (DstFolder->GetObject())
								DstObj = dynamic_cast<LDataFolderI*>(DstFolder->GetObject());
							else
								LAssert(!"No object?");

							if (DstObj)
								DstStore = DstObj->GetStore();
							else
								LAssert(!"No object?");

							DstMsgIds.Empty();
							if (DstFolder->GetItemType() == MAGIC_MAIL)
							{
								// Make a map of destination folder message IDs
								if (DstObj)
								{
									auto &c = DstObj->Children();
									for (auto t = c.First(); t; t = c.Next())
									{
										if (t->Type() != MAGIC_MAIL)
											continue;

										auto Id = t->GetStr(FIELD_MESSAGE_ID);
										if (Id)
											DstMsgIds.Add(Id, t);
									}
								}
							}
						}
						else
							State = ExpGetNext;
					});
				}
				else
				{
					State = ExpGetNext;
				}					
			});
			break;
		}
		case ExpLoadFolders:
		{
			// No-op, but we should probably time out...
			break;
		}
		case ExpItems:
		{
			if (!SrcFolder || !DstFolder || !DstStore)
			{
				State = ExpGetNext;
				break;
			}

			auto Trans = DstStore->StartTransaction();

			auto StartTs = LCurrentTime();
			int Processed = 0;
			while (	SrcItems.Length() > 0 &&
					LCurrentTime() - StartTs < WORK_SLICE_MS)
			{
				auto in = SrcItems[0];
				SrcItems.DeleteAt(0);

				switch (in->Type())
				{
					case MAGIC_MAIL:
					{
						auto Id = in->GetStr(FIELD_MESSAGE_ID);
						if (!Id)
						{
							MailErrors++;
							break;
						}

						if (DstMsgIds.Find(Id))
						{
							MailSkipped++;
							break;
						}

						// Create new mail...
						auto outMail = DstStore->Create(MAGIC_MAIL);
						outMail->CopyProps(*in);

						// Now create all the attachments
						auto inSeg = dynamic_cast<LDataI*>(in->GetObj(FIELD_MIME_SEG));
						LDataI *outSeg = NULL;
						if (inSeg)
						{
							outSeg = DstStore->Create(MAGIC_ATTACHMENT);
							if (!outSeg)
								MailErrors++;
							else
							{
								outSeg->CopyProps(*inSeg);

								auto outMime = outSeg->GetStr(FIELD_MIME_TYPE);
								if (!outMime)
								{
									auto hdrs = outSeg->GetStr(FIELD_INTERNET_HEADER);
									if (!hdrs)
									{
										// This is going to cause an assert later
										outSeg->SetStr(FIELD_MIME_TYPE, sAppOctetStream);
										LgiTrace("%s:%i - Setting default mime on %p\n", _FL, outSeg);
									}
								}

								LgiTrace("%s:%i - Setting root seg: %p\n", _FL, outSeg);
								if (outMail->SetObj(FIELD_MIME_SEG, outSeg) < Store3Delayed)
									MailErrors++;
								else
								{
									LString err;
									if (!CopyAttachments(outMail, outSeg, inSeg, err))
										MailErrors++;
									else
										MailCreated++;
								}
							}
						}
						else MailCreated++;

						outMail->Save(DstFolder->GetObject());
						break;
					}
					default:
					{
						LgiTrace("%s:%i - Unhandled object type.\n", _FL);
						break;
					}
				}

				Processed++;

			}

			if (SrcItems.Length() == 0)
			{
				SrcFolder = NULL;
				DstFolder = NULL;
				DstStore = NULL;
				State = ExpGetNext;
				(*this)++; // move progress...
			}

			LgiTrace("Processed: %i\n", Processed);
			break;
		}
		case ExpFinished:
		{
			return false;
		}
	}

	return true;
}

// This should copy all the child objects of 'inSeg' to new child objects of 'outSeg'
bool ScribeExportTask::CopyAttachments(LDataI *outMail, LDataPropI *outSeg, LDataPropI *inSeg, LString &err)
{
	#define ERR(str) \
		{ err = str; return false; }
	if (!outMail || !outSeg || !inSeg)
		ERR("param error");

	auto children = inSeg->GetList(FIELD_MIME_SEG);
	if (!children)
		return true; // Nothing to copy...

	for (auto i = children->First(); i; i = children->Next())
	{
		auto inMime = i->GetStr(FIELD_MIME_TYPE);
		if (!inMime)
			continue;

		auto o = outMail->GetStore()->Create(MAGIC_ATTACHMENT);
		if (!o)
			ERR("couldn't create attachment");

		if (!o->CopyProps(*i))
			ERR("copy attachment properties failed");

		auto outData = dynamic_cast<LDataI*>(outSeg);
		if (!outData)
			ERR("outSeg isn't a LDataI object");

		if (!o->Save(outData))
			ERR("failed to save attachment to output mail");

		if (!CopyAttachments(outMail, o, i, err))
			return false; // but leave the error message untouched.
	}

	return true;
}

void ExportScribe(ScribeWnd *App, LMailStore *Store)
{
	auto Dlg = new ScribeExportDlg(App, Store);
	Dlg->DoModal([Dlg, App](auto dlg, auto ctrlId)
	{
		if (ctrlId && Dlg->Dst)
		{
			new ScribeExportTask(Dlg);
		}
		delete dlg;
	});
}
