#ifndef _STORE3_COMMON_H_
#define _STORE3_COMMON_H_

#include "lgi/common/Store3.h"
#include "lgi/common/Mime.h"

#include "ScribeDefs.h"

class Store3MimeType
{
	LAutoString Mt;

public:
	Store3MimeType(const char *mt)
	{
		Mt.Reset(NewStr(mt));
	}

	#undef IsText

	operator char*() { return Mt; }
	Store3MimeType &operator =(const char *s) { Mt.Reset(NewStr(s)); return *this; }
	bool Is(const char *Type)	{ return Mt && _stricmp(Mt, Type) == 0; }
	bool IsMultipart()			{ return Mt && _strnicmp(Mt, "multipart/", 10) == 0; }
	bool IsText()				{ return Mt && _strnicmp(Mt, "text/", 5) == 0; }
	bool IsPlainText()			{ return Is(sTextPlain); }
	bool IsHtml()				{ return Is(sTextHtml); }
	bool IsMixed()				{ return Is(sMultipartMixed); }
	bool IsAlternative()		{ return Is(sMultipartAlternative); }
	bool IsRelated()			{ return Is(sMultipartRelated); }
};

class Store3Addr : public LDataPropI
{
	LDataStoreI *Store;

public:
	LString Name, Addr;
	int CC;

	Store3Addr(LDataStoreI *store, LDataPropI *i = NULL);	
	~Store3Addr();

	const char *GetClass() override { return "Store3Addr"; }
	Store3CopyDecl;

	void SetStore(LDataStoreI *s);
	void Empty();
	const char *GetStr(int id) override;
	Store3Status SetStr(int id, const char *str) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
	size_t Sizeof();

	bool GetVariant(const char *n, LVariant &Value, const char *Array = NULL) override;
	bool SetVariant(const char *n, LVariant &Value, const char *Array = NULL) override;
};

class Store3Field : public LDataPropI
{
public:
	int32 Id, Width;

	Store3Field(LDataStoreI *Store, int id = 0, int width = 100);
	
	const char *GetClass() override { return "Store3Field"; }
	LDataPropI &operator =(LDataPropI &p) { LAssert(0); return *this; }

	const char *GetStr(int id) override;
	int64 GetInt(int id) override;
	Store3Status SetInt(int id, int64 i) override;
};

template<class TStore, class TMail, class TAttach>
class Store3Attachment : public LDataI
{
protected:
	TStore *Kit = NULL;
	TMail *Mail = NULL;
	TAttach *Parent = NULL;
	DIterator<LDataPropI, TAttach, TStore> Children;
	bool Dirty = true;
	LAutoPtr<LStreamI> Import;

	void _Delete()
	{
		if (Parent)
		{
			#ifdef _DEBUG
			TAttach *This = dynamic_cast<TAttach*>(this);
			#endif
			LAssert(This && Parent->Children.IndexOf(This) >= 0);
			Parent->Children.Delete(this);
			Parent = NULL;
		}
		else if (Mail)
		{
			Mail->Seg = NULL;
		}
		Mail = NULL;

		TAttach *c;
		while (Children.Length())
		{
			c = Children.a[0];
			LAssert(c->Parent == this);
			DeleteObj(c);
		}
	}

public:
	Store3Attachment(TStore *store)
	{
		Kit = store;
		Children.State = Store3Loaded;
	}
	
	~Store3Attachment()
	{
		// If this assert fires, you didn't call 
		// _Delete() in your destructor.
		LAssert(Parent == NULL && Mail == NULL);
	}

	bool GetDirty()
	{
		return Dirty;
	}

	void Detach()
	{
		if (Parent)
		{
			TAttach *This = dynamic_cast<TAttach*>(this);
			LAssert(This!=NULL);
			LAssert(Parent->Children.a.HasItem(This));
			Parent->Children.a.Delete(This);
			Parent = NULL;
			Dirty = true;
		}
		else if (Mail)
		{
			if (Mail->Seg == this)
			{
				Mail->Seg = NULL;
			}
			Dirty = true;
		}
		
		Mail = NULL;
	}
	
