#pragma once

#include "lgi/common/Html.h"

////////////////////////////////////////////////////////////////////////////////////
class DynamicHtml : public Html1::LHtml, public LDefaultDocumentEnv
{
	class DynamicHtmlPrivate *d;

public:
	DynamicHtml(ScribeWnd *app, const char *file);
	~DynamicHtml();

	LString OnDynamicContent(LDocView *Parent, const char *Code) override;
	bool OnNavigate(LDocView *Parent, const char *Uri) override;
};

