#include "lgi/common/Lgi.h"
#include "lgi/common/Net.h"

#include "../common/StringClass.h"
#include "context.h"

struct PrintLog : public LStream
{
	ssize_t Write(const void *Ptr, ssize_t Size, int Flags = 0) override
	{
		printf("%.*s", (int)Size, (const char*)Ptr);
		return Size;
	}
};

static PrintLog mainLog;

LString Context::fullPath(LDataFolderI *f)
{
	LString::Array p;
	while (f)
	{
		p.Add(f->GetStr(FIELD_FOLDER_NAME));
		f = dynamic_cast<LDataFolderI*>(f->GetObj(FIELD_PARENT));
	}
	return LString(sep).Join(p.Reverse().Slice(1, -1)) + ".csv";
}

FolderMeta *Context::getMeta(LDataFolderI *f)
{
	if (!f)
		return nullptr;
	auto m = folderMetaData.Find(f);
	if (m)
		return m;

	LFile::Path inst(LSP_APP_INSTALL);
	auto full = fullPath(f);
	LAssert(full);

	m = new FolderMeta((inst / full).GetFull(), &log);
	m->serialize(false);
	folderMetaData.Add(f, m);

	return m;
}

Context::Context()
	: log(mainLog)
{
	LFile::Path p(LSP_APP_DATA);
	optionsPath = p / "options.json";
	if (LFileExists(optionsPath))
		options.SetJson(LReadFile(optionsPath));
	validateOptions();
}

Context::~Context()
{
}

bool Context::authenticateUser(const char *user, const char *pass)
{
    auto optUser = options.Get(Context::OptImapUser);
    auto optPass = options.Get(Context::OptImapPass);
    return optUser == user && optPass == pass;
}

bool Context::saveOptions()
{
	LFile out(optionsPath, O_WRITE);
	if (out)
		out.Write(options.GetJson());
	else
		return false;
	return true;
}

bool Context::validateOptions()
{
	bool modified = false;
	auto chkOpt = [&](const char *name, const char *defVal) {
		if (!options.Get(name))
		{
			options.Set(name, defVal);
			modified = true;
		}
	};

	chkOpt(OptMapiProfile, "Outlook");
	chkOpt(OptMapiUser, "----");

	chkOpt(OptImapUser, "----");
	chkOpt(OptImapPass, "----");
	chkOpt(OptImapPort, LString::Fmt("%i", IMAP_PORT));

	chkOpt(OptSmtpPort, LString::Fmt("%i", SMTP_PORT));

	if (modified)
		saveOptions();
	return false;
}

LDataFolderI *Context::GetFolder(LString path)
{
	auto parts = path.SplitDelimit(sep);
	LDataFolderI *f = root;
	for (auto &p: parts)
	{
		// find 'p' in the children of 'f'
		LDataFolderI *match = nullptr;
		auto &it = f->SubFolders();
		if (it.GetState() != Store3Loaded)
			continue;
		for (auto c = it.First(); c; c = it.Next())
		{
			if (auto cFolder = dynamic_cast<LDataFolderI*>(c))
			{
				auto name = cFolder->GetStr(FIELD_FOLDER_NAME);
				if (p.Equals(name))
				{
					match = cFolder;
					break;
				}
			}
		}
		if (match)
			f = match;
		else
			return nullptr;
	}

	return f;
}

void Context::ForAllFolders(LArray<FolderInfo> &info, LDataFolderI *f, LString::Array path, int depth)
{
	LAssert(path.Length() == depth);

	size_t idx = info.Length();
	{
		auto &i = info.New();
		i.folder = f;
		i.name = f->GetStr(FIELD_FOLDER_NAME);
		i.full = path;
		i.full.Add(i.name);
		i.depth = depth;
	}

	auto &it = f->SubFolders();
	if (it.GetState() == Store3Loaded)
		for (auto c = it.First(); c; c = it.Next())
		{
			if (auto cFolder = dynamic_cast<LDataFolderI*>(c))
			{
				auto &i = info[idx];
				ForAllFolders(info, cFolder, i.full, depth + 1);
				i.subFolders++;
			}
		}
}

