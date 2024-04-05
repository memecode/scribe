#include <functional>
#include "Scribe.h"
#include "resdefs.h"
#include "BayesianFilter.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/SpellCheck.h"

#define SECONDS(n)				((n) * 1000)
#define TIMEOUT_BAYES_LOAD		SECONDS(10)
#define TIMEOUT_SPELL_CHECK		SECONDS(3)
#define TIMEOUT_UPDATE_REBUILD	SECONDS(2)
#define TIMEOUT_BAYES_IDLE		(50) // ms, out of 100ms idle timer.

#define WHITELIST_MY_EMAIL		0
#define WHITELIST_CONTACTS		0
#define STORE_SIZE				16
#define WORD_INTEREST_CENTER	0.5

static const char HamWordsFile[]  = "hamwords.idx";
static const char SpamWordsFile[] = "spamwords.idx";
static const char WhiteListFile[] = "whitelist.idx";

void ProcessWords(LString Words, std::function<void(const char*)> Callback)
{
	char *end = Words.Get() + Words.Length();

	if (!Callback)
	{
		LAssert(0);
		return;
	}

	for (char *s = Words; s < end; )
	{
		char *e = s;
		while (*e && *e != ' ' && e < end)
			e++;
		if (*e == ' ')
			*e = 0;
		Callback(s);
		s = e + 1;
	}
}

double WordInterest(double d)
{
	d -= WORD_INTEREST_CENTER;
	if (d < 0) d *= -1;
	return d;
}

class Token
{
public:
	LString Word;
	double Prob = 0.0;
	double Interest = 0.0;
};

class TokenStore
{
public:
	constexpr static int StoreSize = STORE_SIZE;
	int Used;
	Token *Tok[StoreSize];
	
	TokenStore()
	{
		Used = 0;
		ZeroObj(Tok);
	}

	~TokenStore()
	{
		for (int i=0; i<CountOf(Tok); i++)
			DeleteObj(Tok[i]);
	}
	
	double Min()
	{
		if (Used > 0)
			return Tok[Used-1]->Interest;
		
		return 0;
	}
	
	double Prob(LStream *Log)
	{
		int i;

		if (Log)
		{
			Log->Print("%s\n", LLoadString(IDS_SPAM_PROB));
			for (i=0; i<Used; i++)
			{
				Log->Print("\t[%i] '%s'=%f\n", i, Tok[i]->Word.Get(), Tok[i]->Prob);
			}
		}
		
		//       ab
		//-------------------
		//ab + (1 - a)(1 - b)
		
		if (Used == 0)
		    return 0.5;

		double top, bottom;
		
		top = Tok[0]->Prob;
		bottom = 1 - Tok[0]->Prob;
		for (i=1; i<Used; i++)
		{
			top *= Tok[i]->Prob;
			bottom *= 1 - Tok[i]->Prob;
		}
		
		double Result = top / (top + bottom);
			
		if (Log)
			Log->Print("\nResult=%f\n", Result);
		
		return Result;
		
	}
	
	bool CanInsert(double i)
	{
		return Used < CountOf(Tok) || i >= Min();
	}
	
	bool Insert(Token *t)
	{
		bool Status = false;
		
		if (t)
		{
			for (int i=0; i<Used; i++)
			{
				if (_stricmp(Tok[i]->Word, t->Word) == 0)
				{
					Status = true;
					DeleteObj(t);
					goto End;
				}
				else if (t->Interest > Tok[i]->Interest)
				{
					if (Used >= CountOf(Tok))
					{
						DeleteObj(Tok[Used-1]);
						memmove(Tok + i + 1, Tok + i, sizeof(Tok[0]) * (Used - i - 1));
					}
					else
					{
						memmove(Tok + i + 1, Tok + i, sizeof(Tok[0]) * (Used - i));
						Used++;
					}

					Tok[i] = t;
					
					Status = true;
					goto End;
				}
			}

			if (Used < CountOf(Tok))
			{
				Tok[Used++] = t;
					
				Status = true;
				goto End;
			}
			else
			{
				DeleteObj(t);
			}
		}
		
		End:
		return Status;
	}
};

