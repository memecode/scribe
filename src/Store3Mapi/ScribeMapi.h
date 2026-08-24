#ifndef _SCRIBE_MAPI_H_
#define _SCRIBE_MAPI_H_

#ifdef WINDOWS

#include <stdint.h>

#define INITGUID
#define USES_IID_IMessage
#include "lgi/common/Lgi.h"
#include "Store3Common.h"
#include "lgi/common/Store3.h"
#include "mapix.h" // https://www.microsoft.com/en-us/download/details.aspx?id=12905
#include "mapiutil.h"

#if _MSC_VER >= 1400
typedef ULONG_PTR	UI_TYPE;
#else
typedef ULONG		UI_TYPE;
#endif
typedef HRESULT (STDAPICALLTYPE *pWrapCompressedRTFStream)
(
	LPSTREAM lpCompressedRTFStream,
	ULONG ulFlags,
	LPSTREAM FAR *lpUncompressedRTFStream
);
typedef HRESULT (STDMETHODCALLTYPE* pHrCreateNewToMapiConverter)
(
	IConverterSession** pConverterSession
);

#define PR_SMTP_ADDRESS				(PROP_TAG(PT_STRING8,	0x39fe))
#define PR_SENDER_SMTP_ADDRESS		(PROP_TAG(PT_STRING8,	0x0065))
#define PR_SENDER_SMTP_ADDRESS2		(PROP_TAG(PT_STRING8,	0x0c1f))
#ifndef PR_INTERNET_MESSAGE_ID
#define PR_INTERNET_MESSAGE_ID		0x1035001E
#endif
#ifndef PR_HTML
#define PR_HTML						0x10130102
#endif
#ifndef PR_BODY_HTML
#define PR_BODY_HTML				0x1013001E
#endif
#ifndef PR_ATTACH_CONTENT_ID
#define PR_ATTACH_CONTENT_ID		0x3712001E
#endif
#ifndef PR_INTERNET_CPID
#define PR_INTERNET_CPID			0x3FDE0003
#endif
#ifndef PR_ATTACH_CONTENT_ID_W
#define PR_ATTACH_CONTENT_ID_W		0x3712001F
#endif
#ifndef PR_LAST_MODIFIER_NAME
#define PR_LAST_MODIFIER_NAME		0x3FFA001F
#endif

extern uint32_t MapiContactEmailTags[];

class LMapiStore;
class LMapiFolder;
class LMapiMail;
class LMapiList;
class LMapiAdviseSink;

struct LMapiEntry : public LArray<uint8_t>
{
	LMapiEntry() {}

	LMapiEntry(const SPropValue *p) { *this = p; }
	LMapiEntry &operator =(const SPropValue *p)
	{
		Empty();
		if (p && PROP_TYPE(p->ulPropTag) == PT_BINARY)		
			Add(p->Value.bin.lpb, p->Value.bin.cb);
		else
			LAssert(!"unexpected type.");

		return *this;
	}

	LMapiEntry(const OBJECT_NOTIFICATION &o) { *this = o; }
	LMapiEntry &operator =(const OBJECT_NOTIFICATION &o)
	{
		Empty();
		Add((uint8_t*)o.lpEntryID, o.cbEntryID);
		return *this;
	}

	operator LPENTRYID()
	{
		return (LPENTRYID)AddressOf();
	}
};

class LMapiBase
{
	LArray<char16*> UnicodeMem;

public:
	virtual ~LMapiBase()
	{
		for (auto &p: UnicodeMem)
			delete[] p;
		UnicodeMem.Empty();
	}

	SPropValue *MapiGetField(SRow *Row, int Field)
	{
		if (!Row)
			return NULL;

		for (unsigned i=0; i<Row->cValues; i++)
		{
			if (PROP_ID(Row->lpProps[i].ulPropTag) == PROP_ID(Field))
			{
				return Row->lpProps + i;
			}
		}
		
		return NULL;
	}

