#include "lgi/common/Lgi.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/TextConvert.h"

#include "ScribeImap.h"
#include "ScribeUtils.h"
#include "resdefs.h"

#if IMAP_PROTOBUF
#include "include/Scribe.pb.cc"
#endif


const char *ToString(ImapMail::ImapMailState s)
{
	switch (s)
	{
		case ImapMail::ImapMailIdle: return "ImapMailIdle";
		case ImapMail::ImapMailGettingBody: return "ImapMailGettingBody";
		case ImapMail::ImapMailMoving: return "ImapMailMoving";
		case ImapMail::ImapMailDeleting: return "ImapMailDeleting";
	}
	return "#errInvalidImapMailState";
}


ImapMail::ImapMail(ImapStore *store, const char *file, int line, uint32_t uid) :
	AllocFile(file), AllocLine(line),
	From(store),
	Reply(store)
{
	LocalFlags = 0;
	Seg = 0;
	State = ImapMailIdle;
	Loaded = Store3Unloaded;
	Parent = 0;
	SegDirty = false;
	RemoteFlags.ImapSeen = true;
	Priority = MAIL_PRIORITY_NORMAL;
	Store = store;
	Uid = uid;
}

ImapMail::~ImapMail()
{
	if (Parent)
	{
		Parent->DelMail(this);
	}

	DeleteObj(Seg);
	To.DeleteObjects();
}

void ImapMail::SetRemoteFlags(ImapMailFlags f)
{
	RemoteFlags = f;

	// Replicate to local flags as well...
	if (f.ImapSeen) LocalFlags |= MAIL_READ;
	else LocalFlags &= ~MAIL_READ;

	if (f.ImapAnswered) LocalFlags |= MAIL_REPLIED;
	else LocalFlags &= ~MAIL_REPLIED;
}

ImapMail::IMeta ImapMail::GetMeta(bool AllowCreate)
{
	if (Data)
		return Data->GetMeta(Uid, AllowCreate);

	if (Parent)
		return Parent->GetMeta(Uid, AllowCreate);
		
	return NULL;
}

void ImapMail::Serialize(IMeta m, bool Write)
{
	LAssert(m != NULL);
	if (!m) return;

	#if IMAP_PROTOBUF

		if (Write)
		{
		}
		else
		{
		}

	#else

		if (Write)
		{
			// Mail -> Meta
			m->SetAttr(ATTR_FLAGS, RemoteFlags.Get());

			if (DateSent.IsValid())
			{
				char c[32];
				DateSent.SetFormat(GDTF_YEAR_MONTH_DAY);
				DateSent.ToUtc();
				DateSent.Get(c, sizeof(c));
				DateSent.SetFormat(DateSent.GetDefaultFormat());
				ValidateImapDate(DateSent);

				m->SetAttr(ATTR_DATE, c);
			}

			m->SetAttr(ATTR_LABEL, Label);
			m->SetAttr(ATTR_COLOUR, (int64)Colour.c32());
		
			char *l = Path ? strrchr(Path, DIR_CHAR) : 0;
			if (l)
				m->SetContent(l + 1);
		
			if (ValidStr(Subject))
				m->SetAttr(ATTR_SUBJECT, Subject);
			else
				m->DelAttr(ATTR_SUBJECT);
		
			LVariant v;
			if (From.GetVariant("Text", v))
				m->SetAttr(ATTR_FROM, v.Str());
			if (Reply.GetVariant("Text", v))
				m->SetAttr(ATTR_REPLYTO, v.Str());

			Parent->SetDirty();
		}
		else
		{
			// Meta -> Mail
			auto Flags = m->GetAttr(ATTR_FLAGS);
			if (Flags)
				RemoteFlags.Set(Flags);
			
			char *sLocal = m->GetAttr(ATTR_LOCAL);
			if (sLocal)
				LocalFlags = atoi(sLocal);
			else if (Flags)
				// Synthesis from remote flags
				LocalFlags = (RemoteFlags.ImapAnswered ? MAIL_REPLIED : 0) |
							 (RemoteFlags.ImapSeen ? MAIL_READ : 0);
		
			char *s = m->GetAttr(ATTR_DATE);
			DateSent.SetFormat(GDTF_YEAR_MONTH_DAY);
			DateSent.SetTimeZone(0, false);
			DateSent.Set(s);
			DateSent.SetFormat(DateSent.GetDefaultFormat());

			// Hmmmm: still don't know where these are coming from
			if (DateSent.Year() == 2000 &&
				DateSent.Hours() == 0)
			{
				DateSent.Empty();
				if (Parent && !Parent->IsLoading())
					Parent->SetDirty();
			}

			DateReceived = DateSent;

			Label = m->GetAttr(ATTR_LABEL);
			char *c = m->GetAttr(ATTR_COLOUR);
			if (c)
				Colour.Set((uint32_t)atoi64(c), 32);
			else
				Colour.Empty();
		
			Subject = m->GetAttr(ATTR_SUBJECT);

			LVariant FromText = m->GetAttr(ATTR_FROM);
			#if 1
				From.SetVariant("Text", FromText);
			#else
				char *sFrom = FromText.Str();
				if (sFrom)
				{
					LRange Nm(-1), Em(-1);
					for (char *c = sFrom; *c; c++)
					{
						if (*c == '\"')
							if (Nm.Start < 0) Nm.Start = c - sFrom + 1;
							else Nm.Len = c - sFrom - Nm.Start;
						else if (*c == '<')
							Em.Start = c - sFrom + 1;
						else if (*c == '>')
							Em.Len = c - sFrom - Em.Start;
					}

					if (Nm.Len)
					{
						if (Nm.Start >= 0)
							From.Name.Set(sFrom + Nm.Start, Nm.Len);
						else
							LAssert(0);
					}
					if (Em.Len)
					{
						if (Em.Start >= 0)
							From.Addr.Set(sFrom + Em.Start, Em.Len);
						else
							LAssert(0);
					}
				}
			#endif

			if (Parent && m->GetContent())
			{
				// Trip whitespace of the end of the filename...
				char *e = m->GetContent() + strlen(m->GetContent());
				while (e > m->GetContent() && strchr(" \t\r\n", e[-1])) e--;
				*e = 0;

				char p[MAX_PATH_LEN];
				LMakePath(p, sizeof(p), Parent->Local, m->GetContent());
				Path = p;
			}
		}

	#endif
}

