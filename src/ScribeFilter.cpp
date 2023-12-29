/*
**	FILE:			ScribeFilter.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			29/10/1999
**	DESCRIPTION:	Scribe filters
**
**	Copyright (C) 1999-2022, Matthew Allen
**		fret@memecode.com
**
*/

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>

#include "Scribe.h"
#include "lgi/common/Combo.h"
#include "lgi/common/Edit.h"
#include "lgi/common/Button.h"
#include "lgi/common/ProgressDlg.h"
#include "lgi/common/FilterUi.h"
#include "lgi/common/XmlTree.h"
#include "lgi/common/ClipBoard.h"
#include "lgi/common/TabView.h"
#include "lgi/common/Printer.h"
#include "lgi/common/LgiRes.h"
#include "lgi/common/FileSelect.h"
#include "lgi/common/Charset.h"

#include "resdefs.h"
#include "resource.h"

#define COMBINE_OP_AND				0
#define COMBINE_OP_OR				1

#define IDM_TRUE					500
#define IDM_FALSE					501
#define IDM_NOTNEW					502
#define IDM_LOCAL					503
#define IDM_SERVER					504
#define IDM_LOCAL_AND_SERVER		505

const char *ATTR_NOT				= "Not";
const char *ATTR_FIELD				= "Field";
const char *ATTR_OP					= "Op";
const char *ATTR_VALUE				= "Value";

const char *ELEMENT_AND				= "And";
const char *ELEMENT_OR				= "Or";
const char *ELEMENT_CONDITION		= "Condition";
const char *ELEMENT_CONDITIONS		= "Conditions";

const char *ELEMENT_ACTION			= "Action";

#define SkipWs(s)					while ((*s) && strchr(WhiteSpace, *s)) s++;

//////////////////////////////////////////////////////////////
class FilterPrivate
{
public:
	bool *Stop = NULL;
	LStream *Log = NULL;
};

//////////////////////////////////////////////////////////////
// Filter field definitions
ItemFieldDef FilterFieldDefs[] = {
	{"Name",     SdName, 		GV_STRING,	FIELD_FILTER_NAME},
	{"Index",    SdIndex, 		GV_INT32,	FIELD_FILTER_INDEX},
	{"Incoming", SdIncoming, 	GV_INT32,	FIELD_FILTER_INCOMING},
	{"Outgoing", SdOutgoing, 	GV_INT32,	FIELD_FILTER_OUTGOING},
	{"Internal", SdInternal, 	GV_INT32,	FIELD_FILTER_INTERNAL},
	{0}
};

int DefaultFilterFields[] =
{
	FIELD_FILTER_NAME,
	FIELD_FILTER_INDEX,
	FIELD_FILTER_INCOMING,
	FIELD_FILTER_OUTGOING,
	FIELD_FILTER_INTERNAL,
	0
};

#define ForCondField(Macro, Value)							\
	switch (Value)											\
	{														\
		case 0: Macro("To"); break;							\
		case 1: Macro("From"); break;						\
		case 2: Macro("Subject"); break;					\
		case 3: Macro("Size"); break;						\
		case 4: Macro("DateReceived"); break;				\
		case 5: Macro("DateSent"); break;					\
		case 6: Macro("Body"); break;						\
		case 7: Macro("InternetHeaders"); break;			\
		case 8: Macro("MessageID"); break;					\
		case 9: Macro("Priority"); break;					\
		case 10: /* flags */ break;							\
		case 11: Macro("Html"); break;						\
		case 12: Macro("Label"); break;						\
		case 13: Macro("From.Contact"); break;				\
		case 14: Macro("ImapCacheFile"); break;				\
		case 15: Macro("ImapFlags"); break;					\
		case 16: Macro("Attachments"); break;				\
		case 17: Macro("AttachmentNames"); break;			\
		case 18: Macro("From.Groups"); break;				\
		case 19: Macro("*"); break;							\
	}

struct ActionName
{
	int Id;
	const char *Default;
};

ActionName ActionNames[] = {
	{IDS_ACTION_MOVE_FOLDER, "Move to Folder"},
	{IDC_DELETE, "Delete"},
	{IDS_PRINT, "Print"},
	{IDS_ACTION_SOUND, "Play Sound"},
	{IDS_ACTION_OPEN, "Open Email"},
	{IDS_ACTION_EXECUTE, "Execute Process"},
	{IDS_ACTION_SET_COLOUR, "Set Colour"},
	{IDS_SET_READ, "Set Read"},
	{IDS_ACTION_SET_LABEL, "Set Label"},
	{IDS_ACTION_EMPTY_FOLDER, "Empty Folder"},
	{IDS_ACTION_MARK_SPAM, "Mark As Spam"},
	{IDS_REPLY, "Reply"},
	{IDS_FORWARD, "Forward"},
	{IDS_BOUNCE, "Bounce"},
	{IDS_ACTION_SAVE_ATTACHMENTS, "Save Attachment(s)"},
	{IDS_ACTION_DELETE_ATTACHMENTS, "Delete Attachments(s)"},
	{L_CHANGE_CHARSET, "Change Charset"},
	{IDS_ACTION_COPY, "Copy to Folder"},
	{IDS_EXPORT, "Export"},
	{0, 0},
	{IDS_ACTION_CREATE_FOLDER, "Create Folder"},
	{0, 0}
};

// These are the english names used for storing XML
const char *OpNames[] =
{
	"=",
	"!=",
	"<",
	"<=",
	">=",
	">",
	"Like",			// LLoadString(IDS_LIKE),
	"Contains",		// LLoadString(IDS_CONTAINS),
	"Starts With",	// LLoadString(IDS_STARTS_WITH),
	"Ends With",		// LLoadString(IDS_ENDS_WITH),
	0
};

const char *TranslatedOpNames[] =
{
	"=",
	"!=",
	"<",
	"<=",
	">=",
	">",
	0, // like
	0, // contains
	0, // starts with
	0, // ends with
	0
};

const char **GetOpNames(bool Translated)
{
	if (Translated)
	{
		if (TranslatedOpNames[6] == NULL)
		{
			TranslatedOpNames[6] = LLoadString(IDS_LIKE);
			TranslatedOpNames[7] = LLoadString(IDS_CONTAINS);
			TranslatedOpNames[8] = LLoadString(IDS_STARTS_WITH);
			TranslatedOpNames[9] = LLoadString(IDS_ENDS_WITH);
		}

		if (TranslatedOpNames[6])
		{
			return TranslatedOpNames;
		}
	}

	return OpNames;
}

//////////////////////////////////////////////////////////////
void SkipSep(const char *&s)
{
	while (s && *s && strchr(" \t,", *s))
		s++;
}

LCombo *LoadTemplates(ScribeWnd *App, LView *Wnd, List<char> &MsgIds, int Ctrl, char *Template)
{
	LCombo *Temp;
	if (Wnd->GetViewById(IDC_TEMPLATE, Temp))
	{
		ScribeFolder *Templates = App->GetFolder(FOLDER_TEMPLATES);
		if (Templates)
		{
			int n=0;
			for (auto t: Templates->Items)
			{
				Mail *m = t->IsMail();
				if (m)
				{
					auto Id = m->GetMessageId(true);
					if (Id)
					{
						MsgIds.Insert(NewStr(Id));
						Temp->Insert(m->GetSubject() ? m->GetSubject() : (char*)"(no subject)");
						if (Template && strcmp(Template, Id) == 0)
						{
							Temp->Value(n);
						}
					}
				}
				n++;
			}
		}
	}
	return Temp;
}

class BrowseReply : public LDialog
{
	List<char> MsgIds;
	LCombo *Temp;

public:
	LString Arg;

	BrowseReply(ScribeWnd *App, LView *Parent, const char *arg)
	{
		SetParent(Parent);
		LoadFromResource(IDD_FILTER_REPLY);
		MoveToCenter();
		
		char *Template = LTokStr(arg);
		SkipSep(arg);
		char *All = LTokStr(arg);
		SkipSep(arg);
		char *MarkReplied = LTokStr(arg);
		if (All)
		{
			SetCtrlValue(IDC_ALL, atoi(All));
		}
		if (MarkReplied)
		{
			SetCtrlValue(IDC_MARK_REPLIED, atoi(MarkReplied));
		}
		
		Temp = LoadTemplates(App, this, MsgIds, IDC_TEMPLATE, Template);
		
		DeleteArray(Template);
		DeleteArray(MarkReplied);
		DeleteArray(All);
	}

	~BrowseReply()
	{
		MsgIds.DeleteArrays();
	}
	
	int OnNotify(LViewI *c, LNotification n)
	{
		switch (c->GetId())
		{
			case IDOK:
			{
				if (Temp)
				{
					char *Template = MsgIds[(int)Temp->Value()];
					int All = (int)GetCtrlValue(IDC_ALL);
					int MarkReplied = (int)GetCtrlValue(IDC_MARK_REPLIED);
					char s[256];
					sprintf_s(s, sizeof(s), "\"%s\" %i %i", Template?Template:(char*)"", All, MarkReplied);
					Arg = s;
				}
				
				// Fall thru
			}
			case IDCANCEL:
			{
				EndModal(c->GetId() == IDOK);
				break;
			}
		}
		
		return 0;
	}	
};

class BrowseForward : public LDialog
{
	List<char> MsgIds;
	ScribeWnd *App;
	LCombo *Temp;
	bool UseTemplate;

public:
	LString Arg;

	BrowseForward(ScribeWnd *app, LView *parent, const char *arg, bool temp)
	{
		UseTemplate = temp;
		App = app;
		Arg = 0;
		Temp = 0;
		SetParent(parent);
		LoadFromResource(IDD_FILTER_FORWARD);
		MoveToCenter();
		
		char *Template = 0;
		if (UseTemplate)
		{
			Template = LTokStr(arg);
			SkipSep(arg);
		}
		char *Email = LTokStr(arg);
		SkipSep(arg);
		char *Forward = LTokStr(arg);
		SkipSep(arg);
		char *MarkForwarded = LTokStr(arg);

		if (UseTemplate)
		{
			bool HasTemplate = ValidStr(Template) ? strlen(Template) > 1 : 0;
			SetCtrlValue(IDC_USE_TEMPLATE, HasTemplate);
			Temp = LoadTemplates(App, this, MsgIds, IDC_TEMPLATE, HasTemplate ? Template : 0);
		}
		else
		{
			LViewI *v = FindControl(IDC_USE_TEMPLATE);
			if (v)
			{
				int y1 = v->GetPos().y1;
				Children.Delete(v);
				DeleteObj(v);
				v = FindControl(IDC_TEMPLATE);
				if (v)
				{
					int y2 = v->GetPos().y2;
					Children.Delete(v);
					DeleteObj(v);

					int Sub = y1 - y2 - 10;
					
					for (auto v: Children)
					{
						LRect r = v->GetPos();
						r.Offset(0, Sub);
						v->SetPos(r);
					}

					LRect r = GetPos();
					r.y2 += Sub;
					SetPos(r);
				}
			}
		}

		SetCtrlName(IDC_EMAIL, Email);
		if (Forward) SetCtrlValue(IDC_ATTACHMENTS, atoi(Forward));
		if (MarkForwarded) SetCtrlValue(IDC_MARK_FORWARDED, atoi(MarkForwarded));

		DeleteArray(Template);
		DeleteArray(Email);
		DeleteArray(Forward);
		DeleteArray(MarkForwarded);
	}
	
	~BrowseForward()
	{
		DeleteArray(Arg);
	}
	
	int OnNotify(LViewI *c, LNotification n)
	{
		switch (c->GetId())
		{
			case IDOK:
			{
				char s[256];
				bool HasTemplate = GetCtrlValue(IDC_USE_TEMPLATE) != 0;
				char *MsgId = HasTemplate ? MsgIds[(int)GetCtrlValue(IDC_TEMPLATE)] : 0;
				const char *Email = GetCtrlName(IDC_EMAIL);
				int Attachments = (int)GetCtrlValue(IDC_ATTACHMENTS);
				int MarkForwarded = (int)GetCtrlValue(IDC_MARK_FORWARDED);
				if (UseTemplate)
				{
					sprintf_s(s, sizeof(s), "\"%s\" \"%s\" %i %i", MsgId, Email?Email:(char*)"", Attachments, MarkForwarded);
				}
				else
				{
					sprintf_s(s, sizeof(s), "\"%s\" %i %i", Email?Email:(char*)"", Attachments, MarkForwarded);
				}
				Arg = s;
				// Fall thru				
			}
			case IDCANCEL:
			{
				EndModal(c->GetId() == IDOK);
			}
		}
		
		return 0;
	}
};

class BrowseSaveAttach : public LDialog
{
	ScribeWnd *App;

public:
	char *Arg;

	BrowseSaveAttach(ScribeWnd *app, LView *parent, const char *arg)
	{
		Arg = 0;
		App = app;
		SetParent(parent);
		if (LoadFromResource(IDD_FILTER_SAVE_ATTACH))
		{
			MoveToCenter();
			
			char *Dir = LTokStr(arg);
			SkipSep(arg);
			char *Types = LTokStr(arg);
			SetCtrlName(IDC_DIR, Dir);
			SetCtrlName(IDC_TYPES, Types);
			DeleteArray(Dir);
			DeleteArray(Types);
		}
	}
	
	~BrowseSaveAttach()
	{
		DeleteArray(Arg);
	}

