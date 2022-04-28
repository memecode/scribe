#include "Store3Mail2.h"

#define COMBINE_OP_AND				0
#define COMBINE_OP_OR				1

enum FilterInFlags
{
	FilterIncoming = 0x1,
	FilterInternal = 0x2,
};

static const char *OpNames[] =
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

//////////////////////////////////////////////////////////////////////
FilterData::FilterData(GMail2Store *s) : ThingData(s)
{
	Flags = 0;
	Index = 0;
	Name = 0;
	Script = 0;
	StopFiltering = true;
	ConditionsXml = 0;
	FilterFlags = FilterIncoming | FilterInternal;
	Outgoing = false;
}

FilterData::~FilterData()
{
	DeleteArray(Name);
	DeleteArray(ConditionsXml);
	DeleteArray(Script);
	Actions.DeleteObjects();
}

LDataI &FilterData::operator =(LDataI &p)
{
	SetStr(FIELD_FILTER_NAME, p.GetStr(FIELD_FILTER_NAME));
	SetStr(FIELD_FILTER_CONDITIONS_XML, p.GetStr(FIELD_FILTER_CONDITIONS_XML));
	SetStr(FIELD_FILTER_SCRIPT, p.GetStr(FIELD_FILTER_SCRIPT));

	SetInt(FIELD_FLAGS, p.GetInt(FIELD_FLAGS));
	SetInt(FIELD_STOP_FILTERING, p.GetInt(FIELD_STOP_FILTERING));
	SetInt(FIELD_FILTER_INDEX, p.GetInt(FIELD_FILTER_INDEX));
	SetInt(FIELD_FILTER_INCOMING, p.GetInt(FIELD_FILTER_INCOMING));
	SetInt(FIELD_FILTER_OUTGOING, p.GetInt(FIELD_FILTER_OUTGOING));
	SetInt(FIELD_FILTER_INTERNAL, p.GetInt(FIELD_FILTER_INTERNAL));
	
	return *this;
}

void FilterData::Load()
{
	if (IsLoaded)
		return;

	if (Store)
	{
		IsLoaded = true;
		LAutoPtr<LFile> f(Store->GotoObject(_FL));
		if (f)
		{
			Serialize(*f, false);
		}
	}
}

char *FilterData::GetStr(int id)
{
	Load();

	switch (id)
	{
		case FIELD_FILTER_NAME:
			return Name;
		case FIELD_FILTER_CONDITIONS_XML:
			return ConditionsXml;
		case FIELD_FILTER_SCRIPT:
			return Script;
		case FIELD_FILTER_ACTIONS_XML:
		{
			// Convert actions into xml
			LStringPipe p;
			LXmlTag r("Actions");
			for (unsigned i=0; i<Actions.Length(); i++)
			{
				LAutoPtr<LXmlTag> t(new LXmlTag("Action"));
				if (Actions.a[i]->Get(t))
				{
					r.InsertTag(t.Release());
				}
			}

			LXmlTree t;
			t.Write(&r, &p);
			ActionXml.Reset(p.NewStr());
			return ActionXml;
			break;
		}			
	}

	LAssert(0);
	return 0;
}

bool FilterData::SetStr(int id, const char *str)
{
	Load();

	switch (id)
	{
		case FIELD_FILTER_NAME:
			_Str(Name);
		case FIELD_FILTER_CONDITIONS_XML:
			_Str(ConditionsXml);
		case FIELD_FILTER_SCRIPT:
			_Str(Script);
		case FIELD_FILTER_ACTIONS_XML:
		{
			// Convert XML into actions
			Actions.DeleteObjects();

			LMemStream m((char*)str, strlen(str), false);
			LXmlTree t;
			LXmlTag r;
			if (t.Read(&r, &m))
			{
				for (LXmlTag *c = r.Children.First(); c; c = r.Children.Next())
				{
					LAutoPtr<FilterAction> a(new FilterAction(Kit));
					if (a->Set(c))
					{
						Actions.Insert(a.Release());
					}
				}
				return true;
			}
			else
			{
				LgiTrace("%s:%i - Action parsing failed.\n", _FL);
				return false;
			}
			break;
		}
	}

	LAssert(0);
	return 0;
}

int64 FilterData::GetInt(int id)
{
	Load();

	switch (id)
	{
		case FIELD_IS_IMAP:
			return false;
		case FIELD_FLAGS:
			return Flags;
		case FIELD_STOP_FILTERING:
			return StopFiltering;
		case FIELD_FILTER_INDEX:
			return Index;
		case FIELD_FILTER_INCOMING:
			return (FilterFlags & FilterIncoming) != 0;
		case FIELD_FILTER_INTERNAL:
			return (FilterFlags & FilterInternal) != 0;
		case FIELD_FILTER_OUTGOING:
			return Outgoing;
	}

	LAssert(0);
	return -1;
}

