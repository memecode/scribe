#include "Scribe.h"
#include "ScribePrivate.h"
#include "lgi/common/Scripting.h"
#include "lgi/common/TextView3.h"
#include "lgi/common/ThreadEvent.h"
#include "lgi/common/LgiRes.h"

#include "resdefs.h"
#include "../src/common/Coding/ScriptingPriv.h"

extern LHostFunc Methods[];
LStringPipe ScribeInitTraceStore;

#ifdef _MSC_VER
#define __func__ __FUNCTION__
#endif

#define ARG_CHECK(op, num) \
	if (Args.Length() op num) \
	{ \
		Args.Throw(_FL, "%s: Wrong number of args: %i (expecting %i)\n", __func__, Args.Length(), num); \
		*Args.GetReturn() = false; \
		return true; \
	}

LScribeScript *LScribeScript::Inst = 0;

LView *CastLView(LVariant *v)
{
	if (v)
	{
		if (v->Type == GV_DOM)
			return dynamic_cast<LView*>(v->Value.Dom);
		else if (v->Type == GV_LVIEW)
			return v->Value.View;
	}
	return 0;
}

ScribeFolder *CastFolder(LVariant *v)
{
	if (v && v->Type == GV_DOM)	
		return dynamic_cast<ScribeFolder*>(v->Value.Dom);
	return NULL;
}

void LScribeScriptPriv_ConsoleClosingCallback(LScriptConsole *Console, void *UserData);

struct LScribeScriptPriv : public LStream, public LThread
{
	ScribeWnd *App;	
	bool Loop = true;
	LThreadEvent Event;
	
	// Incoming buffer
	LMutex InputLock; // Covers Used and Buffer
	ssize_t Used;
	LArray<char> Buffer;
	
	// Outputs
	LMutex OutputLock; // Covers LogFile, LogMem and Console
	LString LogFile;
	LArray<char> LogMem;
	ssize_t LogMemUsed = 0;
	LScriptConsole *Console = NULL;
	
	LScribeScriptPriv(ScribeWnd *app) :
		LThread("LScribeScriptPriv.Thread"),
		App(app),
		InputLock("LScribeScriptPriv.Input"),
		OutputLock("LScribeScriptPriv.Output")
	{
		LogMem.Length(64 << 10);

		// Get the log path
		if (!(LogFile = LTraceGetFilePath()))
		{
			LAssert(0);
		}
		
		// Setup our buffer first
		Used = 0;
		Buffer.Length(64 << 10);

		// Now register to get trace messages...
		// ScribeInitTraceStore has all the trace logs before this point
		LTraceSetStream(this);

		auto TraceContent = ScribeInitTraceStore.NewLStr();
		Write(TraceContent.Get(), TraceContent.Length());
		
		// Start thread
		Run();
	}
	
	~LScribeScriptPriv()
	{
		Loop = false;
		
		LTraceSetStream(NULL);
		DeleteObj(Console);
		
		Event.Signal();
		while (!IsExited())
			LSleep(true);
	}
	
	/// This method can't block at all, and must accept data from any
	/// thread.
	ssize_t Write(const void *Data, ssize_t Size, int Flags = 0)
	{
		ssize_t Status = 0;
		
		LAssert(Size < (16 << 10));
		
		if (Data && InputLock.LockWithTimeout(500, _FL))
		{
			// Put all data into a buffer as quickly as possible
			ssize_t Remaining = (ssize_t)Buffer.Length() - Used;
			ssize_t Common = MIN(Remaining, Size);
			char *Ptr = &Buffer[Used];
			if (Ptr)
			{
				memcpy(Ptr, Data, Common);
				Status = Common;
				Used += Common;
			}
			
			InputLock.Unlock();
			
			if (Status > 0)
				Event.Signal();
		}
		
		return Status;
	}
	
	void ShowScriptingWindow(bool Show)
	{
		LMutex::Auto Lock(&OutputLock, _FL);
		
		if (Show)
		{
			if (!Console)
			{
				Console = new LScriptConsole(App, LScribeScriptPriv_ConsoleClosingCallback, this);
				if (Console)
					Console->Write(&LogMem[0], LogMemUsed);
			}
		}
		else
		{
			DeleteObj(Console);
		}
	}