	int OnNotify(LViewI *c, LNotification n)
	{
		switch (c->GetId())
		{
			case IDC_BROWSE_DIR:
			{
				auto s = new LFileSelect(this);
				s->Name(GetCtrlName(IDC_DIR));
				s->OpenFolder([this](auto s, auto status)
				{
					if (status)
						SetCtrlName(IDC_DIR, s->Name());
					delete s;
				});
				break;
			}
			case IDOK:
			{
				char s[512];
				const char *Dir = GetCtrlName(IDC_DIR);
				const char *Types = GetCtrlName(IDC_TYPES);
				sprintf_s(s, sizeof(s), "\"%s\",\"%s\"", Dir?Dir:(char*)"", Types?Types:(char*)"");
				Arg = NewStr(s);
				// Fall thru				
			}
			case IDCANCEL:
			{
				EndModal(c->GetId() == IDOK);
			}
		}
		
		return 0;
	}
};

bool LgiCreateTempFileName(char *Path, int PathLen)
{
	if (Path)
	{
		#if defined WIN32
		int Len = GetTempPathA(PathLen, Path);
		#else
		strcpy(Path, "/tmp");
		int Len = (int)strlen(Path);
		#endif
		if (Path[Len-1] != DIR_CHAR) strcat(Path, DIR_STR);
		
		int len = (int) strlen(Path);
		sprintf_s(Path+len, PathLen-len, "~%i.txt", LRand(10000));
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////
FilterCondition::FilterCondition()
{
	Op = 0;
	Not = false;
}

FilterCondition &FilterCondition::operator=(FilterCondition &c)
{
	Source.Reset(NewStr(c.Source));
	Op = c.Op;
	Not = c.Not;
	Value.Reset(NewStr(c.Value));
	
	return *this;
}

bool FilterCondition::Test(Filter *F, Mail *m, LStream *Log)
{
	if (Log) Log->Print("\tCondition.Test Fld='%s'\n", (char*)Source);

	if (ValidStr(Source))
	{
		int Flds = 0;
		for (; MailFieldDefs[Flds].FieldId; Flds++)
		{
		}

		LVariant v;

		// Get data
		if (_stricmp(Source, "mail.attachments") == 0)
		{
			// attachment(s) data
			List<Attachment> Attachments;

			if (m->GetAttachments(&Attachments))
			{
				for (auto a: Attachments)
				{
					char *Data;
					ssize_t Length;
					LDateTime Temp;
					
					if (a->Get(&Data, &Length))
					{
						// Zero terminate the string
						LVariant v;
						v.SetBinary(Length, Data);

						// Test the file
						if (TestData(F, v, Log))
						{
							return true;
						}
					}
				}
			}

			return false;
		}
		else if (_stricmp(Source, "mail.attachmentnames") == 0)
		{
			// attachment(s) name
			List<Attachment> Attachments;
			// ItemFieldDef AttachType = {"Attachment(s) Name", SdAttachmentNames, GV_STRING};
			LDateTime Temp;

			if (m->GetAttachments(&Attachments))
			{
				for (auto a: Attachments)
				{
					LVariant v = a->GetName();
					if (TestData(F, v, Log))
					{
						return true;
					}
				}
			}

			return false;
		}
		else
		{
			bool Status = false;
			ItemFieldDef *f = 0;
			if (_stricmp(Source, "mail.*") == 0)
			{
				ItemFieldDef *Start = MailFieldDefs;
				ItemFieldDef *End = MailFieldDefs + Flds - 1;

				for (f = Start; f <= End && !Status; f++)
				{
					switch (f->FieldId)
					{
						case FIELD_TO:
						{
							for (LDataPropI *a = m->GetTo()->First(); a;
								a = m->GetTo()->Next())
							{
								char Data[256];
								sprintf_s(Data, sizeof(Data),
										"%s <%s>",
										a->GetStr(FIELD_NAME),
										a->GetStr(FIELD_EMAIL));
								LVariant v(Data);
								if (TestData(F, v, Log))
								{
									Status |= true;
								}
							}
							continue;
							break;
						}
						case FIELD_FROM:
						{
							char Data[256];
							sprintf_s(Data, sizeof(Data),
									"%s <%s>",
									m->GetFrom()->GetStr(FIELD_NAME),
									m->GetFrom()->GetStr(FIELD_EMAIL));
							v = Data;
							break;
						}
						case FIELD_REPLY:
						{
							char Data[256];
							sprintf_s(Data, sizeof(Data),
									"%s <%s>",
									m->GetReply()->GetStr(FIELD_NAME),
									m->GetReply()->GetStr(FIELD_EMAIL));
							v = Data;
							break;
						}
						case FIELD_SUBJECT:
							v = m->GetSubject();
							break;
						case FIELD_SIZE:
							v = m->TotalSizeof();
							break;
						case FIELD_DATE_RECEIVED:
							v = m->GetDateReceived();
							break;
						case FIELD_DATE_SENT:
							v = m->GetDateSent();
							break;
						case FIELD_TEXT:
							v = m->GetBody();
							break;
						case FIELD_INTERNET_HEADER:
							v = m->GetInternetHeader();
							break;
						case FIELD_MESSAGE_ID:
							v = m->GetMessageId();
							break;
						case FIELD_PRIORITY:
							v = m->GetPriority();
							break;
						case FIELD_ALTERNATE_HTML:
							v = m->GetHtml();
							break;
						case FIELD_LABEL:
							v = m->GetLabel();
							break;
					}
				}
			}
			else
			{
				if (F)
				{
					F->GetValue(Source, v);
				}
				else if (Log)
				{
					Log->Print("%s:%i - Error: No filter to query value.\n", __FILE__, __LINE__);
				}
			}

			if (v.Type)
			{
				// Test data
				Status |= TestData(F, v, Log);
			}
			else if (Log)
			{
				Log->Print("%s:%i - Error: Variant doesn't have type!\n", __FILE__, __LINE__);
			}
			
			return Status;
		}
	}
	
	return false;
}

char *LogPreview(char *s)
{
	LStringPipe p(1 << 10);
	if (s)
	{
		char *c;
		for (c = s; *c && c - s < 200; c++)
		{
			switch (*c)
			{
				case '\n': p.Push("\\n"); break;
				case '\r': p.Push("\\r"); break;
				case '\t': p.Push("\\t"); break;
				default:
				{
					p.Push(c, 1);
				}
			}
		}
		if (*c)
		{
			p.Push("...");
		}
	}
	return p.NewStr();
}

bool FilterCondition::TestData(Filter *F, LVariant &Var, LStream *Log)
{
	// Do DOM lookup on the Value
	LVariant Val;
	if (F && F->Evaluate(Value, Val))
	{
		// Compare using type
		switch (Var.Type)
		{
			case GV_LIST:
			{
				for (auto v: *Var.Value.Lst)
				{
					if (TestData(F, *v, Log))
					{
						return true;
					}
				}
				break;
			}
			case GV_DOM:
			{
				// Probably an address field
				LVariant n;
				if (Var.Value.Dom->GetValue("Name", n))
				{
					if (TestData(F, n, Log))
					{
						return true;
					}
				}

				if (Var.Value.Dom->GetValue("Email", n))
				{
					if (TestData(F, n, Log))
					{
						return true;
					}
				}
				break;
			}
			case GV_STRING:
			{
				char *sVar = Var.Str();
				char *sVal = Val.Str();
				bool IsStr = ValidStr(sVar);
				bool IsVal = ValidStr(sVal);
				char *VarLog = Log ? LogPreview(sVar) : 0;
				bool m = false;

				switch (Op)
				{
					case OP_EQUAL:
					{
						if (!IsStr && !IsVal)
						{
							m = true;
						}
						else if (IsStr && IsVal)
						{
							m = _stricmp(sVal, sVar) == 0;
						}

						if (Log) Log->Print("\t\t\t'%s' == '%s' = %i\n", VarLog, sVal, m);
						break;
					}
					case OP_LIKE:
					{
						m = MatchStr(sVal, sVar);
						if (Log) Log->Print("\t\t\t'%s' like '%s' = %i\n", VarLog, sVal, m);
						break;
					}
					case OP_CONTAINS:
					{
						if (IsVal && IsStr)
						{
							m = stristr(sVar, sVal) != 0;
							if (Log) Log->Print("\t\t\t'%s' contains '%s' = %i\n", VarLog, sVal, m);
						}
						break;
					}
					case OP_STARTS_WITH:
					{
						if (IsVal && IsStr)
						{
							size_t Len = strlen(sVal);
							m = _strnicmp(sVar, sVal, Len) == 0;
							if (Log) Log->Print("\t\t\t'%s' starts with '%s' = %i\n", VarLog, sVal, m);
						}
						break;
					}
					case OP_ENDS_WITH:
					{
						if (IsVal && IsStr)
						{
							size_t SLen = strlen(sVar);
							size_t VLen = strlen(sVal);
							if (SLen >= VLen)
							{
								m = _strnicmp(sVar + SLen - VLen, sVal, VLen) == 0;
								if (Log) Log->Print("\t\t\t'%s' ends with '%s' = %i\n", VarLog, sVal, m);
							}
							else
							{
								if (Log) Log->Print("\t\t\tEnds With Error: '%s' is shorter than '%s'\n", sVar, sVal);
							}
						}
						else
						{
							if (Log) Log->Print("\t\t\tEnds With Error: invalid arguments\n");
						}
						break;
					}
				}

				DeleteArray(VarLog);
				return m;
				break;
			}
			case GV_INT32:
			{
				// Convert Val to int
				int Int = 0;
				if (Val.Str())
				{
					Int = atoi(Val.Str());
				}
				else if (Val.Type == GV_INT32)
				{
					Int = Val.Value.Int; 
				}
				
				int IntVal = Var.Value.Int;

				switch (Op)
				{
					case OP_LIKE: // for lack anything better
					case OP_EQUAL:
					{
						bool m = Int == IntVal;
						if (Log) Log->Print("\t\t\t%i == %i = %i\n", Int, IntVal, m);
						return m;
					}
					case OP_NOT_EQUAL:
					{
						bool m = Int != IntVal;
						if (Log) Log->Print("\t\t\t%i != %i = %i\n", Int, IntVal, m);
						return m;
					}
					case OP_LESS_THAN:
					{
						bool m = Int < IntVal;
						if (Log) Log->Print("\t\t\t%i < %i = %i\n", Int, IntVal, m);
						return m;
					}
					case OP_LESS_THAN_OR_EQUAL:
					{
						bool m = Int <= IntVal;
						if (Log) Log->Print("\t\t\t%i <= %i = %i\n", Int, IntVal, m);
						return m;
					}
					case OP_GREATER_THAN:
					{
						bool m = Int > IntVal;
						if (Log) Log->Print("\t\t\t%i > %i = %i\n", Int, IntVal, m);
						return m;
					}
					case OP_GREATER_THAN_OR_EQUAL:
					{
						bool m = Int >= IntVal;
						if (Log) Log->Print("\t\t\t%i >= %i = %i\n", Int, IntVal, m);
						return m;
					}
				}
				break;
			}
			case GV_DATETIME:
			{
				LDateTime Temp;
				LDateTime *DVal;
				if (Val.Type == GV_DATETIME)
				{
					DVal = Val.Value.Date;
				}
				else if (Val.Type == GV_STRING)
				{
					Temp.Set(Val.Str());
					DVal = &Temp;
				}
				else break;
				
				LDateTime *DVar = Var.Value.Date;

				if (DVal && DVar)
				{
					bool Less = *DVar < *DVal;
					bool Greater = *DVar > *DVal;
					bool Equal = !Less && !Greater;
					char LogVal[64];
					char LogVar[64];
					if (Log)
					{
						DVal->Get(LogVal, sizeof(LogVal));
						DVar->Get(LogVar, sizeof(LogVar));
					}

					switch (Op)
					{
						case OP_LIKE:
						case OP_EQUAL:
						{
							if (Log) Log->Print("\t\t\t%s = %s == %i\n", LogVar, LogVal, Equal);
							return Equal;
						}
						case OP_NOT_EQUAL:
						{
							if (Log) Log->Print("\t\t\t%s != %s == %i\n", LogVar, LogVal, !Equal);
							return !Equal;
						}
						case OP_LESS_THAN:
						{
							if (Log) Log->Print("\t\t\t%s <= %s == %i\n", LogVar, LogVal, Less);
							return Less;
						}
						case OP_LESS_THAN_OR_EQUAL:
						{
							if (Log) Log->Print("\t\t\t%s < %s == %i\n", LogVar, LogVal, Less || Equal);
							return Less || Equal;
						}
						case OP_GREATER_THAN:
						{
							if (Log) Log->Print("\t\t\t%s > %s == %i\n", LogVar, LogVal, Greater);
							return Greater;
						}
						case OP_GREATER_THAN_OR_EQUAL:
						{
							if (Log) Log->Print("\t\t\t%s >= %s == %i\n", LogVar, LogVal, Greater || Equal);
							return Greater || Equal;
						}
					}
				}
				break;
			}
			default:
			{
				if (Log) Log->Print("\t\t\tUnknown data type %i.\n", Var.Type);
				break;
			}
		}
	}

	return false;
}

ThingUi *FilterCondition::DoUI(MailContainer *c)
{
	return NULL;
}

//////////////////////////////////////////////////////////////
// #define OPT_Action	"Action"
#define OPT_Type		"Type"
#define OPT_Arg1		"Arg1"

#define IDC_TYPE_CBO	2000
#define IDC_ARG_EDIT	2001
#define IDC_BROWSE_ARG	2002

static FilterIcon FilterActionIcons[] = { IconMoveDown, IconMoveUp, IconDelete };

FilterAction::FilterAction(Filter *Owner, LDataStoreI *Store)
{
	owner = Owner;
}

FilterAction::~FilterAction()
{
}

LImageList *FilterAction::GetIcons()
{
	return owner->GetIcons();
}

void FilterAction::OnMouseClick(LMouse &m)
{
	for (size_t i=0; i<iconPos.Length(); i++)
	{
		if (iconPos[i].Overlap(m))
		{
			if (m.Down())
				OnIconClick(FilterActionIcons[i]);
			return; // item could be deleted by now.
		}
	}

	LListItem::OnMouseClick(m);
}

void FilterAction::OnIconClick(int icon)
{
	LNotification n(LNotifyItemChange);
	n.Int[0] = icon;
	LListItem::GetList()->SendNotify(n);
}

int FilterAction::OnNotify(LViewI *c, LNotification n)
{
	switch (c->GetId())
	{
		case IDC_TYPE_CBO:
		{
			Type = (FilterActionTypes) c->Value();
			break;
		}
		case IDC_ARG_EDIT:
		{
			Arg1 = c->Name();
			break;
		}
		case IDC_BROWSE_ARG:
		{
			if (ArgEdit)
				ArgEdit->Name(Arg1);
			break;
		}
		default:
			break;
	}

	return 0;
}


void FilterAction::OnMeasure(LPoint *Info)
{
	LListItem::OnMeasure(Info);
	if (Select())
		Info->y += 2;
}

bool FilterAction::Select()
{
	return LListItem::Select();
}

void FilterAction::OnPaintColumn(LItem::ItemPaintCtx &Ctx, int i, LItemColumn *c)
{
	LListItem::OnPaintColumn(Ctx, i, c);

	if (LListItem::Select() && !TypeCbo && !ArgEdit)
		Select(true);
	else if (i == 0 && TypeCbo)
		TypeCbo->SetPos(*GetPos(i));
	else if (i == 1 && ArgEdit)
		ArgEdit->SetPos(*GetPos(i));
	else if (i == 2 && Btn)
		Btn->SetPos(*GetPos(i));
	else if (i == 3)
	{
		auto icons = GetIcons();
		if (!icons)
		{
			LDisplayString ds(GetFont(), "#errNoIcons");
			GetFont()->Colour(Ctx.Fore, Ctx.Back);
			ds.Draw(Ctx.pDC, Ctx.x1, Ctx.y1);
			return;
		}

		int x = Ctx.x1;
		int y = Ctx.y1 + ((Ctx.Y() - icons->TileY()) >> 1);
		for (int i=0; i<CountOf(FilterActionIcons); i++)
		{
			iconPos[i].ZOff(icons->TileX()-1, icons->TileY()-1);
			iconPos[i].Offset(x, y);

			icons->Draw(Ctx.pDC, iconPos[i].x1, iconPos[i].y1, FilterActionIcons[i], Ctx.Back);
			
			x += icons->TileX() + 2;
		}
	}
}

void FilterAction::Select(bool b)
{
	LListItem::Select(b);

	if (b)
	{
		LList *Lst = LListItem::GetList();
		if (Lst && Lst->IsAttached())
		{
			LRect *r = GetPos(0);

			if (!TypeCbo)
			{
				if (!TypeCbo.Reset(new LCombo(IDC_TYPE_CBO, r->x1, r->y1, r->X(), r->Y(), 0)))
					return;
				for (int i=0; ActionNames[i].Id; i++)
					TypeCbo->Insert(LLoadString(ActionNames[i].Id));
				TypeCbo->Attach(Lst);
			}
			TypeCbo->Value(Type);
			TypeCbo->SetPos(*r);

			r = GetPos(1);
			if (!ArgEdit)
			{
				if (!ArgEdit.Reset(new LEdit(IDC_ARG_EDIT, r->x1, r->y1, r->X(), r->Y(), 0)))
					return;
				ArgEdit->Attach(Lst);
			}
			ArgEdit->Name(Arg1);
			ArgEdit->SetPos(*r);

			r = GetPos(2);
			if (!Btn)
			{
				if (!Btn.Reset(new LButton(IDC_BROWSE_ARG, r->x1, r->y1, r->X(), r->Y(), "...")))
					return;
				Btn->Attach(Lst);
			}
			Btn->SetPos(*r);
		}
	}
	else
	{
		TypeCbo.Reset();
		ArgEdit.Reset();
		Btn.Reset();
	}
}

const char *FilterAction::GetText(int Col)
{
	switch (Col)
	{
		case 0:
			return (char*)LLoadString(ActionNames[Type].Id);
			break;
		case 1:
			return Arg1;
			break;
		case 2:
			break;
	}

	return 0;
}

bool FilterAction::Get(LXmlTag *t)
{
	if (!t) return false;

	// Obj -> XML
	t->SetAttr(OPT_Type, Type);
	t->SetAttr(OPT_Arg1, Arg1);
	return true;
}

bool FilterAction::Set(LXmlTag *t)
{
	if (!t) return false;

	// XML -> Obj
	Type = (FilterActionTypes) t->GetAsInt(OPT_Type);
	Arg1 = t->GetAttr(OPT_Arg1);
	return true;
}

LDataPropI &FilterAction::operator =(LDataPropI &p)
{
	FilterAction *c = dynamic_cast<FilterAction*>(&p);
	if (c)
	{
		Type = c->Type;
		Arg1 = c->Arg1;
	}

	return *this;
}

ThingUi *FilterAction::DoUI(MailContainer *c)
{
	return 0;
}

const char *GetFileName(const char *Path)
{
	if (Path)
	{
		auto d = strrchr(Path, DIR_CHAR);
		if (d)
			return d + 1;
		else
			return Path;
	}
	
	return 0;
}

bool CollectAttachmentsByPattern(Mail *m, LString Pattern, List<Attachment> &Files)
{
	Files.Empty();
	
	if (m)
	{
		List<Attachment> Attachments;
		if (m->GetAttachments(&Attachments))
		{
			auto p = Pattern.SplitDelimit(" ,;");
			for (auto a: Attachments)
			{
				bool Match = true;
				for (unsigned i=0; Match && i<p.Length(); i++)
				{
					auto d = GetFileName(a->GetName());
					if (d)
						Match = MatchStr(p[i], d);
				}				

				if (Match)
					Files.Insert(a);
			}
		}
	}
	
	return Files[0] != 0;
}

Mail *GetTemplateMail(ScribeWnd *App, char *TemplateMsgId)
{
	ScribeFolder *Templates = App->GetFolder(FOLDER_TEMPLATES);
	if (Templates)
	{
		// Mail *Template = 0;
		for (auto t: Templates->Items)
		{
			Mail *m = t->IsMail();
			if (m)
			{
				auto MsgId = m->GetMessageId();
				if (MsgId && strcmp(MsgId, TemplateMsgId) == 0)
				{
					return m;
					break;
				}
			}
		}
	}

	return 0;
}

class FilterScribeDom : public ScribeDom
{
public:
	FilterScribeDom(ScribeWnd *a) : ScribeDom(a)
	{
	}
	
	const char *GetClass() override { return "FilterScribeDom"; }

	bool GetVariant(const char *Name, LVariant &Value, const char *Array = NULL) override
	{
		if (!Name)
			return false;

		if (!_stricmp(Name, "file"))
		{
			LVariant v;
			if (!GetValue(Array, v))
				return false;

			char p[MAX_PATH_LEN], fn[32];
			do
			{
				sprintf_s(fn, sizeof(fn), "file_%d.tmp", LRand());
				LMakePath(p, sizeof(p), ScribeTempPath(), fn);
			}
			while (LFileExists(p));

			LFile f;
			if (f.Open(p, O_WRITE))
			{
				switch (v.Type)
				{
					case GV_INT32:
						f.Print("%i", v.Value.Int);
						break;
					case GV_INT64:
						f.Print(LPrintfInt64, v.Value.Int64);
						break;
					case GV_BOOL:
						f.Print("%s", v.Value.Bool ? "true" : "false");
						break;
					case GV_DOUBLE:
						f.Print("%f", v.Value.Dbl);
						break;

					case GV_STRING:
					case GV_WSTRING:
						f.Print("%s", v.Str());
						break;

					case GV_BINARY:
						f.Write(v.Value.Binary.Data, v.Value.Binary.Length);
						break;

					case GV_DATETIME:
						v.Value.Date->Get(fn, sizeof(fn));
						f.Write(fn, strlen(fn));
						break;

					default:
						f.Print("Unsupported type.");
						break;
				}

				f.Close();

				Value = p;
				return true;
			}
		}
		
		return ScribeDom::GetVariant(Name, Value, Array);
	}
};

bool FilterAction::Do(Filter *F, ScribeWnd *App, Mail *&m, LStream *Log)
{
    bool Status = false;
    
    if (!F || !App || !m)
    {
        LAssert(!"Param error.");
        return false;
    }
    
	switch (Type)
	{
		case ACTION_MOVE_TO_FOLDER:
		{
			ScribeFolder *Folder = App->GetFolder(Arg1);
			if (Folder)
			{
				LArray<Thing*> Items;
				Items.Add(m);
				Folder->MoveTo(Items, false, [this, Log](auto result, auto status)
				{
			        if (Log)
				        Log->Print("\tACTION_MOVE_TO_FOLDER(%s) = %i.\n", Arg1.Get(), result);
				});
				Status = true;
			}
			else if (Log)
			{
			    Log->Print("\tACTION_MOVE_TO_FOLDER(%s) failed, folder missing.\n", Arg1.Get());
			}
			break;
		}
		case ACTION_COPY:
		{
			ScribeFolder *Folder = App->GetFolder(Arg1);
			if (Folder)
			{				
				LArray<Thing*> Items;
				Items.Add(m);
				Folder->MoveTo(Items, true, [this, Log](auto result, auto status)
				{
					if (Log)
    					Log->Print("\tACTION_COPY(%s) = %i.\n", Arg1.Get(), result);
				});
				Status = true;
			}
			else if (Log)
			{
			    Log->Print("\tACTION_COPY(%s) failed, folder missing.\n", Arg1.Get());
			}
			break;
		}
		case ACTION_EXPORT:
		{
			LFile::Path p(Arg1);
			if (!p.IsFolder())
			{
				if (Log)
					Log->Print("\tACTION_EXPORT(%s) failed, folder missing.\n", Arg1.Get());
				break;
			}
		
			auto Fn = m->GetDropFileName();
			p += LGetLeaf(Fn);
			
			LAutoPtr<LFile> out(new LFile);
			if (!out->Open(p, O_WRITE))
			{
				if (Log)
					Log->Print("\tACTION_EXPORT(%s) failed: couldn't open file: %s.\n", Arg1.Get(), p.GetFull().Get());
				break;
			}
			
			if (!m->Export(m->AutoCast(out), sMimeMessage))
			{
				if (Log)
					Log->Print("\tACTION_EXPORT(%s) failed: couldn't export file.\n", Arg1.Get());
			}
			break;
		}
		case ACTION_DELETE:
		{
			bool Local = ValidStr(Arg1) ? stristr(Arg1, "local") != 0 : true;
			bool Server = stristr(Arg1, "server") != 0;

			if (Server)
			{
				ScribeAccount *a = m->GetAccountSentTo();
				if (!a)
				    break;

				auto Uid = m->GetServerUid();
				if (Uid.Str())
				{
					if (Log)
					    Log->Print("\tACTION_DELETE - Setting '%s' to be deleted on the server (Uid=%s)\n", m->GetSubject(), Uid.Str());
					    
					a->Receive.DeleteAsSpam(Uid.Str());
					Uid.Empty();
					m->SetServerUid(Uid);
				}
				else
				{
					LVariant Uid;
					if (m->GetValue("InternetHeader[X-UIDL]", Uid))
					{
						if (Log)
					        Log->Print("\tACTION_DELETE - Setting '%s' to be deleted on the server (Uid=%s)\n", m->GetSubject(), Uid.Str());
					        
						a->Receive.DeleteAsSpam(Uid.Str());
					}
				}
			}

			if (Local)
			{
				bool DeleteStatus = m->OnDelete();
				if (Log)
			        Log->Print("\tACTION_DELETE(%s) status %i.\n", Arg1.Get(), DeleteStatus);

				/*
				ScribeFolder *Folder = App->GetFolder(FOLDER_TRASH);
				if (Folder)
				{
					Thing *t = m;
					Status = Folder->MoveTo(t);
					m = t->IsMail();

                    if (Log)
			            Log->Print("\tACTION_DELETE(%s) = %i.\n", Arg1.Get(), Status);
				}
			    else if (Log)
			    {
			        Log->Print("\tACTION_DELETE(%s) failed, trash missing.\n", Arg1.Get());
			    }
			    */
			}
			break;
		}
		case ACTION_SET_READ:
		{
			bool Read = true;
			bool NotNew = false;
			
			if (ValidStr(Arg1))
			{
				if (IsDigit(*Arg1))
				{
					// boolean number
					Read = Arg1.Int() != 0;
				}
				else if (stristr(Arg1, "true"))
				{
					Read = true;
				}
				else if (stristr(Arg1, "false"))
				{
					Read = false;
				}
				
				if (stristr(Arg1, "notnew"))
				{
					NotNew = true;
				}
			}

			int f = m->GetFlags();

			if (Read)
				SetFlag(f, MAIL_READ);
			else
				ClearFlag(f, MAIL_READ);

			m->SetFlags(f);
			
			if (Log)
			    Log->Print("\tACTION_SET_READ(%s)\n", Arg1.Get());
			
			if (NotNew)
			{
				List<Mail> Objs;
				Objs.Insert(m);
				App->OnNewMail(&Objs, false);
			}
			break;
		}
		case ACTION_LABEL:
		{
		    LVariant v;
		    if (!F || !F->GetValue(Arg1, v))
				v = Arg1;

		    m->SetVariant("Label", v);
			break;
		}
		case ACTION_EMPTY_FOLDER:
		{
			if (F->App && Arg1)
			{
				ScribeFolder *Folder = F->App->GetFolder(Arg1);
				if (Folder)
				{
					Folder->LoadThings();

					List<Mail> m;

					Thing *t;
					while ((t = Folder->Items[0]))
					{
						if (t->IsMail())
						{
							m.Insert(t->IsMail());
						}

						if (Folder->DeleteThing(t))
						{
							DeleteObj(t);
						}
					}

					F->App->OnNewMail(&m, false);
					Folder->OnUpdateUnRead(0, true);

			        if (Log)
			            Log->Print("\tACTION_EMPTY_FOLDER(%s)\n", Arg1.Get());
				}
			}
			break;
		}
		case ACTION_MARK_AS_SPAM:
		{
			m->DeleteAsSpam(App);
			break;
		}
		case ACTION_PRINT:
		{
			LPrinter Info;

	        #ifdef _MSC_VER
			#pragma message ("Warning: ACTION_PRINT not implemented.")
	        #endif
			/* FIXME
			if (Info.Serialize(Arg1, false))
			{
				App->ThingPrint(m, &Info);
			}
			*/
			break;
		}
		case ACTION_PLAY_SOUND:
		{
			LPlaySound(Arg1, true); 
			break;
		}
		case ACTION_EXECUTE:
		{
			FilterScribeDom dom(App);
			dom.Email = m;
			dom.Fil = F;
			LAutoString cmd(ScribeInsertFields(Arg1, &dom));
			if (cmd)
			{
				const char *s = cmd;
				LAutoString exe(LTokStr(s));
				LExecute(exe, s);
			}
			break;
		}
		case ACTION_OPEN:
		{
			m->DoUI();
			break;
		}
		case ACTION_MARK:
		{
			if (Stricmp(Arg1.Get(), "false") == 0)
			{
				// unmark the item...
				m->SetMarkColour(0);
			}
			else
			{
				// parse out RGB
				auto T = Arg1.SplitDelimit(",");
				if (T.Length() == 3)
				{
					// we have an RGB, so set it baby
					uint32_t c = Rgb32(atoi(T[0]), atoi(T[1]), atoi(T[2]));
					m->SetMarkColour(c);
				}
				else
				{
					uint32_t c = Rgb32(0, 0, 255);
					m->SetMarkColour(c);
				}
			}
			break;
		}
		case ACTION_REPLY:
		{
			if (Arg1)
			{
				const char *s = Arg1;
				char *TemplateMsgId = LTokStr(s);
				SkipSep(s);
				char *ReplyAll = LTokStr(s);
				SkipSep(s);
				char *MarkReplied = LTokStr(s);
				
				Mail *Template = GetTemplateMail(App, TemplateMsgId);
				if (Template)
				{
					Thing *t = App->CreateThingOfType(MAGIC_MAIL);
					if (t)
					{
						Mail *n = t->IsMail();
						if (n)
						{
							bool MarkOriginal = MarkReplied && atoi(MarkReplied);

							// Prepare mail...
							n->OnReply(m, ValidStr(ReplyAll)?atoi(ReplyAll)!=0:false, MarkOriginal);
							n->SetFlags(m->GetFlags() | MAIL_READY_TO_SEND);
							
							if (ValidStr(Template->GetSubject()))
							{
								n->SetSubject(Template->GetSubject());
							}
							
							n->SetBody(ScribeInsertFields(Template->GetBody(), F));

							// Save it...
							n->Save(0);
							
							// Send it... if not offline.
							LVariant Offline;
							App->GetOptions()->GetValue(OPT_WorkOffline, Offline);
							if (!Offline.CastInt32())
							{
								App->PostEvent(M_COMMAND, IDM_SEND_MAIL, 0);
							}
						}
						else DeleteObj(t);
					}
				}
				
				DeleteArray(TemplateMsgId);
				DeleteArray(ReplyAll);
				DeleteArray(MarkReplied);
			}
			break;
		}
		case ACTION_FORWARD:
		{
			const char *s = Arg1;
			char *TemplateMsgId = LTokStr(s);
			SkipSep(s);
			char *Email = LTokStr(s);
			SkipSep(s);
			char *Attach = LTokStr(s);
			SkipSep(s);
			char *MarkForwarded = LTokStr(s);

			bool Attachments = Attach ? atoi(Attach)!=0 : true;
			if (ValidStr(Email))
			{
				Thing *t = App->CreateThingOfType(MAGIC_MAIL);
				if (t)
				{
					Mail *n = t->IsMail();
					if (n)
					{
						bool MarkOriginal = MarkForwarded && atoi(MarkForwarded);
						
						// Setup email...
						Mail *Template = TemplateMsgId ? GetTemplateMail(App, TemplateMsgId) : 0;
						if (Template)
						{
							n->SetSubject(Template->GetSubject());
							if (ValidStr(Template->GetBody()))
							{
								n->SetBody(ScribeInsertFields(Template->GetBody(), F));
							}

							if (Attachments)
							{
								List<Attachment> Att;
								m->GetAttachments(&Att);
								for (auto a: Att)
								{
									n->AttachFile(new Attachment(App, a));
								}
							}
						}
						else
						{
							n->OnForward(m, MarkOriginal, Attachments);
						}

						n->SetFlags(m->GetFlags() | MAIL_READY_TO_SEND);

						LDataPropI *Addr = n->GetTo()->Create(n->GetObject()->GetStore());
						if (Addr)
						{
							LVariant v;
							if (F->GetValue(Email, v))
							{
								Addr->SetStr(FIELD_EMAIL, v.Str());
							}
							else
							{
								Addr->SetStr(FIELD_EMAIL, Email);
							}
							n->GetTo()->Insert(Addr);
						}
						
						// Save it...
						n->Save(0);
						
						// Send it... if not offline.
						LVariant Offline;
						App->GetOptions()->GetValue(OPT_WorkOffline, Offline);
						if (!Offline.CastInt32())
						{
							App->PostEvent(M_COMMAND, IDM_SEND_MAIL, 0);
						}
					}
					else DeleteObj(t);
				}
			}
			
			DeleteArray(Email);
			DeleteArray(Attach);
			DeleteArray(MarkForwarded);			
			break;
		}
		case ACTION_BOUNCE:
		{
			const char *s = Arg1;
			char *Email = LTokStr(s);
			SkipSep(s);
			char *Attach = LTokStr(s);
			SkipSep(s);
			char *Mark = LTokStr(s);

			if (ValidStr(Email))
			{
				Thing *t = App->CreateThingOfType(MAGIC_MAIL);
				if (t)
				{
					Mail *n = t->IsMail();
					if (n)
					{
						bool Attachments = Attach && atoi(Attach);
						bool MarkOriginal = Mark && atoi(Mark);
						
						// Setup email...
						n->OnBounce(m, MarkOriginal, Attachments);
						n->SetFlags(m->GetFlags() | MAIL_READY_TO_SEND);

						LDataPropI *Addr = n->GetTo()->Create(n->GetObject()->GetStore());
						if (Addr)
						{
							LVariant v;
							if (F->GetValue(Email, v))
							{
								Addr->SetStr(FIELD_EMAIL, v.Str());
							}
							else
							{
								Addr->SetStr(FIELD_EMAIL, Email);
							}
							n->GetTo()->Insert(Addr);
						}
						
						// Save it...
						n->Save(0);
						
						// Send it... if not offline.
						LVariant Offline;
						App->GetOptions()->GetValue(OPT_WorkOffline, Offline);
						if (!Offline.CastInt32())
						{
							App->PostEvent(M_COMMAND, IDM_SEND_MAIL, 0);
						}
					}
					else DeleteObj(t);
				}
			}
			
			DeleteArray(Email);
			DeleteArray(Attach);
			DeleteArray(Mark);
			break;
		}
		case ACTION_SAVE_ATTACHMENTS:
		{
			const char *arg = Arg1;
			char *Dir = LTokStr(arg);
			SkipSep(arg);
			char *Types = LTokStr(arg);

			List<Attachment> Files;
			if (CollectAttachmentsByPattern(m, Types, Files))
			{
				for (auto a: Files)
				{
					auto d = GetFileName(a->GetName());
					if (d)
					{
						char Path[256];
						LMakePath(Path, sizeof(Path), Dir, d);
						a->SaveTo(Path);
					}
				}
			}

			DeleteArray(Dir);
			DeleteArray(Types);
			break;
		}
		case ACTION_DELETE_ATTACHMENTS:
		{
			List<Attachment> Files;
			if (CollectAttachmentsByPattern(m, Arg1, Files))
			{
				for (auto a: Files)
				{
					m->DeleteAttachment(a);
				}
			}
			break;
		}
		case ACTION_CHANGE_CHARSET:
		{
			m->SetBodyCharset(Arg1);
			break;
		}
	}

	return Status;
}

int CsCmp(LCharset **a, LCharset **b)
{
	return _stricmp((*a)->Charset, (*b)->Charset);
}

void FilterAction::DescribeHtml(Filter *Flt, LStream &s)
{
	s.Print("%s ", GetText(0));
	switch (Type)
	{
		case ACTION_MOVE_TO_FOLDER:
		{
			ScribeFolder *Folder = Flt->App->GetFolder(Arg1);
			if (Folder)
				s.Print("\"%s\"\n", Arg1.Get());
			else
				s.Print("\"<span class=error>%s</span>\"\n", Arg1.Get());
			break;
		}
		case ACTION_DELETE:
			break;
		case ACTION_PRINT:
			break;
		case ACTION_PLAY_SOUND:
			break;
		case ACTION_OPEN:
			break;
		case ACTION_EXECUTE:
			break;
		case ACTION_MARK:
			break;
		case ACTION_SET_READ:
			break;
		case ACTION_LABEL:
			break;
		case ACTION_EMPTY_FOLDER:
			break;
		case ACTION_MARK_AS_SPAM:
			break;
		case ACTION_REPLY:
			break;
		case ACTION_FORWARD:
			break;
		case ACTION_BOUNCE:
			break;
		case ACTION_SAVE_ATTACHMENTS:
			break;
		case ACTION_DELETE_ATTACHMENTS:
			break;
		case ACTION_CHANGE_CHARSET:
			break;
		case ACTION_COPY:
			break;
		case ACTION_EXPORT:
			break;
	}
}

void FilterAction::Browse(ScribeWnd *App, LView *Parent)
{
	if (!Parent) return;

	switch (Type)
	{
		default:
			LAssert(0);
			break;
		case ACTION_MOVE_TO_FOLDER:
		case ACTION_COPY:
		case ACTION_EMPTY_FOLDER:
		{
			auto Dlg = new FolderDlg(Parent, App, MAGIC_MAIL);
			Dlg->DoModal([this, Dlg](auto dlg, auto id)
			{
				if (id)
				{
					Arg1 = Dlg->Get();
					OnNotify(Btn, LNotifyValueChanged);
				}
				delete dlg;
			});
			break;
		}
		case ACTION_EXPORT:
		{
			auto s = new LFileSelect(Parent);
			s->OpenFolder([this](auto s, auto status)
			{
				if (status)
				{
					Arg1 = s->Name();
					OnNotify(Btn, LNotifyValueChanged);
				}
				delete s;
			});
			break;
		}
		case ACTION_DELETE:
		{
			LSubMenu RClick;
			RClick.AppendItem("Local (default)", IDM_LOCAL, true);
			RClick.AppendItem("From Server", IDM_SERVER, true);
			RClick.AppendItem("Local and from Server", IDM_LOCAL_AND_SERVER, true);

			LMouse m;
			if (!Parent->GetMouse(m, true))
				break;

			switch (RClick.Float(Parent, m.x, m.y))
			{
				case IDM_LOCAL:
				{
					Arg1 = "local";
					break;
				}
				case IDM_SERVER:
				{
					Arg1 = "server";
					break;
				}
				case IDM_LOCAL_AND_SERVER:
				{
					Arg1 = "local,server";
					break;
				}
				default:
				{
					return;
				}
			}

			OnNotify(Btn, LNotifyValueChanged);
			break;
		}
		case ACTION_OPEN:
		{
			// no configuration
			break;
		}
		case ACTION_SET_READ:
		{
			LSubMenu s;
			s.AppendItem("Read", IDM_TRUE, true);
			s.AppendItem("Unread", IDM_FALSE, true);
			s.AppendItem("Unread But Not New", IDM_NOTNEW, true);

			LMouse m;
			if (!Parent->GetMouse(m, true))
				break;

			switch (s.Float(Parent, m.x, m.y))
			{
				case IDM_TRUE:
				{
					Arg1 = "true";
					break;
				}
				case IDM_FALSE:
				{
					Arg1 = "false";
					break;
				}
				case IDM_NOTNEW:
				{
					Arg1 = "false,notnew";
					break;
				}
				default:
				{
					return;
				}
			}

			OnNotify(Btn, LNotifyValueChanged);
			break;
		}
		case ACTION_MARK:
		{
			LSubMenu s;
			BuildMarkMenu(&s, MS_One, 0);

			LMouse m;
			if (!Parent->GetMouse(m, true))
				break;

			int Result = s.Float(Parent, m.x, m.y);
			if (Result == IDM_UNMARK)
			{
				Arg1 = "False";
				OnNotify(Btn, LNotifyValueChanged);
			}
			else if (Result >= IDM_MARK_BASE)
			{
				char s[32];
				sprintf_s(s, sizeof(s),
						"%i,%i,%i",
						R32(MarkColours32[Result-IDM_MARK_BASE]),
						G32(MarkColours32[Result-IDM_MARK_BASE]),
						B32(MarkColours32[Result-IDM_MARK_BASE]));
				Arg1 = s;
				OnNotify(Btn, LNotifyValueChanged);
			}
			break;
		}
		case ACTION_PRINT:
		{
			LPrinter Info;
			#ifdef _MSC_VER
			#pragma message ("Warning: ACTION_PRINT not implemented.")
			#endif
			/*
			Info.Serialize(Arg1, false);
			if (Info.Browse(Parent))
			{
				Info.Serialize(Arg1, true);
			}
			*/
			break;
		}
		case ACTION_PLAY_SOUND:
		case ACTION_EXECUTE:
		{
			auto Select = new LFileSelect(Parent);

			Select->Parent(Parent);
			if (Type == ACTION_PLAY_SOUND)
			{
				Select->Type("Wave files", "*.wav");
			}
			else
			{
				Select->Type("Executables", "*.exe");
				Select->Type("All Files", LGI_ALL_FILES);
			}
			Select->Name(Arg1);

			Select->Open([this](auto s, auto ok)
			{
				if (ok)
				{
					Arg1 = s->Name();
					OnNotify(Btn, LNotifyValueChanged);
				}
				delete s;
			});
			break;
		}
		case ACTION_REPLY:
		{
			auto Dlg = new BrowseReply(App, Parent, Arg1);
			Dlg->DoModal([this, Dlg](auto dlg, auto id)
			{
				if (id)
				{
					Arg1 = Dlg->Arg;
					OnNotify(Btn, LNotifyValueChanged);
				}
				delete dlg;
			});
			break;
		}
		case ACTION_FORWARD:
		{
			auto Dlg = new BrowseForward(App, Parent, Arg1, true);
			Dlg->DoModal([this, Dlg](auto dlg, auto id)
			{
				if (id)
				{
					Arg1 = Dlg->Arg;
					OnNotify(Btn, LNotifyValueChanged);
				}
				delete dlg;
			});
			break;
		}
		case ACTION_BOUNCE:
		{
			auto Dlg = new BrowseForward(App, Parent, Arg1, false);
			Dlg->DoModal([this, Dlg](auto dlg, auto id)
			{
				if (id)
				{
					Arg1 = Dlg->Arg;
					OnNotify(Btn, LNotifyValueChanged);
				}
				delete dlg;
			});
			break;
		}
		case ACTION_SAVE_ATTACHMENTS:
		{
			auto Dlg = new BrowseSaveAttach(App, Parent, Arg1);
			Dlg->DoModal([this, Dlg](auto dlg, auto id)
			{
				if (id)
				{
					Arg1 = Dlg->Arg;
					OnNotify(Btn, LNotifyValueChanged);
				}
				delete dlg;
			});
			break;
		}
		case ACTION_CHANGE_CHARSET:
		{
			LSubMenu s;

			LArray<LCharset*> Cs;
			for (LCharset *c = LGetCsList(); c->Charset; c++)
			{
				Cs.Add(c);
			}
			Cs.Sort(CsCmp);
			for (unsigned i=0; i<Cs.Length(); i++)
			{
				unsigned n = 1;
				while (i + n < Cs.Length())
				{
					if (Cs[i + n]->Charset[0] != Cs[i]->Charset[0])
						break;
					n++;
				}
				if (n > 1)
				{
					char a[64];
					char *One = LSeekUtf8(Cs[i]->Charset, 1);
					ssize_t Len = One - Cs[i]->Charset;
					memcpy(a, Cs[i]->Charset, Len);
					strcpy_s(a + Len, sizeof(a) - Len, "...");

					auto Sub = s.AppendSub(a);
					if (Sub)
					{
						for (unsigned k=0; k<n; k++)
						{
							Sub->AppendItem(Cs[i+k]->Charset, i+k+1, Cs[i+k]->IsAvailable());
						}

						i += n - 1;
					}
				}
				else
				{
					s.AppendItem(Cs[i]->Charset, i+1, Cs[i]->IsAvailable());
				}
			}

			LMouse m;
			Parent->GetMouse(m, true);
			int Result = s.Float(Parent, m.x, m.y, true);
			if (Result)
			{
				Result--;
				if (Result >= 0 && Result < (int)Cs.Length())
				{
					Arg1 = Cs[Result]->Charset;
					OnNotify(Btn, LNotifyValueChanged);
				}
			}
			break;
		}
	}
}

///////////////////////////////////////////////////////////////
int Filter::MaxIndex = -1;

Filter::Filter(ScribeWnd *window, LDataI *object) : Thing(window, object)
{
	DefaultObject(object);
	d = new FilterPrivate;
	Ui = 0;
	Current = 0;

	IgnoreCheckEvents = true;
	ChkIncoming = new LListItemCheckBox(this, 2, GetIncoming()!=0);
	ChkOutgoing = new LListItemCheckBox(this, 3, GetOutgoing()!=0);
	ChkInternal = new LListItemCheckBox(this, 4, GetInternal()!=0);
	IgnoreCheckEvents = false;
}

Filter::~Filter()
{
	Empty();
	DeleteObj(d);
}

bool Filter::GetFormats(bool Export, LString::Array &MimeTypes)
{
	MimeTypes.Add(sTextXml);
	return MimeTypes.Length() > 0;
}

char *Filter::GetDropFileName()
{
	if (!DropFileName)
	{
		auto Nm = GetName();
		char f[256];
		if (Nm)
		{
			LUtf8Ptr o(f);
			for (LUtf8Ptr i(Nm); (uint32_t)i; i++)
			{
				if (!strchr(LGI_IllegalFileNameChars, (uint32_t)i))
					o.Add(i);
			}
			o.Add(0);
		}
		else strcpy_s(f, sizeof(f), "Filter");
		strcat_s(f, sizeof(f), ".xml");
		DropFileName.Reset(NewStr(f));
	}
	return DropFileName;
}

bool Filter::GetDropFiles(LString::Array &Files)
{
	char Tmp[MAX_PATH_LEN];
	LMakePath(Tmp, sizeof(Tmp), ScribeTempPath(), GetDropFileName());
	LAutoPtr<LFile> Out(new LFile);
	if (!Out->Open(Tmp, O_WRITE))
		return false;

	if (!Export(AutoCast(Out), sTextXml))
		return false;

	Files.Add(Tmp);
	return true;
}

Thing::IoProgress Filter::Import(IoProgressImplArgs)
{
	if (Stricmp(mimeType, sTextXml) &&
	    Stricmp(mimeType, sMimeXml))
	    IoProgressNotImpl();

	LXmlTree Tree;
	LXmlTag r;
	if (!Tree.Read(&r, stream))
		IoProgressError("Xml parse error.");

	if (!r.IsTag("Filter"))
		IoProgressError("No filter tag.");

	Empty();

	LXmlTag *t = r.GetChildTag("Name");
	if (t && t->GetContent())
		SetName(t->GetContent());
	SetIndex(r.GetAsInt("index"));

	if ((t = r.GetChildTag(ELEMENT_CONDITIONS)))
	{
		LStringPipe p;
		if (Tree.Write(t, &p))
		{
			LAutoString s(p.NewStr());
			ConditionsCache.Reset();
			SetConditionsXml(s);
		}

		if ((t = r.GetChildTag("Actions")))
		{
			LStringPipe p;
			if (Tree.Write(t, &p))
			{
				LAutoString s(p.NewStr());
				SetActionsXml(s);
			}
			else IoProgressError("Xml write failed.");
		}
	}
	
	IoProgressSuccess();
}

Thing::IoProgress Filter::Export(IoProgressImplArgs)
{
	if (Stricmp(mimeType, sMimeXml))
		IoProgressNotImpl();

	LXmlTag r("Filter");
	LXmlTag *t;
	if ((t = r.CreateTag("Name")))
		t->SetContent(GetName());
	r.SetAttr("index", GetIndex());

	LAutoPtr<LXmlTag> Cond = Parse(false);
	r.InsertTag(Cond.Release());
	LAutoPtr<LXmlTag> Act = Parse(true);
	r.InsertTag(Act.Release());
	
	LXmlTree tree;

	if (!tree.Write(&r, stream))
		IoProgressError("Failed to write xml.");

	IoProgressSuccess();
}

/// This filters a list of email. The email will have it's NewEmail state set to
/// one of three things:
/// If the email is not filtered then:
///		Mail::NewEmailBayes
/// If the email is filtered but not set to !NEW then:
///		Mail::NewEmailGrowl
/// If the email is filtered AND set to !NEW then:
///		Mail::NewMailNone
int Filter::ApplyFilters(LView *Parent, List<Filter> &Filters, List<Mail> &Email)
{
	int Status = 0;
	ScribeWnd *App = Filters.Length() > 0 ? Filters[0]->App : NULL;
	if (!App)
		return 0;

	bool Logging = App->LogFilterActivity();
	LStream *LogStream = NULL;
	if (Logging)
		LogStream = App->ShowScriptingConsole();

	LAutoPtr<LProgressDlg> Prog;
	if (Parent && Prog.Reset(new LProgressDlg(Parent)))
	{
		Prog->SetRange(Email.Length());
		Prog->SetDescription("Filtering...");
		Prog->SetType("email");
	}

	for (auto m: Email)
	{
		bool Act = false;
		bool Stop = false;

		m->IncRef();
		for (auto f: Filters)
		{
			if (Stop)
				break;

			if (f->Test(m, Stop, LogStream))
			{
				f->DoActions(m, Stop, LogStream);
				Act = true;
			}
		}
		
		if (Act)
		{
			Status++;
			if (m && m->NewEmail == Mail::NewEmailFilter)
			{
				m->NewEmail = Mail::NewEmailGrowl;
			}
		}
		else if (m && m->NewEmail == Mail::NewEmailFilter)
		{
			m->NewEmail = Mail::NewEmailBayes;
		}

		m->DecRef();
		m = NULL;

		if (Prog)
		{
			Prog->Value(Prog->Value() + 1);
			if (Prog->IsCancelled())
				break;
		}
	}

	return Status;
}

Filter *Filter::GetFilterAt(size_t Index)
{
	ScribeFolder *f = GetFolder();
	if (f)
	{
		for (auto t: f->Items)
		{
			Filter *f = t->IsFilter();
			if (f && f->GetIndex() == Index)
			{
				return f;
			}
		}
	}

	return 0;
}

enum TermType
{
	TermString,
	TermVariant
};

class ExpTerm
{
public:
	TermType Type;
	LVariant Value;

	ExpTerm(TermType t)
	{
		Type = t;
	}
};

bool Filter::Evaluate(char *str, LVariant &v)
{
	char *BufStr = NewStr(str);
	char *s = BufStr;
	if (s)
	{
		List<ExpTerm> Terms;
		const char *Ws = " \t\r\n";

		while (s && *s)
		{
			while (*s && strchr(Ws, *s)) s++;
			
			if (*s && *s == '\"')
			{
				char *Start = ++s;
				char *In = s;
				char *Out = s;
				while (*In)
				{
					if (In[0] == '\"')
					{
						if (In[1] == '\"')
						{
							// Quote
							*Out++ = '\"';
							In += 2;
						}
						else
						{
							// End of string
							In++;
							break;
						}
					}
					else
					{
						*Out++ = *In++;
					}
				}
				*Out++ = 0;
				s = In;
				
				ExpTerm *t;
				Terms.Insert(t = new ExpTerm(TermString));
				if (t)
				{
					t->Value = Start;
				}
			}
			else
			{
				char *Start = s;
				while (*s && !strchr(Ws, *s)) s++;
				while (*s && strchr(Ws, *s)) s++;
				char *Src = NewStr(Start, s-Start);
				if (Src)
				{
					ExpTerm *t;
					Terms.Insert(t = new ExpTerm(TermVariant));
					if (t)
					{
						if (GetValue(Start, t->Value))
						{
							switch (t->Value.Type)
							{
								default:
									break;
								case GV_BINARY:
								case GV_LIST:
								case GV_DOM:
								case GV_VOID_PTR:
								{
									v = t->Value;
									DeleteArray(BufStr);
									DeleteArray(Src);
									Terms.DeleteObjects();
									return true;
								}
							}
						}
						else
						{
							t->Type = TermString;
							t->Value = Src;
						}
					}

					DeleteArray(Src);
				}
			}
		}

		LStringPipe Out;
		auto It = Terms.begin();
		ExpTerm *t = *It;
		if (t)
		{
			if (Terms.Length() > 1)
			{
				// Collapse terms
				for (; t; t=*(++It))
				{
					char Buf[128];

					switch (t->Value.Type)
					{
						default:
							break;
						case GV_INT32:
						{
							sprintf_s(Buf, sizeof(Buf), "%i", t->Value.Value.Int);
							Out.Push(Buf);
							break;
						}
						case GV_INT64:
						{
							sprintf_s(Buf, sizeof(Buf), LPrintfInt64, t->Value.Value.Int64);
							Out.Push(Buf);
							break;
						}
						case GV_BOOL:
						{
							sprintf_s(Buf, sizeof(Buf), "%i", (int)t->Value.Value.Bool);
							Out.Push(Buf);
							break;
						}
						case GV_DOUBLE:
						{
							sprintf_s(Buf, sizeof(Buf), "%g", t->Value.Value.Dbl);
							Out.Push(Buf);
							break;
						}
						case GV_STRING:
						{
							if (t->Value.Str())
								Out.Push(t->Value.Str());
							break;
						}
						case GV_DATETIME:
						{
							t->Value.Value.Date->Get(Buf, sizeof(Buf));
							Out.Push(Buf);
							break;
						}
						case GV_BINARY:
						case GV_LIST:
						case GV_DOM:
						case GV_NULL:
						case GV_VOID_PTR:
						{
							break;
						}
					}
				}

				v.OwnStr(Out.NewStr());
			}
			else
			{
				v = t->Value;
			}
		}
		
		Terms.DeleteObjects();
	}

	DeleteArray(BufStr);
	return true;
}

bool Filter::SetVariant(const char *Name, LVariant &Value, const char *Array)
{
	ScribeDomType Field = StrToDom(Name);

	switch (Field)
	{
		case SdName:
			SetName(Value.Str());
			break;
		case SdConditionsXml:
		    ConditionsCache.Reset();
			SetConditionsXml(Value.Str());
			break;
		case SdActionsXml:
			SetActionsXml(Value.Str());
			break;
		case SdDateModified:
			return SetDateField(FIELD_DATE_MODIFIED, Value);
		default:
			return false;
	}

	return true;
}

bool Filter::CallMethod(const char *MethodName, LScriptArguments &Args)
{
	ScribeDomType Method = StrToDom(MethodName);
	switch (Method)
	{
		case SdAddCondition: // Type: (String Feild, String Op, String Value)
		{
			if (Args.Length() != 3)
			{
				LgiTrace("%s:%i - SdAddCondition: wrong number of parameters %i (expecting 3)\n", _FL, Args.Length());
				break;
			}
			
			const char *Field = Args[0]->Str();
			const char *Op = Args[1]->Str();
			const char *Value = Args[2]->Str();
			if (!Field || !Op || !Value)
			{
				LgiTrace("%s:%i - SdAddCondition: Missing values.\n", _FL);
				break;
			}

			LAutoPtr<LXmlTag> t = Parse(false);
			LXmlTag *Cond;
			if (!t || !t->IsTag(ELEMENT_CONDITIONS) || (Cond = t->Children[0]) == NULL)
			{
				LgiTrace("%s:%i - SdAddCondition: Failed to parse conditions.\n", _FL);
				break;
			}

			if (!Cond->IsTag(ELEMENT_AND) && !Cond->IsTag(ELEMENT_OR))
			{
				LgiTrace("%s:%i - SdAddCondition: Unexpected root operator.\n", _FL);
				break;
			}
			
			// Check that the condition doesn't already exist...
			for (auto c : Cond->Children)
			{
				if (c->IsTag(ELEMENT_CONDITION))
				{
					char *CFeild = c->GetAttr(ATTR_FIELD);
					char *COp = c->GetAttr(ATTR_OP);
					char *CVal = c->GetAttr(ATTR_VALUE);
					if (!Stricmp(Field, CFeild) &&
						!Stricmp(Op, COp) &&
						!Stricmp(Value, CVal))
					{
						return true;
					}
				}
			}
			
			LXmlTag *n = new LXmlTag(ELEMENT_CONDITION);
			if (!n)
			{
				LgiTrace("%s:%i - SdAddCondition: Alloc failed.\n", _FL);
				break;
			}

			n->SetAttr(ATTR_FIELD, Field);
			n->SetAttr(ATTR_OP, Op);
			n->SetAttr(ATTR_VALUE, Value);
			Cond->InsertTag(n);
			
			LXmlTree tree;
			LStringPipe p;
			if (!tree.Write(t, &p))
			{
				LgiTrace("%s:%i - SdAddCondition: Failed to write XML.\n", _FL);
				break;
			}
			
			LAutoString a(p.NewStr());
			ConditionsCache.Reset();
			SetConditionsXml(a);
			SetDirty(true);
			
			return true;
		}
		case SdAddAction: // Type: (String ActionName, String Value)
		{
			if (Args.Length() != 2)
			{
				LgiTrace("%s:%i - SdAddAction: wrong number of parameters %i (expecting 3)\n", _FL, Args.Length());
				break;
			}
			
			LString Action = Args[0]->CastString();
			if (!Action)
			{
				LgiTrace("%s:%i - SdAddAction: Missing action name.\n", _FL);
				break;
			}
			
			FilterAction a(this, GetObject()->GetStore());
			for (ActionName *an = ActionNames; an->Id; an++)
			{
				if (Action.Equals(an->Default))
				{
					a.Type = (FilterActionTypes) (an - ActionNames);
					a.Arg1 = Args[1]->CastString();
					AddAction(&a);
					return true;
				}
			}
			
			LgiTrace("%s:%i - SdAddAction: Action '%s' not found.\n", _FL, Action.Get());
			return false;
		}
		case SdStopFiltering: // Type: ()
		{
			if (d->Stop)
			{
				*d->Stop = true;
				return true;
			}
			else LgiTrace("%s:%i - No stop parameter to set.\n", _FL);
			return true;
		}
		case SdDoActions: // Type: (Mail Object)
		{
			if (Args.Length() == 1)
			{
				LDom *d = Args[0]->CastDom();
				Mail *m = dynamic_cast<Mail*>(d);
				if (m)
				{
					bool Stop = false;
					return DoActions(m, Stop);
				}
				else LgiTrace("%s:%i - DoActions: failed to cast arg1 to Mail object.\n", _FL);
			}
			else LgiTrace("%s:%i - DoActions is expecting 1 argument, not %i.\n", _FL, Args.Length());
			return true;
		}
		default:
			break;
	}

	return Thing::CallMethod(MethodName, Args);
}

bool Filter::GetVariant(const char *Var, LVariant &Value, const char *Array)
{
	ScribeDomType Fld = StrToDom(Var);
	switch (Fld)
	{
		case SdMail: // Type: Mail
		{
			if (!Current)
				return false;

			Value = *Current;
			break;
		}
		case SdScribe: // Type: ScribeWnd
		{
			Value = (LDom*)App;
			break;
		}
		case SdName: // Type: String
		{
			Value = GetName();
			break;
		}
		case SdTestConditions: // Type: Bool
		{
			if (Current && *Current)
			{
				bool s;
				bool &Stop = d->Stop ? *d->Stop : s;
				Value = EvaluateXml(*Current, Stop, d->Log);
			}
			else return false;
			break;
		}
		case SdType: // Type: Int32
		{
			Value = GetObject()->Type();
			break;
		}
		case SdConditionsXml: // Type: String
		{
			Value = GetConditionsXml();
			break;
		}
		case SdActionsXml: // Type: String
		{
			Value = GetActionsXml();
			break;
		}
		case SdIndex: // Type: Int32
		{
			Value = GetIndex();
			break;
		}
		case SdDateModified: // Type: DateTime
		{
			return GetDateField(FIELD_DATE_MODIFIED, Value);
		}
		default:
		{
			return false;
		}
	}

	return true;
}

Thing &Filter::operator =(Thing &t)
{
	Filter *f = t.IsFilter();
	if (f)
	{
		if (GetObject() && f->GetObject())
		{
			GetObject()->CopyProps(*f->GetObject());
		}
	}

	return *this;
}

int Filter::Compare(LListItem *Arg, ssize_t Field)
{
	Filter *a = dynamic_cast<Filter*>(Arg);
	if (a)
	{
		switch (Field)
		{
			case FIELD_FILTER_NAME:
				return a && GetName() ? _stricmp(GetName(), a->GetName()) : -1;
			case FIELD_FILTER_INDEX:
				return GetIndex() - a->GetIndex();
			case FIELD_FILTER_INCOMING:
				return GetIncoming() - a->GetIncoming();
			case FIELD_FILTER_OUTGOING:
				return GetOutgoing() - a->GetOutgoing();
		}
	}
	return 0;
}

int Filter::GetImage(int Flags)
{
	return ICON_FILTER;
}

void DisplayXml(LStream &s, char *xml)
{
	char *start = xml;
	char *e;
	for (e = xml; *e; e++)
	{
		if (*e == '<')
		{
			s.Write(start, e - start);
			s.Print("&lt;");
			start = e + 1;
		}
	}
	s.Write(start, e - start);
}

void DescribeCondition(LStream &s, LXmlTag *t)
{
	int i = 0;
	if (t->IsTag(ELEMENT_AND) || t->IsTag(ELEMENT_OR))
	{
		for (auto c: t->Children)
		{
			LStringPipe p;
			DescribeCondition(p, c);
			LAutoString a(p.NewStr());
			if (i)
				s.Print(" <span class=op>%s</span> ", t->GetTag());
			s.Print("(%s)", a.Get());
			i++;
		}
	}
	else
	{
		// Condition
		char *f = t->GetAttr(ATTR_FIELD);
		char *v = t->GetAttr(ATTR_VALUE);
		int Not = t->GetAsInt(ATTR_NOT) > 0;
		const char *o = t->GetAttr(ATTR_OP);
		if (o && IsDigit(*o))
			o = OpNames[atoi(o)];
		s.Print("%s<span class=var>%s</span> <span class=op>%s</span> \"%s\"", Not ? "!" : "", f, o, v);		
	}
}

LAutoString Filter::DescribeHtml()
{
	LStringPipe p(256);
	p.Print("<html><title><style>\n"
			".error { color: red; font-weight: bold; }\n"
			".op { color: blue; }\n"
			".var { color: #800; }\n"
			"pre { color: green; }\n"
			"</style></title>\n"
			"<body style='background: ThreeDFace;'>\n"
			"Name: %s<br>\n", GetName());
	p.Print("Incoming: %i<br>\n", GetIncoming());
	p.Print("Outgoing: %i<br>\n", GetOutgoing());
	
	if (GetConditionsXml())
	{
		LAutoPtr<LXmlTag> r = Parse(false);
		if (r && r->Children.Length())
		{
			p.Print("Conditions:<ul>\n");
			for (auto c: r->Children)
			{
				p.Print("<li>");
				DescribeCondition(p, c);
			}
			p.Print("</ul>\n");
		}
	}
	
	if (GetActionsXml())
	{
		LAutoPtr<LXmlTag> r = Parse(true);
		if (r && r->Children.Length())
		{
			p.Print("Actions:<ul>\n");
			for (auto c: r->Children)
			{
				if (!c->IsTag(ELEMENT_ACTION))
					continue;

				LAutoPtr<FilterAction> a(new FilterAction(this, GetObject()->GetStore()));
				if (a->Set(c))
				{
					p.Print("<li> ");
					a->DescribeHtml(this, p);
				}
			}
			p.Print("</ul>\n");
		}
	}
	
	if (GetScript())
	{
		LXmlTree t;
		LAutoString e(t.EncodeEntities(GetScript(), -1, "<>"));
		p.Print("Script:<pre>%s</pre>\n", e.Get());
	}
	
	p.Print("</body></html>\n");
	return LAutoString(p.NewStr());
}

void Filter::Empty()
{
	if (GetObject())
	{
	    ConditionsCache.Reset();
		SetConditionsXml(0);
		SetName(0);
	}
}

bool Filter::EvaluateTree(LXmlTag *n, Mail *m, bool &Stop, LStream *Log)
{
	bool Status = false;

	if (n && n->GetTag() && m && m->GetObject())
	{
		if (n->IsTag(ELEMENT_AND))
		{
			if (Log) Log->Print("\tAnd {\n");
			for (auto c: n->Children)
			{
				if (!EvaluateTree(c, m, Stop, Log))
				{
					if (Log) Log->Print("\t} (false)\n");
					return false;
				}
			}
			if (Log) Log->Print("\t} (true)\n");

			Status = true;
		}
		else if (n->IsTag(ELEMENT_OR))
		{
			if (Log) Log->Print("\tOr {\n");
			for (auto c: n->Children)
			{
				if (EvaluateTree(c, m, Stop, Log))
				{
					if (Log) Log->Print("\t} (true)\n");
					return true;
				}
			}
			if (Log) Log->Print("\t} (false)\n");
		}
		else if (n->IsTag(ELEMENT_CONDITION))
		{
			FilterCondition *c = new FilterCondition;
			if (c)
			{
				if (!c->Set(n))
				{
					LAssert(0);
				}
				else
				{
					Status = c->Test(this, m, Log);
					if (c->Not) Status = !Status;
					if (Log)
					{
						Log->Print("\tResult=%i (not=%i)\n", Status, c->Not);
					}
				}

				DeleteObj(c);
			}
		}
		else LAssert(0);
	}
	else LAssert(0);

	return Status;
}

bool FilterCondition::Set(LXmlTag *t)
{
	if (!t)
		return false;

	Source.Reset(NewStr(t->GetAttr(ATTR_FIELD)));
	Value.Reset(NewStr(t->GetAttr(ATTR_VALUE)));
	Not = t->GetAsInt(ATTR_NOT) > 0;

	char *o = t->GetAttr(ATTR_OP);
	if (o)
	{
		if (IsDigit(*o))
			Op = atoi(o);
		else
		{
			for (int i=0; OpNames[i]; i++)
			{
				if (!_stricmp(OpNames[i], o))
				{
					Op = i;
					break;
				}
			}
		}
	}

	return true;
}

bool Filter::EvaluateXml(Mail *m, bool &Stop, LStream *Log)
{
	bool Status = false;

	if (ValidStr(GetConditionsXml()))
	{
	    if (!ConditionsCache)
		    ConditionsCache = Parse(false);
		if (ConditionsCache &&
			ConditionsCache->Children.Length())
			Status = EvaluateTree(ConditionsCache->Children[0], m, Stop, Log);
	}

	return Status;
}

bool Filter::Test(Mail *m, bool &Stop, LStream *Log)
{
	bool Status = false;

	if (Log) Log->Print("Filter.Test '%s':\n", GetName());

	Current = &m;
	if (m && m->GetObject())
	{
		d->Stop = &Stop;
		d->Log = Log;

		if (ValidStr(GetScript()))
		{
			OnFilterScript(this, m, GetScript());
		}
		else if (ValidStr(GetConditionsXml()))
		{
			Status = EvaluateXml(m, Stop, Log);
		}
		else LAssert(0);

		d->Stop = 0;
		d->Log = 0;
	}
	Current = 0;

	return Status;
}

bool Filter::DoActions(Mail *&m, bool &Stop, LStream *Log)
{
	if (!App || !m)
		return false;

	Current = &m;

	LAutoPtr<LXmlTag> r = Parse(true);
	if (r)
	{
		LArray<FilterAction*> Act;

		for (auto c: r->Children)
		{
			if (c->IsTag(ELEMENT_ACTION))
			{
				auto a = new FilterAction(this, GetObject()->GetStore());
				if (a)
				{
					if (a->Set(c))
						Act.Add(a);
					else
						LAssert(!"Can't convert xml to action.");
				}
			}
		}
		

		for (unsigned i=0; i<Act.Length() && m->GetObject(); i++)
		{
			FilterAction *a = Act[i];
			a->Do(this, App, m, Log);
		}

		Act.DeleteObjects();
	}

	Current = 0;

	if (GetStopFiltering())
	{
		Stop = true;
	}
	return true;
}

ThingUi *Filter::DoUI(MailContainer *c)
{
	if (!Ui)
	{
		MaxIndex = MAX(MaxIndex, GetIndex());
		if (GetIndex() < 0)
		{
			SetIndex(++MaxIndex);
		}

		Ui = new FilterUi(this);
	}

	#if WINNATIVE
	if (Ui) SetForegroundWindow(Ui->Handle());
	#endif

	return Ui;
}

LAutoPtr<LXmlTag> Filter::Parse(bool Actions)
{
	LAutoPtr<LXmlTag> Ret;
	
	auto RawXml = Actions ? GetActionsXml() : GetConditionsXml();
	if (!RawXml)
		return Ret;

	LMemStream Xml(RawXml, strlen(RawXml), false);
	LXmlTree t;
	Ret.Reset(new LXmlTag);
	if (!t.Read(Ret, &Xml))
		Ret.Reset();

	return Ret;
}


void Filter::AddAction(FilterAction *Action)
{
	if (!Action)
		return;

	LAutoPtr<LXmlTag> a = Parse(true);
	if (!a)
		a.Reset(new LXmlTag("Actions"));

	LAutoPtr<LXmlTag> n(new LXmlTag(ELEMENT_ACTION));
	if (!Action->Get(n))
		return;

	a->InsertTag(n.Release());

	LXmlTree t;
	LStringPipe p;
	if (!t.Write(a, &p))
		return;

	LAutoString Xml(p.NewStr());
	SetActionsXml(Xml);
	SetDirty(true);
}


bool Filter::Save(ScribeFolder *Into)
{
	bool Status = false;

	if (!Into)
	{
		Into = GetFolder();
		if (!Into)
		{
			Into = App->GetFolder(FOLDER_FILTERS);
		}
	}
	
	if (Into)
	{
		SetParentFolder(Into);

		if (ChkIncoming)
			SetIncoming(ChkIncoming->Value()!=0);
		if (ChkOutgoing)
			SetOutgoing(ChkOutgoing->Value()!=0);
		if (ChkInternal)
			SetInternal(ChkInternal->Value()!=0);

		LDateTime Now;
		GetObject()->SetDate(FIELD_DATE_MODIFIED, &Now.SetNow());

		Store3Status s = Into->WriteThing(this);
		Status = s != Store3Error;
		if (Status)
			SetDirty(false);
	}

	return Status;
}

void Filter::OnPaint(ItemPaintCtx &Ctx)
{
	LListItem::OnPaint(Ctx);
}

void Filter::OnMouseClick(LMouse &m)
{
	LListItem::OnMouseClick(m);

	if (m.Double())
	{
		// open the UI for the Item
		DoUI();
	}
	else if (m.IsContextMenu())
	{
		// open the right click menu
		LSubMenu RClick;
		RClick.AppendItem(LLoadString(IDS_OPEN), IDM_OPEN);
		RClick.AppendItem(LLoadString(IDS_DELETE), IDM_DELETE);
		RClick.AppendItem(LLoadString(IDS_EXPORT), IDM_EXPORT);

		switch (RClick.Float(Parent, m))
		{
			case IDM_OPEN:
			{
				DoUI();
				break;
			}
			case IDM_DELETE:
			{
				LVariant ConfirmDelete;
				App->GetOptions()->GetValue(OPT_ConfirmDelete, ConfirmDelete);

				if (!ConfirmDelete.CastInt32() ||
					LgiMsg(GetList(), LLoadString(IDS_DELETE_ASK), AppName, MB_YESNO) == IDYES)
				{
					List<LListItem> Del;
					LList *ParentList = LListItem::Parent;
					if (ParentList && ParentList->GetSelection(Del))
					{
						for (auto m: Del)
						{
							auto f = dynamic_cast<Filter*>(m);
							if (f)
								f->OnDelete();
						}
					}
				}
				break;
			}
			case IDM_EXPORT:
			{
				ExportAll(GetList(), sTextXml, NULL);
				break;
			}
		}
	}
}

int *Filter::GetDefaultFields()
{
	static int Def[] = { FIELD_FILTER_NAME, 0, 0, 0 };
	return Def;
}

const char *Filter::GetFieldText(int Field)
{
	switch (Field)
	{
		case FIELD_FILTER_NAME:
			return GetName();
		case FIELD_FILTER_CONDITIONS_XML:
			return GetConditionsXml();
		case FIELD_FILTER_ACTIONS_XML:
			return GetActionsXml();
		case FIELD_FILTER_SCRIPT:
			return GetScript();
	}

	return NULL;
}

LImageList *Filter::GetIcons()
{
	LAssert(Ui);
	return Ui ? Ui->GetIcons() : NULL;
}

void Filter::OnColumnNotify(int Col, int64 Data)
{
	if (!IgnoreCheckEvents)
	{
		switch (Col)
		{
			case 2:
			case 3:
			case 4:
			{
				SetDirty(true);
				Update();
				break;
			}
		}
	}
}

const char *Filter::GetText(int i)
{
	int Field = 0;
	if (FieldArray.Length())
	{
		if (i >= 0 && i < (int)FieldArray.Length())
			Field = FieldArray[i];
	}
	else if (i >= 0 && i < CountOf(DefaultFilterFields))
	{
		Field = DefaultFilterFields[i];
	}

	switch (Field)
	{
		case FIELD_FILTER_NAME:
		{
			return GetName();
			break;
		}
		case FIELD_FILTER_INDEX:
		{
			static char i[16];
			sprintf_s(i, sizeof(i), "%i", GetIndex());
			return i;
			break;
		}
	}

	return 0;
}

int FilterIndexCmp(Filter **a, Filter **b)
{
	int ai = (*a)->GetIndex();
	int bi = (*b)->GetIndex();
	return ai - bi;
}

void Filter::Reindex(ScribeFolder *Folder)
{
	if (!Folder)
		return;

	LArray<Filter*> Zero;
	LArray<Filter*> Sort;
	for (auto t : Folder->Items)
	{
		Filter *f = t->IsFilter();
		if (f)
		{
			int Idx = f->GetIndex();
			if (Idx > 0)
			{
				Sort.Add(f);
			}
			else
			{
				Zero.Add(f);
			}
		}
	}

	Sort.Sort(FilterIndexCmp);

	LArray<Filter*> Map;

	for (unsigned n=0; n<Zero.Length(); n++)
		Sort.Add(Zero[n]);

	bool Changed = false;
	for (unsigned i=0; i<Sort.Length(); i++)
	{
		int Idx = Sort[i]->GetIndex();
		if (Idx != i + 1)
		{
			Sort[i]->SetIndex(i + 1);
			Sort[i]->SetDirty();
			Sort[i]->Update();
			Changed = true;
		}
	}

	if (Changed)
		Folder->ReSort();
}

//////////////////////////////////////////////////////////
enum ConditionOptions
{
	FIELD_ANYWHERE = FIELD_MAX
};

//////////////////////////////////////////////////////////
struct FilterUiPriv
{
	LDocView *Script = NULL;
	LScriptUi Commands;
	LTabView *Tab = NULL;
	LFilterView *Conditions = NULL;
	LList *Actions = NULL;
	LAutoPtr<LImageList> FilterIcons;
};

FilterUi::FilterUi(Filter *item) :
	ThingUi(item, "Filter")
{
	d = new FilterUiPriv;
	Item = item;
	if (!(Item && Item->App))
	{
		return;
	}

	LRect r(100, 100, 800, 600);
	SetPos(r);
	MoveSameScreen(item->App);
	d->FilterIcons = LFilterView::CreateIcons(this);

	// Create window
	#if WINNATIVE
	CreateClassW32("FilterUi", LoadIcon(LProcessInst(), MAKEINTRESOURCE(IDI_FILTER)));
	#endif

	if (Attach(0))
	{
		// Setup UI
		d->Commands.Toolbar = Item->App->LoadToolbar(this,
													Item->App->GetResourceFile(ResToolbarFile),
													Item->App->GetToolbarImgList());
		if (d->Commands.Toolbar)
		{
			d->Commands.Toolbar->Attach(this);
			d->Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_SAVE)), IDM_SAVE, TBT_PUSH, true, IMG_SAVE);
			d->Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_SAVE_CLOSE)), IDM_SAVE_CLOSE, TBT_PUSH, true, IMG_SAVE_AND_CLOSE);
			d->Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_DELETE)), IDM_DELETE, TBT_PUSH, true, IMG_TRASH);
			d->Commands.Toolbar->AppendButton(RemoveAmp(LLoadString(IDS_HELP)), IDM_HELP, TBT_PUSH, true, IMG_HELP);
			d->Commands.SetupCallbacks(GetItem()->App, this, GetItem(), LThingUiToolbar);
		}

		d->Tab = new LTabView(91, 0, 0, 1000, 1000, 0);
		if (d->Tab)
		{
			d->Tab->Attach(this);
			d->Tab->SetPourChildren(true);

			auto Filter = d->Tab->Append(LLoadString(IDS_FILTER));
			if (Filter)
			{
				#ifdef _DEBUG
				auto Status =
				#endif
				Filter->LoadFromResource(IDD_FILTER);
				LAssert(Status);
				Name(Filter->Name());
			}

			auto Cond = d->Tab->Append(LLoadString(IDS_CONDITIONS));
			if (Cond)
			{
				d->Conditions = new LFilterView(this, [this](auto View, auto Item, auto Menu, auto &r, auto GetList)
					{
						int Status = -1;
						if (!View)
							return Status;

						switch (Menu)
						{
							case FMENU_FIELD:
							{
								LSubMenu s;

								LString::Array Names;
								ItemFieldDef *FieldDefs = MailFieldDefs;
								for (ItemFieldDef *i = FieldDefs; i->DisplayText; i++)
								{
									const char *Trans = LLoadString(i->FieldId);
									auto idx = (i - FieldDefs) + 1;
									Names[idx] = DomToStr(i->Dom);
									s.AppendItem(Trans ? Trans : i->DisplayText, (int)idx, true);
								}

								s.AppendItem(LLoadString(IDS_ATTACHMENTS_DATA), FIELD_ATTACHMENTS_DATA, true);
								s.AppendItem(LLoadString(IDS_ATTACHMENTS_NAME), FIELD_ATTACHMENTS_NAME, true);
								s.AppendItem(LLoadString(IDS_MEMBER_OF_GROUP), FIELD_MEMBER_OF_GROUP, true);
								s.AppendItem(LLoadString(IDS_ANYWHERE), FIELD_ANYWHERE, true);

								LPoint p(r.x1, r.y2 + 1);
								View->PointToScreen(p);
								int Cmd = s.Float(View, p.x, p.y, true);
								switch (Cmd)
								{
									case FIELD_ATTACHMENTS_DATA:
										Item->SetField("mail.Attachments");
										break;
									case FIELD_ATTACHMENTS_NAME:
										Item->SetField("mail.AttachmentNames");
										break;
									case FIELD_MEMBER_OF_GROUP:
										Item->SetField("mail.From.Groups");
										break;
									case FIELD_ANYWHERE:
										Item->SetField("mail.*");
										break;
									default:
										if (Cmd > 0 && Cmd < Names.Length())
											Item->SetField(LString("mail.") + Names[Cmd]);
										break;
								}
								break;
							}
							case FMENU_OP:
							{
								if (GetList)
								{
									for (const char **o = GetOpNames(true); *o; o++)
									{
										GetList->Add(NewStr(*o));
									}
									Status = true;
								}
								/*
								else
								{
									auto s = new LSubMenu;
									if (s)
									{
										int n = 1;
										
										for (char **o = GetOpNames(true); *o; o++)
										{
											s->AppendItem(*o, n++, true);
										}

										LPoint p(r.x1, r.y2 + 1);
										View->PointToScreen(p);
										int Cmd = s->Float(View, p.x, p.y, true);

										if (Cmd > 0)
										{
											Item->SetOp(OpNames[Cmd - 1]);
										}

										DeleteObj(s);
									}
								}
								*/
								break;
							}
							case FMENU_VALUE:
							{
								break;
							}
						}

						return Status;
					});

				if (d->Conditions)
				{
					Cond->Append(d->Conditions);
					d->Conditions->SetPourLargest(true);
				}
			}

			auto Act = d->Tab->Append(LLoadString(IDS_ACTIONS));
			if (Act)
			{
				#ifdef _DEBUG
				auto Status =
				#endif
				Act->LoadFromResource(IDD_FILTER_ACTION);
				LAssert(Status);
				if (GetViewById(IDC_FILTER_ACTIONS, d->Actions))
					d->Actions->MultiSelect(false);
			}

			auto ScriptTab = d->Tab->Append("");
			if (ScriptTab &&
				ScriptTab->LoadFromResource(IDD_FILTER_SCRIPT))
			{
				if (GetViewById(IDC_SCRIPT, d->Script))
				{
					d->Script->SetWrapType(L_WRAP_NONE);
					d->Script->Sunken(true);
					d->Script->SetPourLargest(true);
				}
				else LAssert(0);
			}
		}

		// Show window
		Visible(true);

		if (Item)
		{
			LCombo *Cbo;
			if (GetViewById(IDC_ACTION, Cbo))
			{
				for (ActionName *o = ActionNames; o->Id; o++)
				{
					const char *s = LLoadString(o->Id, o->Default);
					Cbo->Insert(s);
				}
			}
		}

		OnLoad();
	}

	RegisterHook(this, LKeyEvents);
}

