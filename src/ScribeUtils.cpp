#include "Scribe.h"

#include "lgi/common/Http.h"
#include "lgi/common/DocView.h"
#include "lgi/common/Store3.h"
#include "lgi/common/Button.h"
#include "lgi/common/TableLayout.h"
#include "lgi/common/TabView.h"
#include "lgi/common/OpenSSLSocket.h"
#include "lgi/common/LgiRes.h"

#include "ScribeUtils.h"
#include "ScribeDefs.h"
#include "ScribeListAddr.h"
#include "resdefs.h"
#include "../src/common/Coding/ScriptingPriv.h"

#define COMP_FUNCTIONS 1
#include "lgi/common/ZlibWrapper.h"

static char Ws[] = " \t\r\n";

#include "chardet.h"

LString DetectCharset(LString s)
{
	DetectObj *obj = detect_obj_init ();
	if (!obj)
		return LString();

	LString cs;
	if (detect_r(s.Get(), s.Length(), &obj) == CHARDET_SUCCESS &&
		obj->confidence >= 0.75)
		cs = obj->encoding;

	#if 0
	LgiTrace("%s:%i - encoding=%s, obj->confidence=%f, obj->bom=%i, str='%s'\n",
		_FL, obj->encoding, obj->confidence, obj->bom, s.Get());
	#endif
	
	detect_obj_free (&obj);
	return cs;
}

const char *ScribeResourcePath()
{
	static char Res[MAX_PATH_LEN] = {0};
	if (!Res[0])
	{
		#if defined(LINUX)
			// Check for AppImage location
			LFile::Path app(LSP_APP_INSTALL);
			app += "../../usr/share/applications";
			if (app.Exists())
			{
				strcpy_s(Res, sizeof(Res), app.GetFull());
				LgiTrace("%s:%i - Res: %s.\n", _FL, Res);
				return Res;
			}
			// else LgiTrace("%s:%i - Warning: app image resource '%s' doesn't exist.\n", _FL, app.GetFull().Get());
			// else fall through to portable mode
		#elif defined(MAC)
			// Find resource folder in app bundle
			LMakePath(Res, sizeof(Res), LGetExeFile(), "Contents/Resources");
			return Res;
		#endif
		
		#if !defined(MAC)
		const char *Paths[] = {
			"./resources",
			"../resources",
			"../../resources",
		};
		bool Found = false;
		for (unsigned i=0; i<CountOf(Paths); i++)
		{
			// Exe relative mode
			LFile::Path p(LSP_APP_INSTALL);
			p += Paths[i];
			if (p.Exists())
			{
				Found = true;
				strcpy_s(Res, sizeof(Res), p.GetFull());
				break;
			}
		}
		if (!Found)
			LgiTrace("Resource folder: '%s' doesn't exist.\n", Res);

		#ifdef _DEBUG
		if (!Found)
			LAssert(!"Can't find resource folder");
		#endif
		#endif
	}
	return Res;	
}

LString::Array ScribeThemePaths()
{
	LString::Array r;
	
	LFile::Path ro(ScribeResourcePath());
	ro += "Themes";
	if (ro.Exists())
		r.Add(ro.GetFull());
	
	LFile::Path rw(LSP_APP_ROOT);
	rw += "Themes";
	if (rw.Exists())
		r.Add(rw.GetFull());
	
	return r;
}

LString AskOverwriteMsg(const char *FileName)
{
	LString a, b;
	a.Printf(LLoadString(IDS_ERROR_FILE_EXISTS), FileName);
	b.Printf("\n%s\n", LLoadString(IDS_ERROR_FILE_OVERWRITE));
	a += b;
	return a;
}

char *RemoveAmp(const char *s)
{
	static char b[256];

	if (!s)
		return NULL;


	char *r = b;
	while (*s && r < b + sizeof(b) - 1)
	{
		if (s[0] != '&' || s[1] == '&')
			*r++ = *s;
		s++;
	}
	*r++ = 0;
	return b;
}

LString AddAmp(const char *menu, int shortcut)
{
	LString s = menu;
	for (unsigned i=0; i<s.Length(); i++)
	{
		if (ToLower(s(i)) == ToLower(shortcut))
		{
			LString m = s(0,i) + "&" + s(i,-1);
			return m;
		}
	}

	return s;
}

void PushVariant(LStringPipe &p, LVariant &v)
{
	switch (v.Type)
	{
		default:
			break;
		case GV_STRING:
		{
			p.Push(v.Str());
			break;
		}
		case GV_INT32:
		{
			char i[32];
			sprintf_s(i, sizeof(i), "%i", v.Value.Int);
			p.Push(i);
			break;
		}
		case GV_DOUBLE:
		{
			char d[32];
			sprintf_s(d, sizeof(d), "%f", v.Value.Dbl);
			p.Push(d);
			break;
		}
		case GV_DATETIME:
		{
			char d[64];
			v.Value.Date->Get(d, sizeof(d));
			p.Push(d);
			break;
		}
	}
}

void PushArrayContent(LStringPipe &p, char *&s, LDom *Source)
{
	// Process inside of array index
	while (s && *s)
	{
		// Skin ws
		while (*s && strchr(Ws, *s)) s++;
		
		// Is end of array brackets?
		if (*s == ']')
		{
			break;
		}
		else if (*s == '\'' || *s == '\"')
		{
			// String const
			char Delim = *s++;
			char *e = strchr(s, Delim);
			if (e)
			{
				p.Push(s, e-s);
				s = e + 1;
			}
			else
			{
				s += strlen(s);
				break;
			}
		}
		else
		{
			// Variable
			LStringPipe Var;
			char *e = s;
			int Depth = 0;
			while (*e && !strchr(Ws, *e))
			{
				if (*e == '[')
				{
					e++;
					Var.Push(s, e-s);
					PushArrayContent(Var, e, Source);
					s = e;
					Depth++;
				}
				else if (*e == ']')
				{
					if (Depth > 0 || e[1] == '.')
					{
						// Continue the variable
						if (Depth) Depth--;
						e++;
					}
					else
					{
						// End the var
						break;
					}
				}
				else
				{
					e++;
				}
			}
			Var.Push(s, e-s);
			
			char *Tok = Var.NewStr();
			if (Tok)
			{
				LVariant v;
				if (Source->GetValue(Tok, v))
				{
					PushVariant(p, v);
				}
				else
				{
					p.Push(Tok);
				}
				DeleteArray(Tok);
			}
			
			s = e;
		}
	}
}