	int Main()
	{
		while (Loop)
		{
			Event.Wait();

			if (!Loop) // Shutting down?
				break;

			// Check for input data
			LArray<char> Input;
			if (InputLock.LockWithTimeout(500, _FL))
			{
				if (Used > 0)
				{
					if (Input.Length(Used))
						memcpy(&Input[0], &Buffer[0], Used);
					Used = 0;
				}
				
				InputLock.Unlock();
			}
			
			if (Input.Length())
			{
				// We have some input... send to outputs
				if (OutputLock.LockWithTimeout(500, _FL))
				{
					// Write to our log file...
					LFile f;
					if (LogFile && f.Open(LogFile, O_WRITE))
					{
						f.SetPos(f.GetSize());
						f.Write(&Input[0], Input.Length());
						f.Close();
					}

					if (LogMemUsed + Input.Length() > LogMem.Length())
					{
						// Move data down to make space... by deleting a 1/4 of the data
						// and shifting down the rest.
						auto i = LogMemUsed >> 2;
						
						// Seek to the end of a line
						for (; i < LogMemUsed; i++)
						{
							if (LogMem[i] == '\n')
							{
								i++;
								break;
							}
						}
						
						// Shift the memory down, and update the used variable
						memmove(&LogMem[0], &LogMem[i], LogMemUsed-i);
						LogMemUsed -= i;
						
						// Check there is enough space now...?
						if (LogMemUsed + Input.Length() > LogMem.Length())
							// Nope... make it bigger then
							LogMem.Length(LogMemUsed + Input.Length());
					}

					if (LogMemUsed + Input.Length() < LogMem.Length())
					{
						memcpy(&LogMem[LogMemUsed], &Input[0], Input.Length());
						LogMemUsed += Input.Length();
					}

					if (Console)
					{
						Console->Write((const char*)&Input[0], Input.Length());
					}

					OutputLock.Unlock();
					
					// Tell the UI about the new messages...
					#if LGI_VIEW_HANDLE
					if (App->Handle())
					#endif
						App->PostEvent(M_NEW_CONSOLE_MSG);
				}
			}
		}
		
		return 0;
	}	
};

void LScribeScriptPriv_ConsoleClosingCallback(LScriptConsole *Console, void *UserData)
{
	((LScribeScriptPriv*)UserData)->Console = NULL;
}

LScribeScript::LScribeScript(ScribeWnd *app)
{
	d = new LScribeScriptPriv(app);
	App = app;
	Eng = 0;
}

LScribeScript::~LScribeScript()
{
	DeleteObj(d);
}

void LScribeScript::ShowScriptingWindow(bool show)
{
	d->ShowScriptingWindow(show);
}

LStream *LScribeScript::GetLog()
{
	return d;
}

LAutoString LScribeScript::GetDataFolder()
{
	return App->GetDataFolder();
}

LString LScribeScript::GetIncludeFile(const char *FileName)
{
	LString Path;
	const char *Search[] = {
		"./scripts",
		"../scripts",
		"../../scripts",
		NULL
	};
	LFile::Path p(ScribeResourcePath());
	p += FileName;
	
	for (int i=0; Search[i] && !p.Exists(); i++)
	{
		p = ScribeResourcePath();
		p += Search[i];
		p += FileName;
	}
	
	if (p.Exists())
		Path = p.GetFull();
	else
		Path = LFindFile(FileName);
	
	if (!Path)
	{
		LgiTrace("%s:%i - GetIncludeFile(%s) failed.\n", _FL, FileName);
		return LString();
	}

	LFile f(Path);
	if (!f)
	{
		LgiTrace("%s:%i - GetIncludeFile(%s) couldn't open '%s' for reading.\n", _FL, FileName, Path.Get());
		return LString();
	}

	return f.Read();
}

LHostFunc *LScribeScript::GetCommands()
{
	return Methods;
}

void LScribeScript::SetEngine(LScriptEngine *eng)
{
}

