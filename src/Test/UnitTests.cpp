#ifdef _DEBUG

#include "Scribe.h"
#include "lgi/common/SpellCheck.h"
#include "Encryption/GnuPG.h"
#include "Store3Mail3/Mail3.h"
#include "lgi/common/RichTextEdit.h"

#include "resdefs.h"
#include "LoadMailStoreState.h"

#define UnitTestFail()	{ if (Callback) Callback(false); return; }
#define UnitTestPass()	{ if (Callback) Callback(true); return; }

class NoSaveOptions : public LOptionsFile
{
	bool Serialize(bool Write) { return true; }
public:
	NoSaveOptions(LOptionsFile *opts) :
		LOptionsFile(PortableMode, AppName)
	{
	}
};

struct ScribeUnitTest
{
	uint64_t StartTs = 0;
	int Timeout = 5000;

	virtual ~ScribeUnitTest() {}
	virtual void OnTimeout() {}
	virtual void Run(std::function<void(bool)> Callback) = 0;

	virtual void OnPulse()
	{
		if (StartTs != 0 &&
			LCurrentTime() - StartTs >= Timeout)
		{
			LgiTrace("%s:%i - UnitTest timed out.\n", _FL);
			StartTs = 0;
			OnTimeout();
		}
	}
};

struct UnitTestState :
	public LView::ViewEventTarget,
	public LThread,
	public LCancel
{
	ScribeWnd *App = NULL;
	NoSaveOptions *Opts = NULL;
	std::function<void(bool)> Callback;
	LArray<ScribeUnitTest*> Tests;
	LAutoPtr<ScribeUnitTest> t;
	bool Status = true;
	bool IsFinished = false;

	// Old app state..
	LAutoPtr<LOptionsFile> OldOpts;
	LArray<LMailStore> OldFolders;

	UnitTestState(ScribeWnd *app, std::function<void(bool)> callback);
	~UnitTestState();

	LMessage::Result OnEvent(LMessage *Msg) override;
	bool Iterate();
	bool Done();
	int Main() override;
	LDataStoreI *CreateTestMail3();

	// Wrappers for protected elements:
	LArray<LMailStore> &GetFolders() { return App->Folders; }
	void UnLoadFolders()
	{
		OldOpts = App->d->Options;
		App->d->Options.Reset(Opts = new NoSaveOptions(OldOpts));

		OldFolders.Swap(App->Folders);
		App->UnLoadFolders();
	}
};

// Basic load mail3 folder...
struct LoadMailStore1 : public ScribeUnitTest
{
	UnitTestState *s;
	LString folderPath;
	bool GotCallback = false;
	std::function<void(bool)> Callback;

	LoadMailStore1(UnitTestState *state) : s(state)
	{
		s->UnLoadFolders();
	}

	~LoadMailStore1()
	{
		if (folderPath)
		{
			s->UnLoadFolders();

			LFile::Path p = folderPath;
			FileDev->RemoveFolder(p / "..", true);
		}
		LAssert(GotCallback);
	}

	// Do operations on mail store before we try and load it...
	virtual void OnMailStore(LDataStoreI *store) {}
	virtual void ConfigureLoadMailStoreState(LoadMailStoreState *state) {}

	void OnTimeout()
	{
		GotCallback = true;
		Callback(false); // This should delete 'this'
	}

	void Run(std::function<void(bool)> cb)
	{
		Callback = cb;

		if (auto store = s->CreateTestMail3())
		{
			folderPath = store->GetStr(FIELD_NAME);
			OnMailStore(store);
			delete store;
		}

		// Setup the options saying what folders to load...
		s->Opts->CreateTag(OPT_MailStores);
		auto MailStores = s->Opts->LockTag(OPT_MailStores, _FL);
		if (MailStores == NULL)
			UnitTestFail();

		auto ms = MailStores->CreateTag(OPT_MailStore);
		ms->SetAttr(OPT_MailStoreLocation, folderPath);
		ms->SetAttr(OPT_MailStoreDisable, "0");
		ms->SetAttr(OPT_MailStoreName, "testing");

		s->Opts->Unlock();
		if (!ms)
			UnitTestFail();

		// Try the load...
		if (auto state = new LoadMailStoreState(s->App, [this](auto status)
			{
				GotCallback = true;

				auto &Folders = s->GetFolders();
				if (Folders.Length() == 0)
					UnitTestFail()

					bool found = false;
				for (auto &f: Folders)
				{
					if (f.Path == folderPath &&
						f.Store != NULL)
					{
						found = true;
						break;
					}
				}
				if (!found)
					UnitTestFail();

				Callback(status);
			})
			)
		{
			ConfigureLoadMailStoreState(state);
			state->Start();
		}
	}
};

