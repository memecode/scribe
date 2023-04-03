class MailBuf : public LFile
{
	int64 Pos;
	int64 Len;
	char *Buf;

public:
	MailBuf(LFile &f, int l)
	{
		Len = l;
		Pos = 0;
		Buf = new char[(int)Len];
		if (Buf)
		{
			Len = f.Read(Buf, l);
			if (Len != l)
			{
				printf("MailBuf didn't read the length of the mail.\n");
			}
		}
	}

	~MailBuf()
	{
		DeleteArray(Buf);
	}

	char *GetPtr()
	{
		return Buf ? Buf + Pos : 0;
	}

	char *GetEnd()
	{
		return Buf ? Buf + Len : 0;
	}

	int Read(void *Buffer, int Size, int Flags)
	{
		int64 r = 0;

		if (Buffer && Size > 0 && !Eof())
		{
			r = min((int64)Size, Len-Pos);
			if (r > 0)
			{
				memcpy(Buffer, Buf + Pos, (int)r);
				Pos += r;
			}
		}

		return (int)r;
	}

	int Write(const void *Buffer, int Size, int Flags)
	{
		LAssert(0);
		return 0;
	}

	int64 Seek(int64 To, int Whence)
	{
		switch (Whence)
		{
			case SEEK_SET:
			{
				Pos = To;
				break;
			}
			case SEEK_CUR:
			{
				Pos += To;
				break;
			}
			case SEEK_END:
			{
				Pos = Len + To;
				break;
			}
		}

		return Pos;
	}

	int64 GetPos()
	{
		return Pos;
	}

	int64 GetSize()
	{
		return Len;
	}

	int64 SetSize(int64 Size)
	{
		LAssert(0);
		return false;
	}

	bool Eof()
	{
		return Pos < 0 || Pos >= Len;
	}

	int ReadStr(char *Buf, int Size)
	{
		int l;
		if (Buf &&
			Read(&l, sizeof(l), 0) == sizeof(l))
		{
			l = min(l, Size-1);

			while (Pos < Len)
			{
				if (Buf[Pos] != '\n' && Buf[Pos])
				{
					*Buf = Buf[Pos];
					Buf++;
					Pos++;
				}
				else
				{
					Pos++;
					break;
				}
			}

			*Buf++ = 0;
		}

		return 0;
	}

	int WriteStr(char *Buf, int Size)
	{
		LAssert(0);
		return 0;
	}
	
	int Print(char *Format, ...)
	{
		LAssert(0);
		return 0;
	}

	// Read
	#define ReadIO() { Read(&i, sizeof(i), 0); return *this; }

	LFile &operator >> (char			&i) ReadIO()
	LFile &operator >> (signed char		&i) ReadIO()
	LFile &operator >> (unsigned char	&i) ReadIO()
	LFile &operator >> (signed short	&i) ReadIO()
	LFile &operator >> (unsigned short	&i) ReadIO()
	LFile &operator >> (signed int		&i) ReadIO()
	LFile &operator >> (unsigned int	&i) ReadIO()
	LFile &operator >> (signed long		&i) ReadIO()
	LFile &operator >> (unsigned long	&i) ReadIO()
	LFile &operator >> (float			&i) ReadIO()
	LFile &operator >> (double			&i) ReadIO()
	LFile &operator >> (long double		&i) ReadIO()

	// Write
	#define WriteIO() { LAssert(0); return *this; }

	LFile &operator << (char			i) WriteIO()
	LFile &operator << (signed char		i) WriteIO()
	LFile &operator << (unsigned char	i) WriteIO()
	LFile &operator << (signed short	i) WriteIO()
	LFile &operator << (unsigned short	i) WriteIO()
	LFile &operator << (signed int		i) WriteIO()
	LFile &operator << (unsigned int	i) WriteIO()
	LFile &operator << (signed long		i) WriteIO()
	LFile &operator << (unsigned long	i) WriteIO()
	LFile &operator << (float			i) WriteIO()
	LFile &operator << (double			i) WriteIO()
	LFile &operator << (long double		i) WriteIO()

	// Custom
	char *GetStr()
	{
		char *s = 0;

		if (Len - Pos >= 4)
		{
			int l = *((int*)(Buf+Pos));
			Pos += 4;

			if (Len - Pos >= l)
			{
				s = new char[l+1];
				if (s)
				{
					memcpy(s, Buf+Pos, l);
					s[l] = 0;
					Pos += l;
				}
			}
		}

		return s;
	}

	int GetInt()
	{
		int i = 0;

		if (Len - Pos >= 4)
		{
			int l = *((int*)(Buf+Pos));
			Pos += 4;

			if (Len - Pos >= l)
			{
				memcpy(&i, Buf+Pos, l);
				Pos += l;
			}
		}

		return i;
	}
};