bool LScribeScript::GetFolder(LScriptArguments &Args)
{
	ARG_CHECK(!=, 1);

	Args.GetReturn()->Empty();
	if (Args[0]->Type == GV_INT32)
	{
		ScribeFolder *f = App->GetFolder(Args[0]->Value.Int);
		if (f)
		{
			*Args.GetReturn() = (LDom*)f;
			return true;
		}
	}

	auto path = Args.StringAt(0);
	if (path)
	{
		auto folder = App->GetFolder(path);
		if (folder)
		{
			*Args.GetReturn() = static_cast<LDom*>(folder);
		}
	}

	return true;
}

bool LScribeScript::GetSourceFolders(LScriptArguments &Args)
{
	ARG_CHECK(!=, 1);

	LArray<ScribeFolder*> Folders;
	auto FolderType = Args.Int32At(0);
	Store3ItemTypes ItemType = MAGIC_NONE;
	switch (FolderType)
	{
		case FOLDER_INBOX:
		case FOLDER_OUTBOX:
		case FOLDER_SENT:
		case FOLDER_TRASH:
		case FOLDER_TEMPLATES:
		case FOLDER_SPAM:
			Folders.Add(App->GetFolder(FolderType));
			break;
		case FOLDER_CONTACTS:
			ItemType = MAGIC_CONTACT;
			break;
		case FOLDER_FILTERS:
			ItemType = MAGIC_FILTER;
			break;
		case FOLDER_CALENDAR:
			ItemType = MAGIC_CALENDAR;
			break;
		case FOLDER_GROUPS:
			ItemType = MAGIC_GROUP;
			break;
	}

	if (ItemType != MAGIC_NONE)
		Folders = App->GetThingSources(ItemType);

	auto r = Args.GetReturn();
	if (!r->SetList())
		return false;

	auto Lst = r->Value.Lst;
	for (auto f: Folders)
		Lst->Add(new LVariant((LDom*)f));
	return true;
}

bool LScribeScript::BrowseFolder(LScriptArguments &Args)
{
	ARG_CHECK(<, 4);

	LView *Parent = CastLView(Args[0]);
	LString MessageTxt = Args.StringAt(1);
	LString DefaultFolderName = Args.StringAt(2);
	LString CallbackName = Args.StringAt(3);

	if (!CallbackName)
	{
		*Args.GetReturn() = false;
		return true;
	}

	*Args.GetReturn() = true;

	auto d = new FolderDlg(	Parent,
							App,
							MAGIC_ANY, // limit to
							0, // root
							0, // init sel
							true,
							DefaultFolderName,
							MessageTxt);
	d->DoModal([this, d, CallbackName](auto dlg, auto id)
	{
		if (id)
		{
			auto FolderPath = d->Get();
			auto cb = App->GetCallback(CallbackName);
			if (cb.Func)
			{
				LVirtualMachine Vm;
				LScriptArguments Args(&Vm);
				LVariant vFolderPath = FolderPath;
				Args.Add(&vFolderPath);		
				App->ExecuteScriptCallback(cb, Args);
			}
			else LgiTrace("%s:%i - No callback called '%s'\n", _FL, CallbackName.Get());
		}
	});

	return true;
}

bool LScribeScript::CreateSubFolder(LScriptArguments &Args)
{
	ARG_CHECK(!=, 3);

	*Args.GetReturn() = false;
	ScribeFolder *Parent = CastFolder(Args[0]);
	if (!Parent)
		return true;

	char *ChildName = Args[1]->Str();
	if (!ChildName)
		return true;

	for (ScribeFolder *c = Parent->GetChildFolder(); c; c = c->GetNextFolder())
	{
		auto n = c->GetName(true);
		if (n.Equals(ChildName))
		{
			*Args.GetReturn() = c;
			return true;
		}
	}

	char *TypeStr = Args[2]->Str();
	if (!TypeStr)
		return true;

	int Type = MAGIC_NONE;

	if (_stricmp(TypeStr, "Mail") == 0)
		Type = MAGIC_MAIL;
	else if (_stricmp(TypeStr, "Contact") == 0)
		Type = MAGIC_CONTACT;
	else if (_stricmp(TypeStr, "Filter") == 0)
		Type = MAGIC_FILTER;
	else if (_stricmp(TypeStr, "Calendar") == 0)
		Type = MAGIC_CALENDAR;
	else if (_stricmp(TypeStr, "Group") == 0)
		Type = MAGIC_GROUP;							

	if (Type >= MAGIC_BASE)
	{
		ScribeFolder *Child = Parent->CreateSubFolder(ChildName, Type);
		if (Child)
		{
			*Args.GetReturn() = Child;
			return true;
		}
	}

	return true;
}