void ImapMail::SetState(ImapMailState s, const char *file, int line)
{
	// LgiTrace("%s:%i - %i::SetState(%s)\n", file, line, Uid, ToString(s));
	State = s;
}

void ImapMail::Load()
{
	if (Loaded == Store3Unloaded)
	{
		LFile f;
		if (LFileExists(Path) &&
			f.Open(Path, O_READ))
		{
    		Loaded = Store3Headers;

			LArray<char> Buf;
			int Chunk = 2 << 10;
			while (!HeaderCache)
			{
				Buf.Length(Buf.Length() + Chunk);
				f.Read(&Buf[Buf.Length()-Chunk], Chunk);
				auto e = Strnstr(&Buf[0], "\r\n\r\n", Buf.Length());
				if (e)
				{
					ssize_t HeaderSize = e - &Buf[0];
					HeaderCache.Reset(NewStr(&Buf[0], HeaderSize));
					break;
				}

				if ((int64)Buf.Length() >= f.GetSize())
					break;
			}

			LAutoString Type(InetGetHeaderField(HeaderCache, "Content-Type"));
			if (Type)
			{
				if (_strnicmp(Type, "multipart/", 10) == 0 &&
					_strnicmp(Type, sMultipartAlternative, 21) != 0)
				{
					SetInt(FIELD_FLAGS, LocalFlags | MAIL_ATTACHMENTS);
				}
			}
		}
	}
}

Store3Status ImapMail::SetRfc822(LStreamI *m)
{
	DeleteObj(Seg);
	
	if (!Stream.Reset(new LTempStream(ScribeTempPath())))
	{
		LgiTrace("%s:%i - Alloc failed.\n", _FL);
		return Store3Error;
	}
	
	LCopyStreamer Cp;
	if (!Cp.Copy(m, Stream))
	{
		LgiTrace("%s:%i - LCopyStreamer copy failed.\n", _FL);
		return Store3Error;
	}

	Loaded = Store3Loaded;

	return Store3Success;
}

#define DEBUG_READ_MIME				0

void ImapMail::ReadMime(IMeta MetaTag)
{
#if DEBUG_READ_MIME
LProfile Prof("ReadMime");
#endif
	if (Loaded == Store3Loaded || !Uid)
		return;

	auto t = MetaTag ? MetaTag : GetMeta();
	if (!t)
	{
		LAssert(!"No Meta");
		return;
	}
	
#if DEBUG_READ_MIME
Prof.Add(_FL);
#endif
	bool exists = LFileExists(Path);
	// LgiTrace("ReadMime Path=%s exists=%i\n", Path.Get(), exists);
	if (!LFileExists(Path))
	{
		if (State != ImapMailGettingBody)
		{
			// bool InParent = Parent && Parent->GetMail(Uid);

			// We don't have a local copy of the message, tell the thread to fetch it...
			auto Msg = new ImapMsg(IMAP_DOWNLOAD, _FL);
			if (Msg)
			{
#if DEBUG_READ_MIME
Prof.Add(_FL);
#endif
				ImapMailInfo &Info = Msg->Mail.New();
				#if IMAP_PROTOBUF
					Info.Size = t->size();
				#else
					Info.Size = t->GetAsInt(ATTR_SIZE);
				#endif
				Info.Uid = Uid;
				Info.Local = Path.Get();

				Msg->Parent = Parent->Remote.Get();
				if (Store->PostThread(Msg, true))
				{
					SetState(ImapMailGettingBody, _FL);
					Loaded = Store3Loading;
				}
				else
					LgiTrace("%s:%i - PostThread failed.\n", _FL);
			}
		}
	}
	else
	{
		// Load the rest of the mime parts
		Loaded = Store3Loaded;

#if DEBUG_READ_MIME
Prof.Add(_FL);
#endif
		LStreamI *Load = NULL;
		LFile f;
		if (Stream)
		{
			Load = Stream;
		}
		else if (f.Open(Path, O_READ))
		{
			Load = &f;
		}
		
#if DEBUG_READ_MIME
Prof.Add(_FL);
#endif
		if (Load)
		{
			if (Load->GetSize() < 8)
			{
				// Hmmm, really? Try and re-download the mail
				auto Msg = new ImapMsg(IMAP_DOWNLOAD, _FL);
				if (Msg)
				{
					Loaded = Store3Headers;

#if DEBUG_READ_MIME
Prof.Add(_FL);
#endif
					auto &Info = Msg->Mail.New();
					#if IMAP_PROTOBUF
						Info.Size = t->size();
						Info.Uid = t->uid();
					#else
						Info.Size = t->GetAsInt(ATTR_SIZE);
						Info.Uid = t->GetAsInt(ATTR_UID);
					#endif

					Info.Local = Path.Get();
					Msg->Parent = Parent->Remote.Get();
					Store->PostThread(Msg, true);
				}
			}
			else
			{
				LAutoPtr<LMime> Mime(new LMime);
				if (Mime)
				{
					uint64 Start = LCurrentTime();
					
#if DEBUG_READ_MIME
Prof.Add(_FL);
#endif
					Mime->Text.Decode.Pull(Load);
					DeleteObj(Seg);
#if DEBUG_READ_MIME
Prof.Add(_FL);
#endif
					Seg = new ImapAttachment(Store, this, Mime.Release());

#if DEBUG_READ_MIME
Prof.Add(_FL);
#endif
					if (Seg->IsMixed())
						SetInt(FIELD_FLAGS, LocalFlags | MAIL_ATTACHMENTS);
					
					uint64 Dur = LCurrentTime() - Start;
					if (Dur > 100)
						LgiTrace("%s:%i - MimeDecode: " LPrintfInt64 "\n", _FL, Dur);
				}
			}
		}
	}
}

