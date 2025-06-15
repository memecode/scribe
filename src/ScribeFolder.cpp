/*
**	FILE:			ScribeFolder.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			17/1/2000
**	DESCRIPTION:	Scribe folder's
**
**	Copyright (C) 2000-2002, Matthew Allen
**		fret@memecode.com
*/

// Includes
#include "Scribe.h"
#include "lgi/common/DropFiles.h"
#include "lgi/common/ProgressDlg.h"
#include "lgi/common/QuickSort.h"
#include "lgi/common/DisplayString.h"
#include "lgi/common/TextFile.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/FileSelect.h"
#include "lgi/common/ClipBoard.h"

#include "Calendar.h"
#include "resdefs.h"
#include "ReplicateDlg.h"
#include "FolderTask.h"
#include "AsyncOperationState.h"

//////////////////////////////////////////////////////////////////////////////
#if WINNATIVE
#include "lgi/common/Com.h"
#endif

size_t AsyncOperationState::Instances = 0;

class LDndFilePromise
	#if defined(WINDOWS)
	#elif defined(__GTK_H__)
	#elif defined(MAC)
	#endif
{
	LStream *Src;
	#if WINNATIVE
	FILEGROUPDESCRIPTOR Gd;
	#elif defined(__GTK_H__)
	#elif defined(MAC)
	#endif
	
public:
	LDndFilePromise(LDragData &dd, LStream *src, LString FileName)
	{
		Src = src;
		
		#if WINNATIVE
		
			ZeroObj(Gd);
			Gd.cItems = 1;
			
			// CLSID Fd.clsid;
			// SIZEL Fd.sizel;
			// POINTL Fd.pointl;
			// FILETIME Fd.ftCreationTime;
			// FILETIME Fd.ftLastAccessTime;
			// FILETIME Fd.ftLastWriteTime;
			
			FILEDESCRIPTOR &Fd = Gd.fgd[0];
			Fd.dwFlags = FD_ATTRIBUTES | FD_PROGRESSUI;
			Fd.dwFileAttributes = FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_NORMAL;

			int64 Sz = Src->GetSize();
			if (Sz >= 0)
			{
				Fd.dwFlags |= FD_FILESIZE;
				Fd.nFileSizeHigh = Sz >> 32;
				Fd.nFileSizeLow = Sz & 0xffffffff;
			}

			auto Leaf = FileName.RFind(DIR_STR);
			LAutoWString FnW(Utf8ToWide(Leaf >= 0 ? FileName(Leaf+1,-1) : FileName));
			Strcpy(Fd.cFileName, CountOf(Fd.cFileName), FnW.Get());

			LVariant &v = dd.Data[0];
			v.SetBinary(sizeof(Gd), &Gd);
		
		#elif defined(__GTK_H__)

			LAssert(!"Not impl.");

		#elif LGI_COCOA
		
			LAssert(!"Not impl.");
		
		#elif LGI_CARBON
		
			// See Apple: Technical Note TN1085
			// https://web.archive.org/web/20080725134839/http://developer.apple.com/technotes/tn/tn1085.html
		
			// flavorTypePromiseHFS = 'phfs'
			PromiseHFSFlavor Promise;
			ZeroObj(Promise);
			Promise.fileType = 'mbox';
			Promise.fileCreator = '****';
			Promise.promisedFlavor = kDragPromisedFlavor; // 'fssP'
			int Sz = sizeof(Promise);

			LVariant &v1 = dd.Data[0];
			v1.SetBinary(sizeof(Promise), &Promise, false);
		
			// The Dnd code will add the 2nd part of the promise
			// There isn't the visibility into that API at this level

		#else

			#warning "Impl LDndFilePromise for this platform"

		#endif
	}
	
	~LDndFilePromise()
	{
		#if defined(WINDOWS)
		#elif defined(__GTK_H__)
		#elif defined(MAC)
		#endif
	}

	#if defined(WINDOWS)
	#elif defined(__GTK_H__)
	#elif defined(MAC)
	#endif

	int AddContents(LDragData &dd)
	{
		dd.Data[0] = Src;
		return 1;
	}
};

//////////////////////////////////////////////////////////////////////////////
#define PROFILE_POPULATE			0
#define PROFILE_LOAD_THINGS			0

int FoldersCurrentlyLoading =		0;

char ScribeFolderObject[] = "com.memecode.Folder";

class ScribeFolderPriv
{
public:
	int8 IsInbox = -1;
	bool InUpdateUnread = false;
	LAutoPtr<LDisplayString> DsBase;
	LAutoPtr<LDisplayString> DsUnread;
	LAutoPtr<LDndFilePromise> FilePromise;
};

//////////////////////////////////////////////////////////////////////////////
ScribeFolder::ScribeFolder()
{
	d = new ScribeFolderPriv;
}

ScribeFolder::~ScribeFolder()
{
	bool isRoot = IsRoot();

	OnItemType(false);
	if (BeforeDelete)
		BeforeDelete();		

	if (CurState != FldState_Idle)
	{
		// int Cur = FoldersCurrentlyLoading;

		#ifdef _DEBUG
		LAssert(!"Can't delete folder while it's busy.");
		#else
		LgiMsg(	App,
				"Can't unload folder while busy. Please report what\n"
				"you were attempting to do to fret@memecode.com",
				"Scribe Error",
				MB_OK);
		#endif
		return;
	}

	switch (GetItemType())
	{
		case MAGIC_CALENDAR:
			CalendarSource::FolderDelete(this);
			// fall through
		case MAGIC_CONTACT:
		case MAGIC_FILTER:
		case MAGIC_GROUP:
			App->RemoveThingSrc(this);
			break;
		default:
			break;
	}
	
	if (View())
	{
		bool IsMe = View()->GetContainer() == this;
		// bool IsSelect = Select();
		
		View()->DeletePlaceHolders();
		
		if (IsMe)
		{
			View()->SetContainer(0);
			View()->RemoveAll();
		}
	}

	Thing *t;
	int i = 0;
	while ((t = Items[i]))
	{
		if (!t->DecRef())
			i++;
	}

	Update();
	EmptyFieldList();
	DeleteObj(d);

	ScribeFolder *f = GetChildFolder();
	DeleteObj(f);
	if (!isRoot)
	{
		f = GetNextFolder();
		DeleteObj(f);
	}

	// Don't delete 'Object' here, it's owned by the backend storage object
}

bool ScribeFolder::SetObject(LDataI *o, bool InDestructor, const char *File, int Line)
{
	if (CurState != FldState_Idle)
	{
		LAssert(!"Can't set object while folder is not idle.");
		return false;
	}

	return LDataUserI::SetObject(o, InDestructor, File, Line);
}

void ScribeFolder::UpdateOsUnread()
{
	#ifdef WIN32
	if (App->GetFolder(FOLDER_INBOX) == this)
	{
		LHashTbl<StrKey<char,false>, int> Un(0, -1);

		// Pre-populate 'Un' with our email accounts
		List<ScribeAccount> *Acc = App->GetAccounts();
		if (Acc)
		{
			for (auto a: *Acc)
			{
				LVariant e = a->Identity.Email();
				if (e.Str())
					Un.Add(e.Str(), 0);
			}
		}

		// Scan folder for unread email...		
		for (auto t : Items)
		{
			Mail *m = t->IsMail();
			if (m)
			{
				if (!TestFlag(m->GetFlags(), MAIL_READ))
				{
					ScribeAccount *a = m->GetAccountSentTo();
					if (a)
					{
						LVariant Email;
						Email = a->Identity.Email();
						if (Email.Str())
						{
							int Cur = Un.Find(Email.Str());
							Un.Add(Email.Str(), Cur + 1);
						}
					}
				}
			}
		}

		#if 0
		// Set system email status
		LArray<int> Ver;
		int Os = LGetOs(&Ver);
		if ((Os == LGI_OS_WIN32 || Os == LGI_OS_WIN64) && (Ver[0] > 5 || (Ver[0] == 5 && Ver[1] > 0)))
		{
			char e[256];
			LgiGetExeFile(e, sizeof(e));

			typedef HRESULT (__stdcall *pSHSetUnreadMailCount)(LPCWSTR pszMailAddress, DWORD dwCount,LPCWSTR pszShellExecuteCommand);
			LLibrary Shell32("shell32");
			pSHSetUnreadMailCount SHSetUnreadMailCount = (pSHSetUnreadMailCount) Shell32.GetAddress("SHSetUnreadMailCountW");
			if (SHSetUnreadMailCount)
			{
				LVariant Exe;
				Exe = e;

				char *k;
				for (int u=Un.First(&k); u>=0; u=Un.Next(&k))
				{
					LVariant Email = k;
					SHSetUnreadMailCount(Email.WStr(), u, Exe.WStr());
				}
			}
		}
		#endif
	}
	#endif
}

void ScribeFolder::SetLoadOnDemand()
{
	if ((LoadOnDemand = new LTreeItem))
	{
		Expanded(false);
		LoadOnDemand->SetText((char*)LLoadString(IDS_LOADING));
		Insert(LoadOnDemand);
	}
}

void ScribeFolder::GetMessageById(const char *Id, std::function<void(Mail*)> Callback)
{
	if (!Id)
	{
		if (Callback) Callback(NULL);
		return;
	}

	LoadThings(NULL, [this, Id=LString(Id), Callback](auto s)
	{
		if (s < Store3Delayed)
		{
			if (Callback) Callback(NULL);
			return;
		}

		for (auto t : Items)
		{
			Mail *r = t->IsMail();
			if (!r)
				continue;
		
			auto rid = r->GetMessageId();
			if (!Stricmp(rid, Id.Get()))
			{
				if (Callback)
					Callback(r);
				return;
			}
		}
	});
}

bool ScribeFolder::InsertThing(Thing *t)
{
	bool Status = false;

	if (t && Select() && View())
	{
		// Filter
		ThingFilter *Filter = App->GetThingFilter();
		t->SetFieldArray(FieldArray);
		if (!Filter || Filter->TestThing(t))
		{
			if (t->GetList() != View())
			{
				View()->Insert(t);
				#if WINNATIVE
				UpdateWindow(View()->Handle());
				#endif
			}
		}
		Status = true;
	}

	return Status;
}

bool ScribeFolder::GetThreaded()
{
	return GetObject() ? GetObject()->GetInt(FIELD_FOLDER_THREAD) != 0 : false;
}

void ScribeFolder::SetThreaded(bool t)
{
	if (GetObject() && GetThreaded() ^ t)
	{
		GetObject()->SetInt(FIELD_FOLDER_THREAD, t);
		SetDirty();
	}
}

ThingList *ScribeFolder::View()
{
	return	App ? App->GetMailList() : 0;
}

bool ScribeFolder::HasFieldId(int Id)
{
	auto o = GetFldObj();
	if (o)
	{
		for (LDataPropI *f = o->Fields().First(); f; f = o->Fields().Next())
		{
			if (Id == f->GetInt(FIELD_ID))
			{
				return true;
			}
		}
	}

	return false;
}

void ScribeFolder::SetFolderPerms(LView *Parent, ScribeAccessType Access, ScribePerm Perm, std::function<void(bool)> Callback)
{
	ScribePerm Current = GetFolderPerms(Access);	
	int Field = (Access == ScribeReadAccess) ? FIELD_FOLDER_PERM_READ : FIELD_FOLDER_PERM_WRITE;

	if (GetObject() && Current != Perm)
	{
		App->GetAccessLevel(Parent, Current, GetPath(), [this, Field, Perm, Callback](auto Allow)
		{
			if (Allow)
			{
				GetObject()->SetInt(Field, Perm);
				SetDirty();
			}
			if (Callback)
				Callback(Allow);
		});
	}
	else
	{
		if (Callback)
			Callback(true); // i.e. not changing
	}
}

ScribePerm ScribeFolder::GetFolderPerms(ScribeAccessType Access)
{
	int Field = (Access == ScribeReadAccess) ? FIELD_FOLDER_PERM_READ : FIELD_FOLDER_PERM_WRITE;
	return GetObject() ? (ScribePerm)GetObject()->GetInt(Field) : PermRequireNone;
}

