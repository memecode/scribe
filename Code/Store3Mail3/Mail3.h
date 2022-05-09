#ifndef _MAIL3_H_
#define _MAIL3_H_

#include "lgi/common/Lgi.h"
#include "Store3Common.h"
#include "v3.6.14/sqlite3.h"

// Debugging stuff
#define MAIL3_TRACK_OBJS		0

#define MAIL3_DB_FILE			"Database.sqlite"
#define MAIL3_TBL_FOLDER		"Folder"
#define MAIL3_TBL_FOLDER_FLDS	"FolderFields"
#define MAIL3_TBL_MAIL			"Mail"
#define MAIL3_TBL_MAILSEGS		"MailSegs"
#define MAIL3_TBL_CONTACT		"Contact"
#define MAIL3_TBL_GROUP			"ContactGroup"
#define MAIL3_TBL_FILTER		"Filter"
#define MAIL3_TBL_CALENDAR		"Calendar"

class GMail3Store;
class GMail3Mail;

enum Mail3SubFormat
{
	Mail3v1, // All the mail and segs in a single tables.
	Mail3v2, // All the mail and segs in per folders tables.
};

struct GMail3Def
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
	GMail3Store *Store;
	sqlite3_blob *b;
	int64 Pos, Size;
	int SegId;
	bool WriteAccess;
	
	bool OpenBlob();
	bool CloseBlob();

public:
	const char *File;
	int Line;

	Mail3BlobStream(GMail3Store *Store, int segId, int size, const char *file, int line, bool Write = false);
	~Mail3BlobStream();
	
	int64 GetPos();
	int64 SetPos(int64 p);
	int64 GetSize();
	int64 SetSize(int64 sz);
	
	ssize_t Read(void *Buf, ssize_t Len, int Flags = 0);
	ssize_t Write(const void *Buf, ssize_t Len, int Flags = 0);
};


extern GMail3Def TblFolder[];
extern GMail3Def TblFolderFlds[];
extern GMail3Def TblMail[];
extern GMail3Def TblMailSegs[];
extern GMail3Def TblContact[];
extern GMail3Def TblFilter[];
extern GMail3Def TblGroup[];
extern GMail3Def TblCalendar[];

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

#define SERIALIZE_GSTR(var, Col) \
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
	

class GMail3Store : public LDataStoreI
{
	friend class GMail3Obj;
	friend class GMail3Mail;
	friend class Mail3Trans;
	friend class CompactThread;

	LDataEventsI *Callback;
	sqlite3 *Db;
	LString Folder;
	LString DbFile;
	class GMail3Folder *Root;
	LHashTbl<ConstStrKey<char,false>, GMail3Def*> Fields;
	LHashTbl<StrKey<char,false>, GMail3Idx*> Indexes;
	Store3Status OpenStatus;
	LHashTbl<ConstStrKey<char,true>, Store3Status> TableStatus;
	LString ErrorMsg;
	LString StatusMsg;
	LString TempPath;

	struct TableDefn : LArray<GMail3Def>
	{
		LString::Array t;
	};

	bool ParseTableFormat(const char *Name, TableDefn &Defs);
	Store3Status CheckTable(const char *Name, GMail3Def *Flds);
	bool UpdateTable(const char *Name, GMail3Def *Flds, Store3Status Check);
	bool DeleteMailById(int64 Id);
	bool OpenDb();
	bool CloseDb();
	GMail3Folder *GetSystemFolder(int Type);

public:
	Mail3SubFormat Format;

	class GStatement
	{
		struct PostBlob
		{
			int64 Size;
			LVariant ColName;
			LStreamI *Data;
		};

		LArray<PostBlob> Post;

	protected:
		GMail3Store *Store;
		sqlite3_stmt *s;
		LVariant Table;
		LAutoString TempSql;
		
	public:
		GStatement(GMail3Store *store, const char *sql = 0);
		virtual ~GStatement();

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

		bool SetStr(int Col, char *s);
		bool SetDate(int Col, LDateTime &d);
		bool SetStream(int Col, const char *ColName, LStreamI *s);
		bool SetBinary(int Col, const char *ColName, LVariant *v);

		virtual int64 GetRowId() { LAssert(0); return -1; }
	};

	class GInsert : public GStatement
	{
	public:
		GInsert(GMail3Store *store, const char *Tbl);

