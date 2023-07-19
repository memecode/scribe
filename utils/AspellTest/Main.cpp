#include "Lgi.h"
#include "../Code/ScribePlugin.h"
#include "../Code/SpellCheck.h"
#include "../Code/ScribeDefs.h"

char *AppName = "Test";

char16 *Txt[] =
{
	L"T",
	L"Th",
	L"Thi",
	L"This",
	L"This ",
	L"i",
	L"is",
	L"is ",
	L"j",
	L"ju",
	L"jus",
	L"just",
	L"t",
	L"th",
	L"the",
	L"b",
	L"be",
	L"beg",
};

ScribePlugin::~ScribePlugin()
{
	LAssert(0);
}

class Sink : public LWindow
{
public:
	SpellThread *t;

	Sink()
	{
		t = 0;
		if (Attach(0))
		{
		}
	}

	LMessage::Result OnEvent(LMessage *m)
	{
		if (MsgCode(m) == M_SCRIBE_SPELL)
		{
			SpellThread::SpellStyles *s;
			while (s = t->Get())
			{
				printf("Got M_SCRIBE_SPELL\n");
				DeleteObj(s);
			}
		}

		return LWindow::OnEvent(m);
	}
};

int main(int args, char **arg)
{
	OsAppArguments Args(args, arg);
	LApp a("application/x-aspell-test", Args);
	if (a.IsOk())
	{
		LAutoPtr<Sink> Wnd(new Sink);
		OsView h = Wnd->Handle();
		ScribePlugin_SpellChecker *Plugin = CreateSpellChecker(Wnd, 0);
		SpellThread Thread(Plugin);
		Wnd->t = &Thread;

		for (int i=0; i<CountOf(Txt); i++)
		{
			printf("Put(%S)\n", Txt[i]);
			Thread.Put(Wnd,0,1,LAutoWString(NewStrW(Txt[i])));
			LSleep(LRand(500));
		}
	}
	return 0;
}