#include "lgi/common/Lgi.h"
#include "lgi/common/Base64.h"
#include "lgi/common/Net.h"
#include "lgi/common/Thread.h"

#include "smtpServer.h"
#include <cstring>

constexpr int SMTP_IDLE_TIMEOUT_MS = 30 * 1000;

static bool StartsWithNoCase(const LString &line, const char *prefix)
{
    auto len = (int)strlen(prefix);
    return line.Length() >= len && _strnicmp(line.Get(), prefix, len) == 0;
}

static LString ReadAddressArg(const LString &line, const char *verb)
{
    auto len = (int)strlen(verb);
    auto p = line.Get() + len;
    while (*p == ' ' || *p == '\t')
        ++p;

    if (*p == '<')
    {
        ++p;
        auto end = strchr(p, '>');
        if (end)
            return LString(p, (ssize_t)(end - p));
    }

    return LString(p).Strip();
}

static bool DecodeAuthPlain(const LString &b64, LString &user, LString &pass)
{
    auto decoded = LToBinary(b64);
    if (decoded.Length() < 3)
        return false;

    auto d = decoded.Get();
    if (!d)
        return false;

    // Typical payload: "\0user\0pass"
    if (d[0] == '\0')
    {
        user = d + 1;
        if (!user)
            return false;

        pass = d + 2 + user.Length();
        return user && pass;

        return false;
    }

    // authzid\0authcid\0passwd fallback.
    auto p = strchr(d, '\0');
    if (!p)
        return false;
    user = p + 1;
    if (!user)
        return false;
    pass = p + 2 + user.Length();
    return pass;
}