bool ImapMail::FindSegs(const char *Type, LArray<ImapAttachment*> &Segs)
{
	if (!Seg)
		ReadMime(NULL);

	if (Seg)
	{
		Seg->FindSegs(Type, Segs);
		
		for (unsigned i=0; i<Segs.Length(); i++)
		{
		    ImapAttachment *a = Segs[i];
		    auto Fn = a->GetSeg()->LGetFileName();
		    if (Fn)
		        Segs.DeleteAt(i--, true);
		}
	}

	return Segs.Length() > 0;
}

static auto DefaultCharset = "windows-1252";

const char *ImapMail::GetStr(int id)
{
	if (State != ImapMailIdle &&
		id != FIELD_INTERNET_HEADER &&
		id != FIELD_MESSAGE_ID &&
		id != FIELD_SERVER_UID)
	{
		if (id != FIELD_SUBJECT)
			return NULL;

		// LgiTrace("%s:%i - State=%i, Uid=%i(0x%x)\n", _FL, State, Uid, Uid);
		if (State == ImapMailGettingBody)
		{
			static const char *Loading = NULL;
			if (!Loading)
				Loading = LLoadString(IDS_LOADING);
			return Loading;
		}
		else if (State == ImapMailDeleting)
		{
			static const char *Deleting = NULL;
			if (!Deleting)
				Deleting = LLoadString(IDS_DELETING);
			return Deleting;
		}
		else if (State == ImapMailMoving)
		{
			return (char*)"Moving...";
		}

		return NULL;
	}

	switch (id)
	{
		case FIELD_DEBUG:
		{
			static char s[64];
			sprintf_s(s, sizeof(s), "Imap.ServerUid=%i", Uid);
			return s;
		}
		case FIELD_INTERNET_HEADER:
		{
			if (Seg && Seg->GetSeg())
				return Seg->GetSeg()->GetHeaders();
			
			if (Path)
			{
				if (LFileExists(Path))
				{
					Load();
				}
				else if (State == ImapMailIdle)
				{
					// Request the headers from the worker thread...
					auto Msg = new ImapMsg(IMAP_DOWNLOAD, _FL);
					if (Msg)
					{
						auto t = GetMeta();
						ImapMailInfo &Info = Msg->Mail.New();
						#if IMAP_PROTOBUF
							Info.Size = t ? t->size() : -1;
							Info.Uid = t ? t->uid() : 0;
						#else
							Info.Size = t ? t->GetAsInt(ATTR_SIZE) : -1;
							Info.Uid = Uid;
						#endif

						Info.Local = Path.Get();
						Msg->Parent = Parent->Remote.Get();

						if (Store->PostThread(Msg, false))
						{
							SetState(ImapMailGettingBody, _FL);
							Loaded = Store3Loading;
						}
						else LgiTrace("%s:%i - PostThread failed.\n", _FL);
					}
				}
			}

			return HeaderCache;
		}
		case FIELD_MESSAGE_ID:
		{
			if (!MsgId)
			{
				auto m = GetMeta();
				if (m)
					#if IMAP_PROTOBUF
					MsgId = m->messageid().c_str();
					#else
					MsgId = m->GetAttr(ATTR_MSGID);
					#endif

				if (!MsgId)
				{
					auto Headers = GetStr(FIELD_INTERNET_HEADER);
					auto MsgIdHdr = LGetHeaderField(Headers, "Message-ID");
					MsgId = LDecodeRfc2047(MsgIdHdr).Replace(" ", "").Replace("\n", "");
					if (MsgId)
					{
						auto hasNewLine = strchr(MsgId, '\n');
					    LAssert(!hasNewLine);
					    
					    if (m)
					    {
							#if IMAP_PROTOBUF
							m->set_messageid(MsgId);
							#else
						    m->SetAttr(ATTR_MSGID, MsgId);
							#endif
						    Parent->SetDirty();
					    }
					}
				}
			}

			return MsgId;
		}
		case FIELD_SUBJECT:
		{
			if (!Subject)
			{
				auto Headers = GetStr(FIELD_INTERNET_HEADER);
				if (!Headers)
					return LLoadString(IDS_LOADING);

				auto enc = LGetHeaderField(Headers, "Subject");
				// LgiTrace("EncSubj: %s\n", enc.Get());
				Subject = LDecodeRfc2047(enc);
				// LgiTrace("DecSubj: %s\n", Subject.Get());

				auto Meta = GetMeta();
				if (Meta)
					Serialize(Meta, true);
			}
			return Subject;
		}
		case FIELD_CHARSET:
		{
			// Conversion is done locally.
			return "utf-8";
		}
		case FIELD_TEXT:
		{
			LArray<ImapAttachment*> Results;
			if (!TextCache && FindSegs("text/plain", Results))
			{
				LStringPipe p;
				for (auto Segment: Results)
				{
					if (Segment->GetInt(FIELD_SIZE) > (512 << 10))
						continue;

					LAutoStreamI Stream = Segment->GetStream(_FL);
					if (Stream)
					{
						LAutoString Buf;
						auto Size = Stream->GetSize();
						if (Size > 0)
						{
							Buf.Reset(new char[Size+1]);
							auto rd = Stream->Read(Buf, Size);
							if (rd <= 0)
								continue;
							Buf.Get()[rd] = 0;

							LString Charset = Segment->GetStr(FIELD_CHARSET);
							if (!Charset)
								Charset = DetectCharset(Buf.Get());
							if (!Charset)
								Charset = DefaultCharset;

							LAutoString Utf8;
							if (!Stricmp(Charset.Get(), "utf-8") && LIsUtf8(Buf))
								Utf8 = Buf;
							else
								Utf8.Reset((char*)LNewConvertCp("utf-8", Buf, Charset, rd));

							if (Results.Length() > 1)
							{
								p.Push(Utf8);
							}
							else
							{
								TextCache = Utf8;
								return TextCache;
							}
						}
					}
				}

				TextCache.Reset(p.NewStr());
			}
			return TextCache;
			break;
		}
		case FIELD_HTML_CHARSET:
		{
			LArray<ImapAttachment*> Results;
			if (FindSegs("text/html", Results))
				return Results[0]->GetStr(FIELD_CHARSET);
			break;
		}
		case FIELD_ALTERNATE_HTML:
		{
			LArray<ImapAttachment*> Results;
			if (!HtmlCache && FindSegs("text/html", Results))
			{
				LAutoStreamI s = Results[0]->GetStream(_FL);
				if (s)
				{
					auto Size = s->GetSize();
					if (Size > 0)
					{
						HtmlCache.Reset(new char[Size+1]);
						s->Read(HtmlCache, Size);
						HtmlCache[Size] = 0;
					}
				}
			}
			return HtmlCache;
			break;
		}
		case FIELD_CACHE_FLAGS:
		{
			static char s[96];
			int ch = 0;
			if (RemoteFlags.ImapAnswered)	ch += sprintf_s(s+ch, sizeof(s)-ch, "Answ ");
			if (RemoteFlags.ImapDeleted)	ch += sprintf_s(s+ch, sizeof(s)-ch, "Del ");
			if (RemoteFlags.ImapDraft)		ch += sprintf_s(s+ch, sizeof(s)-ch, "Drft ");
			if (RemoteFlags.ImapFlagged)	ch += sprintf_s(s+ch, sizeof(s)-ch, "Flg ");
			if (RemoteFlags.ImapRecent)		ch += sprintf_s(s+ch, sizeof(s)-ch, "Rec ");
			if (RemoteFlags.ImapSeen)		ch += sprintf_s(s+ch, sizeof(s)-ch, "Seen ");
			if (RemoteFlags.ImapExpunged)	ch += sprintf_s(s+ch, sizeof(s)-ch, "Exp ");
			return s;
			break;
		}
		case FIELD_CACHE_FILENAME:
		{
			char *l = Path ? strrchr(Path, DIR_CHAR) : 0;
			return l ? l + 1 : Path.Get();
		}
		case FIELD_SERVER_UID:
		{
			// We allow the app to get the integer UID here as a string
			// because POP3 UID's ARE strings, so we need to be compatible
			if (!UidCache)
				UidCache.Printf("%u", Uid);
			return UidCache;
			break;
		}
		case FIELD_LABEL:
		{
			auto m = GetMeta();
			#if IMAP_PROTOBUF
			return m ? m->label().c_str() : NULL;
			#else
			return m ? m->GetAttr(ATTR_LABEL) : NULL;
			#endif
			break;
		}
	}

	return NULL;
}

