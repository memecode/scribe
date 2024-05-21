#undef UNICODE
#include "Scribe.h"
#include "lgi/common/Com.h"
#include "mapix.h"
#include "mapiutil.h"
#include "lgi/common/Button.h"
#include "lgi/common/Combo.h"
#include "lgi/common/Edit.h"
#include "lgi/common/ProgressDlg.h"
#include "lgi/common/RtfHtml.h"
#include "Calendar.h"
#include "resdefs.h"
#include "lgi/common/LgiRes.h"

#if _MSC_VER >= 1400
typedef ULONG_PTR	UI_TYPE;
#else
typedef ULONG		UI_TYPE;
#endif

typedef HRESULT (STDAPICALLTYPE *pWrapCompressedRTFStream)(LPSTREAM lpCompressedRTFStream,
        ULONG ulFlags, LPSTREAM FAR * lpUncompressedRTFStream);

#define PR_SMTP_ADDRESS				(PROP_TAG(PT_STRING8,	0x39fe))
#define PR_SENDER_SMTP_ADDRESS		(PROP_TAG(PT_STRING8,	0x0065))
#define PR_SENDER_SMTP_ADDRESS2		(PROP_TAG(PT_STRING8,	0x0c1f))
#ifndef PR_INTERNET_MESSAGE_ID
#define PR_INTERNET_MESSAGE_ID		0x1035001E
#endif
#ifndef PR_BODY_HTML
#define PR_BODY_HTML				0x1013001E
#endif
#ifndef PR_ATTACH_CONTENT_ID
#define PR_ATTACH_CONTENT_ID		0x3712001E
#endif

char *RClientsEmail = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Clients\\Mail";

// Helper classes/functions
void RemoveChars(char *Str, int Start, int Len)
{
	memmove(Str, Str + Len, strlen(Str + Len) + 1);
}

void InsertChars(char *Ins, char *Str, int At)
{
	auto Len = strlen(Ins);
	memmove(Str + Len, Str, strlen(Str) + 1);
	memcpy(Str, Ins, Len);
}

class MapiEntry
{
public:
	uchar *Bin;
	int Len;

	MapiEntry(SPropValue *v)
	{
		Len = 0;
		Bin = 0;
		if (v)
		{
			Len = v->Value.bin.cb;
			Bin = new uchar[Len];
			if (Bin)
			{
				memcpy(Bin, v->Value.bin.lpb, Len);
			}
		}
	}

	~MapiEntry()
	{
		DeleteArray(Bin);
	}
};

SPropValue *MapiGetField(SRow *Row, int Field)
{
	SPropValue *v = 0;
	if (Row)
	{
		for (unsigned i=0; i<Row->cValues; i++)
		{
			if (PROP_ID(Row->lpProps[i].ulPropTag) == PROP_ID(Field))
			{
				return Row->lpProps + i;
			}
		}
	}
	return v;
}

SPropValue *MapiGetProp(IMAPIProp *Props, int Field)
{
	SPropTagArray InTag;
	InTag.cValues = 1;
	InTag.aulPropTag[0] = Field;

	SPropValue *OutTag = 0;
	ULONG Tags = 0;
	if (Props)
	{
		if (Props->GetProps(&InTag, 0, &Tags, &OutTag) == S_OK)
		{
			if (Tags == 1)
			{
				return OutTag;
			}
		}
	}

	return 0;
}

int MapiCastInt(SPropValue *Val)
{
	if (Val && PROP_TYPE(Val->ulPropTag) == PT_LONG)
	{
		return Val->Value.l;
	}
	if (Val && PROP_TYPE(Val->ulPropTag) == PT_SHORT)
	{
		return Val->Value.i;
	}

	return 0;
}

char *MapiCastString(SPropValue *Val)
{
	if (Val && PROP_TYPE(Val->ulPropTag) == PT_STRING8)
	{
		return Val->Value.lpszA;
	}

	return 0;
}

bool MapiCastBinary(SPropValue *Val, void *&Ptr, int &Size)
{
	if (Val && PROP_TYPE(Val->ulPropTag) == PT_BINARY)
	{
		Ptr = Val->Value.bin.lpb;
		Size = Val->Value.bin.cb;
		return true;
	}

	return false;
}

char *MapiGetPropStr(IMAPIProp *Props, int Field)
{
	SPropValue *Val = MapiGetProp(Props, Field);
	if (Val)
	{
		return MapiCastString(Val);
	}
	return 0;	
}

int MapiGetPropInt(IMAPIProp *Props, int Field)
{
	SPropValue *Val = MapiGetProp(Props, Field);
	if (Val)
	{
		return MapiCastInt(Val);
	}
	return 0;	
}

bool MapiSetPropStr(IMAPIProp *Props, int Field, const char *Str, bool Unicode = false)
{
	if (Props && Str)
	{
		LAssert(PROP_TYPE(Field) == PT_STRING8);

		SPropValue p;
		LString n;
		p.ulPropTag = Field;
		if (Unicode)
		{
			p.Value.lpszA = n = LToNativeCp(Str);
		}
		else
		{
			p.Value.lpszA = (LPSTR)Str;
		}
		bool Status = Props->SetProps(1, &p, 0) == S_OK;
		return Status;
	}

	return false;
}

bool MapiSetPropLong(IMAPIProp *Props, int Field, ULONG lng)
{
	if (Props)
	{
		SPropValue p;
		p.ulPropTag = Field;
		p.Value.l = lng;
		return Props->SetProps(1, &p, 0) == S_OK;
	}

	return false;
}

bool MapiSetPropBool(IMAPIProp *Props, int Field, bool b)
{
	if (Props)
	{
		SPropValue p;
		p.ulPropTag = Field;
		p.Value.b = b;
		return Props->SetProps(1, &p, 0) == S_OK;
	}

	return false;
}

bool MapiSetPropDate(IMAPIProp *Props, int Field, const LDateTime &d, bool AdjustTz = true)
{
	if (d.Year())
	{
		// Set sent date
		SPropValue Prop;
		Prop.ulPropTag = Field;

		LDateTime a = d;
		if (AdjustTz)
			a.ToUtc();

		SYSTEMTIME st;
		st.wDay = a.Day();
		st.wMonth = a.Month();
		st.wYear = a.Year();
		st.wMinute = a.Minutes();
		st.wHour = a.Hours();
		st.wSecond = a.Seconds();
		st.wMilliseconds = 0;
		if (SystemTimeToFileTime(&st, &Prop.Value.ft))
		{
			return Props->SetProps(1, &Prop, 0) == S_OK;
		}
	}

	return false;
}

class LMapiList
{
	LPMAPITABLE List;
	ULONG Rows;
	SRowSet *BaseRow;
	uint32_t i, StartIndex;
	bool ReleaseList;

public:
	LMapiList(LPMAPITABLE &list, bool release = true)
	{
		List = list;
		list = NULL;
		Rows = 0;
		BaseRow = 0;
		i = 0;
		StartIndex = 0;
		ReleaseList = release;

		if (List)
		{
			HRESULT r = List->GetRowCount(0, &Rows);
			if (Rows &&
				List->SeekRow(BOOKMARK_BEGINNING, 0, NULL) == S_OK)
			{
				if (List->QueryRows(Rows, 0, &BaseRow) == S_OK)
				{
					// Ok
				}
			}
		}
	}

	~LMapiList()
	{
		if (List && ReleaseList)
		{
			List->Release();
		}
	}

	int Index() { return i; }
	int Length() { return Rows; }
	bool More() { return (BaseRow && i >= StartIndex && i < BaseRow->cRows + StartIndex); }
	void Next()
	{
		i++;

		if (BaseRow->cRows < Rows &&
			i-StartIndex >= BaseRow->cRows)
		{
			// run into the end of the list section
			int s = StartIndex + BaseRow->cRows;
			if (List->QueryRows(Rows-StartIndex, 0, &BaseRow) == S_OK)
			{
				StartIndex = s;
			}
		}
	}

	SRowSet *Current()
	{
		if (More()) // in range
		{
			return BaseRow + i - StartIndex;
		}

		return 0;
	}

	SPropValue *GetField(int Field)
	{
		if (More()) // in range
		{
			return MapiGetField(BaseRow->aRow + i - StartIndex, Field);
		}

		return 0;
	}
};

// DEBUG STUFF
#ifdef _DEBUG
class LRow : public LListItem
{
	SRow *Row;
	char **Data;
	int Cols;

public:
	LRow(SRow *row)
	{
		Cols = 0;
		Row = row;
		if (Row)
		{
			Cols = Row->cValues;
			Data = new char*[Cols];
			if (Data)
			{
				memset(Data, 0, sizeof(*Data)*Cols);
			}
		}
	}

	const char *GetText(int i)
	{
		SPropValue *Value = (Row)?Row->lpProps+i:0;
		if (Value)
		{
			char Str[256] = "";

			DeleteArray(Data[i]);
			switch (PROP_TYPE(Value->ulPropTag))
			{
				case PT_I2:
				{
					sprintf_s(Str, sizeof(Str), "%i", Value->Value.i);
					break;
				}
				case PT_I4:
				{
					sprintf_s(Str, sizeof(Str), "%i", Value->Value.l);
					break;
				}
				case PT_R8:
				{
					sprintf_s(Str, sizeof(Str), "%f", Value->Value.dbl);
					break;
				}
				case PT_BOOLEAN:
				{
					sprintf_s(Str, sizeof(Str), "%s", (Value->Value.b)?"true":"false");
					break;
				}
				case PT_STRING8:
				{
					sprintf_s(Str, sizeof(Str), "%s", Value->Value.lpszA);
					break;
				}
				case PT_ERROR:
				{
					sprintf_s(Str, sizeof(Str), "e(%08.8X)", Value->Value.err);
					break;
				}
				case PT_BINARY:
				{
					sprintf_s(Str, sizeof(Str), "Bin(%i)", Value->Value.bin.cb);
					break;
				}
				default:
				{
					sprintf_s(Str, sizeof(Str), "#TYPE(%04.4X)", PROP_TYPE(Value->ulPropTag));
					break;
				}
			}

			if (strlen(Str)>0)
			{
				Data[i] = NewStr(Str);
			}

			return (Data[i])?Data[i]:"";
		}
		return "";
	}
};

class LShowTable : public LDialog
{
	SRowSet *Table;
	LList *List;

public:
	LShowTable(LView *parent, SRowSet *table)
	{
		SetParent(parent);
		SetPos(LRect(0, 0, 660, 490));
		MoveToCenter();
		Name("Show Table");
		Table = table;

		Children.Insert(new LButton(101, 550, 420, 60, 20, "Close"));
		Children.Insert(List = new LList(100, 10, 10, 600, 400));
		if (List && Table)
		{
			if (Table->cRows > 0)
			{
				SRow *First = Table->aRow;
				for (unsigned i=0; i<First->cValues; i++)
				{
					char Str[256];
					sprintf_s(Str, sizeof(Str), "%08.8X", First->lpProps[i].ulPropTag);
					List->AddColumn(Str, 80);
				}

				for (unsigned i=0; i<Table->cRows; i++)
				{
					List->Insert(new LRow(Table->aRow+i));
				}
			}
		}
	}

	int OnNotify(LViewI *Ctrl, LNotification n)
	{
		if (Ctrl->GetId() == 101)
		{
			EndModal(0);
		}
		return 0;
	}

};

void ViewTable(LPMAPITABLE Table, LView *Wnd)
{
	LMapiList Lst(Table);
	if (Lst.Current())
	{
		auto Tbl = new LShowTable(Wnd, Lst.Current());
		Tbl->DoModal(NULL);
	}
}
#endif // _DEBUG

class EntryRef
{
public:
	char *DisplayName;
	int Size;
	LPENTRYID Entry;

	EntryRef()
	{
		DisplayName = 0;
		Size = 0;
		Entry = 0;
	}

	bool OpenRoot(LComPtr<IMAPISession> &Session, UI_TYPE UiHnd, LComPtr<IMsgStore> &MsgStore, LComPtr<IMAPIFolder> &RootFolder)
	{
		if (!Session)
		{
			LgiTrace("%s:%i - No Session.\n", _FL);
			return false;
		}

		if (Session->OpenMsgStore(	UiHnd,
									Size,	// entry bytes
									Entry,	// ptr to entry
									NULL,	// default interface: IMsgStore
									MAPI_BEST_ACCESS,
									MsgStore.Set()) != S_OK ||
			!MsgStore)
		{
			LgiTrace("%s:%i - OpenMsgStore failed.\n", _FL);
			return false;
		}

		SPropValue *SubTree = MapiGetProp(MsgStore, PR_IPM_SUBTREE_ENTRYID);
		if (!SubTree)
		{
			LgiTrace("%s:%i - Failed to get PR_IPM_SUBTREE_ENTRYID.\n", _FL);
			return false;
		}

		ULONG ObjType;
		if (MsgStore->OpenEntry(SubTree->Value.bin.cb,
								(LPENTRYID)SubTree->Value.bin.lpb,
								NULL,
								MAPI_MODIFY,
								&ObjType,
								(IUnknown**) RootFolder.Set()) != S_OK ||
			!RootFolder)
		{
			LgiTrace("%s:%i - OpenEntry failed.\n", _FL);
			MsgStore.Release();
			return false;
		}

		return true;
	}
};

class MessageStores : public List<EntryRef>
{
	LPMAPITABLE MsgStores;

public:
	bool Status;