// This tests the DB schema upgrade functionality of the mail3 store.
struct LoadMailStore2 : public LoadMailStore1
{
	LoadMailStore2(UnitTestState *state) :
		LoadMailStore1(state)
	{
	}

	void OnMailStore(LDataStoreI *store) override
	{
		auto ms3 = dynamic_cast<LMail3Store*>(store);
		if (!ms3)
			UnitTestFail();

		// Delete a field from the database so that we force an "upgrade"
		LMail3Store::LStatement s(ms3, "alter table Calendar rename column DateModified to DateMod");
		if (!s.Exec())
			UnitTestFail();
	}

	// This is a simple placeholder dialog to simulate asking the user to
	// upgrade the mail store. It then returns a 'Yes' to the callback after
	// a few seconds.
	struct UpgradeYes : public LWindow
	{
		UnitTestState *s;
		LoadMailStoreState::IntCb Cb;

		UpgradeYes(UnitTestState *state, LoadMailStoreState::IntCb cb) :
			s(state),
			Cb(cb)
		{
			Name("Upgrade Dlg");

			// Just needs to be open long enough to run the msg loop a bit.
			SetPulse(500);

			LRect r(0, 0, 100, 100);
			SetPos(r);
			MoveSameScreen(s->App);

			if (Attach(NULL))
				Visible(true);
		}

		void OnPulse()
		{
			Cb(IDYES);
			Quit();
		}
	};

	void ConfigureLoadMailStoreState(LoadMailStoreState *state) override
	{
		state->AskStoreUpgrade = [this](auto path, auto detail, auto cb)
		{
			new UpgradeYes(s, cb);
		};
	}
};

struct MimeTreeTest : public ScribeUnitTest
{
	UnitTestState *s;
	Mail *m = NULL;
	ThingUi *ui = NULL;
	std::function<void(bool)> callback;
	int step = 0;
	const char16 *htmlContent = L"This is the content";
	const char *content1 = "content1";
	const char *content2 = "content2";

	const char *GetClass() { return "MimeTreeTest"; }
	
	MimeTreeTest(UnitTestState *state) : s(state)
	{
	}

	void Run(std::function<void(bool)> cb)
	{
		callback = cb;
		m = s->App->CreateMail();
		ui = m->DoUI();
	}

	LString CreateTmp(const char *name)
	{
		LFile::Path p(ScribeTempPath());
		LString leaf;
		leaf.Printf("%s.txt", name);
		p += leaf;

		LFile f;
		if (f.Open(p.GetFull(), O_WRITE))
		{
			f.SetSize(0);
			for (int i=0; i<5; i++)
				f.Print("%s\n", name);
			f.Close();
		}

		return p.GetFull();
	}

	void OnComplete(bool status, const char *errMsg)
	{
		if (!status)
			LgiTrace("%s:%i - %s failed: %s\n", _FL, GetClass(), errMsg);
		if (callback)
			callback(status);
	}

	void Dump(LMime &m, int depth = 0)
	{
		auto indent = LString(" ") * (depth<<2);
		auto mimetype = m.LGetMimeType();
		auto filename = m.LGetFileName();
		LgiTrace("%sseg:%s fn=%s\n", indent.Get(), mimetype.Get(), filename.Get());
		for (int i=0; i<m.Length(); i++)
			Dump(*m[i], depth+1);
	}

	bool Check(LMime &m, LString content)
	{
		auto s = m.GetData();
		if (!s)
			return false;
		LArray<char> data;
		data.Length(s->GetSize());
		auto rd = s->Read(data.AddressOf(), s->GetSize());
		return Strnstr(data.AddressOf(), content.Get(), rd) != NULL;
	}

	void OnMime(LMime &m)
	{
		Dump(m);

		auto type = m.LGetMimeType();
		if (Stricmp(type.Get(), sMultipartMixed))
			return OnComplete(false, "wrong root node type");
		if (m.Length() == 0)
			return OnComplete(false, "no child segs");

		LMime *html = NULL;
		LArray<LMime*> others, attachments;
		for (int i=0; i<m.Length(); i++)
		{
			auto c = m[i];
			type = c->LGetMimeType();
			auto fn = c->LGetFileName();
			if (type.Equals(sTextHtml))
				html = c;
			else if (fn && type.Equals(sTextPlain))
				attachments.Add(c);
			else
				others.Add(c);
		}

		if (!html)
			return OnComplete(false, "missing html.");
		if (others.Length())
			return OnComplete(false, "should be no other segs.");
		if (attachments.Length() != 2)
			return OnComplete(false, "wrong attachment count.");

		if (!Check(*html, htmlContent))
			return OnComplete(false, "html missing the right content");
		for (auto a: attachments)
		{
			if (!Check(*a, content1) &&
				!Check(*a, content2))
				return OnComplete(false, "attachment missing content.");
		}

		OnComplete(true, "success");
	}

