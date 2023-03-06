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
class ScribeMapiList;
class LMapiAdviseSink;

class LMapiBase
{
public:
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
			return NULL;
			
		SPropTagArray InTag;
		InTag.cValues = 1;
		InTag.aulPropTag[0] = Field;

		SPropValue *OutTag = 0;
		ULONG Tags = 0;
		if (Props)
		{
			HRESULT res = Props->GetProps(&InTag, 0, &Tags, &OutTag);
			if (SUCCEEDED(res))
			{
				if (Tags == 1)
				{
					return OutTag;
				}
			}
		}

		return NULL;
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
		SPropValue *Val = MapiGetProp(Props, Field);
		if (Val)
		{
			return MapiCastString(Val);
		}
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
			
		SPropValue *p = MapiGetProp(Props, Field);
		if (!p)
			return false;

		return MapiCastDate(dt, p);
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
				p.Value.lpszA = n = LToNativeCp(Str);
			else
				p.Value.lpszA = (LPSTR)Str;
			
			HRESULT res = Props->SetProps(1, &p, 0);
			return SUCCEEDED(res);
		}

		return false;
	}

	bool MapiSetPropLong(IMAPIProp *Props, int Field, ULONG lng)
	{
		if (!Props)
			return false;

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
		if (Props)
		{
			SPropValue p;
			p.ulPropTag = Field;
			p.Value.b = b;
			
			HRESULT res = Props->SetProps(1, &p, 0);
			return SUCCEEDED(res);
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
				HRESULT res = Props->SetProps(1, &Prop, 0);
				return SUCCEEDED(res);
			}
		}

		return false;
	}
};

class LMapiAddr : public LDataPropI
{
	LMapiStore *Store;

public:
	LMapiMail *m;
	int CC, Status;
	LString Name, Email;

	LMapiAddr(LMapiStore *store);

	Store3CopyDecl;

	const char *GetStr(int id);
	Store3Status SetStr(int id, const char *str);
	int64 GetInt(int id);
	Store3Status SetInt(int id, int64 i);
};

class LMapiThing : public LDataI, public LMapiBase
{
	friend class LMapiStore;

protected:
	LArray<uint8_t> Entry;
	LPMESSAGE MapiMsg;
	LString Class;
	LMapiFolder *Parent;
	bool IsDirty;

public:
	LMapiStore *Store;

	LMapiThing(LMapiStore *store);	
	~LMapiThing();

	virtual void Set(SPropValue *entry, LMapiFolder *parent, ScribeMapiList *lst) {}
	virtual LPMESSAGE Handle() { return MapiMsg; }
	virtual void ReleaseHandle();
	void SetDirty();

	uint32_t Type() { LAssert(0); return MAGIC_NONE; }
	bool IsOnDisk() { LAssert(0); return false; }
	bool IsOrphan() { LAssert(0); return false; }
	uint64 Size() { LAssert(0); return 0; }
	Store3Status Save(LDataI *Parent = 0) { LAssert(0); return Store3Error; }
	Store3Status Delete(bool ToTrash = true) { LAssert(0); return Store3Error; }
	LDataStoreI *GetStore();
	LAutoStreamI GetStream(const char *file, int line) { LAssert(0); LAutoStreamI s; return s; }
	Store3Status SetRfc822(LStreamI *m) { LAssert(0); return Store3Error; }
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
	
public:
	LMapiAttachment(LMapiStore *store);
	~LMapiAttachment();

	LPATTACH Handle();
	bool Set(LMapiMail *mail, ScribeMapiList *Lst);
	bool Set(const char *Content, const char *Charset, const char *MimeType);

	Store3CopyDecl;

	const char *GetStr(int id);
	Store3Status SetStr(int id, const char *str);
	int64 GetInt(int id);
	Store3Status SetInt(int id, int64 i);

	uint32_t Type();
	bool IsOnDisk();
	bool IsOrphan();
	uint64 Size();
	Store3Status Save(LDataI *Parent = NULL);
	Store3Status Delete(bool ToTrash = false);
	LAutoStreamI GetStream(const char *file, int line);
	void OnSave();
};

class LMapiMail : public LMapiThing
{
	LString Subject;
	LMapiAddr From;
	LMapiAddr Reply;
	DIterator<LDataPropI, LMapiAddr, LMapiStore> To;
	LDateTime Date;
	uint64 Flags;
	uint64 MsgSize;
	LString Charset;
	LString TxtBody;
	LString HtmlBody;
	LString MimeType;

public:
	LMapiAttachment *Seg;

	LMapiMail(LMapiStore *store);
	~LMapiMail();

	void Set(SPropValue *entry, LMapiFolder *parent, ScribeMapiList *lst);	
	LPMESSAGE Handle();

	// LDataPropI API
	Store3CopyDecl;
	const char *GetStr(int id);
	Store3Status SetStr(int id, const char *str);
	int64 GetInt(int id);
	Store3Status SetInt(int id, int64 i);
	const LDateTime *GetDate(int id);
	Store3Status SetDate(int id, const LDateTime *i);
	LDataPropI *GetObj(int id);
	Store3Status SetObj(int id, LDataPropI *i);
	LDataIt GetList(int id);
	Store3Status SetRfc822(LStreamI *m);

	// LDataI API
	uint32_t Type();
	bool IsOnDisk();
	bool IsOrphan();
	uint64 Size();
	Store3Status Save(LDataI *Parent);
	Store3Status Delete(bool ToTrash = true);
	LAutoStreamI GetStream(const char *file, int line);
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

	void Set(SPropValue *entry, LMapiFolder *parent, ScribeMapiList *lst);	
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

	void Set(SPropValue *entry, LMapiFolder *parent, ScribeMapiList *lst);	
	LPMESSAGE Handle();

