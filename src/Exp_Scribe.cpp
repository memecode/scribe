
#include "Scribe.h"

#include "lgi/common/List.h"
#include "lgi/common/ProgressDlg.h"
#include "lgi/common/Store3.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/FileSelect.h"

#include "ScribeFolderSelect.h"
#include "resdefs.h"
#include "FolderTask.h"

#define OnError(...) \
{ \
	Errors.Add(in->Type(), Errors.Find(in->Type()) + 1); \
	LgiTrace(__VA_ARGS__); \
	break; \
}
#define OnSkip() \
{ \
	Skipped.Add(in->Type(), Skipped.Find(in->Type()) + 1); \
	break; \
}
#define OnCreate() \
{ \
	Created.Add(in->Type(), Created.Find(in->Type()) + 1); \
}

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

	bool CheckDirty(ScribeFolder *f)
	{
		if (f->GetDirty())
			return true;
		for (auto c = f->GetChildFolder(); c; c = c->GetNextFolder())
			if (CheckDirty(c))
				return true;
		return false;
	}

	bool IsDirty()
	{
		if (!Root)
			return false;
		return CheckDirty(Root);
	}
};

struct ScribeExportTask : public FolderTask
{
	int FolderLoadErrors = 0;

	LHashTbl<IntKey<int>,int> Created, Errors, Skipped;

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
		ExpCleanup
	}	State = ExpNone;

	LString::Array InputPaths;
	ScribeFolder *SrcFolder = NULL;
	LArray<LDataI*> SrcItems;
	ScribeFolder *DstFolder = NULL;
	LDataFolderI *DstObj = NULL;
	LDataStoreI *DstStore = NULL;
	LHashTbl<ConstStrKey<char>,LDataI*> DstObjMap;

	ScribeExportTask(struct ScribeExportDlg *dlg);

	LString ContactKey(Contact *c);	
	bool TimeSlice();
	bool CopyAttachments(LDataI *outMail, LDataPropI *outSeg, LDataPropI *inSeg, LString &err);

	LString MakePath(LString path)
	{
		LString sep = "/";
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

	ScribeFolder *GetFolder(LString Path, Store3ItemTypes CreateItemType = MAGIC_NONE)
	{
		if (!Dst.Root)
		{
			LAssert(!"No root loaded.");
			return NULL;
		}

		Dst.Root->LoadFolders();

		bool Create = false;
		auto parts = Path.SplitDelimit("/");
		ScribeFolder *f = Dst.Root;
		for (auto p: parts)
		{
			auto c = f->GetSubFolder(p);
			if (!c)
			{
				if (CreateItemType != MAGIC_NONE)
				{
					c = f->CreateSubFolder(p, CreateItemType);
					if (!c)
					{
						Errors.Add(MAGIC_FOLDER, Errors.Find(MAGIC_FOLDER)+1);
						return NULL;
					}

					Create = true;
				}
				else return NULL;
			}

			f = c;
		}

		if (Create)
			Created.Add(MAGIC_FOLDER, Created.Find(MAGIC_FOLDER)+1);
		else
			Skipped.Add(MAGIC_FOLDER, Skipped.Find(MAGIC_FOLDER)+1);

		return f;
	}

	void OnComplete()
	{
		Store3ItemTypes types[] = { MAGIC_FOLDER, MAGIC_MAIL, MAGIC_CONTACT, MAGIC_CALENDAR, MAGIC_GROUP, MAGIC_FILTER };
		LStringPipe html;

		html.Print("<style> td{background:ThreeDFace; padding:3px;}</style>\n"
					"<body style='background:ThreeDFace;'><div>Mail export complete.</div>\n"
					"<br>\n"
					"<table style='border-spacing: 1px; background:#aaa;'>\n"
					"<tr><th>Type <th>Created <th>Errors <th>Skipped </tr>\n");
		for (int i=0; i<CountOf(types); i++)
		{
			auto type    = types[i];
			auto name    = LString(Store3ItemTypeName(type)).Replace("MAGIC_");
			StrLwr(name.Get() + 1);
			auto created = Created.Find(type);
			auto errors  = Errors.Find(type);
			auto skipped = Skipped.Find(type);
			auto errStyle = errors ? " style='color:red'" : "";

			html.Print("<tr><td>%s <td>%i <td%s>%i <td>%i </tr>\n",
				name.Get(), created, errStyle, errors, skipped);
		}

		html.Print("</table>\n"
					"<br>\n"
					"<b>Created:</b> new item created in destination store.<br>\n"
					"<b>Error:</b> there was an error replicating item.<br>\n"
					"<b>Skipped:</b> the item already existed in the destination store.<br>\n"
					);

		LHtmlMsg(NULL, App, html.NewLStr(), "Export", MB_OK);
	}

	LString GetOrCreateMessageId(LDataI &obj)
	{
		auto msgId = obj.GetStr(FIELD_MESSAGE_ID);
		if (msgId)
			return msgId;

		// Check the headers:
		auto hdrs = obj.GetStr(FIELD_INTERNET_HEADER);
		if (hdrs)
		{
			LAutoString Header(InetGetHeaderField(hdrs, "Message-ID"));
			if (Header)
			{
				auto ids = ParseIdList(Header);
				auto id = ids[0];
				obj.SetStr(FIELD_MESSAGE_ID, id);
				obj.Save();
				return id;
			}
		}

		// Msg has no ID and no header... create one.
		auto from = obj.GetObj(FIELD_FROM);
		if (!from)
		{
			LgiTrace("%s:%i - No from for email: %p\n", _FL, &obj);
			return LString();
		}

		auto fromEmail = from->GetStr(FIELD_EMAIL);
		LVariant Email;
		const char *At = fromEmail ? strchr(fromEmail, '@') : NULL;
		if (!At)
		{
			if (App->GetOptions()->GetValue(OPT_Email, Email) && Email.Str())
				At = strchr(Email.Str(), '@');
			else
				At = "@domain.com";
		}
		if (!At)
		{
			LgiTrace("%s:%i - No at in email: %p\n", _FL, &obj);
			return LString();
		}

		char m[96], a[32], b[32];
		Base36(a, LCurrentTime());
		Base36(b, LRand(RAND_MAX));
		sprintf_s(m, sizeof(m), "<%s.%i%s%s>", a, LRand(RAND_MAX), b, At);
		obj.SetStr(FIELD_MESSAGE_ID, m);
		obj.Save();
		return m;
	}

	LString ObjToId(LDataI &obj)
	{
		switch (obj.Type())
		{
			case MAGIC_MAIL:
			{
				return obj.GetStr(FIELD_MESSAGE_ID);
			}
			case MAGIC_CONTACT:
			{
				auto fn = obj.GetStr(FIELD_FIRST_NAME);
				auto ln = obj.GetStr(FIELD_LAST_NAME);
				auto em = obj.GetStr(FIELD_EMAIL);
				LString s;
				s.Printf("%s,%s,%s", fn, ln, em);
				return s;
			}
			case MAGIC_CALENDAR:
			{
				auto sub = obj.GetStr(FIELD_CAL_SUBJECT);
				auto start = obj.GetDate(FIELD_CAL_START_UTC);
				LString s;
				s.Printf("%s," LPrintfInt64, sub, start?start->Ts().Get():0);
				return s;
				break;
			}
			case MAGIC_GROUP:
			{
				return obj.GetStr(FIELD_GROUP_NAME);
			}
			case MAGIC_FILTER:
			{
				return obj.GetStr(FIELD_FILTER_NAME);
			}
			default:
			{
				LAssert(!"Impl me.");
				break;
			}
		}

		return LString();
	}

	bool CheckModified(LDataI *in, LDataI *out)
	{
		if (!out)
			// No existing object
			return true;

		auto inMod = in->GetDate(FIELD_DATE_MODIFIED);
		auto outMod = in->GetDate(FIELD_DATE_MODIFIED);
		if (!inMod ||
			!outMod ||
			!inMod->IsValid() ||
			!outMod->IsValid())
			return true; // Can't tell... no dates stored.
	
		bool mod = *inMod > *outMod;

		return mod;
	}

	void MakeDstObjMap()
	{
		DstObjMap.Empty();

		if (!DstFolder || !DstObj)
			return;

		auto &c = DstObj->Children();
		for (auto t = c.First(); t; t = c.Next())
		{
			auto Id = ObjToId(*t);
			if (Id)
				DstObjMap.Add(Id, t);
		}
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
					Params.SrcPaths.Empty();
					Params.SrcPaths.SetFixedLength(false);
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
				App->GetOptions()->SetValue(OPT_ScribeExpAll,     v = (int)GetCtrlValue(IDC_ALL));
				App->GetOptions()->SetValue(OPT_ScribeExpExclude, v = (int)GetCtrlValue(IDC_NO_SPAM_TRASH));
				App->GetOptions()->SetValue(OPT_ScribeExpFolders, v = GetCtrlName(IDC_DEST));

				EndModal(c->GetId() == IDOK);
			}
		}

		return 0;
	}
};

