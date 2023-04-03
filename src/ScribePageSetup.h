#ifndef __SCRIBE_PAGE_SETUP_H
#define __SCRIBE_PAGE_SETUP_H

#define OPT_MarginX1	"Print.MarginX1"
#define OPT_MarginY1	"Print.MarginY1"
#define OPT_MarginX2	"Print.MarginX2"
#define OPT_MarginY2	"Print.MarginY2"
#define OPT_PrintFont	"Print.Font"

#define PageDefaultX	21.0
#define PageDefaultY	29.7

class ScribePageSetup : public LDialog
{
	LFontType Font;
	class LOptionsFile *Options;

public:
	ScribePageSetup(LView *parent, class LOptionsFile *options);
	void Serialize(bool Write);
	int OnNotify(LViewI *Ctrl, LNotification n);
};

#endif
