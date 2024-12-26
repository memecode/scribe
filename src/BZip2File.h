#pragma once

#include "../../libs/bzip2-1.0.6/bzlib.h"

class LBzip2File : public LFile
{
	int BufLen;
	LAutoString Buf;
	bz_stream Bz;
	bool Decomp = false;
	bool Opened = false;
	int Result = BZ_PARAM_ERROR;

public:
	LBzip2File(int buflen = 64 << 10)
	{
		ZeroObj(Bz);
		BufLen = buflen;
		Buf.Reset(new char[BufLen]);
	}

	int GetResult()
	{
		return Result;
	}
	
	int Open(const char *Name, int Attrib)
	{
		if (!LFile::Open(Name, Attrib))
			return false;

		Decomp = Attrib == O_READ;
		if (Decomp)
			Result = BZ2_bzDecompressInit(&Bz, 0/*verbosity*/, 0/*small*/);
		else
			Result = BZ2_bzCompressInit(&Bz, 7/*block size*/, 0/*verbosity*/, 0/*default workfactor*/);
		if (Result < BZ_OK)
			return false;
		
		Opened = true;
		return true;
	}
	
	int Close()
	{
		if (Opened)
		{
			if (Decomp)
				BZ2_bzDecompressEnd(&Bz);
			else
				BZ2_bzCompressEnd(&Bz);
			Opened = false;
		}
		
		return LFile::Close();
	}
	
	ssize_t Read(void *Buffer, ssize_t Size, int Flags = 0)
	{
		if (Result == BZ_STREAM_END)
			return 0;

		if (!Buffer || Size <= 0 || !Buf)
		{
			LAssert(!"Bad parameter.");
			return 0;
		}
		if (!Decomp)
		{
			LAssert(!"Can't read from a compression stream.");
			return 0;
		}

		ssize_t Rd = 0, Wr = 0;
		do
		{
			if (Bz.avail_in > 0)
			{
				// Shift data down to the bottom of the buffer.
				memmove(Buf, Bz.next_in, Bz.avail_in);
			}
			
			Rd = LFile::Read(Buf + Bz.avail_in, BufLen - Bz.avail_in);
			// LgiTrace("Src.Read next_in=%i avail_in=%i Rd=%i\n", Bz.next_in?Bz.next_in-Buf:-1, Bz.avail_in, Rd);
			
			Bz.next_in = Buf;
			Bz.avail_in += (unsigned int)Rd;
			Bz.next_out = (char*)Buffer;
			Bz.avail_out = (int)Size;
			
			// LgiTrace("\tBZ2_bzDecompress avail_in=%i, avail_out=%i\n", Bz.avail_in, Bz.avail_out);
			Result = BZ2_bzDecompress(&Bz);
			Wr = Size - Bz.avail_out;
			// LgiTrace("\tResult=%i avail_in=%i, avail_out=%i, Wr=%i\n", Result, Bz.avail_in, Bz.avail_out, Wr);

			if (Result != BZ_OK)
			{
				if (Result < 0)
				{
					LgiTrace("%s:%i - BZ2_bzDecompress failed with %i\n", _FL, Result);
					return -1;
				}
				break;
			}
		}
		while (Rd > 0 && Wr == 0);
		
		return Wr;
	}
	
	ssize_t Write(const void *Buffer, ssize_t Size, int Flags = 0)
	{
		LAssert(!"not impl.");
		return -1;
	}
};