bool LScribeScript::SaveThing(LScriptArguments &Args)
{
	ARG_CHECK(!=, 2);

	ScribeFolder *Folder = dynamic_cast<ScribeFolder*>(Args[0]->CastDom());
	Thing *T = dynamic_cast<Thing*>(Args[1]->CastDom());
	*Args.GetReturn() = Folder && T ? T->Save(Folder) : false;

	return true;
}

bool LScribeScript::CreateThing(LScriptArguments &Args)
{
	ARG_CHECK(<, 1);

	Thing *t;
	uint32_t Type = Args[0]->CastInt32();
	ScribeFolder *Folder = Args.Length() > 1 ? dynamic_cast<ScribeFolder*>(Args[1]->CastDom()) : NULL;
	t = App->CreateItem(Type, Folder, false);
	*Args.GetReturn() = dynamic_cast<LDom*>(t);

	return true;
}

bool LScribeScript::MoveThing(LScriptArguments &Args)
{
	ScribeFolder *To = NULL;
	Thing *t = NULL;
	LArray<Thing*> Items;

	ARG_CHECK(!=, 2);

	// Resolve the thing...
	t = dynamic_cast<Thing*>(Args[1]->CastDom());
	if (!t)
	{
		LgiTrace("%s:%i - MoveThing error: Arg 1 not a DOM object.\n", _FL);
		goto MoveThingErr;
	}

	// Resolve the folder...
	if (Args[0]->Type == GV_DOM)
	{
		To = dynamic_cast<ScribeFolder*>(Args[0]->CastDom());
		if (!To)
			LgiTrace("%s:%i - MoveThing error: Arg 0 (object ptr) not a Folder.\n", _FL);
	}
	else if (Args[0]->Type == GV_STRING)
	{
		char *Path = Args[0]->CastString();
		To = t->App->GetFolder(Path);
		if (!To)
			LgiTrace("%s:%i - MoveThing error: Arg 0 ('%s') not a valid path to a folder.\n", _FL, Path);
	}
	if (!To)
		goto MoveThingErr;

	// Move the thing to the folder...
	Items.Add(t);
	To->MoveTo(Items, false);
	*Args.GetReturn() = true;
	return true;

MoveThingErr:
	*Args.GetReturn() = false;
	return true;
}

bool LScribeScript::LoadFolder(LScriptArguments &Args)
{
	ARG_CHECK(!=, 1);

	*Args.GetReturn() = false;
	
	if (Args[0]->Type == GV_DOM)
	{
		ScribeFolder *Folder = dynamic_cast<ScribeFolder*>(Args[0]->Value.Dom);
		if (Folder)
			*Args.GetReturn() = Folder->LoadThings();
	}

	return true;
}

bool LScribeScript::LookupContact(LScriptArguments &Args)
{
	ARG_CHECK(!=, 1);
	auto Email = Args[0]->CastString();
	*Args.GetReturn() = (LDom*) Contact::LookupEmail(Email);
	return true;
}

bool LScribeScript::MsgBox(LScriptArguments &Args)
{
	LViewI *Parent = CastLView(Args[0]);
	LgiMsg(Parent, "The 'MsgBox' function is deprecated. Please use 'MessageDlg' (it has the same arguments).", AppName);
	*Args.GetReturn() = 0;
	return true;
}

