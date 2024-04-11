#ifndef _MAIL3_H_
#define _MAIL3_H_

#include "lgi/common/Lgi.h"
#include "Store3Common.h"
#include "v3.41.2/sqlite3.h"
#include "Store3CalendarObj.h"

// Debugging stuff
#define MAIL3_TRACK_OBJS			0

#define MAIL3_DB_FILE				"Database.sqlite"
#define MAIL3_TBL_FOLDER			"Folder"
#define MAIL3_TBL_FOLDER_FLDS		"FolderFields"
#define MAIL3_TBL_MAIL				"Mail"
#define MAIL3_TBL_MAILSEGS			"MailSegs"
#define MAIL3_TBL_CONTACT			"Contact"
#define MAIL3_TBL_GROUP				"ContactGroup"
#define MAIL3_TBL_FILTER			"Filter"
#define MAIL3_TBL_CALENDAR			"Calendar"
#define MAIL3_TBL_CALENDAR_FILES	"CalendarFiles"

class LMail3Store;
class LMail3Mail;
class LMail3Calendar;

enum Mail3SubFormat
{
	Mail3v1, // All the mail and segs in a single tables.
	Mail3v2, // All the mail and segs in per folders tables.
};

struct LMail3Def
{
	const char *Name;
	const char *Type;
};

struct GMail3Idx
{
	const char *IdxName;
	const char *Table;
	const char *Column;
};

class Mail3BlobStream : public LStream
{
	LMail3Store *Store;
	sqlite3_blob *b;
	int64 Pos, Size;
	int SegId;
	bool WriteAccess;
	
	bool OpenBlob();
	bool CloseBlob();

public:
	const char *File;
	int Line;

	Mail3BlobStream(LMail3Store *Store, int segId, int size, const char *file, int line, bool Write = false);
	~Mail3BlobStream();
	
	int64 GetPos();
	int64 SetPos(int64 p);
	int64 GetSize();
	int64 SetSize(int64 sz);
	
	ssize_t Read(void *Buf, ssize_t Len, int Flags = 0);
	ssize_t Write(const void *Buf, ssize_t Len, int Flags = 0);
};


extern LMail3Def TblFolder[];
extern LMail3Def TblFolderFlds[];
extern LMail3Def TblMail[];
extern LMail3Def TblMailSegs[];
extern LMail3Def TblContact[];
extern LMail3Def TblFilter[];
extern LMail3Def TblGroup[];
extern LMail3Def TblCalendar[];
extern LMail3Def TblCalendarFiles[];

#define SERIALIZE_STR(Var, Col) \
	if (Write) \
	{ \
		if (!s.SetStr(Col, Var.Str())) \
			return false; \
	} \
	else \
		Var = s.GetStr(Col);

#define SERIALIZE_DATE(Var, Col) \
	if (Write) \
	{ \
		if (Var.IsValid()) Var.ToUtc(); \
		if (!s.SetDate(Col, Var)) \
			return false; \
	} \
	else \
	{ \
		int Fmt = Var.GetFormat(); \
		Var.SetFormat(GDTF_YEAR_MONTH_DAY|GDTF_24HOUR); \
		Var.Set(s.GetStr(Col)); \
		Var.SetFormat(Fmt); \
		Var.SetTimeZone(0, false); \
	}

#define SERIALIZE_BOOL(Var, Col) \
	if (Write) \
	{ \
		if (!s.SetInt(Col, Var)) \
			return false; \
	} \
	else \
		Var = s.GetBool(Col);

#define SERIALIZE_INT(Var, Col) \
	if (Write) \
	{ \
		if (!s.SetInt(Col, Var)) \
			return false; \
	} \
	else \
		Var = s.GetInt(Col);


#define SERIALIZE_INT64(Var, Col) \
	if (Write) \
	{ \
		if (!s.SetInt64(Col, Var)) \
			return false; \
	} \
	else \
		Var = s.GetInt64(Col);

