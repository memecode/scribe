#ifndef __SCRIBE_LIST_ADDR_H
#define __SCRIBE_LIST_ADDR_H

class RecipientItem : public LDom
{
	Contact *c;
	ContactGroup *g;
	// ScribePlugin_DirectoryEntry *e;
	LVariant Name, Email;
	char Buf[128];
	
	void _New();

public:
	RecipientItem(RecipientItem *item);
	RecipientItem(Contact *contact);
	RecipientItem(ContactGroup *group);

	const char *GetClass() override { return "RecipientItem"; }

	// Name
	const char *GetName();
	const char *GetFirst();
	const char *GetLast();

	// Attribs
	const char *GetEmail();
	const char *GetAttribute(char *Attr);

	// Methods
	void DoUI(LView *Parent);

	// Interface
	Contact *GetContact() { return c; }
	ContactGroup *GetGroup() { return g; }
};

class ScribeClass ListAddr :
	public AddressDescriptor,
	public LListItem,
	public LDataPropI
{
	char *MakeName(const char *Delim = "", bool LocalTime = true);

	bool Loaded;
	ScribeWnd *App;
	List<RecipientItem> Who;

public:
	ListAddr(ScribeWnd *App);
	ListAddr(ScribeWnd *App, LDataPropI *Prop);
	ListAddr(ScribeWnd *App, RecipientItem *c);
	ListAddr(ScribeWnd *App, AddressDescriptor *a, List<Contact> *Cache = NULL);
	ListAddr(ScribeWnd *App, const char *Email, const char *Name, List<Contact> *Cache = NULL);

	ListAddr(Contact *c);
	ListAddr(ContactGroup *g);

	~ListAddr();
	void _Delete();
	
	const char *GetClass() override { return "ListAddr"; }

	void CopyFrom(AddressDescriptor &a);
	ListAddr &operator =(AddressDescriptor &a) { CopyFrom(a); return *this; }

	// Methods
	void AddToContacts(bool Ui, ScribeFolder *Folder = NULL);
	int Length();
	RecipientItem *operator [](int i);
	List<RecipientItem>::I begin();
	List<RecipientItem>::I end();
	void Delete(RecipientItem *i);
	void SetWho(RecipientItem *i, int Idx = -1);
	bool Serialize(LString &s, bool write);

	// List item
	const char *GetText(int i) override;
	bool SetText(const char *s, int i = 0) override;
	int GetImage(int Flags) override;
	void OnFound(bool Persist = false);
	void OnFind(List<Contact> *Cache = NULL, bool Persist = false);
	void OnMouseClick(LMouse &m) override;

	// Dom
	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;
	bool SetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override;

	// Clipboard
	char *Copy();
	void Paste(char *s);

	// DataI
	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
	LDataPropI &operator =(LDataPropI &p);
};

#endif
