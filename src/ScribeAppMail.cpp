/*
**	FILE:			ScribeApp.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			22/10/1998
**	DESCRIPTION:	Scribe email application
**
**	Copyright (C) 1998, Matthew Allen
**		fret@memecode.com
*/

// Debug defines
// #define PRINT_OUT_STORAGE_TREE
// #define TEST_OBJECT_SIZE

#define USE_SPELLCHECKER			1
#define USE_INTERNAL_BROWSER		1		// for help
#define RUN_STARTUP_SCRIPTS			1
#define PROFILE_ON_PULSE			0
#define TRAY_CONTACT_BASE			1000
#define TRAY_MAIL_BASE				10000

// Includes
#include <cstddef>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#include "Scribe.h"

#include "lgi/common/StoreConvert1To2.h"
#include "lgi/common/ProgressDlg.h"
#include "lgi/common/TextLabel.h"
#include "lgi/common/Button.h"
#include "lgi/common/CheckBox.h"
#include "lgi/common/OpenSSLSocket.h"
#include "lgi/common/SoftwareUpdate.h"
#include "lgi/common/Html.h"
#include "lgi/common/TextView3.h"
#include "lgi/common/RichTextEdit.h"
#include "lgi/common/Store3.h"
#include "lgi/common/Edit.h"
#include "lgi/common/Box.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/SpellCheck.h"
#include "lgi/common/SubProcess.h"
#include "lgi/common/CssTools.h"
#include "lgi/common/Charset.h"
#include "lgi/common/RefCount.h"
#include "lgi/common/PopupNotification.h"
#include "lgi/common/Base64.h"
#include "lgi/common/Html2.h"
#include "lgi/common/LiteHtmlView.h"

#include "ScribePrivate.h"
#include "PreviewPanel.h"
#include "ScribeStatusPanel.h"
#include "ScribeFolderDlg.h"
#include "ScribePageSetup.h"
#include "Calendar.h"
#include "CalendarView.h"
#include "ScribeSpellCheck.h"
#include "Store3Common.h"
#include "PrintContext.h"
#include "resource.h"
#include "ManageMailStores.h"
#include "ReplicateDlg.h"
#include "ScribeAccountPreview.h"
#include "Encryption/GnuPG.h"
#include "resdefs.h"
#include "ScribeIpc.h"
#include "DynamicHtml.h"

#define DEBUG_STORE_EVENTS			0
#if DEBUG_STORE_EVENTS
#define LOG_STORE(...)				LgiTrace(__VA_ARGS__)
#else
#define LOG_STORE(...)
#endif

#define IDM_LOAD_MSG				2000
#define RAISED_LOOK					0
#define SUNKEN_LOOK					false
#ifdef MAC
#define SUNKEN_CTRL					false
#else
#define SUNKEN_CTRL					true
#endif

enum TrayIconIndex
{
	TRAY_ICON_NORMAL = 0,
	TRAY_ICON_ERROR,
	TRAY_ICON_MAIL,
	TRAY_ICON_NONE
};

#include "ScribeWndPrivate.h"

void ScribeWnd::OnNewMailSound()
{
	THREAD_UNSAFE();
	static uint64 PrevTs = 0;
	auto Now = LCurrentTime();
	if (Now - PrevTs > 30000)
	{
		PrevTs = Now;
		LVariant v;
		if (GetOptions()->GetValue(OPT_NewMailSoundFile, v) &&
			LFileExists(v.Str()))
		{
			LPlaySound(v.Str(), SND_ASYNC);
		}
	}
}

void ScribeWnd::OnFolderSelect(ScribeFolder *f)
{
	THREAD_UNSAFE();
	if (SearchView)
		SearchView->OnFolder();
}