class BayesianThread :
	public LThread,
	public LMutex,
	public LEventTargetI,
	public LMappedEventSink
{
public:
	enum ThreadState
	{
		BayesLoading,
		BayesReady,
		BayesExiting,
	};

	struct Build
	{
	    /// Empty all the word databases first... otherwise append new words on top of old ones
	    bool ResetDb;
	
		LHashTbl<ConstStrKeyPool<char,false>,int> WhiteList;

		int HamEmailCount;
		LHashTbl<ConstStrKeyPool<char,false>,int> HamWords;

		int SpamEmailCount;
		LHashTbl<ConstStrKeyPool<char,false>,int> SpamWords;
		
		Build(bool Reset) :
		    WhiteList(0, -1),
		    HamWords(0, -1),
		    SpamWords(0, -1)
		{
		    ResetDb = Reset;
			HamEmailCount = 0;
			SpamEmailCount = 0;

			HamWords.SetMaxSize(HamWords.Unlimited);
			SpamWords.SetMaxSize(SpamWords.Unlimited);
		}

		void InsertWhiteList(const char *Word)
		{
			int c = WhiteList.Find(Word);
			WhiteList.Add(Word, c < 0 ? 1 : c + 1);
		}

		void InsertHamWords(const char *Word)
		{
			int c = HamWords.Find(Word);
			HamWords.Add(Word, c < 0 ? 1 : c + 1);
		}

		void InsertSpamWords(const char *Word)
		{
			int c = SpamWords.Find(Word);
			SpamWords.Add(Word, c < 0 ? 1 : c + 1);
		}
	};
	
	/// This is used when the GUI thread requests an email to be
	/// tested by the Bayesian Classifier. Initially the GUI thread
	/// fills out this structure and passes it over to the worker
	/// thread and it then does the calculation and passes it back 
	/// to the GUI thread for actioning.
	struct Test
	{
	    bool Analyse = false;
	    LStringPipe Log;
	    LString FromAddr;
	    LString MsgRef;
    	LString Words;
    	double Score = 0.0;
		bool WhiteListed = false;
	};

	/// This is a change of classification container for
	/// when the email changes from ham to spam or the reverse.
	struct Change
	{
    	LString Words;
		LString Str;
    	ScribeMailType OldType = BayesMailUnknown;
    	ScribeMailType NewType = BayesMailUnknown;
		bool RemoveWhite = false;
		bool IncrementWhite = false;
	};

private:
	ScribeWnd *App;
	LAutoPtr<LWordStore> WhiteList;
	LAutoPtr<LWordStore> Ham;
	LAutoPtr<LWordStore> Spam;
	ThreadState State;
	LArray< LAutoPtr<Build> > Work;
	LArray< LAutoPtr<Test> > Tests;
	LArray< LAutoPtr<Test> > Results;
	LArray< LAutoPtr<Change> > Changes;
	LAutoPtr<TokenStore> Tokens;
	uint64_t CheckTextTs = 0;
	LAutoPtr<LSpellCheck::CheckText> CheckText;

	void Empty()
	{
		WhiteList.Reset(new LWordStore);
		Spam.Reset(new LWordStore);
		Ham.Reset(new LWordStore);

		#define SetListFile(list, file) \
			if (!list->GetFile()) \
			{ \
				if (auto s = FindWordDb(file)) \
				{ \
					list->SetFile(s); \
				} \
			}

		SetListFile(WhiteList, WhiteListFile);
		SetListFile(Spam, SpamWordsFile);
		SetListFile(Ham, HamWordsFile);

		// empty the word lists
		WhiteList->Empty();
		WhiteList->Serialize(0, true);
		Spam->Empty();
		Spam->Serialize(0, true);
		Ham->Empty();
		Ham->Serialize(0, true);
	}

public:
	BayesianThread(ScribeWnd *app) :
		LThread("BayesianThread.Thread"),
		LMutex ("BayesianThread.Mutex")
	{
		App = app;
		State = BayesLoading;		
		Run();
	}

	~BayesianThread()
	{
		State = BayesExiting;
		int64 Start = LCurrentTime();
		while (!IsExited())
		{
			LSleep(20);
			if (LCurrentTime()-Start > 5000)
			{
				Terminate();
				break;
			}
		}
	}

	LMessage::Result OnEvent(LMessage *Msg) override
	{
		switch (Msg->Msg())
		{
			case M_CHECK_TEXT:
				CheckTextTs = LCurrentTime();
				CheckText.Reset((LSpellCheck::CheckText*)Msg->A());
				break;
		}

		return 0;
	}

	bool PostEvent(int Cmd, LMessage::Param a = 0, LMessage::Param b = 0, int64_t TimeoutMs = -1) override
	{
		LMessage m(Cmd, a, b);
		OnEvent(&m);
		return true;
	}
	
	void Add(LAutoPtr<Build> b)
	{
		if (Lock(_FL))
		{
		    Work.New() = b;
			Unlock();
		}
	}

	void Add(LAutoPtr<Test> t)
	{
		if (Lock(_FL))
		{
		    Tests.New() = t;
			Unlock();
		}
	}

	void Add(LAutoPtr<Change> c)
	{
		if (Lock(_FL))
		{
		    Changes.New() = c;
			Unlock();
		}
	}
	
	bool GetResults(LArray< LAutoPtr<Test> > &results)
	{
	    if (Lock(_FL))
	    {
	        results = Results;
	        Unlock();
	    }
	    
	    Results.Length(0);
	    return results.Length() > 0;
	}
	
	ThreadState GetState()
	{
		return State;
	}

	void SetStore(LAutoPtr<LWordStore> &s, LWordStore *ws)
	{
		if (ws && Lock(_FL))
		{
			s.Reset(ws);
			Unlock();
		}
	}

	LString FindWordDb(const char *Name)
	{
		LString OptPath;
		auto Opts = App->GetOptions();
		
		// Look in the same folder as the options file:
		if (Opts && Opts->GetFile())
		{
			LFile::Path p(Opts->GetFile());
			p--;
			OptPath = p.GetFull();
			p += Name;
			if (p.IsFile())
				return p.GetFull();
		}

		// Check the install folder too:
		LFile::Path p(LSP_APP_INSTALL);
		p += Name;
		if (p.IsFile())
			return p.GetFull();

		// No existing file found, so create a path using the options location:
		p = OptPath;
		p += Name;
		return p.GetFull();
	}

	void OnCheckText(LSpellCheck::CheckText *Ct)
	{
		auto p = Ct->Errors.Length() ? 0.3 : 0.7;
		auto i = WordInterest(p);
		if (Tokens->CanInsert(i))
		{
			Token *t = new Token;
			if (t)
			{
				t->Word = Ct->Text;
				t->Prob = p;
				t->Interest = i;
				Tokens->Insert(t);
			}
		}
	}
	
    double IsSpam(Test *t)
    {
		// Check the auto white list
		if (WhiteList)
		{
			long Count = WhiteList->GetWordCount(t->FromAddr);
			if (Count > 0)
			{
				// It's from someone we've accepted mail from before
				if (t->Analyse) t->Log.Print("%s\n", LLoadString(IDS_IS_WHITELIST));
				t->WhiteListed = true;
				return 0.0;
			}
		}

		bool Analyse = t->Analyse;
		LAutoPtr<LSpellCheck> Spell(App->CreateSpellObject());
		Tokens.Reset(new TokenStore);
		
		if (!Ham || !Spam)
		{
			LgiTrace("%s:%i - No Ham/Spam DB loaded?\n", _FL);
			return 0.0;
		}
		
		ssize_t HamItems = Ham->Length();
		ssize_t SpamItems = Spam->Length();
		
		if (Analyse)
			LgiTrace("HamItems=" LPrintfSSizeT " SpamItems=" LPrintfSSizeT "\n");

		if (Spell)
		{
			Spell->Check(GetHandle(), t->Words, 0, t->Words.Length());

			auto Start = LCurrentTime();
			while (!CheckText) // Wait for the spell check to complete
			{
				if (LCurrentTime() - Start > TIMEOUT_SPELL_CHECK)
				{
					LgiTrace("%s:%i - Bayesian filter didn't get response from spell check, continuing without.\n", _FL);
					break;
				}
				LSleep(10);
			}
			if (CheckText)
				LgiTrace("%s:%i - SpellCheck took %ims\n", _FL, (int)(LCurrentTime()-CheckTextTs));
		}

		struct Entry
		{
			LString word;
			ssize_t spam = 0, ham = 0;
			bool spellErr = false;

			int Compare(Entry *e)
			{
				auto i = (spam+ham) - (e->spam+e->ham);
				if (i < 0) return -1;
				return i > 0 ? 1 : 0;
			}
		};

		LArray<Entry> entries;
		ProcessWords(t->Words, [this, t, &entries](auto w)
		{
			size_t nextErr = 0;
	
			Entry &e = entries.New();
			e.word = w;
			e.spam = Spam->GetWordCount(w);
			e.ham = Ham->GetWordCount(w);

			size_t Cur = w - t->Words.Get();
			// bool hasAt = Strchr(w, '@') != NULL;
			if (CheckText)
			{
				while (nextErr < CheckText->Errors.Length())
				{
					auto &errs = CheckText->Errors[nextErr];
					if (errs.Overlap(Cur))
					{
						e.spellErr = true;
						nextErr++;
						break;
					}
					if (errs.Start < (ssize_t)Cur)
						nextErr++;
					else
						break;
				}
			}
		});

		entries.Sort([](auto a, auto b)
		{
			return a->Compare(b);
		});

		for (auto &e: entries)
		{
			double s, p, i;

			if (e.spam && e.ham)
			{
				s = (double) e.spam / SpamItems;
				p =	s / ( ((double) e.ham / HamItems) + s);
			}
			else if (e.spam)
				p = 0.99;
			else if (e.ham)
				p = e.spellErr ? 0.6 : 0.01;
			else
				p = e.spellErr ? 0.88 : 0.4;

			i = WordInterest(p);

			if (Tokens->CanInsert(i))
			{
				Token *t = new Token;
				if (t)
				{
					t->Word = e.word;
					t->Prob = p;
					t->Interest = i;
					Tokens->Insert(t);
				}
			}

			if (Analyse)
				LgiTrace("	%s, " LPrintfSSizeT ", " LPrintfSSizeT ", %i, %g\n", e.word.Get(), e.spam, e.ham, e.spellErr, p);
		}

		auto result = Tokens->Prob(&t->Log);
		if (Analyse)
			LgiTrace("	result=%g\n", result);
		return result;
    }

    void ConvertHashToBtree(LWordStore *Ws,
                            LHashTbl<ConstStrKeyPool<char,false>,int> &Hash,
                            int EmailCount,
                            bool Append,
                            LStream *Debug = NULL)
    {
	    auto Items = Hash.Length();
	    int64 Start = LCurrentTime();

	    if (Debug)
		    Debug->Print("ConvertHashToBtree(%s, %i words, %i emails)\n", Ws->GetFile(), Items, EmailCount);

		Ws->Empty();
		
		for (auto i: Hash)
	    {
	        ssize_t Result;
	        
	        if (Append)
	        {
	            ssize_t Old = Ws->GetWordCount(i.key);
	            Result = Ws->SetWordCount(i.key, Old + i.value);
	        }
	        else
	        {
	            Result = Ws->SetWordCount(i.key, i.value);
	        }
		    
		    if (!Result)
		    {
			    if (Debug)
				    Debug->Print("SetWordCount(%s, %i) failed\n", i.key, i.value);
			    LAssert(0);
			    return;
		    }
	    }

	    if (EmailCount)
	    {
		    Ws->SetItems(EmailCount);
	    }

	    Hash.Empty();

	    LgiTrace("ConvertHashToBtree(%s) took %.1f sec, for " LPrintfInt64 " items.\n",
	    	Ws->GetFile(),
	    	((double)((int64)LCurrentTime()-Start))/1000.0,
	    	Items);
    }

	void ApplyChange(Change *c, LWordStore *Ws, bool Add)
	{
		bool status = false;

		if (!Ws || !c)
		{
			LgiTrace("%s:%i - Invalid param: %p, %p\n", _FL, c, Ws);
			return;
		}

		ProcessWords(c->Words, [this, Ws, Add, &status](auto w)
		{
			ssize_t c = Ws->GetWordCount(w);
			if (Add)
				status = Ws->SetWordCount(w, c + 1) > 0;
			else
				status = Ws->SetWordCount(w, c > 0 ? c - 1 : 0) > 0;
			LAssert(status);
		});
		
		ssize_t Total = Ws->GetItems();
		status = Ws->SetItems((int)Total + (Add ? 1 : -1));
		LAssert(status);
	}

	int Main() override
	{
		if (auto s = FindWordDb(HamWordsFile))
			SetStore(Ham, new LWordStore(s));
		if (auto s = FindWordDb(SpamWordsFile))
			SetStore(Spam, new LWordStore(s));
		if (auto s = FindWordDb(WhiteListFile))
			SetStore(WhiteList, new LWordStore(s));

		State = BayesReady;
	    bool Notify = false;
		while (State == BayesReady)
		{
		    LAutoPtr<Build> b;
		    LAutoPtr<Test> t;
		    LAutoPtr<Change> c;
		    if (Lock(_FL))
		    {
		        if (Work.Length())
		        {
		            b = Work[0];
		            Work.DeleteAt(0, true);
		        }
		        else if (Tests.Length())
		        {
		            t = Tests[0];
		            Tests.DeleteAt(0, true);
		        }
		        else if (Changes.Length())
		        {
					c = Changes[0];
					Changes.DeleteAt(0, true);
		        }
		        
		        Unlock();
		    }
		    
		    if (b)
		    {
		        if (b->ResetDb)
		            Empty();

                ConvertHashToBtree(Spam,        b->SpamWords,   b->SpamEmailCount,  b->ResetDb == false);
                ConvertHashToBtree(Ham,         b->HamWords,    b->HamEmailCount,   b->ResetDb == false);
                ConvertHashToBtree(WhiteList,   b->WhiteList,   0,                  b->ResetDb == false);
		    }
		    else if (t)
		    {
		        t->Score = IsSpam(t);
		        if (Lock(_FL))
		        {
		            Results.New() = t;
		            Notify = true;
		            Unlock();
		        }		        
		    }
		    else if (c)
		    {
				switch (c->OldType)
				{
					default:
						break;
					case BayesMailHam:
						ApplyChange(c, Ham, false);
						break;
					case BayesMailSpam:
						ApplyChange(c, Spam, false);
						break;
				}
				switch (c->NewType)
				{
					default:
						break;
					case BayesMailHam:
						ApplyChange(c, Ham, true);
						break;
					case BayesMailSpam:
						ApplyChange(c, Spam, true);
						break;
				}

				if (c->Str)
				{
					if (!WhiteList)
					{
						LgiTrace("Missing whitelist obj.\n");
					}
					else if (c->NewType == BayesMailSpam || c->RemoveWhite)
					{
						// Make sure the email address is not in the white list...
						WhiteList->DeleteWord(c->Str);
						LAssert(WhiteList->GetWordCount(c->Str) == 0);
					}
					else if (c->IncrementWhite)
					{
						auto i = WhiteList->GetWordCount(c->Str);
						WhiteList->SetWordCount(c->Str, i + 1);
					}
				}
		    }
		    else
		    {
		        // No work to do...		        
		        if (Notify)
		        {
		            App->PostEvent(M_SCRIBE_BAYES_RESULT);
		            Notify = false;		            
		        }
		        
		        LSleep(20);
		    }
		}
					
		return 0;
	}
};

