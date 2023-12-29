#include "lgi/common/Lgi.h"
#include "Scribe.h"
#include "ScribeListAddr.h"

class DynamicHtmlPrivate
{
public:
	ScribeWnd *App;
};

DynamicHtml::DynamicHtml(ScribeWnd *app, const char *file) :
	Html1::LHtml(100, 0, 0, 100, 100)
{
	SetEnv(this);
	d = new DynamicHtmlPrivate;
	d->App = app;

	auto f = LFindFile(file);
	if (f)
	{
		auto s = LReadFile(f);
		if (s)
			Name(s);
	}
	else
	{
		Name("<html>\n"
			"<body style='color:LC_TEXT;background:LC_WORKSPACE;'>\n"
			"Couldn't find template file.\n"
			"</body>\n"
			"</html>");
	}
}

DynamicHtml::~DynamicHtml()
{
	DeleteObj(d);
}

LString DynamicHtml::OnDynamicContent(LDocView *Parent, const char *Code)
{
	LVariant Val;
	
	if (!d->App->GetValue(Code, Val))
		return LString();

	switch (Val.Type)
	{
		default:
		{
			LAssert(!"Not impl.");
			break;
		}
		case GV_INT32:
		{
			char i[32];
			sprintf_s(i, sizeof(i), "%i", Val.Value.Int);
			return LString(i);
		}
		case GV_STRING:
		{
			return Val.Str();
		}
		case GV_NULL:
			break;
	}

	return LString();
}

bool DynamicHtml::OnNavigate(LDocView *Parent, const char *Uri)
{
	if (Uri)
	{
		LUri u(Uri);
		if (!u.sProtocol)
			return false;

		if (!Stricmp(u.sProtocol.Get(), "http"))
		{
			return LExecute(Uri);
		}
		else if (!Stricmp(u.sProtocol.Get(), "file"))
		{
			const char *f = Uri + 7;
			if (ValidStr(f))
			{
				char File[256];
				if (*f == '.')
				{
					auto Exe = LGetExePath();

					#ifdef WIN32
					char *Last = strrchr(Exe, DIR_CHAR);
					if (Last &&
						_stricmp(Last + 1,
								#ifdef _DEBUG
								"Debug"
								#else
								"Release"
								#endif
								) == 0)
					{
						*Last = 0;
					}
					#endif
					LMakePath(File, sizeof(File), Exe, f);

					#if defined(_DEBUG)
					if (!LFileExists(File))
					{
						char e[MAX_PATH_LEN];
						LMakePath(e, sizeof(e), Exe, "Code");
						LMakePath(File, sizeof(File), e, f);
					}
					#endif
				}
				else
				{
					strcpy_s(File, sizeof(File), f);
				}

				if (LFileExists(File))
				{
					return LExecute(File);
				}
			}
		}
		else if (!Stricmp(u.sProtocol.Get(), "folder"))
		{
			const char *Name = Uri + 9;
			if (ValidStr(Name))
			{
				ScribeFolder *f = 0;
				if (IsDigit(*Name))
				{
					f = d->App->GetFolder(atoi(Name));
				}
				else
				{
					f = d->App->GetFolder(Name);
				}
				if (f)
				{
					f->Select(true);
				}
			}
		}
		else if (!Stricmp(u.sProtocol.Get(), "thing"))
		{
			const char *u = Uri + 8;
			if (u[0] == '0' && u[1] == 'x') u += 2;

			Thing *t = (Thing*)htoi64(u);
			if (t)
			{
				t->DoUI();
			}
		}
		else if (!Stricmp(u.sProtocol.Get(), "mailto"))
		{
			Mailto mt(d->App, Uri);
			if (!mt.To.Length())
				return false;

			Thing *t = d->App->CreateThingOfType(MAGIC_MAIL);
			if (!t)
				return false;

			Mail *m = t->IsMail();
			if (!m)
			{
				DeleteObj(t);
				return false;
			}

			m->OnCreate();
			
			LDataPropI *dTo = m->GetTo()->Create(m->GetObject()->GetStore());
			if (dTo)
			{
				AddressDescriptor *sTo = mt.To[0];
				if (sTo)
				{
					dTo->SetStr(FIELD_NAME, sTo->sName);
					dTo->SetStr(FIELD_EMAIL, sTo->sAddr);
					m->GetTo()->Insert(dTo);
				}
			}
			m->SetSubject(mt.Subject);
			m->SetBody(mt.Body);
			m->DoUI();
		}
	}
	return false;
}
