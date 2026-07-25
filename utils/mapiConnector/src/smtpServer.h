#pragma once

#include "context.h"
#include <functional>

class SmtpConnection;

struct SmtpMessage
{
	LString helo;
	LString mailFrom;
	LArray<LString> rcptTo;
	LString data;
};

using SmtpOnMessage = std::function<bool(const SmtpMessage &msg)>;

class SmtpServer
{
    struct SmtpServerImpl *d = nullptr;

public:
	SmtpServer(Context &ctx, SmtpOnMessage onMessage = SmtpOnMessage());
    ~SmtpServer();

	void SetOnMessage(SmtpOnMessage onMessage);
};

class SmtpConnection : public LSocket
{
	public:
	Context *ctx;

	SmtpConnection(Context *c) :
		ctx(c)
	{
	}

	~SmtpConnection()
	{
	}
};

