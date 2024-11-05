#include "Scribe.h"

class ScribePasswordPrivate
{
public:
	LView *Dlg;
	LOptionsFile *Props;
	const char *Opt;
	int CtrlEnable;
	int CtrlPwd;
	int CtrlConfirm;
};

ScribePassword::ScribePassword(LOptionsFile *p, const char *opt, int check, int pwd, int confirm)
{
	d = new ScribePasswordPrivate;
	d->Props = p;
	d->Opt = opt;
	d->CtrlEnable = check;
	d->CtrlPwd = pwd;
	d->CtrlConfirm = confirm;
	d->Dlg = 0;
}

ScribePassword::~ScribePassword()
{
	DeleteObj(d);
}

bool ScribePassword::IsOk()
{
	bool Status =
					#ifndef __llvm__
					this != 0 &&
					#endif
					d != 0 &&
					d->Dlg != 0 &&
					d->Opt != 0 &&
					d->Props != 0;
	LAssert(Status);
	return Status;
}

bool ScribePassword::Load(LView *dlg)
{
	bool Status = false;
	d->Dlg = dlg;
	if (IsOk())
	{
		LPassword p;
		if (p.Serialize(d->Props, d->Opt, false))
		{
			d->Dlg->SetCtrlValue(d->CtrlEnable, true);
		}
		else
		{
			d->Dlg->SetCtrlEnabled(d->CtrlPwd, false);
			d->Dlg->SetCtrlEnabled(d->CtrlConfirm, false);
		}
		Status = true;
	}
	return Status;
}

bool ScribePassword::Save()
{
	bool Status = false;
	if (IsOk())
	{
		if (d->Dlg->GetCtrlValue(d->CtrlEnable))
		{
			auto Psw = d->Dlg->GetCtrlName(d->CtrlPwd);
			auto Confirm = d->Dlg->GetCtrlName(d->CtrlConfirm);
			if (ValidStr(Psw) && ValidStr(Confirm))
			{
				if (strcmp(Psw, Confirm) == 0)
				{
					LPassword p;
					p.Set(Psw);
					if (p.Serialize(d->Props, d->Opt, true))
					{
						Status = true;
					}
				}
				else
				{
					LgiMsg(d->Dlg, "Passwords don't match.", AppName);
				}
			}
			else if (ValidStr(Psw) || ValidStr(Confirm))
			{
				LgiMsg(d->Dlg, "Both the password and the confirmation need to be entered.", AppName);
			}
			else
			{
				// Unchanged
				Status = true;
			}
		}
		else
		{
			d->Props->DeleteValue(d->Opt);
			Status = true;
		}
	}
	return Status;
}

void ScribePassword::OnNotify(LViewI *c, const LNotification &n)
{
	if (c->GetId() == d->CtrlEnable && IsOk())
	{
		bool On = c->Value() != 0;
		d->Dlg->SetCtrlEnabled(d->CtrlPwd, On);
		d->Dlg->SetCtrlEnabled(d->CtrlConfirm, On);
	}
}