FilterUi::~FilterUi()
{
    if (Item)
	    Item->Ui = 0;
	DeleteObj(d);
}

LImageList *FilterUi::GetIcons()
{
	return d->FilterIcons;
}

bool FilterUi::OnViewKey(LView *v, LKey &k)
{
	THREAD_UNSAFE(false);

	if (k.CtrlCmd())
	{
		switch (k.c16)
		{
			case 's':
			case 'S':
			{
				if (k.Down())
					OnSave();
				return true;
			}
			case 'w':
			case 'W':
			{
				if (k.Down())
				{
					OnSave();
					Quit();
				}
				return true;
			}
		}
	}

	return false;
}

void FilterUi::ReorderAction(int offset)
{
	if (!d->Actions)
		return;

	List<FilterAction> Items;
	if (!d->Actions->GetSelection(Items) && Items[0])
		return;

	auto Idx = d->Actions->IndexOf(Items[0]) + offset;
	FilterAction *Last = NULL;
	for (auto a: Items)
	{
		d->Actions->Remove(a);
		d->Actions->Insert(a, Idx++);
		Last = a;
	}
	if (Last)
	{
		d->Actions->Focus(true);
		Last->Select(true);
	}
}

void FilterUi::DeleteAction()
{
	List<FilterAction> Items;
	if (d->Actions &&
		d->Actions->GetSelection(Items) &&
		Items.Length())
	{
		int Idx = d->Actions->IndexOf(Items[0]);
		Items.DeleteObjects();

		if (Idx >= (int)d->Actions->Length())
			Idx = (int)d->Actions->Length() - 1;
		d->Actions->Select(d->Actions->ItemAt(Idx));
		d->Actions->Focus(true);
	}
}

