
#include "Scribe.h"

#include "lgi/common/List.h"
#include "lgi/common/ProgressDlg.h"
#include "lgi/common/Store3.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/FileSelect.h"

#include "ScribeFolderSelect.h"
#include "../Resources/resdefs.h"

class ScribeExport : public LDialog, public LDataEventsI
{
	ScribeWnd *App = NULL;
	LAutoPtr<LDataStoreI> Folders;
	ScribeFolder *Mailbox = NULL;
	LList *Lst = NULL;
	ScribeFolder *Spam = NULL;
	ScribeFolder *Trash = NULL;

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

public:
	bool AllFolders = false;
	bool ExceptTrashSpam = false;
	LString DestPath;
	LString::Array SrcPaths;

	int MailCreated = 0;
	int MailSkipped = 0;
	int MailErrors = 0;
	int ContactCreated = 0;
	int ContactSkipped = 0;
	int ContactErrors = 0;

	ScribeExport(ScribeWnd *app)
	{
		SetParent(App = app);
		if (LoadFromResource(IDD_SCRIBE_EXPORT))
		{
			MoveToCenter();
			// EnableCtrls(false);
			GetViewById(IDC_SRC_FOLDERS, Lst);

			LVariant s;
			if (Lst && App->GetOptions()->GetValue(OPT_ScribeExpSrcPaths, s) && s.Str())
			{
				SrcPaths = LString(s.Str()).SplitDelimit(":");
				for (auto p: SrcPaths)
					Lst->Insert(new LListItem(p));

				Lst->ResizeColumnsToContent();
			}

			if (App->GetOptions()->GetValue(OPT_ScribeExpDstPath, s) &&
				ValidStr(s.Str()))
			{
				SetCtrlName(IDC_FOLDER, s.Str());
			}
			else
			{
				SetCtrlName(IDC_FOLDER, "/");
			}

			LVariant n;
			if (App->GetOptions()->GetValue(OPT_ScribeExpAll, n))
			{
				SetCtrlValue(IDC_ALL, n.CastInt32());
			}

			if (App->GetOptions()->GetValue(OPT_ScribeExpExclude, n))
			{
				SetCtrlValue(IDC_NO_SPAM_TRASH, n.CastInt32());
			}
			else
			{
				SetCtrlValue(IDC_NO_SPAM_TRASH, true);
			}

			if (App->GetOptions()->GetValue(OPT_ScribeExpFolders, s) && s.Str())
			{
				SetCtrlName(IDC_DEST, s.Str());
				LoadFolders();
			}

			OnAll();
		}
	}

	~ScribeExport()
	{
		UnloadFolders();
		SrcPaths.DeleteArrays();
	}

	void OnAll()
	{
		SetCtrlEnabled(IDC_SRC_FOLDERS, !GetCtrlValue(IDC_ALL));
		SetCtrlEnabled(IDC_ADD_SRC_FOLDER, !GetCtrlValue(IDC_ALL));
		SetCtrlEnabled(IDC_DEL_SRC_FOLDER, !GetCtrlValue(IDC_ALL));
	}

	/*
	void EnableCtrls(bool e)
	{
		SetCtrlEnabled(IDC_DEST, !e);
		SetCtrlEnabled(IDC_ALL, e);
		SetCtrlEnabled(IDC_NO_SPAM_TRASH, e);
		SetCtrlEnabled(IDC_SRC_FOLDERS, e);
		SetCtrlEnabled(IDC_ADD_SRC_FOLDER, e);
		SetCtrlEnabled(IDC_DEL_SRC_FOLDER, e);
		SetCtrlEnabled(IDC_FOLDER, e);
		SetCtrlEnabled(IDC_SET_FOLDER, e);
		SetCtrlEnabled(IDOK, e);
	}
	*/

	void UnloadFolders()
	{
		DeleteObj(Mailbox);
		Folders.Reset();
	}

	void LoadFolders()
	{
		if (!Folders)
		{
			auto path = GetCtrlName(IDC_DEST);
			Folders.Reset(App->CreateDataStore(path, true));
		}

		if (Folders)
		{
			Mailbox = new ScribeFolder;
			if (Mailbox)
			{
				Mailbox->App = App;
				Mailbox->SetObject(Folders->GetRoot(), false, _FL);
			}
		}
	}

