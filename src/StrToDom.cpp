#include "Scribe.h"

// Static tables: don't require locking.
static LHashTbl<ConstStrKey<char,false>, ScribeDomType, true> Scribe_StrToDom(0, SdNone);
static LHashTbl<IntKey<int,SdNone>, const char *, true> Scribe_DomToStr;

void InitStrToDom()
{
	if (Scribe_StrToDom.Length() == 0)
	{
		// If this asserts it's likely you have a duplicate value in DomTypeValues.h
		#undef _
		#define _(name) LAssert(Scribe_StrToDom.Find(#name) == SdNone); \
						Scribe_StrToDom.Add(#name, Sd##name); \
						LAssert(Scribe_StrToDom.Find(#name) == Sd##name); \
						\
						LAssert(Scribe_DomToStr.Find(Sd##name) == NULL); \
						Scribe_DomToStr.Add(Sd##name, #name); \
						LAssert(Scribe_DomToStr.Find(Sd##name) != NULL);
		#include "DomTypeValues.h"
		#undef _
	}
}

void FreeStrToDom()
{
	Scribe_StrToDom.Empty(true);
	Scribe_DomToStr.Empty(true);
}

ScribeDomType StrToDom(const char *s)
{
	ScribeDomType d = Scribe_StrToDom.Find(s);
	return d;
}

const char *DomToStr(ScribeDomType d)
{
	const char *s = Scribe_DomToStr.Find(d);
	return s;
}

const char *Store3ItemTypeName(Store3ItemTypes t)
{
	switch (t)
	{
	case MAGIC_NONE:		return "MAGIC_NONE";
	case MAGIC_BASE:		return "MAGIC_BASE";
	case MAGIC_MAIL:		return "MAGIC_MAIL";
	case MAGIC_CONTACT:		return "MAGIC_CONTACT";
		// case MAGIC_FOLDER:	return "MAGIC_FOLDER";
	case MAGIC_MAILBOX:		return "MAGIC_MAILBOX";
	case MAGIC_ATTACHMENT:	return "MAGIC_ATTACHMENT";
	case MAGIC_ANY:			return "MAGIC_ANY";
	case MAGIC_FILTER:		return "MAGIC_FILTER";
	case MAGIC_FOLDER:		return "MAGIC_FOLDER";
	case MAGIC_CONDITION:	return "MAGIC_CONDITION";
	case MAGIC_ACTION:		return "MAGIC_ACTION";
	case MAGIC_CALENDAR:	return "MAGIC_CALENDAR";
	case MAGIC_ATTENDEE:	return "MAGIC_ATTENDEE";
	case MAGIC_GROUP:		return "MAGIC_GROUP";
	default:
	LAssert(0);
	break;
	}

	return "(error)";
}
