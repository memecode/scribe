#pragma once

#include "lgi/common/Browser.h"
#include "lgi/common/ClipBoard.h"
#include "lgi/common/Growl.h"
#include "lgi/common/EventTargetThread.h"

#include "../unittests/UnitTest.h"
#include "../src/common/Coding/ScriptingPriv.h"

class NoContactType : public Contact
{
	LString NoFace80Path;
	LString NoFace160Path;

public:
	NoContactType(ScribeWnd *wnd) : Contact(wnd)
	{
	}

	const char *GetClass() override { return "NoContactType"; }

	Thing &operator =(Thing &c) override
	{
		return *this;
	}

	bool GetVariant(const char *Name, LVariant &Value, const char *Array) override
	{
		ScribeDomType Fld = StrToDom(Name);
		int Px = Array ? atoi(Array) : 80;
		LString &Str = Px == 160 ? NoFace160Path : NoFace80Path;

		if (!Str)
		{
			LString f;
			f.Printf("NoFace%i.png", Px);
			Str = LFindFile(f);
			LAssert(Str != NULL); // This should always resolve.
		}

		if (!Str)
			return false;

		if (Fld == SdImageHtml)
		{
			LString html;
			html.Printf("<img src='file://%s'>\n", Str.Get());
			Value = html;
			return true;
		}
		else if (Fld == SdImage)
		{
			Value = Str;
			return true;
		}

		return false;
	}
};