bool LScribeScript::GetSystemPath(LScriptArguments &Args)
{
	ARG_CHECK(!=, 1);

	Args.GetReturn()->Empty();
	LSystemPath sp = LSP_TEMP;
	const char *Name; 
	if (Args[0]->IsInt())
		sp = (LSystemPath) Args[0]->CastInt32();
	else if ((Name = Args[0]->CastString()))
	{
		#undef _
		#define _(path) if (!_stricmp(Name, #path)) sp = path;
		_(LSP_ROOT) _(LSP_OS) _(LSP_OS_LIB) _(LSP_TEMP) _(LSP_COMMON_APP_DATA)
		_(LSP_USER_APP_DATA) _(LSP_LOCAL_APP_DATA) _(LSP_DESKTOP) _(LSP_HOME)
		_(LSP_USER_APPS) _(LSP_EXE) _(LSP_TRASH) _(LSP_APP_INSTALL)
		_(LSP_APP_ROOT) _(LSP_USER_DOCUMENTS) _(LSP_USER_MUSIC) _(LSP_USER_VIDEO)
		_(LSP_USER_DOWNLOADS) _(LSP_USER_LINKS)
	}
	else return true;

	LFile::Path p(sp);
	*Args.GetReturn() = p.GetFull();

	return true;
}

bool LScribeScript::GetScribeTempPath(LScriptArguments &Args)
{
	*Args.GetReturn() = ScribeTempPath();
	return true;
}

bool LScribeScript::JoinPath(LScriptArguments &Args)
{
	char p[MAX_PATH_LEN] = "";
	if (Args.Length() > 0)
	{
		bool First = true;
		for (unsigned i=0; i<Args.Length(); i++)
		{
			char *s = Args[i]->CastString();
			if (s)
			{
				if (First)
					strcpy_s(p, sizeof(p), s);
				else
					LMakePath(p, sizeof(p), p, s);
				First = false;
			}
		}
	}

	*Args.GetReturn() = p;
	return true;
}

bool LScribeScript::DeleteThing(LScriptArguments &Args)
{
	*Args.GetReturn() = false;

	LArray<Thing*> a;
	int Ok = 0, Error = 0;
	LDom *dom;
	bool ToTrash = false;

	for (auto Arg: Args)
	{
		if (Arg->Type == GV_DOM)
		{
			if (!(dom = Arg->CastDom())) continue;
			Attachment *attachment = dynamic_cast<Attachment*>(dom);
			if (attachment)
			{
				Mail *m = attachment->GetOwner();
				if (m)
				{
					if (m->DeleteAttachment(attachment)) Ok++;
					else Error++;
				}
				else LAssert(!"No owner?");
			}
			else
			{
				auto t = dynamic_cast<Thing*>(dom);
				if (t) a.Add(t);
			}
		}
		else if (Arg->Type == GV_LIST)
		{
			auto l = Arg->Value.Lst;
			if (!l) continue;
			for (auto i: *l)
			{
				if (!(dom = i->CastDom())) continue;
				auto t = dynamic_cast<Thing*>(dom);
				if (t) a.Add(t);
			}
		}
		else if (Arg->Type == GV_STRING)
		{
			if (Stristr(Arg->Str(), "trash"))
				ToTrash = true;
		}
	}

	while (a.Length() > 0)
	{
		auto t = a[0];
		auto Store = t->GetObject()->GetStore();
		LArray<LDataI*> Del;
		for (auto it = a.begin(); it != a.end(); )
		{
			t = *it;
			if (t->GetObject()->GetStore() == Store)
			{
				Del.Add(t->GetObject());
				a.Delete(it);
			}
			else it++;
		}
		if (Store->Delete(Del, ToTrash) > Store3Error) Ok++;
		else Error++;
	}

	*Args.GetReturn() = Error == 0;
	return true;
}

bool LScribeScript::ShowThingWindow(LScriptArguments &Args)
{
	ARG_CHECK(!=, 1);

	*Args.GetReturn() = false;

	if (Args[0]->Type == GV_DOM)
	{
		Thing *t = dynamic_cast<Thing*>(Args[0]->CastDom());
		if (t)
			*Args.GetReturn() = t->DoUI();
	}

	return true;
}

bool LScribeScript::AddToolsMenuItem(LScriptArguments &Args)
{
	ARG_CHECK(<, 2);

	*Args.GetReturn() = App->RegisterCallback(LToolsMenu, Args);

	return true;
}