struct BayesEvent
{
	Mail *m;
	bool Loading, Done;
	ScribeMailType OldType, NewType;
};

class BuildSpamDB
{
	int FolderLoads = 0;
	int MailLoads = 0;

public:
	ScribeWnd *App;
	BayesianFilter *Filter;
	LAutoPtr<LProgressDlg> Prog;
	LAutoPtr<LFile> Debug;

	// Processing part 1... load the folders:
	LArray<ScribeFolder*> Folders;

	// Processing part 2... convert the mail to words:
	struct BuildItem
	{
		Mail *m = NULL;
		bool loading = false;
		ScribeMailType type = BayesMailUnknown;

		void Set(Mail *mail, ScribeMailType Type)
		{
			m = mail;
			m->IncRef();
			type = Type;
			loading = false;
		}
	};
	LArray<BuildItem> Items;

	int HamCount = 0;
	int SpamCount = 0;
	int FalsePositives = 0;
	int FalseNegatives = 0;
	int LoadFailures = 0;

    LAutoPtr<BayesianThread::Build> b;

	BuildSpamDB(ScribeWnd *app);
	~BuildSpamDB();

	/// \returns true when the processing is finished
	bool Process();
	void ProcessMail(Mail *m, ScribeMailType type);
	void AbortProcess();