	SPropValue *MapiGetProp(IMAPIProp *Props, int Field)
	{
		if (!Props)
			return nullptr;
			
		SPropTagArray InTag;
		InTag.cValues = 1;
		InTag.aulPropTag[0] = Field;

		SPropValue *OutTag = nullptr;
		ULONG Tags = 0;
		auto res = Props->GetProps(&InTag, 0, &Tags, &OutTag);
		if (SUCCEEDED(res))
		{
			if (Tags == 1)
			{
				if (PROP_TYPE(OutTag->ulPropTag) == PT_ERROR)
				{
					LgiTrace("%s:%i - MapiGetProp err: 0x%x\n", _FL, OutTag->Value.err);
					return nullptr;
				}

				return OutTag;
			}
		}

		return nullptr;
	}

	int64 MapiCastInt(SPropValue *Val)
	{
		if (!Val)
			return 0;

		switch (PROP_TYPE(Val->ulPropTag))
		{
			case PT_SHORT:
				return Val->Value.i;
			case PT_LONG:
				return Val->Value.l;
			case PT_I8:
				return Val->Value.li.QuadPart;
		}

		return 0;
	}

	LString MapiCastString(SPropValue *Val)
	{
		if (!Val)
			return LString();
			
		switch (PROP_TYPE(Val->ulPropTag))
		{
			case PT_STRING8:
			{
				return LString(Val->Value.lpszA);
			}
			case PT_UNICODE:
			{
				LAutoString u(WideToUtf8(Val->Value.lpszW));
				return LString(u.Get());
			}
			case PT_BINARY:
			{
				LString s((const char*)Val->Value.bin.lpb, Val->Value.bin.cb);
				return s;
			}
		}

		return LString();
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

	bool MapiCastDate(LDateTime &dt, SPropValue *Val)
	{
		if (!Val)
			return false;

		if (PROP_TYPE(Val->ulPropTag) != PT_SYSTIME)
			return false;

		dt.Set((uint64)Val->Value.ft.dwHighDateTime << 32 | Val->Value.ft.dwLowDateTime);
		return true;
	}

	LString MapiGetPropStr(IMAPIProp *Props, int Field)
	{
		if (auto Val = MapiGetProp(Props, Field))
			return MapiCastString(Val);

		return LString();
	}

	int64 MapiGetPropInt(IMAPIProp *Props, int Field)
	{
		SPropValue *Val = MapiGetProp(Props, Field);
		if (Val)
		{
			return MapiCastInt(Val);
		}
		return 0;	
	}

	bool MapiGetPropDate(LDateTime &dt, IMAPIProp *Props, int Field)
	{
		if (!Props)
			return false;
			
		if (auto p = MapiGetProp(Props, Field))
			return MapiCastDate(dt, p);

		return false;
	}

	bool MapiSetPropStr(IMAPIProp *Props, int Field, const char *Str)
	{
		if (!Props || !Str)
		{
			LAssert(!"invalid param");
			return false;
		}

		auto propType = PROP_TYPE(Field);
		SPropValue p;
		if (propType == PT_STRING8)
		{
			p.ulPropTag = Field;
			// if (Unicode)
			// p.Value.lpszA = n = LToNativeCp(Str);
			p.Value.lpszA = (LPSTR)Str;
		}
		else if (propType == PT_UNICODE)
		{
			auto w = Utf8ToWide(Str);
			UnicodeMem.Add(w);
			p.ulPropTag = Field;
			p.Value.lpszW = w;
		}
		else
		{
			LAssert(!"unexpected type.");
			return false;
		}
			
		HRESULT res = Props->SetProps(1, &p, 0);
		if (SUCCEEDED(res))
			return true;
		LAssert(!"SetProps failed.");

		return false;
	}

	bool MapiSetPropLong(IMAPIProp *Props, int Field, ULONG lng)
	{
		if (!Props)
		{
			LAssert(!"invalid param");
			return false;
		}

		SPropValue p;
		p.ulPropTag = Field;
		p.Value.l = lng;
		HRESULT res = Props->SetProps(1, &p, 0);
		if (SUCCEEDED(res))
			return true;
		
		LgiTrace("%s:%i - Failed to set prop %i\n", _FL, Field);
		return false;
	}

	bool MapiSetPropBool(IMAPIProp *Props, int Field, bool b)
	{
		if (!Props)
		{
			LAssert(!"invalid param");
			return false;
		}

		SPropValue p;
		p.ulPropTag = Field;
		p.Value.b = b;
		
		HRESULT res = Props->SetProps(1, &p, 0);
		return SUCCEEDED(res);
	}

	bool MapiSetPropDate(IMAPIProp *Props, int Field, const LDateTime &d, bool AdjustTz = true)
	{
		if (!d.Year())
		{
			LAssert(!"invalid date");
			return false;
		}

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
		if (!SystemTimeToFileTime(&st, &Prop.Value.ft))
		{
			LAssert(!"SystemTimeToFileTime failed");
			return false;
		}

		HRESULT res = Props->SetProps(1, &Prop, 0);
		return SUCCEEDED(res);
	}
};

class LMapiThing : public LDataI, public LMapiBase
{
	friend class LMapiStore;

protected:
	LString Class;
	LMapiEntry Entry;
	LPMESSAGE MapiMsg = nullptr;
	LMapiFolder *Parent = nullptr;
	bool IsDirty = false;

public:
	LMapiStore *Store;