Store3Status ScribeFolder::CopyTo(ScribeFolder *NewParent, int NewIndex)
{
	Store3Status Copied = Store3Error;

	if (NewParent == GetParent())
	{
		NewParent->GetObject()->GetStore();
	}
	else if (GetObject() &&
			GetObject()->GetStore() &&
			NewParent->GetObject() &&
			NewParent->GetObject()->GetStore())
	{
		// Find or create the destination folder
		LDataFolderI *Dst = 0;
		auto Name = GetObject()->GetStr(FIELD_FOLDER_NAME);
		for (ScribeFolder *f = NewParent->GetChildFolder(); f; f = f->GetNextFolder())
		{
			auto n = f->GetObject()->GetStr(FIELD_FOLDER_NAME);
			if (n && !_stricmp(n, Name))
			{
				Dst = f->GetFldObj();
			}
		}

		if (!Dst)
		{
			// Create sub-folder
			if ((Dst = dynamic_cast<LDataFolderI*>(NewParent->GetObject()->GetStore()->Create(GetObject()->Type()))))
			{
				Dst->CopyProps(*GetObject());
				if (Dst->Save(NewParent->GetObject()) == Store3Error)
					return Store3Error;
			}
			else return Store3Error;
		}

		if (Dst)
		{
			Copied = Store3ReplicateFolders(App, Dst, GetFldObj(), true, false, 0);
		}
	}
	else LAssert(!"Pointer error");

	return Copied;
}

ScribeFolder *ScribeFolder::FindChild(const char *name)
{
	if (!name)
		return nullptr;
	for (auto c = GetChildFolder(); c; c = c->GetNextFolder())
		if (c->GetName(true).Equals(name))
			return c;
	return nullptr;
}

void ScribeFolder::SetFolder(ScribeFolder *newParent, std::function<void(Store3Status)> callback)
{
	LDataI *obj = NULL;
	LDataStoreI *store = NULL;
	if (newParent == this || !newParent)
	{
		LAssert(!"invalid ptr");
		if (callback)
			callback(Store3Error);
	}
	else if (!(obj = GetObject()) ||
			!(store = obj->GetStore()) ||
			!newParent->GetObject() ||
			!newParent->GetObject()->GetStore())
	{
		LAssert(!"missing object");
		if (callback)
			callback(Store3Error);
	}
	else
	{
		Store3Status status = Store3Error;

		if (store == newParent->GetObject()->GetStore())
		{
			// Simple in storage movement
			LArray<LDataI*> Mv;
			Mv.Add(obj);
			status = store->Move(newParent->GetFldObj(), Mv);
			if (status)
			{
				//  && Param >= 0
				// GetObject()->SetInt(FIELD_FOLDER_INDEX, Param);
			}
		}
		else
		{
			// Cross storage movement...

			// Find or create the destinate folder
			LDataFolderI *Dst = NULL;
			int Idx = 0;
			auto Name = obj->GetStr(FIELD_FOLDER_NAME);
			for (auto c = newParent->GetChildFolder(); c; c = c->GetNextFolder(), Idx++)
			{
				auto n = c->GetObject()->GetStr(FIELD_FOLDER_NAME);
				if (n && !Stricmp(n, Name))
				{
					Dst = c->GetFldObj();
					break;
				}
			}

			if (!Dst)
			{
				Dst = dynamic_cast<LDataFolderI*>(newParent->GetObject()->GetStore()->Create(newParent->Type()));
			}
			else
			{
				ScribeFolder *c = CastFolder(dynamic_cast<LDataFolderI*>(Dst));
				if (c)
				{
					c->Remove();
					DeleteObj(c);
				}
			}

			if (Dst)
			{
				if (View())
					View()->RemoveAll();

				// Clean up all the items and sub-folders, they will be created by the 
				// replication code calling "OnNew".
				Thing *t;
				while ((t = Items[0]))
				{
					t->DecRef();
					Items.Delete(t);
				}
				
				// Delete the child folders...
				ScribeFolder *Cf;
				while ((Cf = GetChildFolder()))
				{
					DeleteObj(Cf);
				}

				// Copy ourself over...
				Dst->CopyProps(*obj);
				
				LDataFolderI *Old = GetFldObj();
				SetObject(Dst, false, _FL);				

				// Save the object to the new store...
				Store3Status s = obj->Save(newParent->GetObject());
				if (s != Store3Error)
				{
					// And replicate all the children objects...
					status = Store3ReplicateFolders(App, GetFldObj(), Old, true, true, 0);
				}
			}
			else LAssert(!"Not a valid folder");
		}

		if (status == Store3Success)
		{
			// Move LTreeItem node...
			newParent->Insert(this); // FIXME: Missing index for insert...
			Select(true);
			LoadFolders();

			// Adjust all the other indexes so that it's remembered
			ScribeFolder *p = GetFolder();
			if (p)
			{
				int Idx = 0;
				for (p = p->GetChildFolder(); p; p = p->GetNextFolder())
				{
					p->SetSortIndex(Idx++);
					p->SetDirty();
				}
			}
		}

		if (callback)
			callback(status);
	}
}

Store3Status ScribeFolder::DeleteAllThings(std::function<void(Store3Status)> Callback)
{
	if (!GetFldObj())
		return Store3Error;
		
	Store3Status r = GetFldObj()->DeleteAllChildren();
	if (r == Store3Error)
	{
		LAssert(!"DeleteAllChildren failed.");
	}
	else if (r == Store3Success)
	{
		LAssert(Items.Length() == 0);
		OnUpdateUnRead(0, true);
		Update();
	}

	return r;
}

Store3Status ScribeFolder::DeleteThing(Thing *t, std::function<void(Store3Status)> Callback)
{
	Store3Status Status = Store3Error;

	if (t && t->GetObject())
	{
		if (t->IsAttachment())
		{
			#ifdef _DEBUG
			Mail *m =
			#endif
			t->IsAttachment()->GetOwner();
			LAssert(m && Items.HasItem(m));
		}

		if (t->GetObject()->Delete())
		{
			t->SetParentFolder(NULL);
			if (View())
				View()->Remove(t);
		}
	}

	return Status;
}

Store3Status ScribeFolder::WriteThing(Thing *t, std::function<void(Store3Status)> Callback)
{
	if (!t)
	{
		if (Callback)
			Callback(Store3Error);
		return Store3Error;
	}

	auto ParentFolder = GetFolder();
	auto Path = ParentFolder ? ParentFolder->GetPath() : LString();

	auto OnAllow = [this, t, Callback]()
	{
		// Generic thing storage..
		bool Create = !t->GetObject();
		if (Create)
			t->SetObject(GetObject()->GetStore()->Create(t->Type()), false, _FL);

		if (!t->GetObject())
		{
			LAssert(!"No object?");
			LgiTrace("%s:%i - No object to save.\n", _FL);
			if (Callback)
				Callback(Store3Error);
			return;
		}

		// saving a thing that already has an item on disk
		auto Obj = GetObject();
		auto Status = t->GetObject()->Save(Obj);
		if (Status > Store3Error)
		{
			// The ScribeWnd::OnNew will take care of inserting the item into the
			// right folder, updating the unread count, any filtering etc.
			t->OnSerialize(true);
			if (Callback)
				Callback(Status);
		}
		else
		{
			if (Create)
				t->SetObject(NULL, false, _FL);
			LgiTrace("%s:%i - Object->Save returned %i.\n", _FL, Status);
			if (Callback)
				Callback(Store3Error);
		}
	};

	if (App)
		App->GetAccessLevel(App,
							GetWriteAccess(),
							Path,
							[cb=std::move(OnAllow)](auto Allow)
							{
								if (Allow)
									cb();
							});
	else
		OnAllow();	

	return Store3Success;
}

int ThingContainerNameCmp(LTreeItem *a, LTreeItem *b, NativeInt d)
{
	auto A = dynamic_cast<ScribeFolder*>(a);
	auto B = dynamic_cast<ScribeFolder*>(b);
	if (A && B)
		return Stricmp(A->GetText(), B->GetText());

	LAssert(!"Invalid objects.");
	return 0;
}

int ThingContainerIdxCmp(LTreeItem *a, LTreeItem *b, NativeInt d)
{
	auto A = dynamic_cast<ScribeFolder*>(a);
	auto B = dynamic_cast<ScribeFolder*>(b);
	if (A && B)
	{
		auto Aidx = A->GetSortIndex();
		auto Bidx = B->GetSortIndex();
		if (Aidx >= 0 || Bidx >= 0)
			return Aidx - Bidx;
	}
	else LAssert(!"Invalid objects.");

	return 0;
}

void ScribeFolder::SortSubfolders()
{
	int i = 0;
	ScribeFolder *c;

	LTreeItem::Items.Sort(ThingContainerNameCmp);

	for (c = GetChildFolder(); c; c = c->GetNextFolder())
	{
		c->SetSortIndex(i++);
		c->SetDirty();
	}

	GetTree()->UpdateAllItems();
	GetTree()->Invalidate();
}