bool LScribeScript::AddCallback(LScriptArguments &Args)
{
	ARG_CHECK(<, 2);

	auto Str = Args[0]->CastString();
	ScribeDomType Type = StrToDom(Str);
	switch (Type)
	{
		#define HandleCallbackType(DomField, CbType) \
			case DomField: \
				*Args.GetReturn() = App->RegisterCallback(CbType, Args); \
				break

		HandleCallbackType(SdOnBeforeMailSend, LMailOnBeforeSend);
		HandleCallbackType(SdOnAfterMailReceive, LMailOnAfterReceive);
		HandleCallbackType(SdOnThingContextMenu, LThingContextMenu);
		HandleCallbackType(SdOnFolderContextMenu, LFolderContextMenu);
		HandleCallbackType(SdOnThingToolbar, LThingUiToolbar);
		HandleCallbackType(SdOnApplicationToolbar, LApplicationToolbar);
		HandleCallbackType(SdOnBeforeInstallBar,LBeforeInstallBar);
		HandleCallbackType(SdOnInstallComponent, LInstallComponent);
		HandleCallbackType(SdOnTimer, LOnTimer);
		HandleCallbackType(SdOnRenderMail, LRenderMail);
		HandleCallbackType(SdOnLoad, LOnLoad);
		default:
			Args.Throw(_FL, "No callback named '%s'", Str);
			*Args.GetReturn() = false;
			break;
	}

	return true;
}

bool LScribeScript::RemoveCallback(LScriptArguments& Args)
{
	ARG_CHECK(!= , 1);

	auto Uid = Args.Int32At(0);
	*Args.GetReturn() = App->RemoveCallback(Uid);

	return true;
}

// MenuAddItem(SubMenu, IconIndex/File, LabelText, Position, CallbackMethod, CallbackId)
bool LScribeScript::MenuAddItem(LScriptArguments &Args)
{
	ARG_CHECK(!=, 6);

	*Args.GetReturn() = false;
	
	auto Dom = Args[0]->CastDom();
	auto Menu = dynamic_cast<LScriptUi*>(Dom);
	char *IconFile = 0;
	int IconIdx = -1;
	
	if (Args[1]->Type == GV_INT32)
		IconIdx = Args[1]->CastInt32();
	else if (Args[1]->Type == GV_STRING)
		IconFile = Args[1]->Str();
	
	auto LabelTxt = Args.StringAt(2);
	auto Position = Args.Int32At(3);
	auto CallbackMethod = Args.StringAt(4);
	auto CallbackId = Args.Int32At(5);
	
	if (!Menu || !Menu->Sub)
		return Args.Throw(NULL, -1, "No menu to append to.");

	if (CallbackId < 0)
	{
		Menu->Sub->AppendSeparator(Position);
		*Args.GetReturn() = true;
		return true;
	}

	if (!LabelTxt)
		return Args.Throw(NULL, -1, "No label text.");
	if (!CallbackMethod)
		return Args.Throw(NULL, -1, "No label callback method name.");

	LScriptCallback Cb = App->GetCallback(CallbackMethod);
	if (!Cb.Func)
	{
		Args.Throw(NULL, -1, "Callback not defined.");
		return true;
	}

	auto Top = Menu->Sub;
	while (Top->GetParent() &&
			Top->GetParent()->GetParent())
		Top = Top->GetParent()->GetParent();
	auto Existing = Top->FindItem(CallbackId);
	if (Existing)
		return Args.Throw(NULL, -1, "Item ID '%i' already exists.", CallbackId);

	Cb.Param = CallbackId;
	Menu->Sub->AppendItem(LabelTxt, CallbackId, true, Position);

	// The callbacks are always in a flat list in the top most LScriptCallback,
	// where the caller can easily see them.
	LScriptUi *Root = Menu;
	while (Root->Parent)
		Root = Root->Parent;
	Root->Callbacks.New() = Cb;

	*Args.GetReturn() = true;
	return true;
}