	void AddFolder(ScribeFolder *f)
	{
		Folders.Add(f);
	}

	bool IsCancelled()
	{
		return Prog ? Prog->IsCancelled() : true;
	}
};

class BayesianFilterPriv : public LMutex
{
	LAutoPtr<BayesianThread> Thread;

public:
    ScribeWnd *App;
	uint64_t Ts;

	uint64_t WorkStartTs = 0;
	LArray<BayesEvent> Work;
	LAutoPtr<LProgressDlg> Prog;
	LAutoPtr<BuildSpamDB> Build;

    BayesianFilterPriv(ScribeWnd *app) :
		LMutex("BayesianFilterPriv")
    {
		Ts = 0;
        App = app;
    }

    BayesianThread *GetThread()
    {
        if (!Thread)
            Thread.Reset(new BayesianThread(App));
        return Thread;
    }

	bool IsCancelled()
	{
		return Build ? Build->IsCancelled() : true;
	}
};

BuildSpamDB::BuildSpamDB(ScribeWnd *app) : App(app), Filter(app)
{
	App->OnFolderTask(Filter->d->GetThread(), true);

	b.Reset(new BayesianThread::Build(true));
	if (Prog.Reset(new LProgressDlg(App)))
		Prog->SetDescription("Scanning folders...");

	LVariant i;
	if (App->GetOptions()->GetValue(OPT_BayesDebug, i))
	{
		if (Debug.Reset(new LFile))
		{
			char s[MAX_PATH_LEN];
			LMakePath(s, sizeof(s), LGetExePath(), "Bayes.txt");
			if (Debug->Open(s, O_WRITE))
				Debug->SetSize(0);
			else
				LgiTrace("%s:%i - Couldn't open '%s'\n", _FL, s);
		}
	}
}

BuildSpamDB::~BuildSpamDB()
{
	// Save stats...
	LVariant i;
	auto opts = App->GetOptions();
	opts->SetValue(OPT_BayesHam, i = HamCount);
	opts->SetValue(OPT_BayesSpam, i = SpamCount);
	opts->SetValue(OPT_BayesFalsePositives, i = FalsePositives);
	opts->SetValue(OPT_BayesFalseNegitives, i = FalseNegatives);

	App->OnFolderTask(Filter->d->GetThread(), false);
}

void BuildSpamDB::AbortProcess()
{
	Folders.Length(0);
	Items.Length(0);
}

