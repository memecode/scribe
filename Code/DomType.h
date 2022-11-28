#ifndef _SCRIBE_DOM_TYPE_H_
#define _SCRIBE_DOM_TYPE_H_

// Declare the DOM fields up front.
#undef _
#define _(name) Sd##name,
enum ScribeDomType
{
	SdNone,
	#include "DomTypeValues.h"
	SdMax
};
#undef _

ScribeExtern ScribeDomType StrToDom(const char *s);
ScribeExtern const char *DomToStr(ScribeDomType t);


#endif