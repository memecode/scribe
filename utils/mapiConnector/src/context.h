#pragma once

#include "lgi/common/Lgi.h"
#include "lgi/common/Json.h"
#include "lgi/common/Store3.h"

struct FolderMeta
{
	LStream *log = nullptr;

	// Storage:
	LString path;
	bool dirty = false;

	// Map the message ID's to UID's
	constexpr static int INVALID = -1;
	int nextUid = 0;
	LHashTbl<ConstStrKey<char>, int> uidMap;

	FolderMeta(LString fullPath, LStream *logger);
	~FolderMeta();

	void save();
	bool serialize(bool write);
	int getUid(const char *msgId);
};

struct Context
{
	constexpr static const char *OptMapiProfile = "profile";
	constexpr static const char *OptMapiUser = "mapiUser";

	constexpr static const char *OptImapUser = "imapUser";
	constexpr static const char *OptImapPass = "imapPass";
	constexpr static const char *OptImapPort = "imapPort";

	constexpr static const char *OptSmtpPort = "smtpPort";

	LJson options;
	LString optionsPath;
	const char *sep = ".";
	LAutoPtr<LDataStoreI> store;
	LStream &log;
	LDataFolderI *root = nullptr;
	LHashTbl<PtrKey<LDataFolderI*>, FolderMeta*> folderMetaData;

	Context();
	~Context();

	// User authentication:
	bool authenticateUser(const char *user, const char *pass);

	LString fullPath(LDataFolderI *f);
	FolderMeta *getMeta(LDataFolderI *f);
	bool saveOptions();
	bool validateOptions();
	LDataFolderI *GetFolder(LString path);

	struct FolderInfo {
		LDataFolderI *folder = nullptr;
		LString name;
		LString::Array full;
		int subFolders = 0;
		int depth = 0;
	};

	void ForAllFolders(LArray<FolderInfo> &info, LDataFolderI *f, LString::Array path, int depth = 0);
	void SegToStructure(LStringPipe &p, LDataPropI *seg, int depth = 0);
	LString BodyStructure(LDataI *mail);
	LArray<FolderInfo> FolderList();
	LString::Array Fetch(LDataFolderI *folder, bool isUid, LString arg, LString fieldSpec);

	// e.g. A0861 UID STORE 875 FLAGS (\seen)
	LString::Array Store(LDataFolderI *folder, bool isUid, LArray<LString> params);
	LString::Array Search(LDataFolderI *folder, bool isUid, LArray<LString> params);
};