	// LDataPropI API
	LDataPropI &operator =(LDataPropI &p);
	const char *GetStr(int id);
	Store3Status SetStr(int id, const char *str);
	int64 GetInt(int id);
	Store3Status SetInt(int id, int64 i);
	const LDateTime *GetDate(int id);
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

class LMapiFolderField : public LDataPropI
{
	LMapiStore *Store;
	LString Name;
	int Id;
	int Width;
	
public:
	LMapiFolderField(LMapiStore *store);
	~LMapiFolderField();

	// LDataPropI API
	LDataPropI &operator =(LDataPropI &p);
	const char *GetStr(int id);
	Store3Status SetStr(int id, const char *str);
	int64 GetInt(int id);
	Store3Status SetInt(int id, int64 i);
	const LDateTime *GetDate(int id);
	Store3Status SetDate(int id, const LDateTime *i);
	LDataPropI *GetObj(int id);
	LDataIt GetList(int id);
	Store3Status SetRfc822(LStreamI *m);
};

class LMapiFolder : public LDataFolderI, public LMapiBase
{
	friend class LMapiStore;

	LPMAPIFOLDER MapiFolder;
	LArray<uint8_t> Entry;
	
	LMapiStore *Store;
	LMapiFolder *Parent;
	LString Name;
	LString Class;
	int64 Unread;
	bool IsOpen;
	int SortIndex;
	uint32_t ItemType;
	Store3SystemFolder FolderType;

	DIterator<LDataFolderI, LMapiFolder, LMapiStore> Sub;
	DIterator<LDataI, LMapiThing, LMapiStore> Items;
	DIterator<LDataPropI, LMapiFolderField, LMapiStore> Flds;
	
public:
	LMapiFolder(LMapiStore *store);
	~LMapiFolder();

	bool Set(LPMAPIFOLDER f);
	bool Set(LMapiFolder *parent, ScribeMapiList *Lst);
	LPMAPIFOLDER Handle();
	void ReleaseHandle();

	// LDataPropI API
	Store3CopyDecl;
	const char *GetStr(int id);
	Store3Status SetStr(int id, const char *str);
	int64 GetInt(int id);
	Store3Status SetInt(int id, int64 i);
	const LDateTime *GetDate(int id);
	Store3Status SetDate(int id, const LDateTime *i);
	LDataPropI *GetObj(int id);
	LDataIt GetList(int id);
	Store3Status SetRfc822(LStreamI *m);

	// LDataI API
	uint32_t Type();
	bool IsOnDisk();
	bool IsOrphan();
	uint64 Size();
	Store3Status Save(LDataI *Parent);
	Store3Status Delete(bool ToTrash = true);
	LDataStoreI *GetStore();
	LAutoStreamI GetStream(const char *file, int line);

	// LDataFolderI API
	LDataIterator<LDataFolderI*> &SubFolders();
	LDataIterator<LDataI*> &Children();
	LDataIterator<LDataPropI*> &Fields();
	Store3Status DeleteAllChildren();
	Store3Status FreeChildren();
	void OnSelect(bool s);
	void OnCommand(const char *Name);
};

class ScribeMapiList : public LMapiBase
{
	LPMAPITABLE List;
	ULONG Rows;
	SRowSet *BaseRow;
	uint32_t i, StartIndex;
	bool ReleaseList;
	bool Status;

public:
	ScribeMapiList(LPMAPITABLE list, bool release = true)
	{
		List = list;
		Rows = 0;
		BaseRow = 0;
		i = 0;
		StartIndex = 0;
		ReleaseList = release;
		Status = false;

		if (List)
		{
			HRESULT res = List->GetRowCount(0, &Rows);
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

	~ScribeMapiList()
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
	LArray<uint8_t> Entry;

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
			for (ScribeMapiList Lst(MsgStores, false); Lst.More(); Lst.Next())
			{
				SPropValue *DisplayName = Lst.GetField(PR_DISPLAY_NAME); 
				SPropValue *Entry = Lst.GetField(PR_ENTRYID);
				if (DisplayName && Entry)
				{
					LAutoPtr<MapiEntryRef> Ref(new MapiEntryRef(Store));
					if (Ref)
					{
						Ref->DisplayName = MapiCastString(DisplayName);

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

class LMapiStore : public LDataStoreI, public LLibrary
{
	friend class LMapiFolder;
	friend class LMapiThing;
	friend class LMapiMail;

	LDataEventsI *Callback;
	LMapiFolder *Root;
	LAutoPtr<MapiEntryRef> EntryRef;
	LString Profile, Username, Password, RootName;
	uint64 AccountId;
	LArray<uint8_t> InboxEntry;
	LArray<LMapiThing*> Dirty;
	LMapiAdviseSink *Notify;
	bool MapiInitialized;

	LPMAPISESSION				Session;
	IMsgStore					*MsgStore;
	UI_TYPE						Ui;
	
	MAPIINITIALIZE				*MAPIInitialize;
	MAPILOGONEX					*MAPILogonEx;
	MAPIUNINITIALIZE			*MAPIUninitialize;
	MAPIALLOCATEBUFFER			*MAPIAllocateBuffer;
	MAPIFREEBUFFER				*MAPIFreeBuffer;
	pWrapCompressedRTFStream	WrapCompressedRTFStream;

	LMapiFolder *FindSystemFolder(Store3SystemFolder Type);
	
public:
	LMapiStore(	const char *Server,
				const char *Username,
				const char *Password,
				uint64 accountId,
				LDataEventsI *callback);
	~LMapiStore();

	// Util
	IMsgStore *Handle() { return MsgStore; }
	bool Error(const char *Fmt, ...);
	ULONG OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifications);

	// MAPI API
	bool Login();

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