bool LScribeScript::MenuAddSubmenu(LScriptArguments &Args)
{
	ARG_CHECK(!=, 3);
	
	Args.GetReturn()->Empty();
	LScriptUi *Menu = dynamic_cast<LScriptUi*>(Args[0]->CastDom());
	char *SubmenuTxt = Args[1]->Str();
	int Position = Args[2]->CastInt32();
	if (Menu && Menu->Sub && SubmenuTxt)
	{
		LScriptUi *s = new LScriptUi(Menu->Sub->AppendSub(SubmenuTxt, Position));
		if (s)
		{
			s->Parent = Menu;
			Menu->Subs.Add(s);
			*Args.GetReturn() = s;
		}
	}

	return true;
}

bool LScribeScript::ToolbarAddItem(LScriptArguments &Args)
{
	ARG_CHECK(<, 6);
	
	LScriptUi *Tb = dynamic_cast<LScriptUi*>(Args[0]->CastDom());
	int Icon = Args[1]->CastInt32();
	char *Label = Args[2]->Str();
	// int Pos = Args[3]->CastInt32();
	char *CallbackMethodName = Args[4]->Str();
	int CallbackId = Args[5]->CastInt32();
	if (Tb && Tb->Toolbar && CallbackMethodName)
	{
		if (CallbackId < 0)
		{
			Tb->Toolbar->AppendSeparator();
			*Args.GetReturn() = true;
		}
		else
		{
			LScriptCallback Cb = App->GetCallback(CallbackMethodName);
			if (Cb.Func)
			{
				Cb.Param = CallbackId;
				Tb->Toolbar->AppendButton(Label, CallbackId, TBT_PUSH, true, Icon);

				LScriptUi *Root = Tb;
				while (Root->Parent)
					Root = Root->Parent;
				Root->Callbacks.New() = Cb;

				*Args.GetReturn() = true;
			}
			else *Args.GetReturn() = false;
		}
	}
	else
		*Args.GetReturn() = false;

	return true;
}

bool LScribeScript::FilterDoActions(LScriptArguments &Args)
{
	ARG_CHECK(!=, 2);

	Filter *f = dynamic_cast<Filter*>(Args[0]->CastDom());
	Mail *m = dynamic_cast<Mail*>(Args[1]->CastDom());
	if (f && m)
	{
		bool Stop = false;
		f->DoActions(m, Stop);
		*Args.GetReturn() = true;
	}
	else
		*Args.GetReturn() = false;
	
	return true;
}


