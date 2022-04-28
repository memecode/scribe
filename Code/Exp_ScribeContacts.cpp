#include "Scribe.h"

void Export_ScribeContacts(ScribeWnd *Parent)
{
	if (Parent)
	{
		List<Contact> Contacts;
		Parent->GetContacts(Contacts);
		if (Contacts.GetItems() > 0)
		{
			LFileSelect Select;

			Select.Parent(Parent);
			Select.Type("Contacts files", "*.contacts");

			if (Select.Save())
			{
				LFile f;
				if (f.Open(Select.Name(), O_WRITE))
				{
					for (Contact *c = Contacts.First(); c; c = Contacts.Next())
					{
						c->Serialize(f, TRUE);
					}
				}
				else
				{
					Error(LLoadString(IDS_COULDNT_OPEN), Select.Name());
				}
			}
		}
	}
}