void CopyImapSegs(ImapMail *Mail, LDataPropI *Dst, LDataPropI *Src)
{
	if (!Dst || !Src)
	{
		LAssert(!"Invalid ptrs");
		return;
	}

	LDataI *DDst = dynamic_cast<LDataI*>(Dst);
	LDataI *DSrc = dynamic_cast<LDataI*>(Src);
	if (!DDst || !DSrc)
	{
		LAssert(!"Cast failed.");
		return;
	}
	
	DDst->CopyProps(*DSrc);
	LDataIt DstLst = Dst->GetList(FIELD_MIME_SEG);
	LDataIt SrcLst = Src->GetList(FIELD_MIME_SEG);
	ImapAttachment *ParentAtt = dynamic_cast<ImapAttachment*>(Dst);
	LMime *ParentMime = ParentAtt->GetSeg();

	for (LDataPropI *c = SrcLst->First(); c; c = SrcLst->Next())
	{
		LDataPropI *n = DstLst->Create(Mail->GetStore());
		if (n)
		{
			CopyImapSegs(Mail, n, c);

			ImapAttachment *ChildAtt = dynamic_cast<ImapAttachment*>(n);
			ChildAtt->AttachTo(ParentAtt);
			
			LMime *ChildMime = ChildAtt->GetSeg();
			if (ParentMime && ChildMime)
			{
				if (!ParentMime->Insert(ChildMime))
				{
					LAssert(!"Insert failed.");
				}
			}
		
		}
		else LAssert(!"Create object failed.");
	}
}

