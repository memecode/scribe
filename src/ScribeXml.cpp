/*
**	FILE:			ScribeXml.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			26/6/99
**	DESCRIPTION:	Scribe Xml methods
**
**	Copyright (C) 1999, Matthew Allen
**		fret@memecode.com
*/

#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#include "Scribe.h"

//////////////////////////////////////////////////////////////////////////////////
XmlTag::XmlTag(char *&s, int Len)
{
	// skip whitespace
	while (strchr(" \r\t\n", *s)) s++;

	char *Start = s;
	while (*s AND *s != '=' AND Len-- > 0)
	{
		s++;
	}
	if (*s == '=')
	{
		int SLen = (int) s - (int) Start;
		Name = new char[SLen+1];
		if (Name)
		{
			memcpy(Name, Start, SLen);
			Name[SLen] = 0;
		}

		s++;
		Len--;
		
		bool Quoted = IsQuote(*s);
		if (Quoted)
		{
			s++;
			Start = s;
			while (	*s AND
					(NOT IsQuote(*s)) AND
					(*s != '>') AND
					(Len-- > 0))
			{
				s++;
			}
		}
		else
		{
			Start = s;
			while (	(*s) AND
					(NOT strchr(" \r\n\t", *s)) AND
					(*s != '>') AND
					(Len-- > 0))
			{
				s++;
			}
		}

		SLen = (int) s - (int) Start;
		if (IsQuote(*s)) s++;
		Value = new char[SLen+1];
		if (Value)
		{
			memcpy(Value, Start, SLen);
			Value[SLen] = 0;
		}
	}
}

XmlTag::~XmlTag()
{
	DeleteArray(Name);
	DeleteArray(Value);
}

////////////////////////////////////////////////////////////////////////////////////////
Xml::Xml(Mail *mailitem, ObjProperties *options)
{
	MailItem = mailitem;
	Options = options;
}

char *Xml::ReadTextFile(char *FileName)
{
	if (FileName)
	{
		LFile F;
		if (F.Open(FileName, O_READ))
		{
			int Len = F.GetSize();
			char *File = new char[Len+1];
			if (File)
			{
				F.Read(File, Len);
				File[Len] = 0;
				return File;
			}
		}
	}

	return 0;
}

char *Xml::GetValue(char *Name)
{
	for (XmlTag *v = Values.First(); v; v = Values.Next())
	{
		if (stricmp(v->Name, Name) == 0)
		{
			return v->Value;
		}
	}

	return 0;
}

bool Xml::ProcessTag(char *TagName)
{
	bool Status = false;
	if (TagName)
	{
		if (stricmp(TagName, "random-line") == 0)
		{
			char *FileName = GetValue();
			if (FileName)
			{
				auto File = LReadFile(FileName);
				if (File)
				{
					List<char> Lines;

					char *s = File;
					while (*s)
					{
						// skip past leading newlines
						while (*s AND strchr("\r\n", *s)) s++;

						// parse lines
						char *StartLine = s;
						int Len = 0;
						do
						{
							char *Start = s;
							while (*s AND NOT strchr("\r\n", *s)) s++;
							Len = (int) s - (int) Start;
							if (*s == '\r') s++;
							if (*s == '\n') s++;

						} while (Len > 0);

						Len = (int) s - (int) StartLine;
						if (Len > 0)
						{
							char *Temp = new char[Len+1];
							if (Temp)
							{
								memcpy(Temp, StartLine, Len);
								Temp[Len] = 0;
								Lines.Insert(Temp);
							}
						}
					}

					char *RandomLine = Lines.ItemAt(LRand(Lines.GetItems()));
					if (RandomLine)
					{
						Str.Push(RandomLine);
					}

					for (s = Lines.First(); s; s = Lines.Next())
					{
						DeleteArray(s);
					}

					Status = true;
				}
			}
		}

		if (stricmp(TagName, "include-file") == 0)
		{
			char *FileName = GetValue();
			if (FileName)
			{
				auto File = LReadFile(FileName);
				if (File)
				{
					Str.Push(File);
					Status = true;
				}
			}
		}

		if (stricmp(TagName, "quote-file") == 0)
		{
			char *FileName = GetValue();
			char *QuoteStr = GetValue("quote");
			if (FileName)
			{
			}
		}
	}
	return Status;
}

bool Xml::ExpandXml(char *s, int *CursorIndex)
{
	bool Status = false;
	char WhiteSpace[] = " \t\r\n";

	if (s)
	{
		// delete any previous tags
		for (XmlTag *v = Values.First(); v; v = Values.Next())
		{
			DeleteObj(v);
		}
		Values.Empty();

		// loop through the input
		Status = true;
		while (*s)
		{
			// seek to next tag
			char *Start = s;
			while (*s AND *s != '<')
			{
				s++;
			}

			// push the section upto the tag onto the output
			int Len = (int) s - (int) Start;
			if (Len > 0) Str.GBytePipe::Push((uchar*) Start, Len);

			if (*s == '<')
			{
				// read tag
				Start = ++s;
				char TagName[256], *p = TagName;
				while (*s AND NOT strchr(WhiteSpace, *s) AND *s != '>')
				{
					*p++ = *s++;
				}
				*p++ = 0;

				// read values
				while (*s AND *s != '>')
				{
					char *End = strchr(s, '>');
					int Len = (int) End - (int) s;
					if (Len > 0)
					{
						XmlTag *Tag = new XmlTag(s, Len);
						if (Tag->Name)
						{
							Values.Insert(Tag);
						}
						else
						{
							// Tag didn't parse
							DeleteObj(Tag);
						}
					}
					else
					{
						break;
					}
				}

				// process tag
				bool TagOk = false;
				if (TagOk = ProcessTag(TagName))
				{
					if (CursorIndex AND stricmp(TagName, "cursor-location") == 0)
					{
						*CursorIndex = Str.Sizeof();
					}
				}

				// skip end delimiter
				if (*s == '>') s++;

				if (NOT TagOk)
				{
					// uh... the tag didn't get recognised so emit the text
					// inside the <>'s
					Start--;
					Str.GBytePipe::Push((uchar*) Start, (int)s-(int)Start+1);
				}
			}
		}
	}

	return Status;
}

char *Xml::NewStrFromXmlFile(char *FileName)
{
	int CursorIndex = 0;

	if (FileName)
	{
		char *Src = LReadTextFile(FileName);
		if (Src)
		{
			ExpandXml(Src, &CursorIndex);
			DeleteArray(Src);
		}
	}

	char *Buffer = Str.NewStr();
	if (Buffer)
	{
		Cursor.x = 0;
		Cursor.y = 0;
		if (CursorIndex > 0)
		{
			for (char *s = Buffer; *s AND ((int)s-(int)Buffer) < CursorIndex; s++)
			{
				if (*s == '\n')
				{
					Cursor.y++;
					Cursor.x = 0;
				}
				else
				{
					Cursor.x++;
				}
			}
		}
	}

	return Buffer;
}