void ScribeWnd::OnNewMail(List<Mail> &MailObjs, bool Add)
{
	THREAD_UNSAFE();

	LVariant v;
	bool ShowDetail = MailObjs.Length() < 5;
	List<Mail> NeedsFiltering;
	LArray<Mail*> NeedsBayes;
	LArray<Mail*> NeedsGrowl;
	LArray<ScribeFolder*> Resort;

	for (auto m: MailObjs)
	{
		if (Add)
		{
			#if DEBUG_NEW_MAIL
			LgiTrace("%s:%i - NewMail.OnNewMail t=%p, uid=%s, mode=%s\n",
				_FL, (Thing*)m, m->GetServerUid().ToString().Get(), toString(m->NewEmail));
			#endif

			switch (m->NewEmail)
			{
				case Mail::NewEmailNone:
				{
					auto Loaded = m->GetLoaded();
					#if DEBUG_NEW_MAIL
					LgiTrace("%s:%i - NewMail.OnNewMail.GetLoaded=%i uid=%s\n", _FL, (int)Loaded, m->GetServerUid().ToString().Get());
					#endif
					if (Loaded != Store3Loaded)
					{
						LOG_STORE("\tOnNewMail calling SetLoaded.\n");
						m->SetLoaded();
						m->NewEmail = Mail::NewEmailLoading;
					}
					else
					{
						m->NewEmail = Mail::NewEmailFilter;
						LOG_STORE("\tOnNewMail none->NeedsFiltering.\n");
						NeedsFiltering.Insert(m);
					}
					break;
				}
				case Mail::NewEmailLoading:
				{
					auto Loaded = m->GetLoaded();
					if (Loaded == Store3Loaded)
					{
						m->NewEmail = Mail::NewEmailFilter;
						NeedsFiltering.Insert(m);

						if (m->GetFolder() &&
							!Resort.HasItem(m->GetFolder()))
						{
							Resort.Add(m->GetFolder());
						}
					}
					break;
				}
				case Mail::NewEmailFilter:
				{
					NeedsFiltering.Insert(m);
					break;
				}
				case Mail::NewEmailBayes:
				{
					NeedsBayes.Add(m);
					break;
				}
				case Mail::NewEmailGrowl:
				{
					if (d->Growl)
					{
						NeedsGrowl.Add(m);
						break;
					}
					else
					{
						m->NewEmail = Mail::NewEmailTray;
						// no Growl loaded so fall through to new tray mail
					}
				}
				case Mail::NewEmailTray:
				{
					LAssert(m->GetObject());
					LAssert(!Mail::NewMailLst.HasItem(m));

					m->NewEmail = Mail::NewEmailNone;
					Mail::NewMailLst.Insert(m);
					OnNewMailSound();
					break;
				}
				default:
				{
					LAssert(!"Hmmm what happen?");
					break;
				}
			}
		}
		else
		{
			#if DEBUG_NEW_MAIL
			LgiTrace("%s:%i - NewMail.OnNewMail.RemoveNewMail t=%p, uid=%s\n",
				_FL, (Thing*)m, m->GetServerUid().ToString().Get());
			#endif

			Mail::NewMailLst.Delete(m);
			
			if (m->NewEmail == Mail::NewEmailFilter)
				m->NewEmail = Mail::NewEmailNone;
		}
	}

	if (Add)
	{
		// Do filtering
		if (NeedsFiltering.Length())
		{
			List<Filter> Filters;
			if (!GetOptions()->GetValue(OPT_DisableUserFilters, v) ||
				!v.CastInt32())
			{
				GetFilters(Filters, true, false, false);
			}
		
			if (Filters.Length() > 0)
			{
				// Run the filters
				#if DEBUG_NEW_MAIL
				LgiTrace("%s:%i - NewMail.OnNewMail.Filtering %i mail through %i filters\n",
					_FL, (int)NeedsFiltering.Length(), (int)Filters.Length());
				#endif
				Filter::ApplyFilters(NULL, Filters, NeedsFiltering);
				
				// All the email not filtered now needs to be sent to the bayes filter.
				for (auto m: NeedsFiltering)
				{
					if (m->NewEmail == Mail::NewEmailBayes)
					{
						#if DEBUG_NEW_MAIL
						LgiTrace("%s:%i - NewMail.OnNewMail.NeedsBayes t=%p, msgid=%s\n",
							_FL, (Thing*)m, m->GetMessageId());
						#endif

						NeedsBayes.Add(m);
					}
					else if (m->NewEmail == Mail::NewEmailGrowl)
					{
						#if DEBUG_NEW_MAIL
						LgiTrace("%s:%i - NewMail.OnNewMail.NeedsGrowl t=%p, msgid=%s\n",
							_FL, (Thing*)m, m->GetMessageId());
						#endif

						NeedsGrowl.Add(m);
					}
				}
			}
		}

		// Do bayes
		if (NeedsBayes.Length())
		{
			ScribeBayesianFilterMode FilterMode = BayesOff;
			if (GetOptions()->GetValue(OPT_BayesFilterMode, v))
				FilterMode = (ScribeBayesianFilterMode)v.CastInt32();

			for (unsigned i=0; i<NeedsBayes.Length(); i++)
			{
				Mail *m = NeedsBayes[i];
				if (FilterMode != BayesOff)
				{
					double Rating = 0.0;
					
					// This adds the mail to a message ID map, so even if it moves we
					// can do the filtering later on when the Bayesian result is returned.
					m->MailMessageIdMap();
					
					// Start the Bayesian rating process off
					Store3Status Status = IsSpam(Rating, m);
					if (Status == Store3Success)
					{
						// Bayes done... this stops OnBayesResult from passing it back to OnNewMail
						m->NewEmail = Mail::NewEmailGrowl;
						
						// Process bayes result
						if (!OnBayesResult(m, Rating))
						{
							// Not spam... so on to growl
							#if DEBUG_NEW_MAIL
							LgiTrace("%s:%i - NewMail.Bayes.NeedsGrowl t=%p, msgid=%s\n",
								_FL, (Thing*)m, m->GetMessageId());
							#endif

							NeedsGrowl.Add(m);
						}
						else
						{
							// Is spam... do nothing...
							#if DEBUG_NEW_MAIL
							LgiTrace("%s:%i - NEW_MAIL: Bayes->IsSpam t=%p, msgid=%s\n",
								_FL, (Thing*)m, m->GetMessageId());
							#endif

							m->NewEmail = Mail::NewEmailNone;
							m = 0;
						}
					}
					else
					{
						// Didn't get classified immediately, so it'll be further
						// processed when OnBayesResult gets called later.
					}
				}
				else
				{
					// Bayes filter not active... move it to growl
					m->NewEmail = Mail::NewEmailGrowl;
					NeedsGrowl.Add(m);
				}
			}
		}

		if (NeedsGrowl.Length())
		{
			if (d->Growl)
			{
				if (!ShowDetail)
				{
					LAutoPtr<LGrowl::LNotify> n(new LGrowl::LNotify);
					n->Name = "new-mail";
					n->Title = "New Mail";
					n->Text.Printf("%i new messages", (int)MailObjs.Length());
					d->Growl->Notify(n);
				}
				else
				{
					for (auto m: NeedsGrowl)
					{
						#ifdef _DEBUG
						auto state =
						#endif
						m->GetLoaded();
						LAssert(state == Store3Loaded);

						// If loaded then notify			                
						GrowlOnMail(m);
					}
				}
			}

			for (auto m: NeedsGrowl)
			{
				m->NewEmail = Mail::NewEmailNone;

				LAssert(m->GetObject());
				LAssert(!Mail::NewMailLst.HasItem(m));
				Mail::NewMailLst.Insert(m);
				OnNewMailSound();
			}
		}

		if (GetOptions()->GetValue(OPT_NewMailNotify, v) &&
			v.CastInt32())
		{
			PostEvent(M_SCRIBE_NEW_MAIL);
		}

		for (auto f: Resort)
		{
			#if DEBUG_NEW_MAIL
			auto Path = Resort[i]->GetPath();
			LgiTrace("%s:%i - NewMail.OnNewMail.Resort=%s\n", _FL, Path.Get());
			#endif

			f->ReSort();
		}
	}
}