#define SERIALIZE_COLOUR(Colour, Col) \
	if (Write) \
	{ \
		int64_t c = Colour.IsValid() ? Colour.c32() : 0; \
		if (!s.SetInt64(Col, c)) \
			return false; \
	} \
	else \
	{ \
		int64_t c = s.GetInt64(Col); \
		if (c > 0) Colour.c32((uint32_t)c); \
		else Colour.Empty(); \
	}

#define SERIALIZE_AUTOSTR(var, Col) \
	{ \
		if (Write) \
		{ \
			if (!s.SetStr(Col, var)) \
				return false; \
		} \
		else \
		{ \
			var.Reset(NewStr(s.GetStr(Col))); \
		} \
	}

#define SERIALIZE_LSTR(var, Col) \
	{ \
		if (Write) \
		{ \
			if (!s.SetStr(Col, var)) \
				return false; \
		} \
		else \
		{ \
			var = s.GetStr(Col); \
		} \
	}
	
struct LMail3StoreMsg
{
	enum Type
	{
		MsgNone,
		MsgCompactComplete,
		MsgRepairComplete,
	}	Msg;

	int64_t Int;
	LString Str;

	LMail3StoreMsg(Type type)
	{
		Msg = type;
	}
};

class LMail3Store : public LDataStoreI
{
	friend class LMail3Obj;
	friend class LMail3Mail;
	friend class Mail3Trans;
	friend class CompactThread;

	LDataEventsI *Callback;
	sqlite3 *Db;
	LString Folder;
	LString DbFile;
	class LMail3Folder *Root;
	LHashTbl<ConstStrKey<char,false>, LMail3Def*> Fields;
	LHashTbl<StrKey<char,false>, GMail3Idx*> Indexes;
	Store3Status OpenStatus;
	LHashTbl<ConstStrKey<char,true>, Store3Status> TableStatus;
	LString ErrorMsg;
	LString StatusMsg;
	LString TempPath;

	std::function<void(bool)> CompactOnStatus;
	std::function<void(bool)> RepairOnStatus;

	struct TableDefn : LArray<LMail3Def>
	{
		LString::Array t;
	};

	bool ParseTableFormat(const char *Name, TableDefn &Defs);
	Store3Status CheckTable(const char *Name, LMail3Def *Flds);
	bool UpdateTable(const char *Name, LMail3Def *Flds, Store3Status Check);
	bool DeleteMailById(int64 Id);
	bool OpenDb();
	bool CloseDb();
	LMail3Folder *GetSystemFolder(int Type);

public:
	Mail3SubFormat Format;

	class LStatement
	{
		struct PostBlob
		{
			int64 Size;
			LVariant ColName;
			LStreamI *Data;
		};

		LArray<PostBlob> Post;

	protected:
		LMail3Store *Store;
		sqlite3_stmt *s;
		LVariant Table;
		LAutoString TempSql;
		
	public:
		LStatement(LMail3Store *store, const char *sql = NULL);
		virtual ~LStatement();

		operator sqlite3_stmt *() { return s; }

		bool IsOk()
		{
			return
				#ifndef __llvm__
				this != 0 &&
				#endif
				Store != 0 &&
				s != 0;
		}
		bool Prepare(const char *Sql);
		bool Row();
		bool Exec();
		bool Reset();
		bool Finalize();
		int64 LastInsertId();
		
		int GetSize(int Col);

		bool GetBool(int Col);
		int GetInt(int Col);
		bool SetInt(int Col, int n);
		int64 GetInt64(int Col);
		bool SetInt64(int Col, int64 n);
		char *GetStr(int Col);
		bool GetBinary(int Col, LVariant *v);

		bool SetStr(int Col, const char *s);
		bool SetDate(int Col, LDateTime &d);
		bool SetStream(int Col, const char *ColName, LStreamI *s);
		bool SetBinary(int Col, const char *ColName, LVariant *v);

		virtual int64 GetRowId() { LAssert(0); return -1; }
	};

	class LInsert : public LStatement
	{
	public:
		LInsert(LMail3Store *store, const char *Tbl);

		int64 GetRowId() { return LastInsertId(); }
	};

	class LUpdate : public LStatement
	{
		int64 RowId;

	public:
		LUpdate(LMail3Store *store, const char *Tbl, int64 rowId, char *ExcludeField = NULL);