Store3CopyImpl(ImapMail)
{
	Priority = (int)p.GetInt(FIELD_PRIORITY);
	SetInt(FIELD_FLAGS, p.GetInt(FIELD_FLAGS));
	SetStr(FIELD_LABEL, p.GetStr(FIELD_LABEL));
	int64 Col = p.GetInt(FIELD_COLOUR);
	SetInt(FIELD_COLOUR, Col);
	
	MsgId = p.GetStr(FIELD_MESSAGE_ID);
	Subject = p.GetStr(FIELD_SUBJECT);
	
	LDataPropI *i = p.GetObj(FIELD_FROM);
	if (i) From.CopyProps(*i);
	i = p.GetObj(FIELD_REPLY);
	if (i) Reply.CopyProps(*i);
	
	const LDateTime *d = p.GetDate(FIELD_DATE_RECEIVED);
	if (d) DateReceived = *d;
	else DateReceived.Empty();
	d = p.GetDate(FIELD_DATE_SENT);
	if (d)
	{
		DateSent = *d;
		ValidateImapDate(DateSent);
	}
	else DateSent.Empty();
	
	To.DeleteObjects();

	LDataIt pTo = p.GetList(FIELD_TO);
	for (unsigned n=0; n<pTo->Length(); n++)
	{
		Store3Addr *a = new Store3Addr(GetStore(), (*pTo)[n]);
		if (a)
			To.Insert(a, -1, true);
	}
	To.State = Store3Loaded;

	Seg = dynamic_cast<ImapAttachment*>(GetObj(FIELD_MIME_SEG));
	if (!Seg)
		Seg = new ImapAttachment(Store, this);
	auto SrcSeg = p.GetObj(FIELD_MIME_SEG);
	if (SrcSeg)
		CopyImapSegs(this, Seg, SrcSeg);
	return true;
}

#define _Str(v) { char *s = NewStr(str); DeleteArray(v); v = s; return !((s != 0) ^ (str != 0)); }

Store3Status ImapMail::SetStr(int id, const char *str)
{
	Load();
	switch (id)
	{
		case FIELD_SUBJECT:
			Subject = str;
			break;
		case FIELD_MESSAGE_ID:
		{
			// Set the local copy
			MsgId = str;

			// Set the folder meta data copy
			auto m = GetMeta();
			if (m)
			{
				#if IMAP_PROTOBUF
				m->set_messageid(str);
				#else
				m->SetAttr(ATTR_MSGID, str);
				#endif
				Parent->SetDirty();
			}
			break;
		}
		case FIELD_LABEL:
		{
			auto m = GetMeta();
			if (m)
			{
				#if IMAP_PROTOBUF
				m->set_label(str);
				#else
				m->SetAttr(ATTR_LABEL, Label = str);
				#endif
				Parent->SetDirty();
			}
			else
			{
				Label = str;
			}					
			break;
		}
		case FIELD_SERVER_UID:
			// ??
			break;
		case FIELD_INTERNET_HEADER:
		{
			if (HeaderCache.Get() != str)
				HeaderCache.Reset(NewStr(str));
			
			// Reset all the cached values...
			MsgId.Empty();
			Subject.Empty();
			From.Empty();
			Reply.Empty();
			DateReceived.Empty();
			DateSent.Empty();
			To.DeleteObjects();
			DeleteObj(Seg);
			Loaded = Store3Unloaded;
			break;
		}
		case FIELD_ALTERNATE_HTML:
		{
			const char *Mt;
			ImapAttachment *Alt = Seg ? Seg->FindChildByMimeType(sMultipartAlternative) : NULL;
			ImapAttachment *Html = Alt ? Alt->FindChildByMimeType(sTextHtml) : NULL;
			if (Html)
			{
				if (str)
				{
					// Set new stream
					LAutoStreamI a(new LMemStream(str, strlen(str)));
					if (Html->SetStream(a))
						SegDirty = true;
				}
				else
				{
					// Delete content
					Html->Delete();
					delete Html;
					break;
				}				
			}
			else if (Seg &&
					(Mt = Seg->GetStr(FIELD_MIME_TYPE)) &&
					!_stricmp(Mt, sTextHtml)) // Is the root segment HTML?
			{
				// Set the data to an empty stream...
				LAutoStreamI a(str ? new LMemStream(str, strlen(str)) : NULL);
				if (Seg->SetStream(a))
					SegDirty = true;
			}

			HtmlCache.Reset();
			break;
		}
	}

	return Store3Success;
}

