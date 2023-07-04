/**
 * \file
 * \author Matthew Allen
 *
 * http://tools.ietf.org/html/rfc3501
 * http://tools.ietf.org/html/rfc4315
 *
 * cmake -G "Visual Studio 14 2015 Win64" -Dprotobuf_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=../../../../install ../..
 */
#ifndef __SCRIBE_IMAP_H__
#define __SCRIBE_IMAP_H__

#include "lgi/common/Mime.h"
#include "lgi/common/Mail.h"
#include "Store3Common.h"
#include "ScribeUtils.h"

#define CastFld(obj) (dynamic_cast<ImapFolder*>(obj))
#define CastMail(obj) (dynamic_cast<ImapMail*>(obj))

#define IMAP_PROTOBUF			0
#if IMAP_PROTOBUF
	#include "include/Scribe.pb.h"
#else
	// Folder meta sub-element names
	#define TAG_FOLDER			"Folder"
	#define TAG_MAIL			"Mail"
	#define TAG_EMAILS			"Emails"
	#define TAG_FIELDS			"Fields"

	// Email attributes
	#define ATTR_UID			"Uid"
	#define ATTR_SIZE			"Size"
	#define ATTR_STRUCT			"Structure"
	#define ATTR_FLAGS			"Flags" // Flags sent to the server...
	#define ATTR_LOCAL			"Local" // Flags not supported by IMAP... (not sent to the server)
	#define ATTR_DATE			"Date"
	#define ATTR_COLOUR			"Colour"
	#define ATTR_MSGID			"MsgId"
	#define ATTR_SUBJECT		"Subject"
	#define ATTR_FROM			"From"
	#define ATTR_REPLYTO		"ReplyTo"
	#define ATTR_LABEL			"Label"
#endif

#define TIMEOUT_STORE_ONPULSE	3000
#define TIMEOUT_LISTING_CHUNK	1000
#define TIMEOUT_FOLDER_LOAD		100 // ms
#define IMAP_BLOCK_SIZE			100 // chuck large processes into this many items

enum ImapMsgType
{
	IMAP_NULL,

	// Connection
	IMAP_ONLINE,
	IMAP_OFFLINE,
	IMAP_ERROR,

	// Lifespan events
	IMAP_ON_NEW,
	IMAP_ON_DEL,
	IMAP_ON_MOVE,

	// Folder
	IMAP_SELECT_FOLDER,
	IMAP_CREATE_FOLDER,
	IMAP_DELETE_FOLDER,
	IMAP_EXPUNGE_FOLDER,
	IMAP_RENAME_FOLDER,
	IMAP_LOAD_FOLDER,

	// Mail
	IMAP_FOLDER_SELECTED,
	IMAP_FOLDER_LISTING,
	IMAP_DOWNLOAD,
	IMAP_APPEND,
	IMAP_MOVE_EMAIL,

	// General
	IMAP_DELETE,
	IMAP_UNDELETE,
	IMAP_SET_FLAGS,
	
	// Check
	IMAP_MSG_MAX
};

extern const char *ImapMsgTypeNames[IMAP_MSG_MAX];
extern bool ValidateImapDate(LDateTime &dt);

/// MAPI folder info struct
struct ImapFolderInfo
{
	// The local path name
	LString Local;
	// The remote path name
	LString Remote;
	// [Optional] Highest known UID
	uint32_t LastUid;
	// Separator for IMAP path
	char Sep;
	
	ImapFolderInfo()
	{
	    LastUid = -1;
	    Sep = 0;
	}
	
	ImapFolderInfo &operator =(ImapFolderInfo &i)
	{
		Local = i.Local;
		Remote = i.Remote;
		return *this;
	}
};

/// MAPI mail info struct, all of these fields are optional
struct ImapMailInfo
{
	// The email meta flags
	ImapMailFlags Flags;
	// The sequence number of the email
	uint32_t Seq;
	// The server UID of the email
	uint32_t Uid;
	// The initial headers
	LString Headers;
	// The local path to that .eml file
	LString Local;
	// The size of the email in bytes
	int64 Size;
	// The structure of the email
	LString Structure;
	// The internal date of the email
	LString Date;
};

struct ImapMsg
{
	ImapMsgType Type;

	// The path of the parent folder if required
	LString Parent;
	// A new remote path name (for moving/renaming folder)
	LString NewRemote;
	
	// Folder related...
	LArray<ImapFolderInfo> Fld;
	
	// Mail related
	LArray<ImapMailInfo> Mail;
	uint8_t Last : 1;
	uint8_t New : 1;

	// Debug
	const char *File;
	int Line;
	int IntParam;
	LError Error;

