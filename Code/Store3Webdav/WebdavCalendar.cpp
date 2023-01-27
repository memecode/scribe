#include "Scribe.h"
#include "CalendarView.h"
#include "lgi/common/Base64.h"
#include "lgi/common/XmlTree.h"
#include "lgi/common/vCard-vCal.h"
#include "WebdavStore.h"
#include "WebdavStorePriv.h"

/////////////////////////////////////////////////////////////////////////////////////
WebdavCalendar::WebdavCalendar(WebdavStore *store, WebdavEvent *e) : WebdavObj(store)
{
	if (e)
	{
		Href = e->Href;
		vCal = e->Data;
	}

	Colour = -1;

	LMemStream m(vCal.Get(), vCal.Length(), false);
	VCal convert;
	Converted = convert.Import(this, &m);
}

void WebdavCalendar::FireOnChange(int Fld)
{
	LArray<LDataI*> a;
	a.Add(this);
	Store->App->SetContext(_FL);
	Store->App->OnChange(a, Fld);
}

bool WebdavCalendar::ConvertToText()
{
	LStringPipe p;
	VCal convert;
	Converted = convert.Export(this, &p);
	if (Converted)
		vCal = p.NewGStr();
	else
		vCal.Empty();

	LgiTrace("vCal=%s\n", vCal.Get());
	return Converted;
}

Store3Status WebdavCalendar::Save(LDataI *parent)
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
		if (Parent->Thread)
		{
			if (!Href)
				Href = Parent->AllocateAddress();

			Ret = Parent->Thread->Save(Href, vCal);
			if (StoreStatus != Ret)
			{
				StoreStatus = Ret;
				FireOnChange(FIELD_STATUS);
			}
		}
		else
			LAssert(!"No thread?");
	}
	else LAssert(!"vCal export failed.");
		
	return Ret;
}
	
Store3Status WebdavCalendar::Delete(bool ToTrash)
{
	Store3Status Status = Store3Error;

	// Now save the vCal object to the WebDav server...
	if (Parent && Parent->Thread)
		Status = Parent->Thread->Delete(Href);
	else
		LAssert(!"No thread?");
		
	return Status;
}

LAutoStreamI WebdavCalendar::GetStream(const char *file, int line)
{
	LAutoStreamI a;
	if (ConvertToText())
		a.Reset(new LMemStream(vCal.Get(), vCal.Length(), true));
	return a;
}

bool WebdavCalendar::CopyProps(LDataPropI &p)
{
	#define _(f,v) SetDate(f, p.GetDate(f));
	WebdavCalendarDates()
	#undef _
	#define _(f,v) SetInt(f, p.GetInt(f));
	WebdavCalendarInts()
	#undef _
	#define _(f,v) SetStr(f, p.GetStr(f));
	WebdavCalendarStrings()
	#undef _

	return true;
}

const char *WebdavCalendar::GetStr(int id)
{
	switch (id)
	{
		#define _(f,v) case f: return v;
		WebdavCalendarStrings()
		#undef _
	}

	return NULL;
}
	
Store3Status WebdavCalendar::SetStr(int id, const char *str)
{
	switch (id)
	{
		#define _(f,v) case f: v = str; break;
		WebdavCalendarStrings()
		#undef _
		default:
		{
			LAssert(!"Impl me.");
			return Store3Error;
		}
	}

	return Store3Success;
}

int64 WebdavCalendar::GetInt(int id)
{
	switch (id)
	{
		#define _(f,v) case f: return v;
		WebdavCalendarInts()
		#undef _
		case FIELD_STORE_TYPE:
			return Store3Webdav;
		default:
		{
			LAssert(!"Impl me.");
			return Store3Error;
		}
	}

	return Store3Success;
}
	
Store3Status WebdavCalendar::SetInt(int id, int64 i)
{
	switch (id)
	{
		#define _(f,v) case f: v = i; break;
		WebdavCalendarInts()
		#undef _
		default:
		{
			LAssert(!"Impl me.");
			return Store3Error;
		}
	}

	return Store3Success;
}

const LDateTime *WebdavCalendar::GetDate(int id)
{
	switch (id)
	{
		#define _(f,v) case f: return &v;
		WebdavCalendarDates()
		#undef _
	}

	LAssert(!"Impl me.");
	return NULL;
}
	
Store3Status WebdavCalendar::SetDate(int id, const LDateTime *i)
{
	if (!i) return Store3Error;

	switch (id)
	{
		#define _(f,v) case f: v = *i; break;
		WebdavCalendarDates()
		#undef _
		default:
		{
			LAssert(!"Impl me.");
			return Store3Error;
		}
	}

	return Store3Success;
}