void ScribeFolder::DoContextMenu(LMouse &m)
{
	MailTree *mt = dynamic_cast<MailTree*>(GetTree());
	LScriptUi s(new LSubMenu);
	List<Mail> Templates;
	bool ForceDelete = m.Shift();
	bool IsTrash = false;
	bool IsSystem = false;

	if (!s.Sub)
		return;

	IsTrash = GetItemType() == MAGIC_ANY;
	IsSystem = App->GetFolderType(this) >= 0;

	if (App->GetFolderType(this) == FOLDER_OUTBOX)
	{
		s.Sub->AppendItem(LLoadString(IDS_MARK_ALL_SEND), IDM_MARK_SEND, true);
	}

	if (IsTrash)
	{
		s.Sub->AppendItem(LLoadString(IDS_EMPTY), IDM_EMPTY, true);
		s.Sub->AppendItem(LLoadString(IDS_MARK_ALL_READ), IDM_MARK_ALL_READ, true);
	}
	else if (!IsRoot())
	{
		auto LiteralNew = LLoadString(IDS_NEW);
		const char *Type = "";
		switch (GetItemType())
		{
			case MAGIC_MAIL:
				Type = LLoadString(IDS_MAIL);
				break;
			case MAGIC_CONTACT:
				Type = LLoadString(IDS_CONTACT);
				break;
			case MAGIC_CALENDAR:
				Type = LLoadString(IDS_CAL_EVENT);
				break;
			case MAGIC_FILTER:
				Type = LLoadString(IDS_FILTER);
				break;
			case MAGIC_GROUP:
				Type = LLoadString(IDS_GROUP);
				break;
			default:
				break;
		}

		if (Type)
			s.Sub->AppendItem(LString::Fmt("%s %s", LiteralNew, Type), IDM_NEW_EMAIL, true);
	}

	switch (GetItemType())
	{
		case MAGIC_CONTACT:
		{
			char Str[256];
			const char *LiteralEmail = LLoadString(IDS_EMAIL);

			sprintf_s(Str, sizeof(Str), "%s '%s'", LiteralEmail, GetText());
			s.Sub->AppendItem(Str, IDM_EMAIL_GROUP, true);

			ScribeFolder *f = App->GetFolder(FOLDER_TEMPLATES);
			if (f)
			{
				// FIXME
				f->LoadThings();

				auto Merge = s.Sub->AppendSub(LLoadString(IDS_MERGE_TEMPLATE));
				if (Merge)
				{
					int n = 0;
					for (auto t: f->Items)
					{
						Mail *m = t->IsMail();
						if (m)
						{
							Templates.Insert(m);
							Merge->AppendItem(m->GetSubject()?m->GetSubject():(char*)"(no subject)", IDM_MERGE_TEMPLATE_BASE+n++, true);
						}
					}
				}
			}
			else
			{
				s.Sub->AppendItem(LLoadString(IDS_MERGE_TEMPLATE), 0, false);
			}

			s.Sub->AppendItem(LLoadString(IDS_MERGE_FILE), IDM_MERGE_FILE, true);
			break;
		}
		case MAGIC_MAIL:
		{
			if (!IsRoot())
			{
				s.Sub->AppendItem(LLoadString(IDC_COLLECT_SUB_MAIL), IDM_COLLECT_MAIL, true);
				s.Sub->AppendItem(LLoadString(IDS_MARK_ALL_READ), IDM_MARK_ALL_READ, true);

				if (GetObject() &&
					GetObject()->GetInt(FIELD_STORE_TYPE) == Store3Imap)
				{
					s.Sub->AppendItem(LLoadString(IDC_UNDELETE), IDM_UNDELETE, true);
					s.Sub->AppendItem("Expunge", IDM_EXPUNGE, true);
					s.Sub->AppendItem(LLoadString(IDC_REFRESH), IDM_REFRESH, true);
				}
			}
			break;
		}
		default:
			break;
	}

	if (s.Sub->ItemAt(0))
	{
		s.Sub->AppendSeparator();
	}

	s.Sub->AppendItem(LLoadString(IDS_FIND), IDM_FIND, true);
	s.Sub->AppendSeparator();

	bool HasFolderTask = App->HasFolderTasks();
	bool FolderOp = !HasFolderTask && !IsRoot();
	s.Sub->AppendItem(LLoadString(IDS_DELETEFOLDER), IDM_DELETE, FolderOp && ((!IsTrash && !IsSystem) || ForceDelete));
	s.Sub->AppendItem(LLoadString(IDS_CREATESUBFOLDER), IDM_CREATE_SUB, !HasFolderTask && CanHaveSubFolders(GetItemType()));
	s.Sub->AppendItem(LLoadString(IDS_RENAMEFOLDER), IDM_RENAME, FolderOp);
	
	auto ExportMimeType = GetStorageMimeType();
	s.Sub->AppendItem(LLoadString(IDS_EXPORT), IDM_EXPORT, FolderOp && ExportMimeType != NULL);
	s.Sub->AppendSeparator();

	s.Sub->AppendItem(LLoadString(IDS_SORT_SUBFOLDERS), IDM_SORT_SUBFOLDERS, true);
	s.Sub->AppendItem(LLoadString(IDS_COPY_PATH), IDM_COPY_PATH, true);
	s.Sub->AppendItem(LLoadString(IDS_PROPERTIES), IDM_PROPERTIES, true);

	LArray<LScriptCallback*> Callbacks;
	if (App->GetScriptCallbacks(LFolderContextMenu, Callbacks))
	{
		LScriptArguments Args(NULL);

		Args[0] = new LVariant(App);
		Args[1] = new LVariant(this);
		Args[2] = new LVariant(&s);

		for (auto c: Callbacks)
			App->ExecuteScriptCallback(*c, Args);

		Args.DeleteObjects();
	}

	m.ToScreen();
	LPoint Scr = _ScrollPos();
	m.x -= Scr.x;
	m.y -= Scr.y;

	int Msg = s.Sub->Float(GetTree(), m.x, m.y);
	switch (Msg)
	{
		case IDM_NEW_EMAIL:
		{
			if (App)
				App->CreateItem(GetItemType(), this);
			else
				LAssert(!"No app");
			break;
		}
		case IDM_MARK_SEND:
		{
			for (Thing *i: Items)
			{
				Mail *m = i->IsMail();
				if (m)
				{
					m->SetFlags(MAIL_READY_TO_SEND | MAIL_READ | MAIL_CREATED);
					m->SetDirty();
					m->Update();
				}
			}
			break;
		}
		case IDM_EMAIL_GROUP:
		{
			if (App)
			{
				Mail *m = dynamic_cast<Mail*>(App->CreateItem(MAGIC_MAIL, NULL, false));
				if (m)
				{
					MailUi *UI = dynamic_cast<MailUi*>(m->DoUI());
					if (UI)
					{
						List<Contact> Cts;
						App->GetContacts(Cts, this);
						for (auto c: Cts)
						{
							UI->AddRecipient(c);
						}
					}
				}
			}
			break;
		}
		case IDM_FIND:
		{
			ScribeFolder *Folder = (ScribeFolder*) GetTree()->Selection();
			OpenFinder(App, Folder);
			break;
		}
		case IDM_CREATE_SUB:
		{
			if (mt)
				mt->OnCreateSubDirectory(this);
			break;
		}
		case IDM_DELETE:
		{
			if (mt)
				mt->OnDelete(this, ForceDelete);
			break;
		}
		case IDM_RENAME:
		{
			auto Dlg = new FolderNameDlg(mt, GetName(true));
			Dlg->DoModal([this, Dlg, mt](auto dlg, auto id)
			{
				if (id && ValidStr(Dlg->Name))
				{
					// check for folder name conflicts...
					ScribeFolder *ParentFolder = GetFolder();
					LString Path;
					if (ParentFolder)
					    Path = ParentFolder->GetPath();
					if (Path)
					{
						char s[256];
						sprintf_s(s, sizeof(s), "%s/%s", Path.Get(), Dlg->Name);
						if (App->GetFolder(s))
						{
							LgiMsg(mt, LLoadString(IDS_SUBFLD_NAME_CLASH), AppName, MB_OK);
							return;
						}
					}

					// change the folders name...
					OnRename(Dlg->Name);
				}
			});
			break;
		}
		case IDM_EXPORT:
		{
			LString DropName = LGetLeaf(GetDropFileName());
			if (!DropName)
			{
				LgiTrace("%s:%i - Failed to create folder name.\n", _FL);
				break;
			}

			auto s = new LFileSelect(mt);
			s->Name(DropName);
			s->Save([this, ExportMimeType](auto s, auto ok)
			{
				if (ok)
				{
					if (LFileExists(s->Name()))
					{
						LString a, b;
						a.Printf(LLoadString(IDS_ERROR_FILE_EXISTS), s->Name());
						b.Printf("\n%s\n", LLoadString(IDS_ERROR_FILE_OVERWRITE));
						if (LgiMsg(GetTree(), a + b, AppName, MB_YESNO) == IDNO)
							return;
					}

					LAutoPtr<LFile> f(new LFile);
					if (!f || !f->Open(s->Name(), O_WRITE))
					{
						LgiTrace("%s:%i - Failed to open '%s' for writing.\n", _FL, s->Name());
						return;
					}
					
					f->SetSize(0);
					LAutoPtr<LStreamI> str(f.Release());
					ExportAsync(str, ExportMimeType);
				}
			});
			break;
		}
		case IDM_EMPTY:
		{
			if (App->GetMailList())
			{
				App->GetMailList()->RemoveAll();
				
				LArray<LDataI*> Del;
				for (ScribeFolder *c = GetChildFolder(); c; c = c->GetNextFolder())
					Del.Add(c->GetObject());
				if (Del.Length())
					GetObject()->GetStore()->Delete(Del, false);

				DeleteAllThings([mt](auto status)
				{
					mt->Invalidate();
				});
			}
			break;
		}
		case IDM_SORT_SUBFOLDERS:
		{
			SortSubfolders();	
			break;
		}
		case IDM_PROPERTIES:
		{
			mt->OnProperties(this);
			break;
		}
		case IDM_COPY_PATH:
		{
			LClipBoard c(GetTree());
			#ifdef WINDOWS
				LAutoWString w(Utf8ToWide(GetPath()));
				c.TextW(w);
			#else
				c.Text(GetPath());
			#endif
			break;
		}
		case IDM_COLLECT_MAIL:
		{
			CollectSubFolderMail();
			break;
		}
		case IDM_MARK_ALL_READ:
		{
			LArray<LDataI*> Change;
			for (auto c : Items)
			{
				Mail *m = c->IsMail();
				if (m)
				{
					if (!TestFlag(m->GetFlags(), MAIL_READ))
						Change.Add(m->GetObject());
				}
			}

			LVariant v = MAIL_READ;
			if (GetObject()->GetStore()->Change(Change, FIELD_FLAGS, v, OpPlusEquals) == Store3Error)
			{
				LProgressDlg Prog(GetTree(), 500);
				Prog.SetRange(Change.Length());
				
				// FIXME!!
				// Prog.SetYieldTime(200);

				Prog.SetDescription("Marking email...");
				for (auto c : Change)
				{
					auto t = CastThing(c);
					Mail *m = t ? t->IsMail() : NULL;
					if (m)
						m->SetFlags(m->GetFlags() | MAIL_READ, false, false);
					Prog++;
					if (Prog.IsCancelled())
						break;
				}

				OnUpdateUnRead(0, true);
			}
			break;
		}
		case IDM_MERGE_FILE:
		{
			auto s = new LFileSelect(mt);
			s->Type("Email Template", "*.txt;*.eml");
			s->Open([this](auto dlg, auto id)
			{
				if (id)
				{
					LArray<ListAddr*> Recip;
					for (auto i: Items)
					{
						Recip.Add(new ListAddr(i->IsContact()));
					}
					App->MailMerge(Recip, dlg->Name(), 0);
					Recip.DeleteObjects();
				}
			});
			break;
		}
		case IDM_UNDELETE:
		{
			const char *s = DomToStr(SdUndelete);
			GetFldObj()->OnCommand(s);
			break;
		}
		case IDM_EXPUNGE:
		{
			const char *s = DomToStr(SdExpunge);
			GetFldObj()->OnCommand(s);
			break;
		}
		case IDM_REFRESH:
		{
			const char *s = DomToStr(SdRefresh);
			GetFldObj()->OnCommand(s);
			break;
		}
		default:
		{
			Mail *Template = Templates[Msg - IDM_MERGE_TEMPLATE_BASE];
			if (Template)
			{
				LArray<ListAddr*> Recip;
				for (auto i: Items)
				{
					Recip.Add(new ListAddr(i->IsContact()));
				}
				App->MailMerge(Recip, 0, Template);
				Recip.DeleteObjects();
			}
			else
			{
				// Handle any installed callbacks for menu items
				for (unsigned i=0; i<s.Callbacks.Length(); i++)
				{
					LScriptCallback &Cb = s.Callbacks[i];
					if (Cb.Param == Msg)
					{
						LScriptArguments Args(NULL);

						Args[0] = new LVariant(App);
						Args[1] = new LVariant(this);
						Args[2] = new LVariant(Cb.Param);
							
						App->ExecuteScriptCallback(Cb, Args);
					}
				}
			}
			break;
		}
	}
}

bool ScribeFolder::IsInTrash()
{
	ScribeFolder *p = this;
	while ((p = p->GetFolder()))
	{
		if (p->GetItemType() == MAGIC_ANY)
			return true;
	}

	return false;
}

/// This just adds certain folder types as a group ware source at the app level
void ScribeFolder::OnItemType(bool load)
{
	switch (GetItemType())
	{
		case MAGIC_CONTACT:
		case MAGIC_FILTER:
		case MAGIC_CALENDAR:
		case MAGIC_GROUP:
			if (load)
			{
				App->AddThingSrc(this);

				// This is mainly to debug when LDataFolderI objects get deleted before
				// the matching ScribeFolder gets deleted. This can cause problems in 
				ObjectLock = true;
				// LgiTrace("%s:%i - AddThingSrc %p=%s\n", _FL, this, GetPath().Get());
			}
			else
			{
				ObjectLock = false;
				App->RemoveThingSrc(this);
			}
			break;
		default:
			break;
	}
}

bool ScribeFolder::LoadFolders()
{
	// static int64 Last = 0;

	auto FldObj = GetFldObj();
	if (!FldObj)
		return true;

	CurState = FldState_Loading;
	FoldersCurrentlyLoading++;
		
	LHashTbl<PtrKey<LDataI*>, ScribeFolder*> Loaded;
	for (ScribeFolder *c = GetChildFolder(); c; c = c->GetNextFolder())
	{
		Loaded.Add(c->GetObject(), c);
	}

	LDataFolderI *s = NULL;
	auto &Sub = FldObj->SubFolders();
	for (unsigned sIdx = 0;
			Sub.GetState() == Store3Loaded &&
			sIdx < Sub.Length();
			sIdx++)
	{
		if (!(s = Sub[sIdx]))
			break;

		if (!Loaded.Find(s))
		{
			if (auto n = new ScribeFolder)
			{
				n->App = App;
				LAssert(s);
				n->SetObject(s, false, _FL);
				Insert(n);

				SetWillDirty(false);
				Expanded(GetOpen() != 0);
				SetWillDirty(true);

				n->LoadFolders();
				n->OnItemType(true);
			}

			#if 0
			int64 Now = LCurrentTime();
			if (Now - Last > 500)
			{
				Last = Now;
			}
			#endif
		}
	}

	LTreeItem::Items.Sort(ThingContainerIdxCmp);
		
	CurState = FldState_Idle;
	FoldersCurrentlyLoading--;

	return true;
}

class LoadProgressItem : public LListItem
{
	int n;
	int Total;
	int64 Last;

public:
	LoadProgressItem(int t)
	{
		n = 0;
		Total = t;
		Last = LCurrentTime();
	}

	void SetPos(int i)
	{
		n = i;

		if (n % 32 == 0)
		{
			int64 Now = LCurrentTime();
			if (Now > Last + 500)
			{
				if (Parent)
					Parent->Invalidate(&Pos, true);
				Last = Now;
			}
			LSleep(0);
		}
	}

	void OnPaint(ItemPaintCtx &Ctx)
	{
		int x = n * 150 / Total;
		LRect &r = Ctx;

		Ctx.pDC->Colour(L_FOCUS_SEL_BACK);
		Ctx.pDC->Rectangle(r.x1, r.y1, r.x1 + x, r.y2);
		Ctx.pDC->Colour(L_MED);
		Ctx.pDC->Rectangle(r.x1 + x + 1, r.y1, r.x1 + 150, r.y2);
		Ctx.pDC->Colour(L_WORKSPACE);
		Ctx.pDC->Rectangle(r.x1 + 151, r.y1, r.x2, r.y2);
	}
};