#define DefFn(Name) \
	LHostFunc(#Name, 0, (ScriptCmd)&LScribeScript::Name)

LHostFunc Methods[] =
{
	DefFn(MsgBox),

	DefFn(GetSystemPath),
	DefFn(GetScribeTempPath),
	DefFn(JoinPath),

	DefFn(GetFolder),
	DefFn(GetSourceFolders),
	DefFn(LoadFolder),
	DefFn(CreateSubFolder),
	DefFn(BrowseFolder),

	DefFn(CreateThing),
	DefFn(MoveThing),
	DefFn(SaveThing),
	DefFn(DeleteThing),
	DefFn(ShowThingWindow),
	DefFn(FilterDoActions),
	DefFn(LookupContact),

	DefFn(AddToolsMenuItem),
	DefFn(AddCallback),
	DefFn(RemoveCallback),

	DefFn(MenuAddItem),
	DefFn(MenuAddSubmenu),
	DefFn(ToolbarAddItem),

	LHostFunc(0, 0, 0),
};

LScriptConsole::LScriptConsole(ScribeWnd *app, ConsoleClosingCallback callback, void *callback_data)
{
	Txt = NULL;
	Callback = callback;
	CallbackData = callback_data;
	App = app;
	LRect r(0, 0, 600, 500);
	SetPos(r);
	if (App)
		MoveSameScreen(App);
	else
		MoveToCenter();
	Name(LLoadString(IDS_CONSOLE, "Console"));

	if (Attach(0))
	{
		Children.Insert(Txt = new LTextLog(100));
		if (Txt)
		{
			Txt->SetPourLargest(true);
		}
		AttachChildren();
		Visible(true);
		RegisterHook(this, LKeyEvents);
	}
}

LScriptConsole::~LScriptConsole()
{
	printf("%p::~LScriptConsole() %p\n", this, Callback);
	if (Callback)
		Callback(this, CallbackData);
}

bool LScriptConsole::OnViewKey(LView *v, LKey &k)
{
	if (k.CtrlCmd() && ToLower(k.c16) == 'w')
	{
		if (k.Down())
			Quit();
		return true;
	}

	return false;
}

bool LScriptConsole::OnRequestClose(bool OsShuttingDown)
{
	LVariant v;
	App->GetOptions()->SetValue(OPT_ShowScriptConsole, v = 0);
	auto Item = App->GetMenu()->FindItem(IDM_SCRIPTING_CONSOLE);
	if (Item) Item->Checked(false);
	if (Callback)
	{
		Callback(this, CallbackData);
		Callback = NULL;
	}

	return true;
}

void LScriptConsole::Write(const char *s, int64 Len)
{
	if (Txt)
		Txt->Write(s, (int)Len);
}

bool OnFilterScript(Filter *f, Mail *m, const char *Script)
{
	if (!Script || !f || !m || !LScribeScript::Inst)
		return false;

	LScriptEngine *e = m->App->GetScriptEngine();
	LAutoPtr<LCompiledCode> obj(new LCompiledCode);
	if (!obj)
	{
		return false;
	}
	
	// Setup the various pre-defined variables
	LVariant v;
	obj->Set("App", v = dynamic_cast<LDom*>(f->App));
	obj->Set("Mail", v = dynamic_cast<LDom*>(m));
	obj->Set("Filter", v = dynamic_cast<LDom*>(f));

	// Compile...
	LString ScriptName;
	ScriptName.Printf("%s.filter", f->GetName());
	if (!e->Compile(obj, NULL, Script, ScriptName))
	{
		f->App->OnScriptCompileError(NULL, f);
		return false;
	}
	
	// And run...
	LExecutionStatus Result = e->Run(obj, NULL, ScribeTempPath());
	return Result != ScriptError;
}

bool OnToolScript(ScribeWnd *App, const char *File)
{
	bool Status = false;

	if (App && File && LScribeScript::Inst)
	{
		auto Script = LReadFile(File);
		if (Script)
		{
			auto e = App->GetScriptEngine();
			if (e)
			{
				LAutoPtr<LCompiledCode> Obj(new LCompiledCode);

				if (e->Compile(Obj, NULL, Script, File))
				{
					LScriptArguments Args(NULL);
					Args.Add(new LVariant((LDom*)App));
					Status = e->CallMethod(Obj, "Main", Args);
					Args.DeleteObjects();
				}
				else
				{
					App->OnScriptCompileError(File, NULL);
				}
			}
		}
	}

	return Status;
}


///////////////////////////////////////////////////////////////////////////
bool LScriptUi::SetupCallbacks(ScribeWnd *App, ThingUi *Parent, Thing *t, LScriptCallbackType Type)
{
	if (!App)
		return false;

	LArray<LScriptCallback*> Callbacks;
	if (!App->GetScriptCallbacks(Type, Callbacks))
		return false;

	LScriptArguments Args(NULL);
	int a = 0;
	Args[a++] = new LVariant(App);
	Args[a++] = new LVariant(this);
	Args[a++] = new LVariant(t);

	for (unsigned i=0; i<Callbacks.Length(); i++)
		App->ExecuteScriptCallback(*Callbacks[i], Args);

	return true;
}

bool LScriptUi::ExecuteCallbacks(ScribeWnd *App, ThingUi *Parent, Thing *t, int Cmd)
{
	if (!App)
		return false;

	LScriptArguments Args(NULL);
	Args[0] = new LVariant(App);	// App
	Args[1] = new LVariant(Parent); // Window
	Args[2] = new LVariant(t);		// Thing
	Args[3] = new LVariant(Cmd);	// CallbackId

	for (auto &c: Callbacks)
	{
		if (c.Param == Cmd)
			App->ExecuteScriptCallback(c, Args);
	}

	return false;
}
