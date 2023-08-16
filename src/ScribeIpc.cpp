#include "lgi/common/Lgi.h"
#include "lgi/common/Lgi.h"
#include "lgi/common/SharedMemory.h"
#include "lgi/common/SubProcess.h"
#include "lgi/common/Thread.h"
#include "lgi/common/Net.h"

#include "ScribeIpc.h"

////////////////////////////////////////////////////////////////////////////////////
// This is a shared memory block format for instances of Scribe to sort out who is
// doing what.
#define SCRIBE_INSTANCE_MAGIC		0x33448080		// Scribe record
#define MUL_INSTANCE_MAGIC			0x334480FF		// Multi-user login record
#define SCRIBE_INSTANCE_MAX			8

#define SCRIBE_IPC_LONG_ARGS		0x00000001		// Args is longer than 256 bytes
// This is first segment.
#define SCRIBE_IPC_CONTINUE_ARGS	0x00000002		// Continue with next segment of
// long args.
#undef _Reserved_
class ScribeIpcInstance
{
public:
	uint32_t Magic;
	OsProcessId Pid;
	uint32_t WindowHandle;
	char OptionsPath[256];
	char Args[256];
	char Password[32];
	uint32_t Flags;
	uchar _Reserved_[92];

	bool Valid()
	{
		return
			#ifndef __llvm__
			this != 0 &&
			#endif
			(Magic == SCRIBE_INSTANCE_MAGIC || Magic == MUL_INSTANCE_MAGIC);
	}

	bool IsMul()
	{
		return Valid() && Magic == MUL_INSTANCE_MAGIC;
	}

	bool IsScribe()
	{
		return Valid() && Magic == SCRIBE_INSTANCE_MAGIC;
	}

	void Clear()
	{
		memset(this, 0, sizeof(ScribeIpcInstance));
	}
};

LString GetUtfArgs()
{
	OsAppArguments *Args = LAppInst->GetAppArgs();

	#if WINNATIVE
	LAutoString u(WideToUtf8(Args->lpCmdLine));
	return LString(u.Get());
	#else
	LStringPipe p;
	for (int i=1; i<Args->Args; i++)
	{
		if (i > 1) p.Push(" ");
		char *Sp = strchr(Args->Arg[i], ' ');
		if (Sp) p.Push("\"");
		p.Push(Args->Arg[i]);
		if (Sp) p.Push("\"");
	}
	return p.NewLStr();
	#endif
}

struct ScribeIpcPriv
{
	virtual ~ScribeIpcPriv() {}

	virtual void OnLoad(const char *FileName, LString *MulPassword, std::function<void(bool)> Callback) = 0;
	virtual bool OnPulse() = 0;
};

struct SocketIpc : public ScribeIpcPriv
{
	struct Connection : public LSocket
	{
		bool connecting = false;
		LString output;
		LArray<char> input;
		std::function<void(bool)> callback;
	};

	constexpr static int IPC_PORT = 4455;
	constexpr static const char *OptPid       = "Pid";
	constexpr static const char *OptArgs      = "Args";
	constexpr static const char *OptStatus    = "Status";
	constexpr static const char *OptProcessed = "Processed";
	constexpr static const char *OptIgnored   = "Ignored";

	LAutoPtr<LSocket> Listen;
	LArray<Connection*> Connections;

	const char *GetClass() { return "SocketIpc"; }

	SocketIpc()
	{
		if (Listen.Reset(new LSocket))
		{
			if (!Listen->Listen(IPC_PORT))
			{
				LgiTrace("%s:%i - Listen on %i failed.\n", _FL, IPC_PORT);
				Listen.Reset();
			}
		}
	}

	~SocketIpc()
	{
		Connections.DeleteObjects();
	}

	void OnArgs(LString Args)
	{
		LgiTrace("%s:%i - OnArgs: %s\n", _FL, Args.Get());
	}

	void OnData(Connection *c)
	{
		auto ptr = c->input.AddressOf();
		if (!ptr)
			return;
		auto endOfFile = Strnstr(ptr, "\n\n", c->input.Length());
		if (!endOfFile)
			return;

		// Data complete...
		int64 RemotePid = -1;
		LString Args;
		auto lines = LString(ptr, endOfFile-ptr).SplitDelimit("\n");
		for (auto ln: lines)
		{
			auto v = ln.SplitDelimit(":", 1);
			if (v.Length() == 2)
			{
				if (v[0].Equals(OptPid))
				{
					RemotePid = v[1].Int();
				}
				else if (v[0].Equals(OptArgs))
				{
					Args = v[1];
				}
				else if (v[0].Equals(OptStatus))
				{
					auto &response = v[1];
					auto status = !response.Equals(OptProcessed);
					LgiTrace("%s:%i - remote status: %i\n", _FL, status);
					if (c->callback)
						c->callback(status);

					Connections.Delete(c);
					DeleteObj(c);
					return;
				}
			}
		}

		if (RemotePid >= 0)
		{
			bool status = LProcessId() != RemotePid && Args.Length() > 0;
			LString response;
			response.Printf("%s:%s\n\n", OptStatus, status ? OptProcessed : OptIgnored);
			auto wr = c->Write(response.Get(), response.Length());
			LgiTrace("%s:%i - Wrote %i status bytes.\n", _FL, (int)wr);
			Connections.Delete(c);
			DeleteObj(c);
			if (status)
				OnArgs(Args);
		}
	}