	MessageStores(LPMAPISESSION Session)
	{
		Status = false;
		MsgStores = 0;

		if (Session->GetMsgStoresTable(0, &MsgStores) == S_OK &&
			MsgStores)
		{
			for (LMapiList Lst(MsgStores, false); Lst.More(); Lst.Next())
			{
				SPropValue *DisplayName = Lst.GetField(PR_DISPLAY_NAME); 
				SPropValue *Entry = Lst.GetField(PR_ENTRYID);
				if (DisplayName && Entry)
				{
					LAutoPtr<EntryRef> Ref(new EntryRef);
					if (Ref)
					{
						Ref->DisplayName = MapiCastString(DisplayName);

						void *p = 0;
						int s = 0;
						if (Ref->DisplayName && MapiCastBinary(Entry, p, s))
						{
							Ref->Size = s;
							Ref->Entry = (ENTRYID*) p;
							Insert(Ref.Release());
						}
					}
				}
			}

			Status = true;
		}
	}

	~MessageStores()
	{
		if (MsgStores)
			MsgStores->Release();
		DeleteObjects();
	}
};

class ChooseMessageStoreDlg : public LDialog
{
	ScribeWnd *App;
	LCombo *CStore;
	LEdit *Folder;
	bool InitOk;
	bool Directory;
	MessageStores *Stores;

public:
	int Size;
	LPENTRYID Entry;
	EntryRef *Ref;
	LString FolderName;

	ChooseMessageStoreDlg(ScribeWnd *Wnd, LPMAPISESSION Session, bool Dir = true)
	{
		App = Wnd;
		Size = 0;
		Entry = 0;
		InitOk = false;
		Directory = Dir;
		Ref = 0;
		Stores = 0;
		CStore = 0;
		Folder = 0;

		if (LoadFromResource(IDD_OUTLOOK_IMPORT))
		{
			Stores = new MessageStores(Session);
			if (Stores && Stores->Status)
			{
				if (GetViewById(IDC_MSG_STORE, CStore))
				{
					InitOk = true;
					for (auto e: *Stores)
						CStore->Insert(e->DisplayName);
				}
			}

			if (GetViewById(IDC_FOLDER, Folder))
			{
				Folder->Name("/");
				Folder->Enabled(false);
			}
		}

		MoveToCenter();
	}

	~ChooseMessageStoreDlg()
	{
		DeleteObj(Stores);
	}

	bool InitCheck()
	{
		return InitOk;
	}

	void OnCreate()
	{
		LViewI *v = FindControl(IDC_TABLE);
		if (v)
		{
			LRect c = GetClient();
			c.Inset(10, 10);
			v->SetPos(c);
		}
	}

