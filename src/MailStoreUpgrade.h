#pragma once

class MailStoreUpgrade :
	public LProgressDlg,
	public LDataPropI
{
public:
	ScribeWnd *App = NULL;
	LDataStoreI *Ds = NULL;
	int Status = -1;
	LString Error;

	MailStoreUpgrade(ScribeWnd *app, LDataStoreI *ds) :
		LProgressDlg(app, -1)
	{
		SetParent(App = app);
		Ds = ds;
		SetCanCancel(false);
		SetDescription("Upgrading mail store...");
		SetPulse(1000);

		Ds->Upgrade(this, this, [this](auto status)
			{
				Status = status;
			});
	}

	~MailStoreUpgrade()
	{
	}

	void OnPulse() override
	{
		if (Status >= 0)
		{
			EndModal(0);
			return;
		}

		return LProgressDlg::OnPulse();
	}

	LDataPropI &operator =(LDataPropI &p)
	{
		LAssert(0);
		return *this;
	}

	Store3Status SetStr(int id, const char *str) override
	{
		switch (id)
		{
		case Store3UiError:
			Error = str;
			break;
		default:
			LAssert(!"Impl me.");
			return Store3Error;
			break;
		}

		return Store3Success;
	}
};