		int64 GetRowId() { return RowId; }
	};

	class LTransaction
	{
		LMail3Store *Store;
		bool Open;

	public:
		LTransaction(LMail3Store *store);
		~LTransaction();
		
		bool RollBack();
	};

	#if MAIL3_TRACK_OBJS
	struct SqliteObjs
	{
		LStatement *Stat;
		Mail3BlobStream *Stream;
		SqliteObjs()
		{
			Stat = 0;
			Stream = 0;
		}
	};
	LArray<SqliteObjs> All;
	template<typename T>
	bool RemoveFromAll(T *p)
	{
		for (int i=0; i<All.Length(); i++)
		{
			SqliteObjs &d = All[i];
			if ((d.Stream && (void*)d.Stream == (void*)p)
				||
				(d.Stat && (void*)d.Stat == (void*)p))
			{
				All.DeleteAt(i);
				return true;
			}
		}
		LAssert(0);
		return false;
	}
	#endif

	LMail3Store(const char *Mail3Folder, LDataEventsI *Callback, bool Create);
	~LMail3Store();
		
	const char *GetClass() override { return "LMail3Store"; }

	int64 GetFolderId(char *Path);
	LDataEventsI *GetEvents() override { return Callback; }
	bool OnIdle() override { return false; }
	sqlite3 *GetDb() { return Db; }
	bool IsOk();
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *s) override;
	LDataPropI *GetObj(int id) override;
	uint64 Size() override;
	LDataI *Create(int Type) override;
	LDataFolderI *GetRoot(bool create = false) override;
	Store3Status Move(LDataFolderI *NewFolder, LArray<LDataI*> &Items) override;
	Store3Status Delete(LArray<LDataI*> &Items, bool ToTrash) override;
	Store3Status Change(LArray<LDataI*> &Items, int PropId, LVariant &Value, LOperator Operator) override;
	void Compact(LViewI *Parent, LDataPropI *Props, std::function<void(bool)> OnStatus) override;
	void Upgrade(LViewI *Parent, LDataPropI *Props, std::function<void(bool)> OnStatus) override;
	void Repair(LViewI *Parent, LDataPropI *Props, std::function<void(bool)> OnStatus) override;
	bool SetFormat(LViewI *Parent, LDataPropI *Props) override;
	void PostStore(LMail3StoreMsg *m) { Callback->Post(this, m); }
	void OnEvent(void *Param) override;
	bool Check(int Code, const char *Sql);
	LMail3Def *GetFields(const char *t) { return Fields.Find(t); }

	StoreTrans StartTransaction() override;

	// LDataEventsI wrappers
	void OnNew(const char *File, int Line, LDataFolderI *parent, LArray<LDataI*> &new_items, int pos, bool is_new);
	bool OnMove(const char *File, int Line, LDataFolderI *new_parent, LDataFolderI *old_parent, LArray<LDataI*> &items);
	bool OnChange(const char *File, int Line, LArray<LDataI*> &items, int FieldHint);
	bool OnDelete(const char *File, int Line, LDataFolderI *parent, LArray<LDataI*> &items);

private:
	class Mail3Trans : public LDataStoreI::LDsTransaction
	{
		Mail3Trans **Ptr;
		LTransaction Trans;

	public:
		Mail3Trans(LMail3Store *s, Mail3Trans **ptr);
		~Mail3Trans();
	} *Transaction;
};

class LMail3Obj
{
protected:
	bool Check(int r, char *sql);

public:
	LMail3Store *Store;
	int64 Id;
	int64 ParentId;

	LMail3Obj(LMail3Store *store)
	{
		Id = ParentId = -1;
		Store = store;
	}

	bool Write(const char *Table, bool Insert);

	virtual bool SetId(int64_t id)
	{
		Id = id;
		return Id >= 0;
	}

	virtual const char *GetClass() { return "LMail3Obj"; }
	virtual bool Serialize(LMail3Store::LStatement &s, bool Write) = 0;
	virtual void SetStore(LMail3Store *s) { Store = s; }
};