	void AttachTo(TAttach *Obj)
	{
		Detach();

		if (Obj)
		{
			TAttach *This = dynamic_cast<TAttach*>(this);
			
			bool HasChild = Obj->Children.a.HasItem(This);
			bool HasParent = false;
			for (TAttach *p = This; p; p = p->GetParent())
			{
				if (p == Obj)
				{
					HasParent = true;
					break;
				}
			}

			if (This &&
				!HasChild &&
				!HasParent)
			{
				Obj->Children.a.Add(This);
				Parent = Obj;
				SetMail(Obj->Mail);
			}
			else
			{
				LAssert(0);
			}
		}
	}
	
	void AttachTo(TMail *Obj)
	{
		Detach();

		if (Obj)
		{
			if (Obj->Seg != NULL &&
				Obj->Seg != this)
			{
				auto s = Obj->Seg;
				Obj->Seg->Detach();
				delete s;
			}
			
			Obj->Seg = dynamic_cast<TAttach*>(this);
			SetMail(Obj);
		}
	}

	void SetMail(TMail *m)
	{
		Mail = m;
		if (m)
			Kit = m->Store; // If the mail's store changes, we should update here

		// TAttach *This = dynamic_cast<TAttach*>(this);
		for (unsigned i=0; i<Children.Length(); i++)
		{
			auto c = Children.a[i];
			bool Ok = true;
			for (auto p = GetParent(); p; p = p->GetParent())
			{
				if (p == c)
				{
					Ok = false;
					break;
				}
			}
			if (Ok)
				Children.a[i]->SetMail(m);
		}
	}
	
	TAttach *GetParent()
	{
		return Parent;
	}
	
	bool FindSegs(const char *SearchMimeType, LArray<TAttach*> &Results)
	{
		const char *Mt = GetStr(FIELD_MIME_TYPE);
		if (!Mt)
			Mt = sTextPlain;

		if (!_stricmp(SearchMimeType, Mt))
		{
			Results.Add(dynamic_cast<TAttach*>(this));
		}

		for (unsigned i=0; i<Children.Length(); i++)
		{
			Children.a[i]->FindSegs(SearchMimeType, Results);
		}

		return Results.Length() > 0;
	}

	LDataIt GetList(int id)
	{
		if (id == FIELD_MIME_SEG)
			return &Children;

		return 0;
	}

	TAttach *FindChildByMimeType(const char *MimeType)
	{
		for (unsigned i=0; i<Children.Length(); i++)
		{
			TAttach *a = dynamic_cast<TAttach*>(Children[i]);
			if (a)
			{
				if (a->Is(MimeType))
					return a;
			}
		}
		return NULL;
	}
	
	bool Is(const char *Type)	{ const char *Mt = GetStr(FIELD_MIME_TYPE); return Mt && _stricmp(Mt, Type) == 0; }
	bool IsMultipart()			{ const char *Mt = GetStr(FIELD_MIME_TYPE); return Mt && _strnicmp(Mt, "multipart/", 10) == 0; }
	bool IsPlainText()			{ return Is(sTextPlain); }
	bool IsHtml()				{ return Is(sTextHtml); }
	bool IsMixed()				{ return Is(sMultipartMixed); }
	bool IsAlternative()		{ return Is(sMultipartAlternative); }
	bool IsRelated()			{ return Is(sMultipartRelated); }

	virtual void OnSave() = 0;
	LDataStoreI *GetStore() { return Kit; }
};

extern bool Store3ToLMime(LMime *Out, LDataPropI *In);
extern bool GMimeToStore3(LDataPropI *Out, LMime *In, bool InMemOnly = false);
extern LString HeadersFromStream(LStreamI *Msg);
extern LString CreateMboxHeader(LDataI *Object);

#endif
