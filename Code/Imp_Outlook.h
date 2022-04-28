#ifndef _OUTLOOK_IO_H_
#define _OUTLOOK_IO_H_

#define IMP_OUTLOOK_PAB			1
#define IMP_OUTLOOK				2
#define EXP_OUTLOOK_EMAIL		3

#if WINNATIVE
extern void Import_Outlook(ScribeWnd *Wnd, int Flags);
extern void Export_Outlook(ScribeWnd *Wnd);
#endif

#endif