class LMail3Thing : public LDataI, public LMail3Obj 
{
	friend class LMail3Store;
	LMail3Thing &operator =(LMail3Thing &p) = delete;

protected:
    bool NewMail = false;

	virtual const char *GetTable() { LAssert(0); return  0; }
	virtual void OnSave() {};

public:
	LMail3Folder *Parent = NULL;

	LMail3Thing(LMail3Store *store = NULL) : LMail3Obj(store)
	{
	}

	~LMail3Thing();

	const char *GetClass() { return "LMail3Thing"; }
	bool IsOnDisk() { return Id > 0; }
	bool IsOrphan() { return Store == NULL || Parent == NULL; }
	uint64 Size() { return sizeof(*this); }
	uint32_t Type() { LAssert(0); return 0; }
	Store3Status Delete(bool ToTrash);
	LDataStoreI *GetStore() { return Store; }
	LAutoStreamI GetStream(const char *file, int line) { LAssert(0); return LAutoStreamI(0); }
	bool Serialize(LMail3Store::LStatement &s, bool Write) { LAssert(0); return false; }

	Store3Status Save(LDataI *Folder = NULL);
	virtual bool DbDelete() { LAssert(0); return false; }
};

class LMail3Folder : public LDataFolderI, public LMail3Obj
{
public:
	LVariant Name;
	int Unread;
	int Open;
	int ItemType;
	int Sort; // Which field to sort contents on
	int Threaded;
	int SiblingIndex; // The index of this folder when sorting amongst other sibling folders
	union {
		int AccessPerms;
		struct {
			int16_t ReadPerm;
			int16_t WritePerm;
		};
	};
	Store3SystemFolder System;

	LMail3Folder *Parent;
	DIterator<LDataFolderI, LMail3Folder, LMail3Store> Sub;
	DIterator<LDataI, LMail3Thing, LMail3Store> Items;
	DIterator<LDataPropI, Store3Field, LMail3Store> Flds;

	LMail3Folder(LMail3Store *store);
	~LMail3Folder();

	Store3CopyDecl;
	bool Serialize(LMail3Store::LStatement &s, bool Write) override;
	const char *GetClass() override { return "LMail3Folder"; }

	uint32_t Type() override;
	bool IsOnDisk() override;
	bool IsOrphan() override;
	uint64 Size() override;
	Store3Status Save(LDataI *Folder) override;
	Store3Status Delete(bool ToTrash) override;
	LDataStoreI *GetStore() override;
	LAutoStreamI GetStream(const char *file, int line) override;
	bool SetStream(LAutoStreamI stream) override;
	LDataIterator<LDataFolderI*> &SubFolders() override;
	LDataIterator<LDataI*> &Children() override;
	LDataIterator<LDataPropI*> &Fields() override;
	Store3Status DeleteAllChildren() override;
	Store3Status FreeChildren() override;
	LMail3Folder *FindSub(char *Name);
	bool DbDelete();
	bool GenSizes();

	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
};

class LMail3Attachment : public Store3Attachment<LMail3Store, LMail3Mail, LMail3Attachment>
{
	int64 SegId;
	int64 BlobSize;

	LString Headers;

	LString Name;
	LString MimeType;
	LString ContentId;
	LString Charset;
	
	/// This is set when the segment is not to be stored on disk.
	/// When signed and/or encrypted messages are stored, the original
	/// rfc822 image is maintained by not MIME decoding into separate
	/// segments but leaving it MIME encoded in one seg (headers and body).
	/// At runtime the segment is loaded and parsed into a temporary tree
	/// of LMail3Attachment objects. This flag is set for those temporary
	/// nodes.
	bool InMemoryOnly;

public:
	LMail3Attachment(LMail3Store *store);
	~LMail3Attachment();
	
	const char *GetClass() override { return "LMail3Attachment"; }

	void SetInMemoryOnly(bool b);
	int64 GetId() { return SegId; }
	LMail3Attachment *Find(int64 Id);
	bool Load(LMail3Store::LStatement &s, int64 &ParentId);
	bool ParseHeaders() override;
	char *GetHeaders();
	bool HasHeaders() { return ValidStr(Headers); }