int64 ImapMail::GetInt(int id)
{
	switch (id)
	{
		case FIELD_STORE_TYPE:
			return Store3Imap;
		case FIELD_LOADED:
			return Loaded;
		case FIELD_DONT_SHOW_PREVIEW:
			return true;
		case FIELD_SIZE:
		{
			auto Meta = GetMeta();
			if (Meta)
			{
				#if IMAP_PROTOBUF
				return DataSize = Meta->size();
				#else
				char *Sz = Meta->GetAttr(ATTR_SIZE);
				if (Sz)
					return atoi64(Sz);
				#endif
			}
			
			DataSize = LFileSize(Path);
			LAssert(DataSize <= 0x7fffffff);
			return DataSize;
		}
		case FIELD_PRIORITY:
			return Priority;
		case FIELD_FLAGS:
			// LgiTrace("%p.LocalFlags = %s\n", this, EmailFlagsToStr(LocalFlags).Get());
			return LocalFlags;
		case FIELD_COLOUR:
		{
			uint32_t col = Colour.c32();
			
			auto m = GetMeta();
			if (m)
			{
				#if IMAP_PROTOBUF
				col = m->colour();
				#else
				char *s = m->GetAttr(ATTR_COLOUR);
				if (s)
					col = (uint32_t)atoi64(s);
				#endif
			}
			
			return col;
			break;
		}
		case FIELD_ACCOUNT_ID:
			return Store->GetInt(id);
		case FIELD_SERVER_UID:
			return Uid;
	}

	return -1;
}

Store3Status ImapMail::SetInt(int id, int64 i)
{
	Load();
	switch (id)
	{
		case FIELD_LOADED:
		{
			if (Loaded >= i)
				return Store3Success;
			
			if (i == Store3Loaded)
				ReadMime(NULL);
			else
				LAssert(!"What other option is there?");

			if (Loaded >= i) 
				return Store3Success;
			
			if (Loaded == Store3Loading)
				return Store3Delayed;

			return Store3Error; // what happened here?
			break;
		}
		case FIELD_COLOUR:
		{
			Colour.Set((uint32_t)i, 32);

			auto m = GetMeta();
			if (m)
			{
				#if IMAP_PROTOBUF
				m->set_colour(i);
				#else
				m->SetAttr(ATTR_COLOUR, i);
				#endif
				Parent->SetDirty();
			}			
			break;
		}
		case FIELD_PRIORITY:
			Priority = (int)i;
			break;
		case FIELD_FLAGS:
		{
			bool Seen = RemoteFlags.ImapSeen;
			bool Answered = RemoteFlags.ImapAnswered;

			LocalFlags = (int)i;
			RemoteFlags.ImapSeen = TestFlag(i, MAIL_READ);
			RemoteFlags.ImapAnswered = TestFlag(i, MAIL_REPLIED);
			
			if (!Parent)
				return Store3Success;

			auto t = GetMeta();
			if (t)
			{
				#if IMAP_PROTOBUF
				t->set_remoteflags(RemoteFlags.All);
				t->set_localflags(RemoteFlags.All);
				#else
				t->SetAttr(ATTR_FLAGS, RemoteFlags.Get());
				t->SetAttr(ATTR_LOCAL, i);
				#endif
					
				Parent->SetDirty();
			}
			else
			{
				LAssert(!"No tag.");
				return Store3Error;
			}

			if
			(
				(RemoteFlags.ImapSeen ^ Seen)
				||
				(RemoteFlags.ImapAnswered ^ Answered)
			)
			{
				// Update the IMAP server
				LAutoPtr<ImapMsg> Msg(new ImapMsg(IMAP_SET_FLAGS, _FL));
				if (!Msg)
					return Store3Error;

				Msg->Parent = Parent->Remote.Get();
				LAssert(Msg->Parent);
				ImapMailInfo &Info = Msg->Mail.New();
				Info.Uid = Uid;
				Info.Flags = RemoteFlags;
				if (!Store->PostThread(Msg.Release(), false))
					return Store3Error;
				
				return Store3Delayed; // caller doesn't have to set the object dirty
			}

			return Store3Success;
		}
	}

	return Store3Success;
}