int FilterUi::OnNotify(LViewI *Col, LNotification n)
{
	THREAD_UNSAFE(0);

	int Reindex = 0;
	switch (Col->GetId())
	{
		case IDC_FILTER_ACTIONS:
		{
			switch (n.Type)
			{
				case LNotifyItemChange:
				{
					// Catch the user clicking on an action item icon
					switch (n.Int[0])
					{
						case IconMoveDown:
						{
							ReorderAction(1);
							break;
						}
						case IconMoveUp:
						{
							ReorderAction(-1);
							break;
						}
						case IconDelete:
						{
							DeleteAction();
							break;
						}
					}
					break;
				}
				default: break;
			}
			break;
		}
		case IDC_TYPE_CBO:
		case IDC_ARG_EDIT:
		{
			if (d->Actions)
			{
				List<FilterAction> Sel;
				if (d->Actions->GetSelection(Sel))
				{
					for (auto a: Sel)
						a->OnNotify(Col, n);
				}
			}
			break;
		}
		case IDC_BROWSE_ARG:
		{
			if (d->Actions)
			{
				List<FilterAction> Sel;
				if (d->Actions->GetSelection(Sel))
				{
					auto a = Sel[0];
					if (a)
						a->Browse(Item->App, this);
				}
			}
			break;			
		}
		case IDC_NEW_FILTER_ACTION:
		{
			if (d->Actions)
			{
				FilterAction *n = new FilterAction(Item, Item->GetObject()->GetStore());
				d->Actions->Insert(n);
				d->Actions->Select(n);
				d->Actions->Focus(true);
			}
			break;
		}
		case IDC_LAUNCH_HELP:
		{
			switch (d->Tab->Value())
			{
				case 0: // Name/Index
				default:
				{
					Item->App->LaunchHelp("filters.html");
					break;
				}
				case 1: // Conditions
				{
					Item->App->LaunchHelp("filters.html#cond");
					break;
				}
				case 2: // Actions
				{
					Item->App->LaunchHelp("filters.html#actions");
					break;
				}
				case 3: // Script
				{
					Item->App->LaunchHelp("filters.html#script");
					break;
				}
			}
			break;
		}
		case IDC_FILTER_UP:
		{
			Reindex = 1;
			break;
		}
		case IDC_FILTER_DOWN:
		{
			Reindex = -1;
			break;
		}
	}

	if (Reindex)
	{
		// Remove holes in the indexing
		ScribeFolder *Folder = Item->GetFolder();
		if (Folder)
		{
			Folder->ReSort();
		}

		// Swap entries
		auto i = GetCtrlValue(IDC_FILTER_INDEX);
		if (i >= 0)
		{
			auto f = Item->GetFilterAt(i - Reindex);
			if (f)
			{
				int n = f->GetIndex();
				f->SetIndex(Item->GetIndex());
				f->SetDirty();

				Item->SetIndex(n);
				Item->SetDirty();

				f->Save();
				Item->Save();

				SetCtrlValue(IDC_FILTER_INDEX, Item->GetIndex());
				Item->App->GetItemList()->ReSort();

				Item->Update();
				f->Update();
			}
		}
	}

	return 0;
}

