#ifndef __SCRIBE_INC_H
#define __SCRIBE_INC_H

// Version
#define ScribeVer					"3.4"

#define ImapSupport
#define FilterSupport

// Debug flags
#define DEBUG_NEW_MAIL				0
#define DEBUG_PRINT_MAIL			0
#define DEBUG_BUILD_WORD_DB			0

// Library
#ifdef SCRIBE_APP

	// is the scribe executable
	#ifdef WIN32
		#define ScribeFunc		extern "C" __declspec(dllexport)
		#define ScribeClass		__declspec(dllexport)
		#define ScribeExtern	extern __declspec(dllexport)
	#else
		#define ScribeFunc		extern "C"
		#define ScribeClass
		#define ScribeExtern	extern
	#endif

#else

	// is a plugin
	#ifdef WIN32
		#define ScribeFunc		extern "C" __declspec(dllimport)
		#define ScribeClass		__declspec(dllimport)
		#define ScribeExtern	extern __declspec(dllimport)
	#else
		#define ScribeFunc		extern "C"
		#define ScribeClass
		#define ScribeExtern	extern
	#endif

#endif

#ifdef WIN32
#pragma warning(disable:4355)
#endif


#endif // __SCRIBE_INC_H