const LDateTime *ImapMail::GetDate(int id)
{
	if ((id == FIELD_DATE_RECEIVED && !DateReceived.Year())
	    ||
	    (id == FIELD_DATE_SENT && !DateSent.Year()))
	{
		/*
		auto m = GetMeta(false);
		if (m)
		{
			auto s = m->GetAttr(ATTR_DATE);
			int asd=0;
		}
		*/

		auto Headers = GetStr(FIELD_INTERNET_HEADER);
		LAutoString h(InetGetHeaderField(Headers, "Date"));
		if (h)
		{
			DateReceived.Decode(h);
			DateReceived.ToUtc();
			DateSent = DateReceived;
			ValidateImapDate(DateSent);
		}
	}

	switch (id)
	{
		case FIELD_DATE_RECEIVED:
			return &DateReceived;
		case FIELD_DATE_SENT:
			return &DateSent;
	}

	return 0;
}

Store3Status ImapMail::SetDate(int id, const LDateTime *i)
{
	Load();
	switch (id)
	{
		case FIELD_DATE_RECEIVED:
			DateReceived = *i;
			break;
		case FIELD_DATE_SENT:
			DateSent = *i;
			ValidateImapDate(DateSent);
			break;
		default:
			return Store3Error;
	}
	return Store3Success;
}

Store3Addr *ImapMail::ProcessAddress(Store3Addr &Addr, const char *FieldId, const char *RfcField)
{
	if (!Addr.Addr)
	{
		// Try and load from the meta...
		auto m = GetMeta();
		if (m)
		{
			#if IMAP_PROTOBUF
			auto f = m->from();
			if (!f.email().empty())
			{
				Addr.SetStr(FIELD_NAME, f.name().c_str());
				Addr.SetStr(FIELD_EMAIL, f.email().c_str());
				return &Addr;
			}
			#else
			auto f = m->GetAttr(FieldId);
			if (f)
			{
				LAutoString Name, Email;
				DecodeAddrName(f, Name, Email, 0);
				Addr.SetStr(FIELD_NAME, Name);
				Addr.SetStr(FIELD_EMAIL, Email);
				return &Addr;
			}
			#endif
		}

		// Otherwise try and load from the headers...
		auto Headers = GetStr(FIELD_INTERNET_HEADER);
		auto f = LDecodeRfc2047(LGetHeaderField(Headers, RfcField));
		if (f)
		{
			LAutoString Name, Email;
			DecodeAddrName(f, Name, Email, 0);
			Addr.SetStr(FIELD_NAME, Name);
			Addr.SetStr(FIELD_EMAIL, Email);
			if (Uid)
				Serialize(m, true);
		}
	}
	return &Addr;
}

LDataPropI *ImapMail::GetObj(int id)
{
	switch (id)
	{
		case FIELD_MIME_SEG:
		{
			if (!Seg)
				ReadMime(NULL);
			return Seg;
		}
		case FIELD_FROM:
		{
			return ProcessAddress(From, ATTR_FROM, "From");
		}
		case FIELD_REPLY:
		{
			return ProcessAddress(Reply, ATTR_REPLYTO, "Reply-To");
		}
	}

	return 0;
}

Store3Status ImapMail::SetObj(int id, LDataPropI *i)
{
	LAssert(0);
	return Store3Error;
}

LDataIt ImapMail::GetList(int id)
{
	Load();
	switch (id)
	{
		case FIELD_TO:
		{
			if (!To.Length())
			{
				auto Headers = GetStr(FIELD_INTERNET_HEADER);

				auto h = LDecodeRfc2047(LGetHeaderField(Headers, "To"));
				if (h)
				{
					List<char> Addr;
					TokeniseStrList(h, Addr, ",");
					for (auto a: Addr)
					{
						Store3Addr *la = new Store3Addr(GetStore());
						if (la)
						{
							DecodeAddrName(a, la->Name, la->Addr, 0);
							To.Insert(la, -1, true);
						}
					}
					Addr.DeleteArrays();
				}
				To.State = Store3Loaded;

				if ((h = LDecodeRfc2047(LGetHeaderField(Headers, "Cc"))))
				{
					List<char> Addr;
					TokeniseStrList(h, Addr, ",");
					for (auto a: Addr)
					{
						Store3Addr *la = new Store3Addr(GetStore());
						if (la)
						{
							DecodeAddrName(a, la->Name, la->Addr, 0);
							la->CC = true;
							To.Insert(la);
						}
					}
					Addr.DeleteArrays();
				}
			}
			return &To;
		}
	}

	return 0;
}