LMessage::Result FilterUi::OnEvent(LMessage *Msg)
{
	THREAD_UNSAFE(0);

	switch (Msg->Msg())
	{
		#ifdef WIN32
		case WM_CLOSE:
		{
			Quit();
			return 0;
		}
		#endif
	}
	return LWindow::OnEvent(Msg);
}

void LoadTree(LFilterView *v, LXmlTag *t, LTreeNode *i)
{
	int idx = 0;
	for (auto c: t->Children)
	{
		if (c->GetTag())
		{
			LFilterItem *n = 0;
			bool Cond = false;

			if (c->IsTag(ELEMENT_AND))
				n = v->Create(LNODE_AND);
			else if (c->IsTag(ELEMENT_OR))
				n = v->Create(LNODE_OR);
			else if (c->IsTag(ELEMENT_CONDITION))
			{
				Cond = true;
				n = v->Create(LNODE_COND);
			}

			if (n)
			{
				if (Cond)
				{
					n->SetNot(c->GetAsInt(ATTR_NOT) > 0);
					n->SetField(c->GetAttr(ATTR_FIELD));
					n->SetValue(c->GetAttr(ATTR_VALUE));

					char *o = c->GetAttr(ATTR_OP);
					if (o)
					{
						if (IsDigit(*o))
							n->SetOp(atoi(o));
						else
						{
							for (int i=0; OpNames[i]; i++)
							{
								if (!_stricmp(OpNames[i], o))
								{
									n->SetOp(i);
									break;
								}
							}
						}
					}
				}
				
				i->Insert(n, idx++);

				LoadTree(v, c, n);
			}
		}
	}
}