	LMapiThing(LMapiStore *store);	
	~LMapiThing();

	const char* GetClass() override { return "LMapiThing"; }

	virtual void Set(SPropValue *entry, LMapiFolder *parent, LMapiList *lst) {}
	virtual LPMESSAGE Handle() { return MapiMsg; }
	virtual void ReleaseHandle();
	void SetDirty();

	uint32_t Type() override { LAssert(0); return MAGIC_NONE; }
	bool IsOnDisk() override { LAssert(0); return false; }
	bool IsOrphan() override { LAssert(0); return false; }
	uint64 Size() override { LAssert(0); return 0; }
	Store3Status Save(LDataI *Parent = 0) override { LAssert(0); return Store3Error; }
	Store3Status Delete(bool ToTrash = true) override { LAssert(0); return Store3Error; }
	LDataStoreI *GetStore() override;
	LAutoStreamI GetStream(const char *file, int line) override { LAssert(0); LAutoStreamI s; return s; }
	Store3Status SetRfc822(LStreamI *m) override { LAssert(0); return Store3Error; }
};

class LMapiAttachment :
	public Store3Attachment<LMapiStore, LMapiMail, LMapiAttachment>,
	public LMapiBase
{
	friend class AttachStream;
	
	LPATTACH MapiAttach;
	LONG AttachNum;
	ULONG AttachMethod;
	uint64 DataSize;
	LString Name;
	LString MimeType;
	LString ContentId;
	LString Charset;
	LString Literal;
	LString Headers;
	LString msgId;
	
public:
	LMapiAttachment(LMapiStore *store);
	~LMapiAttachment();

	const char* GetClass() override { return "LMapiAttachment"; }

	LPATTACH Handle();
	bool Set(LMapiMail *mail, LMapiList *Lst);
	bool Set(const char *Content, const char *Charset, const char *MimeType);

	Store3CopyDecl;

	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;

	uint32_t Type() override;
	bool IsOnDisk() override;
	bool IsOrphan() override;
	uint64 Size() override;
	Store3Status Save(LDataI *Parent = NULL) override;
	Store3Status Delete(bool ToTrash = false) override;
	LAutoStreamI GetStream(const char *file, int line) override;
	void OnSave() override;
};

class LMapiMail : public LMapiThing
{
	LString Subject;
	Store3Addr From;
	Store3Addr Reply;
	DIterator<LDataPropI, Store3Addr, LMapiStore> To;
	LDateTime Date;
	uint64 Flags = 0;
	uint64 MsgSize = 0;
	LString Charset;
	LString TxtBody;
	LString HtmlBody;
	LString MimeType;
	LString MsgId;
	LString InetHeaders;
	LString Rfc822;

public:
	LMapiAttachment *Seg = nullptr;

	LMapiMail(LMapiStore *store);
	~LMapiMail();

	void Set(SPropValue *entry, LMapiFolder *parent, LMapiList *lst) override;
	LPMESSAGE Handle() override;

	// LDataPropI API
	Store3CopyDecl;
	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
	const LDateTime *GetDate(int id) override;
	Store3Status SetDate(int id, const LDateTime *i) override;
	LDataPropI *GetObj(int id) override;
	Store3Status SetObj(int id, LDataPropI *i) override;
	LDataIt GetList(int id) override;
	Store3Status SetRfc822(LStreamI *m) override;