	void OnLoad(const char *FileName, LString *MulPassword, std::function<void(bool)> Callback)
	{
		// Try and send data to running inst:
		LAutoPtr<Connection> s(new Connection);
		if (s)
		{
			s->IsBlocking(false);
			s->callback = Callback;
			s->connecting = true;
			s->output.Printf("%s:%u\n"
							"%s:%s\n"
							"\n",
							OptPid, LProcessId(),
							OptArgs, GetUtfArgs().Get());

			auto connected = s->Open("localhost", IPC_PORT);
			if (connected)
				s->connecting = false;
			LgiTrace("%s:%i - outbound connection: %i\n", _FL, connected);
			Connections.Add(s.Release());
			return;
		}
		
		if (Callback)
			Callback(true);
	}

	bool OnPulse()
	{
		if (Listen &&
			Listen->IsReadable())
		{
			LgiTrace("%s:%i - listen is readable\n", _FL);
			LAutoPtr<Connection> client(new Connection);
			if (Listen->Accept(client))
			{
				LgiTrace("%s:%i - got client\n", _FL);
				Connections.Add(client.Release());
			}
		}
		LArray<Connection*> dead;
		for (auto c: Connections)
		{
			if (c->connecting)
			{
				if (c->Open("localhost", IPC_PORT))
				{
					LgiTrace("%s:%i - outgoing connection success.\n", _FL);
					c->connecting = false;
				}
				else
				{
					LgiTrace("%s:%i - outgoing connection failed.\n", _FL);
				}
			}
			else if (c->output)
			{
				if (c->IsWritable(1))
				{
					auto wr = c->Write(c->output.Get(), c->output.Length());
					LgiTrace("%s:%i - Wrote %i to socket\n", _FL, (int)wr);
					if (wr != c->output.Length())
						dead.Add(c);
					else
						c->output.Empty();
				}
				else
				{
					LgiTrace("%s:%i - not writeable.\n", _FL);
				}
			}
			else if (c->IsReadable())
			{
				char buf[256];
				auto rd = c->Read(buf, sizeof(buf));
				LgiTrace("%s:%i - got %i bytes for connection\n", _FL, (int)rd);
				if (rd > 0)
				{
					c->input.Add(buf, rd);
					OnData(c);
				}
				else dead.Add(c);
			}
		}
		for (auto d: dead)
		{
			LgiTrace("%s:%i - deleting dead connection\n", _FL);
			Connections.Delete(d);
			DeleteObj(d);
		}

		return true;
	}
};

struct SharedMemIpc : public ScribeIpcPriv
{
	// Shared memory impl:
	LAutoPtr<LSharedMemory>	Mem;
	ScribeIpcInstance *ThisInst = NULL;
	ScribeIpcInstance *Mul = NULL;

	SharedMemIpc()
	{
		#ifdef HAIKU

		#else

			if (Mem.Reset(new LSharedMemory("Scribe", SCRIBE_INSTANCE_MAX * sizeof(ScribeIpcInstance)))
				&&
				Mem->GetPtr())
			{
				ScribeIpcInstance *InstLst = (ScribeIpcInstance*) Mem->GetPtr();
				for (int i=0; i<SCRIBE_INSTANCE_MAX; i++)
				{
					if (InstLst[i].Valid())
					{
						if (!LIsProcess(InstLst[i].Pid))
						{
							LgiTrace("Crashed instance %i\n", InstLst[i].Pid);
							InstLst[i].Clear();
						}
					}
					else if (!ThisInst)
					{
						// Use free slot for this instance:
						ThisInst = InstLst + i;
						memset(ThisInst, 0, sizeof(ScribeIpcInstance));
						ThisInst->Pid = LProcessId();
						ThisInst->Magic = SCRIBE_INSTANCE_MAGIC; // Do this last
					}
				}
			}
			else Mem.Reset();

		#endif
	}

	~SharedMemIpc()
	{
		Close();
	}

	void Close()
	{
		// Remove our instance from the shared memory
		if (Mem && Mem->GetPtr())
		{
			auto InstLst = (ScribeIpcInstance*) Mem->GetPtr();

			if (ThisInst)
				memset(ThisInst, 0, sizeof(*ThisInst));

			int c = 0;
			for (int i=0; i<SCRIBE_INSTANCE_MAX; i++)
			{
				if (InstLst[i].Valid())
					c++;
			}

			if (!c)
				Mem->Destroy();
		}	

		ThisInst = NULL;
		Mem.Reset();
	}

