#include "lgi/common/Lgi.h"
#include "Scribe.h"
#include "../Resources/resdefs.h"
#include "Store3Imap/ScribeImap.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/FileSelect.h"

/////////////////////////////////////////////////////////////////////////////////
ThingType::ThingType()
{
}

ThingType::~ThingType()
{
	if (Dirty)
	{
		if (DirtyThings.HasItem(this))
		{
			LAssert(!"Should not be deleting something in the dirty list...?");
			DirtyThings.Delete(this);
		}
	}
}

void ThingType::WhenLoaded(const char *file, int line, std::function<void()> Callback, int index)
{
	if (!Callback)
	{
		LAssert(!"No callback.");
		return;
	}

	if (!Loaded && GetObject())
	{
		// Lets just check the state of the object first...
		auto i = GetObject()->GetInt(FIELD_LOADED);
		if (i == Store3Loaded)
		{
			// This is the default for mail3 for instance...
			Loaded = true;
		}
	}

	if (Loaded)
	{
		Callback();
	}
	else
	{
		ThingEventInfo *cb = new ThingEventInfo;
		cb->File = file;
		cb->Line = line;
		cb->Callback = Callback;

		if (index < 0)
			OnLoadCallbacks.Add(cb);
		else
			OnLoadCallbacks.AddAt(index, cb);
	}
}

bool ThingType::IsLoaded(int Set)
{
	if (Set >= 0)
	{
		if (!Loaded && Set > 0)
		{
			Loaded = true;

			for (auto cb: OnLoadCallbacks)
			{
				// LgiTrace("OnLoadCallbacks %s:%i\n", cb.File, cb.Line);
				cb->Callback();
			}
			OnLoadCallbacks.DeleteObjects();
		}

		Loaded = Set > 0;
	}

	return Loaded;
}

bool ThingType::SetDirty(bool b)
{
	bool Status = false;

	if (WillDirty)
	{
		Thing *t = dynamic_cast<Thing*>(this);
		if (t &&
			t->GetObject() &&
			t->GetObject()->GetInt(FIELD_STORE_TYPE) == Store3Imap)
		{
			// IMAP email need to be explicitly saved when the message
			// is fully constructed.
			return true;
		}

		if (Dirty == b)
		{
		    if (Dirty)
		        LAssert(Thing::DirtyThings.HasItem(this));
		    else
		        LAssert(!Thing::DirtyThings.HasItem(this));
		}
		else
		{
		    if (b)
		    {
		        Dirty = true;
				if (!Thing::DirtyThings.HasItem(this))
				    Thing::DirtyThings.Add(this);
		    }
		    else
		    {
		        Dirty = false;
			    Thing::DirtyThings.Delete(this);
			}
		}
	}

	return Status;
}

/////////////////////////////////////////////////////////////////////////////////
LArray<ThingType*> ThingType::DirtyThings;

Thing::Thing(ScribeWnd *app, LDataI *object)
{
	_UserPtr = this;
	App = app;
	IncRef(); // Someone always starts with owning this object.
	SetObject(object, false, _FL);
}

Thing::~Thing()
{
	if (GetUI())
	{
		LAssert(!"Really, should we still be linked to a UI here?");
	}

	DirtyThings.Delete(this);
	DeleteObj(Data);

	SetParentFolder(NULL);

	auto o = GetObject();
	if (o)
	{
		SetObject(NULL, true, _FL);
		DeleteObj(o);
	}
}

LDataI *Thing::DefaultObject(LDataI *arg)
{
	LAssert(App != NULL);

	if (arg)
	{
		SetObject(arg, false, _FL);
	}
	else if (!GetObject() &&
			App &&
			App->GetDefaultMailStore())
	{
		LMailStore *Ms = App->GetDefaultMailStore();
		if (Ms)
			SetObject(Ms->Store->Create(Type()), false, _FL);
	}

	return GetObject();
}

bool Thing::OnKey(LKey &k)
{
	#ifndef WINDOWS
    //  This is being done by the VK_APPS key on windows... 
	if (k.IsContextMenu())
	{
		if (k.Down())
		{
			LMouse m;
			m.x = 5;
			m.y = 5;
			m.ViewCoords = true;
			m.Target = GetList();
			DoContextMenu(m);
		}
		return true;
	}
	#endif
	
	return false;
}