class ScribeWndPrivate :
	public LBrowser::LBrowserEvents,
	public LVmCallback,
	public LHtmlStaticInst
{
	LOptionsFile::PortableType InstallMode = LOptionsFile::UnknownMode;

public:
	ScribeWnd		*App;

	uint64          LastTs = 0;
	LClipBoard::FormatType ClipboardFormat = 0;
	LFont			*PreviewFont = NULL;
	int				PrintMaxPages = -1;
	int				NewMailTimeout = -1;
	bool			SendAfterReceive = false;
	bool			IngoreOnClose = false;
	LString			UiTags;
	LAutoPtr<LGrowl> Growl;
	LArray<Contact*> TrayMenuContacts;
	bool            ExitAfterSend = false;
	LToolButton		*ShowConsoleBtn = NULL;
	LString			MulPassword;
	LString			CalendarSummary;
	LBox			*SubSplit = NULL, *SearchSplit = NULL;
	LArray<ScribeFolder*> ThingSources;
	int				LastLayout = 0;
	LMenuItem		*DisableUserFilters = NULL;
	LAutoPtr<LOptionsFile> Options;
	HttpImageThread	*ImageLoader = NULL;
	int				LastMinute = -1, LastHour = -1;
	LArray<LDataEventsI*> Store3EventCallbacks;
	LAutoPtr<LPrinter> PrintOptions;
	LHashTbl<IntKey<SribeResourceType,ResNone>, LString> ResFiles;
	int				CmdLineEvents = 0;
	bool			FakeIpcEvent = false; // No options filename so do a fake OnCommandLineEvent(IpcEvent) after startup.

	// These are for the LDataEventsI callbacks to store source context
	// Mainly for debugging where various events came from.
	const char		*CtxFile = NULL;
	int				CtxLine = 0;

	// Contact no face images
	LAutoRefPtr<NoContactType> NoContact;

	// Remote content white/blacklists
	bool RemoteContent_Init = false;
	LString::Array RemoteWhiteLst, RemoteBlackLst;

	// Spell checking
	int AppWndHnd;
	LAutoPtr<LSpellCheck> SpellerThread;

	// Missing caps
	LCapabilityTarget::CapsHash MissingCaps;
	MissingCapsBar *Bar = NULL;
	LString ErrSource; // Script file that has an error.
	Filter *ErrFilter = NULL; // Filter that has scripting error.

							  // Load state
	bool			FoldersLoaded = false;	

	// Bayesian filter
	LStringPipe BayesLog;

	// Thread item processing
	LArray<MailTransferEvent*> Transfers;

	// Scripting...
	LAutoPtr<LScriptEngine> Engine;
	LArray<LScript*> Scripts;
	LArray<LScript*> CurrentScripts;
	LScript *CurrentScript() { return CurrentScripts.Length() ? CurrentScripts.Last() : NULL; }
	int NextToolMenuId = IDM_TOOL_SCRIPT_BASE;
	int NextScriptUid = 1000;
	LAutoPtr<LScriptUi> ScriptToolbar;
	LArray<LScriptCallback*> OnSecondTimerCallbacks;

	// Encryption
	LAutoPtr<GpgConnector> GpgInst; 

	// Unit tests
	LAutoPtr<LUnitTestServer> UnitTestServer;

	class ScribeTextControlFactory : public LViewFactory
	{
		ScribeWnd *Wnd;

		LView *NewView(const char *Class, LRect *Pos, const char *Text)
		{
			if (!_stricmp(Class, "ScribeTextView"))
				return Wnd->CreateTextControl(-1, 0, true);
			return NULL;
		}

	public:
		ScribeTextControlFactory(ScribeWnd *wnd)
		{
			Wnd = wnd;
		}
	} TextControlFactory;

	ScribeWndPrivate(ScribeWnd *app) :
		App(app),
		TextControlFactory(app)
	{
		NoContact = new NoContactType(app);
		NoContact->DecRef(); // 2->1
		AppWndHnd = LEventSinkMap::Dispatch.AddSink(App);

#ifdef WIN32
		ClipboardFormat = RegisterClipboardFormat(
#ifdef UNICODE
			L"Scribe.Item"
#else
			"Scribe.Item"
#endif
		);
#endif

		LScribeScript::Inst = new LScribeScript(App);
		if (Engine.Reset(new LScriptEngine(App, LScribeScript::Inst, this)))
			Engine->SetConsole(LScribeScript::Inst->GetLog());
	}

	~ScribeWndPrivate()
	{
		// Why do we need this? ~LView will take care of it?
		// LEventSinkMap::Dispatch.RemoveSink(App);
		Options.Reset();
		Scripts.DeleteObjects();
		DeleteObj(ImageLoader);
		Engine.Reset();
		DeleteObj(LScribeScript::Inst);
	}

	const char *GetClass() override { return "ScribeWndPrivate"; }

	LGrowl *GetGrowl()
	{
		if (!Growl &&
			Growl.Reset(new LGrowl))
		{
			LAutoPtr<LGrowl::LRegister> r(new LGrowl::LRegister);
			r->App = "Scribe";
			r->IconUrl = "http://memecode.com/images/scribe/growl-app.png";

			LGrowl::LNotifyType &NewMail = r->Types.New();
			NewMail.Name = "new-mail";
			NewMail.IconUrl = "http://memecode.com/images/scribe/growl-new-mail.png";
			NewMail.Enabled = true;

			LGrowl::LNotifyType &Cal = r->Types.New();
			Cal.Name = "calendar";
			Cal.IconUrl = "http://memecode.com/images/scribe/growl-calendar.png";
			Cal.Enabled = true;

			LGrowl::LNotifyType &Debug = r->Types.New();
			Debug.Name = "debug";
			Debug.IconUrl = "http://memecode.com/images/scribe/growl-bug.png";
			Debug.Enabled = false;

			LGrowl::LNotifyType &Info = r->Types.New();
			Info.IconUrl = "http://memecode.com/images/scribe/growl-note.png";
			Info.Name = "info";
			Info.Enabled = true;

			Growl->Register(r);
		}

		return Growl;
	}

	LVmDebugger *AttachVm(LVirtualMachine *OriginalVm, LCompiledCode *Code, const char *Assembly) override
	{
		if (!OriginalVm || !Code)
			return NULL;

		LVariant v;
		if (Options)
			Options->GetValue(OPT_ScriptDebugger, v);
		if (!v.CastInt32())
			return NULL;

		LAutoPtr<LVirtualMachine> CopiedVm(new LVirtualMachine(OriginalVm));
		LAutoPtr<LCompiledCode> CopiedCode(new LCompiledCode(*Code));
		return new LVmDebuggerWnd(App, this, CopiedVm, CopiedCode, Assembly);
	}

	bool CallCallback(LVirtualMachine &Vm, LString CallbackName, LScriptArguments &Args) override
	{
		for (auto s: Scripts)
		{
			if (!s->Code)
				continue;

			auto Method = s->Code->GetMethod(CallbackName);
			if (!Method)
				continue;

			auto Status = Vm.ExecuteFunction(s->Code, Method, Args);
			return Status > ScriptError;
		}

		Vm.SetDebuggerEnabled(true); // Lets show the UI when we throw the callback not found error.
		Args.Throw(_FL, "There is no function '%s' for callback.", CallbackName.Get());
		return false;
	}

	bool CompileScript(LAutoPtr<LCompiledCode> &Output, const char *FileName, const char *Source) override
	{
		LCompiler c;
		return c.Compile(Output, Engine->GetSystemContext(), LScribeScript::Inst, FileName, Source, NULL);
	}

	bool OnSearch(LBrowser *br, const char *txt) override
	{
		char Path[256];
		if (!App->GetHelpFilesPath(Path, sizeof(Path)))
			return false;

		auto Terms = LString(txt).SplitDelimit(", ");

		LStringPipe p;
		p.Print("<html>\n<body><h1>Search Results</h1>\n<ul>\n");
		LDirectory Dir;
		for (int b = Dir.First(Path, "*.html"); b; b = Dir.Next())
		{
			if (!Dir.IsDir())
			{
				char Path[256];
				Dir.Path(Path, sizeof(Path));
				LFile f;
				if (f.Open(Path, O_READ))
				{
					LXmlTree t(GXT_NO_DOM);
					LXmlTag r;
					if (t.Read(&r, &f))
					{
						char *PrevName = 0;
						char PrevUri[256] = "";
						for (auto c: r.Children)
						{
							if (c->IsTag("a"))
							{
								char *Name = c->GetAttr("name");
								if (Name)
								{
									PrevName = Name;
								}
							}
							else if (c->GetContent())
							{
								bool Hit = false;
								for (unsigned i=0; !Hit && i<Terms.Length(); i++)
								{
									Hit = stristr(c->GetContent(), Terms[i]) != 0;
								}
								if (Hit)
								{
									LStringPipe Uri(256);
									char *Leaf = strrchr(Path, DIR_CHAR);
									Leaf = Leaf ? Leaf + 1 : Path;

									Uri.Print("file://%s", Path);
									if (PrevName)
										Uri.Print("#%s", PrevName);

									LAutoString UriStr(Uri.NewStr());

									if (_stricmp(UriStr, PrevUri))
									{
										p.Print("<li> <a href='%s'>%s",
											UriStr.Get(),
											Leaf);
										if (PrevName)
											p.Print("#%s", PrevName);
										p.Print("</a>\n");

										strcpy_s(PrevUri, sizeof(PrevUri), UriStr);
									}
								}
							}
						}
					}
				}
			}
		}

		p.Print("</ul>\n</body>\n</html>\n");
		LAutoString Html(p.NewStr());
		br->SetHtml(Html);
		return true;
	}

	void AskUserForInstallMode(std::function<void(LOptionsFile::PortableType)> callback)
	{
		auto Dlg = new LAlert(App,
			AppName,
			LLoadString(IDS_PORTABLE_Q),
			LLoadString(IDS_HELP),
			LLoadString(IDS_DESKTOP),
			LLoadString(IDS_PORTABLE));

		Dlg->SetButtonCallback(1, [this](auto idx)
			{
				App->LaunchHelp("install.html");
			});

		Dlg->DoModal([callback](auto dlg, auto Btn)
			{
				if (Btn == 1)
				{
					// Help
					LAssert(!"Help btn should use callback.");
				}
				else if (Btn == 2)
				{
					// Desktop
					if (callback)
						callback(LOptionsFile::DesktopMode);
				}
				else if (Btn == 3)
				{
					// Portable
					if (callback)
						callback(LOptionsFile::PortableMode);
				}
				else
				{
					delete dlg;
					LAppInst->Exit(1);
				}

				delete dlg;
			});
	}

	LOptionsFile::PortableType GetInstallMode()
	{
		if (InstallMode == LOptionsFile::UnknownMode)
		{
			if (LAppInst->GetOption("portable"))
			{
				InstallMode = LOptionsFile::PortableMode;
				LgiTrace("Selecting portable mode based on -portable switch.\n");
			}
			else if (LAppInst->GetOption("desktop"))
			{
				InstallMode = LOptionsFile::DesktopMode;
				LgiTrace("Selecting portable mode based on -desktop switch.\n");
			}
		}

		if (InstallMode == LOptionsFile::UnknownMode)
		{
			bool PortableIsPossible = true;

			char Inst[MAX_PATH_LEN] = "";
			LGetSystemPath(LSP_APP_INSTALL, Inst, sizeof(Inst));

			// Do write check
			char Wr[MAX_PATH_LEN];
			LMakePath(Wr, sizeof(Wr), Inst, "_write_test.txt");
			LFile f;
			if (f.Open(Wr, O_WRITE))
			{
				// Clean up
				f.Close();
				FileDev->Delete(Wr, false);
			}
			else
			{
				// No write perms
				PortableIsPossible = false;
			}

			if (PortableIsPossible && LAppInst->IsElevated())
			{
				// Check if the install is in some read only location:
				// e.g. c:\Program Files
				char Pm[MAX_PATH_LEN];
				if (LGetSystemPath(LSP_USER_APPS, Pm, sizeof(Pm)))
				{
					size_t n = strlen(Pm);
					PortableIsPossible = _strnicmp(Pm, Inst, n) != 0;
					// LgiMsg(App, "%i\n%s\n%s", AppName, MB_OK, PortableIsPossible, Pm, Inst);
				}
				else LgiTrace("%s:%i - Failed to get paths.", _FL);
			}

			if (PortableIsPossible)
			{
				// Basically "ask the user" here...
				return LOptionsFile::UnknownMode;
			}
			else
			{
				InstallMode = LOptionsFile::DesktopMode;
				LgiTrace("Selecting Desktop based on lack of write permissions to install folder.\n");
			}
		}

		return InstallMode;
	}

	void SetInstallMode(LOptionsFile::PortableType t)
	{
		InstallMode = t;
	}

	void DeleteCallbacks(LArray<LScriptCallback> &Callbacks)
	{
		for (unsigned i=0; i<Callbacks.Length(); i++)
		{
			if (Callbacks[i].Type == LToolsMenu &&
				Callbacks[i].Param)
			{
				auto it = App->GetMenu()->FindItem(Callbacks[i].Param);
				if (it)
				{
					it->Remove();
					DeleteObj(it);
				}
			}
		}
	}
};