void Context::SegToStructure(LStringPipe &p, LDataPropI *seg, int depth)
{
	if (!seg)
		return;
	auto children = seg->GetList(FIELD_MIME_SEG);
	bool hasChild = children->Length() > 0;

	if (hasChild || depth == 0)
		p.Print("(");

	for (auto child = children->First(); child; child = children->Next())
	{
		SegToStructure(p, child, depth + 1);
		p.Print(" ");
	}

	auto mimeType = seg->GetStr(FIELD_MIME_TYPE);
	auto mimeParts = LString(mimeType).SplitDelimit("/");
	auto multi = mimeParts[0].Equals("multipart");
	if (multi)
	{
		LString hdrs = seg->GetStr(FIELD_INTERNET_HEADER);
		auto contentType = LGetHeaderField(hdrs, "Content-Type");
		auto boundary = LGetSubField(contentType, "boundary");

		p.Print("\"%s\" (\"boundary\" \"%s\")  NIL NIL NIL", mimeParts[1].Get(), boundary.Get());
	}
	else // single
	{
		p.Print("(\"%s\" \"%s\"", mimeParts[0].Get(), mimeParts[1].Get());

		// figure out what fields to print
		LString::Array fields;
		if (auto charSet = seg->GetStr(FIELD_CHARSET))
			fields.New().Printf("\"charset\" \"%s\"", charSet);
		if (auto name = seg->GetStr(FIELD_NAME))
			fields.New().Printf("\"name\" \"%s\"", name);

		if (fields.Length())
			p.Print(" (%s)", LString(" ").Join(fields).Get());
		else
			p.Print(" NIL");

		if (auto contentId = seg->GetStr(FIELD_CONTENT_ID))
			p.Print(" \"%s\"", contentId);
		else
			p.Print(" NIL");

		p.Print(" NIL"); // Content description

		// FIXME:
		p.Print(" NIL"); // Content-Transfer-Encoding

		auto size = seg->GetInt(FIELD_SIZE);
		p.Print(" " LPrintfSizeT ")", size);
	}

	if (hasChild || depth == 0)
		p.Print(")");
}

LString Context::BodyStructure(LDataI *mail)
{
	LStringPipe p;

	if (auto root = mail->GetObj(FIELD_MIME_SEG))
		SegToStructure(p, root);
	else
		LAssert(!"no seg?");

	return p.NewLStr();
}

LArray<Context::FolderInfo> Context::FolderList()
{
	LArray<FolderInfo> a;
	LString::Array full;
	if (root)
		ForAllFolders(a, root, full);
	return a;
}

LString::Array Context::Fetch(LDataFolderI *folder, bool isUid, LString arg, LString fieldSpec)
{
	auto argRange = arg.SplitDelimit(":");
	LString::Array a;
	if (!folder)
	{
		log.Print("%s:%i - error: no folder.\n", _FL);
		return a;
	}

	auto meta = getMeta(folder);
	if (!meta)
	{
		log.Print("%s:%i - error: no meta.\n", _FL);
		return a;
	}

	LAssert(isUid); // don't support not UID yet...

	int maxUid = 0;
	auto fields = fieldSpec.Strip("()").SplitDelimit();
	auto &it = folder->Children();
	for (auto i = it.First(); i; i = it.Next())
	{
		if (i->Type() != MAGIC_MAIL)
			continue;

		if (auto msgId = i->GetStr(FIELD_MESSAGE_ID))
		{
			auto uid = meta->getUid(msgId);
			maxUid = MAX(maxUid, uid);

			if (uid != FolderMeta::INVALID)
			{
				// is the UID in range?
				bool match = false;
				if (argRange.Length() == 2)
				{
					match = (argRange[0].Equals("*") || uid >= argRange[0].Int())
							&&
							(argRange[1].Equals("*") || uid <= argRange[1].Int());
				}
				else if (argRange.Length() == 1)
				{
					match = argRange[0].Int() == uid;
				}
				else LAssert(!"unexpected arg range count");

				if (!match)
					continue;

				// Create a result record...
				LStringPipe record;
				record.Print("* %i FETCH (", uid);

				int idx = 0;
				for (auto &fld: fields)
				{
					auto space = idx++ ? " " : "";
					if (fld.Equals("FLAGS"))
					{
						auto flags = i->GetInt(FIELD_FLAGS);
						LString::Array imapFlags;
						if (flags & MAIL_READ)
							imapFlags.Add("/Seen");
						record.Print("%sFLAGS (%s)", space, LString(" ").Join(imapFlags).Get());
					}
					else if (fld.Equals("UID"))
					{
						record.Print("%sUID %i", space, uid);
					}
					else if (fld.Equals("RFC822.SIZE"))
					{
						auto sz = i->GetInt(FIELD_SIZE);
						record.Print("%sRFC822.SIZE " LPrintfInt64, space, sz);
					}
					else if (fld.Equals("BODYSTRUCTURE"))
					{
						// FIXME
						record.Print("%sBODYSTRUCTURE %s", space, BodyStructure(i).Get());
					}
					else if (fld.Equals("BODY.PEEK[HEADER]"))
					{
						auto inetHdr = i->GetStr(FIELD_INTERNET_HEADER);
						auto len = Strlen(inetHdr);
						record.Print("%sBODY.PEEK[HEADER] {" LPrintfInt64 "}\r\n%s", space, len, inetHdr);
					}
					else if (fld.Equals("BODY.PEEK[]"))
					{
						if (auto rfc822 = i->GetStream(_FL))
						{
							auto len = rfc822->GetSize();
							record.Print("%sBODY[] {" LPrintfInt64 "}\r\n", space, len);
							LCopyStreamer copy;
							copy.Copy(rfc822, &record);
						}
						else
							LAssert(0);
					}
					else
					{
						LAssert(!"not implemented");
					}
				}

				record.Print(")\r\n");
				a.Add(record.NewLStr());
			}
		}
	}

	meta->save();

	if (a.Length() == 0)
	{
		log.Print("Warn: fetched no records '%s', '%s', maxUid=%i\n",
			arg.Get(),
			fieldSpec.Get(),
			maxUid);
	}

	return a;
}

