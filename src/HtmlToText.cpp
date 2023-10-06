#include <stdlib.h>
#include <stdio.h>

#include "lgi/common/Lgi.h"
#include "lgi/common/Html.h"
#include "lgi/common/Css.h"
#include "lgi/common/Html.h"
#include "lgi/common/Charset.h"

//////////////////////////////////////////////////////////////////////////////////
LString HtmlToText(const char *InputHtml, const char *CharSet)
{
	Html1::LHtml Html(100, 0, 0, GdcD->X(), GdcD->Y());
	if (CharSet)
	{
		char *Eq = strchr(CharSet, '=');
		if (Eq)
		{
			LAutoString a(TrimStr(Eq+1));
			if (LGetCsInfo(a))
				Html.SetCharset(a);
		}
		else
		{
			if (LGetCsInfo(CharSet))
				Html.SetCharset(CharSet);
		}
	}
	Html.Name(InputHtml);
	
	LString Out;
	Html.GetFormattedContent("text/plain", Out, NULL);
	return Out;
}

//////////////////////////////////////////////////////////////////////////////////
LString TextToHtml(const char *Txt, const char *Charset)
{
	LStringPipe p(1024);
	p.Print("<html><body>\n");
	
	if (Txt)
	{
		LAutoString Utf(Charset && _stricmp(Charset, "utf-8") ? (char*)LNewConvertCp("utf-8", Txt, Charset) : NewStr(Txt));
		for (LUtf8Str u(Utf); (uint32_t)u; u++)
		{
			uint32_t c = u;
			if (c == '\n')
			{
				p.Print("<br>\n");
			}
			else
			{
				if (c > 0x7f)
					p.Print("&#%i;", c);
				else
					p.Print("%c", c);				
			}
		}
	}

	p.Print("</body></html>\n");
	return p.NewLStr();
}
