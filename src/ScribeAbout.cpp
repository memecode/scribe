#include <stdio.h>
#include "lgi/common/Lgi.h"
#include "lgi/common/Html.h"
#include "Scribe.h"
#include "ScribePrivate.h"
#include "lgi/common/Button.h"
#include "resdefs.h"
#include "lgi/common/LgiRes.h"
#include "DynamicHtml.h"

class ScribeAboutWnd : public LWindow, public LDefaultDocumentEnv
{
	ScribeWnd *App;
	Html1::LHtml *Ctrl;

public:
	ScribeAboutWnd(ScribeWnd *parent)
	{
		Ctrl = 0;
		Name(LLoadString(IDS_ABOUT));

		SetParent(App = parent);
		
		LRect r(0, 0, 600, 500);
		SetPos(r);
		MoveSameScreen(App);
		Children.Insert(Ctrl = new DynamicHtml(App, "About.html"));

		if (Attach(0))
		{
			AttachChildren();
			Visible(true);
			
			Ctrl->Focus(true);
			RegisterHook(this, LKeyEvents);
		}
	}

	bool OnViewKey(LView *v, LKey &k) override
	{
		if (k.CtrlCmd() && ToLower(k.c16) == 'w')
		{
			Quit();
			return true;
		}
		
		return false;
	}
	
	void OnPosChange() override
	{
		LRect c = GetClient();
		
		if (Ctrl)
		{
			LRect r(0, 0, c.X()-1, c.Y()-1);
			if (Ctrl) Ctrl->SetPos(r, true);
		}
			
		LWindow::OnPosChange();
	}

	int OnNotify(LViewI *c, const LNotification &n) override
	{
		if (c->GetId() == IDOK)
		{
			delete this;
		}

		return 0;
	}

	bool GetImageUri(char *Uri, LSurface **pDC, char *FileName, int FileBufSize)
	{
		bool Status = false;

		LString File = LFindFile(Uri);
		if (File)
		{
			if (pDC)
			{
				Status = (*pDC = GdcD->Load(File)) != 0;
			}
			else if (FileName)
			{
				strcpy_s(FileName, FileBufSize, File);
				Status = true;
			}
		}

		return Status;
	}

	bool OnNavigate(LDocView *Parent, const char *Uri) override
	{
		if (Uri &&
			_strnicmp(Uri, "mailto:", 7) == 0)
		{
			// Mail address
			return App->CreateMail(0, Uri+7, 0) != 0;
		}
		else
		{
			return LDefaultDocumentEnv::OnNavigate(Parent, Uri);
		}

		return false;
	}
};

void ScribeAbout(ScribeWnd *Parent)
{
	new ScribeAboutWnd(Parent);
}
