#include "Lgi.h"
#include "LCss.h"
#include "ScribePlugin.h"
#include "SpellCheck.h"
#include "ScribeDefs.h"
#include "resdefs.h"
#include "LgiRes.h"

///////////////////////////////////////////////////////////////////////////////////////////////
SpellThread::SpellThread(ScribePlugin_General *spell) : LThread("SpellThread"), Event("SpellThreadEvent")
{
	DoInstall = false;
	Loop = true;
	Speller = spell;
	Spell.Reset(new ScribePlugin_SpellChecker(Speller));

	Run();
}

SpellThread::~SpellThread()
{
	Loop = false;
	Event.Signal();
	while (!IsExited())
	{
		LSleep(1);
	}

	In.DeleteObjects();	
	Out.DeleteObjects();	
}

LAutoPtr<SpellThread::SpellStyles> SpellThread::Get()
{
	LAutoPtr<SpellThread::SpellStyles> r;
	if (Lock(_FL))
	{
		if (Out.Length())
		{
			r.Reset(Out[0]);
			Out.DeleteAt(0, true);
		}
		Unlock();
	}
	return r;
}

void SpellThread::Put(LViewI *owner, int Start, int Len, LAutoWString Txt)
{
	LAssert(Len > 0);

	SpellStyles *s = new SpellStyles(owner);
	s->Start = Start;
	s->Len = Len;
	s->Txt = Txt;
	if (Lock(_FL))
	{
		In.Add(s);
		Unlock();
	}
	else DeleteObj(s);
	
	Event.Signal();
}

void SpellThread::OnDelete(LViewI *owner)
{
	if (Lock(_FL))
	{
		DeletedOwners.Add(owner);
		Unlock();
	}
}

void SpellThread::InstallDictionary()
{
	DoInstall = true;
	Event.Signal();
}

int SpellThread::Main()
{
	if (!Spell)
		return -1;

	while (Loop)
	{
		GThreadEvent::WaitStatus s = Event.Wait();
		if (s != GThreadEvent::WaitSignaled)
			return -1;
		
		if (DoInstall)
		{
			Spell->DoInstall();
			DoInstall = false;
		}

		while (true)
		{
			LAutoPtr<SpellStyles> Work;
			if (Lock(_FL))
			{
				if (In.Length())
				{
					Work.Reset(In[0]);
					In.DeleteAt(0, true);
				}

				Unlock();
			}
			
			if (!Work)
				break;

			char16 *e = 0;
			for (char16 *s = Work->Txt; Loop && s && *s; s = *e ? e + 1 : e)
			{
				// Skip leading delimiters
				while (*s && (StrchrW(SpellDelim, *s) || *s == '\'') ) s++;

				// Scan over the word
				for (e = s; *e && !StrchrW(SpellDelim, *e); e++);

				// Move back over trailing quote
				if (*e == '\'') e--;

				size_t Len = e - s;

				if (Len > 0 && !IsDigit(*s))
				{
					List<char> Options;

					LAutoString utf(WideToUtf8(s, Len));
					
					LAssert(Spell->IsOk());
					if (!Spell->IsOk())
						break;
					
					if (!Spell->CheckWord(utf, &Options, false, 0))
					{
						SpellErrorStyle *Err = new SpellErrorStyle(Spell, utf);
						if (Err)
						{
							Err->Start = Work->Start + (int)(s - Work->Txt);
							Err->Len = (int)Len;
							for (char *c=Options.First(); c; c=Options.Next())
							{
								Err->Options.Add(c);
							}

							Work->Errors.Add(Err);
						}
					}
				}
			}

			if (Lock(_FL))
			{
				if (!DeletedOwners.HasItem(Work->Owner))
				{
					LViewI *v = Work->Owner;
					if (v)
					{
						Out.Add(Work.Release());
						v->PostEvent(M_SCRIBE_SPELL);
					}
				}
				for (unsigned i=0; i<DeletedOwners.Length(); i++)
				{
					for (unsigned n=0; n<In.Length(); n++)
					{
						if (In[n]->Owner == DeletedOwners[i])
						{
							DeleteObj(In[n]);
							In.DeleteAt(n--, true);
						}
					}
					for (unsigned n=0; n<Out.Length(); n++)
					{
						if (Out[n]->Owner == DeletedOwners[i])
						{
							DeleteObj(Out[n]);
							Out.DeleteAt(n--, true);
						}
					}
				}
				DeletedOwners.Length(0);

				Unlock();
			}
		}
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////
char16 SpellDelim[] =
{
	' ', '\t', '\r', '\n', ',', ',', '.', ':', ';',
	'{', '}', '[', ']', '!', '@', '#', '$', '%', '^', '&', '*',
	'(', ')', '_', '-', '+', '=', '|', '\\', '/', '?', '\"',
	0
};

SpellErrorStyle::SpellErrorStyle(ScribePlugin_SpellChecker *spell, LAutoString word) :
	LTextView3::LStyle(1)
{
	Spell = spell;
	Word = word;
	Decor = DecorSquiggle;
	DecorColour.Rgb(255, 0, 0);		
}

SpellErrorStyle::~SpellErrorStyle()
{
	Options.DeleteArrays();
}

bool SpellErrorStyle::OnMenu(GSubMenu *m)
{
	if (Options.Length())
	{
		for (unsigned i=0; i<Options.Length() && i<10; i++)
		{
			m->AppendItem(Options[i], 100 + i, true);
		}

		m->AppendSeparator();
	}

	char Buf[256];
	sprintf_s(Buf, sizeof(Buf), LLoadString(IDS_ADD_TO_DICTIONARY, "Add '%s' to dictionary"), Word.Get());
	m->AppendItem(Buf, 1, true);
	return true;
}

void SpellErrorStyle::OnMenuClick(int i)
{
	if (i == 1)
	{
		// Add to dictionary...
		if (!Spell->AddToDictionary(Word))
			LgiMsg(View, LLoadString(IDS_ERROR_CANT_ADD_DICT), LAppInst->AppWnd?LAppInst->AppWnd->Name():(char*)"SpellCheck");
	}
	else if (i >= 100 && i < 100 + (int)Options.Length())
	{
		// Change spelling..
		char *Replace = Options[i - 100];
		if (Replace)
		{
			char16 *w = Utf8ToWide(Replace);
			if (w)
			{
				int NewLen = StrlenW(w);
				if (NewLen > Len)
				{
					// Bigger...
					memcpy(View->NameW() + Start, w, Len * sizeof(char16));
					View->Insert(Start + Len, w + Len, NewLen - Len);
				}
				else if (NewLen < Len)
				{
					// Smaller...
					memcpy(View->NameW() + Start, w, NewLen * sizeof(char16));
					View->Delete(Start + NewLen, Len - NewLen);
				}
				else
				{
					// Just copy...
					memcpy(View->NameW() + Start, w, Len * sizeof(char16));
					RefreshLayout(Start, Len);
				}

				DeleteArray(w);
			}
		}
	}
}