bool FilterData::SetInt(int id, int64 i)
{
	Load();

	switch (id)
	{
		case FIELD_FLAGS:
			Flags = (int)i;
			return true;
		case FIELD_STOP_FILTERING:
			StopFiltering = (uint8)i;
			return true;
		case FIELD_FILTER_INDEX:
			Index = (int)i;
			return true;
		case FIELD_FILTER_INCOMING:
			if (i)
				FilterFlags |= FilterIncoming;
			else
				FilterFlags &= ~FilterIncoming;
			return true;
		case FIELD_FILTER_INTERNAL:
			if (i)
				FilterFlags |= FilterInternal;
			else
				FilterFlags &= ~FilterInternal;
			return true;
		case FIELD_FILTER_OUTGOING:
			Outgoing = (uint8)i;
			return true;
	}

	LAssert(0);
	return 0;
}

GDataIt FilterData::GetList(int id)
{
	Load();

	switch (id)
	{
		case FIELD_ACTION:
			return &Actions;
	}

	LAssert(0);
	return 0;
}

int FilterData::Type()
{
	return MAGIC_FILTER;
}

int Action_Sizeof(FilterAction *a)
{
	int Size =	
		sizeof(ulong) +				// magic: MAGIC_ACTION
		sizeof(ulong) +				// number of fields
		SizeIntField(a->Type) +
		SizeStrField(a->Arg1);

	return Size;
}

bool Action_Serialize(FilterAction *a, LFile &f, bool Write)
{
	ulong Magic = MAGIC_ACTION;

	if (Write)
	{
		f << Magic;
		f << ((ulong) 2);
		WriteIntField(FIELD_ACT_TYPE, a->Type);
		WriteStrField(FIELD_ACT_ARG, a->Arg1);
	}
	else
	{
		f >> Magic;
		if (Magic == MAGIC_ACTION)
		{
			ulong Fields = 0;
			f >> Fields;

			for (unsigned i=0; i<Fields; i++)
			{
				short FieldId = 0;
				ulong FieldSize = 0;

				f >> FieldId;
				switch (FieldId)
				{
					ReadIntField(FIELD_ACT_TYPE, (int&)a->Type);
					ReadAutoStrField(FIELD_ACT_ARG, a->Arg1);
					default:
					{
						// skip over unknown chunk
						int Size;
						f >> Size;
						f.Seek(Size, SEEK_CUR);
					}
				}
			}
		}
		else return false;
	}

	return f.GetStatus();
}

int FilterData::Sizeof()
{
	uint8 v;

	int Size =	
		sizeof(ulong) +				// magic
		sizeof(ulong) +				// number of fields
		SizeIntField(Flags) +
		SizeIntField(Index) + 
		SizeStrField(Name) + 
		SizeIntField(v) +			// Incoming
		SizeIntField(v) +			// Outgoing
		SizeIntField(StopFiltering) +
		SizeStrField(ConditionsXml) +
		(Script ? SizeStrField(Script) : 0);

	for (unsigned i=0; i<Actions.Length(); i++)
	{			
		FilterAction *fa = Actions.a[i];
		Size += sizeof(short) +				// field type
				sizeof(ulong) +				// field size
				Action_Sizeof(fa);
	}

	return Size;
}

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
		case 10: /* flags */ break;						\
		case 11: Macro("Html"); break;						\
		case 12: Macro("Label"); break;						\
		case 13: Macro("From.Contact"); break;				\
		case 14: Macro("Attachments"); break;				\
		case 15: Macro("AttachmentNames"); break;			\
		case 16: Macro("From.Groups"); break;				\
		case 17: Macro("*"); break;							\
	}

class FilterDataCondition
{
public:
	// Data
	char *Source; // Data Source (used to be "int Field")
	char Op;
	uint8 Not;
	char *Value; // Constant

	FilterDataCondition()
	{
		Op = 0;
		Not = false;
		Value = 0;
		Source = 0;
	}

	~FilterDataCondition()
	{
		DeleteArray(Value);
		DeleteArray(Source);
	}

	int Sizeof()
	{
		int Size =	
			sizeof(ulong) +				// magic: MAGIC_CONDITION
			sizeof(ulong) +				// number of fields
			SizeStrField(Source) +
			SizeIntField(Op) +
			SizeIntField(Not) +
			SizeStrField(Value);

		return Size;
	}

	bool Serialize(LFile &f, bool Write)
	{
		ulong Magic = MAGIC_CONDITION;

		if (Write)
		{
			f << Magic;
			f << ((ulong) 5);
			WriteStrField(FIELD_COND_SOURCE, Source);
			WriteIntField(FIELD_COND_OPERATOR, Op);
			WriteIntField(FIELD_COND_NOT, Not);
			WriteStrField(FIELD_COND_VALUE, Value);
		}
		else
		{
			f >> Magic;
			if (Magic == MAGIC_CONDITION)
			{
				ulong Fields = 0;
				f >> Fields;

				int OldField = -1;

				for (unsigned i=0; i<Fields; i++)
				{
					short FieldId = 0;
					ulong FieldSize = 0;

					f >> FieldId;
					switch (FieldId)
					{
						ReadIntField(FIELD_COND_FIELD, OldField);
						ReadIntField(FIELD_COND_OPERATOR, Op);
						ReadStrField(FIELD_COND_VALUE, Value);
						ReadIntField(FIELD_COND_NOT, Not);
						ReadStrField(FIELD_COND_SOURCE, Source);
						default:
						{
							// skip over unknown chunk
							int Size;
							f >> Size;
							f.Seek(Size, SEEK_CUR);
						}
					}
				}

				if (OldField >= 0)
				{
					// Convert field into source
					#define Cf(To) Source = NewStr("mail." To);
					ForCondField(Cf, OldField);
				}
			}
			else return false;
		}

		return f.GetStatus();
	}
};