	ImapMsg(ImapMsgType t, const char *file, int line)
	{
		Type = t;
		Last = 0;
		New = 0;
		File = file;
		Line = line;
		IntParam = 0;
	}
	
	void SetType(ImapMsgType t, const char *file, int line)
	{
		Type = t;
		File = file;
		Line = line;
	}
};

extern char *TrimWhite(char *s);

class ImapFolder;
class ImapThread;
class ImapMail;

class ImapStore : public LDataStoreI
{
	friend class ImapThread;
	friend class ImapFolder;
	friend class FolderLoaderThread;

	LVariant Host, User, Pass;
	int Port;
	int ConnectFlags;
	LDataEventsI *Callback;
	ImapFolder *Root;
	char *Cache;
	int AccountId;
	ImapThread *Thread;
	LStream *Log;
	ScribeAccountletStatusIcon Online;
	LArray<ImapMsg*> Listing;
	int64 LastPulse;
	int64 SelectTime;
	int64 ListingTime;
	LArray<ImapMsg*> Msgs;
	LAutoString ErrorMsg;
	LAutoPtr<LThread> FolderLoader;
	MailProtocolProgress *ItemProgress, *DataProgress;
	
	LAutoPtr<ProtocolSettingStore> SettingStore;

	ImapFolder *GetParent(char *Local, char *Remote);
	ImapFolder *GetTrash();
	ImapFolder *GetSystemFolder(int Type);

public:
	ImapStore(	char *host,
				int port,
				char *user,
				char *pass,
				int flags,
				LDataEventsI *callback,
				LCapabilityClient *caps,
				MailProtocolProgress *prog[2],
				LStream *log,
				int accoundid,
				LAutoPtr<ProtocolSettingStore> store);
	~ImapStore();

	const char *GetClass() override { return "ImapStore"; }
	void PostStore(ImapMsg *m) { Callback->Post(this, m); }
	bool PostThread(ImapMsg *m, bool UiPriority);

	void OnNew(const char *File, int Line, LDataFolderI *parent, LArray<LDataI*> &new_items, int pos, bool is_new);
	bool OnDelete(const char *File, int Line, LDataFolderI *parent, LArray<LDataI*> &del);
	bool OnChange(const char *File, int Line, LArray<LDataI*> &items, int FieldHint);
	bool OnIdle();
	LStreamI *GetLogger() { return Log; }

	LDataEventsI *GetEvents() { return Callback; }
	char *GetCache();
	LStream *GetLog() { return Log; }
	uint64 Size();
	LDataI *Create(int Type);
	LDataFolderI *GetRoot(bool create);
	Store3Status Move(LDataFolderI *NewFolder, LArray<LDataI*> &Items);
	Store3Status Delete(LArray<LDataI*> &Items, bool ToTrash);
	Store3Status Change(LArray<LDataI*> &Items, int PropId, LVariant &Value, LOperator Operator);
	void Compact(LViewI *Parent, LDataPropI *Props, std::function<void(bool)> OnStatus);
	void OnEvent(void *Param);
	const char *GetStr(int id);
	int64 GetInt(int id);
	Store3Status SetInt(int id, int64 val);
	LDataPropI *GetObj(int id);
};

class ImapThread : public LThread, public LMutex
{
	struct ImapThreadPrivate *d;

	void Log(LSocketI::SocketMsgType Type, const char *Fmt, ...);

public:
	ImapThread(ImapStore *s, LCapabilityClient *caps, LStream *log, ProtocolSettingStore *Store);
	~ImapThread();

	void PostThread(ImapMsg *m, bool UiPriority);
	void PostStore(ImapMsg *m);
	int Main();

	ImapMsg *GetListing();
	void FlushListing(bool Force = false);
};

class ImapAttachment : public Store3Attachment<ImapStore, ImapMail, ImapAttachment>
{
	LAutoString Name, MimeType, ContentId, Charset;
	LMime *Seg;

	void _ClearChildSegs();

	/// This is set when the segment is not to be stored on disk.
	/// When signed and/or encrypted messages are stored, the original
	/// rfc822 image is maintained by not MIME decoding into separate
	/// segments but leaving it MIME encoded in one seg (headers and body).
	/// At runtime the segment is loaded and parsed into a temporary tree
	/// of LMail3Attachment objects. This flag is set for those temporary
	/// nodes.
	bool InMemoryOnly;

public:
	ImapAttachment(ImapStore *store, ImapMail *mail = 0, LMime *seg = 0);
	ImapAttachment(ImapStore *store, ImapMail *mail, LDataPropI *att);
	~ImapAttachment();

	const char *GetClass() override { return "ImapAttachment"; }
	void SetInMemoryOnly(bool b);

