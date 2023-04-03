// Scribe mail store item types and properties

// Item types
#define SITEM_FOLDER			1
#define SITEM_MAIL				2
#define SITEM_CONTACT			3
#define SITEM_CALENDER			4
#define SITEM_TASK				5

// Property/type macros
#define MakeProp(Type, Id)		(((Type)<<16)|((Id)&0xffff))
#define PropType(Prop)			((Prop)>>16)
#define PropId(Prop)			((Prop)&0xffff)

// general item properties
#define SPROP_NAME				MakeProp(STYPE_STRING, 1)
#define SPROP_FLAGS				MakeProp(STYPE_INT, 2)
#define SPROP_DATE_CREATE		MakeProp(STYPE_QUAD, 3)
#define SPROP_DATE_MOD			MakeProp(STYPE_QUAD, 4)
#define SPROP_DATE_ACCESS		MakeProp(STYPE_QUAD, 5)
#define SPROP_PRIORITY			MakeProp(STYPE_INT, 6)

// folder props
#define SPROP_LIST_TYPE			MakeProp(STYPE_INT, 100)
#define SPROP_COL				MakeProp(STYPE_LIST, 101)		// ScribeField[SPROP_COLS]

// email props
#define SPROP_FROM				MakeProp(STYPE_ADDRESS, 200)
#define SPROP_TO				MakeProp(STYPE_LIST, 201)		// of STYPE_ADDRESS
#define SPROP_REPLY_TO			MakeProp(STYPE_ADDRESS, 202)
#define SPROP_SUBJECT			MakeProp(STYPE_STRING, 203)
#define SPROP_BODY				MakeProp(STYPE_STRING, 204)
#define SPROP_DIGEST			MakeProp(STYPE_LIST, 205)		// optional
#define SPROP_HEADERS			MakeProp(STYPE_STRING, 206)
#define SPROP_RECEIVE_DATE		MakeProp(STYPE_QUAD, 207)
#define SPROP_SENT_DATE			MakeProp(STYPE_QUAD, 208)
// SPROP_PRIORITY

// contact props
#define SPROP_FIRST_NAME		MakeProp(STYPE_STRING, 300)
#define SPROP_LAST_NAME			MakeProp(STYPE_STRING, 301)
#define SPROP_EMAIL_ADDR		MakeProp(STYPE_STRING, 302)
#define SPROP_STREET			MakeProp(STYPE_STRING, 303)
#define SPROP_SUBURB			MakeProp(STYPE_STRING, 304)
#define SPROP_POSTCODE			MakeProp(STYPE_STRING, 305)
#define SPROP_STATE				MakeProp(STYPE_STRING, 306)
#define SPROP_COUNTRY			MakeProp(STYPE_STRING, 307)
#define SPROP_WORK_PHONE		MakeProp(STYPE_STRING, 308)
#define SPROP_HOME_PHONE		MakeProp(STYPE_STRING, 309)
#define SPROP_MOBILE			MakeProp(STYPE_STRING, 310)
#define SPROP_ICQ				MakeProp(STYPE_STRING, 311)
#define SPROP_FAX				MakeProp(STYPE_STRING, 312)
#define SPROP_WEBPAGE			MakeProp(STYPE_STRING, 313)
#define SPROP_NICK				MakeProp(STYPE_STRING, 314)
#define SPROP_SPOUSE			MakeProp(STYPE_STRING, 315)
#define SPROP_NOTE				MakeProp(STYPE_STRING, 316)

// calender props
#define SPROP_START_DATE		MakeProp(STYPE_QUAD, 400)
#define SPROP_END_DATE			MakeProp(STYPE_QUAD, 402)
#define SPROP_CAL_TEXT			MakeProp(STYPE_STRING, 403)
#define SPROP_LOCATION			MakeProp(STYPE_STRING, 404)
#define SPROP_ATTENDEES			MakeProp(STYPE_LIST, 405)
#define SPROP_RECUR_PERIOD		MakeProp(STYPE_INT, 406)	// day/week/month/etc
#define SPROP_RECUR_COUNT		MakeProp(STYPE_INT, 407)	// 

// task props
#define SPROP_TASK_TEXT			MakeProp(STYPE_STRING, 500)
#define SPROP_DUE				MakeProp(STYPE_QUAD, 501)
#define SPROP_STATUS			MakeProp(STYPE_INT, 502)
#define SPROP_COMPLETED			MakeProp(STYPE_QUAD, 503)
#define SPROP_NOTIFY			MakeProp(STYPE_LIST, 504)
// SPROP_NAME
// SPROP_PRIORITY
