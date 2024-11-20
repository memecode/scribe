#include "terminator.h"

#include "Scribe.h"
#include "resdefs.h"
#include "BayesianFilter.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/SpellCheck.h"

typedef LHashTbl<ConstStrKey<char,false>,bool> TokenMap;
#define TIMEOUT_BAYES_LOAD		(10 * 1000)	// 10sec
#define TIMEOUT_PREDICT			(10 * 1000)
#define THRESHOLD				0.615

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

class BayesianFilterPriv : public LThread, public LMutex, public LCancel
{
	Terminator *Classifier = NULL;
	LString StatusMsg;
	bool EmptyDb = false;
	bool DeleteDb = false;

public:
	ScribeWnd *App;
	LString Path;
	uint32_t Saved = 0;

	struct Train
	{
		LString content;
		bool spam;
	};

	struct Prediction
	{
		LString content;
		double result = -1.0;
	};

	LArray<Train> Training;
	LArray<Prediction*> Pred;

	int HamHam = 0;
	int HamSpam = 0;
	int SpamSpam = 0;
	int SpamHam = 0;

	BayesianFilterPriv(ScribeWnd *app) :
		App(app),
		LThread("BayesianFilterPriv.Thread"),
		LMutex("BayesianFilterPriv.Lock")
	{
		LString Leaf = "terminator.kch";
		if (App->GetOptions() && App->GetOptions()->GetFile())
		{
			LFile::Path p(App->GetOptions()->GetFile());
			p--;
			p += Leaf;
			Path = p.GetFull();
		}
		else
		{
			LFile::Path p(LSP_APP_INSTALL);
			p += Leaf;
			Path = p.GetFull();
		}

		Run();
	}

	~BayesianFilterPriv()
	{
		Cancel();
		while (!IsExited())
			LSleep(1);
		DeleteObj(Classifier);
	}

	double Predict(LString content, LString &errMsg)
	{
		if (!content)
		{
			errMsg = "No content.";
			return -1.0;
		}

		if (!Lock(_FL))
		{
			errMsg = "Can't lock.";
			return -1.0;
		}

		Prediction *p;
		Pred.New() = p = new Prediction;
		p->content = content;
		
		Unlock();

		auto Start = LCurrentTime();
		while (!IsExited() && p->result < 0.0)
		{
			if (LCurrentTime() - Start > TIMEOUT_PREDICT)
			{
				LAssert(!"Timeout.");
				break;
			}
			LSleep(1);
		}

		double result = -1.0;
		if (Lock(_FL))
		{
			Pred.Delete(p);
			result = p->result;
			delete p;
			Unlock();
		}		

		return result;
	}

	void Delete()
	{
		DeleteDb = true;
		while (EmptyDb)
			LSleep(1);
	}

	void Empty()
	{
		EmptyDb = true;
		while (EmptyDb)
			LSleep(1);
	}

	int Main()
	{
		uint64_t Ts = 0;

		auto CreateDb = [&]() {
			if (Lock(_FL))
			{
				StatusMsg = "Db loading...";
				Unlock();
			}

			auto t = new Terminator(Path.Get(), 10 << 20, [](auto str)
			{
				LgiTrace("%s:%i - %s\n", _FL, str);
			});
			if (t && Lock(_FL))
			{
				Classifier = t;
				StatusMsg = "Db loaded.";
				LgiTrace("%s:%i - Loaded '%s'.\n", _FL, Path.Get());
				Unlock();
			}
		};

		CreateDb();

		while (!IsCancelled())
		{
			if (EmptyDb)
			{
				DeleteObj(Classifier);
				EmptyDb = false;
			}
			else if (DeleteDb)
			{
				DeleteObj(Classifier);
				FileDev->Delete(Path);
				DeleteDb = false;
			}
			else if (!Classifier)
			{
				CreateDb();
			}

			if (LockWithTimeout(100, _FL))
			{
				Train t;
				if (Training.Length())
					t = Training.PopLast();

				Unlock();

				if (Classifier)
				{
					for (auto p: Pred)
					{
						if (p->result < 0.0)
							p->result = Classifier->Predict(p->content.Get());
					}
					if (t.content)
					{
						auto result = Classifier->Predict(t.content.Get());
						bool isSpam = result >= THRESHOLD;

						if (!t.spam && !isSpam)
							HamHam++;
						else if (!t.spam && isSpam)
							HamSpam++;
						else if (t.spam && !isSpam)
							SpamHam++;
						else if (t.spam && isSpam)
							SpamSpam++;

						Classifier->Train(t.content.Get(), t.spam);
						Saved++;
					}
				}
			}
			LSleep(10);
		}

		return 0;
	}
};

BayesianFilter::BayesianFilter(ScribeWnd *app)
{
	d = new BayesianFilterPriv(App = app);
}