bool BuildSpamDB::Process()
{
	if (IsCancelled())
		AbortProcess();

	// This should execute for only a small time slice...
	if (Folders.Length() || FolderLoads)
	{
		if (!Folders.Length())
			return false; // Just wait for them...

		auto f = Folders[0];
		Folders.DeleteAt(0);

		if (f)
		{
			auto Path = f->GetPath();
			ScribeMailType Type = Filter->BayesTypeFromPath(Path);

			if (Debug)
				Debug->Print("%s:%i - add folder '%s', Type=%i\n", _FL, Path.Get(), Type);

			auto Parent = f->GetParent();

			if (Type != BayesMailUnknown && Parent)
			{
				FolderLoads++;
				f->WhenLoaded(_FL, [this, f, Type, Path](auto status)
				{
					LgiTrace("%s:%i - Scanning '%s' got %i items, FolderLoads=%i\n", _FL, Path.Get(), (int)f->Items.Length(), FolderLoads);
					for (auto i: f->Items)
					{
						auto m = i->IsMail();
						if (!m)
							continue;

						// Add item to the work queue...
						Items.New().Set(m, Type);
					}

					FolderLoads--;
					(*Prog)++;
				});

				// FIXME: does this need to do something on callback?
				f->LoadThings();
			}
			else
			{
				// LgiTrace("%s:%i - Unknown folder '%s'\n", _FL, Path.Get());
				(*Prog)++;
			}
		}

		if (Folders.Length() == 0 && FolderLoads == 0)
		{
			Prog->SetDescription("Processing mail...");
			Prog->SetRange(Items.Length());
			Prog->Value(0);
		}
		return false;
	}

	if (Items.Length() || MailLoads)
	{
		if (Items.Length() && MailLoads < 100)
		{
			auto Start = LCurrentTime();
			while (	(LCurrentTime() - Start) < TIMEOUT_BAYES_IDLE &&
					Items.Length())
			{	
				// Process mail items...
				auto &i = Items[0];
		
				// Operate on read mail only...
				auto flags = i.m->GetFlags();
				if (TestFlag(flags, MAIL_READ))
				{
					MailLoads++;

					// auto loaded = i.m->GetLoaded();

					i.m->WhenLoaded(_FL, [this, mail = i.m, type = i.type](auto status)
					{
						ProcessMail(mail, type);
						MailLoads--;
						(*Prog)++;
					});

					i.m->SetLoaded();
				}

				Items.DeleteAt(0);
			}
		}
		return false;
	}

	// We're done...
	return true;
}

void BuildSpamDB::ProcessMail(Mail *m, ScribeMailType Type)
{
	auto LoadState = m->GetLoaded();
	if (LoadState != Store3Loaded)
	{
		LAssert(!"Should only be called on loaded email...");
		return;
	}

	const char *email;
	if (Type == BayesMailHam &&
		(email = m->GetFromStr(FIELD_EMAIL)))
	{
		b->InsertWhiteList(email);
	}

	LString Words;
	auto Status = Filter->MakeMailWordList(m, Words);
	if (Status != Store3Success)
	{
		LAssert(!"MakeMailWordList failed.");
		return;
	}

	ProcessWords(Words, [this, Type](auto w)
	{
		if (Type == BayesMailSpam)
			b->InsertSpamWords(w);
		else if (Type == BayesMailHam)
			b->InsertHamWords(w);
		// else do nothing
	});

	auto flags = m->GetFlags();

	// Remove the bayes DB flags...
	flags &= ~(MAIL_HAM_DB|MAIL_SPAM_DB);

	if (Type == BayesMailSpam)
	{
		b->SpamEmailCount++;
		flags |= MAIL_SPAM_DB;
	}
	else if (Type == BayesMailHam)
	{
		b->HamEmailCount++;
		flags |= MAIL_HAM_DB;
	}

	if (flags & MAIL_BAYES_HAM) // originally classified as ham..
	{
		if (flags & MAIL_SPAM_DB) // but now is spam...
			FalseNegatives++;
		else if (Type == BayesMailHam)
			HamCount++;
	}
	else if (flags & MAIL_BAYES_SPAM) // originally classified as spam..
	{
		if (flags & MAIL_SPAM_DB)
			SpamCount++;
		else if (Type == BayesMailHam) // but now is ham...
			FalsePositives++;
	}

	// Update the object..
	m->SetFlags(flags);

	// Delete our reference...
	m->DecRef();
}

#ifdef _DEBUG
bool HashSerialize(LHashTbl<StrKeyPool<char>,uint32_t> &h, char *file, bool write)
{
	bool Status = false;

	struct Field
	{
		uint32_t Value;
		uint16_t Len;
		char Str[1];
	};

	LFile f;
	if (f.Open(file, write?O_WRITE:O_READ))
	{
		Field *fld = (Field*)malloc(64<<10);
		if (fld)
		{
			int header = 6;
			if (write)
			{
				f.SetSize(0);

				// char *key;
				// for (int i=h.First(&key); i; i=h.Next(&key))
				for (auto i : h)
				{
					fld->Value = (uint32_t)i.value;
					fld->Len = (uint16_t)strlen(i.key);
					memcpy(fld->Str, i.key, fld->Len + 1);

					Status = f.Write(fld, header + fld->Len) == (header + fld->Len);
				}
			}
			else
			{
				h.Empty();

				while (!f.Eof())
				{
					if (f.Read(fld, header) == header)
					{
						if (f.Read(fld->Str, fld->Len) == fld->Len)
						{
							fld->Str[fld->Len] = 0;
							Status = h.Add(fld->Str, fld->Value);
						}
						else break;
					}
					else break;
				}
			}

			free(fld);
		}
	}

	return Status;
}
#endif

BayesianFilter::BayesianFilter(ScribeWnd *app)
{
    d = new BayesianFilterPriv(app);
    App = app;
}

BayesianFilter::~BayesianFilter()
{
    DeleteObj(d);
}

void BayesianFilter::AddFolderToSpamDb(ScribeFolder *f)
{
	if (d->IsCancelled())
		return;

	d->Build->AddFolder(f);
	for (auto c = f->GetChildFolder(); c; c = c->GetNextFolder())
		AddFolderToSpamDb(c);
}

/*
static size_t FolderCount(ScribeFolder *f)
{
	ssize_t len = f->Length();
	auto items = f->GetItems();
	auto n = MAX(len, items);

	for (auto c = f->GetChildFolder(); c; c = c->GetNextFolder())
		n += FolderCount(c);	
	
	return n;
}
*/

void BayesianFilter::BuildStats()
{
	auto prob = App->GetFolder("/Mail3/Spam/Probably");
	auto inbox = App->GetFolder("/IMAP/INBOX");
	if (!prob || !inbox)
		return;

	prob->LoadThings();
	inbox->LoadThings();
}