class LoadingItem : public LListItem
{
	LDataIterator<LDataI*> *Iter;
	LString s;

public:
	LoadingItem(LDataIterator<LDataI*> *it)
	{
		_UserPtr = NULL;
		if ((Iter = it))
		{
			Iter->SetProgressFn([this](ssize_t pos, ssize_t sz)
			{
				this->s.Printf("%.1f%%", (double)pos * 100 / sz);
				this->Update();
			});
		}
	}

	~LoadingItem()
	{
		if (Iter)
			Iter->SetProgressFn(NULL);
	}

	bool SetText(const char *str, int i)
	{
		s = str;
		Update();
		return true;
	}

	void OnPaint(LItem::ItemPaintCtx &Ctx)
	{
		LString msg;
		auto loading = LLoadString(IDS_LOADING);
		if (s)
			msg.Printf("%s %s", loading, s.Get());
		else
			msg = loading;

		LDisplayString d(LSysFont, msg);
		LSysFont->Colour(L_TEXT, L_WORKSPACE);
		LSysFont->Transparent(false);
		d.Draw(Ctx.pDC, (Ctx.X()-d.X())/2, Ctx.y1, &Ctx);
	}

	void Select(bool b)
	{
	}
};

bool ScribeFolder::UnloadThings()
{
	bool Status = true;

	if (IsLoaded())
	{
		// LgiTrace("Unloading %s, Items=%i\n", GetPath(), Items.Length());

		// Emptying the item list, leave the store nodes around though
		Thing *t;
		while ((t = Items[0]))
		{
			if (t->GetDirty())
			{
				t->Save(0);
			}

			t->SetObject(NULL, false, _FL);
			t->DecRef();
		}

		Items.Empty();
		IsLoaded(false);
		GetObject()->SetInt(FIELD_LOADED, false);
	}

	return Status;
}

#define DEBUG_PROF_LOAD_THINGS		0
#if DEBUG_PROF_LOAD_THINGS
	#define PROFILE(str) 			prof.Add("Add");
.Add(str)
#else
	#define PROFILE(str)
#endif

void ScribeFolder::ContinueLoading(int OldUnread, std::function<void(Store3Status)> Callback, bool waitForResults)
{
	auto FldObj = GetFldObj();

	WhenLoaded(_FL,
		[this, OldUnread, Callback, FldObj](auto loadStatus)
		{
			// This is called when all the Store3 objects are loaded
			int Unread = OldUnread;
			if (Unread < 0)
				Unread = GetUnRead();

			Loading.Reset();

			if (loadStatus == Store3Success)
			{
				auto &Children = GetFldObj()->Children();
				if (Children.GetState() != Store3Loaded)
				{
					LAssert(!"Really should be loaded by now.");
					return;
				}

				for (auto c = Children.First(); c; c = Children.Next())
				{
					auto t = CastThing(c);
					if (t)
					{
						// LAssert(Items.HasItem(t));
					}
					else if ((t = App->CreateThingOfType((Store3ItemTypes) c->Type(), c)))
					{
						t->SetObject(c, false, _FL);
						t->SetParentFolder(this);
						t->OnSerialize(false);
					}
				}
			}

			int NewUnRead = 0;
			for (auto t: Items)
			{
				if (t->GetFolder() != this)
				{
					#ifdef _DEBUG
					char s[256];
					sprintf_s(s, sizeof(s),
						"%s:%i - Error, thing not parented correctly: this='%s', child='%x'\n",
						_FL,
						GetText(0),
						t->GetObject() ? t->GetObject()->Type() : 0);
					printf("%s", s);
					LgiMsg(App, s, AppName);
					#endif
			
					t->SetFolder(this, NULL);
				}

				Mail *m = t->IsMail();
				if (m)
					NewUnRead += (m->GetFlags() & MAIL_READ) ? 0 : 1;

				t->SetFieldArray(FieldArray);
			}

			if (Unread != NewUnRead)
				OnUpdateUnRead(NewUnRead - Unread, false);

			Update();

			if (d->IsInbox < 0 && App)
			{
				d->IsInbox = App->GetFolder(FOLDER_INBOX) == this;
				if (d->IsInbox > 0)
					UpdateOsUnread();
			}

			if (Callback)
				Callback(loadStatus);
		},
		0,
		waitForResults);

	if (!IsLoaded())
	{
		bool Ui = Tree ? Tree->InThread() : false;
		if (Ui)
			Tree->Capture(false);

		auto &Children = FldObj->Children();
		auto Status = Children.GetState();
		if (Status != Store3Loaded)
		{
			if (View() && Loading.Reset(Ui ? new LoadingItem(&Children) : NULL))
				View()->Insert(Loading);

			return; // Ie deferred or error...
		}

		IsLoaded(true);
	}
}

Store3Status ScribeFolder::LoadThings(LViewI *Parent, std::function<void(Store3Status)> Callback, bool waitForResults)
{
	int OldUnRead = GetUnRead();

	auto FldObj = GetFldObj();
	if (!FldObj)
	{
		LgiTrace("%s:%i - No folder object.\n", _FL);
		if (Callback)
			Callback(Store3Error);
		return Store3Error;
	}

	if (!Parent)
		Parent = App;

	auto Path = GetPath();
	if (!App || !Path)
	{
		LAssert(!"We should probably always have an 'App' and 'Path' ptrs...");
		if (Callback)
			Callback(Store3Error);
		return Store3Error;
	}

	auto Access = App->GetAccessLevel(
		Parent,
		GetReadAccess(),
		Path,
		[this, OldUnRead, Callback, waitForResults](auto access)
		{
			if (access)
				ContinueLoading(OldUnRead, Callback, waitForResults);
			else if (Callback)
				Callback(Store3NoPermissions);
		});

	if (Access == Store3Error)
	{		
		// No read access:
		LgiTrace("%s:%i - Folder read access denied.\n", _FL);

		// Emptying the item list, leave the store nodes around though
		for (auto t: Items)
			t->SetObject(NULL, false, _FL);

		Items.Empty();
		IsLoaded(false);
		
		Update();
	}
		
	return Access;
}

void ScribeFolder::OnRename(char *NewName)
{
	if (!NewName)
		return;

	int FolderType = App->GetFolderType(this);

	SetName(NewName, true); // Calls update too..
	SetDirty();

	if (FolderType >= 0)
	{
		// it's a system folder so reflect the changes in
		// the system folder tracking options.
		char KeyName[32];
		sprintf_s(KeyName, sizeof(KeyName), "Folder-%i", FolderType);
		auto Path = GetPath();
		if (Path)
		{
			LVariant v = Path.Get();
			App->GetOptions()->SetValue(KeyName, v);
		}
	}
}

void ScribeFolder::OnDelete()
{
	if (GetObject())
	{
		char Msg[256];
 		sprintf_s(Msg, sizeof(Msg), LLoadString(IDS_DELETE_FOLDER_DLG), GetText());
		int Result = LgiMsg(App, Msg, AppName, MB_YESNO);
		if (Result == IDYES)
		{
			if (App->GetMailList())
				App->GetMailList()->RemoveAll();

			ScribeFolder *Parent = GetFolder();
			LArray<LDataI*> Del;
			Del.Add(GetObject());
			if (GetObject()->GetStore()->Delete(Del, true))
			{
				if (Parent)
					Parent->Select(true);
			}
		}
	}
}

bool ScribeFolder::ReindexField(int OldIndex, int NewIndex)
{
	if (!GetFldObj())
		return false;
	
	auto &Flds = GetFldObj()->Fields();
	if (GetFldObj()->Fields().Length() <= 0)
		return false;

	LDataPropI *f = Flds[OldIndex];
	if (!f)
		return false;

	Flds.Delete(f);
	Flds.Insert(f, OldIndex < NewIndex ? NewIndex - 1 : NewIndex);

	int i=0;
	FieldArray.Length(0);
	for (f = Flds.First(); f; f = Flds.Next())
	{
		auto Id = f->GetInt(FIELD_ID);
		FieldArray[i++] = (int)Id;
	}

	for (auto t: Items)
		t->SetFieldArray(FieldArray);

	SetDirty();
	return true;
}

int ScribeFolder::_UnreadChildren()
{
	int Status = 0;

	for (ScribeFolder *f=GetChildFolder(); f; f=f->GetNextFolder())
	{
		int u = f->GetUnRead();
		if (u > 0)
			Status += u;

		Status += f->_UnreadChildren();
	}

	return Status;
}

void ScribeFolder::_PourText(LPoint &Size)
{
	if (!d)
		return;

	const char *Text = GetText();
	Size.x = Size.y = 0;
	
	if (Text && !d->DsBase)
	{
		if (GetUnRead() > 0 || ChildUnRead)
		{
			const char *StartCount = strrchr(Text, '(');
			ssize_t NameLen = StartCount ? StartCount-Text : strlen(Text);

			LFont *b = (App->GetBoldFont()) ? App->GetBoldFont() : GetTree()->GetFont();

			d->DsBase.Reset(new LDisplayString(b, Text, NameLen));
			d->DsUnread.Reset(new LDisplayString(GetTree()->GetFont(), StartCount));
		}
		else
		{
			d->DsBase.Reset(new LDisplayString(GetTree()->GetFont(), Text));
		}
	}
	
	if (d->DsBase)
		Size.x += d->DsBase->X();
	if (d->DsUnread)
		Size.x += d->DsUnread->X();

	Size.x += 4;
	Size.y = MAX(16, LSysFont->GetHeight());
}

void ScribeFolder::_PaintText(LItem::ItemPaintCtx &Ctx)
{
	if (!d)
		return;
		
	LRect *_Text = _GetRect(TreeItemText);
	if (d->DsBase)
	{
		LFont *f = d->DsBase->GetFont();
		int Tab = f->TabSize();
		f->TabSize(0);
		f->Transparent(false);
		f->Colour(Ctx.Fore, Ctx.TxtBack);

		d->DsBase->Draw(Ctx.pDC, _Text->x1 + 2, _Text->y1 + 1, _Text);
		if (d->DsUnread)
		{
			f = d->DsUnread->GetFont();
			f->Transparent(true);

			LColour UnreadCol(App->GetColour(L_UNREAD_COUNT));
			if
			(
				abs
				(
					(UnreadCol.GetGray() - Ctx.Back.GetGray())
				)
				< 80
			)
			{
				// too close.. use fore
				f->Colour(Ctx.Fore, Ctx.TxtBack);
			}
			else
			{
				// contrast ok.. use unread
				f->Colour(UnreadCol, Ctx.TxtBack);
			}
			
			d->DsUnread->Draw(Ctx.pDC, _Text->x1 + 2 + d->DsBase->X(), _Text->y1 + 1);
		}

		f->TabSize(Tab);
		
		if (Ctx.x2 > _Text->x2)
		{
			Ctx.pDC->Colour(Ctx.Back);
			Ctx.pDC->Rectangle(_Text->x2 + 1, Ctx.y1, Ctx.x2, Ctx.y2);
		}
	}
	else
	{
		Ctx.pDC->Colour(Ctx.Back);
		Ctx.pDC->Rectangle(&Ctx);
	}
}

bool ScribeFolder::Save(ScribeFolder *Into)
{
	bool Status = false;
	
	if (GetObject())
	{
		// saving a disk object
		Store3Status s = GetObject()->Save();
		if (s != Store3Error)
		{
			Status = true;
			SetDirty(false);
		}
		else
		{
			LAssert(0);
		}
	}

	return Status;
}

void ScribeFolder::OnExpand(bool b)
{
	if (b && LoadOnDemand)
	{
		DeleteObj(LoadOnDemand);
		
		if (App->ScribeState != ScribeWnd::ScribeExiting)
		{
			ScribeWnd::AppState Prev = App->ScribeState;
			App->ScribeState = ScribeWnd::ScribeLoadingFolders;
			LoadFolders();
			if (App->ScribeState == ScribeWnd::ScribeExiting)
			{
				LCloseApp();
				return;
			}
			App->ScribeState = Prev;
		}
	}

	OnUpdateUnRead(0, false);

	SetDirty(((uchar)b) != GetOpen());
	SetOpen(b);
}

bool ScribeFolder::OnKey(LKey &k)
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
			m.Target = GetTree();
			DoContextMenu(m);
		}
		return true;
	}
	#endif
	
	return false;
}