LColour ScribeWnd::GetColour(int i)
{
	THREAD_UNSAFE(LColour());
	
	static LColour MailPreview;
	static LColour UnreadCount;
	#define ReadColDef(Var, Tag, Default)		\
		case Tag: \
		{ \
			if (!Var.IsValid()) \
			{ \
				Var = Default; \
				LColour::GetConfigColour("Colour."#Tag, Var); \
			} \
			return Var; \
			break; \
		}

	switch (i)
	{
		ReadColDef(MailPreview, L_MAIL_PREVIEW, LColour(0, 0, 255));
		ReadColDef(UnreadCount, L_UNREAD_COUNT, LColour(0, 0, 255));
		default:
		{
			return LColour((LSystemColour)i);
			break;
		}
	}

	return LColour();
}

bool WriteXmlTag(LStream &p, LXmlTag *t)
{
	const char *Tag = t->GetTag();
	bool ValidTag = ValidStr(Tag) && !IsDigit(Tag[0]);
	if (ValidTag)
		p.Print("<%s", Tag);
	else
	{
		LAssert(0);
		return false;
	}

	LXmlTree Tree;
	static const char *EncodeEntitiesAttr	= "\'<>\"\n";
	for (unsigned i=0; i<t->Attr.Length(); i++)
	{
		auto &a = t->Attr[i];

		// Write the attribute name
		p.Print(" %s=\"", a.GetName());
		
		// Encode the value
		if (!Tree.EncodeEntities(&p, a.GetValue(), -1, EncodeEntitiesAttr))
		{
			LAssert(0);
			return false;
		}

		// Write the delimiter
		p.Write((void*)"\"", 1);

		if (i<t->Attr.Length()-1 /*&& TestFlag(d->Flags, GXT_PRETTY_WHITESPACE)*/)
		{
			p.Write((void*)"\n", 1);
		}
	}
	
	p.Write(">", 1);
	
	return true;
}

LString ScribeWnd::ProcessReplyForwardTemplate(Mail *m, Mail *r, char *Xml, int &Cursor, const char *MimeType)
{
	THREAD_UNSAFE(LString());

	LStringPipe p(256);

	if (m && r && Xml)
	{
		bool IsHtml = MimeType && !_stricmp(MimeType, sTextHtml);
		LMemStream mem(Xml, strlen(Xml));
		LXmlTag x;
		LXmlTree t(GXT_KEEP_WHITESPACE | GXT_NO_DOM);
		if (t.Read(&x, &mem, 0))
		{
			ScribeDom Dom(this);
			Dom.Email = m;

			if (IsHtml)
			{
				const char *EncodeEntitiesContent	= "\'<>\"";
				
				for (auto Tag: x.Children)
				{
					if (!WriteXmlTag(p, Tag))
					{
						break;
					}
					
					for (const char *c = Tag->GetContent(); c; )
					{
						const char *s = strstr(c, "<?");
						const char *e = s ? strstr(s + 2, "?>") : NULL;
						if (s && e)
						{
							if (s > c)
							{
								t.EncodeEntities(&p, (char*)c, s - c, EncodeEntitiesContent);
							}
							s += 2;
							
							LString Var = LString(s, e - s).Strip();
							LVariant v;
							if (Var)
							{
								LString::Array parts = Var.SplitDelimit(" ");
								if (parts.Length() > 0)
								{
									if (Dom.GetValue(parts[0], v))
									{
										for (unsigned mod = 1; mod < parts.Length(); mod++)
										{
											LString::Array m = parts[mod].SplitDelimit("=", 1);
											if (m.Length() == 2)
											{
												if (m[0].Equals("quote"))
												{
													LVariant Quote;
													if (Dom.GetValue(m[1], Quote))
													{
														LVariant WrapColumn;
														if (!GetOptions()->GetValue(OPT_WrapAtColumn, WrapColumn) ||
															WrapColumn.CastInt32() <= 0)
															WrapColumn = 76;
															
														WrapAndQuote(p, Quote.Str(), WrapColumn.CastInt32(), v.Str(), NULL, MimeType);
														v.Empty();
													}
												}
											}
										}
										
										switch (v.Type)
										{
											case GV_STRING:
											case GV_WSTRING:
											case GV_LSTRING:
											{
												p.Push(v.Str());
												break;
											}
											case GV_DATETIME:
											{
												p.Push(v.Value.Date->Get());
												break;
											}
											case GV_NULL:
												break;
											default:
											{
												LAssert(!"Unsupported type.");
												break;
											}
										}
									}
								}
							}
							
							c = e + 2;
						}
						else
						{						
							p.Print("%s", c);
							break;
						}
					}
				}
			}
			else
			{			
				LArray<LXmlTag*> Tags;
				Tags.Add(&x);
				for (auto Tag: x.Children)
				{
					Tags.Add(Tag);
				}
				
				for (unsigned i=0; i<Tags.Length(); i++)
				{
					LXmlTag *Tag = Tags[i];
					
					LVariant v;
					if (Tag->GetTag() && Dom.GetValue(Tag->GetTag(), v))
					{
						char *s = v.Str();
						if (s)
						{
							const char *Quote;
							if ((Quote = Tag->GetAttr("quote")))
							{
								LVariant q, IsQuote;
								GetOptions()->GetValue(OPT_QuoteReply, IsQuote);
								if (r->GetValue(Quote, q))
								{
									Quote = q.Str();
								}
								else
								{
									Quote = "> ";
								}

								if (Quote && IsQuote.CastInt32())
								{
									LVariant WrapColumn;
									if (!GetOptions()->GetValue(OPT_WrapAtColumn, WrapColumn) ||
										WrapColumn.CastInt32() <= 0)
										WrapColumn = 76;
										
									WrapAndQuote(p, Quote, WrapColumn.CastInt32(), s);
								}
								else
								{
									p.Push(s);
								}
							}
							else
							{
								p.Push(s);
							}
						}
						else if (v.Type == GV_DATETIME &&
								 v.Value.Date)
						{
							char b[64];
							v.Value.Date->Get(b, sizeof(b));
							p.Push(b);
						}
					}
					else if (Tag->IsTag("cursor"))
					{
						auto Buf = p.Peek(p.GetSize());
						if (Buf)
						{
							RemoveReturns(Buf);
							Cursor = LCharLen(Buf, "utf-8");
						}
					}

					if (Tag->GetContent())
					{
						p.Push(Tag->GetContent());
					}
				}
			}
		}
	}

	return p.NewLStr();
}

LString	ScribeWnd::ProcessSig(Mail *m, char *Xml, const char *MimeType)
{
	THREAD_UNSAFE(LString());

	LStringPipe p;

	if (!m || !Xml)
		return LString();
	
	if (MimeType && !_stricmp(MimeType, sTextHtml))
		p.Write(Xml, strlen(Xml));
	else
	{
		LMemStream mem(Xml, strlen(Xml));
		LXmlTag x;
		LXmlTree t(GXT_KEEP_WHITESPACE|GXT_NO_DOM);
		if (t.Read(&x, &mem, 0))
		{
			for (auto Tag: x.Children)
			{
				if (Tag->IsTag("random-line"))
				{
					char *FileName = 0;
					if ((FileName = Tag->GetAttr("Filename")))
					{
						LFile f(FileName);
						if (f)
						{
							auto Lines = f.Read().SplitDelimit("\r\n");
							char *RandomLine = Lines[LRand((unsigned)Lines.Length())];
							if (RandomLine)
							{
								p.Push(RandomLine);
							}
						}
					}
				}
				else if (Tag->IsTag("random-paragraph"))
				{
					char *FileName = 0;
					if ((FileName = Tag->GetAttr("Filename")))
					{
						auto File = LReadFile(FileName);
						if (File)
						{
							List<char> Para;
							for (auto f = File.Get(); f && *f; )
							{
								// skip whitespace
								while (strchr(" \t\r\n", *f)) f++;
								if (*f)
								{
									char *Start = f;
									char *n;
									while ((n = strchr(f, '\n')))
									{
										f = n + 1;
										if (f[1] == '\n' ||
											(f[1] == '\r' && f[2] == '\n'))
										{
											break;
										}
									}
									if (f == Start) f += strlen(f);

									Para.Insert(NewStr(Start, f-Start));
								}
							}

							auto RandomPara = Para.ItemAt(LRand((int)Para.Length()));
							if (RandomPara)
							{
								p.Push(RandomPara);
							}

							Para.DeleteArrays();
						}
					}
				}
				else if (Tag->IsTag("include-file"))
				{
					char *FileName = 0;
					if ((FileName = Tag->GetAttr("filename")))
					{
						auto File = LReadFile(FileName);
						if (File)
							p.Push(File);
					}
				}
				else if (Tag->IsTag("quote-file"))
				{
					char *FileName = 0;
					char *QuoteStr = 0;
					if ((FileName = Tag->GetAttr("filename")) &&
						(QuoteStr = Tag->GetAttr("Quote")))
					{
					}
				}
				else
				{
					p.Push(Tag->GetContent());
				}
			}
		}
	}

	return p.NewLStr();
}

// Get the effective permissions for a resource.
//
// This method can be used by both sync and async code:
// In sync mode, don't supply a callback (ie = NULL) and the return value will be:
//		Store3Error - no access
//		Store3Delayed - no access, asking the user for password
//		Store3Success - allow immediate access
//
// In async mode, supply a callback and wait for the response.
//		callback(false) - no access
//		callback(true) - allow immediate access
// in this mode the same return values as sync mode are used.
Store3Status ScribeWnd::GetAccessLevel(LViewI *Parent, ScribePerm Required, const char *ResourceName, std::function<void(bool)> Callback)
{
	THREAD_UNSAFE(Store3Error);

	if (CurrentAuthLevel >= Required)
	{
		if (Callback) Callback(true);
		return Store3Success;
	}
	
	if (!Parent)
		Parent = this;
	
	switch (Required)
	{
		default:
			break;
		case PermRequireUser:
		{
			LPassword p;
			if (!p.Serialize(GetOptions(), OPT_UserPermPassword, false))
			{
				if (Callback) Callback(true);
				return Store3Success;
			}

			char Msg[256];
			sprintf_s(Msg, sizeof(Msg), LLoadString(IDS_ASK_USER_PASS), ResourceName);
				
			auto d = new LInput(Parent, "", Msg, AppName, true);
			d->DoModal([this, d, p, Callback](auto dlg, auto id)
			{
				if (id && d->GetStr())
				{
					char Pass[256];
					p.Get(Pass);
					bool Status = strcmp(Pass, d->GetStr()) == 0;
					if (Status)
					{
						CurrentAuthLevel = PermRequireUser;
						
						auto i = Menu->FindItem(IDM_LOGOUT);
						if (i) i->Enabled(true);
						if (Callback) Callback(true);
					}
					else
					{
						if (Callback) Callback(false);
					}
				}
			});

			return Store3Delayed;
		}
		case PermRequireAdmin:
		{
			LString Key;
			Key.Printf("Scribe.%s", OPT_AdminPassword);
			auto Hash = LAppInst->GetConfig(Key);
			if (ValidStr(Hash))
			{
				if (Callback) Callback(false);
				return Store3Error;
			}

			uchar Bin[256];
			ssize_t BinLen = 0;
			if ((BinLen = ConvertBase64ToBinary(Bin, sizeof(Bin), Hash, strlen(Hash))) != 16)
			{
				LgiMsg(Parent, "Admin password not correctly encoded.", AppName);
				if (Callback) Callback(false);
				return Store3Error;
			}

			auto d = new LInput(Parent, "", LLoadString(IDS_ASK_ADMIN_PASS), AppName, true);
			d->DoModal([this, d, Bin, Callback](auto dlg, auto id)
			{
				if (id && d->GetStr())
				{
					unsigned char Digest[16];
					char Str[256];
					sprintf_s(Str, sizeof(Str), "%s admin", d->GetStr().Get());
					MDStringToDigest(Digest, Str);
						
					if (memcmp(Bin, Digest, 16) == 0)
					{
						CurrentAuthLevel = PermRequireAdmin;
						auto i = Menu->FindItem(IDM_LOGOUT);
						if (i) i->Enabled(true);
						if (Callback) Callback(true);
					}
					else
					{
						if (Callback) Callback(false);
					}
				}
			});

			return Store3Delayed;
		}
	}

	if (Callback) Callback(true);
	return Store3Success;
}

void ScribeWnd::GetAccountSettingsAccess(LViewI *Parent, ScribeAccessType AccessType, std::function<void(bool)> Callback)
{
	THREAD_UNSAFE();

	LVariant Level = (int)PermRequireNone;
	
	// Check if user level access is required
	char *Opt = (char*)(AccessType == ScribeReadAccess ? OPT_AccPermRead : OPT_AccPermWrite);
	GetOptions()->GetValue(Opt, Level);

	GetAccessLevel(Parent ? Parent : this, (ScribePerm)Level.CastInt32(), "Account Settings", Callback);
}

void ScribeWnd::OnBeforeConnect(ScribeAccount *Account, bool Receive)
{
	THREAD_UNSAFE();

	if (Receive)
		Account->Receive.Enabled(false);
	else
		Account->Send.Enabled(true);

	if (StatusPanel)
		StatusPanel->Invalidate();
}

void ScribeWnd::OnAfterConnect(ScribeAccount *Account, bool Receive)
{
	THREAD_UNSAFE();

	if (Account)
	{
		SaveOptions();
		if (ScribeState == ScribeExiting)
		{
			LCloseApp();
		}
	}

	if (Receive)
		Account->Receive.Enabled(true);
	else
		Account->Send.Enabled(true);

	if (StatusPanel)
		StatusPanel->Invalidate();

	if (d->SendAfterReceive)
	{
		bool Online = false;
		for (auto a: Accounts)
		{
			bool p = a->Receive.IsPersistant();
			bool o = a->Receive.IsOnline();
			if (!p && o)
			{
				Online = true;
				break;
			}
		}
		if (!Online)
		{
			Send(-1, true);
			d->SendAfterReceive = false;
		}
	}
}

void ScribeWnd::Send(ssize_t Which, bool Quiet)
{
	THREAD_UNSAFE();

	if (ScribeState == ScribeExiting)
		return;

	if (Which < 0)
	{
		LVariant v;
		if (GetOptions()->GetValue(OPT_DefaultSendAccount, v))
			Which = v.CastInt32();
	}

	LArray<ScribeFolder*> Outboxes;
	unsigned i;
	for (i=0; i<Folders.Length(); i++)
	{
		ScribeFolder *OutBox = GetFolder(FOLDER_OUTBOX, &Folders[i]);
		if (OutBox)
		{
			OutBox->LoadThings();
			Outboxes.Add(OutBox);
		}
	}

	if (Outboxes.Length() < 1)
	{
		LgiMsg(this, LLoadString(IDS_NO_OUTGOING_FOLDER), AppName, MB_OK);
		return;
	}

	int MailToSend = 0;
	List<SendAccountlet> Acc;
	SendAccountlet *Default = 0;
	
	{
		// Create list of accounts
		for (auto a: Accounts)
		{
			Acc.Insert(&a->Send);
			a->Send.Outbox.DeleteObjects();
		
			if (Which < 0 || a->GetIndex() == Which)
				Default = &a->Send;
		}
		
		// If the default it not in the list try the first one..
		if (!Default)
			Default = Acc[0];
	}

	for (i=0; i<Outboxes.Length(); i++)
	{
		auto OutBox = Outboxes[i];
		for (auto t: OutBox->Items)
		{
			auto m = t->IsMail();
			if (!m)
				continue;

			auto Flags = m->GetFlags();
			if (!TestFlag(Flags, MAIL_SENT) &&
				TestFlag(Flags, MAIL_READY_TO_SEND))
			{
				auto To = m->GetObject()->GetList(FIELD_TO);
				if (To && To->Length())
				{
					LAutoPtr<ScribeEnvelope> Out(new ScribeEnvelope);
					if (Out)
					{
						if (m->OnBeforeSend(Out))
						{
							#if 0
								LFile tmp(LFile::Path(ScribeTempPath()) / "mime.txt", O_WRITE);
								tmp.Write(Out->Rfc822);
								tmp.Close();
							#endif

							SendAccountlet *Send = nullptr;
							for (auto a: Acc)
							{
								LVariant Ie = a->GetAccount()->Identity.Email();
								if (ValidStr(Ie.Str()) &&
									a->OnlySendThroughThisAccount())
								{
									if (Ie.Str() &&
										m->GetFromStr(FIELD_EMAIL) &&
										_stricmp(Ie.Str(), m->GetFromStr(FIELD_EMAIL)) == 0)
									{
										Send = a;
										break;
									}
								}
							}

							if (!Send)
							{
								Send = Default;
							}
							
							if (Send)
							{
								LAssert(Out->To.Length() > 0);
								Out->SourceFolder = OutBox->GetPath();
								Send->Outbox.Add(Out.Release());
								MailToSend++;
							}
						}
					}
				}
				else
				{
					LgiMsg(	this,
							LLoadString(IDS_ERROR_NO_RECIPIENTS),
							AppName,
							MB_OK,
							m->GetSubject() ? m->GetSubject() : (char*)"(none)");
				}
			}
		}
	}

	if (MailToSend)
	{
		for (auto a: Acc)
		{
			if (a->Outbox.Length() > 0 && !a->IsOnline())
			{
				if (a->IsConfigured())
				{
					a->Connect(0, Quiet);
				}
				else
				{
					auto d = new LAlert(this,
							AppName,
							LLoadString(IDS_ERROR_NO_CONFIG_SEND),
							LLoadString(IDS_CONFIGURE),
							LLoadString(IDS_CANCEL));
					d->DoModal([this, d, a](auto dlg, auto id)
					{
						if (id == 1)
							a->GetAccount()->InitUI(this, 1, NULL);
					});
				}
			}
		}
	}
	else
	{
		LgiMsg(this, LLoadString(IDS_NO_MAIL_TO_SEND), AppName, MB_OK, Outboxes[0]->GetText());
	}
}

void ScribeWnd::Receive(ssize_t Which)
{
	THREAD_UNSAFE();

	#define LOG_RECEIVE		0
	
	if (ScribeState == ScribeExiting)
	{
		LgiTrace("%s:%i - Won't receive, is trying to exit.\n", _FL);
		return;
	}

	for (ScribeAccount *i: Accounts)
	{
		if (i->GetIndex() != Which)
			continue;
			
		if (i->Receive.IsOnline())
		{
			#if LOG_RECEIVE
			LgiTrace("%s:%i - %i already online.\n", _FL, Which);
			#endif
		}
		else if (i->Receive.Disabled() > 0)
		{
			#if LOG_RECEIVE
			LgiTrace("%s:%i - %i is disabled.\n", _FL, Which);
			#endif
		}
		else if (!i->Receive.IsConfigured())
		{
			#if LOG_RECEIVE
			LgiTrace("%s:%i - %i is not configured.\n", _FL, Which);
			#endif

			auto a = new LAlert(this,
					AppName,
					LLoadString(IDS_ERROR_NO_CONFIG_RECEIVE),
					LLoadString(IDS_CONFIGURE),
					LLoadString(IDS_CANCEL));
			a->DoModal([this, a, i](auto dlg, auto id)
			{
				if (id == 1)
					i->InitUI(this, 2, NULL);
			});
		}
		else
		{
			i->Receive.Connect(0, false);
		}
		break;
	}
}

bool ScribeWnd::GetHelpFilesPath(char *Path, int PathSize)
{
	THREAD_UNSAFE(false);

	const char *Index = "index.html";
	char Install[MAX_PATH_LEN];
	strcpy_s(Install, sizeof(Install), ScribeResourcePath());
	
	for (int i=0; i<5; i++)
	{
		char p[MAX_PATH_LEN];
		LMakePath(p, sizeof(p), Install, "help");
		LMakePath(p, sizeof(p), p, Index);
		LgiTrace("Trying '%s'\n", p);
		if (LFileExists(p))
		{
			LTrimDir(p);
			strcpy_s(Path, PathSize, p);
			return true;
		}
		#ifdef MAC
		LMakePath(p, sizeof(p), Install, "Resources/Help");
		LMakePath(p, sizeof(p), p, Index);
		// LgiTrace("Trying '%s'\n", p);
		if (LFileExists(p))
		{
			LTrimDir(p);
			strcpy_s(Path, PathSize, p);
			return true;
		}
		#endif
		LTrimDir(Install); // Try all the parent folders...
	}

	LArray<const char*> Ext;
	LArray<char*> Help;
	Ext.Add("index.html");
	LMakePath(Install, sizeof(Install), ScribeResourcePath(), "..");
	LRecursiveFileSearch(Install, &Ext, &Help);
	for (unsigned i=0; i<Help.Length(); i++)
	{
		if (stristr(Help[i], "leveldb"))
			continue;
		strcpy_s(Path, PathSize, Help[i]);
		LTrimDir(Path);
		Help.DeleteArrays();
		return true;
	}
	
	return false;
}

bool ScribeWnd::LaunchHelp(const char *File)
{
	THREAD_UNSAFE(false);

	if (File)
	{
		char *Hash = 0;
		LBrowser *Browse = 0;
		
		// Find help files...
		char Path[MAX_PATH_LEN];
		if (!GetHelpFilesPath(Path, sizeof(Path)))
		{
			LgiTrace("%s:%i - GetHelpFilesPath failed.\n", _FL);
			goto HelpError;
		}

		// Add filename...
		LMakePath(Path, sizeof(Path), Path, File);
		
		// Check existance...
		Hash = strrchr(Path, '#');
		if (Hash) *Hash = 0;
		if (!LFileExists(Path))
		{
			LgiTrace("%s:%i - FileExists('%s') failed.\n", _FL, Path);
			goto HelpError;
		}
		if (Hash) *Hash = '#';

		#if USE_INTERNAL_BROWSER

		Browse = new LBrowser(this, "Help");
		if (Browse)
		{
			auto p = ScribeResourcePath();
			Browse->AddPath(ScribeResourcePath());
			Browse->SetEvents(d);
			return Browse->SetUri(Path);
		}

		#else

			#ifdef MAC

			if (LExecute(Path))
				return true;
			
			#else

			if (Hash) *Hash = '#';
			
			// Get browser...
			char Browser[256];
			if (!LGetAppForMimeType("application/browser", Browser, sizeof(Browser)))
			{
				LgiTrace("%s:%i - LGetAppForMimeType('text/html') failed.\n", _FL);
				goto HelpError;
			}
			
			// Execute browser to view help...
			char Uri[256];
			sprintf_s(Uri, sizeof(Uri), "\"file://%s\"", Path);
			#ifdef WIN32
			char *c;
			while (c = strchr(Uri, '\\')) *c = '/';
			#endif

			LgiTrace("LaunchHelp('%s','%s').\n", Browser, Uri);
			if (!LExecute(Browser, Uri))
			{
				LgiTrace("%s:%i - LExecute('%s','%s') failed.\n", _FL, Browser, Uri);
				goto HelpError;
			}
			return true;
			
			#endif

		#endif
		
		HelpError:
		LgiMsg(this, LLoadString(IDS_ERROR_NO_HELP), AppName, MB_OK);
	}
	
	return false;
}

void ScribeWnd::Preview(ssize_t Which)
{
	THREAD_UNSAFE();

	LArray<ScribeAccount*> a;
	a.Add(Accounts[Which]);
	OpenPopView(this, a);
}

bool MergeSegments(LDataPropI *DstProp, LDataPropI *SrcProp, LDom *Dom)
{
	LDataI *Src = dynamic_cast<LDataI *>(SrcProp);
	LDataI *Dst = dynamic_cast<LDataI *>(DstProp);
	if (!Dst || !Src)
		return false;

	Store3MimeType Mt(Src->GetStr(FIELD_MIME_TYPE));
	if (Mt.IsText())
	{
		// Set the headers...
		Dst->SetStr(FIELD_INTERNET_HEADER, Src->GetStr(FIELD_INTERNET_HEADER));

		// Do mail merge of data part...
		LAutoStreamI Data = Src->GetStream(_FL);
		if (Data)
		{
			// Read data out into a string string...
			LAutoString Str(new char[(int)Data->GetSize()+1]);
			Data->Read(Str, (int)Data->GetSize());
			Str[Data->GetSize()] = 0;

			// Do field insert and save result to segment
			char *Merged = ScribeInsertFields(Str, Dom);
			LAutoStreamI Mem(new LMemStream(Merged, strlen(Merged)));
			Dst->SetStream(Mem);
		}
	}
	else
	{
		// Straight copy...
		Dst->CopyProps(*Src);
	}

	// Merge children segments as well
	LDataIt Sc = Src->GetList(FIELD_MIME_SEG);
	LDataIt Dc = Dst->GetList(FIELD_MIME_SEG);
	if (Dc && Sc)
	{
		for (unsigned i=0; i<Sc->Length(); i++)
		{
			// Create new dest child, and merge the source child across
			LDataPropI *NewDestChild = Dc->Create(Dst->GetStore());
			if (!MergeSegments(NewDestChild, (*Sc)[i], Dom))
				return false;

			LDataI *NewDest = dynamic_cast<LDataI*>(NewDestChild);
			if (NewDest)
				NewDest->Save(Dst);
		}
	}

	return true;
}

// Either 'FileName' or 'Source' will be valid
void ScribeWnd::MailMerge(LArray<ListAddr*> &Contacts, const char *FileName, Mail *Source)
{
	THREAD_UNSAFE();

	ScribeFolder *Outbox = GetFolder(FOLDER_OUTBOX);

	if (Outbox && Contacts.Length() && (FileName || Source))
	{
		LAutoPtr<Mail> ImportEmail;
		if (FileName)
		{
			LAutoPtr<LFile> File(new LFile);
			if (File->Open(FileName, O_READ))
			{
				Thing *t = CreateItem(MAGIC_MAIL, Outbox, false);
				ImportEmail.Reset(Source = t->IsMail());
				if (!Source->Import(Source->AutoCast(File), sMimeMessage))
				{
					Source = 0;
				}
			}
		}
		
		if (Source)
		{
			List<Mail> Msgs;
			ScribeDom Dom(this);

			// Do the merging of the document with the database
			for (unsigned i=0; i<Contacts.Length(); i++)
			{
				ListAddr *la = Contacts[i];
				RecipientItem *ri = (*la)[0];
				Contact *c = ri ? ri->GetContact() : 0;
				Contact *temp = new Contact(this);
				if (!c)
				{
					temp->SetFirst(la->sName);
					temp->SetEmail(la->sAddr);
					c = temp;
				}

				Dom.Con = c;
				Thing *t = CreateItem(MAGIC_MAIL, Outbox, false);
				if (t)
				{
					Dom.Email = t->IsMail();
					
					LAutoString s;
					if (s.Reset(ScribeInsertFields(Source->GetSubject(), &Dom)))
						Dom.Email->SetSubject(s);
					LDataPropI *Recip = Dom.Email->GetTo()->Create(Dom.Email->GetObject()->GetStore());
					if (Recip)
					{
						Recip->CopyProps(*Dom.Con->GetObject());
						Recip->SetInt(FIELD_CC, 0);
						Dom.Email->GetTo()->Insert(Recip);
					}
					MergeSegments(	Dom.Email->GetObject()->GetObj(FIELD_MIME_SEG),
									Source->GetObject()->GetObj(FIELD_MIME_SEG),
									&Dom);

					Msgs.Insert(Dom.Email);
				}

				temp->DecRef();
			}

			// Ask user what to do
			if (Msgs[0])
			{
				char Msg[256];
				sprintf_s(Msg, sizeof(Msg), LLoadString(IDS_MAIL_MERGE_Q), Msgs.Length());
				
				auto Ask = new LAlert(this, AppName, Msg,
					LLoadString(IDS_SAVE_TO_OUTBOX), LLoadString(IDS_CANCEL));

				Ask->DoModal([this, Msgs, Outbox](auto dlg, auto id)
				{
					switch (id)
					{
						case 1: // Save To Outbox
						{
							for (size_t i=0; i<Msgs.Length(); i++)
							{
								Msgs[i]->Save(Outbox);
							}
							break;
						}
						case 2: // Cancel
						{
							for (size_t i=0; i<Msgs.Length(); i++)
							{
								Msgs[i]->OnDelete();
							}
							break;
						}
					}
				});
			}
			else
			{
				LgiMsg(this, LLoadString(IDS_MAIL_MERGE_EMPTY), AppName);
			}
		}
	}
}

ScribeFolder *CastFolder(LDataI *f)
{
	if (!f)
	{
		LAssert(!"Null pointer");
		return 0;
	}

	if (f->Type() == MAGIC_FOLDER)
	{
		return (ScribeFolder*)f->UserData;
	}

	return 0;
}

Thing *CastThing(LDataI *t)
{
	if (!t)
	{
		LAssert(!"Null pointer");
		return 0;
	}

	if (t->Type() == MAGIC_FOLDER)
	{
		LAssert(!"Shouldn't be a folder");
		return 0;
	}

	return (Thing*)t->UserData;
}

LString _GetUids(LArray<LDataI*> &items)
{
	LString::Array a;
	for (auto i: items)
		a.New().Printf(LPrintfInt64, i->GetInt(FIELD_SERVER_UID));
	return LString(",").Join(a);
}