	// LDataI API
	uint32_t Type() override { return MAGIC_MAIL; }
	bool IsOnDisk() override { return true; }
	bool IsOrphan() override { return false; }
	uint64 Size() override;
	Store3Status Save(LDataI *Parent) override;
	Store3Status Delete(bool ToTrash = true) override;
	LAutoStreamI GetStream(const char *file, int line) override;
};

class LMapiCalendar : public LMapiThing
{
	LDateTime StartDt, EndDt;
	LString Subject, Location, Notes;
	CalendarShowTimeAs ShowAs;
	CalendarPrivacyType Priv;
	LString TimeZone;
	uint64 Colour;
	bool Recur;
	
public:
	LMapiCalendar(LMapiStore *store);	
	~LMapiCalendar();

	void Set(SPropValue *entry, LMapiFolder *parent, LMapiList *lst);	
	LPMESSAGE Handle();

	// LDataPropI API
	LDataPropI &operator =(LDataPropI &p);
	const char *GetStr(int id);
	Store3Status SetStr(int id, const char *str);
	int64 GetInt(int id);
	Store3Status SetInt(int id, int64 i);
	LDateTime *GetDate(int id);
	Store3Status SetDate(int id, const LDateTime *i);
	LDataPropI *GetObj(int id);
	LDataIt GetList(int id);

	// LDataI API
	LDataI &operator =(LDataI &p);
	uint32_t Type();
	bool IsOnDisk();
	bool IsOrphan();
	uint64 Size();
	Store3Status Save(LDataI *Parent);
	Store3Status Delete(bool ToTrash = true);
};

class LMapiContact : public LMapiThing
{
	struct Address
	{
		LString Street, Suburb, Postcode, State, Country, Url;
	};
	struct Numbers
	{
		LString Number, Fax, Mobile;
	};

	LString PrimaryEmail;
	LString AltEmails;
	LString Title, First, Last, Nick, Spouse;
	Address Home, Work;
	Numbers HomePh, WorkPh;
	LString Company, Position;
	LString Notes;
	
	void ReadEmails();
	char *CacheGetStr(LString &s, uint32_t Prop);

public:
	LMapiContact(LMapiStore *store);	
	~LMapiContact();

	void Set(SPropValue *entry, LMapiFolder *parent, LMapiList *lst) override;
	LPMESSAGE Handle() override;

	// LDataPropI API
	LDataPropI &operator =(LDataPropI &p);
	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
	const LDateTime *GetDate(int id) override;
	Store3Status SetDate(int id, const LDateTime *i) override;
	LDataPropI *GetObj(int id) override;
	LDataIt GetList(int id) override;
	const LVariant *GetVar(int id) override;

	// LDataI API
	LDataI &operator =(LDataI &p);
	uint32_t Type() override;
	bool IsOnDisk() override;
	bool IsOrphan() override;
	uint64 Size() override;
	Store3Status Save(LDataI *Parent) override;
	Store3Status Delete(bool ToTrash = true) override;
};

class LMapiFolderField : public LDataPropI
{
	LMapiStore *Store;
	LString Name;
	int Id;
	int Width;
	
public:
	LMapiFolderField(LMapiStore *store);
	~LMapiFolderField();

	const char* GetClass() override { return "LMapiFolderField"; }

	// LDataPropI API
	LDataPropI &operator =(LDataPropI &p);
	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
	const LDateTime *GetDate(int id) override;
	Store3Status SetDate(int id, const LDateTime *i) override;
	LDataPropI *GetObj(int id) override;
	LDataIt GetList(int id) override;
	Store3Status SetRfc822(LStreamI *m) override;
};

class LMapiFolder : public LDataFolderI, public LMapiBase
{
	friend class LMapiStore;
	friend struct LMapiAdvise;

	LPMAPIFOLDER MapiFolder = nullptr;
	LMapiEntry Entry;
	
	LMapiStore *Store = nullptr;
	LMapiFolder *Parent = nullptr;
	LString Name;
	LString Class;
	int64 Unread = 0;
	bool IsOpen = false;
	int SortIndex = -6;
	uint32_t ItemType = MAGIC_MAIL;
	Store3SystemFolder FolderType = Store3SystemNone;