#define DefField(id, wid) \
{ \
	LDataPropI *Fld = Fields.Create(GetFldObj()->GetStore()); \
	if (Fld) \
	{ \
		Fld->SetInt(FIELD_ID, id); \
		Fld->SetInt(FIELD_WIDTH, wid); \
		Fields.Insert(Fld); \
	} \
}

void ScribeFolder::SetDefaultFields(bool Force)
{
	if (!GetFldObj())
		return;

	if (Force || GetFldObj()->Fields().Length() <= 0)
	{
		LDataIterator<LDataPropI*> &Fields = GetFldObj()->Fields();
		Fields.DeleteObjects();

		int FolderType = App ? App->GetFolderType(this) : -1;
		if (FolderType == FOLDER_OUTBOX ||
			FolderType == FOLDER_SENT)
		{
			DefField(FIELD_PRIORITY, 10);
			DefField(FIELD_FLAGS, 15);
					
			DefField(FIELD_TO, 150);
			DefField(FIELD_SUBJECT, 250);
			DefField(FIELD_SIZE, 80);
			DefField(FIELD_DATE_SENT, 100);
		}
		else
		{
			switch (GetItemType())
			{
				case MAGIC_MAIL:
				{
					DefField(FIELD_PRIORITY, 10);
					DefField(FIELD_FLAGS, 10);
					
					DefField(FIELD_FROM, 150);
					DefField(FIELD_SUBJECT, 250);
					DefField(FIELD_SIZE, 80);
					DefField(FIELD_DATE_SENT, 100);
					break;
				}
				case MAGIC_CONTACT:
				{
					DefField(FIELD_FIRST_NAME, 200);
					DefField(FIELD_LAST_NAME, 200);
					DefField(FIELD_EMAIL, 300);
					break;
				}
				case MAGIC_CALENDAR:
				{
					DefField(FIELD_CAL_SUBJECT, 200);
					DefField(FIELD_CAL_START_UTC, 150);
					DefField(FIELD_CAL_END_UTC, 150);
					break;
				}
				case MAGIC_FILTER:
				{
					for (int i=0; DefaultFilterFields[i]; i++)
					{
						DefField(DefaultFilterFields[i], i == 0 ? 200 : 100);
					}
					break;
				}
				case MAGIC_GROUP:
				{
					DefField(FIELD_GROUP_NAME, 300);
					break;
				}
				default:
					break;
			}
		}

		// set for save
		SetDirty(Fields.Length() > 0);
	}
	else
	{
		// already has fields, so don't interfere with them
	}
}

LString ScribeFolder::GetPath()
{
	LString::Array p;

	ScribeFolder *f = this;
	while (f)
	{
		auto name = f->GetName(true);
		if (name)
			p.Add(name);
		f = dynamic_cast<ScribeFolder*>(f->GetParent());
	}

	LString dir = "/";
	return dir + dir.Join(p.Reverse());
}

void ScribeFolder::SetName(const char *Name, bool Encode)
{
	if (Name)
	{
		d->DsBase.Reset();
		d->DsUnread.Reset();
		NameCache.Reset();

		if (GetObject())
			GetObject()->SetStr(FIELD_FOLDER_NAME, Name);
		SetText(Name); // Calls update
	}
}

LString ScribeFolder::GetName(bool Decode)
{
	if (!GetObject())
		return LString();

	auto In = GetObject()->GetStr(FIELD_FOLDER_NAME);
	if (!In)
		return LString();

	if (Decode)
		return LUrlDecode(In);
	else
		return In;
}

void ScribeFolder::Update()
{
	if (Tree && !Tree->InThread())
	{
		// LgiTrace("%s:%i - Update not in thread?\n", _FL);
		return;
	}

	d->DsBase.Reset();
	d->DsUnread.Reset();
	NameCache.Reset();
	LTreeItem::Update();
}

const char *ScribeFolder::GetText(int i)
{
	if (!NameCache)
	{
		LString Name = GetName(true);
		if (!Name)
			return "...loading...";
		
		size_t NameLen = strlen(Name) + 40;
		NameCache.Reset(new char[NameLen]);
		if (NameCache)
		{
			bool ShowTotals = false;
			LVariant v;
			if (App->GetOptions()->GetValue(OPT_ShowFolderTotals, v))
				ShowTotals = v.CastInt32() != 0;

			int c = sprintf_s(NameCache, NameLen, "%s", Name.Get());
			
			auto ItemType = GetItemType();
			if (ItemType &&
				(ShowTotals || GetUnRead() || ChildUnRead))
			{			
				c += sprintf_s(NameCache+c, NameLen-c, " (");
				if (ItemType == MAGIC_MAIL ||
					ItemType == MAGIC_ANY)
				{
					const char *Ch = ChildUnRead ? "..." : "";

					if (ShowTotals)
						c += sprintf_s(NameCache+c, NameLen-c, "%i%s/", GetUnRead(), Ch);
					else if (GetUnRead())
						c += sprintf_s(NameCache+c, NameLen-c, "%i%s", GetUnRead(), Ch);
					else if (ChildUnRead)
						c += sprintf_s(NameCache+c, NameLen-c, "...");
				}
				if (ShowTotals)
				{
					if (IsLoaded())
						c += sprintf_s(NameCache+c, NameLen-c, "%zi", Items.Length());
					else
						c += sprintf_s(NameCache+c, NameLen-c, "?");
				}
				c += sprintf_s(NameCache+c, NameLen-c, ")");
			}
		}
	}

	return NameCache;
}

int ScribeFolder::GetImage(int Flags)
{
	if (GetItemType() == MAGIC_ANY)
	{
		return ICON_TRASH;
	}

	if (Flags)
	{
		return ICON_OPEN_FOLDER;
	}

	switch (GetItemType())
	{
		case MAGIC_MAIL:
			return ICON_FOLDER_MAIL;
		case MAGIC_CONTACT:
			return ICON_FOLDER_CONTACTS;
		case MAGIC_FILTER:
			return ICON_FOLDER_FILTERS;
		case MAGIC_CALENDAR:
			return ICON_FOLDER_CALENDAR;
		case MAGIC_NONE:
			return ICON_MAILBOX;
		default:
			return ICON_CLOSED_FOLDER;
	}
}

class NullMail : public Mail
{
	Thing &operator =(Thing &c) override
	{
		return *this;
	}

public:
	NullMail(ScribeWnd *App, MContainer *c, ScribeFolder *f) : Mail(App)
	{
		SetFlags(MAIL_READ | MAIL_CREATED);
		Container = c;
		SetParentFolder(f);
		SetSubject((char*)LLoadString(IDS_MISSING_PARENT));
	}

	~NullMail()
	{
		SetParentFolder(NULL);
	}

	bool IsPlaceHolder() override
	{
		return true;
	}
	
	const char *GetText(int i) override
	{
		// Clear all the fields
		return 0;
	}

	int Compare(LListItem *Arg, ssize_t Field) override
	{
		// Use the first mail to sort into position
		if (Container)
		{
			MContainer *c = Container->Children.Length() ? Container->Children[0] : 0;
			if (c && c->Message)
			{
				return c->Message->Compare(Arg, Field);
			}
		}

		return -1;
	}

	void OnMouseClick(LMouse &m) override
	{
		// Disable the mouse click
	}

	bool SetDirty(bool b = true) override
	{
		// Don't set it to dirty, otherwise the dirty object
		// clean up code will save it into the outbox.
		return true;
	}
};

template <class T>
int TrashCompare(T *pa, T *pb, NativeInt Data)
{
	ScribeFolder *f = (ScribeFolder*)Data;
	Thing *a = dynamic_cast<Thing*>(pa);
	Thing *b = dynamic_cast<Thing*>(pb);
	if (!a || !b)
		return 0;

	int type = a->Type() - b->Type();
	if (type)
		return type;

	int col = f->GetSortCol();
	int *defs = a->GetDefaultFields();
	if (!defs || !defs[col])
		return 0;

	return (f->GetSortAscend() ? 1 : -1) * a->Compare(b, defs[col]);
}

template int TrashCompare<LListItem>(LListItem *pa, LListItem *pb, NativeInt Data);

int ListItemCompare(LListItem *a, LListItem *b, NativeInt Data)
{
	ScribeFolder *f = (ScribeFolder*)Data;
	return (f->GetSortAscend() ? 1 : -1) * a->Compare(b, f->GetSortField());
}

int ThingCompare(Thing *a, Thing *b, NativeInt Data)
{
	ScribeFolder *f = (ScribeFolder*)Data;
	return (f->GetSortAscend() ? 1 : -1) * a->Compare(b, f->GetSortField());
}

int ThingSorter(Thing *a, Thing *b, ThingSortParams *Params)
{
	return (Params->SortAscend ? 1 : -1) * a->Compare(b, Params->SortField);
}

bool ScribeFolder::Thread()
{
	bool Status = false;

	if (GetItemType() == MAGIC_MAIL)
	{
		int Thread = GetThreaded();
		ThingList *Lst = View();

		List<Mail> m;
		for (auto t: Items)
		{
			Mail *e = t->IsMail();
			if (e)
			{
				if (Thread)
				{
					m.Insert(e);
				}
				else if (e->Container)
				{
					DeleteObj(e->Container);
				}
			}
		}

		if (Lst && m[0])
		{
			// Thread
			LArray<MContainer*> Containers;
			MContainer::Thread(m, Containers);
			
			LAssert(m.Length() == Items.Length());

			// Insert blank items for missing thread parents
			for (unsigned i=0; i<Containers.Length(); i++)
			{
			    MContainer *c = Containers[i];
				if (!c->Message)
				{
					LAssert(c->Children.Length() > 1);

					c->Message = new NullMail(App, c, this);
					if (c->Message)
					{
						Lst->PlaceHolders.Insert(c->Message);
						if (View() && c->Message)
						{
							c->Message->SetFieldArray(FieldArray);
							Items.Insert(c->Message);
						}
					}
				}
			}
			
			// Sort root list
			LArray<MContainer*> Containers2 = Containers;
			ThingSortParams Params;
			Params.SortAscend = GetSortAscend();
			Params.SortField = GetSortField();

            #if 0
			for (int i=0; i<Containers.Length(); i++)
			{
				// LAssert(Containers[i] == Containers2[i]);
				char *Str1 = Containers[i]->Message ? Containers[i]->Message->GetFieldText(Params.SortField) : NULL;
				LgiTrace("%p(%s)\n", Containers[i], Str1);
			}

			Containers.Sort(ContainerCompare);

			LQuickSort(Base, Containers2.Length(), ContainerSorter, &Params);
			
			for (int i=0; i<Containers.Length(); i++)
			{
				// LAssert(Containers[i] == Containers2[i]);
				char *Str1 = Containers[i]->Message ? Containers[i]->Message->GetFieldText(Params.SortField) : NULL;
				char *Str2 = Containers2[i]->Message ? Containers2[i]->Message->GetFieldText(Params.SortField) : NULL;
				LgiTrace("%p(%s), %p(%s) - %s\n", Containers[i], Str1, Containers2[i], Str2, Containers[i]!=Containers2[i]?"DIFFERENT":"");
			}
			#else
			MContainer **Base = &Containers[0];
	        if (Containers.Length() > 1)
	    		LQuickSort(Base, Containers.Length(), ContainerSorter, &Params);

            /*
			for (int i=0; i<Containers.Length(); i++)
			{
				char *Str1 = Containers[i]->Message ? Containers[i]->Message->GetFieldText(Params.SortField) : NULL;
				LgiTrace("%p(%s)\n", Containers[i]->Message, Str1);
			}
			LgiTrace("\n");
			*/
    	    #endif

			// Position and index the thread tree
			int Index = 0;
			for (unsigned i=0; i<Containers.Length(); i++)
			{
				Containers[i]->Pour(Index, 0, 0, i < Containers.Length()-1, &Params);
			}
			
			// Sort all the items by index			
			Items.Sort(ContainerIndexer);
			Status = true;
			
			/*
			int Idx = 0;
			for (Thing *t = Items.First(); t; t = Items.Next(), Idx++)
			{
			    Mail *m = t->IsMail();
			    if (m)
			    {
			        char *Str = m->GetFieldText(Params.SortField);
			        LgiTrace("%i,%i %p %s\n", Idx, m->Container ? m->Container->Index : -1, m, Str);
			    }
			}
			*/
		}
	}
	else
	{
		for (auto t: Items)
		{
			Mail *e = t->IsMail();
			if (e)
				DeleteObj(e->Container);
		}
	}

	return Status;
}

struct SortPairInt
{
	Thing *t;
	uint64 ts;

