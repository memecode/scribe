#ifndef _SPELL_CHECK_H_
#define _SPELL_CHECK_H_

#include "lgi/common/TextView3.h"
#include "lgi/common/OptionsFile.h"
#include "lgi/common/ThreadEvent.h"

/*
class SpellErrorStyle : public LTextView3::LStyle
{
public:
	LString Word;
	LString::Array Options;

	SpellErrorStyle(LString word);
	~SpellErrorStyle();

	bool OnMenu(LSubMenu *m);
	void OnMenuClick(int i);
};

class SpellThread : public LThread, public LMutex
{
	GThreadEvent Event;

public:
	struct SpellStyles
	{
		/// Window that requested the spell check
		LViewI *Owner;
		
		/// The position in the text body of the word, or -1 to change the language
		int Start;
		
		/// Length of the word to check
		int Len;

		/// The string of text to check		
		LAutoWString Txt;

		int End() { return Start + Len; }

		// Styles
		LArray<SpellErrorStyle*> Errors;

		SpellStyles(LViewI *owner)
		{
			Owner = owner;
			Start = -1;
			Len = 0;
		}
		
		~SpellStyles()
		{
		    Errors.DeleteObjects();
		}
	};

protected:
	bool Loop;
	bool DoInstall;
	ScribePlugin_General *Speller;
	LAutoPtr<ScribePlugin_SpellChecker> Spell;
	LArray<SpellStyles*> In, Out;
	LArray<LViewI*> DeletedOwners;

public:
	SpellThread(ScribePlugin_General *spell);
	~SpellThread();

	LAutoPtr<SpellStyles> Get();
	void Put(LViewI *owner, int Start, int Len, LAutoWString Txt);
	void OnDelete(LViewI *owner);
	void InstallDictionary();
	int Main();
};
*/

extern char16 SpellDelim[];
// extern ScribePlugin_General *CreateSpellChecker(LViewI *App, SpellCheckParams *Params);

#endif