void Thing::SetParentFolder(ScribeFolder *f)
{
	if (GetFolder() == f)
		return;
	
	if (_ParentFolder)
	{
		if (!_ParentFolder->Items.HasItem(this))
		{
			LAssert(!"_ParentFolder->Items incorrect.");
		}
		_ParentFolder->Items.Delete(this);
	}

	_ParentFolder = f;

	if (_ParentFolder)
	{
		LAssert(!_ParentFolder->Items.HasItem(this));
		_ParentFolder->Items.Insert(this);
	}
}

Store3Status Thing::SetFolder(ScribeFolder *New, int Param)
{
	Store3Status Moved = Store3Error;

	if (New)
	{
		ScribeFolder *Old = GetFolder();
		if (Old)
		{
			if (Old->GetObject() &&
				New->GetObject() &&
				Old->GetObject()->GetStore() != 0 &&
				Old->GetObject()->GetStore() == New->GetObject()->GetStore())
			{
				// Both source and dest are local folders...
				// This is really an optimization to reduce the overhead of moving objects
				// between folders, a function which is provided by the storage sub-system
				// does all the work for us.
				LArray<LDataI*> Mv;
				Mv.Add(GetObject());

				Moved = Old->GetObject()->GetStore()->Move(New->GetFldObj(), Mv);
				if (Moved == Store3Success)
				{
					LAssert(!Old->Items.HasItem(this));
					LAssert(GetFolder() == New);
					LAssert(New->Items.HasItem(this));
				}
			}
			else
			{
				if (IsPlaceHolder())
				{
				}
				else if (New->GetObject() && GetObject() && New->GetObject()->GetStore())
				{
					// Source OR Dest are remote...
					LDataI *NewObject = New->GetObject()->GetStore()->Create(Type());
					if (NewObject)
					{
						LDataI *OldObject = GetObject();

						// Copy the current data into the new object
						NewObject->CopyProps(*GetObject());
						SetObject(NewObject, false, _FL);

						// Try writing it to the store...
						// bool InOld = Old->Items.HasItem(this);
						Store3Status WrStatus = New->WriteThing(this);
						switch (WrStatus)
						{
							default:
							case Store3Error:
							{
								// It failed, delete the new object...
								SetObject(OldObject, false, _FL);
								DeleteObj(NewObject);
								break;
							}
							case Store3Success:
							{
								// Ok, immediate save, set new object
								// delete old object
								Moved = OldObject->Delete(false);
								if (Moved == Store3Error)
								{
									SetObject(OldObject, false, _FL);
									DeleteObj(NewObject);
								}
								else if (Moved == Store3Success)
								{
									// Remove the Thing from the old folder.
									Old->Items.Delete(this);
									LAssert(New->Items.HasItem(this));
									
									if (GetList())
										GetList()->Remove(this);
								}
								else // Delayed
								{
									// Because the list item is the Mail object itself we can't leave a 
									// place holder in the LList until the delayed delete happens. The 
									// mail object is need to appear in the destination folder, as it's
									// now associated with 'NewObject'.
									Old->Items.Delete(this);
									if (GetList())
										GetList()->Remove(this);
								}
								break;
							}
							case Store3Delayed:
							{
								// We have to wait for the object to be written.
								// There will be a ScribeWnd::OnNew(...) call back 
								// when that happens.
								// 
								// If is succeeds:
								// - we need to swap the objects over... complete
								// the updating of the UI.
								//
								// If it fails:
								// - do nothing...
								//
								// In the meantime change the object back to the old
								// one. But leave the UserData pointing to us. This
								// is so the OnNew handler can finish the move for
								// us later, and still know whats going on.
								LAssert(Old->Items.HasItem(this));	// The old folder needs to have 
																		// a pointer to us until "OnNew".
								SetObject(OldObject, false, _FL);

								// Setup a new Thing for the new Object...
								Thing *t = App->CreateThingOfType(Type(), NewObject);
								if (t)
								{
									// Add it to the new folder...
									New->Items.Add(t);

									// Setup a delete operation to be executed when the object arrives
									// back at the app with an OnNew events.
									t->DeleteOnAdd.Path = Old->GetPath();
									t->DeleteOnAdd.Obj = this;
								}

								Moved = WrStatus;
								break;
							}
						}
					}
				}
			}
		}
		else
		{
			Moved = New->WriteThing(this);
		}
	}

	return Moved;
}

void Thing::OnCreate()
{
}