bool FilterData::Serialize(LFile &f, bool Write)
{
	bool Status = false;
	ulong Magic = Type();

	if (Write)
	{
		f << Magic;
		f << ((ulong) 8 + Actions.Length() + (Script ? 1 : 0) );
		WriteIntField(FIELD_FLAGS, Flags);
		WriteIntField(FIELD_STOP_FILTERING, StopFiltering);
		// WriteIntField(FIELD_COMBINE_OP, CombineOp);
		WriteIntField(FIELD_FILTER_INDEX, Index);
		WriteStrField(FIELD_FILTER_NAME, Name);
		WriteStrField(FIELD_FILTER_CONDITIONS_XML, ConditionsXml);
		WriteIntField(FIELD_FILTER_INCOMING, FilterFlags);
		WriteIntField(FIELD_FILTER_OUTGOING, Outgoing);
		
		if (Script)
		{
			WriteStrField(FIELD_FILTER_SCRIPT, Script);
		}

		for (unsigned i=0; i<Actions.Length(); i++)
		{			
			FilterAction *fa = Actions.a[i];
			f << ((short)FIELD_ACTION);
			f << ((ulong)Action_Sizeof(fa));
			Action_Serialize(fa, f, Write);
		}

		Status = true;
	}
	else
	{
		f >> Magic;
		if (Magic == MAGIC_FILTER)
		{
			ulong Fields = 0;
			f >> Fields;

			uint8 In = true;
			uint8 Out = false;
			LArray<FilterDataCondition*> OldConditions;
			int CombineOp = 0;

			for (unsigned i=0; i<Fields && !Store->EndOfObj(f); i++)
			{
				short FieldId = 0;
				ulong FieldSize = 0;

				f >> FieldId;
				switch (FieldId)
				{
					ReadIntField(FIELD_FLAGS, Flags);
					ReadIntField(FIELD_COMBINE_OP, CombineOp);
					ReadStrField(FIELD_FILTER_NAME, Name);
					ReadIntField(FIELD_FILTER_INDEX, Index);
					ReadStrField(FIELD_FILTER_SCRIPT, Script);
					ReadIntField(FIELD_STOP_FILTERING, StopFiltering);
					ReadIntField(FIELD_FILTER_INCOMING, In);
					ReadIntField(FIELD_FILTER_OUTGOING, Out);
					ReadStrField(FIELD_FILTER_CONDITIONS_XML, ConditionsXml);

					case FIELD_CONDITION:
					{
						f >> FieldSize;
						FilterDataCondition *c = new FilterDataCondition;
						if (c)
						{
							c->Serialize(f, Write);
							OldConditions.Add(c);
						}
						break;
					}
					case FIELD_ACTION:
					{
						f >> FieldSize;
						LAutoPtr<FilterAction> a(new FilterAction(GetStore()));
						if (a && Action_Serialize(a, f, Write))
						{
							Actions.Insert(a.Release());
						}
						break;
					}
					default:
					{
						// skip over unknown chunk
						int Size;
						f >> Size;
						f.Seek(Size, SEEK_CUR);
					}
				}
			}

			// Do we need to upgrade old filter conditions to new XML ones?
			if (OldConditions.Length() > 0)
			{
				// Convert to XML
				LXmlTag *r = new LXmlTag("Conditions");
				if (r)
				{
					LXmlTag *o = new LXmlTag((char*)(CombineOp == COMBINE_OP_AND ? "And" : "Or"));
					if (o)
					{
						r->InsertTag(o);

						for (unsigned i=0; i<OldConditions.Length(); i++)
						{
							FilterDataCondition *c = OldConditions[i];

							LXmlTag *n = new LXmlTag("Condition");
							if (n)
							{
								n->SetAttr("Not", (int)c->Not);
								n->SetAttr("Field", c->Source);
								n->SetAttr("Op", OpNames[c->Op]);
								n->SetAttr("Value", c->Value);

								o->InsertTag(n);
							}
						}

						LStringPipe p;
						LXmlTree t;
						if (t.Write(r, &p))
						{
							DeleteArray(ConditionsXml);
							ConditionsXml = p.NewStr();
						}
					}
				}

				OldConditions.DeleteObjects();
			}

			Status = true;
		}
	}

	return Status;
}

