#pragma once

class ScribeIpc
{
	struct ScribeIpcPriv *d;

public:
	ScribeIpc();
	virtual ~ScribeIpc();

	void OnLoad(const char *FileName, LString *MulPassword, std::function<void(bool)> Callback);
	bool OnPulse();
};