bool BayesianFilter::BuildSpamDb()
{
	if (!d->GetThread())
		return false;

	// scan folders
	LVariant MoveTo;
	if (!App->GetOptions()->GetValue(OPT_BayesMoveTo, MoveTo))
		MoveTo = "/Spam/Probably";

	if (!d->Build.Reset(new BuildSpamDB(App)))
		return false;

	// Recurse over the folders
	for (auto &s: App->GetStorageFolders())
	{
		if (s.Root)
			AddFolderToSpamDb(s.Root);
	}
		
	for (auto a : *App->GetAccounts())
	{
		if (a->Receive.GetRootFolder())
			AddFolderToSpamDb(a->Receive.GetRootFolder());
	}

	d->Build->Prog->SetRange(d->Build->Folders.Length());
	return true;
}

#define IsCJK(c) \
	( \
		((c)>=0x4E00  && (c)<=0x9FFF) || \
		((c)>=0x3400  && (c)<=0x4DFF) || \
		((c)>=0x20000 && (c)<=0x2A6DF)|| \
		((c)>=0xF900  && (c)<=0xFAFF) || \
		((c)>=0x2F800 && (c)<=0x2FA1F)   \
	)

bool IsUriChar(int32 ch)
{
	if (!ch)
		return false;

	if (IsDigit(ch) || IsAlpha(ch))
		return true;

	if (strchr("-._~:/?#[]@!$&\'()*+,;%=", ch))
		return true;

	return false;
}

typedef LHashTbl<ConstStrKey<char,false>,bool> TokenMap;
void TokeniseText(const char *Source, bool *Lut, LString::Array &Blocks, TokenMap *Ignore = NULL)
{
	if (!Source || !Lut)
		return;

	char buf[16 << 10];
	ssize_t used = 0;
	auto oldWarn = LUtf8Ptr::Warn;
	LUtf8Ptr::Warn = false;

	auto emitBuf = [&Blocks, &used, &buf]() {
		if (used > 0)
			Blocks.New().Set(buf, used);
		used = 0;
	};

	for (auto *s = Source; *s;)
	{
		int32 ch;
		LUtf8Ptr start(s);

		// Skip non-word
		while (
				(ch = start)
				&&
				ch < 256
				&&
				!Lut[ch]
			)
		{
			start++;
		}
			
		// Seek end of word
		LUtf8Ptr end(start);
		while (
				(ch = end)
				&&
				(
					ch >= 256
					||
					Lut[ch]
				)
			)
		{
			if (end > start && IsCJK(ch))
				break;
			end++;
		}
			
		auto len = end - start;

		// Is the word something that looks like a URI?
		bool isUrl =	(len == 4 && !Strnicmp((const char*)start.GetPtr(), "http", 4)) ||
						(len == 5 && !Strnicmp((const char*)start.GetPtr(), "https", 5));
		if (isUrl)
		{
			// Seek to the end of the URI
			while ((ch = end)
					&&
					IsUriChar(ch)
					&&
					(end - start) < sizeof(buf) - 10)
				end++;
			isUrl = true;
			len = end - start;
		}

		if (len > 0)
		{
			ch = start;
			if (ch != '*' ||
				len != 10 ||
				Strnicmp((const char*)start.GetPtr(), "***SPAM***", len))
			{
				if (used + len >= sizeof(buf) - 2)
					emitBuf();

				auto word = buf + used;
				if (isUrl)
				{
					LUri uri(LString((char*)start.GetPtr(), len));
					if (uri.sHost)
					{
						memcpy(word, uri.sHost.Get(), uri.sHost.Length());
						used += uri.sHost.Length();
						buf[used++] = ' ';
					}
				}
				else
				{
					memcpy(word, start.GetPtr(), len);
					used += len;

					if (Ignore)
					{
						buf[used] = 0;
						if (Ignore->Find(buf+used-len))
							used -= len; // undo adding word
						else
							buf[used++] = ' '; // convert to space delimit
					}
					else
					{
						buf[used++] = ' ';
					}
				}
			}
		}
		else
		{
			if (ch)
				LgiTrace("%s:%i - Invalid utf-8, aborting parse...\n", _FL);
			break;
		}

		s = (const char*)end.GetPtr();
	}

	emitBuf();
	LUtf8Ptr::Warn = oldWarn;
}

Store3Status BayesianFilter::MakeMailWordList(Mail *m, LString &out)
{
	LString::Array Blocks;

	if (!m || !m->GetObject())
		return Store3Error;

	static bool Processing = false;
	if (!Processing)
	{
		Processing = true;

		// create word lut for deciding whether a char is part of a word or not
		bool Lut[256];
		ZeroObj(Lut);
		memset(Lut + 'a', true, 'z'-'a'+1);
		memset(Lut + 'A', true, 'Z'-'A'+1);
		Lut[(int)'-'] = true;
		// Lut[(int)'!'] = true;
		Lut[(int)'$'] = true;
		
		bool Email[256];
		memcpy(Email, Lut, sizeof(Email));
		Email[(int)'@'] = true;
		Email[(int)'.'] = true;
		
		memset(Lut + 0x80, true, 128);
		
		// create ignored words list
		LString::Array Temp;
		TokenMap Ignore;
		for (auto a: *App->GetAccounts())
		{
			auto s = a->Identity.Name();
			if (ValidStr(s.Str()))
				TokeniseText(s.Str(), Lut, Temp);
			s = a->Identity.Email();
			if (ValidStr(s.Str()))
				TokeniseText(s.Str(), Email, Temp);
		}
		ProcessWords(LString("").Join(Temp), [&Ignore](auto w)
		{
			Ignore.Add(w, true);
		});
		
		// process various parts of the email
		TokeniseText(m->GetSubject(), Lut, Blocks, &Ignore);
		TokeniseText(m->GetFromStr(FIELD_EMAIL), Email, Blocks, &Ignore);
		TokeniseText(m->GetFromStr(FIELD_NAME), Lut, Blocks, &Ignore);
		LVariant Body;

		Store3State Loaded = (Store3State)m->GetObject()->GetInt(FIELD_LOADED);
		if (Loaded < Store3Loaded)
		{
			m->GetBody();
			Loaded = (Store3State)m->GetObject()->GetInt(FIELD_LOADED);
			if (Loaded < Store3Loaded)
			{
				Processing = false;
				return Store3Delayed;
			}
			// else continue...
		}

		auto Req = m->GetValue("BodyAsText", Body);
		if (Req)
		{
			// auto id = m->GetMessageId();
			TokeniseText(Body.Str(), Lut, Blocks, &Ignore);
		}
		else
		{
			LString path = "(nullFolder)";
			auto fld = m->GetFolder();
			if (fld)
				path = fld->GetPath();
			LgiTrace("%s:%i - couldn't get body for %s/%s\n", _FL, path.Get(), m->GetMessageId());
			/* Technically not an error... body can be blank.
			Processing = false;
			return Store3Error;
			*/
		}
	
		out = LString("").Join(Blocks);
		Processing = false;
		return Store3Success;
	}
	else
	{
		LAssert(!"Recursion.");
		return Store3Error;
	}
}

