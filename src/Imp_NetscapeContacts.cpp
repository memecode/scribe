#include "Scribe.h"
#include "lgi/common/FileSelect.h"

typedef LXmlTag ImpRecord;
class ImpRecordSet : public List<ImpRecord> {
public:
	LString::Array fields;

	ImpRecordSet() {}
	~ImpRecordSet()
	{
		for (auto r: *this)
		{
			DeleteObj(r);
		}
	}
};

int ReadCsv(const char *Name, ImpRecordSet &Rs, bool HasHeadings = true)
{
	int Status = 0;
	LFile f;
	if (f.Open(Name, O_READ))
	{
		char Buf[1024];
		bool Done = false;

		if (HasHeadings)
		{
			// read field headings
			while (!Done)
			{
				char *c = Buf;
				while (	!f.Eof() &&
						f.Read(c, 1) == 1 &&
						!strchr(",\r\n", *c))
				{
					c++;
				}

				Done = strchr("\r\n", *c) != 0;
				if (Done)
					f >> *c;
				*c++ = 0;
				Rs.fields.New() = LString(Buf).Strip("\"' \t\r\n");
			}
		}

		// read records
		while (!f.Eof())
		{
			ImpRecord *r = new ImpRecord;
			if (r)
			{
				Done = false;
				auto It = Rs.fields.begin();
				char *Field = *It;
				while (!Done)
				{
					char *c = Buf;
					for (	;
							f.Read(c, 1) == 1 &&
							!f.Eof() &&						
							!strchr(",\r\n", *c);
							c++)
					{
					}
					
					Done = strchr("\r\n", *c) != 0;
					if (Done) f >> *c;
					*c++ = 0;
					
					auto Str = LString(Buf).Strip("\"'");
					if (Str && Field)
					{
						r->SetAttr(Field, Str);
					}
					DeleteArray(Str);
					Field = *(++It);
				}

				Rs.Insert(r);
				Status++;
			}
		}
	}

	return Status;
}

static void CopyField(Contact *d, const char *Dest, LXmlTag *s, const char *Src)
{
	if (d && Dest && s && Src)
	{
		char *Str;
		if ((Str = s->GetAttr(Src)))
			d->Set(Dest, Str);
	}
}

void Import_NetscapeContacts(ScribeWnd *Parent)
{
	if (!Parent)
		return;

	auto Select = new LFileSelect(Parent);
	Select->Type("Netscape Contacts", "*.csv");
	Select->Open([Parent](auto dlg, auto status)
	{
		if (!status)
			return;

		LString Name = dlg->Name();
		auto Dlg = new FolderDlg(Parent, Parent, MAGIC_CONTACT);
		Dlg->DoModal([Parent, Dlg, Name](auto dlg, auto id)
		{
			if (!id)
				return;

			auto Contacts = Parent->GetFolder(Dlg->Get());
			if (!Contacts)
				return;

			ImpRecordSet Rs;

			// Pre populated the field names
			Rs.fields.Add("DisplayName");
			Rs.fields.Add("Surname");
			Rs.fields.Add("First");
			Rs.fields.Add("Notes");
			Rs.fields.Add("City");
			Rs.fields.Add("State");
			Rs.fields.Add("Email");
			Rs.fields.Add("Title");
			Rs.fields.Add("Unknown");
			Rs.fields.Add("Address");
			Rs.fields.Add("PostCode");
			Rs.fields.Add("Country");
			Rs.fields.Add("PhoneWork");
			Rs.fields.Add("Fax");
			Rs.fields.Add("PhoneHome");
			Rs.fields.Add("Organization");
			Rs.fields.Add("Nick");
			Rs.fields.Add("Mobile");
			Rs.fields.Add("Pager");
			Rs.fields.Add("Unknown2");

			// read file
			if (ReadCsv(Name, Rs, false) <= 0)
				return;

			for (auto r: Rs)
			{
				Contact *c = new Contact(Parent);
				if (c)
				{
					c->App = Parent;

					CopyField(c, OPT_First, r, "First");
					CopyField(c, OPT_Last, r, "Surname");
					CopyField(c, OPT_Email, r, "Email");
					CopyField(c, OPT_HomeStreet, r, "Address");
					CopyField(c, OPT_HomeSuburb, r, "City");
					CopyField(c, OPT_HomeState, r, "State");
					CopyField(c, OPT_HomePostcode, r, "PostCode");
					CopyField(c, OPT_HomeCountry, r, "Country");
					CopyField(c, OPT_WorkPhone, r, "PhoneWork");
					CopyField(c, OPT_HomePhone, r, "PhoneHome");
					CopyField(c, OPT_HomeFax, r, "Fax");
					CopyField(c, OPT_HomeMobile, r, "Mobile");
					// CopyField(c, OPT_WebPage, r, "Web Page");
					CopyField(c, OPT_Note, r, "Notes");
					CopyField(c, OPT_Nick, r, "Nick");

					c->Save(Contacts);
				}
			}
		});
	});
}
