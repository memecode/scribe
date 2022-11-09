#include "Scribe.h"
#include "lgi/common/List.h"
#include "ScribeFolderSelect.h"
#include "lgi/common/ProgressDlg.h"
#include "../Resources/resdefs.h"
#include "lgi/common/Store3.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/FileSelect.h"

/*
#define OPT_ScribeExpSrcPaths		"ExpSrcPaths"		//(char*)
#define OPT_ScribeExpDstPath		"ExpDstPath"		//(char*)
#define OPT_ScribeExpFolders		"ExpFlds"			//(char*)
#define OPT_ScribeExpAll			"ExpAll"			//(bool)
#define OPT_ScribeExpExclude		"ExpExc"			//(bool)
*/

class ScribeExport : public LDialog, public LDataEventsI
{
	ScribeWnd *App;
	LDataStoreI *Folders;
	ScribeFolder *Mailbox;
	LList *Lst;
	ScribeFolder *Spam;
	ScribeFolder *Trash;

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
	bool AllFolders;
	bool ExceptTrashSpam;
	char *DestPath;
	LArray<char*> SrcPaths;

	int MailCreated;
	int MailSkipped;
	int MailErrors;
	int ContactCreated;
	int ContactSkipped;
	int ContactErrors;

	ScribeExport(ScribeWnd *app)
	{
		Folders = 0;
		Lst = 0;
		Mailbox = 0;
		DestPath = 0;
		MailCreated = 0;
		MailSkipped = 0;
		MailErrors = 0;
		ContactCreated = 0;
		ContactSkipped = 0;
		ContactErrors = 0;
		Spam = 0;
		Trash = 0;

		SetParent(App = app);
		if (LoadFromResource(IDD_SCRIBE_EXPORT))
		{
			MoveToCenter();
			EnableCtrls(false);
			GetViewById(IDC_SRC_FOLDERS, Lst);

			LVariant s;
			if (Lst && App->GetOptions()->GetValue(OPT_ScribeExpSrcPaths, s) && s.Str())
			{
				LToken t(s.Str(), ":");
				for (unsigned i=0; Lst && i<t.Length(); i++)
				{
					LListItem *n = new LListItem;
					if (n)
					{
						n->SetText(t[i]);
						Lst->Insert(n);
					}
				}

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
				OnSelectFolders();
			}

			OnAll();
		}
	}

	~ScribeExport()
	{
		DeleteObj(Folders);
		DeleteArray(DestPath);
		SrcPaths.DeleteArrays();
	}

	void OnAll()
	{
		SetCtrlEnabled(IDC_SRC_FOLDERS, !GetCtrlValue(IDC_ALL));
		SetCtrlEnabled(IDC_ADD_SRC_FOLDER, !GetCtrlValue(IDC_ALL));
		SetCtrlEnabled(IDC_DEL_SRC_FOLDER, !GetCtrlValue(IDC_ALL));
	}

	void OnSelectFolders()
	{
		const char *s = GetCtrlName(IDC_DEST);
		if (LFileExists(s))
		{
			for (unsigned i=0; i<App->GetStorageFolders().Length(); i++)
			{
				/* FIXME
				char *f = App->GetStorageFolders()->GetFileName();
				if (f)
				{
					if (_stricmp(f, s) == 0)
					{
						LgiMsg(this, LLoadString(IDS_ERROR_CANT_OPEN_FOLDERS), AppName, MB_OK, s);
						EnableCtrls(false);
						return;
					}
				}
				*/
			}

			DeleteObj(Folders);
			Folders = OpenMail3(s, this, false);
			if (Folders && Folders->GetInt(FIELD_STATUS) == Store3Success)
			{
				EnableCtrls(true);
			}
			else
			{
				LgiMsg(this, LLoadString(IDS_ERROR_CANT_OPEN_FOLDERS), AppName, MB_OK, s);
				EnableCtrls(false);
			}
		}
	}

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