		int64 GetRowId() { return LastInsertId(); }
	};

	class GUpdate : public GStatement
	{
		int64 RowId;

	public:
		GUpdate(GMail3Store *store, const char *Tbl, int64 rowId, char *ExcludeField = 0);

		int64 GetRowId() { return RowId; }
	};

	class GTransaction
	{
		GMail3Store *Store;
		bool Open;

	public:
		GTransaction(GMail3Store *store);
		~GTransaction();
		
		bool RollBack();
	};

	#if MAIL3_TRACK_OBJS
	struct SqliteObjs
	{
		GStatement *Stat;
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

	GMail3Store(const char *Mail3Folder, LDataEventsI *Callback, bool Create);
	~GMail3Store();

	int64 GetFolderId(char *Path);
	LDataEventsI *GetEvents() { return Callback; }
	bool OnIdle() { return false; }
	sqlite3 *GetDb() { return Db; }
	bool IsOk();
	int64 GetInt(int id);
	Store3Status SetInt(int id, int64 i);
	const char *GetStr(int id);
	Store3Status SetStr(int id, const char *s);
	LDataPropI *GetObj(int id);
	uint64 Size();
	LDataI *Create(int Type);
	LDataFolderI *GetRoot(bool create = false);
	Store3Status Move(LDataFolderI *NewFolder, LArray<LDataI*> &Items);
	Store3Status Delete(LArray<LDataI*> &Items, bool ToTrash);
	Store3Status Change(LArray<LDataI*> &Items, int PropId, LVariant &Value, LOperator Operator);
	bool Compact(LViewI *Parent, LDataPropI *Props);
	bool Upgrade(LViewI *Parent, LDataPropI *Props);
	bool Repair(LViewI *Parent, LDataPropI *Props);
	bool SetFormat(LViewI *Parent, LDataPropI *Props);
	void OnEvent(void *Param);
	bool Check(int Code, const char *Sql);
	GMail3Def *GetFields(const char *t) { return Fields.Find(t); }

	StoreTrans StartTransaction();

	// LDataEventsI wrappers
	void OnNew(const char *File, int Line, LDataFolderI *parent, LArray<LDataI*> &new_items, int pos, bool is_new);
	bool OnMove(const char *File, int Line, LDataFolderI *new_parent, LDataFolderI *old_parent, LArray<LDataI*> &items);
	bool OnChange(const char *File, int Line, LArray<LDataI*> &items, int FieldHint);
	bool OnDelete(const char *File, int Line, LDataFolderI *parent, LArray<LDataI*> &items);

private:
	class Mail3Trans : public LDataStoreI::LDsTransaction
	{
		Mail3Trans **Ptr;
		GTransaction Trans;

	public:
		Mail3Trans(GMail3Store *s, Mail3Trans **ptr);
		~Mail3Trans();
	} *Transaction;
};

class GMail3Obj
{
protected:
	bool Check(int r, char *sql);

public:
	GMail3Store *Store;
	int64 Id;
	int64 ParentId;

	GMail3Obj(GMail3Store *store)
	{
		Id = ParentId = -1;
		Store = store;
	}

	bool Write(const char *Table, bool Insert);

	virtual const char *GetClass() { return "GMail3Obj"; }
	virtual bool Serialize(GMail3Store::GStatement &s, bool Write) = 0;
	virtual void SetStore(GMail3Store *s) { Store = s; }
};

class GMail3Thing : public LDataI, public GMail3Obj 
{
	friend class GMail3Store;
	GMail3Thing &operator =(GMail3Thing &p) = delete;

protected:
    bool NewMail;

	virtual const char *GetTable() { LAssert(0); return  0; }
	virtual void OnSave() {};

public:
	GMail3Folder *Parent;

	GMail3Thing(GMail3Store *store) : GMail3Obj(store)
	{
		Parent = 0;
		NewMail = false;
	}

	~GMail3Thing();

	const char *GetClass() { return "GMail3Thing"; }
	bool IsOnDisk() { return Id > 0; }
	bool IsOrphan() { return false; }
	uint64 Size() { return sizeof(*this); }
	uint32_t Type() { LAssert(0); return 0; }
	Store3Status Delete(bool ToTrash);
	LDataStoreI *GetStore() { return Store; }
	LAutoStreamI GetStream(const char *file, int line) { LAssert(0); return LAutoStreamI(0); }
	bool Serialize(GMail3Store::GStatement &s, bool Write) { LAssert(0); return false; }

