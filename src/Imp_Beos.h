#pragma once
#ifdef HAIKU

#define IMP_BEOS_PEOPLE					0x0001
#define IMP_BEOS_MAIL					0x0002
extern bool Import_Beos(ScribeWnd *App, int Flags);

#endif
