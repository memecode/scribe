#include "lgi/common/Lgi.h"


int main(int args, const char **arg)
{
	OsAppArguments appArgs(args, arg);
	LApp app(appArgs, "mapiConnector");
	if (app)
	{
		app.Run();
	}

	return 0;
}

