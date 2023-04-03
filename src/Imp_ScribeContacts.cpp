#include "Scribe.h"

void Import_ScribeContacts(ScribeWnd *Parent)
{
	if (Parent)
	{
		LFileSelect Select;

		Select.Parent(Parent);
		Select.Type("Contacts files", "*.contacts");

		if (Select.Open())
		{
			LFile f;

			if (f.Open(Select.Name(), O_READ))
			{
				ScribeFolder *Contacts = Parent->GetFolder(FOLDER_CONTACTS);
				if (NOT Contacts AND Parent->GetMailbox())
				{
					// create the folder if it doesn't exist
					Contacts = Parent->GetMailbox()->CreateSubDirectory("Contacts", MAGIC_CONTACT);
				}

				if (Contacts AND Contacts->Store)
				{
					bool Done = FALSE;
					Contact *c = new Contact;
					while (c AND NOT Done)
					{
						if (c->Serialize(f, FALSE))
						{
							StorageItem *New = Contacts->Store->CreateSub(c);
							if (New)
							{
								New->Object = c;
								c->Store = New;
							}
							else
							{
								Done = TRUE;
							}
						}
						else
						{
							Done = TRUE;
						}

						c = new Contact;
					}

					DeleteObj(c);
				}
			}
			else
			{
				Error(LLoadString(IDS_COULDNT_OPEN), Select.Name());
			}
		}
	}
}