	DIterator<LDataFolderI, LMapiFolder, LMapiStore> Sub;
	DIterator<LDataI, LMapiThing, LMapiStore> Items;
	DIterator<LDataPropI, LMapiFolderField, LMapiStore> Flds;
	
	// MAPI notifications:
	LAutoPtr<struct LMapiAdvise> advise;
	ULONG OnNotify(ULONG cNotif, LPNOTIFICATION lpNotif);

public:
	LMapiFolder(LMapiStore *store);
	~LMapiFolder();

	const char* GetClass() override { return "LMapiFolder"; }

	bool Set(LPMAPIFOLDER f);
	bool Set(LMapiFolder *parent, LMapiList *Lst);
	LPMAPIFOLDER Handle();
	void ReleaseHandle();

	// LDataPropI API
	Store3CopyDecl;
	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
	const LDateTime *GetDate(int id) override;
	Store3Status SetDate(int id, const LDateTime *i) override;
	LDataPropI *GetObj(int id) override;
	LDataIt GetList(int id) override;
	Store3Status SetRfc822(LStreamI *m) override;

	// LDataI API
	uint32_t Type() override;
	bool IsOnDisk() override;
	bool IsOrphan() override;
	uint64 Size() override;
	Store3Status Save(LDataI *Parent) override;
	Store3Status Delete(bool ToTrash = true) override;
	LDataStoreI *GetStore() override;
	LAutoStreamI GetStream(const char *file, int line) override;

	// LDataFolderI API
	LDataIterator<LDataFolderI*> &SubFolders() override;
	LDataIterator<LDataI*> &Children() override;
	LDataIterator<LDataPropI*> &Fields() override;
	Store3Status DeleteAllChildren() override;
	Store3Status FreeChildren() override;
	void OnSelect(bool s) override;
	void OnCommand(const char *Name) override;
};

class LMapiList : public LMapiBase
{
	LPMAPITABLE List;
	ULONG Rows = 0;
	SRowSet *BaseRow = nullptr;
	uint32_t i = 0, StartIndex = 0;
	bool ReleaseList = false;
	bool Status = false;

public:
	LMapiList(LPMAPITABLE list, bool release = true)
	{
		ReleaseList = release;
		if ((List = list))
		{
			auto res = List->GetRowCount(0, &Rows);
			if (SUCCEEDED(res) && Rows)
			{
				res = List->SeekRow(BOOKMARK_BEGINNING, 0, NULL);
				if (SUCCEEDED(res))
				{
					res = List->QueryRows(Rows, 0, &BaseRow);
					Status = SUCCEEDED(res);
				}
			}
		}
	}