	Store3CopyDecl;
	bool IsOrphan() override { return Mail == 0; }
	LMime *GetSeg() { return Seg; }
	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;

	uint32_t Type() override;
	bool IsOnDisk() override;
	uint64 Size() override;
	Store3Status Save(LDataI *Folder = 0) override;
	Store3Status Delete(bool ToTrash = false) override;
	LAutoStreamI GetStream(const char *file, int line) override;
	bool SetStream(LAutoStreamI stream) override;
	void OnSave() override;
};

class ImapFolderData;
class ImapMail : public LDataI
{
public:
	enum ImapMailState
	{
		ImapMailIdle,
		ImapMailGettingBody,
		ImapMailMoving,
		ImapMailDeleting
	};

protected:
	ImapMailState State;
	LAutoString TextCache, HtmlCache;
	LAutoString HeaderCache;
	LString UidCache;

	Store3Addr *ProcessAddress(Store3Addr &Addr, const char *FieldId, const char *RfcField);

public:
	const char *AllocFile = NULL;
	int AllocLine = 0;
	
	#if IMAP_PROTOBUF
	typedef scribe::Email *IMeta;
	#else
	typedef LXmlTag *IMeta;
	#endif

	ImapStore *Store = NULL;
	ImapFolderData *Data = NULL;
	ImapFolder *Parent = NULL;

	Store3State Loaded = Store3Unloaded;
	LString Path;
	uint32_t Uid; // Server UID

	ImapMailFlags RemoteFlags;
	int LocalFlags;

	int Priority;
	int64 DataSize;
	uint8_t SegDirty : 1;

	LString MsgId;
	LString Subject;
	LString Label;
	LString Structure;
	Store3Addr From;
	Store3Addr Reply;
	LDateTime DateReceived;
	LDateTime DateSent;
	LColour Colour;
	DIterator<LDataPropI, Store3Addr, ImapStore> To;
	
	LAutoStreamI Stream;
	ImapAttachment *Seg;

	ImapMail(ImapStore *store, const char *file = NULL, int line = 0, uint32_t uid = 0);
	~ImapMail();

	const char *GetClass() override { return "ImapMail"; }
	
	void SetState(ImapMailState s);
	void SetRemoteFlags(ImapMailFlags f);
	bool IsOrphan() override { return Parent == 0; }
	uint32_t Type() override { return MAGIC_MAIL; }
	bool IsOnDisk() override { return Path.Get() != NULL; }
	uint64 Size() override { return LFileSize(Path); }
	LDataStoreI *GetStore() override { return Store; }
	void Serialize(IMeta m, bool Write);
	IMeta GetMeta(bool AllowCreate = true);

	Store3CopyDecl;

	void Load();
	void ReadMime(ImapMail::IMeta MetaTag);
	Store3Status SetRfc822(LStreamI *m) override;
	bool FindSegs(const char *Type, LArray<ImapAttachment*> &Segs);
	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
	const LDateTime *GetDate(int id) override;
	Store3Status SetDate(int id, const LDateTime *i) override;
	LDataPropI *GetObj(int id) override;
	Store3Status SetObj(int id, LDataPropI *i) override;
	LDataIt GetList(int id) override;
	Store3Status Save(LDataI *Folder = 0) override;
	LAutoStreamI GetStream(const char *file, int line) override;
	bool SetStream(LAutoStreamI stream) override;

	/// This is called to start the deletion process
	Store3Status Delete(bool ToTrash) override;
	/// When the thread has deleted the email, this is called to finialize the deletion.
	bool OnDelete();
	void OnDownload(LAutoString &Headers);
};

#if IMAP_PROTOBUF
	#define FOLDER_META_NAME	"Folder.proto"
#else
	#define FOLDER_META_NAME	"Folder.xml"
	#define PROP_SORT			"Sort"
	#define PROP_EXPANDED		"Expanded"
	#define PROP_UNREAD			"Unread"
	#define PROP_READ			"ReadPerm"
	#define PROP_WRITE			"WritePerm"
	#define PROP_THREAD			"Thread"
#endif

class ImapFolderFld : public LXmlTag, public LDataPropI
{
	ImapFolder *Parent;

public:
	ImapFolderFld(LDataStoreI *s, ImapFolder *p = 0, int id = 0, int width = 100);

	const char *GetStr(int id);
	int64 GetInt(int id);
	Store3Status SetInt(int id, int64 i);
};

class ImapFolderData
{
	friend class ImapStore;

protected:
	// Mail containers, "UidMap" and "Mail" should always mirror each other 1:1 for email
	// that is not deleted. Deleted email will exist on in "UidMap" till they get expunged.
	//
	// The only exception to this is when a new email is being appended to the mailbox,
	// In that case it'll exist (with a temporary filename and no UID) in the Mail store
	// for a little while until the thread responds with an IMAP_APPEND msg.
	LHashTbl<IntKey<uint32_t>,ImapMail::IMeta> UidMap;
	DIterator<LDataI, ImapMail, ImapStore> Mail;

