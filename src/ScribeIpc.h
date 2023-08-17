#pragma once

class ScribeIpc
{
	struct ScribeIpcPriv *d;

public:
	ScribeIpc(LView *view);
	virtual ~ScribeIpc();

	void OnLoad(const char *FileName,
				LString *MulPassword,
				// This is called with:
				// - false: if the arguments were successfully passed to 
				//          another running instance of Scribe.
				// - true:  if the arguments should be processed locally.
				std::function<void(bool)> Callback);
	bool OnPulse();
};