Store3Status BayesianFilter::IsSpam(double &Result, Mail *m, bool Analyse)
{
    if (!m)
    {
        Result = 0.0;        
        return Store3Error;
    }

	Store3Status Status = Store3NotImpl;
    LAutoPtr<BayesianThread::Test> t(new BayesianThread::Test);
	const char *FromAddr;
	if ((FromAddr = m->GetFromStr(FIELD_EMAIL)))
	{
		#if WHITELIST_MY_EMAIL
	    // Check if from yourself...
	    if (App->IsMyEmail(FromAddr))
		    goto OnWhiteListed;
		#endif
	    
		// Check the user white list
		LVariant Wl;
		if (App->GetOptions()->GetValue(OPT_BayesUserWhiteList, Wl))
		{
			auto w = Wl.LStr().SplitDelimit("\r\n\t ");
			bool IsWhite = false;
			for (unsigned i=0; i<w.Length(); i++)
			{
				if (strchr(w[i], '?') || strchr(w[i], '*'))
				{
					if (MatchStr(w[i], FromAddr))
					{
						IsWhite = true;
						break;
					}
				}
				else
				{
					if (_stricmp(w[i], FromAddr) == 0)
					{
						IsWhite = true;
						break;
					}
				}
			}
			if (IsWhite)
			    goto OnWhiteListed;
		}

		#if WHITELIST_CONTACTS
	    // Check the contacts folder
		List<Contact> Contacts;
		App->GetContacts(Contacts);
		for (auto c: Contacts)
		{
			if (c->HasEmail(FromAddr))
			    goto OnWhiteListed;
		}
		#endif

		t->FromAddr = FromAddr;
	}
	
	// Tokenise the mail...
	Status = MakeMailWordList(m, t->Words);
	if (Status == Store3Error)
	    return Status;
	if (Status == Store3Delayed)
	{
		// Not loaded yet, retry it later...
		m->WhenLoaded(_FL, [this, Analyse, m](auto status)
		{
			double r;
			IsSpam(r, m, Analyse);
		});
		return Store3Delayed;
	}

    // Pass it over to the thread
    t->Analyse = Analyse;
    t->MsgRef = m->GetMailRef();
    d->GetThread()->Add(t);
	
	return Store3Delayed;
	
OnWhiteListed:
	if (Analyse)
	    OnBayesAnalyse(LLoadString(IDS_IS_WHITELIST), FromAddr);
	Result = 0.0;
	return Store3Success;
}

ScribeMailType BayesianFilter::BayesTypeFromPath(LString Path)
{
	if (!Path)
		return BayesMailUnknown;

	auto spam = App->GetFolder(FOLDER_SPAM);
	auto folder = App->GetFolder(Path);
	if (spam && folder)
	{
		if (folder == spam)
			return BayesMailSpam;

		// If folder is a child of the spam folder, it's NOT ham but "unknown".
		// Typically a staging ground for "possible" spam. So it should not contribute to
		// bayes word counts.
		for (auto p = folder->GetParent(); p; p = p->GetParent())
			if (p == spam)
				return BayesMailUnknown;
	}
	else
	{
		LVariant v;
		if (App->GetOptions()->GetValue(OPT_SpamFolder, v))
		{
			auto spamPath = LString(v.Str()).SplitDelimit("/");
			auto inPath = Path.SplitDelimit("/");
			unsigned matching = 1;
			while (matching < spamPath.Length() &&
				   matching < inPath.Length())
			{
				if (spamPath[matching] == inPath[matching])
					matching++;
				else
					break;
			}
			
			if (matching > 1)
				return matching == inPath.Length() ? BayesMailSpam : BayesMailUnknown;
				
			return BayesMailUnknown;
		}
		else
		{
			// This code should never run anymore, it's deprecated...
			LAssert(!"Dont use old hard coding spam name.");
			
			auto t = Path.SplitDelimit("/");
			ssize_t spamIdx = -1;
			for (size_t i=0; i<t.Length(); i++)
				if (t[i].Equals("Spam"))
				{
					spamIdx = i;
					break;
				}

			if (spamIdx == 0 || spamIdx == 1)
				return (ssize_t)t.Length() > spamIdx + 1 ? BayesMailUnknown : BayesMailSpam;
		}
	}

	return BayesMailHam;
}

ScribeMailType BayesianFilter::BayesTypeFromPath(Mail *m)
{
	ScribeMailType Status = BayesMailUnknown;

	if (m)
	{
		ScribeFolder *f = m->GetFolder();
		if (f)
			Status = BayesTypeFromPath(f->GetPath());
	}

	return Status;
}

void BayesianFilter::WhiteListIncrement(const char *Word)
{
	if (!Word)
		return;

	LAutoPtr<BayesianThread::Change> c(new BayesianThread::Change);
	c->IncrementWhite = true;
	c->Str = Word;
	d->GetThread()->Add(c);
}

bool BayesianFilter::RemoveFromWhitelist(const char *Email)
{
	if (!Email)
		return false;

	LAutoPtr<BayesianThread::Change> c(new BayesianThread::Change);
	c->Str = Email;
	c->RemoveWhite = true;
	d->GetThread()->Add(c);	
	return true;
}

