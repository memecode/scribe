#pragma once

#include "context.h"

class SmtpConnection;

class SmtpServer
{
    struct SmtpServerImpl *d = nullptr;

public:
    SmtpServer(Context &ctx);
    ~SmtpServer();
};

class SmtpConnection : public LSocket
{
	Context *ctx;

	SmtpConnection(Context *c) :
		ctx(c)
	{
	}

	~SmtpConnection()
	{
	}
};