	Store3CopyDecl;
	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
	Store3Status Delete(bool ToTrash = false) override;
	LAutoStreamI GetStream(const char *file, int line) override;
	bool SetStream(LAutoStreamI stream) override;

	uint32_t Type() override { return MAGIC_ATTACHMENT; }
	bool IsOnDisk() override { return SegId > 0; }
	bool IsOrphan() override { return false; }
	uint64 Size() override;
	uint64 SizeChildren();
	Store3Status Save(LDataI *Folder = NULL) override;
	void OnSave() override;
};

class LMail3Mail : public LMail3Thing
{
	LAutoString TextCache;
	LAutoString HtmlCache;
	LString IdCache;
	LAutoPtr<uint64> SizeCache;
	LString InferredCharset;
	LString ErrMsg;

	void LoadSegs();
	void OnSave() override;

	const char *GetTable() override { return MAIL3_TBL_MAIL; }
	const char *InferCharset(const char *ExampleTxt);
	bool Utf8Check(LString &v);
	bool Utf8Check(LVariant &v);
	bool Utf8Check(LAutoString &v);

public:
	int Priority = MAIL_PRIORITY_NORMAL;
	int Flags = 0;
	int AccountId = 0;
	int64 MailSize = 0;
	uint32_t MarkColour = Rgba32(0, 0, 0, 0); // FIELD_MARK_COLOUR, 32bit rgba colour

	LString Subject;
	DIterator<LDataPropI, Store3Addr, LMail3Store> To;
	LMail3Attachment *Seg = NULL;
	Store3Addr From;
	Store3Addr Reply;
	LVariant Label;
	LVariant MessageID;
	LVariant References;
	LVariant FwdMsgId;
	LVariant BounceMsgId;
	LVariant ServerUid;

	LDateTime DateReceived;
	LDateTime DateSent;

	LMail3Mail(LMail3Store *store);
	~LMail3Mail();

	Store3CopyDecl;
	void SetStore(LMail3Store *s) override;
	bool Serialize(LMail3Store::LStatement &s, bool Write) override;
	const char *GetClass() override { return "LMail3Mail"; }
	LMail3Attachment *GetAttachment(int64 Id);
	bool FindSegs(const char *MimeType, LArray<LMail3Attachment*> &Segs, bool Create = false);
	int GetAttachments(LArray<LMail3Attachment*> *Lst = NULL);
    bool ParseHeaders() override;
    void ResetCaches();

	uint32_t Type() override;
	uint64 Size() override;
	bool DbDelete() override;
	LDataStoreI *GetStore() override;
	LAutoStreamI GetStream(const char *file, int line) override;
	bool SetStream(LAutoStreamI  stream) override;

	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
	const LDateTime *GetDate(int id) override;
	Store3Status SetDate(int id, const LDateTime *t) override;
	LDataPropI *GetObj(int id) override;
	Store3Status SetObj(int id, LDataPropI *i) override;
	LDataIt GetList(int id) override;
	Store3Status SetRfc822(LStreamI *m) override;
};

class LMail3Contact : public LMail3Thing
{
	const char *GetTable() override { return MAIL3_TBL_CONTACT; }
	LHashTbl<IntKey<int64>, LString*> f;

public:
	LVariant Image;
	LDateTime DateMod;

	LMail3Contact(LMail3Store *store);
	~LMail3Contact();

	uint32_t Type() override { return MAGIC_CONTACT; }
	bool Serialize(LMail3Store::LStatement &s, bool Write) override;
	Store3CopyDecl;
	const char *GetClass() override { return "LMail3Contact"; }
	bool DbDelete() override;

	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;

	int64 GetInt(int id) override { return -1; }

	LVariant *GetVar(int id) override;
	Store3Status SetVar(int id, LVariant *i) override;

	const LDateTime *GetDate(int id) override;
	Store3Status SetDate(int id, const LDateTime *i) override;
};

class LMail3Group : public LMail3Thing
{
	const char *GetTable() override { return MAIL3_TBL_GROUP; }

	LString Name;
	LString Group;
	LDateTime DateMod;

public:
	LMail3Group(LMail3Store *store);
	~LMail3Group();