	void Swap(ImapFolderData &s)
	{
		UidMap.Swap(s.UidMap);
		Mail.Swap(s.Mail);
	}

public:
	ImapMail::IMeta GetMeta(uint32_t Uid, bool Create = false);
	ImapMail *GetMail(uint32_t Uid);
	bool DeleteMeta(uint32_t Uid);
	bool AddMail(ImapMail *m, LArray<LDataI*> *OnNew);
	bool DelMail(ImapMail *m);
	bool HasMail(ImapMail *m) { return Mail.IndexOf(m) >= 0; }

	virtual void LoadMail() = 0;
	virtual void SetDirty(bool b = true) = 0;
};

class ImapFolder : public LDataFolderI, public ImapFolderData
{
	friend struct ImapFolderLoadThread;

	int64 LastIdleSelect;
	bool IsOnline;
	int SortCache;
	LString RootName;
	LAutoPtr<LThread> WriteThread;
	ImapFolder *_Parent;

	// Threaded loading...
	LAutoPtr<struct ImapFolderLoadThread> LoadThread;

	#if IMAP_PROTOBUF
		bool ThreadView;
		ScribePerm PermRead;
		ScribePerm PermWrite;
		int SortField;
		bool Expanded;
		int UnreadCount;
	#else
		LXmlTag Meta;
	#endif

	// Loading of the files and folder
	int ScanState;
	LArray<LString> Folders;
	LArray<LString> Files;
	bool Dirty;

	// Events...
	LArray<std::function<void(Store3Status)>> OnLoad;

public:
	ImapStore *Store;
	Store3SystemFolder System;
	
	/// The item type...
	int ItemType;
	/// The IMAP path
	LString Remote;
	/// The on disk cache folder
	LString Local;
	/// Temp storage for leaf name, used before written to disk
	LString LeafName;
	/// The sub-folders of this folder
	DIterator<LDataFolderI, ImapFolder, ImapStore> Sub;
	/// The configured fields in the folder
	DIterator<LDataPropI, ImapFolderFld, ImapStore> Field;

	ImapFolder(ImapStore *store, const char *path = 0);
	~ImapFolder();

	const char *GetClass() override { return "ImapFolder"; }
	Store3CopyDecl;

	bool IsOrphan() override { return _Parent == NULL; }
	bool IsRoot() { return ItemType == MAGIC_NONE; }
	uint32_t Type() override { return MAGIC_FOLDER; }
	bool IsOnDisk() override { return Local && Remote; }
	LDataStoreI *GetStore() override { return Store; }
	ImapFolder *Find(const char *Local, const char *Remote);
	int GetLastUid();
	void Swap(ImapFolder &f);
	ImapFolder *GetParent() { return _Parent; }
	void SetParent(ImapFolder *p);

	void LoadMail() override;
	bool IsLoading() { return LoadThread.Get() != NULL; }
	void OnLoadMail(bool Threaded);
	Store3Status WhenLoaded(std::function<void(Store3Status)> cb);

	void LoadSub(bool All = false);
	void ScanFolder();

	Store3Status Move(LArray<LDataI*> &Items);
	LString MakeImapPath(char *From);
	uint64 Size() override;
	Store3Status Save(LDataI *Into = 0) override;
	Store3Status Delete(bool ToTrash = true) override;
	LAutoStreamI GetStream(const char *file, int line) override;
	bool SetStream(LAutoStreamI stream) override;
	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
	const LDateTime *GetDate(int id) override;
	Store3Status SetDate(int id, const LDateTime *i) override;
	LDataPropI *GetObj(int id) override;
	LDataIt GetList(int id) override;
	LDataIterator<LDataFolderI*> &SubFolders() override;
	LDataIterator<LDataI*> &Children() override;
	LDataIterator<LDataPropI*> &Fields() override;
	Store3Status FreeChildren() override;
	Store3Status DeleteAllChildren() override;
	void SetDirty(bool b = true) override;
	LString MailPath(uint32_t Uid, bool CheckExists = true);

	// Mail container stuff
	ImapMail *FindByFile(char *File);
	bool Serialize(bool Write);

	// Events
	void OnPulse();
	void OnDeleteComplete();
	void OnListing(ImapMsg *m);
	void OnSelect(bool b) override;
	void OnCommand(const char *Name) override;
	void OnExpunge();
	bool OnRename(const char *NewRemote);
};


#endif