	~LMapiList()
	{
		if (List && ReleaseList)
			List->Release();
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
			HRESULT res = List->QueryRows(Rows-StartIndex, 0, &BaseRow);
			if (SUCCEEDED(res))
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

class MapiEntryRef : public LMapiBase
{
	LMapiStore *Store;
	
public:
	LString DisplayName;
	LMapiEntry Entry;
	bool IsDefault = false;

	MapiEntryRef(LMapiStore *store)
	{
		Store = store;
	}

	bool OpenRoot(LPMAPISESSION Session, UI_TYPE UiHnd, IMsgStore **MsgStore, IMAPIFolder **RootFolder);
};


class ScribeMsgStores : public LArray<MapiEntryRef*>, public LMapiBase
{
	LMapiStore *Store;

public:
	bool Status;

	ScribeMsgStores(LMapiStore *store, LPMAPISESSION Session)
	{
		Store = store;
		Status = false;

		Session->AddRef();
		ULONG r = Session->Release();

		LPMAPITABLE MsgStores = 0;
		HRESULT res = Session->GetMsgStoresTable(0, &MsgStores);
		if (SUCCEEDED(res) && MsgStores)
		{
			for (LMapiList Lst(MsgStores, false); Lst.More(); Lst.Next())
			{
				auto DisplayName = Lst.GetField(PR_DISPLAY_NAME); 
				auto Entry = Lst.GetField(PR_ENTRYID);
				if (DisplayName && Entry)
				{
					LAutoPtr<MapiEntryRef> Ref(new MapiEntryRef(Store));
					if (Ref)
					{
						Ref->DisplayName = MapiCastString(DisplayName);

						auto def = Lst.GetField(PR_DEFAULT_STORE);
						if (def)
						{
							switch (PROP_TYPE(def->ulPropTag))
							{
								case PT_BOOLEAN:
									Ref->IsDefault = def->Value.b != 0;
									break;
								case PT_LONG:
									Ref->IsDefault = def->Value.l != 0;
									break;
							}
						}

						void *p = NULL;
						int s = 0;
						if (Ref->DisplayName && MapiCastBinary(Entry, p, s))
						{
							Ref->Entry.Add((uint8_t*)p, s);
							Add(Ref.Release());
						}
					}
				}
			}

			Status = true;
		}
	}

	~ScribeMsgStores()
	{
		DeleteObjects();
	}
};

class LMapiStore :
	public LDataStoreI,
	public LLibrary
{
	friend class LMapiFolder;
	friend class LMapiThing;
	friend class LMapiMail;
	friend struct LMapiAdvise;

	LDataEventsI *Callback = nullptr;
	LMapiFolder *Root = nullptr;
	LAutoPtr<MapiEntryRef> EntryRef;
	LString Profile, Username, Password, RootName, profileCache;
	uint64 AccountId;
	LMapiEntry InboxEntry;
	LArray<LMapiThing*> Dirty;
	// LMapiAdviseSink *Notify = nullptr;
	LString::Array availableProfiles;
	bool MapiInitialized = false;
	LStream *Log = nullptr;
	LMapiBase mapi;

	LPMAPISESSION				Session = nullptr;
	IMsgStore					*MsgStore = nullptr;
	UI_TYPE						Ui;
	
	MAPIINITIALIZE				*MAPIInitialize = nullptr;
	MAPILOGONEX					*MAPILogonEx = nullptr;
	MAPIUNINITIALIZE			*MAPIUninitialize = nullptr;
	MAPIALLOCATEBUFFER			*MAPIAllocateBuffer = nullptr;
	MAPIFREEBUFFER				*MAPIFreeBuffer = nullptr;
	pWrapCompressedRTFStream	WrapCompressedRTFStream = nullptr;
	pHrCreateNewToMapiConverter HrCreateNewToMapiConverter = nullptr;

	LMapiFolder *FindSystemFolder(Store3SystemFolder Type);
	IConverterSession *CreateConverterSession();
	
public:
	enum TLogMsg { LogMsg, LogErr };

	LMapiStore(	const char *Server,
				const char *Username,
				const char *Password,
				uint64 accountId,
				LDataEventsI *callback,
				LStream *log);
	~LMapiStore();

	const char* GetClass() override { return "LMapiStore"; }
	void LOG(const char *Fmt, ...);
	bool ERR(const char *Fmt, ...);

	// Util
	IMsgStore *Handle() { return MsgStore; }
	ULONG OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifications);

	// MAPI API
	bool Login();

	// LDom API, supported methods:
	constexpr static const char *Method_init = "init";
	constexpr static const char *Method_sendMessage = "sendMessage";
		enum MSendMsgArgs {
			SendMsg_MailFrom,
			SendMsg_RcptTo,
			SendMsg_Data,
		};
	bool CallMethod(const char *MethodName, LScriptArguments &Args) override;

	// LDataPropI API
	Store3Status SetInt(int id, int64 i);
	int64 GetInt(int id);
	const char *GetStr(int id);

	// LDataEventsI wrappers
	bool OnChange(const char *File, int Line, LArray<LDataI*> &items, int FieldHint);

	// LDataStoreI API
	uint64 Size() { return 0; }
	LDataI *Create(int Type);
	LDataFolderI *GetRoot(bool create = false);
	Store3Status Move(LDataFolderI *NewFolder, LArray<LDataI*> &Items);
	Store3Status Delete(LArray<LDataI*> &Items, bool ToTrash);
	Store3Status Change(LArray<LDataI*> &Items, int PropId, LVariant &Value, LOperator Operator);
	void Compact(LViewI *Parent, LDataPropI *Props, std::function<void(bool)> OnStatus);
	void OnEvent(void *Param);
	bool OnIdle();
	LDataEventsI *GetEvents();
};

#endif

#endif