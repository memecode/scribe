#ifndef _STORE3_MAIL2_H_
#define _STORE3_MAIL2_H_

#include "Lgi.h"
#include "Scribe.h"
#include "Store2.h"
#include "Store3Common.h"

extern char *HtmlToText(char *Html, char *InitialCharSet);
extern char *TextToHtml(char *Txt, char *Charset);

#define FIELD_CONDITION				41
#define FIELD_ACTION				42
#define FIELD_COND_FIELD			43
#define FIELD_COND_OPERATOR			44
#define FIELD_COND_VALUE			45
#define FIELD_COND_SOURCE			51
#define FIELD_COND_NOT				53

class FolderData;
class ThingData;
class AttachmentData;

#define _Str(var) \
	if (var != str) { DeleteArray(var); var = NewStr(str); } return (var != 0) == (str != 0);

#ifdef WIN32
#pragma pack(push, before_pack)
#pragma pack(1)
#endif
struct Mail2Date
{
	uint8 Day;
	uint8 Month;
	int16 Year;
	uint8 Hour;
	uint8 Minute;
	uint16 ThouSec;
};
#ifdef WIN32
#pragma pack(pop, before_pack)
#endif

class Mail2Addr : public Store3Addr
{
public:
	Mail2Addr(LDataStoreI *store, LDataPropI *i = 0) : Store3Addr(store, i)
	{
	}

	int Sizeof()
	{
		return	SizeofStr(Name) +
				SizeofStr(Addr);
	}

	bool Serialize(LFile &f, bool Write)
	{
		bool Status = true;
		if (Write)
		{
			WriteStr(f, Name);
			WriteStr(f, Addr);
		}
		else
		{
			Name.Reset(ReadStr(f PassDebugArgs));
			Addr.Reset(ReadStr(f PassDebugArgs));
		}
		return Status;
	}
};

class Mail2Field : public Store3Field
{
public:
	Mail2Field(LDataStoreI *Store) : Store3Field(Store)
	{
	}

	int Sizeof()
	{
		return	sizeof(Id) +
				sizeof(Width);
	}

	bool Serialize(LFile &f, bool Write)
	{
		if (Write)
		{
			f << Id;
			f << Width;
		}
		else
		{
			f >> Id;
			f >> Width;
		}
		return true;
	}
};

class LMail2Store : public LDataStoreI, public Storage2::StorageKitImpl
{
	friend class FolderData;
	friend class MailData;
	friend class ThingData;

	FolderData *Mailbox;
	LDataEventsI *Callback;
	LAutoString ErrorMsg;

public:
	LMail2Store(char *file, LDataEventsI *callback);

	~LMail2Store();

	LDataEventsI *GetEvents() { return Callback; }
	bool OnIdle() { return false; }
	uint64 Size();
	char *GetStr(int id);
	bool SetStr(int id, const char *str);
	int64 GetInt(int id);
	bool SetInt(int id, int64 i);
	LDataI *Create(int Type);
	LDataFolderI *GetRoot(bool Create);
	FolderData *GetFolder(char *Path);
	FolderData *CastFolder(StorageItem *i);
	Store3Status Delete(LArray<LDataI*> &Items, bool ToTrash);
	Store3Status Move(LDataFolderI *NewFolder, LArray<LDataI*> &Items);
	Store3Status Change(LArray<LDataI*> &Items, int PropId, LVariant &Value);
	bool Compact(LViewI *Parent, LDataPropI *Props);
	void OnEvent(void *Param);
};

/// This class glues the mail2 backend to the virtual store3 API
class FolderData : public LDataFolderI, public StorageObj
{
	bool Debug;

public:
	// Glue members
	LMail2Store *Kit;

	// Folder data memebers
	int32 Sort;
	uint8 Open;
	uint32 ItemType;
	uint32 UnRead;
	int16 Index;
	char *Name;
	ScribePerm ReadAccess, WriteAccess;
	uint8 Threaded;
	Store3SystemFolder System;
	