	ScribeIpcInstance *FindRunningInstance(const char *FileName, LString *MulPassword)
	{
		if (!Mem)
			return false;

		auto InstLst = (ScribeIpcInstance*) Mem->GetPtr();

		for (int i=0; i<SCRIBE_INSTANCE_MAX; i++)
		{
			if (InstLst[i].IsMul())
			{
				Mul = InstLst + i;
				if (MulPassword)
					*MulPassword = Mul->Password;
				break;
			}
		}

		for (int i=0; i<SCRIBE_INSTANCE_MAX; i++)
		{
			if (InstLst[i].IsScribe() && (InstLst+i) != ThisInst)
			{
				if (Mul || _stricmp(InstLst[i].OptionsPath, FileName) == 0)
				{
					int Pid = InstLst[i].Pid;
					if (!LIsProcess(Pid))
					{
						// Not a valid process... it died?
						continue;
					}

					return InstLst + i;
				}
			}
		}

		return NULL;
	}

	bool SendArgs(ScribeIpcInstance *RunningInst)
	{
		auto Utf8 = GetUtfArgs();
		if (!Utf8)
			return false;

		if (Utf8.Length() > 255)
		{
			RunningInst->Flags |= SCRIBE_IPC_LONG_ARGS;
			RunningInst->Flags &= ~SCRIBE_IPC_CONTINUE_ARGS;
			auto Len = Utf8.Length();
			for (char *u = Utf8.Get(); Len > 0; u += 255)
			{
				ssize_t Part = MIN(sizeof(RunningInst->Args)-1, Len);

				memcpy(RunningInst->Args, u, Part);
				Len -= Part;

				int64 Start = LCurrentTime();
				while (LCurrentTime() - Start < 60000)
				{
					if (TestFlag(RunningInst->Flags, SCRIBE_IPC_CONTINUE_ARGS) &&
						RunningInst->Args[0] == 0)
					{
						Start = 0;
						break;
					}
					LSleep(10);
				}
				if (Start)
				{
					LgiTrace("%s:%i - SendLA timed out.\n", _FL);
					break;
				}
			}

			RunningInst->Flags &= ~(SCRIBE_IPC_CONTINUE_ARGS | SCRIBE_IPC_LONG_ARGS);
		}
		else
		{
			strcpy_s(RunningInst->Args, sizeof(RunningInst->Args), Utf8);
		}

		return true;
	}

	void OnLoad(const char *FileName, LString *MulPassword, std::function<void(bool)> Callback)
	{
		// Search for other instances of Scribe
		auto RunningInst = FindRunningInstance(FileName, MulPassword);
		if (RunningInst)
		{
			if (SendArgs(RunningInst))
			{
				LgiTrace("Passed args to the other running instance of Scribe (pid=%i)\n", RunningInst->Pid);
				if (Callback)
					Callback(false);
				return;
			}

			if (Mul && LFileExists(Mul->OptionsPath))
			{
				// No instance of Scribe is running, but MUL may be keeping
				// the previously run instance around. So we should run that
				// by using the options file and password from MUL's instance
				// record.
				LAssert(!"Fixme");
				// d->Options.Reset(new LOptionsFile(Mul->OptionsPath));
			}
		}

		// Insert ourselves into the instance list
		if (ThisInst)
		{
			strcpy_s(ThisInst->OptionsPath, sizeof(ThisInst->OptionsPath), FileName);
		}

		if (Callback)
			Callback(true);
	}

	bool OnPulse()
	{
		bool Result = false;

		if (ThisInst && ValidStr(ThisInst->Args))
		{
			LStringPipe p;
			p.Push(ThisInst->Args);
			if (ThisInst->Flags & SCRIBE_IPC_LONG_ARGS)
			{
				ThisInst->Flags |= SCRIBE_IPC_CONTINUE_ARGS;

				int64 Start = LCurrentTime();
				while (	TestFlag(ThisInst->Flags, SCRIBE_IPC_LONG_ARGS) &&
					LCurrentTime() - Start < 60000)
				{
					ZeroObj(ThisInst->Args);
					while (	TestFlag(ThisInst->Flags, SCRIBE_IPC_LONG_ARGS) &&
						!ThisInst->Args[0] &&
						LCurrentTime() - Start < 60000)
					{
						LSleep(10);
					}
					p.Push(ThisInst->Args);
				}
			}
			ZeroObj(ThisInst->Args);

			LAutoString Arg(p.NewStr());
			if (Arg)
			{
				OsAppArguments AppArgs(0, 0);

				LgiTrace("Received cmd line: %s\n", Arg.Get());
				AppArgs.Set(Arg);

				LAppInst->SetAppArgs(AppArgs);

				if (LAppInst->GetOption("m") &&
					LAppInst->GetOption("f"))
					;
				else
					LAppInst->OnCommandLine();

				Result = true;
			}
		}

		return Result;
	}
};

ScribeIpc::ScribeIpc()
{
	#ifdef HAIKU
	d = new SocketIpc;
	#else
	d = new SharedMemIpc;
	#endif
}

ScribeIpc::~ScribeIpc()
{
	DeleteObj(d);
}

void ScribeIpc::OnLoad(const char *FileName, LString *MulPassword, std::function<void(bool)> Callback)
{
	d->OnLoad(FileName, MulPassword, Callback);
}

bool ScribeIpc::OnPulse()
{
	return d->OnPulse();
}
