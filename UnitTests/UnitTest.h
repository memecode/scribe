#ifndef _UNIT_TEST_H_
#define _UNIT_TEST_H_

#include "lgi/common/Thread.h"
#include "lgi/common/Net.h"
#include "lgi/common/Json.h"

#define M_START_NEXT_TEST		(M_USER+3000)
#define M_TEST_FINISHED			(M_USER+3001)

class UnitTest : public LThread, public LBase
{
protected:
	LViewI *App;

public:
	UnitTest(LViewI *app, char *n) : LThread("UnitTest")
	{
		App = app;
		LBase::Name(n);
	}

	~UnitTest()
	{
		while (!IsExited())
		{
			LSleep(10);
		}
	}
	
	virtual bool Test() { return false; }
	virtual void OnEvent(LMessage *m) {}
	virtual bool OnIdle() { return false; }
	
	int Main()
	{
		printf("Starting '%s'...\n", LBase::Name());
		int Result = Test();
		App->PostEvent(M_TEST_FINISHED, (LMessage::Param)this, Result);
		return 0;
	}
};

class LUnitTestServer : public LThread, public LCancel
{
	struct Connect
	{
		LArray<int8_t> Buf;
		LAutoPtr<LSocket> Sock;

		Connect &operator =(Connect &c)
		{
			Buf = c.Buf;
			Sock = c.Sock;
			return *this;
		}
	};

	LWindow *App;
	LArray<Connect> Connections;

public:
	constexpr static int ServerPort = 3154;
	constexpr static int MsgMagic = 'unit';
	constexpr static int HdrSize = 8; // 4 bytes = MsgMagic, 4 bytes = payload size

	LUnitTestServer(LWindow *app) : LThread("UnitTestServer")
	{
		App = app;
		Run();
	}

	~LUnitTestServer()
	{
		Cancel();
		WaitForExit();
	}

	void OnMsg(LString s)
	{
		App->PostEvent(M_UNIT_TEST, (LMessage::Param)new LJson(s));		
	}

	int Main()
	{
		LSocket Listen;
		if (!Listen.Listen(ServerPort))
		{
			LgiTrace("%s:%i - Can't listen on %i\n", _FL, ServerPort);
			return -1;
		}

		while (!IsCancelled())
		{
			if (Listen.IsReadable(100))
			{
				LAutoPtr<LSocket> Conn(new LSocket);
				if (Listen.Accept(Conn))
				{
					Connections.New().Sock = Conn;
				}
			}
			
			for (unsigned i=0; i<Connections.Length(); i++)
			{
				auto &c = Connections[i];

				if (c.Sock->IsReadable())
				{
					int8_t buf[512];
					auto rd = c.Sock->Read(buf, sizeof(buf));
					if (rd > 0)
					{
						c.Buf.Add(buf, rd);

						if (c.Buf.Length() >= HdrSize)
						{
							LPointer p;
							p.s8 = c.Buf.AddressOf();
							if (p.u32[0] == MsgMagic &&
								p.u32[1] <= c.Buf.Length() - HdrSize)
							{
								OnMsg(LString((char*)p.s8 + HdrSize, p.u32[1]));
								c.Buf.DeleteRange(LRange(0, HdrSize + p.u32[1]));
							}
						}
					}
					else // Disconnect
					{
						Connections.DeleteAt(i--);
					}
				}
			}
		}

		return 0;
	}
};

class LUnitTestClient : public LSocket
{
public:
	bool Send(LString s)
	{
		if (!IsOpen())
		{
			bool op = Open("localhost", LUnitTestServer::ServerPort);
			LgiTrace("%s:%i - LUnitTestClient open = %i\n", _FL, op);
			if (!op)
				return false;
		}

		uint32_t hdr[] = { LUnitTestServer::MsgMagic, (uint32_t) s.Length() };
		auto wr = Write(hdr, sizeof(hdr));
		if (wr != sizeof(hdr))
			return false;

		wr = Write(s.Get(), s.Length());
		return wr == s.Length();
	}

	bool Send(LJson &j)
	{
		return Send(j.GetJson());
	}
};

#endif