	void LoadFolders()
	{
		if (!Mailbox)
		{
			// StorageItem *Root = Folders->GetRoot();
			LDataFolderI *Root = Folders->GetRoot();
			if (Root)
			{
				/*
				Root->SetObject(Mailbox = new ScribeFolder("MailBox", MAGIC_NONE));
				if (Mailbox)
				{
					Mailbox->Store = Root;
					Mailbox->LoadFolders();
				}
				*/
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

	bool ExportFolder(char *ToPath, const char *FromPath, bool Children, LProgressDlg *Prog)
	{
		bool Status = false;

		if (ToPath && FromPath)
		{
			ScribeFolder *From = App->GetFolder(FromPath);
			if (From)
			{
				if ((!Spam || From != Spam) &&
					(!Trash || From != Trash))
				{
					ScribeFolder *To = GetFolder(ToPath, From);
					if (To)
					{
						bool FromLoaded = From->IsLoaded();
						bool ToLoaded = From->IsLoaded();

						switch ((uint32_t)To->GetItemType())
						{
							case MAGIC_MAIL:
							{
								if (Prog)
									Prog->SetDescription(FromPath);

								To->LoadThings();
								From->LoadThings();

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
								uint64 Last = LCurrentTime();
								
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
													To->Items.Insert(n);

													n->SetObject(To->GetObject()->GetStore()->Create(MAGIC_MAIL), _FL);
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
																	NewAttachment->SetObject(n->GetObject()->GetStore()->Create(MAGIC_ATTACHMENT), _FL);
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
									{
										Prog->Value(Prog->Value() + 1);
										uint64 Now = LCurrentTime();
										if (Now > Last + 300)
										{
											LYield();
											Last = Now;
										}
									}
								}

								Status |= MailErrors == InitMailErrors;
								break;
							}
							case MAGIC_CONTACT:
							{
								// bool FromLoaded = From->IsLoaded();
								To->LoadThings();
								From->LoadThings();

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
													To->Items.Insert(n);

													/*
													n->Store = To->Store->CreateSub(n);
													if (n->Store)
													{
														n->Store->Object = n;
														ContactCreated++;
													}
													else ContactErrors++;
													*/
												}
												else ContactErrors++;
											}
											else ContactSkipped++;
										}
										else ContactErrors++;
									}

									if (Prog)
									{
										Prog->Value(Prog->Value() + 1);
										uint64 Now = LCurrentTime();
										if (Now > Last + 300)
										{
											LYield();
											Last = Now;
										}
									}
								}

								Status |= ContactErrors == InitContactErrors;
								break;
							}
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
									
									ExportFolder(t, f, true, Prog);
								}
							}
						}
					}
				}
			}
		}

		return Status;
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
				LFileSelect s;
				s.Parent(this);
				s.Type("Scribe Folders", "*.mail3");
				s.Type("All Files", LGI_ALL_FILES);
				if (s.Open())
				{
					DeleteObj(Folders);
					EnableCtrls(false);
					SetCtrlName(IDC_DEST, s.Name());
					OnSelectFolders();
				}
				break;
			}
			case IDC_ADD_SRC_FOLDER:
			{
				if (Lst)
				{
					FolderDlg s(this, App);
					if (s.DoModal() && ValidStr(s.Get()))
					{
						bool Has = false;

						for (auto n : *Lst)
						{
							const char *p = n->GetText(0);
							if (p && _stricmp(p, s.Get()) == 0)
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
								i->SetText(s.Get());
								Lst->Insert(i);
								Lst->ResizeColumnsToContent();
							}
						}
					}
				}
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
				if (Mailbox)
				{
					FolderDlg s(this, App, MAGIC_NONE, Mailbox);
					if (s.DoModal())
					{
						SetCtrlName(IDC_FOLDER, s.Get());
					}
				}
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

				DestPath = NewStr(GetCtrlName(IDC_FOLDER));
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
	ScribeExport Dlg(App);
	if (Dlg.DoModal())
	{
		{
			LProgressDlg Prog(App);
			Prog.SetDescription("Initializing...");
			Prog.SetType("items");

			LMailStore *Ms = App->GetDefaultMailStore();
			if (!Ms)
				return;

			int Items = 0;
			if (Dlg.AllFolders)
			{
				Items += Dlg.CountItems(Ms->Root, true);
			}
			else
			{
				for (unsigned i=0; i<Dlg.SrcPaths.Length(); i++)
				{
					Items += Dlg.CountItems(App->GetFolder(Dlg.SrcPaths[i]), false);
				}
			}
			Prog.SetRange(Items);

			if (Dlg.AllFolders)
			{
				Dlg.ExportFolder(Dlg.DestPath, "/", true, &Prog);
			}
			else
			{
				for (unsigned i=0; i<Dlg.SrcPaths.Length() && !Prog.IsCancelled(); i++)
				{
					char Dest[256];
					strcpy_s(Dest, sizeof(Dest), Dlg.DestPath);
					char *e = Dest + strlen(Dest) - 1;
					if (*e == '/') *e = 0;
					strcat(Dest, Dlg.SrcPaths[i]);

					bool s = Dlg.ExportFolder(Dest, Dlg.SrcPaths[i], false, &Prog);
					if (!s)
					{
						break;
					}
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
				Dlg.MailCreated,
				Dlg.MailSkipped,
				Dlg.MailErrors,
				Dlg.ContactCreated,
				Dlg.ContactSkipped,
				Dlg.ContactErrors
				);
	}
}