	void OnPulse()
	{
		switch (step)
		{
			case 0:
			{
				if (!ui)
					break;
				step++;

				LEdit *e;
				if (ui->GetViewById(IDC_ENTRY, e))
				{
					e->Name("fret@memecode.com");
					e->SendNotify(LNotifyReturnKey);
				}
				ui->SetCtrlName(IDC_SUBJECT, GetClass());
			
				MailUi *mailui = dynamic_cast<MailUi*>(ui);
				if (!mailui)
					LAssert(!"Not a MailUi?");
				else
				{
					mailui->AttachFile(CreateTmp(content1));
					mailui->AttachFile(CreateTmp(content2));

					auto html = mailui->GetDoc(sTextHtml);
					if (html)
					{
						auto edit = dynamic_cast<LRichTextEdit*>(html);
						if (edit)
							edit->Insert(0, htmlContent, StrlenW(htmlContent)); 
					}
				}
				break;
			}
			case 1:
			{
				step++;
				ui->OnSave();
				ui->Quit();
				break;
			}
			case 2:
			{
				step++;
				LAutoPtr<LStreamI> stream(new LStringPipe);
				auto result = m->Export(stream, sMimeMessage, [this](auto prog, auto mime)
				{
					LMime parser;
					auto sz = mime->GetSize();
					mime->SetPos(0);
					if (!parser.Text.Decode.Pull(mime))
						OnComplete(false, "mime decode failed");
					else
						OnMime(parser);
				});
				if (result.status == Store3Error)
					OnComplete(false, "mail export failed");
				break;
			}
		}

		ScribeUnitTest::OnPulse();
	}
};

UnitTestState::UnitTestState(ScribeWnd *app, std::function<void(bool)> callback) :
	LView::ViewEventTarget(app, M_UNIT_TEST_TICK),
	LThread("UnitTestState"),
	App(app),
	Callback(callback)
{
#if 0
	Tests.Add(new LoadMailStore1(this));
	Tests.Add(new LoadMailStore2(this));
#else
	Tests.Add(new MimeTreeTest(this));
#endif

	LgiTrace("%s:%i - Starting with " LPrintfInt64 " unit tests.\n", _FL, Tests.Length());
	Run();
}

UnitTestState::~UnitTestState()
{
	Cancel();

	// Restore app state...
	if (OldOpts)
		App->d->Options = OldOpts;

	if (OldFolders.Length())
		OldFolders.Swap(App->Folders);

	WaitForExit();
}

LMessage::Result UnitTestState::OnEvent(LMessage *Msg)
{
	if (!IsFinished &&
		Msg->Msg() == M_UNIT_TEST_TICK)
	{
		if (t)
			t->OnPulse();
		else
			Iterate();
	}
	return 0;
}

bool UnitTestState::Iterate()
{
	if (Tests.Length() == 0)
		return Done();

	t.Reset(Tests[0]);
	Tests.DeleteAt(0, true);
	t->StartTs = LCurrentTime();
	LgiTrace("%s:%i - Running unit test..\n", _FL);
	t->Run([this](auto ok)
		{
			LgiTrace("%s:%i - Unit test status: %i\n", _FL, ok);
			Status &= ok;
			t.Reset(); // Reset for the next unit test.
		});

	return true;
}

bool UnitTestState::Done()
{
	// Stop more tick events..
	IsFinished = true;
	Cancel();

	if (Callback)
		Callback(Status);

	delete this;
	return true;
}

int UnitTestState::Main()
{
	while (!IsCancelled())
	{
		PostEvent(M_UNIT_TEST_TICK);
		LSleep(500);
	}

	return 0;
}

LDataStoreI *UnitTestState::CreateTestMail3()
{
	LFile::Path p(ScribeTempPath());
	p += "UnitTesting";
	if (!p.Exists())
		FileDev->CreateFolder(p);
	p += "Folders.mail3";

	return App->CreateDataStore(p, true);
}

void ScribeWnd::UnitTests(std::function<void(bool)> Callback)
{
	new UnitTestState(this, Callback);
}

#endif