	void SetDate(Thing *th, int sf)
	{
		t = th;
		auto obj = th->GetObject();
		// Store3State loaded = (Store3State)obj->GetInt(FIELD_LOADED);
		auto dt = obj->GetDate(sf);
		ts = dt && dt->Year() ? dt->Ts().Get() : 0;
	}

	void SetInt(Thing *th, int sf)
	{
		t = th;
		auto obj = th->GetObject();
		// Store3State loaded = (Store3State)obj->GetInt(FIELD_LOADED);
		ts = obj->GetInt(sf);
	}

	static int Compare(SortPairInt *a, SortPairInt *b)
	{
		int64_t diff = (int64_t)a->ts - (int64_t)b->ts;
		if (diff < 0) return -1;
		return diff > 0 ? 1 : 0;
	}
};

struct SortPairStr
{
	Thing *t;
	const char *ts;

	void SetStr(Thing *th, int sf)
	{
		t = th;
		ts = th->GetObject()->GetStr(sf);
	}

	static int Compare(SortPairStr *a, SortPairStr *b)
	{
		return stricmp(a->ts ? a->ts : "", b->ts ? b->ts : "");
	}
};

bool ScribeFolder::SortItems()
{
	int sf = GetSortField();
	LVariantType type = GV_NULL;
	auto StartTs = LCurrentTime();
	const static int TimeOut = 2/*sec*/ * 1000;

	switch (sf)
	{
		case FIELD_DATE_SENT:
		case FIELD_DATE_RECEIVED:
			type = GV_DATETIME;
			break;
		case FIELD_SUBJECT:
			type = GV_STRING;
			break;
		default:			
			return false;
	}

	LArray<SortPairInt> intPairs;
	LArray<SortPairStr> strPairs;
	bool intType =	type == GV_DATETIME ||
					type == GV_INT32 ||
					type == GV_INT64;
	if (intType)
	{
		intPairs.Length(Items.Length());
		int n = 0;
		auto s = intPairs.AddressOf();
		
		if (type == GV_DATETIME)
		{
			for (auto i: Items)
			{
				s[n++].SetDate(i, sf);
				if (n % 50 == 0 && LCurrentTime() - StartTs >= TimeOut)
					return false;
			}
		}
		else
		{
			for (auto i: Items)
			{
				s[n++].SetInt(i, sf);
				if (n % 50 == 0 && LCurrentTime() - StartTs >= TimeOut)
					return false;
			}
		}
	
		intPairs.Sort(SortPairInt::Compare);
	}
	else if (type == GV_STRING)
	{
		strPairs.Length(Items.Length());
		int n = 0;
		auto s = strPairs.AddressOf();
		
		for (auto i: Items)
			s[n++].SetStr(i, sf);
	
		strPairs.Sort(SortPairStr::Compare);
	}

	Items.Empty();

	if (intType)
	{
		if (GetSortAscend())
			for (auto i: intPairs) Items.Add(i.t);
		else
			for (auto it = intPairs.rbegin(); it != intPairs.end(); it--)
				Items.Add((*it).t);
	}
	else
	{
		if (GetSortAscend())
			for (auto i: strPairs) Items.Add(i.t);
		else
			for (auto it = strPairs.rbegin(); it != strPairs.end(); it--)
				Items.Add((*it).t);
	}

	return true;
}

void ScribeFolder::Populate(ThingList *list)
{
LProfile Prof("ScribeFolder::Populate", 1000);

	App->OnSelect();

	if (!GetFldObj() || !list)
		return;

	CurState = FldState_Populating;
		
	ScribeFolder *Prev = list->GetContainer();
	bool Refresh = Prev == this;

	// Remove old items from list
Prof.Add("Delete Placeholders");
	list->DeletePlaceHolders();
	list->RemoveAll();

	if (!Refresh || list->GetColumns() == 0 || GetItemType() == MAGIC_FILTER)
	{
		if (Prev)
		{
			// save previous folders settings
			Prev->SerializeFieldWidths();
		}

Prof.Add("Empty cols");
		LVariant GridLines;
		if (App->GetOptions()->GetValue(OPT_GridLines, GridLines))
		{
			list->DrawGridLines(GridLines.CastInt32() != 0);
		}
		list->EmptyColumns();

Prof.Add("Set def fields");
		bool ForceDefaultFields = GetItemType() == MAGIC_FILTER;
		if (GetFldObj()->Fields().Length() <= 0 || ForceDefaultFields)
		{
			SetDefaultFields(ForceDefaultFields);
		}
			
		// Add fields to list view
		int n = 0;
		LArray<int> Empty;
		for (auto t: Items)
		{
			t->SetFieldArray(Empty);
		}
		LRect *Bounds = 0;
		if (App->GetIconImgList())
		{
			Bounds = App->GetIconImgList()->GetBounds();
		}

		switch (GetItemType())
		{
			case MAGIC_ANY:
			{
				list->AddColumn("", 170);
				list->AddColumn("", 170);
				list->AddColumn("", 170);
				list->AddColumn("", 170);
				break;
			}
			default:
			{
				n = 0;
				FieldArray.Length(0);
					
				for (LDataPropI *i = GetFldObj()->Fields().First(); i; i = GetFldObj()->Fields().Next())
				{
					int FieldId = (int)i->GetInt(FIELD_ID);
					const char *FName = LLoadString(FieldId);
					int Width = (int)i->GetInt(FIELD_WIDTH);
					const char *FieldText = FName ? FName : i->GetStr(FIELD_NAME);
					LAssert(FieldText != NULL);
						
					LItemColumn *c = list->AddColumn(FieldText, Width);
					if (c)
					{
						switch (i->GetInt(FIELD_ID))
						{
							case FIELD_PRIORITY:
							{
								int x = 12;
								if (Bounds)
								{
									x = Bounds[ICON_PRIORITY_BLACK].X() + 7;
								}							
								c->Width(x);

								c->Image(ICON_PRIORITY_BLACK);
								c->Resizable(false);
								break;
							}
							case FIELD_FLAGS:
							{
								int x = 14;
								if (Bounds)
								{
									x = Bounds[ICON_FLAGS_BLACK].X() + 7;
								}							
								c->Width(x);

								c->Image(ICON_FLAGS_BLACK);
								c->Resizable(false);
								break;
							}
							case FIELD_SIZE:
							{
								c->TextAlign(LCss::Len(LCss::AlignRight));
								break;
							}
						}
					}
						
					FieldArray[n++] = (int)i->GetInt(FIELD_ID);
				}
				break;
			}
		}

		// Add all items to list
		if (View())
		{
			View()->SetContainer(this);

			// tell the list who we are
			if (GetSortCol() >= 0)
			{
				// set current sort settings
				View()->SetSort(GetSortCol(), GetSortAscend());
			}
		}

Prof.Add("Load things");
		// FIXME:
		LoadThings();
	}

	// Filter
	List<LListItem> Is;

	// Do any threading/sorting
	static LString SortMsg;
	if (!Thread() &&
		GetSortField())
	{
		SortMsg.Printf("Sorting " LPrintfInt64 " items", Items.Length());
Prof.Add(SortMsg);
		if (GetItemType() == MAGIC_ANY)
		{
			Items.Sort(TrashCompare<Thing>, (NativeInt)this);
		}
		else
		{
			// Sort..
			// if (!SortItems())
			Items.Sort(ThingCompare, (NativeInt)this);
		}
	}

	// Do any filtering...
Prof.Add("Filtering");
	ThingFilter *Filter = App->GetThingFilter();
	auto FilterStart = LCurrentTime();
	size_t Pos = 0;
	for (auto t: Items)
	{
		t->SetFieldArray(FieldArray);
		if (!Filter || Filter->TestThing(t))
		{
			// Add anyway... because all items are not part of list
			Is.Insert(t);
		}

		if ((LCurrentTime()-FilterStart) > 300)
		{
			if (!Loading && Loading.Reset(new LoadingItem(NULL)))
				list->Insert(Loading, 0);
			if (Loading)
			{
				LString s;
				s.Printf(LPrintfInt64 " of " LPrintfInt64 ", %.1f%%",
					Pos, Items.Length(), (double)Pos * 100 / Items.Length());
				Loading->SetText(s);
			}
			FilterStart = LCurrentTime();
		}

		Pos++;
	}

Prof.Add("Inserting");
	if (View() && Is[0])
	{
		View()->Insert(Is, -1, true);
	}

Prof.Add("Deleting");
	list->DeletePlaceHolders();
	Loading.Reset();

Prof.Add("OnSelect");
	GetFldObj()->OnSelect(true);
	CurState = FldState_Idle;
}

void ScribeFolder::OnUpdateUnRead(int Offset, bool ScanItems)
{
	if (!d->InUpdateUnread)
	{
		d->InUpdateUnread = true;

		int OldUnRead = GetUnRead();

		d->DsBase.Reset();
		d->DsUnread.Reset();
		NameCache.Reset();

		if (ScanItems)
		{
			if (GetItemType() == MAGIC_MAIL ||
				GetItemType() == MAGIC_ANY)
			{
				size_t Count = 0;

				for (auto t: Items)
				{
					Mail *m = t->IsMail();
					if (m && !TestFlag(m->GetFlags(), MAIL_READ))
						Count++;
				}

				SetUnRead((int32_t)Count);
			}
		}
		else if (Offset != 0)
		{
			SetUnRead(GetUnRead() + Offset);
		}

		if (GetUnRead() < 0)
			SetUnRead(0);

		if (OldUnRead != GetUnRead())
		{
			for (auto i = GetParent(); i; i = i->GetParent())
			{
				ScribeFolder *tc = dynamic_cast<ScribeFolder*>(i);
				if (tc && tc->GetParent())
					tc->OnUpdateUnRead(0, false);
			}

			SetDirty();
			if (d->IsInbox > 0)
				UpdateOsUnread();
		}
	
		ChildUnRead = Expanded() ? 0 : _UnreadChildren();
		Update();

		d->InUpdateUnread = false;
	}
}

void ScribeFolder::EmptyFieldList()
{
	if (GetFldObj())
		GetFldObj()->Fields().DeleteObjects();
}

void ScribeFolder::SerializeFieldWidths(bool Write)
{
	if (GetFldObj() && View())
	{
		// LAssert(View()->GetColumns() == Object->Fields().Length());
		
		int Cols = MIN(View()->GetColumns(), (int)GetFldObj()->Fields().Length());
		for (int i=0; i<Cols; i++)
		{
			LItemColumn *c = View()->ColumnAt(i);
			LDataPropI *f = GetFldObj()->Fields()[i];
			if (c && f)
			{
				if (f->GetInt(FIELD_WIDTH) != c->Width())
				{
					if (Write)
					{
						c->Width((int)f->GetInt(FIELD_WIDTH));
					}
					else
					{
						f->SetInt(FIELD_WIDTH, c->Width());
						SetDirty();
					}
				}
			}
			else LAssert(0);
		}
	}
}

void ScribeFolder::OnProperties(int Tab)
{
	if (!GetObject())
		return;

	SerializeFieldWidths();
	if (View())
	{
		SetSort(View()->GetSortCol(), View()->GetSortAscending());
	}

	OpenFolderProperties(this, Tab, [this](auto repop)
	{
		if (repop)
		{
			SetDirty();
			SerializeFieldWidths(true);
			Populate(View());
		}
	});
}

ScribeFolder *ScribeFolder::GetSubFolder(const char *Path)
{
	if (!Path)
		return NULL;

	if (*Path == '/')
		Path++;

	ScribeFolder *Folder = NULL;
	char Name[256];
	auto Sep = strchr((char*)Path, '/');
	if (Sep)
	{
		ZeroObj(Name);
		memcpy(Name, Path, Sep-Path);
	}
	else
	{
		strcpy_s(Name, sizeof(Name), Path);
	}

	LTreeItem *Starts[2] =
	{
		GetChild(),
		IsRoot() ? GetNext() : 0
	};

	for (int s=0; !Folder && s<CountOf(Starts); s++)
	{
		for (auto i = Starts[s]; i; i = i->GetNext())
		{
			ScribeFolder *f = dynamic_cast<ScribeFolder*>(i);
			if (f)
			{
				auto n = f->GetName(true);
				if (n.Equals(Name))
				{
					if (Sep)
						Folder = f->GetSubFolder(Sep+1);
					else
						Folder = f;
					break;
				}
			}
		}
	}

	return Folder;
}