void FilterUi::OnLoad()
{
	THREAD_UNSAFE();

	if (Item &&
		d->Actions &&
		d->Conditions)
	{
		auto r = Item->Parse(true);
		if (r)
		{
			for (auto c: r->Children)
			{
				auto a = new FilterAction(Item, Item->GetObject()->GetStore());
				if (a)
				{
					if (a->Set(c))
					{
						d->Actions->Insert(a);
					}
					else LAssert(!"Can't convert xml to action.");
				}
			}
		}

		auto FilterName = Item->GetName();
		SetCtrlName(IDC_NAME, FilterName);
		SetCtrlValue(IDC_FILTER_INDEX, Item->GetIndex());
		SetCtrlName(IDC_SCRIPT, Item->GetScript());
		SetCtrlValue(IDC_STOP_FILTERING, Item->GetStopFiltering());
		SetCtrlValue(IDC_INCOMING, Item->GetIncoming());
		SetCtrlValue(IDC_OUTGOING, Item->GetOutgoing());
		SetCtrlValue(IDC_INTERNAL_FILTERING, Item->GetInternal());

		auto Xml = Item->GetConditionsXml();
		if (Xml)
		{
			LAutoPtr<LXmlTag> x(new LXmlTag);
			if (x)
			{
				LMemStream p(Xml, strlen(Xml));
				LXmlTree t;
				if (t.Read(x, &p, 0))
				{
					d->Conditions->Empty();
					LoadTree(d->Conditions, x, d->Conditions->GetRootNode());
					if (!d->Conditions->GetRootNode()->GetChild())
					{
						d->Conditions->SetDefault();
					}
				}
			}
		}
		
		if (ValidStr(FilterName))
		{
			LString s;
			s.Printf("%s - %s", LLoadString(IDS_FILTER), FilterName);
			Name(s);
		}
	}
}