Store3Status BayesianFilter::OnBayesianMailEvent(Mail *m, ScribeMailType OldType, ScribeMailType NewType)
{
	if (!m)
	    return Store3Error;

	auto flags = m->GetFlags();

	if (NewType == BayesMailHam)
	{
		if (TestFlag(flags, MAIL_HAM_DB))
			return Store3Success;
		if (!TestFlag(flags, MAIL_BAYES_HAM|MAIL_BAYES_SPAM))
			flags |= MAIL_BAYES_HAM;
	}

	if (NewType == BayesMailSpam)
	{
		if (TestFlag(flags, MAIL_SPAM_DB))
			return Store3Success;
		if (!TestFlag(flags, MAIL_BAYES_HAM|MAIL_BAYES_SPAM))
			flags |= MAIL_BAYES_SPAM;
	}

	m->SetFlags(flags);

	if (d->Work.Length() == 0)
		d->WorkStartTs = LCurrentTime();

	auto &w = d->Work.New();
	w.m = m;
	w.m->IncRef();
	w.Loading = false;
	w.Done = false;
	w.OldType = OldType;
	w.NewType = NewType;

	return Store3Delayed;
}

void BayesianFilter::OnEvent(LMessage *Msg)
{
	switch (Msg->Msg())
	{
		case M_SCRIBE_IDLE:
		{
			// LProfile prof("M_SCRIBE_IDLE", 15);

			if (d->Build)
			{
				if (d->Build->Process())
				{
					// Give the hash tables to the worker thread to convert to disk:
					d->GetThread()->Add(d->Build->b);
					
					// And finish doing the build processing:
					d->Build.Reset();
				}				
				break;
			}

			auto EmptyWorkQueue = [this]()
			{
				for (auto j: d->Work)
				{
					if (!j.Done && j.m)
					{
						j.m->DecRef();
						j.m = NULL;
					}
				}
				d->Work.Length(0);
			};

			// Look for something we can do...
			auto StartTs = LCurrentTime();
			size_t i, processedCount = 0;
			for (i=0; i<d->Work.Length(); i++)
			{
				auto &job = d->Work[i];

				if (job.Done)
					continue;

				if (!job.m->GetObject())
				{
					// Why?
					LgiTrace("%s:%i - Error: object not found.\n", _FL);
					job.Done = true; // We can't continue anyway
					continue;
				}

				if (job.Loading)
				{
					// prof.Add("loading");
					auto loaded = job.m->GetLoaded();
					if (loaded < Store3Loaded)
						continue; // Still hasn't loaded..
				}

				LString words;
				// prof.Add("MakeMailWordList");
				auto Status = MakeMailWordList(job.m, words);
				if (Status == Store3Error)
				{
					// Remove the work from the list... it's probably going to keep failing
					d->Work.DeleteAt(i--);
					continue;
				}
				else if (Status == Store3Delayed)
				{
					if (job.Loading)
						LAssert(!"Already waiting for load.");
					else
						job.Loading = true;
					continue;
				}

				// prof.Add("change");
				LAutoPtr<BayesianThread::Change> c(new BayesianThread::Change);

				c->Str = job.m->GetFromStr(FIELD_EMAIL);
				c->Words = words;
				c->OldType = job.OldType;
				c->NewType = job.NewType;

				d->GetThread()->Add(c);

				// Update the spam/ham flags...
				auto flags = job.m->GetFlags();
				flags &= ~(MAIL_BAYES_SPAM|MAIL_BAYES_HAM);
				if (job.NewType == BayesMailHam)
					flags |= MAIL_BAYES_HAM;
				else if (job.NewType == BayesMailSpam)
					flags |= MAIL_BAYES_SPAM;
				job.m->SetFlags(flags);

				job.m->DecRef();
				job.m = NULL;
				job.Done = true;
				processedCount++;
				
				if (LCurrentTime()-StartTs >= TIMEOUT_BAYES_IDLE)
					break;
			}

			// prof.Add("post");
			if (d->Work.Length() > 0)
			{
				if (!processedCount)
				{
					EmptyWorkQueue();
				}
				else
				{
					auto Now = LCurrentTime();
					LAssert(d->WorkStartTs);
					if (Now - d->WorkStartTs > 300 && !d->Prog)
					{
						d->Prog.Reset(new LProgressDlg(App, 500));
						d->Prog->SetDescription("Bayesian filtering...");
					}
					if (d->Prog)
					{
						d->Prog->SetRange(d->Work.Length());
						d->Prog->Value(i);
						if (d->Prog->IsCancelled())
						{
							// Dump the work queue
							EmptyWorkQueue();
						}
					}
				}
			}
			else
			{
				d->Prog.Reset();
				d->WorkStartTs = 0;
			}
			break;
		}
		case M_SCRIBE_BAYES_RESULT:
		{
			LArray< LAutoPtr<BayesianThread::Test> > Results;
    		if (!d->GetThread()->GetResults(Results))
    			break;

			for (auto t: Results)
			{
				if (!t)
				{
					LAssert(!"Null ptr in Results");
					continue;
				}
				
				if (t->Analyse)
				{
					LArray<char> a;
					int Size = (int) t->Log.GetSize();
					a.Length(Size+1);
					t->Log.Read(&a[0], Size);
					a[Size] = 0;
					OnBayesAnalyse(&a[0], t->WhiteListed ? t->FromAddr : LString());
				}
				else if (t->MsgRef)
				{
					OnBayesResult(t->MsgRef, t->Score);
				}
				else LAssert(!"There should always be a msg ref");
			}
			break;
    	}	    
	}
}

bool BayesianFilter::UnitTests(ScribeWnd *app)
{
	BayesianFilter inst(app);
	ScribeMailType prob, none;
	auto spam = inst.BayesTypeFromPath("/Folders1/Spam");
	if (spam == BayesMailHam)
		goto OnError; // BayesMailUnknown is ok on first startup
	
	prob = inst.BayesTypeFromPath("/Folders1/Spam/Probably");
	if (prob != BayesMailUnknown)
		goto OnError;
		
	none = inst.BayesTypeFromPath("/Folders1/Inbox");
	if (none == BayesMailSpam)
		goto OnError;
		
	return true;
	
OnError:
	LAssert(!"Failed.");
	return false;
}

