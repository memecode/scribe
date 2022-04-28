#include "Lgi.h"
#include "Scribe.h"

//////////////////////////////////////////////////////////
GFilePipe::GFilePipe(char *f, bool Write)
{
	if (File.Open(f, Write ? O_WRITE : O_READ))
	{
		if (Write)
		{
			Empty();
		}
	}
	else
	{
		// If this happens it's usually a virus-scanner deleting the
		// .eml file from the Temp folder before we load it in again...
		// which is not an error I guess.
		// LAssert(0);
	}
}

bool GFilePipe::IsOpen()
{
	return File.IsOpen();
}

int GFilePipe::Read(void *Ptr, int Size, int Flags)
{
	return File.Read(Ptr, Size);
}

int GFilePipe::Write(const void *Ptr, int Size, int Flags)
{
	return File.Write(Ptr, Size);
}

bool GFilePipe::IsEmpty()
{
	return GetSize() == 0;
}

void GFilePipe::Empty()
{
	File.SetSize(0);
}

int64 GFilePipe::GetSize()
{
	return File.GetSize();
}

void *GFilePipe::New(int AddBytes)
{
	char *Buf = 0;
	int Len = File.GetSize();
	if (Len > 0)
	{
		Buf = new char[Len + AddBytes];
		if (Buf)
		{
			File.Read(Buf, Len);
			memset(Buf+Len, 0, AddBytes);
		}
	}
	else
	{
		LAssert(0);
	}

	return Buf;
}

int64 GFilePipe::Peek(uchar *Ptr, int Size)
{
	LAssert(0);
	return 0;
}