bool Thing::OnDelete()
{
	if (!App)
		return false;

	if (IsPlaceHolder())
	{
		DecRef();
		return true;
	}

	Mail *m = IsMail();
	if (m)
	{
		List<Mail> Lst;
		Lst.Insert(m);
		App->OnNewMail(&Lst, false);
	}

	if (GetObject() && GetObject()->GetStore())
	{
		LArray<LDataI*> Del;
		Del.Add(GetObject());
		if (GetObject()->GetStore()->Delete(Del, true))
		{
			return true;
		}
	}

	ScribeFolder *Trash = App->GetFolder(FOLDER_TRASH);
	if (!Trash)
		return false;

	LArray<Thing*> Items;
	Items.Add(this);
	return Trash->MoveTo(Items);
}

void Thing::OnMove()
{
	/*
	if (LListItem::Parent)
	{
		int MyIndex = LListItem::Parent->IndexOf(this);
		LListItem::Parent->Remove(this);

		if (Parent && Parent->Length() < 1)
		{
			Window->OnSelect();
		}
		else if (LListItem::Parent && MyIndex >= 0)
		{
			LListItem::Parent->Value(MyIndex);
		}
	}
	*/
}

bool Thing::OnBeginDrag(LMouse &m)
{
	int Ico = -1;
	switch (Type())
	{
		case MAGIC_MAIL:
			Ico = ICON_UNREAD_MAIL;
			break;
		case MAGIC_CONTACT:
			Ico = ICON_CONTACT;
			break;
		case MAGIC_FILTER:
			Ico = ICON_FILTER;
			break;
		case MAGIC_CALENDAR:
			Ico = ICON_CALENDAR;
		default:
			break;
	}

	if (Ico >= 0)
	{
		LImageList *s = App->GetIconImgList();
		if (s)
		{
			LRect r;
			r.ZOff(s->TileX()-1, s->TileY()-1);
			r.Offset(s->TileX() * Ico, 0);
			SetIcon(s, &r);
		}
	}

	Drag(App, m.Event, DROPEFFECT_MOVE | DROPEFFECT_COPY);

	SetIcon(NULL);
	return true;
}

bool Thing::GetFormats(LDragFormats &Formats)
{
	Formats.Supports(ScribeThingList);
	
	#if !defined(LGI_COCOA)
	Formats.Supports(LGI_FileDropFormat);
	#else
	Formats.Supports(LGI_StreamDropFormat);
	#endif

	return Formats.Length() > 0;
}

void Thing::ExportAll(	LViewI *Parent,
						const char *ExportMimeType,
						std::function<void(bool)> Callback)
{
	List<Thing> Sel;
	if (GetList())
		GetList()->GetSelection(Sel);
	else
		Sel.Insert(this);
	
	auto Process = [&](LFileSelect *Select)
	{
		int Exported = 0;
		int Errors = 0;
		for (auto m: Sel)
		{
			const char *Out;
			char Buf[MAX_PATH_LEN];
			if (Sel.Length() == 1)
			{
				Out = Select->Name();
			}
			else
			{
				char *Leaf = LGetLeaf(m->GetDropFileName());
				if (!Leaf)
				{
					Errors++;
					continue;
				}
				
				// Make a unique name...
				for (int Index = 1; Index < 1000; Index++)
				{
					LString Nm = Leaf;
					if (Index > 1)
					{
						LString::Array a = Nm.RSplit(".", 1);
						if (a.Length() == 2)
							Nm.Printf("%s %i.%s", a[0].Get(), Index, a[1].Get());
						else
							Nm.Printf("%s %i", a[0].Get(), Index);
					}				
					if (!LMakePath(Buf, sizeof(Buf), Select->Name(), Nm))
					{
						Errors++;
						break;
					}
					
					if (!LFileExists(Buf))
						break;
				}
				
				Out = Buf;
			}					
		
			LAutoPtr<LFile> f(new LFile);
			if (!f->Open(Out, O_WRITE))
			{
				LgiTrace("%s:%i - Couldn't open '%s' for writing.", _FL, Select->Name());
				Errors++;
			}
			else
			{
				f->SetSize(0);
				if (m->Export(m->AutoCast(f), ExportMimeType))
					Exported++;
				else
					Errors++;
			}
		}

		if (Errors > 0)
			LgiMsg(Parent, "Export failed: %i exported, %i errors.", AppName, MB_OK, Exported, Errors);
			
		if (Callback)
			Callback(Errors == 0);
	};
	
	auto Select = new LFileSelect(Parent);
	if (Sel.Length() == 1)
		Select->Name(LGetLeaf(GetDropFileName()));
	Select->Type("Email", "*.eml");

	if (Sel.Length() > 1)
	{
		Select->OpenFolder([&](auto dlg, auto status)
		{
			if (status)
				Process(dlg);
			delete dlg;
		});
	}
	else
	{
		Select->Save([&](auto dlg, auto status)
		{
			if (status)
				Process(dlg);
			delete dlg;
		});
	}
}