ScribeFolder *ScribeFolder::CreateSubFolder(const char *Name, int Type)
{
	auto thisFolder = dynamic_cast<LDataFolderI*>(GetObject());
	if (!Name || !thisFolder || !thisFolder->GetStore())
		return nullptr;

	ScribeFolder *NewFolder = nullptr;
	if (auto Fld = GetObject()->GetStore()->Create(MAGIC_FOLDER))
	{
		if (auto folderObj = dynamic_cast<LDataFolderI*>(Fld))
		{
			if ((NewFolder = new ScribeFolder))
			{
				NewFolder->App = App;
				NewFolder->SetObject(folderObj, false, _FL);

				// Set name and type
				NewFolder->SetName(Name, true);
				NewFolder->GetObject()->SetInt(FIELD_FOLDER_TYPE, Type);

				thisFolder->SubFolders();
				if (NewFolder->GetObject()->Save(thisFolder))
				{
					Insert(NewFolder);
					NewFolder->SetDefaultFields();
					NewFolder->OnItemType(true);
				}
				else
				{
					DeleteObj(NewFolder);
				}
			}
		}
	}

	return NewFolder;
}

bool ScribeFolder::Delete(LArray<Thing*> &Items, bool ToTrash)
{
	if (!App)
		return false;

	List<Mail> NotNew;
	LArray<LDataI*> Del;
	LDataStoreI *Store = NULL;
	for (auto i: Items)
	{
		if (i->IsPlaceHolder())
		{
			i->DecRef();
		}
		else
		{
			Mail *m = i->IsMail();
			if (m)
				NotNew.Insert(m);

			auto ObjStore = i->GetObject() ? i->GetObject()->GetStore() : NULL;
			if (!Store)
				Store = ObjStore;
			if (Store == ObjStore)
				Del.Add(i->GetObject());
			else
				LAssert(!"All objects must have the same store.");
		}
	}

	if (NotNew.Length())
		App->OnNewMail(&NotNew, false);

	if (Del.Length() == 0)
		return true;
	if (!Store)
	{
		LgiTrace("%s:%i - No Store?\n", _FL);
		return false;
	}

	if (!Store->Delete(Del, ToTrash))
	{
		LgiTrace("%s:%i - Store.Delete failed.\n", _FL);
		return false;
	}

	return true;
}

void ScribeFolder::MoveTo(LArray<Thing*> &Items, bool CopyOnly, std::function<void(bool, LArray<Store3Status>&)> Callback)
{
	if (Items.Length() == 0)
		return;
	if (!GetObject() || !App)
		return;

	new AsyncOperationState(this, Items, CopyOnly, Callback);
}

int ThingFilterCompare(Thing *a, Thing *b, NativeInt Data)
{
	auto A = a->IsFilter();
	auto B = b->IsFilter();
	return (A && B) ? A->GetIndex() - B->GetIndex() : 0;
}

void ScribeFolder::ReSort()
{
	if (View() && Select())
	{
		View()->SetSort(GetSortCol(), GetSortAscend());
	}
}

void ScribeFolder::SetSort(int Col, bool Ascend, bool CanDirty)
{
	if (GetItemType() == MAGIC_FILTER)
	{
		// Remove any holes in the indexing
		int i = 1;
		Items.Sort(ThingFilterCompare);

		for (auto t : Items)
		{
			Filter *f = t->IsFilter();
			if (f)
			{
				if (f->GetIndex() != i)
				{
					f->SetIndex(i);
				}

				i++;
			}
		}
	}

	if (GetSortCol() != Col ||
		GetSortAscend() != (uchar)Ascend)
	{
		GetObject()->SetInt(FIELD_SORT, (Col + 1) * (Ascend ? 1 : -1));

		if (CanDirty)
			SetDirty();
	}
}

int ScribeFolder::GetSortField()
{
	int Status = 0;
	int Col = GetSortCol();

	if (Col >= 0 && Col < (int)FieldArray.Length())
		Status = FieldArray[Col];

	return Status;
}

class FolderStream : public LStream
{
	ScribeFolder *f;

	// Current index into the thing array
	int Idx;
	
	// Total known size of stream...
	int64 Sz;

	// A memory buffer containing just the encoded 'Thing'
	LMemStream Mem;

	LString FolderMime;

public:
	FolderStream(ScribeFolder *folder) : Mem(512 << 10)
	{
		f = folder;
		Idx = 0;
		Sz = 0;

		switch (f->GetItemType())
		{
			case MAGIC_MAIL:
				FolderMime = sMimeMbox;
				break;
			case MAGIC_CONTACT:
				FolderMime = sMimeVCard;
				break;
			case MAGIC_CALENDAR:
				FolderMime = sMimeVCalendar;
				break;
			default:
				LAssert(!"Need a mime type?");
				break;
		}
	}

	int GetIndex() { return Idx; }
	int Open(const char *Str = 0,int Int = 0) { return true; }
	bool IsOpen() { return true; }
	int Close() { return 0; }
	
	int64 GetSize()
	{
		return -1;
	}
	
	int64 SetSize(int64 Size)
	{
		return -1;
	}
	
	int64 GetPos()
	{
		return -1;
	}
	
	int64 SetPos(int64 pos)
	{
		// This means that IStream::Seek return E_NOTIMPL, which is important
		// for windows to support copies of unknown size.
		return -1;
	}
	
	ssize_t Read(void *Buffer, ssize_t Size, int Flags = 0)
	{
		// Read from stream..
		int64 Remaining = Mem.GetSize() - Mem.GetPos();
		if (Remaining <= 0)
		{
			Thing *t = Idx < (ssize_t)f->Items.Length() ? f->Items[Idx++] : NULL;
			if (t)
			{
				Mem.SetSize(0);
				
				// We can't allow Export to delete the Mem object, we own it.
				// So create a proxy object for it.
				LAutoPtr<LStreamI> cp(new LProxyStream(&Mem));
				
				if (t->Export(cp, FolderMime))
					Sz += Mem.GetSize();
				else
					LAssert(0);
				
				Mem.SetPos(0);
			}
			else return 0;
		}

		Remaining = Mem.GetSize() - Mem.GetPos();
		if (Remaining > 0)
		{
			int Common = (int)MIN(Size, Remaining);
			ssize_t Rd = Mem.Read(Buffer, Common);
			if (Rd > 0)
				return Rd;
			else
				LAssert(0);
		}
		
		return 0;
	}
	
	ssize_t Write(const void *Buffer, ssize_t Size, int Flags = 0)
	{
		LAssert(0);
		return 0;
	}

	LStreamI *Clone()
	{
		LAssert(0);
		return NULL;
	}
};

// ScribeFolder drag'n'drop
bool ScribeFolder::GetFormats(LDragFormats &Formats)
{
	if (GetItemType() == MAGIC_MAIL ||
		GetItemType() == MAGIC_CONTACT ||
		GetItemType() == MAGIC_CALENDAR)
	{
		Formats.SupportsFileStreams();
	}

	Formats.Supports(ScribeFolderObject);
	return Formats.Length() > 0;
}

bool ScribeFolder::OnBeginDrag(LMouse &m)
{ 
	if (GetParent())
		Drag(Tree, m.Event, DROPEFFECT_MOVE | DROPEFFECT_COPY);
	return true;
}

void ScribeFolder::OnEndData()
{
	DropFileName.Empty();
}

LString ScribeFolder::GetDropFileName()
{
	LAutoString Fn(MakeFileName(GetObject()->GetStr(FIELD_FOLDER_NAME), 0));
	DropFileName = Fn;

	if (GetItemType() == MAGIC_MAIL)
		DropFileName += ".mbx";
	else if (GetItemType() == MAGIC_CONTACT)
		DropFileName += ".vcf";
	else if (GetItemType() == MAGIC_CALENDAR)
		DropFileName += ".ics";
	else if (GetItemType() == MAGIC_FILTER)
		DropFileName += ".xml";

	return DropFileName;
}

bool ScribeFolder::GetData(LArray<LDragData> &Data)
{
	ssize_t DataSet = 0;
	
	for (unsigned idx=0; idx<Data.Length(); idx++)
	{
		LDragData &dd = Data[idx];
		
		if (dd.IsFormat(LGI_StreamDropFormat))
		{
			LAutoStreamI s(new FolderStream(this));
			if (s)
			{
				s->SetPos(0);
				
				auto Fn = GetDropFileName();
				auto MimeType = sMimeMbox;
				auto r = dd.AddFileStream(LGetLeaf(Fn), MimeType, s);
				LAssert(r);
			}

			DataSet = dd.Data.Length();
		}
		else if (dd.IsFormat(LGI_FileDropFormat))
		{
			LMouse m;
			if (App->GetMouse(m, true))
			{
				LString::Array Files;

				if (GetDropFileName())
				{
					LAutoPtr<LFile> f(new LFile);
					if (f->Open(DropFileName, O_WRITE))
					{
						if (GetItemType() == MAGIC_MAIL)
							Export(AutoCast(f), sMimeMbox);
						else if (GetItemType() == MAGIC_CONTACT)
							Export(AutoCast(f), sMimeVCard);
						else if (GetItemType() == MAGIC_CALENDAR)
							Export(AutoCast(f), sMimeVCalendar);
					}
				}

				if (DropFileName)
				{
					Files.Add(DropFileName.Get());
					if (CreateFileDrop(&dd, m, Files))
					{
						DataSet++;
					}
					else LgiTrace("%s:%i - CreateFileDrop failed.\n", _FL);
				}
				else LgiTrace("%s:%i - No drop file name.\n", _FL);
			}
			else LgiTrace("%s:%i - GetMouse failed.\n", _FL);
		}
		else if (dd.IsFormat(ScribeFolderObject))
		{
			ScribeClipboardFmt *Fmt = ScribeClipboardFmt::Alloc(true, 1);
			if (Fmt)
			{
				Fmt->FolderAt(0, this);
				dd.Data[0].SetBinary(Fmt->Sizeof(), Fmt);
				DataSet++;

				free(Fmt);
			}
		}
	}

	return DataSet > 0;
}

void ScribeFolder::CollectSubFolderMail(ScribeFolder *To)
{
	if (!To) To = this;

	LoadThings(NULL, [this, To](auto Status)
	{
		LArray<Thing*> Items;
		for (auto Item: Items)
		{
			if (To != this && Item->IsMail())
				Items.Add(Item);
		}
		To->MoveTo(Items, false, NULL);

		for (ScribeFolder *f = GetChildFolder(); f; f = f->GetNextFolder())
		{
			f->CollectSubFolderMail(To);
		}
	});
}

void ScribeFolder::OnReceiveFiles(LArray<const char*> &Files)
{
	if (Files.Length())
	{
		for (unsigned i=0; i<Files.Length(); i++)
		{
			auto File = Files[i];

			// Ask OS what mime type the file is
			LString MimeType = ScribeGetFileMimeType(File);
			// printf("[%i]=%s %s\n", i, File, MimeType.Get());

			// Import the file...
			LAutoPtr<LTextFile> f(new LTextFile);
			if (f->Open(File, O_READ))
				Import(AutoCast(f), MimeType);
		}
	}
}

// Import/export
bool ScribeFolder::GetFormats(bool Export, LString::Array &MimeTypes)
{
	MimeTypes.Add(sMimeMbox);

	if (!Export)
		MimeTypes.Add(sMimeMessage);

	return MimeTypes.Length() > 0;
}

ThingType::IoProgress ScribeFolder::Import(IoProgressImplArgs)
{
	if (Stricmp(mimeType, sMimeMbox) == 0 ||
		Stricmp(mimeType, "text/x-mail") == 0)
	{
		// Mail box format...
		ThingType::IoProgress p(Store3Delayed);
		p.prog = new ImportFolderTask(this, stream, mimeType, cb);
		return p;
	}
	else if (Stricmp(mimeType, sMimeVCard) == 0)
	{
		VCard Io;
		Thing *t;
		bool Error = false;
		int Imported = 0;
		int Idx = 0;

		while ((t = App->CreateItem(GetItemType(), 0, false)))
		{
			Contact *c = t->IsContact();
			if (!c)
			{
				t->DecRef();
				Error = true;
				break;
			}

			if (Io.Import(c->GetObject(), stream))
			{
				const char *First = 0, *Last = 0;
				c->GetField(FIELD_FIRST_NAME, First);
				c->GetField(FIELD_LAST_NAME, Last);
				LgiTrace("Import %i %s %s\n", Idx, First, Last);

				if (t->Save(this))
				{
					Imported++;
				}
				else
				{
					Error = true;
					break;
				}
			}
			else
			{
				t->DecRef();
				break;
			}

			Idx++;
		}
		
		if (Error)
		{
			LgiMsg(	App,
					LLoadString(IDS_ERROR_IMPORT_COUNT),
					AppName, MB_OK,
					LLoadString(IDC_CONTACTS),
					Imported);
			IoProgressError("Contact import error.");
		}

		IoProgressSuccess();
	}
	else if (Stricmp(mimeType, sMimeVCalendar) == 0)
	{
		VCal Io;
		Thing *t;

		while ((t = App->CreateItem(GetItemType(), this, false)))
		{
			if (Io.Import(t->GetObject(), stream))
			{
				if (!t->Save(this))
					IoProgressError("Contact save failed.");
			}
			else
			{
				t->DecRef();
				break;
			}
		}

		IoProgressSuccess();
	}
	else if (GetObject())
	{
		Thing *t = App->CreateThingOfType(GetItemType(), GetObject()->GetStore()->Create(GetItemType()));
		if (!t)
			IoProgressError("Failed to create contact");

		if (!t->Import(stream, mimeType))
		{
			t->DecRef();
			IoProgressError("Contact import failed.");
		}

		if (!t->Save(this))
		{
			t->DecRef();
			IoProgressError("Contact save failed.");
		}

		IoProgressSuccess();
	}
	
	IoProgressNotImpl();
}

