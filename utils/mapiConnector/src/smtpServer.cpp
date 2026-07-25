#include "lgi/common/Lgi.h"
#include "lgi/common/Net.h"
#include "lgi/common/Thread.h"

#include "smtpServer.h"

struct SmtpServerImpl :
    public LThread
{
    Context &ctx;

    SmtpServerImpl(Context &c)
        : LThread("smtpServer.Th")
        , ctx(c)
    {
    }

    int Main() override
    {
        LSocket smtpListen;
        auto smtpPort = ctx.options.Get(ctx.OptSmtpPort);
        auto status = smtpListen.Listen(smtpPort ? (int)smtpPort.Int() : SMTP_PORT);
		if (!status)
		{
            ctx.log.Print("Error: failed to listen on SMTP port\n");
			LCloseApp();
			return -1;
		}

        return 0;
    }
};
    
SmtpServer::SmtpServer(Context &ctx)
    : d(new SmtpServerImpl(ctx))
{
}

SmtpServer::~SmtpServer()
{
    delete d;
}
