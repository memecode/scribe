#include "Scribe.h"

static LHashTbl<ConstStrKey<char,false>, ScribeDomType> Scribe_StrToDom(0, SdNone);
static LHashTbl<IntKey<int,SdNone>, const char *> Scribe_DomToStr;

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

