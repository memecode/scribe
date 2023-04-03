#ifndef _SCRIBE_SHARED_MEM_H_
#define _SCRIBE_SHARED_MEM_H_

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

#endif