ScribeExportTask::ScribeExportTask(ScribeExportDlg *dlg) :
	FolderTask(dlg->Dst.Root, LAutoPtr<LStreamI>(NULL), LString(), NULL),
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
			DstFolder = GetFolder(dst, SrcFolder->GetItemType());
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
							SetDescription(SrcFolder->GetPath());

							if (DstFolder->GetObject())
								DstObj = dynamic_cast<LDataFolderI*>(DstFolder->GetObject());
							else
								LAssert(!"No object?");

							if (DstObj)
								DstStore = DstObj->GetStore();
							else
								LAssert(!"No object?");

							MakeDstObjMap();
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
						auto Id = GetOrCreateMessageId(*in);
						if (!Id)
							OnError("%s:%i - Email %p has no MsgId\n", _FL, in)

						if (DstObjMap.Find(Id))
							OnSkip()

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
								OnError("%s:%i - Failed to create attachment\n", _FL)
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
										// LgiTrace("%s:%i - Setting default mime on %p\n", _FL, outSeg);
									}
								}

								if (outMail->SetObj(FIELD_MIME_SEG, outSeg) < Store3Delayed)
									OnError("%s:%i - Failed to attach seg to mail.\n", _FL)
								else
								{
									LString err;
									if (!CopyAttachments(outMail, outSeg, inSeg, err))
										OnError("%s:%i - CopyAttachments failed\n", _FL)
									else
										OnCreate()
								}
							}
						}
						else
							OnCreate()

						outMail->Save(DstFolder->GetObject());
						break;
					}
					default:
					{
						// Is the object already in the dst map?
						auto Id = ObjToId(*in);
						auto existing = DstObjMap.Find(Id);
						if (!CheckModified(in, existing))
							OnSkip();

						auto outObj = DstStore->Create(in->Type());
						if (!outObj)
						{
							OnError("%s:%i - %s failed to create %s\n", _FL,
								DstStore->GetStr(FIELD_STORE_TYPE),
								Store3ItemTypeName((Store3ItemTypes)in->Type()))
						}

						if (!outObj->CopyProps(*in))
							OnError("%s:%i - CopyProps failed.\n", _FL)

						if (outObj->Save(DstFolder->GetObject()) < Store3Delayed)
							OnError("%s:%i - Save failed\n", _FL)

						OnCreate();
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

			// LgiTrace("Processed: %i\n", Processed);
			break;
		}
		case ExpFinished:
		{
			OnComplete();
			State = ExpCleanup;
			return true;
		}
		case ExpCleanup:
		{
			if (Dst.IsDirty())
				return true;

			// We're done...
			return false;
		}
		default:
		{
			LAssert(!"Not impl.");
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