bool Thing::CallMethod(const char *MethodName, LVariant *ReturnValue, LArray<LVariant*> &Args)
{
	ScribeDomType Fld = StrToDom(MethodName);
	switch (Fld)
	{
		case SdImport: // Type: (String FileName, String MimeType)
		{
			*ReturnValue = false;
			if (Args.Length() != 2)
				LgiTrace("%s:%i - Error: expecting 2 arguments to 'Import'.\n", _FL);
			else
			{
				auto FileName = Args[0]->Str();
				LAutoPtr<LFile> f(new LFile);
				if (f->Open(FileName, O_READ))
				{
					auto status = Import(AutoCast(f), Args[1]->Str());
					*ReturnValue = status.status;
				}
				else
					LgiTrace("%s:%i - Error: Can't open '%s' for reading.\n", _FL, FileName);
			}
			break;
		}
		case SdExport: // Type: (String FileName, String MimeType)
		{
			*ReturnValue = false;
			if (Args.Length() != 2)
				LgiTrace("%s:%i - Error: expecting 2 arguments to 'Export'.\n", _FL);
			else
			{
				auto FileName = Args[0]->Str();
				LAutoPtr<LFile> f(new LFile);
				if (f->Open(FileName, O_WRITE))
				{
					auto status = Export(AutoCast(f), Args[1]->Str());
					*ReturnValue = status.status;
				}
				else
					LgiTrace("%s:%i - Error: Can't open '%s' for writing.\n", _FL, FileName);
			}
			break;
		}
		default:
			return false;
	}

	return true;
}

bool Thing::GetData(LArray<LDragData> &Data)
{
	bool Status = false;

	LArray<Thing*> Objs;
	LList *ParentList = LListItem::Parent;
	if (ParentList)
		ParentList->GetSelection(Objs);

	for (unsigned idx = 0; idx < Data.Length(); idx++)
	{
		LDragData &dd = Data[idx];
		if (!dd.Format)
			continue;
		
		if (dd.IsFormat(LGI_FileDropFormat))
		{
			LMouse m;
			App->GetMouse(m, true);

			LString::Array Files;
			for (auto t: Objs)
				Status |= t->GetDropFiles(Files);

			if (Status && CreateFileDrop(&dd, m, Files))
				Status = true;
		}
		else if (dd.IsFormat(LGI_StreamDropFormat))
		{
			for (auto t: Objs)
			{
				if (t->GetObject())
				{
					LAutoStreamI s = t->GetObject()->GetStream(_FL);
					if (s)
					{
						s->SetPos(0);
						
						auto Fn = t->GetDropFileName();
						auto MimeType = Store3ItemTypeToMime(t->Type());
						dd.AddFileStream(LGetLeaf(Fn), MimeType, s);
					}
				}
			}
			
			Status = dd.Data.Length() > 0;
		}
		else if (dd.IsFormat(ScribeThingList))
		{
			ScribeClipboardFmt *Fmt = ScribeClipboardFmt::Alloc(Objs);
			if (Fmt)
			{
				Status |= dd.Data[0].SetBinary(Fmt->Sizeof(), Fmt);
				free(Fmt);
			}
		}
	}

	return Status;
}

////////////////////////////////////////
bool Thing::SetField(int Field, int n)
{
	return GetObject() ? GetObject()->SetInt(Field, n) != 0 : false;
}

bool Thing::SetField(int Field, double n)
{
	LAssert(!"Not implemented");
	return false;
}

bool Thing::SetField(int Field, char *n)
{
	return GetObject() ? GetObject()->SetStr(Field, n) != 0 : false;
}

bool Thing::SetField(int Field, LDateTime &n)
{
	return GetObject() ? GetObject()->SetDate(Field, &n) != 0 : false;
}

