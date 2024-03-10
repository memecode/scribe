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

// Mail flags
#define ScribeMailSend			(1 << 0)
#define ScribeMailReceived		(1 << 1)
#define ScribeMailCreated		(1 << 2)
#define ScribeMailForwarded		(1 << 3)
#define ScribeMailReplies		(1 << 4)
#define ScribeMailAttachments	(1 << 5)
#define ScribeMailRead			(1 << 6)
// 7 is deleted
#define ScribeMailReadyToSend	(1 << 8)
#define ScribeMailReadReceipt	(1 << 9)
#define ScribeMailIgnore		(1 << 10)
#define ScribeMailMonospace		(1 << 11)
#define ScribeMailBounced		(1 << 12)
#define ScribeMailBounce		(1 << 13)
#define ScribeMailShowImages	(1 << 14)
#define ScribeMailNew			(1 << 15)
#define ScribeMailStoredFlat	(1 << 16)
#define ScribeMailBayesHam		(1 << 17)
#define ScribeMailBayesSpam		(1 << 18)
// 19?
#define ScribeMailHamDb			(1 << 20)
#define ScribeMailSpamDb		(1 << 21)