struct SmtpServerImpl :
    public LThread,
    public LCancel
{
    Context &ctx;
    SmtpOnMessage onMessage;

    SmtpServerImpl(Context &c, SmtpOnMessage cb)
        : LThread("smtpServer.Th")
        , ctx(c)
        , onMessage(cb)
    {
        Run();
    }

    ~SmtpServerImpl()
    {
        Cancel();
        WaitForExit();
    }

    void SetOnMessage(SmtpOnMessage cb)
    {
        onMessage = cb;
    }

    bool HandleConnection(SmtpConnection &conn)
    {
        enum AuthState
        {
            AuthNone,
            AuthPlain,
            AuthLoginUser,
            AuthLoginPass
        };

        auto writeResp = [&](int code, const char *msg)
        {
            auto w = LString::Fmt("%i %s\r\n", code, msg);
            conn.Write(w);
        };

        auto writeChallenge = [&](const char *b64)
        {
            auto w = LString::Fmt("334 %s\r\n", b64 ? b64 : "");
            conn.Write(w);
        };

        auto clientIp = LString("unknown");
        {
            char ip[64] = {0};
            if (conn.GetRemoteIp(ip))
                clientIp = ip;
        }

        ctx.log.Print("SMTP: accepted connection from %s\n", clientIp.Get());
        writeResp(220, "mapiConnector SMTP ready");

        LStringPipe rdBuf(8 << 10);
        SmtpMessage msg;
        bool authenticated = false;
        bool inData = false;
        AuthState authState = AuthNone;
        LString pendingUser;

        while (!IsCancelled())
        {
            if (!conn.IsReadable(SMTP_IDLE_TIMEOUT_MS))
            {
                writeResp(421, "Timeout");
                return false;
            }

            char buf[1024];
            auto rd = conn.Read(buf, sizeof(buf));
            if (rd <= 0)
                return false;

            rdBuf.Write(buf, rd);

            while (rdBuf.Find("\n") >= 0)
            {
                auto rawLine = rdBuf.Pop().Replace("\r");
                auto line = rawLine.Strip();
                if (!line)
                    continue;

                if (authState != AuthNone)
                {
                    if (authState == AuthPlain)
                    {
                        LString user, pass;
                        auto ok = DecodeAuthPlain(line, user, pass) && ctx.authenticateUser(user.Get(), pass.Get());
                        if (ok)
                        {
                            authenticated = true;
                            writeResp(235, "2.7.0 Authentication successful");
                        }
                        else
                        {
                            writeResp(535, "5.7.8 Authentication credentials invalid");
                        }
                        authState = AuthNone;
                        pendingUser.Empty();
                        continue;
                    }

                    if (authState == AuthLoginUser)
                    {
                        pendingUser = LToBinary(line);
                        if (!pendingUser)
                        {
                            writeResp(501, "5.5.2 Invalid base64 username");
                            authState = AuthNone;
                            continue;
                        }
                        authState = AuthLoginPass;
                        writeChallenge("UGFzc3dvcmQ6"); // "Password:"
                        continue;
                    }

                    if (authState == AuthLoginPass)
                    {
                        auto pass = LToBinary(line);
                        auto ok = pendingUser && pass && ctx.authenticateUser(pendingUser.Get(), pass.Get());
                        if (ok)
                        {
                            authenticated = true;
                            writeResp(235, "2.7.0 Authentication successful");
                        }
                        else
                        {
                            writeResp(535, "5.7.8 Authentication credentials invalid");
                        }
                        authState = AuthNone;
                        pendingUser.Empty();
                        continue;
                    }
                }

                if (inData)
                {
                    if (line == ".")
                    {
                        inData = false;
                        auto delivered = onMessage ? onMessage(msg) : true;
                        if (delivered)
                        {
                            writeResp(250, "Message accepted");
                        }
                        else
                        {
                            writeResp(451, "Message callback failed");
                        }

                        msg.mailFrom.Empty();
                        msg.rcptTo.Empty();
                        msg.data.Empty();
                        continue;
                    }

                    if (StartsWithNoCase(line, ".."))
                        line = line(1, -1);
                    msg.data += line + "\r\n";
                    continue;
                }

                if (StartsWithNoCase(line, "EHLO") ||
                    StartsWithNoCase(line, "HELO"))
                {
                    auto pos = line.Find(" ");
                    msg.helo = pos > 0 ? line(pos + 1, -1).Strip() : "unknown";
                    if (StartsWithNoCase(line, "EHLO"))
                    {
                        conn.Write("250-mapiConnector\r\n", 19);
                        conn.Write("250-AUTH PLAIN LOGIN\r\n", 22);
                        conn.Write("250 OK\r\n", 8);
                    }
                    else
                    {
                        writeResp(250, "mapiConnector");
                    }
                }
                else if (StartsWithNoCase(line, "AUTH"))
                {
                    LString::Array parts;
                    const char *ptr = line.Get();
                    while (auto s = LTokLStr(ptr))
                        parts.Add(s);

                    if (parts.Length() < 2)
                    {
                        writeResp(501, "5.5.4 Missing AUTH mechanism");
                        continue;
                    }

                    auto mech = parts[1];
                    if (StartsWithNoCase(mech, "PLAIN"))
                    {
                        if (parts.Length() >= 3 && parts[2] && !parts[2].Equals("="))
                        {
                            LString user, pass;
                            auto ok = DecodeAuthPlain(parts[2], user, pass) && ctx.authenticateUser(user.Get(), pass.Get());
                            if (ok)
                            {
                                authenticated = true;
                                writeResp(235, "2.7.0 Authentication successful");
                            }
                            else
                            {
                                writeResp(535, "5.7.8 Authentication credentials invalid");
                            }
                        }
                        else
                        {
                            authState = AuthPlain;
                            writeChallenge("");
                        }
                    }
                    else if (StartsWithNoCase(mech, "LOGIN"))
                    {
                        if (parts.Length() >= 3 && parts[2] && !parts[2].Equals("="))
                        {
                            pendingUser = LToBinary(parts[2]);
                            if (!pendingUser)
                            {
                                writeResp(501, "5.5.2 Invalid base64 username");
                            }
                            else
                            {
                                authState = AuthLoginPass;
                                writeChallenge("UGFzc3dvcmQ6"); // "Password:"
                            }
                        }
                        else
                        {
                            authState = AuthLoginUser;
                            writeChallenge("VXNlcm5hbWU6"); // "Username:"
                        }
                    }
                    else
                    {
                        writeResp(504, "5.5.4 Unrecognized authentication type");
                    }
                }
                else if (StartsWithNoCase(line, "MAIL FROM:"))
                {
                    if (!authenticated)
                    {
                        writeResp(530, "5.7.0 Authentication required");
                        continue;
                    }

                    msg.mailFrom = ReadAddressArg(line, "MAIL FROM:");
                    msg.rcptTo.Empty();
                    msg.data.Empty();
                    writeResp(250, "Sender OK");
                }
                else if (StartsWithNoCase(line, "RCPT TO:"))
                {
                    if (!authenticated)
                    {
                        writeResp(530, "5.7.0 Authentication required");
                        continue;
                    }

                    auto rcpt = ReadAddressArg(line, "RCPT TO:");
                    if (!rcpt)
                    {
                        writeResp(501, "Bad recipient");
                    }
                    else
                    {
                        msg.rcptTo.Add(rcpt);
                        writeResp(250, "Recipient OK");
                    }
                }
                else if (StartsWithNoCase(line, "DATA"))
                {
                    if (!authenticated)
                    {
                        writeResp(530, "5.7.0 Authentication required");
                        continue;
                    }

                    if (!msg.mailFrom || msg.rcptTo.Length() == 0)
                    {
                        writeResp(503, "Need MAIL FROM and RCPT TO first");
                    }
                    else
                    {
                        inData = true;
                        msg.data.Empty();
                        writeResp(354, "End data with <CR><LF>.<CR><LF>");
                    }
                }
                else if (StartsWithNoCase(line, "RSET"))
                {
                    msg.mailFrom.Empty();
                    msg.rcptTo.Empty();
                    msg.data.Empty();
                    writeResp(250, "Reset OK");
                }
                else if (StartsWithNoCase(line, "NOOP"))
                {
                    writeResp(250, "OK");
                }
                else if (StartsWithNoCase(line, "QUIT"))
                {
                    writeResp(221, "Bye");
                    return true;
                }
                else
                {
                    writeResp(502, "Command not implemented");
                }
            }
        }

        return true;
    }

    int Main() override
    {
        LSocket smtpListen;
        auto smtpPort = ctx.options.Get(ctx.OptSmtpPort);
        auto status = smtpListen.Listen(smtpPort ? (int)smtpPort.Int() : SMTP_PORT);
		if (!status)
		{
            ctx.log.Print("Error: failed to listen on SMTP port\n");
			return -1;
		}

        ctx.log.Print("SMTP: listening on port %i\n", smtpPort ? (int)smtpPort.Int() : SMTP_PORT);
        while (!IsCancelled())
        {
            if (smtpListen.IsReadable(100))
            {
                if (auto c = new SmtpConnection(&ctx))
                {
                    if (smtpListen.Accept(c))
                    {
                        HandleConnection(*c);
                    }
                    else
                    {
                        ctx.log.Print("SMTP: accept failed\n");
                    }

                    delete c;
                }
            }
        }

        return 0;
    }
};
    
SmtpServer::SmtpServer(Context &ctx, SmtpOnMessage onMessage)
    : d(new SmtpServerImpl(ctx, onMessage))
{
}

SmtpServer::~SmtpServer()
{
    delete d;
}

void SmtpServer::SetOnMessage(SmtpOnMessage onMessage)
{
    if (d)
        d->SetOnMessage(onMessage);
}