	Store3Status Save(LDataI *Folder = 0);
	virtual bool DbDelete() { LAssert(0); return false; }
};

class GMail3Folder : public LDataFolderI, public GMail3Obj
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

	GMail3Folder *Parent;
	DIterator<LDataFolderI, GMail3Folder, GMail3Store> Sub;
	DIterator<LDataI, GMail3Thing, GMail3Store> Items;
	DIterator<LDataPropI, Store3Field, GMail3Store> Flds;

	GMail3Folder(GMail3Store *store);
	~GMail3Folder();

	Store3CopyDecl;
	bool Serialize(GMail3Store::GStatement &s, bool Write) override;
	const char *GetClass() override { return "GMail3Folder"; }

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
	GMail3Folder *FindSub(char *Name);
	bool DbDelete();
	bool GenSizes();

	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
};

class GMail3Attachment : public Store3Attachment<GMail3Store, GMail3Mail, GMail3Attachment>
{
	int64 SegId;
	int64 BlobSize;

	LAutoString Headers;

	LString Name;
	LString MimeType;
	LString ContentId;
	LString Charset;
	
	/// This is set when the segment is not to be stored on disk.
	/// When signed and/or encrypted messages are stored, the original
	/// rfc822 image is maintained by not MIME decoding into separate
	/// segments but leaving it MIME encoded in one seg (headers and body).
	/// At runtime the segment is loaded and parsed into a temporary tree
	/// of GMail3Attachment objects. This flag is set for those temporary
	/// nodes.
	bool InMemoryOnly;

public:
	GMail3Attachment(GMail3Store *store);
	~GMail3Attachment();

	void SetInMemoryOnly(bool b);
	int64 GetId() { return SegId; }
	GMail3Attachment *Find(int64 Id);
	bool Load(GMail3Store::GStatement &s, int64 &ParentId);
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
	Store3Status Save(LDataI *Folder = 0) override;
	void OnSave() override;
};

class GMail3Mail : public GMail3Thing
{
	LAutoString TextCache;
	LAutoString HtmlCache;
	LString IdCache;
	LAutoPtr<uint64> SizeCache;
	LString InferredCharset;

	void LoadSegs();
	void OnSave() override;

	const char *GetTable() override { return MAIL3_TBL_MAIL; }
	void ParseAddresses(char *Str, int CC);
	const char *InferCharset();
	bool Utf8Check(LVariant &v);
	bool Utf8Check(LAutoString &v);

public:
	int Priority;
	int Flags;
	int AccountId;
	int64 MailSize;
	uint32_t MarkColour; // FIELD_MARK_COLOUR, 32bit rgba colour

	LVariant Subject;
	DIterator<LDataPropI, Store3Addr, GMail3Store> To;
	GMail3Attachment *Seg;
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

	GMail3Mail(GMail3Store *store);
	~GMail3Mail();

	Store3CopyDecl;
	void SetStore(GMail3Store *s) override;
	bool Serialize(GMail3Store::GStatement &s, bool Write) override;
	const char *GetClass() override { return "GMail3Mail"; }
	GMail3Attachment *GetAttachment(int64 Id);
	bool FindSegs(const char *MimeType, LArray<GMail3Attachment*> &Segs, bool Create = false);
	int GetAttachments(LArray<GMail3Attachment*> *Lst = 0);
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
	GDataIt GetList(int id) override;
	Store3Status SetRfc822(LStreamI *m) override;
};

class GMail3Contact : public GMail3Thing
{
	const char *GetTable() override { return MAIL3_TBL_CONTACT; }
	LHashTbl<IntKey<int64>, LString*> f;

public:
	LVariant Image;
	LDateTime DateMod;

	GMail3Contact(GMail3Store *store);
	~GMail3Contact();

	uint32_t Type() override { return MAGIC_CONTACT; }
	bool Serialize(GMail3Store::GStatement &s, bool Write) override;
	Store3CopyDecl;
	const char *GetClass() override { return "GMail3Contact"; }
	bool DbDelete() override;

	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;

	int64 GetInt(int id) override { return -1; }