	int OnNotify(LViewI *Ctrl, LNotification n)
	{
		switch (Ctrl->GetId())
		{
			case IDC_SET_FOLDER:
			{
				if (!Folder)
					break;

				auto Dlg = new FolderDlg(this, App);
				Dlg->DoModal([this, Dlg](auto dlg, auto ctrlId)
				{
					if (ctrlId)
						this->Folder->LView::Name(Dlg->Get());
				});
				break;
			}
			case IDOK:
			{
				if (CStore)
				{
					Ref = Stores->ItemAt((int)CStore->Value());
					if (Ref)
					{
						Size = Ref->Size;
						Entry = Ref->Entry;
					}
				}

				if (Directory && Folder)
					FolderName = Folder->Name();

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
};

// Import / Export parameters
class ImportParams
{
public:
	ScribeWnd *App;
	Progress *Prog;
	Progress *ItemProg;
	bool AllFolders;
	LString::Array Import;

	ImportParams()
	{
		AllFolders = false;
		App = NULL;
		Prog = NULL;
		ItemProg = NULL;
	}
	
	~ImportParams()
	{
	}
};

class ExportParams
{
public:
	ScribeWnd *App;
	Progress *Prog;
	Progress *ItemProg;
	bool AllFolders;

	List<ScribeFolder> Include;
	List<ScribeFolder> Export;
	List<ScribeFolder> Exclude;

	ExportParams()
	{
		AllFolders = false;
		App = 0;
		Prog = 0;
		ItemProg = 0;
	}
};

int StrCmp(char *a, char *b, NativeInt d)
{
	return _stricmp(a, b);
}

// Importer class
class OutlookIO
{
	friend class MailMapiSource;
	friend class ImportDlg;
	friend class ExportDlg;

	ScribeWnd					*App;
	ScribeAccount				*Account;
	LAutoString                 DefClient;

	HINSTANCE					hMapi;
	LComPtr<IMAPISession>		Session;
	LComPtr<IMsgStore>			MsgStore;
	UI_TYPE						Ui;
	LComPtr<IAddrBook>			AddrBook;

	MAPIINITIALIZE				*MAPIInitialize;
	MAPILOGONEX					*MAPILogonEx;
	MAPIUNINITIALIZE			*MAPIUninitialize;
	MAPIALLOCATEBUFFER			*MAPIAllocateBuffer;
	MAPIFREEBUFFER				*MAPIFreeBuffer;
	pWrapCompressedRTFStream	WrapCompressedRTFStream;

	bool ImportEmail(ScribeFolder *Folder, Mail *&To, IMessage *From, bool Post);

public:
	OutlookIO(ScribeWnd *Wnd, int Flags, ScribeAccount *Account = 0);
	~OutlookIO();

	bool LoadSession(LViewI *Parent, char *ResetClient);
	LComPtr<IMAPISession> &GetSession() { return Session; }
	UI_TYPE GetUiHnd() { return Ui; }

	// Import functions
	bool Import(	ImportParams *P,
					IMAPIFolder *In,
					ScribeFolder *Out,
					LString::Array &Path);
	void ImportPersonalAddressBook(std::function<void(bool)> callback);

	// Export functions
	bool Export(	ExportParams *P,
					ScribeFolder *In,
					IMAPIFolder *Out,
					int Depth = 0);
	bool ExportCalendar(Calendar *c, IMessage *m);
	int CountFolders(ScribeFolder *f);

	// Workers
	bool ImportItem(ScribeFolder *Out, IMAPIFolder *In, SPropValue *EntryId);
	int FolderType(IMAPIFolder *In);
};

// UI classes
class AddFolderDlg : public LDialog
{
	OutlookIO *Io;
	LTree *Tree;

	void Load(IMAPIFolder *f, LTreeItem *i)
	{
		if (f && i)
		{
			LPMAPITABLE Folders = 0;
			if (f->GetHierarchyTable(0, &Folders) == S_OK)
			{
				for (LMapiList Lst(Folders); Lst.More(); Lst.Next())
				{
					SPropValue *v = Lst.GetField(PR_ENTRYID);
					LString Name = LFromNativeCp(MapiCastString(Lst.GetField(PR_DISPLAY_NAME)));
					if (v && Name)
					{
						LTreeItem *ChildItem = new LTreeItem;
						if (ChildItem)
						{
							ChildItem->SetText(Name);
							i->Insert(ChildItem);
							i->Expanded(true);

							IMAPIFolder *ChildFolder = 0;
							ULONG Type;
							if (f->OpenEntry(v->Value.bin.cb, (LPENTRYID)v->Value.bin.lpb, NULL, MAPI_BEST_ACCESS, &Type, (IUnknown**)&ChildFolder) == S_OK &&
								ChildFolder)
							{
								Load(ChildFolder, ChildItem);
								ChildFolder->Release();
							}
						}
					}
				}
			}
		}
	}

public:
	LAutoString Path;

	AddFolderDlg(LView *parent, OutlookIO *io, EntryRef *Store)
	{
		Io = io;
		SetParent(parent);
		if (LoadFromResource(IDD_OUTLOOK_ADD_FLD))
		{
			MoveToCenter();
			GetViewById(IDC_FOLDERS, Tree);

			auto Session = Io->GetSession();
			if (Tree && Session && Store)
			{
				LTreeItem *RootItem = new LTreeItem;
				if (RootItem)
				{
					RootItem->SetText(Store->DisplayName);
					Tree->Insert(RootItem);
					RootItem->Expanded(true);

					LComPtr<IMsgStore> MsgStore;
					LComPtr<IMAPIFolder> RootFolder;

					if (Store->OpenRoot(Session, Io->GetUiHnd(), MsgStore, RootFolder))
						Load(RootFolder, RootItem);
				}
			}
		}
	}

	int OnNotify(LViewI *c, LNotification n)
	{
		switch (c->GetId())
		{
			case IDOK:
			{
				LArray<LTreeItem*> p;
				for (LTreeItem *s = Tree ? Tree->Selection() : 0; s; s = s->GetParent())
				{
					p.AddAt(0, s);
				}
				LStringPipe n;
				for (unsigned i=1; i<p.Length(); i++)
				{
					n.Print("/%s", p[i]->GetText());
				}
				Path.Reset(n.NewStr());
				LAssert(Path != NULL);

				// Fall thru
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

class IoDlg : public LDialog
{
protected:
	ScribeWnd *App;
	List<char> Clients;
	OutlookIO *Io;
	char *DefClient;
	LAutoPtr<MessageStores> Stores;
	LList *SrcFolders;

	LComPtr<IMAPIFolder> *MapiFolder;
	ScribeFolder **OurFolder;

public:
	IoDlg(	ScribeWnd *app,
			OutlookIO *io,
			LComPtr<IMAPIFolder> *mapifolder,
			ScribeFolder *&scribefolder)
	{
		App = app;
		Io = io;
		DefClient = 0;
		SrcFolders = 0;
		MapiFolder = mapifolder;
		OurFolder = &scribefolder;
	}

	~IoDlg()
	{
		Clients.DeleteArrays();
		DeleteArray(DefClient);
	}

	virtual void AddFolder() {}
	virtual void OnLogin(bool e) {}

	void InitSrcClients()
	{
		LRegKey Email(false, RClientsEmail);
		LCombo *SrcClient;
		if (Email.GetKeyNames(Clients) && GetViewById(IDC_CLIENT, SrcClient))
		{
			Clients.Sort(StrCmp);

			DefClient = NewStr(Email.GetStr());
			int i = 0;
			for (auto c: Clients)
			{
				SrcClient->Insert(c);
				if (DefClient && _stricmp(DefClient, c) == 0)
				{
					SrcClient->Value(i);
				}
				i++;
			}
		}
	}

	void AddPath(char *p)
	{
		if (SrcFolders && p)
		{
			bool Has = false;
			for (auto i : *SrcFolders)
			{
				const char *s = i->GetText(0);
				if (s && _stricmp(s, p) == 0)
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
					i->SetText(p);
					SrcFolders->Insert(i);
					SrcFolders->ResizeColumnsToContent();
				}
			}
		}
	}

	int OnNotify(LViewI *v, LNotification n)
	{
		bool Ok = false;

		switch (v->GetId())
		{
			case IDC_LOGIN:
			{
				const char *NewClient = GetCtrlName(IDC_CLIENT);
				if (!NewClient)
				{
					LgiMsg(this, "Unknown Client Name.", AppName);
					break;
				}

				if (!Io->LoadSession(this, DefClient))
				{
					LgiMsg(this, "Outlook LoadSession failed.\n", AppName);
					break;
				}

				OnLogin(true);

				Stores.Reset(new MessageStores(Io->GetSession()));

				LCombo *StoreCbo;
				if (!Stores || !GetViewById(IDC_MSG_STORE, StoreCbo))
				{
					LgiMsg(this, "No store combo?\n", AppName);
					break;
				}

				for (auto e: *Stores)
					StoreCbo->Insert(e->DisplayName);

				StoreCbo->Value(0);
				break;
			}
			case IDC_ADD_SRC_FOLDER:
			{
				AddFolder();
				break;
			}
			case IDC_DEL_SRC_FOLDER:
			{
				if (!SrcFolders)
				{
					LgiTrace("%s:%i - SrcFolders is NULL.\n", _FL);
					break;
				}

				List<LListItem> s;
				if (!SrcFolders->GetSelection(s))
				{
					LgiTrace("%s:%i - GetSelection failed.\n", _FL);
					break;
				}

				s.DeleteObjects();
				break;
			}
		}

		return 0;
	}
};

class ImportDlg : public IoDlg
{
	ImportParams *Params;

public:
	ImportDlg(	ScribeWnd *app,
				ImportParams *params,
				OutlookIO *io,
				LComPtr<IMAPIFolder> *mapifolder,
				ScribeFolder *&scribefolder) :
		IoDlg(app, io, mapifolder, scribefolder)
	{
		SetParent(App = app);
		Params = params;

		if (LoadFromResource(IDD_OUTLOOK_IMPORT))
		{
			MoveToCenter();
			OnLogin(false);
			GetViewById(IDC_SRC_FOLDERS, SrcFolders);

			LVariant s;
			if (App->GetOptions()->GetValue(OPT_OutlookImportSrc, s) && s.Str())
			{
				auto t = s.LStr().SplitDelimit(",");
				for (unsigned i=0; i<t.Length(); i++)
				{
					AddPath(t[i]);
				}
			}
			if (App->GetOptions()->GetValue(OPT_OutlookImportDst, s) && s.Str())
				SetCtrlName(IDC_FOLDER, s.Str());
			else
			{
				auto Cur = App->GetCurrentFolder();
				if (Cur)
				{
					auto Path = Cur->GetPath();
					if (Path)
						SetCtrlName(IDC_FOLDER, Path);
				}
			}
			if (App->GetOptions()->GetValue(OPT_OutlookImportAll, s))
			{
				SetCtrlValue(IDC_ALL, s.CastInt32());
			}

			InitSrcClients();
		}
	}

	void OnLogin(bool e)
	{
		SetCtrlEnabled(IDC_CLIENT, !e);
		SetCtrlEnabled(IDC_LOGIN, !e);

		SetCtrlEnabled(IDC_MSG_STORE, e);
		SetCtrlEnabled(IDC_ALL, e);
		SetCtrlEnabled(IDC_SRC_FOLDERS, e);
		SetCtrlEnabled(IDC_ADD_SRC_FOLDER, e);
		SetCtrlEnabled(IDC_DEL_SRC_FOLDER, e);
		SetCtrlEnabled(IDC_SET_FOLDER, e);
		SetCtrlEnabled(IDOK, e);

		SetCtrlEnabled(IDC_FOLDER, false);
	}

	void AddFolder()
	{
		if (Stores)
		{
			EntryRef *e = (*Stores)[(int)GetCtrlValue(IDC_MSG_STORE)];
			if (!e)
			{
				auto Dlg = new AddFolderDlg(this, Io, e);
				Dlg->DoModal([this, Dlg](auto dlg, auto ctrlId)
				{
					if (ctrlId)
						AddPath(Dlg->Path);
				});
			}
		}
	}

	int OnNotify(LViewI *v, LNotification n)
	{
		bool Ok = false;

		switch (v->GetId())
		{
			case IDC_SET_FOLDER:
			{
				auto Dlg = new FolderDlg(this, App);
				Dlg->DoModal([this, Dlg](auto dlg, auto ctrlId)
				{
					if (ctrlId)
						SetCtrlName(IDC_FOLDER, Dlg->Get());
				});
				break;
			}
			case IDOK:
			{
				Ok = true;
				// fall thru
			}
			case IDCANCEL:
			{
				LVariant k;
				const char *s;
				if (SrcFolders)
				{
					Params->Import.Empty();
					Params->Import.SetFixedLength(false);
					for (auto i : *SrcFolders)
						Params->Import.New() = i->GetText(0);
					Params->Import.SetFixedLength(true);

					auto s = LString(",").Join(Params->Import);
					if (s)
						App->GetOptions()->SetValue(OPT_OutlookImportSrc, k = s);
					else
						App->GetOptions()->DeleteValue(OPT_OutlookImportSrc);
				}
				s = GetCtrlName(IDC_FOLDER);
				if (s)
				{
					App->GetOptions()->SetValue(OPT_OutlookImportDst, k = s);

					if (Ok)
						*OurFolder = App->GetFolder(s);
				}
				else
				{
					App->GetOptions()->DeleteValue(OPT_OutlookImportDst);
				}

				Params->AllFolders = GetCtrlValue(IDC_ALL) != 0;
				App->GetOptions()->SetValue(OPT_OutlookImportAll, k = (int)Params->AllFolders);

				if (Ok && Io->GetSession() && Stores)
				{
					EntryRef *e = (*Stores)[(int)GetCtrlValue(IDC_MSG_STORE)];
					if (e)
					{
						e->OpenRoot(Io->GetSession(), Io->GetUiHnd(), Io->MsgStore, *MapiFolder);
					}
				}

				EndModal(v->GetId());
				break;
			}
		}

		return IoDlg::OnNotify(v, n);
	}
};

class ExportDlg : public IoDlg
{
	ExportParams *Params;

public:
	ExportDlg(	ScribeWnd *app,
				ExportParams *params,
				OutlookIO *io,
				LComPtr<IMAPIFolder> *MapiFolder,
				ScribeFolder *&ScribeFolder) :
		IoDlg(app, io, MapiFolder, ScribeFolder)
	{
		SetParent(app);
		Params = params;

		if (LoadFromResource(IDD_OUTLOOK_EXPORT))
		{
			MoveToCenter();
			OnLogin(false);
			GetViewById(IDC_SRC_FOLDERS, SrcFolders);

			SetCtrlValue(IDC_NO_SPAM_TRASH, true);

			LVariant v;
			if (App->GetOptions()->GetValue(OPT_OutlookExportSrc, v) && v.Str())
			{
				auto t = v.LStr().SplitDelimit(",");
				for (unsigned i=0; i<t.Length(); i++)
				{
					AddPath(t[i]);
				}
			}
			if (App->GetOptions()->GetValue(OPT_OutlookExportDst, v))
			{
				SetCtrlName(IDC_FOLDER, v.Str());
			}
			if (App->GetOptions()->GetValue(OPT_OutlookExportAll, v))
			{
				SetCtrlValue(IDC_ALL, v.CastInt32());
			}
			if (App->GetOptions()->GetValue(OPT_OutlookExportExclude, v))
			{
				SetCtrlValue(IDC_NO_SPAM_TRASH, v.CastInt32());
			}

			InitSrcClients();
		}
	}

	void OnLogin(bool e)
	{
		SetCtrlEnabled(IDC_CLIENT, !e);
		SetCtrlEnabled(IDC_LOGIN, !e);

		SetCtrlEnabled(IDC_MSG_STORE, e);
		SetCtrlEnabled(IDC_ALL, e);
		SetCtrlEnabled(IDC_NO_SPAM_TRASH, e);
		SetCtrlEnabled(IDC_SRC_FOLDERS, e);
		SetCtrlEnabled(IDC_ADD_SRC_FOLDER, e);
		SetCtrlEnabled(IDC_DEL_SRC_FOLDER, e);
		SetCtrlEnabled(IDC_SET_FOLDER, e);
		SetCtrlEnabled(IDOK, e);

		SetCtrlEnabled(IDC_FOLDER, false);
	}

	void AddFolder()
	{
		auto Fs = new FolderDlg(this, App);
		Fs->DoModal([this, Fs](auto dlg, auto ctrlId)
		{
			if (ctrlId && Fs->Get())
				AddPath(Fs->Get());
		});
	}

	LComPtr<IMAPIFolder> GetSubFolderByName(IMAPIFolder *f, char *name)
	{
		LComPtr<IMAPIFolder> ChildFolder;

		if (f && name)
		{
			LPMAPITABLE Folders = 0;
			if (f->GetHierarchyTable(0, &Folders) == S_OK)
			{
				for (LMapiList Lst(Folders); Lst.More(); Lst.Next())
				{
					SPropValue *v = Lst.GetField(PR_ENTRYID);
					char *Name = MapiCastString(Lst.GetField(PR_DISPLAY_NAME));
					if (v && Name)
					{
						if (_stricmp(Name, name) == 0)
						{
							ULONG Type;
							f->OpenEntry(v->Value.bin.cb, (LPENTRYID)v->Value.bin.lpb, NULL, MAPI_BEST_ACCESS, &Type, (IUnknown**)ChildFolder.Set());
						}
					}					
				}
			}
		}

		return ChildFolder;
	}

	int OnNotify(LViewI *c, LNotification n)
	{
		switch (c->GetId())
		{
			case IDC_SET_FOLDER:
			{
				if (Stores)
				{
					EntryRef *e = (*Stores)[(int)GetCtrlValue(IDC_MSG_STORE)];
					if (e)
					{
						auto Dlg = new AddFolderDlg(this, Io, e);
						Dlg->DoModal([this, Dlg](auto dlg, auto id)
						{
							if (id)
								SetCtrlName(IDC_FOLDER, Dlg->Path);
						});
					}
				}
				break;
			}
			case IDOK:
			{
				Params->Include.Empty();
				Params->AllFolders = GetCtrlValue(IDC_ALL) != 0;
				for (auto i : *SrcFolders)
				{
					ScribeFolder *f = App->GetFolder(i->GetText(0));
					if (f)
					{
						if (!Params->Export.HasItem(f))
						{
							Params->Export.Insert(f);
						}

						if (!Params->Include.HasItem(f))
						{
							Params->Include.Insert(f);

							ScribeFolder *p = f;
							while (p = p->GetFolder())
							{
								if (!Params->Include.HasItem(p))
								{
									Params->Include.Insert(p);
								}
							}
						}
					}
				}

				if (GetCtrlValue(IDC_NO_SPAM_TRASH))
				{
					Params->Exclude.Insert(App->GetFolder("/Spam"));
					Params->Exclude.Insert(App->GetFolder(FOLDER_TRASH));
				}

				// Get the scribe folder to start at...
				*OurFolder = App->GetFolder("/");

				// Get the mapi folder to start at..
				if (Io->GetSession() && Stores)
				{
					EntryRef *e = (*Stores)[(int)GetCtrlValue(IDC_MSG_STORE)];
					if (e)
					{
						// Open the store...
						e->OpenRoot(Io->GetSession(), Io->GetUiHnd(), Io->MsgStore, *MapiFolder);

						// Drill down to the select sub-folder...
						auto t = LString(GetCtrlName(IDC_FOLDER)).SplitDelimit("/");
						if (t.Length() > 0)
						{
							LComPtr<IMAPIFolder> f = *MapiFolder;
							for (unsigned i=0; i<t.Length(); i++)
							{
								f = GetSubFolderByName(f, t[i]);
								if (!f)
									break;
							}
							*MapiFolder = f;
						}
					}
				}

				// Fall thru
			}
			case IDCANCEL:
			{
				LVariant v;
				LStringPipe p;
				for (auto i : *SrcFolders)
				{
					if (p.GetSize()) p.Push(",");
					p.Push(i->GetText(0));
				}
				char *Fld = p.NewStr();
				if (Fld)
				{
					App->GetOptions()->SetValue(OPT_OutlookExportSrc, v = Fld);
					DeleteArray(Fld);
				}
				else
				{
					App->GetOptions()->DeleteValue(OPT_OutlookExportSrc);
				}
				App->GetOptions()->SetValue(OPT_OutlookExportDst, v = GetCtrlName(IDC_FOLDER));
				App->GetOptions()->SetValue(OPT_OutlookExportAll, v = GetCtrlValue(IDC_ALL));
				App->GetOptions()->SetValue(OPT_OutlookExportExclude, v = GetCtrlValue(IDC_NO_SPAM_TRASH));

				EndModal(c->GetId() == IDOK);
				break;
			}
		}

		return IoDlg::OnNotify(c, n);
	}
};

void Import_Outlook(ScribeWnd *Wnd, int Flags)
{
	OutlookIO Imp(Wnd, Flags);
}

void Export_Outlook(ScribeWnd *Wnd)
{
	OutlookIO Exp(Wnd, EXP_OUTLOOK_EMAIL);
}

OutlookIO::OutlookIO(ScribeWnd *Wnd, int Flags, ScribeAccount *account)
{
	App = Wnd;
	hMapi = NULL;
	Ui = (UI_TYPE) ((Wnd) ? Wnd->Handle() : 0);
	MAPIInitialize = NULL;
	MAPILogonEx = NULL;
	MAPIUninitialize = NULL;
	MAPIAllocateBuffer = NULL;
	MAPIFreeBuffer = NULL;
	WrapCompressedRTFStream = NULL;
	Account = account;

	ImportParams IParams;
	IParams.App = App;

	ExportParams EParams;
	EParams.App = App;

	LComPtr<IMAPIFolder> MapiFolder;
	ScribeFolder *OurFolder = NULL;

	#if 0 // FIXME
	if (Flags == IMP_OUTLOOK)
	{
		ImportDlg Dlg(Wnd, &IParams, this, &MapiFolder, OurFolder);
		if (Dlg.DoModal() == IDOK)
		{
		}
		else return;
	}
	else if (Flags == EXP_OUTLOOK_EMAIL)
	{
		ExportDlg Dlg(Wnd, &EParams, this, &MapiFolder, OurFolder);
		if (Dlg.DoModal() == IDOK)
		{
		}
		else return;
	}

	if (Session &&
		MapiFolder &&
		OurFolder)
	{
		if (Flags == EXP_OUTLOOK_EMAIL)
		{
			LProgressDlg *Prog = new LProgressDlg(Wnd);
			if (Prog)
			{
				Prog->SetDescription("Initializing...");
				if (EParams.Export.Length())
				{
					Prog->SetRange(EParams.Export.Length());
				}
				else
				{
					LMailStore *Ms = App->GetDefaultMailStore();
					Prog->SetRange(Ms ? CountFolders(Ms->Root) : 0);
				}
				EParams.Prog = Prog->ItemAt(0);
				EParams.ItemProg = Prog->Push();
				if (EParams.ItemProg)
				{
					EParams.ItemProg->SetDescription("Items...");
				}
			}

			for (auto *f = OurFolder->GetChildFolder(); f; f = f->GetNextFolder())
			{
				Export(&EParams, f, MapiFolder);
			}
			DeleteObj(Prog);
		}
		else
		{
			if (Flags == IMP_OUTLOOK_PAB)
			{
				ImportPersonalAddressBook();
			}
			else
			{
				LProgressDlg *Prog = new LProgressDlg(Wnd);
				if (Prog)
				{
					Prog->SetDescription("Initializing...");
					if (IParams.Import.Length())
					{
						Prog->SetRange(IParams.Import.Length());
					}
					else
					{
						LMailStore *Ms = App->GetDefaultMailStore();
						Prog->SetRange(Ms ? CountFolders(Ms->Root) : 0);
					}
					IParams.Prog = Prog->ItemAt(0);
					IParams.ItemProg = Prog->Push();
					if (IParams.ItemProg)
					{
						IParams.ItemProg->SetDescription("Items...");
					}
				}

				LString::Array Path;
				Import(&IParams, MapiFolder, OurFolder, Path);
				DeleteObj(Prog);
			}
		}
	}
	#endif

	if (MsgStore)
		MsgStore.Release();
}

OutlookIO::~OutlookIO()
{
	if (AddrBook)
		AddrBook.Release();

	if (Session)
	{
		Session->Logoff(0, 0, 0);
		Session.Release();
	}

	if (MsgStore)
		MsgStore.Release();

	if (MAPIUninitialize)
	{
		MAPIUninitialize();
	}

	if (hMapi)
	{
		FreeLibrary(hMapi);
		hMapi = NULL;
	}
}

bool OutlookIO::LoadSession(LViewI *Parent, char *ResetClient)
{
	bool Status = false;
    char Cur[MAX_PATH_LEN];
    GetCurrentDirectory(sizeof(Cur), Cur);

	DefClient.Reset(NewStr(ResetClient));

	HRESULT Error = S_OK;
	hMapi = LoadLibraryA("mapi32.dll");
	if (hMapi)
	{
		MAPIInitialize			= (MAPIINITIALIZE*)				GetProcAddress(hMapi, "MAPIInitialize");
		MAPILogonEx				= (MAPILOGONEX*)				GetProcAddress(hMapi, "MAPILogonEx");
		MAPIAllocateBuffer		= (MAPIALLOCATEBUFFER*)			GetProcAddress(hMapi, "MAPIAllocateBuffer");
		MAPIFreeBuffer			= (MAPIFREEBUFFER*)				GetProcAddress(hMapi, "MAPIFreeBuffer");
		WrapCompressedRTFStream = (pWrapCompressedRTFStream)	GetProcAddress(hMapi, "WrapCompressedRTFStream");

		if (MAPIInitialize &&
			MAPILogonEx &&
			MAPIAllocateBuffer &&
			MAPIFreeBuffer &&
			MAPIInitialize(NULL) == S_OK)
		{
			MAPIUninitialize = (MAPIUNINITIALIZE*)	GetProcAddress(hMapi, "MAPIUninitialize");
			
			if (!Account)
			{
				if (MAPILogonEx(Parent ? (UI_TYPE)Parent->Handle() : Ui,
								NULL,
								NULL,
								MAPI_LOGON_UI | MAPI_EXTENDED,
								Session.Set()) == S_OK &&
					Session)
				{
                    SetCurrentDirectory(Cur);
					Status = true;
				}
				else
				{
					LgiMsg(App, LLoadString(IDS_MAPI_LOGIN_FAILED), AppName);
				}
			}
		}
		else
		{
			LgiMsg(App, LLoadString(IDS_ERROR_MAPI_INIT_FAILED), AppName);
		}
	}
	else
	{
		LgiMsg(App, LLoadString(IDS_ERROR_MAPI_NOT_INSTALLED), AppName);
	}

	if (!Status && hMapi)
	{
		MAPIUninitialize();
		MAPIUninitialize = NULL;
		MAPIInitialize = NULL;
		MAPILogonEx = NULL;
		MAPIAllocateBuffer = NULL;
		MAPIFreeBuffer = NULL;
		WrapCompressedRTFStream = NULL;

		FreeLibrary(hMapi);
		hMapi = NULL;
	}

	return Status;
}

Mail *MatchEmail(ScribeFolder *f, Mail *m1)
{
	f->LoadThings();

	for (auto t: f->Items)
	{
		Mail *m2 = t->IsMail();
		if (m2)
		{
			const char *id1, *id2;
			if ((id1 = m1->GetMessageId()) &&
				(id2 = m2->GetMessageId()) &&
				strcmp(id1, id2) == 0)
			{
				return m2;
			}
		}
	}

	for (auto t: f->Items)
	{
		Mail *m2 = t->IsMail();
		if (m2)
		{
			if (m1->GetSubject() &&
				m2->GetSubject() &&
				strcmp(m1->GetSubject(), m2->GetSubject()) == 0 &&
				*m1->GetDateSent() == *m2->GetDateSent())
			{
				return m2;
			}
		}
	}

	return 0;
}

int MapNetCpToWin(int Cp)
{
	switch (Cp)
	{
		case 708:
			return 1256;
		case 720:
			return 1256;
		case 28596:
			return 1256;
		case 10004:
			return 1256;
		case 1256:
			return 1256;
		case 775:
			return 1257;
		case 28594:
			return 1257;
		case 1257:
			return 1257;
		case 852:
			return 1250;
		case 28592:
			return 1250;
		case 10029:
			return 1250;
		case 1250:
			return 1250;
		case 51936:
			return 936;
		case 936:
			return 936;
		case 52936:
			return 936;
		case 10008:
			return 936;
		case 950:
			return 950;
		case 20000:
			return 950;
		case 20002:
			return 950;
		case 10002:
			return 950;
		case 866:
			return 1251;
		case 28595:
			return 1251;
		case 20866:
			return 1251;
		case 21866:
			return 1251;
		case 10007:
			return 1251;
		case 1251:
			return 1251;
		case 29001:
			return 1252;
		case 20106:
			return 1252;
		case 737:
			return 1253;
		case 28597:
			return 1253;
		case 10006:
			return 1253;
		case 1253:
			return 1253;
		case 869:
			return 1253;
		case 862:
			return 1255;
		case 38598:
			return 1255;
		case 28598:
			return 1255;
		case 10005:
			return 1255;
		case 1255:
			return 1255;
		case 20420:
			return 1256;
		case 20880:
			return 1251;
		case 21025:
			return 1251;
		case 20277:
			return 1252;
		case 1142:
			return 1252;
		case 20278:
			return 1252;
		case 1143:
			return 1252;
		case 1147:
			return 1252;
		case 20273:
			return 1252;
		case 1141:
			return 1252;
		case 875:
			return 1253;
		case 20423:
			return 1253;
		case 20424:
			return 1255;
		case 20871:
			return 1252;
		case 1149:
			return 1252;
		case 1148:
			return 1252;
		case 20280:
			return 1252;
		case 1144:
			return 1252;
		case 50930:
			return 932;
		case 50939:
			return 932;
		case 50931:
			return 932;
		case 20290:
			return 932;
		case 50933:
			return 949;
		case 20833:
			return 949;
		case 870:
			return 1250;
		case 50935:
			return 936;
		case 20284:
			return 1252;
		case 1145:
			return 1252;
		case 20838:
			return 874;
		case 50937:
			return 950;
		case 1026:
			return 1254;
		case 20905:
			return 1254;
		case 20285:
			return 1252;
		case 1146:
			return 1252;
		case 37:
			return 1252;
		case 1140:
			return 1252;
		case 861:
			return 1252;
		case 10079:
			return 1252;
		case 57006:
			return 57006;
		case 57003:
			return 57003;
		case 57002:
			return 57002;
		case 57010:
			return 57010;
		case 57008:
			return 57008;
		case 57009:
			return 57009;
		case 57007:
			return 57007;
		case 57011:
			return 57011;
		case 57004:
			return 57004;
		case 57005:
			return 57005;
		case 51932:
			return 932;
		case 50220:
			return 932;
		case 50222:
			return 932;
		case 50221:
			return 932;
		case 10001:
			return 932;
		case 932:
			return 932;
		case 949:
			return 949;
		case 51949:
			return 949;
		case 50225:
			return 949;
		case 1361:
			return 1361;
		case 10003:
			return 949;
		case 28593:
			return 1254;
		case 28605:
			return 1252;
		case 20108:
			return 1252;
		case 437:
			return 1252;
		case 20107:
			return 1252;
		case 874:
			return 874;
		case 857:
			return 1254;
		case 28599:
			return 1254;
		case 10081:
			return 1254;
		case 1254:
			return 1254;
		case 1200:
			return 1200;
		case 1201:
			return 1200;
		case 65000:
			return 1200;
		case 65001:
			return 1200;
		case 20127:
			return 1252;
		case 1258:
			return 1258;
		case 850:
			return 1252;
		case 20105:
			return 1252;
		case 28591:
			return 1252;
		case 10000:
			return 1252;
		case 1252:
			return 1252;
	}

	return 0;
}

LAutoString MapiToUtf(const char *Charset, const char *Text)
{
	LAutoString s;

	char16 *w = (char16*) LNewConvertCp(LGI_WideCharset, Text, Charset ? Charset : LAnsiToLgiCp());
	if (w)
	{
		s.Reset(WideToUtf8(w));
		DeleteArray(w);
	}

	return s;
}

bool OutlookIO::ImportEmail(ScribeFolder *Folder, Mail *&To, IMessage *From, bool Post)
{
	if (!To)
	    return false;
	
	if (!From)
	{
	    To->DecRef();
	    return false;
	}
	
	char *MsgId = MapiGetPropStr(From, PR_INTERNET_MESSAGE_ID);
	if (MsgId)
	{
		To->SetMessageId(MsgId);
	}

	// Get the codepage...
	int CodePage = MapNetCpToWin(MapiGetPropInt(From, 0x3FDE0003));
	const char *Charset = LAnsiToLgiCp(CodePage);

	// Get Internet Header fairly early in the peice to allow us to
	// extract the codepage for the body of the message.
	char *Header = MapiGetPropStr(From, PR_TRANSPORT_MESSAGE_HEADERS);
	if (Header)
	{
		To->SetInternetHeader(Header);
	}

	// Get subject
	char *Subject = MapiGetPropStr(From, PR_SUBJECT);
	if (Subject)
	{
	    LAutoString a = MapiToUtf(Charset, Subject);
		To->SetSubject(a);
	}

	// Get sent date
	SPropValue *Sent = MapiGetProp(From, PR_CLIENT_SUBMIT_TIME);
	if (Sent)
	{
		SYSTEMTIME st;
		FileTimeToSystemTime(&Sent->Value.ft, &st);
		LDateTime dt;
		dt.Day(st.wDay);
		dt.Month(st.wMonth);
		dt.Year(st.wYear);
		dt.Minutes(st.wMinute);
		dt.Hours(st.wHour);
		dt.Seconds(st.wSecond);
		To->SetDateSent(&dt);
	}

	// Do replication side of things...
	Mail *Match = MatchEmail(Folder, To);
	if (Match)
	{
	    To->DecRef();
		return true;
	}

	// Get from
	char *SenderEmail = MapiGetPropStr(From, PR_SENDER_EMAIL_ADDRESS);
	char *SenderName = MapiGetPropStr(From, PR_SENT_REPRESENTING_NAME);
	if (!SenderName)
		SenderName = MapiGetPropStr(From, PR_SENDER_NAME);
	
	
	To->GetFrom()->SetStr(FIELD_NAME, SenderName);

	if (SenderEmail && strchr(SenderEmail, '@'))
	{
		To->GetFrom()->SetStr(FIELD_EMAIL, SenderEmail);
	}
	else
	{
		// Ok we have to look up their email address ourselves.
		SPropValue *EmailEntryId = MapiGetProp(From, PR_SENDER_ENTRYID);
		if (EmailEntryId)
		{
			if (!AddrBook)
				Session->OpenAddressBook(Ui, NULL, 0, AddrBook.Set());

			if (AddrBook)
			{
				ULONG Type = 0;
				IMailUser *User = 0;
				if (AddrBook->OpenEntry(EmailEntryId->Value.bin.cb,
										(LPENTRYID) EmailEntryId->Value.bin.lpb,
										0,
										MAPI_BEST_ACCESS,
										&Type,
										(IUnknown**)&User) == S_OK &&
					User)
				{
					char *E1 = MapiGetPropStr(User, PR_EMAIL_ADDRESS);
					char *E2 = MapiGetPropStr(User, PR_SMTP_ADDRESS);
					if (E1 && strchr(E1, '@'))
					{
						To->GetFrom()->SetStr(FIELD_EMAIL, E1);
					}
					else if (E2 && strchr(E2, '@'))
					{
						To->GetFrom()->SetStr(FIELD_EMAIL, E2);
					}
				}
			}
		}
	}

	// for all recipients
	LPMAPITABLE Recipients = 0;
	if (From->GetRecipientTable(0, &Recipients) == S_OK &&
		Recipients)
	{
		for (LMapiList Lst(Recipients); Lst.More(); Lst.Next())
		{
			SPropValue *Name = Lst.GetField(PR_DISPLAY_NAME);
			SPropValue *Type = Lst.GetField(PR_RECIPIENT_TYPE);
			SPropValue *Email1 = Lst.GetField(PR_EMAIL_ADDRESS);
			SPropValue *Email2 = Lst.GetField(PR_SMTP_ADDRESS);
			if (Name || Email1 || Email2)
			{
				LDataPropI *r = To->GetTo()->Create(To->GetObject()->GetStore());
				if (r)
				{
					if (Name)
					{
						r->SetStr(FIELD_NAME, MapiCastString(Name));
					}

					char *E1 = (Email1) ? MapiCastString(Email1) : 0;
					char *E2 = (Email2) ? MapiCastString(Email2) : 0;
					if (E1 && strchr(E1, '@'))
					{
						r->SetStr(FIELD_EMAIL, E1);
					}
					else if (E2 && strchr(E2, '@'))
					{
						r->SetStr(FIELD_EMAIL, E2);
					}

					if (Type)
					{
						LONG Flags = Type->Value.ul;
						if (Flags & MAPI_TO)
						{
							r->SetInt(FIELD_CC, 0);
						}
						else if (Flags & MAPI_CC)
						{
							r->SetInt(FIELD_CC, 1);
						}
						else if (Flags & MAPI_BCC)
						{
							r->SetInt(FIELD_CC, 2);
						}
					}

					To->GetTo()->Insert(r);
				}
			}
		}
	}

	// Get Body/Text
	char *Body = MapiGetPropStr(From, PR_BODY);
	if (Body)
	{
	    LAutoString a = MapiToUtf(Charset, Body);
		To->SetBody(a);
		To->SetBodyCharset("utf-8");
	}

	char *Html = MapiGetPropStr(From, PR_BODY_HTML);
	if (Html)
	{
		To->SetHtml(Html);
	}
	else
	{
		// Rtf/HTML
		IStream *Comp = 0;
		if (WrapCompressedRTFStream &&
			From->OpenProperty(	PR_RTF_COMPRESSED,
								&IID_IStream,
								0,
								0,
								(IUnknown**) &Comp) == S_OK)
		{
			IStream *Uncomp = 0;
			if (WrapCompressedRTFStream(Comp, 0, &Uncomp) == S_OK)
			{
				LStringPipe p;
				char Buf[1024];
				ULONG r;
				while (Uncomp->Read(Buf, sizeof(Buf), &r) == S_OK)
				{
					if (r)
					{
						p.Push(Buf, r);
					}
					else break;
				}
				char *Rtf = p.NewStr();
				if (Rtf)
				{
					{
						int i=0;
						char p[256];
						for (; true; i++)
						{
							#ifdef WINDOWS
							sprintf_s(p, sizeof(p), "c:\\temp\\rtf-%i.txt", i);
							#else
							sprintf_s(p, sizeof(p), "/tmp/rtf-%i.txt", i);
							#endif
							if (!LFileExists(p)) break;
						}

						LFile f;
						if (f.Open(p, O_WRITE))
						{
							f.Write(Rtf, strlen(Rtf));
						}
					}

					LAutoString HtmlStr(MsRtfToHtml(Rtf));
					To->SetHtml(HtmlStr);
					DeleteArray(Rtf);
				}

				Uncomp->Release();
			}
			Comp->Release();
		}
	}

	// Get Attachments
	LPMAPITABLE Attachments = 0;
	if (From->GetAttachmentTable(0, &Attachments) == S_OK &&
		Attachments)
	{
		for (LMapiList Lst(Attachments); Lst.More(); Lst.Next())
		{
			SPropValue *Num = Lst.GetField(PR_ATTACH_NUM);
			if (!Num)
			    continue;

			IAttach *Attach = 0;
			if (From->OpenAttach(Num->Value.ul, NULL, MAPI_BEST_ACCESS, &Attach) != S_OK || !Attach)
			    continue;

			SPropValue *Method = MapiGetProp(Attach, PR_ATTACH_METHOD);
			if (Method)
			{
			    char *AttachName = MapiGetPropStr(Attach, PR_ATTACH_LONG_FILENAME);
			    char *ContentId = MapiGetPropStr(Attach, PR_ATTACH_CONTENT_ID);
			    switch (Method->Value.ul)
			    {
				    case ATTACH_BY_VALUE:
				    {
					    Attachment *File = 0;
					    IStream *Stream = 0;
					    if (Attach->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, 0, 0, (IUnknown**)&Stream) == S_OK &&
						    Stream)
					    {
						    STATSTG Stat;
						    if (Stream->Stat(&Stat, STATFLAG_DEFAULT) == S_OK)
						    {
							    int Size = Stat.cbSize.LowPart;
							    char *Data = new char[Size];
							    if (Data)
							    {
								    int Block = 128 << 10;
								    char *p = Data; 
								    for (int i=0; i<Size; i+=Block)
								    {
									    auto Len = min(Size - (p - Data), Block);
									    if (Stream->Read(p, (ULONG)Len, NULL) != S_OK)
									    {
										    break;
									    }

									    p += Len;
								    }

								    if (p == Data + Size)
								    {
									    File = new Attachment(Folder->App);
									    if (File)
									    {
										    File->Set(Data, Size);
									    }
								    }

								    DeleteArray(Data);
							    }
						    }

						    Stream->Release();
					    }

					    if (File)
					    {
					        char *MimeType = MapiGetPropStr(Attach, PR_ATTACH_MIME_TAG);
					        File->SetMimeType(MimeType ? MimeType : "application/octet-stream");
						    
						    if (AttachName)
						    {
							    File->SetName(AttachName);
						    }
						    else
						    {
							    char Str[256];
							    sprintf_s(Str, sizeof(Str), "file%i", Num->Value.ul);
							    File->SetName(Str);
						    }

						    if (ContentId)
						    {
							    File->SetContentId(ContentId);
						    }

						    To->AttachFile(File);

						    // remove attachment from memory
						    if (File->GetObject())
						    {
							    /* FIXME
							    File->Store->Object = 0;
							    File->Store = 0;
							    DeleteObj(File);
							    */
						    }
					    }
					    break;
				    }
				    case ATTACH_BY_REFERENCE:
				    case ATTACH_BY_REF_RESOLVE:
				    case ATTACH_BY_REF_ONLY:
				    {
					    char *FileName = MapiGetPropStr(Attach, PR_ATTACH_LONG_PATHNAME);
					    if (FileName)
					    {
						    To->AttachFile(App, FileName);
					    }																		
					    break;
				    }
				    case ATTACH_EMBEDDED_MSG:
				    {
					    break;
				    }
			    }
			}

		    Attach->Release();
		}
	} 
	
	// Set Sent/Received/Read/Unread Flags
	SPropValue *Flags = MapiGetProp(From, PR_MESSAGE_FLAGS);
	if (Flags)
	{
		ulong f = 0;
		if (Flags->Value.ul & (MSGFLAG_SUBMIT | MSGFLAG_UNSENT) && 
			!Post)
		{
			f |= MAIL_CREATED;
		}
		else
		{
			f |= MAIL_RECEIVED;
		}

		if (Flags->Value.ul & MSGFLAG_HASATTACH)
		{
			f |= MAIL_ATTACHMENTS;
		}
		if (Flags->Value.ul & MSGFLAG_READ)
		{
			f |= MAIL_READ;
		}

		To->SetFlags(f);
	}

	To->Update();
	To->Save(Folder);
	To = NULL;

	return true;
}

bool PropToDate(SPropValue *p, LDateTime &dt)
{
	if (p && PROP_TYPE(p->ulPropTag) == PT_SYSTIME)
	{
		uint64 i = (((uint64)p->Value.ft.dwHighDateTime) << 32) | p->Value.ft.dwLowDateTime;
		return dt.Set(i);
	}
	return false;
}

Contact *MatchContact(ScribeFolder *f, Contact *c1)
{
	f->LoadThings();

	for (auto t: f->Items)
	{
		Contact *c2 = t->IsContact();
		if (c2)
		{
			#define CmpOpt(name) \
			{ \
				const char *s1, *s2; \
				if (!c1->Get(name, s1) || \
					!c2->Get(name, s2) || \
					_stricmp(s1, s2) != 0) \
				{ \
					continue; \
				} \
			}

			CmpOpt(OPT_First);
			CmpOpt(OPT_Last);
			CmpOpt(OPT_Email);

			return c2;
		}
	}

	return 0;
}

Calendar *MatchCalendar(ScribeFolder *f, Calendar *c1)
{
	f->LoadThings();

	for (auto t: f->Items)
	{
		Calendar *c2 = t->IsCalendar();
		if (c2)
		{
			LDateTime d1, d2;
			if (c1->GetField(FIELD_CAL_START_UTC, d1) &&
				c2->GetField(FIELD_CAL_START_UTC, d2) &&
				d1 != d2)
			{
				continue;
			}
			if (c1->GetField(FIELD_CAL_END_UTC, d1) &&
				c2->GetField(FIELD_CAL_END_UTC, d2) &&
				d1 != d2)
			{
				continue;
			}
			const char *s1, *s2;
			if (c1->GetField(FIELD_CAL_SUBJECT, s1) &&
				c2->GetField(FIELD_CAL_SUBJECT, s2) &&
				_stricmp(s1, s2) != 0)
			{
				continue;
			}
			int t1, t2;
			if (c1->GetField(FIELD_CAL_TYPE, t1) &&
				c2->GetField(FIELD_CAL_TYPE, t2) &&
				t1 != t2)
			{
				continue;
			}

			return c2;
		}
	}

	return 0;
}

bool OutlookIO::ImportItem(ScribeFolder *Out, IMAPIFolder *In, SPropValue *EntryId)
{
	bool Status = false;
	if (Out && In && EntryId)
	{
        LDataStoreI::StoreTrans StoreTransaction = Out->GetObject()->GetStore()->StartTransaction();
		
		ULONG Type = 0;
		IUnknown *Item = 0;
		HRESULT e;
		if ((e = In->OpenEntry(	EntryId->Value.bin.cb,
								(LPENTRYID)EntryId->Value.bin.lpb,
								NULL,
								MAPI_BEST_ACCESS,
								&Type,
								&Item)) == S_OK &&
			Item)
		{
			switch (Type)
			{
				case MAPI_MESSAGE:
				{
					IMessage *Msg = 0;
					if (Item->QueryInterface(IID_IMessage, (void**)&Msg) == S_OK && Msg)
					{
						SPropValue *Class = MapiGetProp(Msg, PR_ORIG_MESSAGE_CLASS);
						if (!Class)
						{
							Class = MapiGetProp(Msg, PR_MESSAGE_CLASS);
						}

						if (Class)
						{
							char *sClass = MapiCastString(Class);
							bool Note = _strnicmp(sClass, "IPM.Note", 8) == 0;
							bool Post = _strnicmp(sClass, "IPM.Post", 8) == 0;
							bool Document = _strnicmp(sClass, "IPM.Document", 12) == 0;

							if (Note || Document || Post)
							{
								// Email
								Mail *Email = new Mail(Out->App);
								if (Email)
								{
									Email->App = App;
									Status = ImportEmail(Out, Email, Msg, Post);
								}
								LAssert(Email == NULL);
							}
							else if (_stricmp(sClass, "IPM.Contact") == 0)
							{
								// Contact
								Contact *c = new Contact(Out->App);
								if (c)
								{
									c->App = App;

									#define ConvertStr(opt, tag) \
										{ auto s = LFromNativeCp(MapiGetPropStr(Msg, tag)); \
										c->Set(opt, s); }

									ConvertStr(OPT_First, PR_GIVEN_NAME);
									ConvertStr(OPT_Last, PR_SURNAME);
									
									char *Email = 0;
									#define CheckTagForEmail(tag) \
										if (!Email) { char *e = MapiGetPropStr(Msg, tag); \
										if (e && strchr(e, '@')) Email = e; }
									
									CheckTagForEmail(0x81b0001e);
									CheckTagForEmail(0x805d001e);
									CheckTagForEmail(0x8060001e);
									CheckTagForEmail(0x81ae001e);

									c->Set(OPT_Email, Email);

									Contact *Match = MatchContact(Out, c);
									if (Match)
									{
										DeleteObj(c);
										c = Match;
									}

									ConvertStr(OPT_HomeStreet, PR_STREET_ADDRESS);
									ConvertStr(OPT_HomeSuburb, PR_LOCALITY); // city
									ConvertStr(OPT_HomePostcode, PR_POSTAL_CODE);
									ConvertStr(OPT_HomeState, PR_STATE_OR_PROVINCE);
									ConvertStr(OPT_HomeCountry, PR_COUNTRY);
									ConvertStr(OPT_WorkPhone, PR_BUSINESS_TELEPHONE_NUMBER);
									ConvertStr(OPT_HomePhone, PR_HOME_TELEPHONE_NUMBER);
									ConvertStr(OPT_HomeMobile, PR_CELLULAR_TELEPHONE_NUMBER);
									ConvertStr(OPT_HomeFax, PR_PRIMARY_FAX_NUMBER);
									char *WebPage = MapiGetPropStr(Msg, PR_PERSONAL_HOME_PAGE);
									if (!WebPage) WebPage = MapiGetPropStr(Msg, PR_BUSINESS_HOME_PAGE);
									c->Set(OPT_HomeWebPage, WebPage);
									ConvertStr(OPT_Nick, PR_NICKNAME);
									ConvertStr(OPT_Spouse, PR_SPOUSE_NAME);
									ConvertStr(OPT_Note, PR_BODY);

									c->Save(Out);

									Status = true;
								}
							}
							else if (_stricmp(sClass, "IPM.Appointment") == 0)
							{
								Calendar *c = new Calendar(Out->App);
								if (c)
								{
									c->App = App;

									bool Historical = false;
									LDateTime dt, Now;
									Now.SetNow();
									SPropValue *p = MapiGetProp(Msg, PR_START_DATE);
									if (PropToDate(p, dt))
									{
										Historical = dt < Now;
										c->SetField(FIELD_CAL_START_UTC, dt);
									}
									p = MapiGetProp(Msg, PR_END_DATE);
									if (PropToDate(p, dt))
									{
										c->SetField(FIELD_CAL_END_UTC, dt);
									}
									char *s = MapiGetPropStr(Msg, PR_SUBJECT);
									if (s) c->SetField(FIELD_CAL_SUBJECT, s);

									Calendar *Match = MatchCalendar(Out, c);
									if (Match)
									{
									    c->DecRef();
										c = Match;
									}

									s = MapiGetPropStr(Msg, 0x810C001E);
									if (s) c->SetField(FIELD_CAL_LOCATION, s);
									s = MapiGetPropStr(Msg, PR_BODY);
									if (s) c->SetField(FIELD_CAL_NOTES, s);

									// Only import alarms for future objects, otherwise a replicate from
									// Outlook -> Scribe sets off all the old alarms. Which is annoying.
									if (!Historical)
									{
										p = MapiGetProp(Msg, 0x81EE000B);
										if (p && p->Value.b)
										{
											/* FIXME
											c->SetField(FIELD_CAL_REMINDER_ACTION, true);
											int i = MapiCastInt(MapiGetProp(Msg, 0x81E90003));
											c->SetField(FIELD_CAL_REMINDER_TIME, -i);
											*/
										}
									}

									// Erm, you'd usually have to map the names here, but I used the Outlook list
									// to write my calendar, so it doesn't need converting... ;)
									int ShowAs = MapiCastInt(MapiGetProp(Msg, 0x81930003));
									c->SetField(FIELD_CAL_SHOW_TIME_AS, ShowAs);

									c->Save(Out);

									Status = true;
								}
							}
						}

						Msg->Release();
					}
					else
					{
						LgiTrace("%s:%i - QueryInterface for IMessage failed.\n", __FILE__, __LINE__);
					}
					break;
				}
			}

			Item->Release();
		}
		else
		{
			LgiTrace("%s:%i - OpenEntry failed.\n", __FILE__, __LINE__);
		}
	}

	return Status;
}

int OutlookIO::FolderType(IMAPIFolder *In)
{
	int Status = MAGIC_MAIL;
	LPMAPITABLE Items = 0;
	if (In &&
		In->GetContentsTable(0, &Items) == S_OK)
	{
		// Loop through all the items
		bool Done = false;
		for (LMapiList Lst(Items); Lst.More() && !Done; Lst.Next())
		{
			SPropValue *EntryId = Lst.GetField(PR_ENTRYID);
			if (EntryId)
			{
				ULONG Type = 0;
				IUnknown *Item = 0;
				if (In->OpenEntry(	EntryId->Value.bin.cb,
									(LPENTRYID)EntryId->Value.bin.lpb,
									NULL,
									MAPI_BEST_ACCESS,
									&Type,
									&Item) == S_OK &&
					Item)
				{
					IMessage *Msg = 0;
					if (Item->QueryInterface(IID_IMessage, (void**)&Msg) == S_OK && Msg)
					{
						char *Vs;
						SPropValue *v = MapiGetProp(Msg, PR_MESSAGE_CLASS);
						if (v && (Vs = MapiCastString(v)))
						{
							if (_stricmp(Vs, "IPM.Note") == 0)
							{
								Status = MAGIC_MAIL;
								Done = true;
							}
							else if (_stricmp(Vs, "IPM.Contact") == 0)
							{
								Status = MAGIC_CONTACT;
								Done = true;
							}
							else if (_stricmp(Vs, "IPM.Appointment") == 0)
							{
								Status = MAGIC_CALENDAR;
								Done = true;
							}
						}

						Msg->Release();
					}
				}
			}
		}
	}

	return Status;
}

LString ConvertToString(LString::Array &Path)
{
	LString d("/");
	LString ret = d + d.Join(Path);
	return ret;
}

bool OutlookIO::Import(	ImportParams *P,
						IMAPIFolder *In,
						ScribeFolder *Out,
						LString::Array &Path)
{
	bool Status = false;
	if (App && Out && In)
	{
		const char *FolderName = Out->GetText(0);
		if (FolderName && P->Prog)
		{
			char FolderStr[256];
			sprintf_s(FolderStr, sizeof(FolderStr), "Folder: '%s'", FolderName);
			P->Prog->SetDescription(FolderStr);
		}

		bool ImportThisFolder = P->AllFolders;
		
		if (!ImportThisFolder)
		{
			auto CurPath = ConvertToString(Path);
			for (auto s: P->Import)
			{
				if (_stricmp(CurPath, s) == 0)
				{
					ImportThisFolder = true;
					break;
				}
			}
		}

		if (ImportThisFolder)
		{
			// look through all the items for mail and contacts
			LPMAPITABLE Items = 0;
			if (In->GetContentsTable(0, &Items) == S_OK)
			{
				// ViewTable(Items, Wnd);
				Status = true;

				LMapiList Lst(Items);
				if (Lst.Length() > 0)
				{
					int GoodConvert = 0;

					if (P->ItemProg)
					{
						P->ItemProg->SetDescription("Processing items...");
						P->ItemProg->SetRange(Lst.Length());
					}

					// Loop through all the items
					for (; Lst.More() && !P->ItemProg->IsCancelled(); Lst.Next())
					{
						SPropValue *v = Lst.GetField(PR_ENTRYID);
						if (v)
						{
							if (ImportItem(Out, In, v))
							{
								GoodConvert++;
							}
							else
							{
								#ifdef _DEBUG
								// try it again
								ImportItem(Out, In, v);
								#endif
							}
						}

						if (P->ItemProg)
						{
							P->ItemProg->Value(Lst.Index());
						}
					}

					if (P->ItemProg)
					{
						P->ItemProg->SetDescription("");
						P->ItemProg->Value(0);
						P->ItemProg->SetRange(0);
					}
				}
			}
		}

		// look through all the children folders for more stuff to import
		LPMAPITABLE Folders = 0;
		if (In->GetHierarchyTable(0, &Folders) == S_OK)
		{
			// Loop through all the folders
			for (LMapiList Lst(Folders); Lst.More() && !P->Prog->IsCancelled(); Lst.Next())
			{
				SPropValue *v = Lst.GetField(PR_ENTRYID);
				SPropValue *Name = Lst.GetField(PR_DISPLAY_NAME);
				if (v && Name)
				{
					IMAPIFolder *ChildIn;
					ULONG Type;
					if (In->OpenEntry(v->Value.bin.cb, (LPENTRYID)v->Value.bin.lpb, NULL, MAPI_BEST_ACCESS, &Type, (IUnknown**)&ChildIn) == S_OK &&
						ChildIn)
					{
						// Check that this folder exists
						auto OutlookName = LFromNativeCp(MapiCastString(Name));
						char Str[256];
						auto OutPath = Out->GetPath();
						strcpy_s(Str, sizeof(Str), OutPath);
						if (Str[strlen(Str)-1] != '/') strcat(Str, "/");
						strcat(Str, OutlookName);
						ScribeFolder *ChildOut = App->GetFolder(Str);
						if (!ChildOut)
						{
							ChildOut = Out->CreateSubFolder(OutlookName, FolderType(ChildIn));
						}

						if (ChildOut)
						{
							Path.New() = MapiCastString(Name);

							Status &= Import(P, ChildIn, ChildOut, Path);
							ChildOut->Save();

							Path.PopLast();
						}

						ChildIn->Release();
					}
				}
			}
		}
	}
	return Status;
}

void OutlookIO::ImportPersonalAddressBook(std::function<void(bool)> callback)
{
	auto Dlg = new FolderDlg(App, App, MAGIC_CONTACT);
	Dlg->DoModal([this, Dlg, callback](auto dlg, auto ctrlId)
	{
		if (!ctrlId)
		{
			if (callback)
				callback(false);
			return;
		}

		bool Status = false;
		HRESULT Error = S_OK;
		IAddrBook *AddrBook = 0;
		auto DestFolder = App->GetFolder(Dlg->Get());
		if (DestFolder &&
			(Error = Session->OpenAddressBook(Ui, NULL, 0, &AddrBook)) == S_OK &&
			AddrBook)
		{
			ULONG EntryIDSize = 0;
			ENTRYID *PersonalAddrBook = 0;
			if (AddrBook->GetPAB(&EntryIDSize, &PersonalAddrBook) == S_OK &&
				PersonalAddrBook)
			{
				ULONG Type = 0;
				IDistList *DistList = 0;
				if (AddrBook->OpenEntry(EntryIDSize,
										PersonalAddrBook,
										NULL,
										0,
										&Type,
										(IUnknown**)&DistList) == S_OK &&
					DistList)
				{
					int NewContacts = 0;
					LPMAPITABLE DistContents = 0;
					if (DistList->GetContentsTable(0, &DistContents) == S_OK &&
						DistContents)
					{
						for (LMapiList Lst(DistContents); Lst.More(); Lst.Next())
						{
							SPropValue *v = Lst.GetField(PR_ENTRYID);
							if (v)
							{
								ULONG type = 0;
								IMailUser *User = 0;
								if (DistList->OpenEntry(	v->Value.bin.cb,
															(LPENTRYID) v->Value.bin.lpb,
															0,
															0,
															&type,
															(IUnknown**)&User) == S_OK &&
									User)
								{
									SPropValue *Array = 0;
									ULONG Values = 0;

									if (User->GetProps(	NULL, // Props
														0,
														&Values,
														&Array) == S_OK &&
										Array)
									{
										Contact *Person = (Contact*)App->CreateItem(MAGIC_CONTACT, DestFolder, false);
										if (Person)
										{
											for (unsigned n=0; n<Values; n++)
											{
												switch (Array[n].ulPropTag)
												{
													case PR_DISPLAY_NAME:
													{
														auto Name = LFromNativeCp(Array[n].Value.lpszA);
														if (Name)
														{
															int Spaces = 0;
															for (int k=0; Name.Get()[k]; k++)
															{
																if (Name.Get()[k] == ' ') Spaces++;
															}

															if (Spaces == 1)
															{
																char *Space = strchr(Name, ' ');
																*Space = 0;
																Person->Set(OPT_First, Name);
																Person->Set(OPT_Last, Space+1);

																const char *Last = 0;
																Person->Get(OPT_Last, Last);
																int n=0;
															}
															else
															{
																Person->Set(OPT_First, Name);
															}
														}
														break;
													}
													case PR_EMAIL_ADDRESS:
													case PR_SMTP_ADDRESS:
													{
														char *Addr = Array[n].Value.lpszA;
														if (strchr(Addr, '@'))
														{
															Person->Set(OPT_Email, Array[n].Value.lpszA);
														}
														break;
													}
													default:
													{
														if (PROP_ID(Array[n].ulPropTag) >= 0x8000 &&
															PROP_ID(Array[n].ulPropTag) <= 0x8100 &&
															PROP_TYPE(Array[n].ulPropTag) == PT_STRING8)
														{
															char *Addr = Array[n].Value.lpszA;
															if (strchr(Addr, '@'))
															{
																Person->Set(OPT_Email, Array[n].Value.lpszA);
															}
														}
														break;
													}
												}
											}

											Person->Save();
											NewContacts++;
										}
									}
								}
							}
						}
					}

					char Msg[256];
					sprintf_s(Msg, sizeof(Msg), "%i contacts imported from Outlook.", NewContacts);
					LgiMsg(App, Msg, AppName, MB_OK);

					Status = true;
				}
				else
				{
					LgiMsg(App, "Couldn't open the personal address book.", "Error", MB_OK);
				}
			}
			else
			{
				LgiMsg(App, "No personal address book.", "Error", MB_OK);
			}
		}
		else
		{
			LgiMsg(App, "Couldn't open the address book.", "Error", MB_OK);
		}

		if (callback)
			callback(Status);
	});
}

void Import_OutlookContacts(ScribeWnd *Parent)
{
	#ifdef WIN32
	Import_Outlook(Parent, IMP_OUTLOOK_PAB);
	#else
	ScribeFolder *Contacts = Parent->GetFolder(FOLDER_CONTACTS);
	if (!Contacts)
		return;

	auto Select = new LFileSelect;

	Select->Parent(Parent);
	Select->Type("Outlook Contacts", "*.csv");

	Select.Open([](auto s, auto ok)
	{
		if (ok)
		{
			ImpRecordSet Rs;
			if (ReadCsv(s->Name(), Rs) > 0)
			{
				for (ImpRecord *r = Rs.First(); r; r = Rs.Next())
				{
					Contact *c = new Contact;
					if (c)
					{
						CopyField(c, OPT_First, r, "First Name");
						CopyField(c, OPT_Last, r, "Last Name");
						CopyField(c, OPT_Email, r, "E-mail Address");
						CopyField(c, OPT_Street, r, "Home Street");
						CopyField(c, OPT_Suburb, r, "Home City");
						CopyField(c, OPT_State, r, "Home State");
						CopyField(c, OPT_Postcode, r, "Home Postal Code");
						CopyField(c, OPT_Country, r, "Home Country");
						CopyField(c, OPT_Work, r, "Business Phone");
						CopyField(c, OPT_Home, r, "Home Phone");
						CopyField(c, OPT_Mobile, r, "Mobile Phone");
						CopyField(c, OPT_Fax, r, "Business Fax");
						CopyField(c, OPT_WebPage, r, "Web Page");

						c->Save(Contacts);
					}
				}
			}
		}

		delete s;
	});
	#endif
}

////////////////////////////////////////////////////////////////////////////
bool SetPropToEntryId(SPropValue &Prop, int Id, char *Email, char *Name)
{
	/*
	struct SenderEntry
	{
		char Flags[4];		// 0
		char Muid[16];		// MAPI_ONE_OFF_UID
		uint16 wVersion;	// 0
		uint16 wFlags;		// MAPI_ONE_OFF_NO_RICH_INFO (1)
		char *DisplayName;
		char *AddrType;
		char *EmailAddr;
	};
	*/

	uchar MapiOneOffUid[] = MAPI_ONE_OFF_UID;
	char AddrType[] = "SMTP";
	
	if (Email && Name)
	{
		LMemQueue p;
		int32 i32 = 0;
		int16 i16 = 0;
		p.Write(&i32, sizeof(i32));
		p.Write(MapiOneOffUid, sizeof(MapiOneOffUid));
		p.Write(&i16, sizeof(i16));
		i16 = MAPI_ONE_OFF_NO_RICH_INFO;
		p.Write(&i16, sizeof(i16));
		#define WriteStr(s) \
			p.Write(s ? s : "", (s ? strlen(s) : 0) + 1);
		if (Name)
		{
			WriteStr(Name);
		}
		else if (Email)
		{
			WriteStr(Email);
		}
		WriteStr(AddrType);
		WriteStr(Email);
		Prop.ulPropTag = Id;
		Prop.Value.bin.cb = (ULONG) p.GetSize();
		Prop.Value.bin.lpb = (uchar*)p.New();

		return Prop.Value.bin.lpb != 0;
	}

	return false;
};

int OutlookIO::CountFolders(ScribeFolder *f)
{
	int Count = 1;
	for (ScribeFolder *c = f->GetChildFolder(); c; c = c->GetNextFolder())
	{
		Count += CountFolders(c);
	}
	return Count;
}

bool OutlookIO::ExportCalendar(Calendar *c, IMessage *m)
{
	bool Status = false;

	if (c && m)
	{
		LDateTime dt;
		const char *s;
		int i;

		if (c->GetCalType() == CalEvent)
		{
			MapiSetPropStr(m, PR_MESSAGE_CLASS, "IPM.Appointment", false);
		}
		else if (c->GetCalType() == CalTodo)
		{
			MapiSetPropStr(m, PR_MESSAGE_CLASS, "IPM.Task", false);
		}

		char Tz[64];
		int TzOff = dt.SystemTimeZone();
		sprintf_s(Tz, sizeof(Tz), "(+%.2f)", (double)TzOff / 60);
		MapiSetPropStr(m, 0x8091001E, Tz);

		if (c->GetField(FIELD_CAL_START_UTC, dt))
		{
			MapiSetPropDate(m, PR_START_DATE, dt);
			MapiSetPropDate(m, 0x80800040, dt);
		}

		if (c->GetField(FIELD_CAL_END_UTC, dt))
		{
			MapiSetPropDate(m, PR_END_DATE, dt);
			MapiSetPropDate(m, 0x80810040, dt);
		}

		if (c->GetField(FIELD_CAL_SUBJECT, s))
			MapiSetPropStr(m, PR_SUBJECT, s);

		if (c->GetField(FIELD_CAL_LOCATION, s))
			MapiSetPropStr(m, 0x810C001E, s);

		if (c->GetField(FIELD_CAL_NOTES, s))
			MapiSetPropStr(m, PR_BODY, s);

		/* FIXME
		if (c->GetField(FIELD_CAL_REMINDER_ACTION, i) && i)
		{
			MapiSetPropBool(m, 0x81EE000B, true);
			if (c->GetField(FIELD_CAL_REMINDER_TIME, i))
				MapiSetPropLong(m, 0x81E90003, -i);				
		}
		*/

		if (c->GetField(FIELD_CAL_SHOW_TIME_AS, i))
			MapiSetPropLong(m, 0x81930003, i);

		MapiSetPropLong(m, PR_MESSAGE_FLAGS, 1);

		/*
		MapiSetPropLong(m, 0x80030003, 369);
		MapiSetPropLong(m, 0x807D0003, 2);
		MapiSetPropBool(m, 0x8001000B, true);
		MapiSetPropBool(m, 0x8002000B, false);
		*/
	}

	return Status;
}

bool OutlookIO::Export(	ExportParams *P,
						ScribeFolder *In,
						IMAPIFolder *Out,
						int Depth)
{
	bool Status = false;
	HRESULT Err;

	// 'In' is created/found as a sub-folder of 'Out'
	if (App &&
		Out &&
		In &&
		!P->Exclude.HasItem(In) &&
		(P->AllFolders || P->Include.HasItem(In)) &&
		(!P->Prog || !P->Prog->IsCancelled()))
	{
		// Create/find the output folder
		LPMAPITABLE Folders = 0;
		if ((Err = Out->GetHierarchyTable(0, &Folders)) == S_OK)
		{
			// ViewTable(Folders, App);
			IMAPIFolder *Match = 0;
			auto InName = In->GetName(true);

			// Loop through all the folders
			{
				for (LMapiList Lst(Folders); Lst.More() && !P->Prog->IsCancelled(); Lst.Next())
				{
					SPropValue *Name = Lst.GetField(PR_DISPLAY_NAME);
					auto n = LFromNativeCp(MapiCastString(Name));
					if (n && InName && _stricmp(n, InName) == 0)
					{
						SPropValue *v = Lst.GetField(PR_ENTRYID);
						if (v)
						{
							ULONG Type;
							if (Out->OpenEntry(	v->Value.bin.cb,
												(LPENTRYID)v->Value.bin.lpb,
												NULL,
												MAPI_BEST_ACCESS,
												&Type,
												(IUnknown**)&Match) == S_OK)
							{
								break;
							}
						}
					}
				}
			}

			if (!Match)
			{
				Out->CreateFolder(FOLDER_GENERIC, InName, 0, 0, OPEN_IF_EXISTS, &Match);
			}

			if (Match)
			{
				auto FolderPath = In->GetPath();

				if (P->AllFolders || P->Export.HasItem(In))
				{
					if (P->Prog)
					{
						char s[256];
						sprintf_s(s, sizeof(s), "Loading %s...", FolderPath ? FolderPath.Get() : InName.Get());
						P->Prog->SetDescription(s);
					}

					// Export all the contained items, by first scanning existing entries so
					// that we can skip existing copies of the items in Scribe					
					LPMAPITABLE Items = 0;
					LHashTbl<StrKey<char>,bool> MsgIds;
					if ((Err = Match->GetContentsTable(0, &Items)) == S_OK)
					{
						for (LMapiList Lst(Items); Lst.More(); Lst.Next())
						{
							SPropValue *v = Lst.GetField(PR_ENTRYID);
							if (v)
							{
								ULONG Type = 0;
								IUnknown *Item = 0;
								if ((Err = Match->OpenEntry(v->Value.bin.cb,
															(LPENTRYID)v->Value.bin.lpb,
															NULL,
															MAPI_BEST_ACCESS,
															&Type,
															&Item)) == S_OK &&
									Item)
								{
									if (Type == MAPI_MESSAGE)
									{
										IMessage *Msg = 0;
										if (Item->QueryInterface(IID_IMessage, (void**)&Msg) == S_OK && Msg)
										{
											char *MsgId = MapiGetPropStr(Msg, PR_INTERNET_MESSAGE_ID);
											if (MsgId)
											{
												MsgIds.Add(MsgId, true);
											}

											Msg->Release();
										}
									}

									Item->Release();
								}
								else
								{
									LgiTrace("%s:%i - Match->OpenEntry failed, Err=%x\n", __FILE__, __LINE__, Err);
								}
							}
						}
					}

					// Copy items from Scribe to the MAPI store
					bool WasLoaded = In->IsLoaded();
					In->LoadThings();

					if (P->Prog)
					{
						char s[256];
						sprintf_s(s, sizeof(s), "Exporting %s...", FolderPath ? FolderPath.Get() : InName.Get());
						P->Prog->SetDescription(s);
					}
					if (P->ItemProg)
					{
						P->ItemProg->Value(0);
						P->ItemProg->SetRange(In->Items.Length());
						P->ItemProg->SetType("email");
						P->ItemProg->Cancel(false);
					}
					for (auto t: In->Items)
					{
						if (P->ItemProg && P->ItemProg->IsCancelled())
							break;

						Mail *m = t->IsMail();
						if (m)
						{
							char MsgId[256];
							sprintf_s(MsgId, sizeof(MsgId), "<%s>", m->GetMessageId(true));
							if (!MsgIds.Find(MsgId))
							{
								IMessage *Msg;
								if ((Err = Match->CreateMessage(0, 0, &Msg)) == S_OK)
								{
									SPropValue v;
									Msg->SetProps(1, &v, 0);

									MapiSetPropLong(Msg, PR_MESSAGE_FLAGS, m->GetFlags() & MAIL_READ ? MSGFLAG_READ : 0);
									MapiSetPropLong(Msg, PR_OBJECT_TYPE, MAPI_MESSAGE);
									MapiSetPropStr(Msg, PR_MESSAGE_CLASS, "IPM.Note", false);
									MapiSetPropStr(Msg, PR_INTERNET_MESSAGE_ID, MsgId, false);

									if (m->GetSubject())
									{
										MapiSetPropStr(Msg, PR_SUBJECT, m->GetSubject(), true);
									}

									// Write the sender...
									if (m->GetFrom())
									{
										SPropValue Prop;
										if (SetPropToEntryId(Prop, PR_SENDER_ENTRYID, (char*)m->GetFromStr(FIELD_EMAIL), (char*)m->GetFromStr(FIELD_NAME)))
										{
											Msg->SetProps(1, &Prop, 0);
											DeleteArray(Prop.Value.bin.lpb);
										}

										if (m->GetFromStr(FIELD_EMAIL))
										{
											MapiSetPropStr(Msg, PR_SENDER_ADDRTYPE, "SMTP", false);
											MapiSetPropStr(Msg, PR_SENDER_EMAIL_ADDRESS, m->GetFromStr(FIELD_EMAIL), false);

											MapiSetPropStr(Msg, PR_SENT_REPRESENTING_ADDRTYPE, "SMTP", false);
											MapiSetPropStr(Msg, PR_SENT_REPRESENTING_EMAIL_ADDRESS, m->GetFromStr(FIELD_EMAIL), false);
										}

										auto Name = m->GetFromStr(FIELD_EMAIL) ? m->GetFromStr(FIELD_EMAIL) : m->GetFromStr(FIELD_EMAIL);
										if (Name)
										{
											MapiSetPropStr(Msg, PR_SENDER_NAME, Name, true);
											MapiSetPropStr(Msg, PR_SENT_REPRESENTING_NAME, Name, true);
										}
									}

									// Write out the recipients table
									if (m->GetTo()->Length())
									{
										ADRLIST *Adr = 0;
										if (MAPIAllocateBuffer(CbNewADRLIST((ULONG)m->GetTo()->Length()), (void**) &Adr) == S_OK)
										{
											Adr->cEntries = (ULONG)m->GetTo()->Length();

											int n = 0;
											for (LDataPropI *a = m->GetTo()->First(); a; a = m->GetTo()->Next(), n++)
											{
												Adr->aEntries[n].cValues = 3 + (a->GetStr(FIELD_EMAIL) ? 1 : 0) + (a->GetStr(FIELD_NAME) ? 1 : 0);
												Adr->aEntries[n].ulReserved1 = 0;
												if (MAPIAllocateBuffer(	sizeof(SPropValue) * Adr->aEntries[n].cValues,
																		(void**) &Adr->aEntries[n].rgPropVals) == S_OK)
												{
													SPropValue *p = Adr->aEntries[n].rgPropVals;
													int i = 0;

													if (SetPropToEntryId(p[0], PR_ENTRYID, (char*)a->GetStr(FIELD_EMAIL), (char*)a->GetStr(FIELD_NAME)))
													{
														i++;
													}
													else
													{
														Adr->aEntries[n].cValues--;
													}
													
													switch (a->GetInt(FIELD_CC))
													{
														default:
														case MAIL_ADDR_TO:
															p[i].Value.l = MAPI_TO;
															break;
														case MAIL_ADDR_CC:
															p[i].Value.l = MAPI_CC;
															break;
														case MAIL_ADDR_BCC:
															p[i].Value.l = MAPI_BCC;
															break;
													}
													p[i].ulPropTag = PR_RECIPIENT_TYPE;
													p[i++].dwAlignPad = 0;

													p[i].ulPropTag = PR_ADDRTYPE;
													p[i].dwAlignPad = 0;
													p[i++].Value.lpszA = "SMTP";

													p[i].ulPropTag = PR_DISPLAY_NAME;
													p[i].Value.lpszA = (char*) (a->GetStr(FIELD_NAME) ? a->GetStr(FIELD_NAME) : a->GetStr(FIELD_EMAIL));
													p[i++].dwAlignPad = 0;
													
													if (a->GetStr(FIELD_EMAIL))
													{
														p[i].ulPropTag = PR_EMAIL_ADDRESS;
														p[i].Value.lpszA = (char*) a->GetStr(FIELD_EMAIL);
														p[i++].dwAlignPad = 0;
													}
												}
											}

											HRESULT e;
											if ((e = Msg->ModifyRecipients(MODRECIP_ADD, Adr)) != S_OK)
											{
												// _asm int 3
											}

											for (unsigned i=0; i<Adr->cEntries; i++)
											{
												MAPIFreeBuffer(Adr->aEntries[i].rgPropVals);
											}

											MAPIFreeBuffer(Adr);
										}
									}
									
									if (m->GetBody())
									{
										LAutoString cs = m->GetCharSet();
										if (cs)
										{
											char *u8 = (char*) LNewConvertCp("utf-8", m->GetBody(), cs);
											MapiSetPropStr(Msg, PR_BODY, u8, true);
											DeleteArray(u8);
										}
										else
										{
											MapiSetPropStr(Msg, PR_BODY, m->GetBody(), false);
										}
									}

									if (m->GetHtml())
									{
										MapiSetPropStr(Msg, PR_BODY_HTML, m->GetHtml(), true);
									}

									if (m->GetInternetHeader())
									{
										MapiSetPropStr(Msg, PR_TRANSPORT_MESSAGE_HEADERS, m->GetInternetHeader(), false);
									}

									MapiSetPropDate(Msg, PR_CLIENT_SUBMIT_TIME, *m->GetDateSent());
									MapiSetPropDate(Msg, PR_MESSAGE_DELIVERY_TIME, *m->GetDateReceived());

									// Write out any attachments
									List<Attachment> Att;
									if (m->GetAttachments(&Att))
									{
										for (auto a: Att)
										{
											char *Ptr;
											ssize_t Size;
											if (a->Get(&Ptr, &Size))
											{
												ULONG AttachmentNum = 0;
												LPATTACH Attach = 0;
												if ((Err = Msg->CreateAttach(0, 0, &AttachmentNum, &Attach)) == S_OK)
												{
													MapiSetPropLong(Attach, PR_ATTACH_METHOD, ATTACH_BY_VALUE);
													if (a->GetName())
													{
														MapiSetPropStr(Attach, PR_ATTACH_LONG_FILENAME, a->GetName(), false);

														auto d = a->GetName();
														const char *Dir = 0;
														while (*d)
														{
															if (*d == '/' || *d == '\\')
															{
																Dir = d;
															}
															d++;
														}
														if (Dir)
														{
															Dir++;
														}
														else
														{
															Dir = a->GetName();
														}
														MapiSetPropStr(Attach, PR_DISPLAY_NAME, Dir, false);
													}
													if (a->GetMimeType())
													{
														MapiSetPropStr(Attach, PR_ATTACH_MIME_TAG, a->GetMimeType(), false);
													}
													if (a->GetContentId())
													{
														MapiSetPropStr(Attach, PR_ATTACH_CONTENT_ID, a->GetContentId(), false);
													}
													
													SPropValue Bin;
													Bin.dwAlignPad = 0;
													Bin.ulPropTag = PR_ATTACH_DATA_BIN;
													Bin.Value.bin.cb = (ULONG) Size;
													Bin.Value.bin.lpb = (uchar*)Ptr;
													Attach->SetProps(1, &Bin, 0);

													Attach->SaveChanges(0);
													Attach->Release();
												}
												else
												{
													LgiTrace("%s:%i - Msg->CreateAttach failed, Err=%x\n", __FILE__, __LINE__, Err);
												}
											}
										}
									}

									Msg->SaveChanges(0);
									Msg->Release();
								}
								else
								{
									LgiTrace("%s:%i - Match->CreateMessage failed, Err=%x\n", __FILE__, __LINE__, Err);
								}
							}
						}

						Calendar *Cal = t->IsCalendar();
						if (Cal)
						{
							if (Cal->GetCalType() == CalEvent)
							{
								IMessage *Msg;
								if ((Err = Match->CreateMessage(0, 0, &Msg)) == S_OK)
								{
									ExportCalendar(Cal, Msg);
									Msg->SaveChanges(0);
									Msg->Release();
								}
							}
							else
							{
								// Find todo folder...
							}
						}

						if (P->ItemProg)
							P->ItemProg->Value(P->ItemProg->Value()+1);
					}

					if (P->Prog)
						P->Prog->Value(P->Prog->Value()+1);

					if (!WasLoaded)
					{
						In->UnloadThings();
					}
				}

				// Export child folders...
				for (ScribeFolder *c = In->GetChildFolder(); c; c = c->GetNextFolder())
				{
					Export(P, c, Match, Depth + 1);
				}

				Match->Release();
			}
		}
		else
		{
			LgiTrace("%s:%i - Out->GetHierarchyTable failed, Err=%x\n", _FL, Err);
		}
	}

	return Status;
}
//////////////////////////////////////////////////////////////////////////////
class MailMapiSource : public MailSource
{
protected:
	OutlookIO *Mapi;
	ScribeWnd *Parent;
	ScribeAccount *Account;
	UI_TYPE Ui;

	IMsgStore *MsgStore;
	IMAPIFolder *pFolder;
	List<MapiEntry> Entries;

	bool ListEntries();
	bool OnMsgStore(HRESULT Result);

public:
	MailMapiSource(ScribeWnd *parent, ScribeAccount *account);
	~MailMapiSource();

	// Connection
	bool Open(LSocketI *S, const char *RemoteHost, int Port, const char *User, const char *Password, LDom *SettingStore, int Flags = 0);
	bool Close();

	// Commands available while connected
	ssize_t GetMessages() override;
	bool Receive(LArray<MailTransaction*> &Trans, MailCallbacks *Callbacks);
	bool Delete(int Message);
	int Sizeof(int Message);
	bool GetUid(int Message, char *Id, int IdLen);
	bool GetUidList(LString::Array &Id);
	LString GetHeaders(int Message);
};

MailMapiSource::MailMapiSource(ScribeWnd *parent, ScribeAccount *account)
{
	MsgStore = 0;
	pFolder = 0;

	Parent = parent;
	Account = account;
	Ui = (UI_TYPE) ((Parent) ? Parent->Handle() : 0);
	Mapi = new OutlookIO(Parent, 0, Account);
}

MailMapiSource::~MailMapiSource()
{
	if (pFolder)
	{
		pFolder->Release();
	}
	if (MsgStore)
	{
		MsgStore->Release();
	}

	DeleteObj(Mapi);
}

bool MailMapiSource::OnMsgStore(HRESULT Result)
{
	if (FAILED(Result))
		return false;

	if (!MsgStore)
		return false;

	SPropValue *SubTree = MapiGetProp(MsgStore, PR_IPM_SUBTREE_ENTRYID);
	if (!SubTree)
		return false;

	ULONG ObjType;
	IMAPIFolder *Root = 0;
	if (MsgStore->OpenEntry(SubTree->Value.bin.cb, (LPENTRYID)SubTree->Value.bin.lpb, NULL, MAPI_BEST_ACCESS, &ObjType, (IUnknown**) &Root) != S_OK ||
		!Root)
		return false;

	LPMAPITABLE Folders = 0;
	if (Root->GetHierarchyTable(0, &Folders) != S_OK)
		return false;

	// Loop through all the folders
	bool Status = false;
	for (LMapiList Lst(Folders); Lst.More(); Lst.Next())
	{
		SPropValue *v = Lst.GetField(PR_ENTRYID);
		SPropValue *Name = Lst.GetField(PR_DISPLAY_NAME);
		if (v && Name)
		{
			char *FolderName = MapiCastString(Name);
			if (FolderName &&
				_stricmp(FolderName, "Inbox") == 0)
			{
				ULONG Type;
				Status |= Root->OpenEntry(v->Value.bin.cb,
										(LPENTRYID)v->Value.bin.lpb,
										NULL,
										MAPI_BEST_ACCESS,
										&Type,
										(IUnknown**)&pFolder) == S_OK &&
										pFolder;
			}
		}
	}

	return Status;
}

bool MailMapiSource::Open(LSocketI *S, const char *RemoteHost, int Port, const char *User, const char *Password, LDom *SettingStore, int Flags)
{
	bool Status = true;

	if (Account &&
		Mapi)
	{
		LVariant Server = Account->Receive.Server();
		if (Server.Str())
		{
			char PassStr[128] = "";
			LPassword p;
			Account->Receive.GetPassword(&p);
			p.Get(PassStr);

			Mapi->MAPILogonEx(	Ui,
								Server.Str(),
								ValidStr(PassStr) ? PassStr : 0,
								MAPI_EXTENDED,
								Mapi->Session.Set());
		}
		
		if (!Status)
		{
			Mapi->MAPILogonEx(	Ui,
								NULL,
								NULL,
								MAPI_LOGON_UI | MAPI_EXTENDED,
								Mapi->Session.Set());

			if (Account &&
				Mapi->Session)
			{
				// when we're called with a blank account we should be nice and
				// record what the user entered for their profile

				LPMAPITABLE Tbl = 0;
				if (Mapi->Session->GetStatusTable(0, &Tbl) == SUCCESS_SUCCESS)
				{
					// ViewTable(Tbl, Window);
					for (LMapiList Lst(Tbl); Lst.More(); Lst.Next())
					{
						SPropValue *Type = Lst.GetField(PR_RESOURCE_TYPE);
						SPropValue *Name = Lst.GetField(PR_DISPLAY_NAME); 
						if (MapiCastInt(Type) == MAPI_SUBSYSTEM)
						{
							// this should be the profile name
							Account->Receive.Server(MapiCastString(Name));
							break;
						}
					}
				}
			}
		}

		if (Mapi->Session)
		{
			LVariant UserName = Account->Receive.UserName();
			if (!ValidStr(UserName.Str()))
			{
				auto Dlg = new ChooseMessageStoreDlg(Parent, Mapi->Session, false);
				if (!Dlg->InitCheck())
				{
					delete Dlg;
				}
				else
				{
					Dlg->DoModal([this, Dlg](auto dlg, auto id)
					{
						if (id && Dlg->Ref)
						{
							Account->Receive.UserName(Dlg->Ref->DisplayName);

							auto Error = Mapi->Session->OpenMsgStore(Ui,
																Dlg->Ref->Size, // entry bytes
																Dlg->Ref->Entry, // ptr to entry
																NULL, // default interface: IMsgStore
																MAPI_BEST_ACCESS,
																&MsgStore);
							OnMsgStore(Error);
						}
					});
				}
			}
			else
			{
				MessageStores Entries(Mapi->Session);
				if (Entries.Status)
				{
					for (auto e: Entries)
					{
						if (e->DisplayName &&
							_stricmp(e->DisplayName, UserName.Str()) == 0)
						{
							auto Error = Mapi->Session->OpenMsgStore(Ui,
																e->Size, // entry bytes
																e->Entry, // ptr to entry
																NULL, // default interface: IMsgStore
																MAPI_BEST_ACCESS,
																&MsgStore);
							if (OnMsgStore(Error))
								break;
						}
					}
				}
			}
		}
	}

	return Status;
}

bool MailMapiSource::Close()
{
	if (Mapi &&
		Mapi->Session)
	{
		return true;
	}
	return false;
}

bool MailMapiSource::ListEntries()
{
	if (pFolder && Entries.Length() < 1)
	{
		LPMAPITABLE Items = 0;
		if (pFolder->GetContentsTable(0, &Items) == S_OK)
		{
			for (LMapiList Lst(Items); Lst.More(); Lst.Next())
			{
				SPropValue *v = Lst.GetField(PR_ENTRYID);
				if (v)
				{
					Entries.Insert(new MapiEntry(v));
				}
			}
		}
	}

	return Entries.Length() > 0;
}

ssize_t MailMapiSource::GetMessages()
{
	ssize_t Status = 0;
	if (ListEntries())
	{
		Status = Entries.Length();
	}
	return Status;
}

bool MailMapiSource::Receive(LArray<MailTransaction*> &Trans, MailCallbacks *Callbacks)
{
	bool Status = false;

	for (unsigned n=0; n<Trans.Length(); n++)
	{
		MapiEntry *e = Entries.ItemAt(Trans[n]->Index);
		if (e && Trans[n]->Stream)
		{
			ULONG Type = 0;
			IUnknown *Item = 0;
			if (pFolder->OpenEntry(	e->Len,
									(LPENTRYID)e->Bin,
									NULL,
									MAPI_BEST_ACCESS,
									&Type,
									&Item) == S_OK &&
				Item)
			{
				switch (Type)
				{
					case MAPI_MESSAGE:
					{
						IMessage *IMsg = 0;
						if (Item->QueryInterface(IID_IMessage, (void**)&IMsg) == S_OK)
						{
							SPropValue *Class = MapiGetProp(IMsg, PR_ORIG_MESSAGE_CLASS);
							if (!Class)
							{
								Class = MapiGetProp(IMsg, PR_MESSAGE_CLASS);
							}

							if (Class)
							{
								char *sClass = MapiCastString(Class);
								bool Note = _stricmp(sClass, "IPM.Note") == 0;
								bool Post = _stricmp(sClass, "IPM.Post") == 0;
								bool Document = _strnicmp(sClass, "IPM.Document", 12) == 0;

								if (Note || Post || Document)
								{
									// FIXME
									// Status |= Trans[n]->Status = Mapi->ImportEmail(dynamic_cast<Mail*>(Msg), IMsg, Post);
								}
							}

							IMsg->Release();
						}
						break;
					}
				}

				Item->Release();
			}
		}
	}

	return Status;
}

bool MailMapiSource::Delete(int Message)
{
	return false;
}

int MailMapiSource::Sizeof(int Message)
{
	return 0;
}

bool MailMapiSource::GetUid(int Message, char *Id, int IdLen)
{
	return false;
}

bool MailMapiSource::GetUidList(LString::Array &Id)
{
	return false;
}

LString MailMapiSource::GetHeaders(int Message)
{
	return NULL;
}

MailSource *NewOutlookMailSource(ScribeWnd *Parent, ScribeAccount *Account)
{
	return new MailMapiSource(Parent, Account);
}

