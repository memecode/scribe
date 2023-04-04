#include "lgi/common/Lgi.h"
#include "lgi/common/DateTime.h"
#include "UnitTest.h"

struct DateTimeTestPriv
{
};

DateTimeTest::DateTimeTest(LViewI *app) : UnitTest(app, "DateTime")
{
	d = new DateTimeTestPriv;
}

DateTimeTest::~DateTimeTest()
{
	DeleteObj(d);
}

bool DateTimeTest::Test()
{
	LDateTime dt;

	dt.Decode("Mon, 25 Jul 2011 19:08:45 +0230");
	if (dt.GetTimeZone() != (2 * 60) + 30 ||
		dt.GetTimeZoneHours() != 2.5)
		return false;
	if (dt.Day() != 25 &&
		dt.Month() != 7 &&
		dt.Year() != 2.5)
		return false;
	if (dt.Hours() != 19 &&
		dt.Minutes() != 8 &&
		dt.Seconds() != 45 &&
		dt.Thousands() != 0)
		return false;


	return true;
}