	LVariant *GetVar(int id) override;
	Store3Status SetVar(int id, LVariant *i) override;

	const LDateTime *GetDate(int id) override;
	Store3Status SetDate(int id, const LDateTime *i) override;
};

class GMail3Group : public GMail3Thing
{
	const char *GetTable() override { return MAIL3_TBL_GROUP; }

	LString Name;
	LString Group;
	LDateTime DateMod;

public:
	GMail3Group(GMail3Store *store);
	~GMail3Group();

	uint32_t Type() override { return MAGIC_GROUP; }
	bool Serialize(GMail3Store::GStatement &s, bool Write) override;
	Store3CopyDecl;
	const char *GetClass() override { return "GMail3Group"; }
	bool DbDelete() override;

	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;

	int64 GetInt(int id) override { return -1; }

	const LDateTime *GetDate(int id) override;
	Store3Status SetDate(int id, const LDateTime *i) override;
};

class GMail3Filter : public GMail3Thing
{
	int Index;
	int StopFiltering;
	int Direction;
	LAutoString Name;
	LAutoString ConditionsXml;
	LAutoString ActionsXml;
	LAutoString Script;

	const char *GetTable() override { return MAIL3_TBL_FILTER; }

public:
	GMail3Filter(GMail3Store *store);
	~GMail3Filter();

	uint32_t Type() override { return MAGIC_FILTER; }
	LDataStoreI *GetStore() override { return Store; }
	bool Serialize(GMail3Store::GStatement &s, bool Write) override;
	Store3CopyDecl;
	const char *GetClass() override { return "GMail3Filter"; }
	bool DbDelete() override;

	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int Col, int64 n) override;
};

class GMail3Calendar : public GMail3Thing
{
	LString ToCache;

private:
	int CalType; // FIELD_CAL_TYPE
	LString To; // FIELD_TO
	CalendarPrivacyType CalPriv; // FIELD_CAL_PRIVACY
	int Completed; // FIELD_CAL_COMPLETED
	LDateTime Start; // FIELD_CAL_START_UTC
	LDateTime  End; // FIELD_CAL_END_UTC
	LString TimeZone; // FIELD_CAL_TIMEZONE
	LString Subject; // FIELD_CAL_SUBJECT
	LString Location; // FIELD_CAL_LOCATION
	LString Uid; // FIELD_UID
	bool AllDay; // FIELD_CAL_ALL_DAY
	int64 StoreStatus; // FIELD_STATUS - ie the current Store3Status
	LString EventStatus; // FIELD_CAL_STATUS

	LString Reminders; // FIELD_CAL_REMINDERS
	LDateTime LastCheck; // FIELD_CAL_LAST_CHECK
	int ShowTimeAs; // FIELD_CAL_SHOW_TIME_AS

	int Recur; // FIELD_CAL_RECUR
	int RecurFreq; // FIELD_CAL_RECUR_FREQ
	int RecurInterval; // FIELD_CAL_RECUR_INTERVAL
	LDateTime RecurEnd; // FIELD_CAL_RECUR_END_DATE
	int RecurCount; // FIELD_CAL_RECUR_END_COUNT
	int RecurEndType; // FIELD_CAL_RECUR_END_TYPE
	LString RecurPos; // FIELD_CAL_RECUR_FILTER_POS
	
	int FilterDays; // FIELD_CAL_RECUR_FILTER_DAYS
	int FilterMonths; // FIELD_CAL_RECUR_FILTER_MONTHS
	LString FilterYears; // FIELD_CAL_RECUR_FILTER_YEARS
	LString Notes; // FIELD_CAL_NOTES
	
	LColour Colour;

	const char *GetTable() override { return MAIL3_TBL_CALENDAR; }

public:
	GMail3Calendar(GMail3Store *store);
	~GMail3Calendar();

	uint32_t Type() override { return MAGIC_CALENDAR; }
	LDataStoreI *GetStore() override { return Store; }
	bool Serialize(GMail3Store::GStatement &s, bool Write) override;
	Store3CopyDecl;
	const char *GetClass() override { return "GMail3Filter"; }
	bool DbDelete() override;

	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int Col, int64 n) override;
	const LDateTime *GetDate(int id) override;
	Store3Status SetDate(int id, const LDateTime *t) override;
};

#endif
