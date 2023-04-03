#include "lgi/common/Lgi.h"
#include "lgi/common/TextLog.h"

const char *AppName = "Google Cal Test";

class App : public LWindow
{
    LTextLog *Log;

public:
    App()
    {
        LRect r(0, 0, 1000, 800);
        SetPos(r);
        MoveToCenter();
        Name(AppName);
        SetQuitOnClose(true);
        
        if (Attach(0))
        {
            Visible(true);
        }
    }
};

int LgiMain(OsAppArguments &Args)
{
    LApp a(Args, "GoogleCalTest");
    if (a.IsOk())
    {
        a.AppWnd = new App();
        a.Run();
    }
    return 0;
}