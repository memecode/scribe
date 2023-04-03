// These definitions come from the Windows 8.1 SDK.
// Instead of adding a new dependency to build Scribe I've copied the relevant parts here
// to make it build.
#ifndef _MAPIW_H_
#define _MAPIW_H_

#if _MSC_VER < _MSC_VER_VS2013

typedef struct
{
    ULONG ulReserved;
    ULONG ulRecipClass;
    PWSTR lpszName;
    PWSTR lpszAddress;
    ULONG ulEIDSize;
    PVOID lpEntryID;
} MapiRecipDescW, *lpMapiRecipDescW;

typedef struct
{
    ULONG ulReserved;
    ULONG flFlags;
    ULONG nPosition;
    PWSTR lpszPathName;
    PWSTR lpszFileName;
    PVOID lpFileType;
} MapiFileDescW, *lpMapiFileDescW;

typedef struct
{
  ULONG ulReserved;
  PWSTR lpszSubject;
  PWSTR lpszNoteText;
  PWSTR lpszMessageType;
  PWSTR lpszDateReceived;
  PWSTR lpszConversationID;
  FLAGS flFlags;
  lpMapiRecipDescW lpOriginator;
  ULONG nRecipCount;
  lpMapiRecipDescW lpRecips;
  ULONG nFileCount;
  lpMapiFileDescW lpFiles;
} MapiMessageW, *lpMapiMessageW;

typedef ULONG (FAR PASCAL MAPISENDMAILW)(
	LHANDLE lhSession,
	ULONG_PTR ulUIParam,
	_In_ lpMapiMessageW lpMessage,
	FLAGS flFlags,
	ULONG ulReserved
);
typedef MAPISENDMAILW FAR *LPMAPISENDMAILW;
MAPISENDMAILW MAPISendMailW;

#endif

#endif