	// Collections
	DIterator<LDataPropI, Mail2Field, LMail2Store> Field;
	DIterator<LDataFolderI, FolderData, LMail2Store> Sub;
	DIterator<LDataI, ThingData, LMail2Store> Things;

	bool Load(bool Flds, bool Things);

	// Iterators
	GDataIterator<LDataFolderI*> &SubFolders();
	GDataIterator<LDataI*> &Children();	
	GDataIterator<LDataPropI*> &Fields();

	// Methods
	FolderData(LMail2Store *store);
	~FolderData();
	
	LDataI &operator =(LDataI &p);

	bool IsOrphan() { return Store == 0; }

	int Type();
	uint64 Size() { return Store ? Store->GetTotalSize() : Sizeof(); }
	bool IsOnDisk() { return Store != 0; }
	char *GetStr(int id);
	bool SetStr(int id, const char *str);
	int64 GetInt(int id);
	bool SetInt(int id, int64 i);
	int Sizeof();
	bool Serialize(LFile &f, bool Write);
	Store3Status DeleteAllChildren();
	Store3Status FreeChildren();

	FolderData *GetParent()
	{
		StorageItem *p = Store ? Store->GetParent() : 0;
		return p ? dynamic_cast<FolderData*>(p->Object) : 0;
	}

	// Stubs
	Store3Status Save(LDataI *Folder = 0);
	Store3Status Delete();
	LDataStoreI *GetStore() { return Kit; }
	GAutoStreamI GetStream(const char *file, int line) { return GAutoStreamI(0); }
};

class ThingData : public LDataI, public StorageObj
{
protected:
	bool Debug;

public:
	// Glue members
	LMail2Store *Kit;
	bool IsLoaded;

	ThingData(LMail2Store *s)
	{
		Debug = false;
		Kit = s;
		IsLoaded = true;
	}

	LDataStoreI *GetStore()
	{
		return Kit;
	}

	bool IsOrphan() { return Store == 0; }

	uint64 Size() { return Store ? Store->GetTotalSize() : 0; }
	bool IsOnDisk() { return Store != 0; }
	Store3Status Save(LDataI *Folder = 0);
	GAutoStreamI GetStream(const char *file, int line);
	Store3Status Delete();

	int Sizeof(int32 i)
	{
		return sizeof(int16) +	// Id
				sizeof(int8) +	// Type
				sizeof(int32);	// Data
	}

	int Write(LFile &f, int16 Id, int32 i)
	{
		uint8 Type = OBJ_INT;
		int w = f.Write(&Id, sizeof(Id));
		w += f.Write(&Type, sizeof(Type));
		w += f.Write(&i, sizeof(i));
		return w;
	}

	int Sizeof(char *s)
	{
		return sizeof(int16) +			// Id
				sizeof(int8) +			// Type
				sizeof(int32) +			// Size
				(s ? strlen(s) : 0);	// String data
	}

	int Write(LFile &f, int16 Id, char *s)
	{
		uint8 Type = OBJ_STRING;
		int32 Size = s ? strlen(s) : 0;
		int w = f.Write(&Id, sizeof(Id));
		w += f.Write(&Type, sizeof(Type));
		w += f.Write(&Size, sizeof(Size));
		if (s)
			w += f.Write(s, Size);
		return w;
	}

	int Sizeof(LDateTime &t)
	{
		return sizeof(int16) +		// Id
				sizeof(int8) +		// Type
				sizeof(int32) +			// Size
				sizeof(Mail2Date);	// Date
	}

	int Write(LFile &f, int16 Id, LDateTime &dt)
	{
		uint8 Type = OBJ_BINARY;
		Mail2Date d;
		int32 Size = sizeof(d);
		int w = f.Write(&Id, sizeof(Id));
		w += f.Write(&Type, sizeof(Type));
		w += f.Write(&Size, sizeof(Size));
		
		d.Year = dt.Year();
		d.Month = dt.Month();
		d.Day = dt.Day();
		d.Hour = dt.Hours();
		d.Minute = dt.Minutes();
		d.ThouSec = dt.Seconds() + (1000 * dt.Thousands());

		w += f.Write(&d, Size);

		return w;
	}

