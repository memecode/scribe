#include "Scribe.h"
#include "CalendarView.h"
#include "lgi/common/Base64.h"
#include "lgi/common/XmlTree.h"
#include "lgi/common/vCard-vCal.h"
#include "WebdavStore.h"
#include "WebdavStorePriv.h"

/////////////////////////////////////////////////////////////////////////////////////
WebdavContact::WebdavContact(WebdavStore *store, WebdavEvent *e) : WebdavObj(store)
{
	if (e)
	{
		Href = e->Href;
		vCard = e->Data;
	}

	LMemStream m(vCard.Get(), vCard.Length(), false);
	VCard convert;
	Converted = convert.Import(this, &m);
}

void WebdavContact::FireOnChange(int Fld)
{
	LArray<LDataI*> a;
	a.Add(this);
	Store->App->SetContext(_FL);
	Store->App->OnChange(a, Fld);
}

bool WebdavContact::ConvertToText()
{
	LStringPipe p;
	VCard convert;
	Converted = convert.Export(this, &p);
	if (Converted)
		vCard = p.NewLStr();
	else
		vCard.Empty();

	LgiTrace("vCard=%s\n", vCard.Get());
	return Converted;
}

Store3Status WebdavContact::Save(LDataI *parent)
{
	Store3Status Ret = Store3Error;

	// Convert the object to text...
	if (ConvertToText())
	{
		if (!Parent)
		{
			Parent = dynamic_cast<WebdavFolder*>(parent);
			if (Parent)
			{
				LAssert(Parent->Items.IndexOf(this) < 0);
				Parent->Items.Insert(this);
			}
		}
		if (!Parent)
			return Ret;

		// Now save the vCal object to the WebDav server...
		if (Parent && Parent->Thread)
		{
			if (!Href)
				Href = Parent->AllocateAddress();

			Ret = Parent->Thread->Save(Href, vCard);
			if (Status != Ret)
			{
				Status = Ret;
				FireOnChange(FIELD_STATUS);
			}
		}
		else
			LAssert(!"No thread?");
	}
	else LAssert(!"vCal export failed.");
		
	return Ret;
}
	
Store3Status WebdavContact::Delete(bool ToTrash)
{
	Store3Status Status = Store3Error;

	// Now save the vCal object to the WebDav server...
	if (Parent && Parent->Thread)
		Status = Parent->Thread->Delete(Href);
	else
		LAssert(!"No thread?");
		
	return Status;
}

LAutoStreamI WebdavContact::GetStream(const char *file, int line)
{
	LAutoStreamI a;
	if (ConvertToText())
		a.Reset(new LMemStream(vCard.Get(), vCard.Length(), true));
	return a;
}

bool WebdavContact::CopyProps(LDataPropI &p)
{
	#define _(f,v) SetDate(f, p.GetDate(f));
	WebdavContactDates()
	#undef _
	#define _(f,v) SetInt(f, p.GetInt(f));
	WebdavContactInts()
	#undef _
	#define _(f,v) SetStr(f, p.GetStr(f));
	WebdavContactStrings()
	#undef _

	return true;
}

const char *WebdavContact::GetStr(int id)
{
	switch (id)
	{
		#define _(f,v) case f: return v;
		WebdavContactStrings()
		#undef _
	}

	return NULL;
}
	
Store3Status WebdavContact::SetStr(int id, const char *str)
{
	switch (id)
	{
		#define _(f,v) case f: v = str; break;
		WebdavContactStrings()
		#undef _
		default:
		{
			LAssert(!"Impl me.");
			return Store3Error;
		}
	}

	return Store3Success;
}

const LVariant *WebdavContact::GetVar(int id)
{
	switch (id)
	{
		#define _(f,v) case f: return &v;
		WebdavContactVariants()
		#undef _
		default:
		{
			LAssert(!"Impl me.");
			break;
		}
	}

	return NULL;
}

Store3Status WebdavContact::SetVar(int id, LVariant *i)
{
	switch (id)
	{
		#define _(f,v) case f: if (i) v = *i; else v.Empty(); break;
		WebdavContactVariants()
		#undef _
		default:
		{
			LAssert(!"Impl me.");
			return Store3Error;
		}
	}

	return Store3Success;
}

int64 WebdavContact::GetInt(int id)
{
	switch (id)
	{
		#define _(f,v) case f: return v;
		WebdavContactInts()
		#undef _
		case FIELD_STORE_TYPE:
			return Store3Webdav;
		default:
			break;
	}
	
	return -1;
}
	
Store3Status WebdavContact::SetInt(int id, int64 i)
{
	switch (id)
	{
		#define _(f,v) case f: v = i; break;
		WebdavContactInts()
		#undef _
		default:
		{
			LAssert(!"Impl me.");
			return Store3Error;
		}
	}

	return Store3Success;
}

const LDateTime *WebdavContact::GetDate(int id)
{
	switch (id)
	{
		#define _(f,v) case f: return &v;
		WebdavContactDates()
		#undef _
	}

	LAssert(!"Impl me.");
	return NULL;
}
	
Store3Status WebdavContact::SetDate(int id, const LDateTime *i)
{
	if (!i) return Store3Error;

	switch (id)
	{
		#define _(f,v) case f: v = *i; break;
		WebdavContactDates()
		#undef _
		default:
		{
			LAssert(!"Impl me.");
			return Store3Error;
		}
	}

	return Store3Success;
}

