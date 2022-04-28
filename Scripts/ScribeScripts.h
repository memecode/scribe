// Various Scribe related magic values

// Type types
#define ScribeItemBase			0xAAFF0000
#define ScribeItemMail			(ScribeItemBase + 1)
#define ScribeItemContact		(ScribeItemBase + 2)
#define ScribeItemMailbox		(ScribeItemBase + 4)
#define ScribeItemAttachment	(ScribeItemBase + 5)
#define ScribeItemAny			(ScribeItemBase + 6)
#define ScribeItemFilter		(ScribeItemBase + 7)
#define ScribeItemFolder		(ScribeItemBase + 8)
#define ScribeItemCondition		(ScribeItemBase + 9)
#define ScribeItemAction		(ScribeItemBase + 10)
#define ScribeItemCalendar		(ScribeItemBase + 11)
#define ScribeItemAttendee		(ScribeItemBase + 12)
#define ScribeItemGroup			(ScribeItemBase + 13)

// System folder types
#define SystemFolderInbox		0
#define SystemFolderOutbox		1
#define SystemFolderSent		2
#define SystemFolderTrash		3
#define SystemFolderContacts	4
#define SystemFolderTemplates	5
#define SystemFolderFilters		6
#define SystemFolderCalendar	7
#define SystemFolderGroups		8
#define SystemFolderSpam		9