BayesianFilter::~BayesianFilter()
{
	DeleteObj(d);
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

void TokeniseText(const char *Source, bool *Lut, LString::Array &Blocks, TokenMap *Ignore = NULL)
{
	if (!Source || !Lut)
		return;

	char buf[16 << 10];
	int used = 0;
	auto oldWarn = LUtf8Ptr::Warn;
	LUtf8Ptr::Warn = false;

	auto emitBuf = [&]() {
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

LString BayesianFilter::MakeMailWordList(Mail *m)
{
	#if 1

		LStringPipe p;
		if (m->Export(p, sMimeMessage))
			return p.NewLStr();

		return NULL;

	#else

		LString::Array Blocks;

		if (!m || !m->GetObject())
			return NULL;

		LProfile prof("Make", 1000);

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
	
		prof.Add("Ignore");
		
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
		ProcessWords(LString("").Join(Temp), [&](auto w)
		{
			Ignore.Add(w, true);
		});
		
		prof.Add("TokHdrs");

		// process various parts of the email
		TokeniseText(m->GetSubject(), Lut, Blocks, &Ignore);
		TokeniseText(m->GetFromStr(FIELD_EMAIL), Email, Blocks, &Ignore);
		TokeniseText(m->GetFromStr(FIELD_NAME), Lut, Blocks, &Ignore);
		LVariant Body;

		prof.Add("WaitLoad");
		Store3State Loaded = (Store3State)m->GetObject()->GetInt(FIELD_LOADED);
		if (Loaded < Store3Loaded)
		{
			m->GetBody();

			auto Start = LCurrentTime();
			uint64_t Total = 0;
			while ( (Loaded = (Store3State)m->GetObject()->GetInt(FIELD_LOADED))  < Store3Loaded )
			{
				LSleep(1);
				Total = LCurrentTime() - Start; 
				if (Total > TIMEOUT_BAYES_LOAD)
					break;
			}
			if (Total > 1000)
				LgiTrace("Waiting %gsec for item to load.\n", (double)Total/1000.0);
		}

		prof.Add("GetBody");

		auto Req = m->GetValue("BodyAsText", Body);
		if (Req)
		{
			prof.Add("TokBody");
			TokeniseText(Body.Str(), Lut, Blocks, &Ignore);
		}
		else
		{
			LString path = "(nullFolder)";
			auto fld = m->GetFolder();
			if (fld)
				path = fld->GetPath();
			LgiTrace("%s:%i - couldn't get body for %s/%s\n", _FL, path.Get(), m->GetMessageId());
			return NULL;
		}
	
		prof.Add("Join");
		return LString("").Join(Blocks);

	#endif
}

void BayesianFilter::AddFolderToSpamDb(ScribeFolder *f, class BuildSpamDB *Build)
{
}

struct MailItem
{
	Mail *obj = NULL;
	bool spam;
};

#define WRITE_CORPSE	0

bool BayesianFilter::BuildSpamDb()
{
	auto inbox = App->GetFolder("/IMAP/INBOX");
	auto spam = App->GetFolder("/Mail3/Spam/Recent");
	if (!inbox || !spam)
		return false;

	#if WRITE_CORPSE
		LString outPath = "E:\\Code\\Scribe\\libs\\terminator\\demo\\corpus";
		LString indexPath = outPath + "\\index";
		LFile index(indexPath, O_WRITE);
		index.SetSize(0);
	#else
		d->Delete();
	#endif

	inbox->LoadThings();
	spam->LoadThings();

	LProgressDlg prog(App, 100);
	auto Saved = prog.Push();
	auto Total = inbox->Items.Length()+spam->Items.Length();
	prog.SetRange(Total);
	prog.SetDescription("Parsing email...");
	Saved->SetRange(Total);
	Saved->SetDescription("Saving training data...");

	LArray<MailItem> Items;

	for (auto i: inbox->Items)
	{
		auto &item = Items.New();
		item.obj = i->IsMail();
		item.spam = false;
	}

	for (auto i: spam->Items)
	{
		auto &item = Items.New();
		item.obj = i->IsMail();
		item.spam = true;
	}

	Items.Sort([](auto a, auto b){		
		auto Ad = a->obj->GetDateReceived();
		auto Bd = b->obj->GetDateReceived();
		return Ad->Compare(Bd);
	});

	#if WRITE_CORPSE
		for (auto &i: Items)
		{
			LString rel;
			LFile::Path p(outPath);
			rel.Printf("%s/%p", i.spam ? "spam" : "ham", &i);
			p += rel;
			index.Print("%s %s\n", 	i.spam ? "spam" : "ham", rel.Get());

			LFile out(p, O_WRITE);
			out.Write(i.img);
		}
	#else
		for (auto &i: Items)
		{
			OnBayesianMailEvent(i.obj, BayesMailUnknown, i.spam ? BayesMailSpam : BayesMailHam);

			prog.Value(prog.Value()+1);
			Saved->Value(d->Saved);
			if (prog.IsCancelled())
				break;
		}

		while (d->Training.Length())
		{
			LString s;
			s.Printf("hh: %i hs: %i sh: %i ss: %i", d->HamHam, d->HamSpam, d->SpamHam, d->SpamSpam);
			Saved->SetDescription(s);
			Saved->Value(d->Saved);			
			if (prog.IsCancelled())
				break;
			LSleep(1);
		}

		d->Empty(); // unload DB to flush everything to disk..
	#endif

	return true;
}

Store3Status BayesianFilter::IsSpam(double &Result, Mail *m, bool Analyse)
{
	if (!m)
		return Store3Error;

	auto content = MakeMailWordList(m);
	LString err;
	Result = d->Predict(content.Get(), err);
	if (Analyse)
	{
		LString s;
		s.Printf("Predict result: %g %s", Result, err?err.Get():"");
		OnBayesAnalyse(s, NULL);
	}

	return Store3Success;
}
    
void BayesianFilter::WhiteListIncrement(const char *Word)
{
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
		auto t = Path.SplitDelimit("/");
		ssize_t spamIdx = -1;
		for (size_t i=0; i<t.Length(); i++)
			if (t[i].Equals("Spam"))
			{
				spamIdx = i;
				break;
			}

		if (spamIdx == 0 || spamIdx == 1)
			return t.Length() > spamIdx + 1 ? BayesMailUnknown : BayesMailSpam;
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

bool BayesianFilter::OnBayesianMailEvent(Mail *m, ScribeMailType OldType, ScribeMailType NewType)
{
	if (!m)
		return false;

	auto content = MakeMailWordList(m);
	if (!content)
		return false;

	if (NewType == BayesMailSpam ||
		NewType == BayesMailHam)
	{
		if (!d->Lock(_FL))
			return false;

		auto &t = d->Training.New();
		t.content = content;
		t.spam = NewType == BayesMailSpam;
		d->Unlock();
	}
	
	return true;
}

bool BayesianFilter::RemoveFromWhitelist(const char *Email)
{
	return false;
}
    
void BayesianFilter::OnEvent(LMessage *Msg)
{
}

#include "lgi/common/Graph.h"
#include "lgi/common/Box.h"
class StatsGraph : public LWindow
{
	LBox *box = NULL;
	LGraph *s = NULL, *in = NULL;
	LArray<double> spamPred, inboxPred;
	
public:
	StatsGraph(LArray<double> &spam, LArray<double> &inbox)
	{
		LRect rc(100, 100, 1000, 800);
		SetPos(rc);

		LGraph::Range r;
		r.Min = 0.0;
		r.Max = 1.0;

		AddView(box = new LBox(100));
		box->AddView(s = new LGraph(101));
		s->ShowCursor(true);
		s->SetLabel(true, "Spam");
		for (int i=0; i<spam.Length(); i++)
		{
			char x[32], y[32];
			sprintf_s(x, sizeof(x), "%i", i);
			sprintf_s(y, sizeof(y), "%g", spam[i]);
			s->AddPair(x, y);
		}
		s->SetRange(false, r);
		
		box->AddView(in = new LGraph(102));
		in->ShowCursor(true);
		in->SetLabel(true, "Ham");
		for (int i=0; i<inbox.Length(); i++)
		{
			char x[32], y[32];
			sprintf_s(x, sizeof(x), "%i", i);
			sprintf_s(y, sizeof(y), "%g", inbox[i]);
			in->AddPair(x, y);
		}
		in->SetRange(false, r);

		if (Attach(0))
		{
			AttachChildren();
			Visible(true);
		}
	}
};

void BayesianFilter::BuildStats()
{
	auto spam = App->GetFolder("/Mail3/Spam/Recent");
	auto inbox = App->GetFolder("/IMAP/INBOX");
	if (spam && inbox)
	{
		LArray<double> spamPred, inboxPred;
		spam->LoadThings();

		LProgressDlg prog;
		prog.SetDescription("Spam...");
		prog.SetRange(spam->Items.Length());
		auto inboxProg = prog.Push();
		inboxProg->SetDescription("Inbox...");
		inboxProg->SetRange(inbox->Items.Length());

		LString err;
		for (auto i: spam->Items)
		{
			auto content = MakeMailWordList(i->IsMail());
			if (content)
			{
				auto r = d->Predict(content, err);
				if (r >= 0.0)
					spamPred.Add(r);
			}

			prog++;
			
			// if (spamPred.Length() > 100) break;
		}
		for (auto i: inbox->Items)
		{
			auto content = MakeMailWordList(i->IsMail());
			if (content)
			{
				auto r = d->Predict(content, err);
				if (r >= 0.0)
					inboxPred.Add(r);
			}
			(*inboxProg)++;
			
			// if (inboxPred.Length() > 100) break;
		}

		spamPred.Sort();
		inboxPred.Sort();

		new StatsGraph(spamPred, inboxPred);
	}
}