bool Thing::SetDateField(int Feild, LVariant &v)
{
	auto obj = GetObject();
	if (!obj)
		return false;

	if (v.Type == GV_DATETIME)
	{
		if (!v.Value.Date || !v.Value.Date->IsValid())
			return false;

		return obj->SetDate(FIELD_DATE_MODIFIED, v.Value.Date) >= Store3Delayed;
	}

	LDateTime dt(v.Str());
	if (!dt.IsValid())
		return false;

	return obj->SetDate(FIELD_DATE_MODIFIED, &dt) >= Store3Delayed;
}

bool Thing::GetField(int Field, int &n)
{
	n = GetObject() ? (int)GetObject()->GetInt(Field) : 0;
	return true;
}

bool Thing::GetField(int Field, double &n)
{
	LAssert(!"Not implemented");
	return false;
}

bool Thing::GetField(int Field, const char *&n)
{
	n = GetObject() ? GetObject()->GetStr(Field) : 0;
	return n != 0;
}

bool Thing::GetField(int Field, LDateTime &n)
{
	const LDateTime *t = GetObject() ? GetObject()->GetDate(Field) : 0;
	if (t)
	{
		n = *t;
		return n.IsValid();
	}

	return false;
}

bool Thing::GetDateField(int Field, LVariant &v)
{
	auto obj = GetObject();
	if (!obj)
		return false;

	auto dt = obj->GetDate(Field);
	if (!dt || !dt->IsValid())
		return false;
	
	v = dt;
	return true;
}

bool Thing::DeleteField(int Field)
{
	LAssert(!"Not implemented");
	return false;
}

//////////////////////////////////////////////////////////////////////////////
LArray<ThingUi*> ThingUi::All;

ThingUi::ThingUi(Thing *item, const char *name)
{	
	_Dirty = false;
	_Running = false;
	_Name = NewStr(name);
	_Item = item;
	App = item ? item->App : NULL;
	LAssert(App != NULL);

	SetQuitOnClose(false);
	SetSnapToEdge(true);
	Name(_Name);

	All.Add(this);
}

ThingUi::~ThingUi()
{
	LAssert(InThread());
	LAssert(All.HasItem(this));
	All.Delete(this);

	_Running = false;
	_Dirty = false;
	DeleteArray(_Name);
}

bool ThingUi::OnViewKey(LView *v, LKey &k)
{
	bool IsPopup = false;
	#ifdef __GTK_H__
	OsView Hnd = GtkCast(GetWindow()->WindowHandle(), gtk_widget, GtkWidget);
	#else
	OsView Hnd = Handle();
	#endif
	for (LViewI *p = v; p; p = p->GetParent())
	{
		if (dynamic_cast<LPopup*>(p))
		{
			IsPopup = true;
			break;
		}
	}	

	bool Status = LWindow::OnViewKey(v, k);

	// The 'this' pointer may not be valid from here on.
	if (IsPopup)
	{
		return Status;
	}

	if (!Status &&
		k.Down() &&
		k.vkey == LK_ESCAPE)
	{
		LPostEvent(Hnd, M_CLOSE);
		return false;
	}

	return Status;
}

bool ThingUi::SetDirty(bool d, bool ui)
{
	bool Status = true;

	if (d ^ _Dirty)
	{
		if (d)
		{
			if (_Item && dynamic_cast<ImapMail*>(_Item->GetObject()))
			{
				LAssert(!"Should imap mail become dirty?");
			}
			
			_Dirty = true;
			OnDirty(_Dirty);
		}
		else
		{
			int Result = ui ? LgiMsg(this, LLoadString(IDS_SAVE_ITEM), AppName, MB_YESNOCANCEL) : IDYES;
			if (Result == IDYES)
			{
				OnSave();
				_Dirty = false;
				OnDirty(_Dirty);
			}
			else if (Result == IDCANCEL)
			{
				Status = false;
			}
			else
			{
				// Result == IDNO
				_Dirty = false;
				OnDirty(_Dirty);
			}
		}

		if (Status)
		{
			char s[256];
			if (_Dirty)
			{
				sprintf_s(s, sizeof(s), "%s (%s)", _Name, LLoadString(IDS_CHANGED));
			}
			else
			{
				strcpy_s(s, sizeof(s), _Name);
			}
			Name(s);
		}
	}

	return Status;
}

bool ThingUi::OnRequestClose(bool OsShuttingDown)
{
	static bool Processing = false;
	bool Status = false;

	if (!Processing)
	{
		Processing = true;

		bool Cleaned = SetDirty(false);
		if (Cleaned)
		{
			Status = LWindow::OnRequestClose(OsShuttingDown);
		}

		Processing = false;
	}

	return Status;
}