char *ScribeInsertFields(const char *Template, LDom *Source)
{
	if (Template && Source)
	{
		LStringPipe p;

		char *n;
		for (const char *s=Template; s && *s; s = n)
		{
			n = strstr((char*)s, "<?");
			if (n)
			{
				// Output what is before this point
				p.Push(s, n-s);

				// Find variable name and write it
				n += 2;
				
				// Skip ws
				while (*n && strchr(Ws, *n)) n++;
				
				LStringPipe Var;
				bool GotEnd = false;
				for (char *s = n; s && *s; )
				{
					char *e = s;
					while (*e)
					{
						if
						(
							(*e == '[')
							||
							(e[0] == '?' && e[1] == '>')
							||
							strchr(Ws, *e)
						)
						{
							break;
						}
						
						e++;
					}
					
					if (*e == '[')
					{
						e++;
						Var.Push(s, e-s);
						
						// Process inside of array index
						PushArrayContent(Var, e, Source);
					}
					else if (strchr(Ws, *e))
					{
						// Skip whitespace
						Var.Push(s, e-s);
						while (*e && strchr(Ws, *e))
						{
							e++;
						}
					}
					else if (e[0] == '?' && e[1] == '>')
					{
						// End of var
						Var.Push(s, e-s);
						GotEnd = true;
						n = e;
						break;
					}
					else
					{
						// error
						n = e;;
						break;
					}
					
					s = e;
				}
				
				if (GotEnd)
				{
					char *Name = Var.NewStr();
					if (Name)
					{
						char *i = Name, *o = Name;
						while (*i)
						{
							if (*i == '=')
							{
								i++;
								char h[] = {i[0], i[1], 0};
								*o++ = htoi(h);
								i += 2;
							}
							else
							{
								*o++ = *i++;
							}
						}
						*o++ = 0;

						LVariant v;
						if (Source->GetValue(Name, v))
						{
							switch (v.Type)
							{
								default: break;
								case GV_STRING:
								{
									p.Push(v.Str());
									break;
								}
								case GV_INT32:
								{
									char i[32];
									sprintf_s(i, sizeof(i), "%i", v.Value.Int);
									p.Push(i);
									break;
								}
								case GV_DOUBLE:
								{
									char d[32];
									sprintf_s(d, sizeof(d), "%f", v.Value.Dbl);
									p.Push(d);
									break;
								}
								case GV_DATETIME:
								{
									char d[64];
									v.Value.Date->Get(d, sizeof(d));
									p.Push(d);
									break;
								}
							}
						}

						DeleteArray(Name);
					}
					n += 2;
				}
			}
			else
			{
				p.Push(s);
				break;
			}
		}

		return p.NewStr();
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////////////////
HttpImageThread::HttpImageThread(ScribeWnd *app, const char *proxy, LThreadTarget *First) :
	LThreadWorker(First, "HtmlImageLoader")
{
	App = app;
	Proxy = proxy;
	Cache = ScribeTempPath();
	if (!LDirExists(Cache))
		FileDev->CreateFolder(Cache);
}

HttpImageThread::~HttpImageThread()
{
}

void HttpImageThread::DoJob(LThreadJob *j)
{
	LDocumentEnv::LoadJob *Job = dynamic_cast<LDocumentEnv::LoadJob*>(j);
	if (!Job)
		return;

	char *d = strrchr(Job->Uri, '/');
	if (!d)
	{
		Job->Status = LDocumentEnv::LoadJob::JobErr_Uri;
		Job->Error.Printf("No '/' in uri '%s'", Job->Uri.Get());
		return;
	}

	LString CachedFile = UriMap.Find(Job->Uri);
	if (!CachedFile)
	{
		char *Ext = LGetExtension(++d);
		auto Qm = Ext ? strchr(Ext, '?') : NULL;
		auto Len = Qm ? Qm - Ext : Strlen(Ext);
		char p[MAX_PATH_LEN];

		for (int i=0; i<1000; i++)
		{
			auto Hash = LString::Fmt("%x_%i.%.*s", LHash<uint32_t, uchar>((uchar*)d + 1, -1, true), i++, (int)Len, Ext);
			if (!LMakePath(p, sizeof(p), Cache, Hash))
			{
				Job->Status = LDocumentEnv::LoadJob::JobErr_Path;
				Job->Error.Printf("MakePath failed: '%s' + '%s'", Cache.Get(), Hash.Get());
				return;
			}
			if (!LFileExists(p))
				break;
		}

		UriMap.Add(Job->Uri, CachedFile = p);
	}

	if (!LFileExists(CachedFile))
	{
		const char *InHeaders = "User-Agent: Memecode Scribe\r\n"
								"Accept: text/html,application/xhtml+xml,application/xml,image/png,image/*;q=0.9,*/*;q=0.8\r\n"
								"Accept-Encoding: gzip, deflate\r\n";

		LFile f;
		if (f.Open(CachedFile, O_READWRITE))
		{
			LUri Prox(Proxy);			
			bool r = LgiGetUri(this, &f, &Job->Error, Job->Uri, InHeaders, Proxy ? &Prox : NULL);
			f.Close();
			if (!r)
			{
				Job->Status = LDocumentEnv::LoadJob::JobErr_GetUri;
				FileDev->Delete(CachedFile, NULL, false);
			}
		}
		else
		{
			Job->Status = LDocumentEnv::LoadJob::JobErr_FileOpen;
		}
	}
	
	if (LFileExists(CachedFile))
	{
		LString::Array Mime = LGetFileMimeType(CachedFile).Split("/");
		if (Mime[0].Equals("image"))
		{
			int Promote = GdcD->SetOption(GDC_PROMOTE_ON_LOAD, 0);
			Job->pDC.Reset(GdcD->Load(CachedFile));
			GdcD->SetOption(GDC_PROMOTE_ON_LOAD, Promote);
			if (Job->pDC)
			{
				Job->Status = LDocumentEnv::LoadJob::JobOk;
			}
			else
			{
				char *d = strrchr(CachedFile, DIR_CHAR);
				Job->Error.Printf("%s:%i - LoadDC(%s) failed [%s].", _FL, d?d+1:CachedFile.Get(), Job->Uri.Get());
				FileDev->Delete(CachedFile, NULL, false);
				Job->Status = LDocumentEnv::LoadJob::JobErr_ImageFilter;
			}
		}
		else
		{
			// Css???
			LFile *f = new LFile;
			if (f)
			{
				if (f->Open(CachedFile, O_READ))
				{
					Job->Stream.Reset(f);
					Job->Status = LDocumentEnv::LoadJob::JobOk;
				}
				else
				{
					Job->Status = LDocumentEnv::LoadJob::JobErr_FileOpen;
					Job->Error.Printf("%s:%i - Cant read from '%s' (err=%i).", _FL, CachedFile.Get(), f->GetError());
					delete f;
				}
			}
			else
				Job->Status = LDocumentEnv::LoadJob::JobErr_NoMem;
		}
	}
	else if (!Job->Error)
	{
		Job->Status = LDocumentEnv::LoadJob::JobErr_NoCachedFile;
		Job->Error = "No file in cache";
	}
	
	if (Job->Error)
	{
		LgiTrace("Image load failed: %s\n", Job->Error.Get());
	}
}

char *ScribeTempPath()
{
	static char Tmp[MAX_PATH_LEN] = "";

	if (Tmp[0] == 0)
	{
		if (LGetSystemPath(LSP_TEMP, Tmp, sizeof(Tmp)))
		{
			LMakePath(Tmp, sizeof(Tmp), Tmp, "Scribe");
		}
		else
		{
			LgiTrace("%s:%i - LgiGetSystemPath(LSP_TEMP) failed.\n", _FL);
			return NULL;
		}
	}

	if (!LDirExists(Tmp))
	{
		LError Err;
		if (!FileDev->CreateFolder(Tmp, true, &Err))
		{
			LgiTrace("%s:%i - CreateFolder(%s) failed with %i\n", _FL, Tmp, Err.GetCode());
			return NULL;
		}
	}

	return Tmp;
}

void ClearTempPath()
{
	char *Tmp = ScribeTempPath();
	if (Tmp)
	{
		if (!LDirExists(Tmp))
			FileDev->CreateFolder(Tmp);

		LDirectory d;
		for (int b = d.First(Tmp); b; b = d.Next())
		{
			if (!d.IsDir())
			{
				char p[256];
				d.Path(p, sizeof(p));
				FileDev->Delete(p, NULL, false);
			}
		}
	}	
}

////////////////////////////////////////////////////////////////////////////////////////////////
#define BufferLen_64ToBin(l)		( ((l)*3)/4 )
#define BufferLen_BinTo64(l)		( ((((l)+2)/3)*4) )

int DecodeUuencodedChar(const char *&s)
{
	int Status = -1;

	if (*s == 0x60)
	{
		Status = 0;
		s++;
	}
	else if (*s >= (' ' + 64) || *s < ' ')
	{
		printf("%s:%i - Invalid uuencode char: %c (%i)\n",
			_FL, *s, (uchar)*s);
	}
	else
	{
		Status = *s - ' ';
		s++;
	}
	
	return Status;
}

bool DecodeUuencodedLine(LStreamI *Out, const char *Text, ssize_t Len)
{
	bool Status = false;
	if (Text && Len > 1)
	{
		uchar *Buf = new uchar[Len];
		if (Buf)
		{
			const char *End = Text + Len;
			const char *c = Text;
			uchar *d = Buf;
			int Count = DecodeUuencodedChar(c);
			int Processed = 0;
			
			if (Count < 0)
				return false;

			while (c < End && *c)
			{
				int t[4];

				// De-text
				t[0] = DecodeUuencodedChar(c);
				if (t[0] < 0) break;
				t[1] = DecodeUuencodedChar(c);
				if (t[1] < 0) break;
				t[2] = DecodeUuencodedChar(c);
				if (t[2] < 0) break;
				t[3] = DecodeUuencodedChar(c);
				if (t[3] < 0) break;
				
				// Convert to binary
				uchar b[3] = {
					(uchar) ((t[0] << 2) | ((t[1] & 0x30) >> 4)),
					(uchar) (((t[1] & 0xF) << 4) | ((t[2] & 0x3C) >> 2)),
					(uchar) (((t[2] & 0x3) << 6) | (t[3]))
				};

				// Push onto the output stream
				switch (Count - Processed)
				{
					case 1:
					{
						*d++ = b[0];
						Processed++;
						break;
					}
					case 2:
					{
						*d++ = b[0];
						*d++ = b[1];
						Processed += 2;
						break;
					}
					default:
					{
						if (Count - Processed >= 3)
						{
							*d++ = b[0];
							*d++ = b[1];
							*d++ = b[2];
							Processed += 3;
						}
						break;
					}
				}
			}
			
			if (Processed != Count)
			{
				printf("%s:%i - uuencode line error, processed %i of %i\n",
					_FL,
					Processed, Count);
			}

			Status = Out->Write(Buf, d-Buf) > 0;

			DeleteArray(Buf);
		}
	}
	return Status;
}

bool DecodeUuencodedAttachment(LDataStoreI *Store, LArray<LDataI*> &Files, LStreamI *Out, const char *In)
{
	if (Store && In)
	{
		// const char Ws[] = " \t\r\n";
		LStringPipe FileName;
		LAutoPtr<LStringPipe> FileData;
		const char *e;
		const char *Last = In;
		int Line = 1;

		for (const char *s = In; s && *s; s = *e?e+1:e)
		{
			// Find the end of the line...
			e = s;
			while (*e && *e != '\n')
				e++;

			if (FileData)
			{
				if (_strnicmp(s, "end", 3) == 0)
				{
					// Write attachment
					LDataI *Attachment = Store->Create(MAGIC_ATTACHMENT);
					if (Attachment)
					{
						LAutoString Name(FileName.NewStr());
						if (Name)
						{
							char *e = Name + strlen(Name);
							while (e > Name.Get() && strchr(" \t\r\n", e[-1]))
								*--e = 0;
							Attachment->SetStr(FIELD_NAME, Name);
						}
						
						LAutoStreamI fd(FileData.Release());
						Attachment->SetStream(fd);
						Files.Add(Attachment);
					}
					
					FileData.Reset();
				}
				else if (!DecodeUuencodedLine(FileData, s, e - s))
				{
					/*
					printf("%s:%i - DecodeUuencodedLine failed on line %i:\n\t%s\n",
						_FL, Line, s);
					*/
				}
			}
			// Is it the start of a file
			else if (_strnicmp(s, "begin ", 6) == 0)
			{
				if (Last)
				{
					Out->Write(Last, s - Last);
					Last = 0;
				}

				auto Header = LString(s, e - s).SplitDelimit(" ", -1, true);
				if (Header.Length() >= 3)
				{
					LMemQueue File;
					for (int n=2; Header[n]; n++)
					{
						FileName.Print("%s%s", n==2?"":" ", Header[n].Get());
					}
				}

				FileData.Reset(new LStringPipe(256));
			}
			else if (!Last)
			{
				Last = s;
			}

			Line++;
		}

		if (Files.Length() && Last)
		{
			Out->Write(Last, strlen(Last));
		}
	}

	return Files.Length() > 0;
}

char *MakeFileName(const char *ContentUtf, const char *Ext)
{
	if (!ContentUtf)
	{
		LAssert(!"Invalid parameter.");
		return 0;
	}

	char *Content = 0;
	if (LIsUtf8(ContentUtf))
	{
		// Valid UTF-8
		Content = NewStr(ContentUtf);
	}
	else
	{
		// Garbage, so just ignore the input data.
		char n[256];
		sprintf_s(n, sizeof(n), "_%i", LRand(1000000));
		Content = NewStr(n);
	}

	if (!Content)
	{
		LAssert(!"No content to make filename from.");
		return 0;
	}

	char File[MAX_PATH_LEN];

	char *e = Content;
	for (int i=0; i<64 && *e; i++)
	{
		char *before = e;
		e = LSeekUtf8(e, 1);
		if (e == before)
		{
			LAssert(!"LSeekUtf8 failed to more pointer forward.");
			break;
		}
	}
	*e = 0;

	if (strlen(Content) > 0)
	{
		if (Ext)
			sprintf_s(File, sizeof(File), "%s.%s", Content, Ext);
		else
			sprintf_s(File, sizeof(File), "%s", Content);
	}
	else
	{
		LAssert(!"No content for file name?");
		strcpy_s(File, sizeof(File), "file");
	}
	
	// Strip out invalid characters...
	char *Out = File;
	for (char *In = File; *In; In++)
	{
		if (!strchr("\\/?*:\"<>|\r\n", *In))
		{
			*Out++ = *In;
		}
	}
	*Out++ = 0;

	LAssert(strlen(File) > 0);

	char Temp[MAX_PATH_LEN];
	LMakePath(Temp, sizeof(Temp), ScribeTempPath(), File);
	
	if (LFileExists(Temp))
	{
		char *Dot = strrchr(Temp, '.');
		for (int i=2; LFileExists(Temp); i++)
		{
			ssize_t Len = Dot - Temp;
			sprintf_s(Dot, sizeof(Temp)-Len, "%i.%s", i, Ext);
		}
	}

	DeleteArray(Content);

	return NewStr(Temp);
}

///////////////////////////////////////////////////////////////////////////////////////////
Store3Progress::Store3Progress(LView *parent, bool interact) : LProgressDlg(parent)
{
	Interact = interact;
	NewFormat = -1;
}

const char *Store3Progress::GetStr(int id)
{
	switch (id)
	{
	case Store3UiError:
		return Err;
	case Store3UiStatus:
		return (Cache = ItemAt(0)->GetDescription());
	}

	LAssert(0);
	return 0;
}

Store3Status Store3Progress::SetStr(int id, const char *str)
{
	switch (id)
	{
	case Store3UiError:
		Err = str;
		// Fall through
	case Store3UiStatus:
		ItemAt(0)->SetDescription(str);
		return Store3Success;
	}

	LAssert(0);
	return Store3Error;
}

int64 Store3Progress::GetInt(int id)
{
	switch (id)
	{
	case Store3UiCurrentPos:
		return ItemAt(0)->Value();
	case Store3UiInteractive:
		return Interact;
	case Store3UiCancel:
		return IsCancelled();
	case Store3UiNewFormat:
		return NewFormat;
	}

	LAssert(0);
	return -1;
}

Store3Status Store3Progress::SetInt(int id, int64 i)
{
	switch (id)
	{
	case Store3UiCancel:
		return Store3Error;
	case Store3UiCurrentPos:
		ItemAt(0)->Value(i);
		break;
	case Store3UiMaxPos:
		ItemAt(0)->SetRange(i);
		break;				
	case Store3UiNewFormat:
		NewFormat = (int)i;
		break;
	default:
		LAssert(0);
		return Store3Error;
	}
	
	return Store3Success;
}


///////////////////////////////////////////////////////////////////////
class BufferedTrace
{
	List<char> Traces;

public:
	~BufferedTrace()
	{
		for (auto s: Traces)
		{
			LgiTrace(s);
			DeleteArray(s);
		}
	}

	void Trace(char *s)
	{
		if (s)
		{
			Traces.Insert(NewStr(s));
		}
	}
} ;

static BufferedTrace Bt;

void TraceTime(char *s)
{
	static int64 Last = 0;
	if (s)
	{
		int64 Now = LCurrentTime();
		int64 Diff = 0;
		if (Last)
		{
			Diff = Now - Last;
		}
		else
		{
			Diff = 0;
		}
		Last = Now;

		char m[256];
		sprintf_s(m, sizeof(m), "%s (+%i)", s, (int)Diff);
		Bt.Trace(m);
	}
	else
	{
		Last = 0;
	}
}

/////////////////////////////////////////////////////////////////////////////
Counter::~Counter()
{
	for (auto c: *this)
	{
		DeleteObj(c);
	}
}

CountItem *Counter::FindType(int Type)
{
	for (auto c: *this)
	{
		if (Type == c->Type)
		{
			return c;
		}
	}

	CountItem *c = new CountItem;
	if (c)
	{
		c->Type = Type;
		Insert(c);
	}

	return c;
}

void Counter::Inc(int Type)
{
	CountItem *c = FindType(Type);
	if (c)
	{
		c->Count++;
	}
}

void Counter::Dec(int Type)
{
	CountItem *c = FindType(Type);
	if (c)
	{
		c->Count--;
	}
}

void Counter::Add(int Type, int64 n)
{
	CountItem *c = FindType(Type);
	if (c)
	{
		c->Count += n;
	}
}

void Counter::Sub(int Type, int64 n)
{
	CountItem *c = FindType(Type);
	if (c)
	{
		c->Count -= n;
	}
}

int64 Counter::GetTypeCount(int Type)
{
	CountItem *c = FindType(Type);
	if (c)
	{
		return c->Count;
	}
	return 0;
}

//////////////////////////////////////////////////////////
ItemFieldDef *ScribeGetFieldDefs(int Type)
{
	switch ((uint32_t)Type)
	{
		case MAGIC_MAIL:
		{
			return MailFieldDefs;
		}
		case MAGIC_CONTACT:
		{
			return ContactFieldDefs;
		}
		case MAGIC_CALENDAR:
		{
			return CalendarFields;
		}
	}

	return 0;
}

Contact *IsContact(LListItem *Item)
{
	return dynamic_cast<Contact*>(Item);
}

Mail *IsMail(LListItem *Item)
{
	return dynamic_cast<Mail*>(Item);
}

//////////////////////////////////////////////////////////////////////////////
char sMimeVCard[] = "text/x-vcard";
char sMimeVCalendar[] = "text/calendar";
char sMimeICalendar[] = "application/ics";
char sMimeMbox[] = "text/mbox";
char sMimeLgiResource[] = "application/x-lgi-resource";
char sMimeMessage[] = "message/rfc822";
char sMimeXml[] = "text/xml";

LString ScribeGetFileMimeType(const char *File)
{
	LString Ret;
	if (!File)
		return Ret;

	auto Ext = LGetExtension(File);
	if (Ext)
	{
		if (_stricmp(Ext, "lr8") == 0)
		{
			Ret = sMimeLgiResource;
		}
		else if (_stricmp(Ext, "ici") == 0)
		{
			Ret = "application/x-ici";
		}
		else if (_stricmp(Ext, "vcf") == 0)
		{
			Ret = sMimeVCard;
		}
		else if (_stricmp(Ext, "vcs") == 0 ||
					_stricmp(Ext, "ics") == 0)
		{
			Ret = sMimeVCalendar;
		}
		else if (_stricmp(Ext, "eml") == 0)
		{
			Ret = sMimeMessage;
		}
		#if defined WIN32
		// Hard code extensions (because windows doesn't get it right)
		else if (_stricmp(Ext, "mbx") == 0 ||
					_stricmp(Ext, "mbox") == 0)
		{
			Ret = sMimeMbox;
		}
		#endif
	}

	if (!Ret)
	{
		// Do normal lookup
		Ret = LGetFileMimeType(File);
	}

	return Ret;
}

/////////////////////////////////////////////////////////////////////
Mailto::Mailto(ScribeWnd *app, const char *s)
{
	App = app;
	Subject = NULL;
	Body = NULL;

	if (!s)
		return;

	// Do some detection of what type of string this is...
	//
	// Could be in various formats:
	//	1. user@isp.com
	//	2. user@isp.com, user2@isp.com, user3@isp.com
	//  3. "First Last" <user@isp.com>
	//  4. "First Last" <user@isp.com>, "First2 Last2" <user2@isp.com>
	//  5. mailto:user@isp.com
	//  6. mailto:user@isp.com?subject=xxxxxx&body=xxxxxxxx

	// Skip whitespace
	while (*s && strchr(" \t\r\n", *s)) s++;

	// Check for mailto prefix
	if (_strnicmp(s, "mailto:", 7) == 0)
	{
		// Parse mailto URI
		char *e = NewStr(s + 7);

		char *In, *Out = e;
		for (In = e; *In; )
		{
			if (In[0] == '%' &&
				In[1] &&
				In[2])
			{
				char h[3] = { In[1], In[2], 0 };
				*Out++ = htoi(h);
				In += 3;
			}
			else
			{
				*Out++ = *In++;
			}
		}
		*Out++ = 0;

		// Process mailto syntax
		char *Question = strchr(e, '?');
		if (Question)
		{
			*Question++ = 0;

			// Split all the headers up
			auto Headers = LString(Question).SplitDelimit("&");
			for (unsigned h=0; h<Headers.Length(); h++)
			{
				char *Header = Headers[h];
				if (Header)
				{
					// Split the header into name, value pairs
					char *Value = strchr(Header, '=');
					if (Value)
					{
						*Value++ = 0;

						if (ValidStr(Header) && ValidStr(Value))
						{
							// Both parts of the header are sane
							if (_stricmp(Header, "Subject") == 0)
							{
								// Subject
								Subject = LDecodeUri(Value);
							}
							else if (_stricmp(Header, "Body") == 0)
							{
								// Body
								Body = LDecodeUri(Value);
							}
							else if (_stricmp(Header, "To") == 0)
							{
								char *s = LDecodeUri(Value);
								if (s)
								{
									// SetRecipients(m->Text, m->To, 0);
									ListAddr *la = new ListAddr(App);
									if (la)
									{
										la->sAddr = s;
										To.Insert(la);
									}
									DeleteArray(s);
								}
							}
							// else unknown header???
						}
					}
				}
			}
		}

		// Anything left over in 'Str' is a recipient
		if (ValidStr(e))
		{
			// add recipients
			ListAddr *la = new ListAddr(App);
			if (la)
			{
				la->sAddr = e;
				To.Insert(la);
			}
		}

		DeleteArray(e);
	}
	else
	{
		// Not a mailto, apply normal email recipient parsing
		char White[] = " \t\r\n";
		#define SkipWhite(s) while (*s && strchr(White, *s)) s++;

		const char *Addr = s;
		for (const char *c = s; true;)
		{
			SkipWhite(c);
			if (*c == '\'' || *c == '\"')
			{
				char Delim = *c++;
				char *e = strchr((char*)c, Delim);
				if (e) c = e + 1;
				else c += strlen(c);
			}
			else if (*c == '<')
			{
				char *e = strchr((char*)c, '>');
				if (e) c = e + 1;
				else c++;
			}
			else if (*c == ',' || *c == 0)
			{
				char *a = NewStr(Addr, c - Addr);
				if (a)
				{
					LAutoString Name, Addr;
					DecodeAddrName(a, Name, Addr, 0);
					if (Name || Addr)
					{
						ListAddr *la = new ListAddr(App);
						if (la)
						{
							if (Name && Addr)
							{
								la->sName = Name.Get();
								la->sAddr = Addr.Get();
							}
							else
							{
								la->sAddr = Name ? Name.Get() : Addr.Get();
							}
							To.Insert(la);
						}
					}

					DeleteArray(a);
				}

				if (!*c)
					break;
				else
				{
					c++;
					SkipWhite(c);
					Addr = c;
				}
			}
			else c++;
		}
	}
}

Mailto::~Mailto()
{
	To.DeleteObjects();
	DeleteArray(Subject);
	DeleteArray(Body);
}

void Mailto::Apply(Mail *m)
{
	if (m)
	{
		bool Dirty = false;

		if (Subject)
		{
			m->SetSubject(Subject);
			Dirty = true;
		}

		if (Body)
		{
			LVariant HtmlEdit;
			m->App->GetOptions()->GetValue(OPT_EditControl, HtmlEdit);

			auto Email = m->GetFromStr(FIELD_EMAIL);
			ScribeAccount *Acc = m->App->GetAccountByEmail(Email);
			if (!Acc)
				Acc = m->App->GetCurrentAccount();
			LVariant Sig;
			LString Content;
			if (Acc)
			{
				if (HtmlEdit.CastInt32())
				{
					Sig = Acc->Identity.HtmlSig();
					if (Sig.Str())
					{
						char *s = Sig.Str();
						char *e = stristr(s, "<body>");
						if (e)
							Content.Printf("%.*s\n%s\n%s", e - s, s, Body, e + 6);
						else
							Content.Printf("%s\n%s", Body, s);
					}
					else
						Content = Body;
				}
				else
				{
					Sig = Acc->Identity.TextSig();
					Content.Printf("%s\n%s", Body, Sig.Str());
				}
			}

			if (HtmlEdit.CastInt32())
				m->SetHtml(Content);
			else
				m->SetBody(Content);
			
			Dirty = true;
		}

		for (auto t: To)
		{
			LDataIt To = m->GetObject()->GetList(FIELD_TO);
			if (To)
			{
				LDataPropI *Addr = To->Create(m->GetObject()->GetStore());
				if (Addr)
				{
					LDataPropI *p = dynamic_cast<LDataPropI*>(t);
					if (p)
					{
						Addr->CopyProps(*p);
						To->Insert(Addr);
					}
					else
					{
						LAssert(!"Not the right object.");
						DeleteObj(Addr);
					}
				}

				if (m->GetUI())
				{
					m->GetUI()->AddRecipient(new ListAddr(App, t));
				}
				Dirty = true;
			}
		}

		if (Dirty)
			m->SetDirty();
	}
}

ScribeDom::ScribeDom(ScribeWnd *a)
{
	App = a;
	Email = NULL;
	Con = NULL;
	Cal = NULL;
	Fil = NULL;
	Grp = NULL;
}

bool ScribeDom::GetVariant(const char *Name, LVariant &Value, const char *Array)
{
	ScribeDomType Fld = StrToDom(Name);
	switch (Fld)
	{
		case SdScribe: // Type: ScribeWnd
		{
			Value = (LDom*)App;
			break;
		}
		case SdMail: // Type: Mail
		{
			Value = Email;
			break;
		}
		case SdContact: // Type: Contact
		{
			Value = Con;
			break;
		}
		case SdContactGroup: // Type: ContactGroup
		{
			Value = Grp;
			break;
		}
		case SdCalendar: // Type: Calendar
		{
			Value = Cal;
			break;
		}
		case SdFilter: // Type: Filter
		{
			Value = Fil;
			break;
		}
		case SdNow: // Type: String
		{
			char n[256];
			LDateTime Now;
			Now.SetNow();
			Now.Get(n, sizeof(n));
			Value = n;
			break;
		}
		default:
		{
			return false;
		}
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////////////////
#include "lgi/common/Html.h"
#include "lgi/common/Button.h"

class HtmlMsg : public LDialog, public LDefaultDocumentEnv
{
	LTableLayout *Tbl = NULL;
	Html1::LHtml *Html2 = NULL;

public:
	HtmlMsg(LViewI *Parent, const char *Html, const char *Title, int Type)
	{
		LPoint Size(300, 300);
		SetParent(Parent);
		Name(Title?Title:"Message");
		
		AddView(Tbl = new LTableLayout(2222));
		auto c = Tbl->GetCell(0, 0);
		if (c->Add(Html2 = new Html1::LHtml(100, 0, 0,
									  (int)(GdcD->X() * 0.5),
									  (int)(GdcD->Y() * 0.75),
									  this)))
		{
			Html2->SetCharset("utf-8");
			Html2->Name(Html);

			/*
			Size = Html2->Layout();
			LRect r(0, 0, Size.x, Size.y);
			Html2->SetPos(r);
			*/
		}

		LArray<LButton*> Btns;
		switch (Type & 0xf)
		{
			case MB_OK:
				Btns.Add(new LButton(IDOK, 0, 0, -1, -1, "Ok"));
				break;
			case MB_OKCANCEL:
				Btns.Add(new LButton(IDOK, 0, 0, -1, -1, "Ok"));
				Btns.Add(new LButton(IDCANCEL, 0, 0, -1, -1, "Cancel"));
				break;
			case MB_YESNO:
				Btns.Add(new LButton(IDYES, 0, 0, -1, -1, "Yes"));
				Btns.Add(new LButton(IDNO, 0, 0, -1, -1, "No"));
				break;
			case MB_YESNOCANCEL:
				Btns.Add(new LButton(IDYES, 0, 0, -1, -1, "Yes"));
				Btns.Add(new LButton(IDNO, 0, 0, -1, -1, "No"));
				Btns.Add(new LButton(IDCANCEL, 0, 0, -1, -1, "Cancel"));
				break;
		}

		c = Tbl->GetCell(0, 1);
		c->TextAlign(LCss::AlignCenter);

		for (auto b: Btns)
			c->Add(b);
		
		LRect r(0, 0,
				Size.x + 20 + LAppInst->GetMetric(LGI_MET_DECOR_X),
				Size.y + 20 + LSysFont->GetHeight() + LAppInst->GetMetric(LGI_MET_DECOR_CAPTION) + LAppInst->GetMetric(LGI_MET_DECOR_Y));
		SetPos(r);
		MoveSameScreen(Parent);
	}

	void OnPosChange()
	{
		if (Tbl)
			Tbl->SetPos(GetClient());
	}
	
	int OnNotify(LViewI *Ctrl, LNotification n)
	{
		switch (Ctrl->GetId())
		{
			case IDOK:
			case IDCANCEL:
			case IDYES:
			case IDNO:
				EndModal(Ctrl->GetId());
				break;
		}
		
		return LDialog::OnNotify(Ctrl, n);
	}
};

void LHtmlMsg(std::function<void(int)> Callback, LViewI *Parent, const char *Html, const char *Title, int Type, ...)
{
	va_list Arg;
	va_start(Arg, Type);
	#undef vsnprintf
	int length = vsnprintf(NULL, 0, Html, Arg);
	LAutoString Msg(new char[++length]);
	vsprintf_s(Msg, length, Html, Arg);
	va_end(Arg);

	auto Dlg = new HtmlMsg(Parent, Msg, Title, Type);
	Dlg->DoModal([Callback](auto dlg, auto id)
	{
		if (Callback)
			Callback(id);
	});
}

/////////////////////////////////////////////////////////////////////////////
void TabDialog::OnCreate()
{
	LTabView *Tab;
	if (GetViewById(TabCtrlId, Tab))
	{
		Tab->SetPourChildren(true);
		LRect r(0, 0, 100, 100);
		Tab->SetPos(r);
	}
	
	OnPosChange();
}

void TabDialog::IdealSize(LButton *b)
{
	LViewLayoutInfo Inf;
	if (b->OnLayout(Inf))
	{
		b->OnLayout(Inf);
	}
	else if (b->GetWindow())
	{
		auto s = b->GetWindow()->GetDpiScale();
		LDisplayString ds(b->GetFont(), b->Name());
		Inf.Width.Max = (int32)(ds.X() + (s.x * LButton::Overhead.x));
		Inf.Height.Max = (int32)(ds.Y() + (s.y * LButton::Overhead.y));
	}
	else
	{
		LAssert(!"No way to set ideal size.");
		return;
	}
	
	LRect p = b->GetPos();
	p.SetSize(Inf.Width.Max, Inf.Height.Max);
	b->SetPos(p);
}

void TabDialog::OnPosChange()
{
	LButton *Ok = 0, *Cancel = 0, *Help = 0;
	LViewI *Tab = 0;

	if (GetViewById(TabCtrlId, Tab) &&
		GetViewById(IDOK, Ok) &&
		GetViewById(IDCANCEL, Cancel))
	{
		GetViewById(HelpBtnId, Help);

		LRect r = GetClient();
		r.Inset(LTableLayout::CellSpacing, LTableLayout::CellSpacing);
		
		IdealSize(Ok);
		IdealSize(Cancel);

		LRect t = r;
		t.y2 -= LTableLayout::CellSpacing + Ok->Y();
		Tab->SetPos(t);
		
		if (Help)
		{
			IdealSize(Help);

			LRect h = Help->GetPos();
			h.Offset(r.x1 - h.x1, r.y2 - h.Y() + 1 - h.y1);
			Help->SetPos(h);
		}

		LRect c = Cancel->GetPos();
		c.Offset(r.x2 - c.X() + 1 - c.x1, r.y2 - c.Y() + 1 - c.y1);
		Cancel->SetPos(c);

		LRect o = Ok->GetPos();
		o.Offset(c.x1 - LTableLayout::CellSpacing - o.X() + 1 - o.x1, r.y2 - o.Y() + 1 - o.y1);
		Ok->SetPos(o);
	}
}

LAutoString ConvertThreadIndex(char *ThreadIndex, int TruncateChars)
{
	LAutoString a;

	if (ThreadIndex)
	{
		uchar InBuf[256];
		ssize_t In = ConvertBase64ToBinary(InBuf, sizeof(InBuf), ThreadIndex, strlen(ThreadIndex));
		LAssert(In >= 22);
		
		LStringPipe OutBuf(256);
		for (int i=0; i<In-TruncateChars; i++)
		{
			OutBuf.Print("%2.2x", InBuf[i]);
		}
		
		a.Reset(OutBuf.NewStr());
	}
	
	return a;
}

ScribeProtocol ProtocolToEnum(const char *str)
{
	if (!str)
		return ProtocolNone;
	#define _(def, en) if (!Stricmp(str, def)) return en;
	ScribeProtocolTypeMap()
	#undef _	
	LAssert(!"Invalid protocol");
	return ProtocolNone;
}

const char *ToString(ScribeProtocol p)
{
	switch (p)
	{
		#define _(def, en) case en: return def;
		ScribeProtocolTypeMap()
		#undef _
		default:
			break;
	}

	return NULL;
}

/////////////////////////////////////////////////////////////////
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

void PatternBox(LSurface *pDC, const LRect &r)
{
	int All = r.X() + r.Y() - 1;
	int MinEdge = MIN(r.X(), r.Y());
	bool Wider = r.X() > r.Y();
	
	for (int i=0; i<All; i++)
	{
		LPoint pt(r.x1 + i, r.y1);
		int Draw = ((pt.x + pt.y) >> 2) % 2;
		
		/*
		if (i <= 4)
			LgiTrace("pt=%i,%i draw=%i\n", pt.x, pt.y, Draw);
		*/		
		if (!Draw)
			continue;
		
		if (i < MinEdge)
		{
			pDC->Line(r.x1, r.y1 + i, r.x1 + i, r.y1);
		}
		else if (Wider)
		{
			if (i < r.X())
			{
				int yy = r.Y() - 1;
				pDC->Line(pt.x, pt.y, pt.x - yy, pt.y + yy);
			}
			else
			{
				int yy = r.Y() - (i - r.X() + 1) - 1;
				pDC->Line(r.x2, r.y2-yy, r.x2-yy, r.y2);
			}
		}
		else // Tall
		{
			if (i < r.Y())
			{
				int xx = r.X() - 1;
				pDC->Line(r.x1, r.y1 + i, r.x1 + xx, r.y1 + i - xx);
			}
			else
			{
				int xx = r.X() - (i - r.Y() + 1) - 1;
				pDC->Line(r.x2-xx, r.y2, r.x2, r.y2-xx);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////
ContactGroup *LookupContactGroup(ScribeWnd *App, const char *Name)
{
	auto Srcs = App->GetThingSources(MAGIC_GROUP);
	if (!Srcs.Length() || !Name)
		return NULL;

	for (auto s: Srcs)
	{
		s->LoadThings();
		
		for (auto t: s->Items)
		{
			ContactGroup *g = t->IsGroup();
			if (!g)
				continue;

			LVariant Nm;
			if (g->GetVariant("Name", Nm) &&
				Nm.Str() &&
				_stricmp(Nm.Str(), Name) == 0)
			{
				return g;
			}
		}
	}
	
	return NULL;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
LOAuth2::Params GetOAuth2Params(const char *Host, Store3ItemTypes Context)
{
	LOAuth2::Params	p;

	// FYI: None of this works due to issues at the providers end. It did sometime in the
	// past. And is only here in case someone wants to try and get it working again.

	if (stristr(Host, "google.") ||
		stristr(Host, "gmail."))
	{
		if (Context == MAGIC_MAIL)
		{
			p.AuthUri = "https://accounts.google.com/o/oauth2/auth";
			p.ApiUri = "https://www.googleapis.com/oauth2/v3/token";
			
			#if 1
			// Old scope:
			p.Scope = "https://mail.google.com/";
			#else
			// New scope: (doesn't work)
			p.Scope = "https://www.googleapis.com/auth/gmail.modify";
			#endif
			
			// p.RevokeUri = "https://accounts.google.com/o/oauth2/revoke";
		}
		/*
		else if (Context == MAGIC_CALENDAR)
		{
			p.AuthUri = "https://accounts.google.com/o/oauth2/v2/auth";
			p.ApiUri = "https://apidata.googleusercontent.com/caldav/v2/%s/user";
			p.Scope = "https://www.googleapis.com/auth/calendar";
		}
		*/
		else return p;
					
		p.Provider = LOAuth2::Params::OAuthGoogle;
					
		p.ClientID = "<add yours here>";
		p.ClientSecret = "<add yours here>";
		p.RedirURIs = "urn:ietf:wg:oauth:2.0:oob\nhttp://localhost";
	}
	else if (stristr(Host, "outlook.") &&
			!stristr(Host, "office365."))
	{
		if (Context == MAGIC_MAIL)
		{
			p.RedirURIs = "urn:ietf:wg:oauth:2.0:oob\nhttp://localhost";
			p.AuthUri = "https://login.microsoftonline.com/common/oauth2/v2.0/authorize";
			p.ApiUri = "https://login.microsoftonline.com/common/oauth2/v2.0/token";
		}
		else return p;

		p.Provider = LOAuth2::Params::OAuthMicrosoft;
					
		p.ClientID = "<add yours here>";
		p.ClientSecret = "<add yours here>";
					
		p.Scope = "https://outlook.office.com/mail.readwrite%20https://outlook.office.com/mail.send";
	}

	return p;
}


//////////////////////////////////////////////////////////////////////////////////////////////////
class ScribeHtmLParser : public LHtmlParser
{
	LScriptEngine Eng;

public:
	struct HtmlElem : public LHtmlElement
	{
		LHashTbl<ConstStrKey<char, false>, LString> Attr;

		HtmlElem(LHtmlElement *e) : LHtmlElement(e)
		{
		}

		bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL)
		{
			if (!Stricmp(Name, "element"))
			{
				Value = Tag.Get();
				return true;
			}
			else if (!Stricmp(Name, "content"))
			{
				Value.OwnStr(WideToUtf8(Txt.Get()));
				return true;
			}
			else if (!Stricmp(Name, "attr"))
			{
				if (Array)
				{
					char *s = Attr.Find(Array);
					if (s)
					{
						Value = s;
						return true;
					}
				}
			}

			return false;
		}

		bool Get(const char *attr, const char *&val)
		{
			auto s = Attr.Find(attr);
			if (!s)
				return false;
			val = s.Get();
			return true;
		}

		void Set(const char *attr, const char *val)
		{
			Attr.Add(attr, val);
		}
	};

	ScribeHtmLParser() : LHtmlParser(NULL),
		Eng(NULL, NULL, NULL)
	{
	}

	LHtmlElement *CreateElement(LHtmlElement *Parent)
	{
		return new HtmlElem(Parent);
	}

	void Evaluate(LArray<HtmlElem*> &Out, LString Search, HtmlElem *Elem)
	{
		LVariant Result;
		if (Eng.EvaluateExpression(&Result, Elem, Search))
		{
			if (Result.CastInt32())
				Out.Add(Elem);
		}

		for (auto e: Elem->Children)
			Evaluate(Out, Search, dynamic_cast<HtmlElem*>(e));
	}
};

bool SearchHtml(LVariant *ReturnValue, const char *Html, const char *SearchExp, const char *ResultExp)
{
	ScribeHtmLParser Parser;
	ScribeHtmLParser::HtmlElem Root(NULL);
	if (!Parser.Parse(&Root, Html))
	{
		LgiTrace("%s:%i - HTML parsing failed.\n", _FL);
		*ReturnValue = false;
		return false;
	}

	LArray<ScribeHtmLParser::HtmlElem*> Matches;
	Parser.Evaluate(Matches, SearchExp, &Root);

	if (!ReturnValue->SetList())
		return false;

	LScriptEngine Eng(NULL, NULL, NULL);
	for (auto e: Matches)
	{
		LVariant *Result = new LVariant;
		Eng.EvaluateExpression(Result, e, ResultExp);
		ReturnValue->Add(Result);
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
ScriptDownloadContentThread::ScriptDownloadContentThread(ScribeWnd *app, LString uri, LString callbackName, LVariant *userData) :
	LThread("ScriptDownloadContentThread", (App = app)->AddDispatch())
{
	Uri = uri;
	CallbackName = callbackName;
	if (userData)
		UserData = *userData;

	DeleteOnExit = true;
	Run();
}

int ScriptDownloadContentThread::Main()
{
	Result = LgiGetUri(this, &Out, &Err, Uri);
	return false;
}

void ScriptDownloadContentThread::OnComplete()
{
	auto Cb = App->GetCallback(CallbackName);
	if (!Cb.Func)
		return;

	LVirtualMachine Vm;
	LScriptArguments Args(&Vm);

	LVariant vApp((LDom*)App);
	Args.Add(&vApp);

	LVariant vUri = Uri.Get();
	Args.Add(&vUri);

	LVariant vResult = Result;
	Args.Add(&vResult);

	LVariant vData;
	if (Result)
		vData.OwnStr(Out.NewStr());
	else
		vData = Err.Get();
	Args.Add(&vData);

	Args.Add(&UserData);
		
	App->ExecuteScriptCallback(Cb, Args);
}
