#ifndef __ATTENDEE_H__
#define __ATTENDEE_H__

enum AttendeeType
{
	ANew				= 0,
	AMeetingOrganiser	= 1,
	ARequiredAttendee	= 2,
	AOptionalAttendee	= 3
};

class Attendee :
	public LListItem
	// , public VObjectImpl<LDataI>
{
	friend class Calendar;
	friend class GTimeLine;

	Calendar *Cal;
	ScribeWnd *App;
	class ListAddr *Addr;
	bool Edit;

	bool GetSelection(List<Attendee> &Attendees);
	char *NewProp(char *s) { return NewStr(s); }
	int NewProp(int i) { return i; }
	void Change();

public:
	Attendee(ScribeWnd *app, Attendee *a);
	Attendee(Calendar *cal, AttendeeType type, Contact *c = 0);
	Attendee(Calendar *cal, AttendeeType type, char *Name, char *Email);
	~Attendee();

	// Methods
	AttendeeType GetAttendeeType();
	void SetAttendeeType(AttendeeType t);	
	bool GetFreeBusy(LDateTime &Start, LDateTime &End, LArray<TimePeriod> &e);
	LColour GetColour();

	// LListItem
	void OnPaint(ItemPaintCtx &Ctx) override;
	const char *GetText(int i) override;
	bool SetText(const char *s, int c) override;
	int GetImage(int i) override;
	void DeleteSelection();
	void OnMouseClick(LMouse &m) override;
	bool OnKey(LKey &k) override;

	// GStorageItem
	int Type() { return MAGIC_ATTENDEE; }

	// Property List
	void OnSerialize(bool Write);


	bool GetField(int Id, char *&s) { return false; }
	bool GetField(int Id, int &n) { return false; }
	bool SetField(int Id, char *s) { return false; }
	bool SetField(int Id, int n) { return false; }
};

#endif
