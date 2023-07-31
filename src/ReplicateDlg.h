/// \file
/// \author Matthew Allen <fret@memecode.com>
/// \date 24/7/2009
#ifndef _REPLICATEDLG_H_
#define _REPLICATEDLG_H_

#include "lgi/common/XmlTreeUi.h"

/// UI for replicating between mail stores.
class ReplicateDlg : public LDialog
{
public:
	struct AccountSpec
	{
		// Resource to load
		LUri Uri;
		
		/// MAIL_SSL, MAIL_USE_STARTTLS etc
		int SslFlags;

		AccountSpec()
		{
			SslFlags = 0;
		}
	};

	struct ReplicateSettings
	{
		AccountSpec Src;
		AccountSpec Dst;
		LArray<int> Types;
	};

private:
	struct ReplicateDlgPriv *d;

public:
	ReplicateDlg(class ScribeWnd *app);
	~ReplicateDlg();

	void OnFolderChange();
	int OnNotify(LViewI *c, LNotification n);
	bool StartProcess();
};

Store3Status Store3ReplicateFolders(ScribeWnd *App,
									LDataFolderI *Dst,
									LDataFolderI *Src,
									bool Recurse,
									bool DeleteSourceOnSuccess, // For 'move' operation
									LArray<Store3ItemTypes> *Types);

#endif