	ScribeFolder *GetFolder(char *Path, ScribeFolder *CreateAs = 0)
	{
		ScribeFolder *f = 0;

		if (Path && Mailbox)
		{
			LToken t(Path, "/");
			f = Mailbox;
			for (unsigned i=0; i<t.Length(); i++)
			{
				bool Found = false;
				for (ScribeFolder *Child = f->GetChildFolder(); Child; Child = Child->GetNextFolder())
				{
					auto n = Child->GetName(true);
					if (n.Equals(t[i]))
					{
						f = Child;
						Found = true;
						break;
					}
				}

				if (!Found)
				{
					if (CreateAs)
					{
						f = f->CreateSubDirectory(t[i], CreateAs->GetItemType());
						if (!f)
							break;
					}
					else
					{
						f = 0;
						break;
					}
				}
			}
		}

		return f;
	}

	LString ContactKey(Contact *c)
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

	void ExportFolder(	LString ToPath,
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

			switch ((uint32_t)To->GetItemType())
			{
				case MAGIC_MAIL:
				{
					if (Prog)
						Prog->SetDescription(FromPath);

					LHashTbl<ConstStrKey<char>,Mail*> ToMsgs;
					for (auto t: To->Items)
					{
						Mail *m = t->IsMail();
						if (m)
						{
							auto Id = m->GetMessageId(true);
							if (Id)
							{
								ToMsgs.Add(Id, m);
							}
						}
					}

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
					{
						UnloadFolders();
						SetCtrlName(IDC_DEST, dlg->Name());
						LoadFolders();
					}
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

						for (auto n : *Lst)
						{
							const char *p = n->GetText(0);
							if (p && _stricmp(p, s->Get()) == 0)
							{
								Has = true;
								break;
							}
						}

						if (!Has)
						{
							LListItem *i = new LListItem;
							if (i)
							{
								i->SetText(s->Get());
								Lst->Insert(i);
								Lst->ResizeColumnsToContent();
							}
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
				LoadFolders();
				if (!Mailbox)
					break;

				auto s = new FolderDlg(this, App, MAGIC_NONE, Mailbox);
				s->DoModal([this, s](auto dlg, auto ctrlId)
				{
					if (ctrlId)
						SetCtrlName(IDC_FOLDER, s->Get());
					delete dlg;
				});
				break;
			}
			case IDOK:
			{
				LoadFolders();

				AllFolders = GetCtrlValue(IDC_ALL) != 0;
				if ((ExceptTrashSpam = GetCtrlValue(IDC_NO_SPAM_TRASH) != 0))
				{
					Spam = App->GetFolder("/Spam");
					Trash = App->GetFolder(FOLDER_TRASH);
				}

				DestPath = GetCtrlName(IDC_FOLDER);
				if (Lst)
				{
					for (auto i : *Lst)
					{
						SrcPaths.Add(NewStr(i->GetText(0)));
					}
				}

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

	int CountItems(ScribeFolder *f, bool Children)
	{
		int Status = 0;
		/*
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
		*/
		return Status;
	}
};

void ExportScribe(ScribeWnd *App)
{
	auto Dlg = new ScribeExport(App);
	Dlg->DoModal([Dlg, App](auto dlg, auto ctrlId)
	{
		if (ctrlId)
		{
			{
				LProgressDlg Prog(App);
				Prog.SetDescription("Initializing...");
				Prog.SetType("items");

				auto Ms = App->GetDefaultMailStore();
				if (!Ms)
					return;

				int Items = 0;
				if (Dlg->AllFolders)
				{
					Items += Dlg->CountItems(Ms->Root, true);
				}
				else
				{
					for (auto path: Dlg->SrcPaths)
						Items += Dlg->CountItems(App->GetFolder(path), false);
				}
				Prog.SetRange(Items);

				if (Dlg->AllFolders)
				{
					Dlg->ExportFolder(Dlg->DestPath, "/", true, &Prog, NULL);
				}
				else
				{
					for (unsigned i=0; i<Dlg->SrcPaths.Length() && !Prog.IsCancelled(); i++)
					{
						char Dest[256];
						strcpy_s(Dest, sizeof(Dest), Dlg->DestPath);
						char *e = Dest + strlen(Dest) - 1;
						if (*e == '/') *e = 0;
						strcat(Dest, Dlg->SrcPaths[i]);

						Dlg->ExportFolder(Dest, Dlg->SrcPaths[i], false, &Prog, NULL);
					}
				}
			}

			LgiMsg(	App,
					"Mail export complete.\n"
					"\n"
					"    Email: %i created, %i already exist, %i errors\n"
					"    Contacts: %i created, %i already exist, %i errors",
					"Export",
					MB_OK,
					Dlg->MailCreated,
					Dlg->MailSkipped,
					Dlg->MailErrors,
					Dlg->ContactCreated,
					Dlg->ContactSkipped,
					Dlg->ContactErrors
					);
		}
		delete dlg;
	});
}