// e.g. A0861 UID STORE 875 FLAGS (\\seen)
LString::Array Context::Store(LDataFolderI *folder, bool isUid, LArray<LString> params, LError &err)
{
	LString::Array a;
	if (!folder)
	{
		err.Set(LErrorInvalidParam, "no folder");
		log.Print("%s:%i - error: no folder.\n", _FL);
		return a;
	}

	auto meta = getMeta(folder);
	if (!meta)
	{
		err.Set(LErrorInvalidParam, "no meta");
		log.Print("%s:%i - error: no meta.\n", _FL);
		return a;
	}

	LAssert(isUid); // don't support not UID yet...

	if (params.Length() != 3)
	{
		err.Set(LErrorInvalidParam, "invalid arg count");
		log.Print("%s:%i - error: unexpected arg count: %s.\n",
			_FL, LString(",").Join(params).Get());
		return a;
	}

	auto storeUids = params[0].SplitDelimit(",");
	auto &storeField = params[1];
	auto &storeValue = params[2];

	bool foundObj = false;
	auto &it = folder->Children();
	for (auto i = it.First(); i; i = it.Next())
	{
		if (i->Type() != MAGIC_MAIL)
			continue;
		
		if (auto msgId = i->GetStr(FIELD_MESSAGE_ID))
		{
			auto uid = meta->getUid(msgId);
			bool match = false;
			for (auto &u: storeUids)
				if (u.Int() == uid)
					match = true;
			if (!match)
				continue;

			log.Print("%s:%i STORE on msgId='%s' uid=%u\n", _FL, msgId, uid);
			foundObj = true;

			if (storeField.Equals("FLAGS"))
			{
				auto flags = storeValue.Strip("()").SplitDelimit();
				auto curFlags = i->GetInt(FIELD_FLAGS);
				int64_t newFlags = curFlags & (~MAIL_READ);
				bool deleteFlag = false;
				for (auto &f: flags)
				{
					if (f.Equals("\\seen"))
						newFlags = MAIL_READ;
					else if (f.Equals("\\deleted"))
						deleteFlag = true;
					else
						log.Print("%s:%i - unsupported store flag '%s'\n", _FL, f.Get());
				}

				if (deleteFlag)
				{
					log.Print("%s:%i - deleting message '%s' uid=%u\n", _FL, msgId, uid);
					auto status = i->Delete();
					log.Print("%s:%i - delete status=%i\n", _FL, status);
				}
				else if (newFlags != curFlags)
				{
					i->SetInt(FIELD_FLAGS, newFlags);
				}
			}
			else
			{
				err.Set(LErrorInvalidParam, "unsupported store field");
				log.Print("%s:%i - unsupported store field '%s'\n", _FL, storeField.Get());
				LAssert(!"unsupported field");
			}
		}
	}

	if (!foundObj)
	{
		err.Set(LErrorPathNotFound, "message not found");
		log.Print("%s:%i STORE failed to find '%s'\n", _FL, params[0].Get());
	}

	return a;
}

LString::Array Context::Search(LDataFolderI *folder, bool isUid, LArray<LString> params, LError &err)
{
	LString::Array a;
	if (!folder)
	{
		err.Set(LErrorInvalidParam, "no folder");
		log.Print("%s:%i - error: no folder.\n", _FL);
		return a;
	}

	auto meta = getMeta(folder);
	if (!meta)
	{
		err.Set(LErrorInvalidParam, "no meta");
		log.Print("%s:%i - error: no meta.\n", _FL);
		return a;
	}

	LAssert(isUid); // don't support not UID yet...

	auto &it = folder->Children();
	for (auto i = it.First(); i; i = it.Next())
	{
		if (i->Type() != MAGIC_MAIL)
			continue;

		if (auto msgId = i->GetStr(FIELD_MESSAGE_ID))
		{
			auto uid = meta->getUid(msgId);
			if (uid != FolderMeta::INVALID)
				continue;

			// Does 'i' match the search params?
			bool match = false;
			for (auto &p: params)
			{
				if (p.Equals("RECENT"))
				{
					auto flags = i->GetInt(FIELD_FLAGS);
					if (!(flags & MAIL_READ))
					{
						match = true;
					}
				}
				else
				{
					err.Set(LErrorNotSupported, "field not supported");
					LAssert(!"Impl support for field");
				}
			}

			if (match)
				a.New().Printf("* SEARCH %i\r\n", uid);
		}
	}

	return a;
}