	uint32_t Type() override { return MAGIC_GROUP; }
	bool Serialize(LMail3Store::LStatement &s, bool Write) override;
	Store3CopyDecl;
	const char *GetClass() override { return "LMail3Group"; }
	bool DbDelete() override;

	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;

	int64 GetInt(int id) override { return -1; }

	const LDateTime *GetDate(int id) override;
	Store3Status SetDate(int id, const LDateTime *i) override;
};

class LMail3Filter : public LMail3Thing
{
	int Index;
	int StopFiltering;
	int Direction;
	LString Name;
	LString ConditionsXml;
	LString ActionsXml;
	LString Script;
	LDateTime Modified;

	const char *GetTable() override { return MAIL3_TBL_FILTER; }

public:
	LMail3Filter(LMail3Store *store);
	~LMail3Filter();

	uint32_t Type() override { return MAGIC_FILTER; }
	LDataStoreI *GetStore() override { return Store; }
	bool Serialize(LMail3Store::LStatement &s, bool Write) override;
	Store3CopyDecl;
	const char *GetClass() override { return "LMail3Filter"; }
	bool DbDelete() override;

	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;	
	int64 GetInt(int id) override;
	Store3Status SetInt(int Col, int64 n) override;
	const LDateTime *GetDate(int id) override;
	Store3Status SetDate(int id, const LDateTime *i) override;
};

class LMail3CalendarFile : public LDataI
{
	friend class LMail3Calendar;

	LMail3Store *Store = NULL;
	LMail3Calendar *Calendar = NULL;
	
	int64 Id = -1;
	int64 ParentId = -1;
	LString FileName;
	LString MimeType;
	LDateTime DateModified;
	LString Uri; // Either 'Uri' or 'Data' must be valid
	LString Data; // Store the data inline

	LString SizeCache;
	bool Dirty = false;

public:
	LMail3CalendarFile(LMail3Store *store);
	const char *GetClass() override { return "LMail3CalendarFile"; }

	bool IsDirty() { return Dirty || Id < 0 || ParentId < 0; }
	bool CopyProps(LDataPropI &p) override;
	bool Serialize(LMail3Store::LStatement &s, bool Write);

	// Impl LDataPropI
	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	const LDateTime *GetDate(int id) override;
	Store3Status SetDate(int id, const LDateTime *i) override;

	// Impl LDataI
	uint32_t Type() override { return MAGIC_CALENDAR_FILE; }
	bool IsOnDisk() override { return Id >= 0; }
	bool IsOrphan() override { return Calendar == NULL; }
	LDataStoreI *GetStore() override { return Store; }

	LAutoStreamI GetStream(const char *file, int line) override;
	uint64 Size() override;
	Store3Status Save(LDataI *Parent = NULL) override;
	Store3Status Delete(bool ToTrash = true) override;

	// Stubs
	int64 GetInt(int id) override { return 0; }
	Store3Status SetInt(int id, int64 i) override { return Store3NotImpl; }
	const LVariant *GetVar(int id) override { return NULL; }
	Store3Status SetVar(int id, LVariant *i) override { return Store3NotImpl; }
	LDataPropI *GetObj(int id) override { return NULL; }
	Store3Status SetObj(int id, LDataPropI *i) override { return Store3NotImpl; }
	LDataIt GetList(int id) override { EmptyVirtual(NULL); }
};

class LMail3Calendar : public Store3CalendarObj<LMail3Thing>
{
	friend class LMail3CalendarFile;

private:
	const char *GetTable() override { return MAIL3_TBL_CALENDAR; }
	DIterator<LDataPropI, LMail3CalendarFile, LMail3Store> Attachments;

	bool SaveAttachments();

public:
	LMail3Calendar(LMail3Store *store);
	~LMail3Calendar();

	LDataStoreI *GetStore() override { return Store; }
	bool SetId(int64_t id) override;
	bool Serialize(LMail3Store::LStatement &s, bool Write) override;
	const char *GetClass() override { return "LMail3Filter"; }
	bool DbDelete() override;
	LDataIt GetList(int id) override;
};

#endif