void SaveTree(LXmlTag *t, LTreeNode *i)
{
	for (LTreeNode *c = i->GetChild(); c; c = c->GetNext())
	{
		auto fi = dynamic_cast<LFilterItem*>(c);
		if (fi)
		{
			const char *Tag = 0;
			bool Cond = false;
			switch (fi->GetNode())
			{
				default: break;
				case LNODE_AND:
					Tag = ELEMENT_AND; break;
				case LNODE_OR:
					Tag = ELEMENT_OR; break;
				case LNODE_COND:
					Cond = true; Tag = ELEMENT_CONDITION; break;
			}

			if (Tag)
			{
				LXmlTag *n = new LXmlTag(Tag);
				if (n)
				{
					if (Cond)
					{
						n->SetAttr(ATTR_NOT, fi->GetNot());
						n->SetAttr(ATTR_FIELD, fi->GetField());
						n->SetAttr(ATTR_OP, fi->GetOp());
						n->SetAttr(ATTR_VALUE, fi->GetValue());
					}

					t->InsertTag(n);

					SaveTree(n, fi);
				}
			}
		}
	}
}

void FilterUi::OnSave()
{
	THREAD_UNSAFE();

	if (Item &&
		d->Actions)
	{
		Item->SetName(GetCtrlName(IDC_NAME));
		Item->SetScript(GetCtrlName(IDC_SCRIPT));
		Item->SetStopFiltering(GetCtrlValue(IDC_STOP_FILTERING)!=0);

		Item->SetIncoming(GetCtrlValue(IDC_INCOMING)!=0);
		Item->SetOutgoing(GetCtrlValue(IDC_OUTGOING)!=0);
		Item->ChkIncoming->Value(GetCtrlValue(IDC_INCOMING));
		Item->ChkOutgoing->Value(GetCtrlValue(IDC_OUTGOING));
		Item->ChkInternal->Value(GetCtrlValue(IDC_INTERNAL_FILTERING));

		if (d->Conditions)
		{
			LXmlTag *x = new LXmlTag(ELEMENT_CONDITIONS);
			if (x)
			{
				SaveTree(x, d->Conditions->GetRootNode());

				LXmlTree t;
				LStringPipe p;
				if (t.Write(x, &p))
				{
				    LAutoString a(p.NewStr());
				    Item->ConditionsCache.Reset();
					Item->SetConditionsXml(a);
				}

				DeleteObj(x);
			}
		}

		LXmlTag x("Actions");
		List<FilterAction> Act;
		d->Actions->GetAll(Act);
		for (size_t i=0; i<Act.Length(); i++)
		{
			FilterAction *a = Act[i];
			LAutoPtr<LXmlTag> c(new LXmlTag("Action"));
			if (a->Get(c))
			{
				x.InsertTag(c.Release());
			}
		}

		LXmlTree t;
		LStringPipe p;
		t.Write(&x, &p);
		LAutoString s(p.NewStr());
		Item->SetActionsXml(s);

		if (Item->Save())
		{
			Item->Reindex(Item->GetFolder());
		}
	}
}

int FilterUi::OnCommand(int Cmd, int Event, OsView Window)
{
	THREAD_UNSAFE(0);

	switch (Cmd)
	{
		case IDM_SAVE:
		{
			OnSave();
			break;
		}
		case IDM_SAVE_CLOSE:
		{
			OnSave();
			// fall thru
		}
		case IDM_CLOSE:
		{
			Quit();
			break;
		}
		case IDM_DELETE:
		{
		    Item->Ui = 0;
		    Item->OnDelete();
		    Item = 0;
		    Quit();
		    break;
		}
		case IDM_HELP:
		{
			App->LaunchHelp("filters.html");
			break;
		}
		default:
		{
			d->Commands.ExecuteCallbacks(GetItem()->App, this, GetItem(), Cmd);
			break;
		}
	}
	return 0;
}


