#include <conio.h>
#include "lgi/common/Lgi.h"
#include "lgi/common/Net.h"
#include "lgi/common/Store3.h"

#include "../Code/ScribeDefs.h"
#include "UnitTest.h"

int Status = 0;

class UnitTests : public LWindow
{
	int Cur;
	UnitTest *Running;
	LArray<UnitTest*> Tests;

public:
	UnitTests()
	{
		Running = 0;
		Name("Unit Tests");
		Attach(0);
		Cur = 0;

		printf("Scribe Unit Tests - Start.\n");

		PostEvent(M_START_NEXT_TEST);
	}

	~UnitTests()
	{
		printf("Scribe Unit Tests - End.\n");
	}

	bool OnIdle()
	{
		return (Running) ? Running->OnIdle() : false;
	}

	LMessage::Result OnEvent(LMessage *m)
	{
		switch (m->Msg())
		{
			case M_START_NEXT_TEST:
			{
				if (Cur < Tests.Length())
				{
					if (Running = Tests[Cur++])
					{
						Running->Run();
					}
				}
				else
				{
					Running = 0;
					Quit();
					LCloseApp();
					return 0;
				}
				break;
			}
			case M_TEST_FINISHED:
			{
				UnitTest *Ut = (UnitTest*)m->A();
				int Result = m->B();
				if (!Result)
				{
					printf("Error: %s failed.\n", Ut?Ut->LBase::Name():0);
					Status = -1;
				}
				DeleteObj(Ut);
				Running = 0;
				PostEvent(M_START_NEXT_TEST);
				break;
			}
			default:
			{
				if (Running)
					Running->OnEvent(m);
				break;
			}
		}

		return LWindow::OnEvent(m);
	}
};

bool Func(UnitTests *u)
{
	return u->OnIdle();
}

int main(int Args, char **Arg)
{
	OsAppArguments AppArgs(Args, Arg);
	LApp a(AppArgs, "ScribeUnitTests");
	if (!a.IsOk())
		return -1;

	UnitTests *Ut = new UnitTests;
	a.AppWnd = Ut;
	a.Run((LAppI::OnIdleProc)Func, Ut);

	//if (Status)
		getch();

	return Status;
}