class FolderExportTask : public LProgressDlg
{
	LAutoPtr<LStreamI> Out;
	ScribeFolder *Folder;
	LString MimeType;
	int Idx;
	bool HasError = false;

public:
	// Minimum amount of time to do work.
	constexpr static int WORK_SLICE_MS		= 130;
	// This should be larger then WORK_SLICE_MS to allow message loop to process
	constexpr static int PULSE_MS			= 200;

	FolderExportTask(LAutoPtr<LStreamI> out, ScribeFolder *folder, LString mimeType) : LProgressDlg(folder->App)
	{
		Out = out;
		Folder = folder;
		MimeType = mimeType;
		Idx = 0;
		Ts = LCurrentTime();
		Folder->App->OnFolderTask(this, true);

		bool Mbox = _stricmp(MimeType, sMimeMbox) == 0;

		// Clear the files contents
		Out->SetSize(0);

		// Setup progress UI
		SetDescription(Mbox ? LLoadString(IDS_MBOX_WRITING) : (char*)"Writing...");
		SetRange(Folder->Items.Length());
		
		switch (Folder->GetItemType())
		{
			case MAGIC_MAIL:
				SetType(LLoadString(IDS_EMAIL));
				break;
			case MAGIC_CALENDAR:
				SetType(LLoadString(IDS_CALENDAR));
				break;
			case MAGIC_CONTACT:
				SetType(LLoadString(IDS_CONTACT));
				break;
			case MAGIC_GROUP:
				SetType("Groups");
				break;
			default:
				SetType("Objects");
				break;
		}

		SetPulse(PULSE_MS);
		SetAlwaysOnTop(true);
	}
	
	~FolderExportTask()
	{
		Folder->App->OnFolderTask(this, false);
	}
	
	bool OnRequestClose(bool OsClose)
	{
		return true;
	}
	
	void OnPulse()
	{
		LProgressDlg::OnPulse();
		PostEvent(M_EXPORT_NEXT);
	}
	
	LMessage::Result OnEvent(LMessage *Msg)
	{
		if (Msg->Msg() == M_EXPORT_NEXT)
		{
			auto StartTs = LCurrentTime();
			while (	!IsCancelled() &&
					(LCurrentTime() - StartTs) < WORK_SLICE_MS)
			{
				if (Idx >= (ssize_t)Folder->Items.Length())
				{
					Quit();
					break;
				}

				// Process all the container's items
				Thing *t = Folder->Items[Idx++];
				if (t)
				{
					if (t->Export(Out, MimeType, NULL))
					{
						Value(Idx);
					}
					else
					{
						HasError = true;
						LgiMsg(this, "Error exporting items.", AppName);
						Quit();
					}
				}
			}			
			return 0;
		}

		return LProgressDlg::OnEvent(Msg);
	}
};


// This is the mime type used to storage objects on disk
const char *ScribeFolder::GetStorageMimeType()
{
	auto Type = GetItemType();
	switch (Type)
	{
		case MAGIC_MAIL:
			return sMimeMbox;
		case MAGIC_CALENDAR:
			return sMimeVCalendar;
		case MAGIC_CONTACT:
			return sMimeVCard;
		case MAGIC_FILTER:
			return sMimeXml;
		default:
			LgiTrace("%s:%i - Unsupported storage type: %s\n", _FL, Store3ItemTypeName(Type));
			break;
	}
	
	return NULL;
}

void ScribeFolder::ExportAsync(LAutoPtr<LStreamI> f, const char *MimeType, std::function<void(LProgressDlg*)> Callback)
{
	if (!MimeType)
	{
		LAssert(!"No Mimetype");
		if (Callback) Callback(NULL);
		return;
	}

	LoadThings(	NULL,
				[
					this,
					Str = f.Release(),
					MimeType = LString(MimeType),
					Callback
				]
				(auto Status)
				{
					LAutoPtr<LStreamI> f(Str);
					FolderExportTask *Task = NULL;
					if (Status == Store3Success)
						Task = new FolderExportTask(f, this, MimeType);
					if (Callback)
						Callback(Task);
				});
}

ThingType::IoProgress ScribeFolder::Export(IoProgressImplArgs)
{
	IoProgress ErrStatus(Store3Error);
	if (!mimeType)
	{
		ErrStatus.errMsg = "No mimetype.";
		if (cb) cb(&ErrStatus, NULL);
		return ErrStatus;
	}
		
	if (!LoadThings())
	{
		ErrStatus.errMsg = "Failed to load things.";
		if (cb) cb(&ErrStatus, NULL);
		return ErrStatus;
	}

	IoProgress Status(Store3Delayed);
	Status.prog = new ExportFolderTask(this, stream, mimeType, cb);
	return Status;
}

size_t ScribeFolder::Length()
{
	if (GetItemType() == MAGIC_MAIL)
	{
		ThingList *v = View();
		if (v)
			return v->Length();
	}

	if (IsLoaded())
		return Items.Length();

	return GetItems();
}

ssize_t ScribeFolder::IndexOf(Mail *m)
{
	if (GetItemType() == MAGIC_MAIL)
	{
		ThingList *v = View();
		if (v)
			return v->IndexOf(m);
	
		return Items.IndexOf(m);
	}

	return -1;
}

Mail *ScribeFolder::operator [](size_t i)
{
	if (GetItemType() == MAGIC_MAIL)
	{
		ThingList *v = View();
		if (v)
			return dynamic_cast<Mail*>(v->ItemAt(i));

		Thing *t = Items[i];
		if (t)
			return t->IsMail();
	}

	return NULL;
}

bool ScribeFolder::GetVariant(const char *Name, LVariant &Value, const char *Array)
{
	auto Fld = StrToDom(Name);
	switch (Fld)
	{
		case SdType: // Type: Int32
		{
			Value = GetObject()->Type();
			break;
		}
		case SdName: // Type: String
		{
			Value = GetName(true).Get();
			break;
		}
		case SdPath: // Type: String
		{
			auto p = GetPath();
			if (p)
				Value = p;
			else
				return false;
			break;
		}
		case SdUnread: // Type: Int32
		{
			Value = GetUnRead();
			break;
		}
		case SdLength: // Type: Int32
		{
			Value = (int32)Items.Length();
			break;
		}
		case SdItem: // Type: Thing[]
		{
			Value.Empty();

			LoadThings(); // Use in sync mode, no callback

			// This call back HAS to set value one way or another...
			if (Array)
			{
				bool IsNumeric = true;
				for (auto *v = Array; *v; v++)
				{
					if (!IsDigit(*v))
					{
						IsNumeric = false;
						break;
					}
				}

				if (IsNumeric)
				{
					int Idx = atoi(Array);
					if (Idx >= 0 && Idx < (ssize_t)Items.Length())
					{
						Value = (LDom*) Items[Idx];
						return true;
					}
				}
				else // Is message ID?
				{
					for (auto t : Items)
					{
						Mail *m = t->IsMail();
						if (!m)
							break;

						auto Id = m->GetMessageId();
						if (Id && !strcmp(Id, Array))
						{
							Value = (LDom*)t;
							return true;
						}
					}
				}
			}
			else if (Value.SetList())
			{
				for (auto t : Items)
					Value.Value.Lst->Insert(new LVariant((LDom*)t));
				return true;
			}
			break;
		}
		case SdItemType: // Type: Int32
		{
			Value = GetItemType();
			break;
		}
		case SdScribe: // Type: ScribeWnd
		{
			Value = (LDom*)App;
			break;
		}
		case SdChild: // Type: ScribeFolder
		{
			Value = (LDom*)GetChildFolder();
			break;
		}
		case SdNext: // Type: ScribeFolder
		{
			Value = (LDom*)GetNextFolder();
			break;
		}
		case SdSelected: // Type: Thing[]
		{
			if (!Select() ||
				!Value.SetList())
				return false;
			
			List<Thing> a;
			if (!App->GetMailList()->GetSelection(a))
				return false;
			
			for (auto t: a)
			{
				Value.Value.Lst->Insert(new LVariant(t));
			}
			break;
		}
		case SdExpanded: // Type: Boolean
		{
			Value = Expanded();
			break;
		}
		default:
		{
			return false;
		}
	}

	return true;
}

bool ScribeFolder::SetVariant(const char *Name, LVariant &Value, const char *Array)
{
	auto Fld = StrToDom(Name);
	switch (Fld)
	{
		case SdName: // Type: String
		{
			char *s = Value.CastString();
			if (ValidStr(s))
				OnRename(s);
			else
				return false;
			break;
		}
		case SdExpanded: // Type: Boolean
		{
			Expanded(Value.CastInt32() != 0);
			break;
		}
		default:
			return false;
	}
	
	return true;
}

bool ScribeFolder::CallMethod(const char *MethodName, LScriptArguments &Args)
{
	auto m = StrToDom(MethodName);
	switch (m)
	{
		case SdAdd: // Type: (Thing obj)
		{
			*Args.GetReturn() = false;
			auto obj = Args.DomAt(0);
			if (!obj)
			{
				LgiTrace("%s:%i - Not an object.\n", _FL);
				return true;
			}

			auto thing = dynamic_cast<Thing*>(obj);
			if (!thing)
			{
				LgiTrace("%s:%i - object not a 'Thing'.\n", _FL);
				return true;
			}

			if (!InsertThing(thing))
			{
				LgiTrace("%s:%i - InsertThing failed.\n", _FL);
				return true;
			}

			auto fld = GetObject();
			auto data = thing->GetObject();
			if (fld && data)
				data->Save(fld);

			*Args.GetReturn() = true;
			return true;
		}
		case SdLoad: // Type: ()
		{
			LoadThings(App);
			// FIXME: Callback for status?
			return true;
		}
		case SdSelect: // Type: ()
		{
			Select(true);
			return true;
		}
		case SdCreate: // Type: ()
		{
			// Create an object suitable for storing in this folder...
			if (!GetObject())
				return false;

			auto data = GetObject()->GetStore()->Create(GetItemType());
			auto thing = data ? App->CreateThingOfType(GetItemType(), data) : NULL;
			*Args.GetReturn() = static_cast<LDom*>(thing);
			return true;
		}
		case SdImport: // Type: (String FileName, String MimeType)
		{
			*Args.GetReturn() = false;
			if (Args.Length() != 2)
				LgiTrace("%s:%i - Error: expecting 2 arguments to 'Import'.\n", _FL);
			else
			{
				auto FileName = Args[0]->Str();
				LAutoPtr<LFile> f(new LFile);
				if (f->Open(FileName, O_READ))
				{
					auto p = Import(AutoCast(f), Args[1]->Str());
					*Args.GetReturn() = p.status;
				}
				else
					LgiTrace("%s:%i - Error: Can't open '%s' for reading.\n", _FL, FileName);
			}
			break;
		}
		case SdExport: // Type: (String FileName, String MimeType)
		{
			*Args.GetReturn() = false;
			if (Args.Length() != 2)
				LgiTrace("%s:%i - Error: expecting 2 arguments to 'Export'.\n", _FL);
			else
			{
				auto FileName = Args[0]->Str();
				LAutoPtr<LFile> f(new LFile);
				if (f->Open(FileName, O_WRITE))
				{
					auto p = Export(AutoCast(f), Args[1]->Str());
					*Args.GetReturn() = p.status;
				}
				else
					LgiTrace("%s:%i - Error: Can't open '%s' for writing.\n", _FL, FileName);
			}
			break;
		}
		default:
			break;
	}

	return false;
}

void ScribeFolder::OnRethread()
{
	if (GetThreaded())
	{
		Thread();
	}
}