	FolderData *GetParent()
	{
		StorageItem *Si = Store ? Store->GetParent() : 0;
		FolderData *Par = Si ? dynamic_cast<FolderData*>(Si->Object) : 0;
		return Par;
	}

	int Sizeof() { LAssert(0); return 0; }
	bool Serialize(LFile &f, bool Write) { LAssert(0); return 0; }
	LDataI &operator =(LDataI &p)  { LAssert(0); return *this; }
	int Type() { LAssert(0); return 0; }
	bool SetStream(GAutoStreamI stream) { LAssert(0); return 0; }
};

/// This class glues the mail2 backend to the virtual store3 API
class MailData : public ThingData
{
public:
	// Mail members
	uint32 Flags;
	char *Label;
	char *FwdMsgId;
	char *ServerUid;
	char *Subject;
	char *Body;
	char *BodyCharset;
	char *Html;
	char *HtmlCharset;
	char *MessageID;
	char *BounceMessageID;
	char *InternetHeader;
	char *References;
	LDateTime DateReceived;
	LDateTime DateSent;
	uint32 AccountId;
	uint8 Priority;
	uint32 MarkColour; // FIELD_MARK_COLOUR, 32bit rgba colour

	Mail2Addr From;
	Mail2Addr Reply;
	DIterator<LDataPropI, Mail2Addr, LMail2Store> To;
	AttachmentData *Seg;

	MailData(LMail2Store *s);
	~MailData();

	LDataI &operator =(LDataI &p);

	bool Load();
	bool ParseHeaders();
	
	/// This function reconstructs a plausible MIME tree 
	/// from the flat format that mail2 saves as.
	bool RebuildMimeTree();

	void Empty();
	Store3Status Save(LDataI *Folder = 0);
	int Type();
	char *GetStr(int id);
	bool SetStr(int id, const char *str);
	int64 GetInt(int id);
	bool SetInt(int id, int64 i);
	LDateTime *GetDate(int id);
	bool SetDate(int id, LDateTime *t);
	LDataPropI *GetObj(int id);
	LDataIt GetList(int id);
	bool SetMime(LAutoPtr<LMime> m);
	int Sizeof();
	bool Serialize(LFile &Stream, bool Write);
};

class AttachmentData :
	public Store3Attachment<LMail2Store, MailData, AttachmentData>,
	public StorageObj
{
	bool PlaceHolder;
	uint32 Content;
	uint32 DataSize;
	char *Name;
	char *MimeType;
	char *ContentId;
	char *Charset;

	char **MailBody;
	char **MailCharset;

	LStreamI *Import;
	LAutoString InternetHeaderCache;

	void Load();

public:
	bool IsLoaded;

	AttachmentData(LMail2Store *store);
	~AttachmentData();

	void SetExtern(char **body = 0, char **charset = 0);
	void SetPlaceholder() { PlaceHolder = true; Dirty = false; }
	LArray<AttachmentData*> GetChildren();

	// Store3Attachment impl
	void OnSave();

	// StorageObj impl
	int Type() { return MAGIC_ATTACHMENT; }
	int Sizeof();
	bool Serialize(LFile &Stream, bool Write);

	// LDataI impl
	LDataI &operator =(LDataI &p);
	bool IsOnDisk();
	bool IsOrphan();
	uint64 Size();
	Store3Status Save(LDataI *Parent);
	Store3Status Delete();
	GAutoStreamI GetStream(const char *file, int line);
	bool SetStream(GAutoStreamI s);

	// LDataPropI
	int64 GetInt(int id);
	bool SetInt(int id, int64 i);
	char *GetStr(int id);
	bool SetStr(int id, const char *str);
};

class ContactData : public ThingData, public ObjProperties
{
	LAutoPtr<char,true> AltEmailCache;

public:
	// Glue members
	LArray<char*> Plugins;
	LArray<char*> AltEmail;