Store3Status ImapMail::Save(LDataI *Folder)
{
	Store3Status Status = Store3Error;
	ImapFolder *Fld = dynamic_cast<ImapFolder*>(Folder);

	if (!Fld)
	{
		LAssert(0);
		LgiTrace("%s:%i - No folder.\n", _FL);
	}
	else if (Path)
	{
		LAssert(0);
		LgiTrace("%s:%i - Already has path?\n", _FL);
	}
	else
	{
		if (!Seg && !Stream)
		{
			// Need to create an empty text segment
			LMime *m = new LMime(ScribeTempPath());
			if (!m)
				return Store3Error;

			m->SetMimeType(sTextPlain);
			if (Subject)
				m->Set("Subject", Subject);
			if (DateSent.IsValid())
				m->Set("Date", DateSent.Get());
			if (MsgId)
				m->Set("MessageID", MsgId);
			if (From.Addr)
			{
				LVariant v;
				if (From.GetValue("Text", v))
					m->Set("From", v.Str());
			}
			if (Reply.Addr)
			{
				LVariant v;
				if (Reply.GetValue("Text", v))
					m->Set("Reply", v.Str());
			}

			Seg = new ImapAttachment(Store, this, m);
		}

		char r[32], p[MAX_PATH_LEN];
		do
		{
			sprintf_s(r, sizeof(r), "temp_%u.eml", LRand());
			LMakePath(p, sizeof(p), Fld->Local, r);
		}
		while (LFileExists(p));
		Path = p;

		LFile f;
		if (f.Open(Path, O_WRITE))
		{
			bool SaveFailed = false;
				
			if (Stream)
			{
				// Copy the import stream directly to disk...
				LCopyStreamer Cp;
				Stream->SetPos(0);
				if (Cp.Copy(Stream, &f))
				{
					// After writing to disk, this is not needed anymore.
					Stream.Release();
				}
				else
				{
					LgiTrace("%s:%i - Copy failed.\n", _FL);
					SaveFailed = true;
				}
			}
			else if (Seg)
			{
				// Re-encode the MIME segment tree...
				auto s = Seg->GetSeg();
				if (!s)
				{
					LgiTrace("%s:%i - No segment.\n", _FL);
					SaveFailed = true;
				}
				else if (!s->Text.Encode.Push(&f))
				{
					LgiTrace("%s:%i - Mime encode failed.\n", _FL);
					SaveFailed = true;
				}
			}
				
			f.Close();

			if (!SaveFailed)
			{
				LAutoPtr<ImapMsg> m(new ImapMsg(IMAP_APPEND, _FL));
					
				m->Parent = Fld->Remote.Get();
				m->Mail[0].Local = Path.Get();
				m->Mail[0].Flags = RemoteFlags;
					
				if (Fld->Store->PostThread(m.Release(), false))
				{
					Status = Store3Delayed;
						
					Fld->LoadMail();
					Fld->AddMail(this, 0);
				}
				else
				{
					LgiTrace("%s:%i - PostThread failed.\n", _FL);
				}
			}
		}
		else
		{
			LgiTrace("%s:%i - Failed to open '%s' for writing.\n", _FL, Path.Get());
		}
	}

	return Status;
}

Store3Status ImapMail::Delete(bool ToTrash)
{
	Store3Status Status = Store3Error;

	LAutoPtr<ImapMsg> m(new ImapMsg(IMAP_DELETE, _FL));
	m->Mail[0].Uid = Uid;
	m->Parent = Parent->Remote.Get();
	if (Store->PostThread(m.Release(), false))
	{
		SetState(ImapMailDeleting, _FL);
		Status = Store3Delayed;
		
		LArray<LDataI*> a;
		a.Add(this);
		Store->OnChange(_FL, a, 0);
	}

	return Status;
}

bool ImapMail::OnDelete()
{
	bool Status = false;

	if (Parent)
	{
		Parent->DelMail(this);
		Parent = 0;
	}

	if (Path)
	{
		Status = FileDev->Delete(Path, NULL, false);
	}

	return Status;
}

LAutoStreamI ImapMail::GetStream(const char *file, int line)
{
	LAutoStreamI Ret;
	
	if (Stream)
	{
		Ret.Reset(new LProxyStream(Stream));
	}
	else
	{
		LFile *f;
		if (Ret.Reset(f = new LFile))
		{
			if (!f->Open(Path, O_READWRITE))
				Ret.Reset();
		}
	}

	return Ret;
}

bool ImapMail::SetStream(LAutoStreamI stream)
{
	if (!stream)
		return false;

	DeleteObj(Seg);

	// MIME parse the stream and store it to segments.
	LAutoPtr<LMime> Mime(new LMime);
	if (Mime->Text.Decode.Pull(stream))
	{
		Seg = new ImapAttachment(Store, this, Mime.Release());
		if (Seg)
		{
			// This stops the objects being written to disk.
			// Which would mean we have multiple copies of the same
			// data on disk. This setting also propagates down the
			// tree automatically as GMimeToStore3 saves new child
			// notes to their parents.
			Seg->SetInMemoryOnly(true);
			Seg->AttachTo(this);
		}
	}

	return false;
}

void ImapMail::OnDownload(LAutoString &Headers)
{
	LFile f;
	if (f.Open(Path, O_WRITE))
	{
		f.Write(Headers, strlen(Headers));
		f.Close();
	}

	HeaderCache = Headers;
	Loaded = Store3Headers;

	SetState(ImapMailIdle, _FL);
	auto t = GetMeta();
	if (t)
	{
		// t->SetAttr(ATTR_HEADER_ONLY, 1);

		// This pulls all the relevant fields out of the headers so they can be written
		// to the meta element. This means it gets loaded without having to touch the
		// email on disk. Which means faster filtering on the commonly displayed list
		// fields.
		GetStr(FIELD_SUBJECT);
		GetObj(FIELD_FROM);
		GetDate(FIELD_DATE_SENT);
		Serialize(t, true);
	}
}