	ContactData(LMail2Store *s);
	~ContactData();

	LDataI &operator =(LDataI &p);

	int SizeofField(const char *Name);

	void Load();
	char *GetStr(int id);
	bool SetStr(int id, const char *str);
	int64 GetInt(int id);
	bool SetInt(int id, int64 i);
	LDataIt GetList(int id);
	int Type();
	int Sizeof();
	bool Serialize(LFile &f, bool Write);
};

class CalendarData : public ThingData
{
public:
	int CalType; // FIELD_CAL_TYPE
	int Completed; // FIELD_CAL_COMPLETED
	LDateTime Start; // FIELD_CAL_START_UTC
	LDateTime  End; // FIELD_CAL_END_UTC
	char *TimeZone; // FIELD_CAL_TIMEZONE
	char *Subject; // FIELD_CAL_SUBJECT
	char *Location; // FIELD_CAL_LOCATION
	char *Uid; // FIELD_UID

	int ReminderTime; // FIELD_CAL_REMINDER_TIME
	int ReminderAction; // FIELD_CAL_REMINDER_ACTION
	char *ReminderArg; // FIELD_CAL_REMINDER_ARG
	int ShowTimeAs; // FIELD_CAL_SHOW_TIME_AS

	int Recur; // FIELD_CAL_RECUR
	int RecurFreq; // FIELD_CAL_RECUR_FREQ
	int RecurInterval; // FIELD_CAL_RECUR_INTERVAL
	LDateTime RecurEnd; // FIELD_CAL_RECUR_END_DATE
	int RecurCount; // FIELD_CAL_RECUR_END_COUNT
	int RecurEndType; // FIELD_CAL_RECUR_END_TYPE
	char *RecurPos; // FIELD_CAL_RECUR_FILTER_POS
	
	int FilterDays; // FIELD_CAL_RECUR_FILTER_DAYS
	int FilterMonths; // FIELD_CAL_RECUR_FILTER_MONTHS
	char *FilterYears; // FIELD_CAL_RECUR_FILTER_YEARS
	char *Notes; // FIELD_CAL_NOTES


	CalendarData(LMail2Store *s);
	~CalendarData();

	LDataI &operator =(LDataI &p);

	void Load();

	char *GetStr(int id);
	bool SetStr(int id, const char *str);
	int64 GetInt(int id);
	bool SetInt(int id, int64 i);
	LDateTime *GetDate(int id);
	bool SetDate(int id, LDateTime *t);
	LDataIt GetList(int id);

	int Type();
	int Sizeof();
	bool Serialize(LFile &f, bool Write);
};

class FilterData : public ThingData
{
public:
	// Glue members
	int Flags;
	int Index;
	char *Name;
	char *Script;
	uint8 StopFiltering;
	char *ConditionsXml;

	DIterator<LDataPropI, FilterAction, LMail2Store> Actions;
	LAutoString ActionXml;
	uint8 FilterFlags;
	uint8 Outgoing;
	// int CombineOp;
	// List<FilterCondition> Conditions;

	FilterData(LMail2Store *s);
	~FilterData();
	
	LDataI &operator =(LDataI &p);

	void Load();
	char *GetStr(int id);
	bool SetStr(int id, const char *str);
	int64 GetInt(int id);
	bool SetInt(int id, int64 i);
	LDataIt GetList(int id);
	int Type();
	int Sizeof();
	bool Serialize(LFile &f, bool Write);
};

class GroupData : public ThingData
{
public:
	// Glue members
	int Flags;
	char *Name;
	char *Group;

	GroupData(LMail2Store *s);
	~GroupData();
	
	LDataI &operator =(LDataI &p);

	void Load();
	char *GetStr(int id);
	bool SetStr(int id, const char *str);
	int64 GetInt(int id);
	bool SetInt(int id, int64 i);
	int Type();
	int Sizeof();
	bool Serialize(LFile &f, bool Write);
};


#endif