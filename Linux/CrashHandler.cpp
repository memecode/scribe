#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <string.h>

#define LAssert assert

#include "lgi/common/Array.h"
#include "lgi/common/StringClass.h"

#define _FL __FILE__, __LINE__

enum HandleType
{
	hRead,
	hWrite,
	hMax,
};

int in[hMax]; // app -> child pipe
int out[hMax]; // child -> app pipe
bool loop = true;

void LSleep(uint32_t i)
{
	struct timespec request = {}, remain = {};

	request.tv_sec = i / 1000;
	request.tv_nsec = (i % 1000) * 1000000;

	while (nanosleep(&request, &remain) == -1)
	    request = remain;
}

template<typename T>
T *strnchr(T *str, char ch, size_t len)
{
	if (!str)
		return NULL;
	T *end = str + len;
	while (str < end)
	{
		if (*str == ch)
			return str;
		str++;
	}
	return NULL;
}

enum AppState
{
	sInit,
	sLocals,
	sArgs,
	sStack,
	sGetThreads,
	sThreadSwitch,
	sThreadBt,
}	state = sInit;

LString::Array *output = NULL;
int curThread = 1, threads = -1;
FILE *crashLog = NULL;

void Log(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	char buf[1024];
	auto ch = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	
	if (ch > 0)
	{
		printf("%.*s", (int)ch, buf);
		if (crashLog)
			fwrite(buf, ch, 1, crashLog);
	}
}

void OpenCrashLog()
{
	if (crashLog)
		return;
		
	time_t now;
	time(&now);

	auto cur = localtime(&now);
	char file[256] = "crash.log";

	if (cur)	
		snprintf(file, sizeof(file),
			"crash-%i%02.2i%02.2i-%02.2i%02.2i%02.2i.log",
			cur->tm_year + 1900,
			cur->tm_mon + 1,
			cur->tm_mday,
			cur->tm_hour,
			cur->tm_min,
			cur->tm_sec);
			
	crashLog = fopen(file, "w");
}

void DumpOutput(const char *Label)
{
	Log("%s:\n", Label);
	for (auto ln: *output)
	{
		if (ln.Find("(gdb)") < 0)
			Log("   %s\n", ln.Get());
	}
	Log("\n");
	output->Length(0);
}

void AtPrompt()
{
	switch (state)
	{
		case sInit:
		{
			auto cmd = "info locals\n";
			int wr = write(in[hWrite], cmd, strlen(cmd));
			
			state = sLocals;
			output = new LString::Array;
			
			OpenCrashLog();
			break;
		}
		case sLocals:
		{
			DumpOutput("Locals");
		
			auto cmd = "info args\n";
			int wr = write(in[hWrite], cmd, strlen(cmd));
			
			state = sArgs;
			break;
		}
		case sArgs:
		{
			DumpOutput("Args");
		
			auto cmd = "bt\n";
			int wr = write(in[hWrite], cmd, strlen(cmd));
			
			state = sStack;
			break;
		}
		case sStack:
		{
			DumpOutput("Stack");
		
			auto cmd = "info threads\n";
			int wr = write(in[hWrite], cmd, strlen(cmd));
			
			state = sGetThreads;
			break;
		}
		case sGetThreads:
		{
			LString last;
			for (int i=output->Length()-1; i>=0; i--)
			{
				last = (*output)[i].Strip();
				if (isdigit(last(0)))
				{
					threads = (int)last.Int();
					break;
				}
			}
			
			Log("threads=%i\n", threads);
			output->Length(0);
			
			if (threads > 0)
			{
				state = sThreadBt;
				// fall through
			}
			else
			{
				Log("%s:%i - Error: failed to get thread count.\n", _FL);
				loop = false;
				break;
			}
		}
		case sThreadSwitch:
		{
			if (output->Length() > 0)
			{
				char label[64];
				snprintf(label, sizeof(label), "Thread %i", curThread++);
				DumpOutput(label);
			}

			if (curThread <= threads)
			{
				char cmd[256];
				auto cmdSz = snprintf(cmd, sizeof(cmd), "thread %i\n", curThread);
				int wr = write(in[hWrite], cmd, cmdSz);
				state = sThreadBt;
			}
			else loop = false;
			break;
		}
		case sThreadBt:
		{
			auto cmd = "bt\n";
			int wr = write(in[hWrite], cmd, strlen(cmd));
			state = sThreadSwitch;
			output->Length(0);
			break;
		}			
	}
}

void OnLine(char *str)
{
	// printf("line='%s'\n", str);
	if (output)
		output->Add(str);
	
	if (!strcasecmp(str, "(gdb) "))
		AtPrompt();
}

int main(int args, char **arg)
{
	int pid = -1;
	for (int i=1; i<args; i++)
	{
		if (!strcasecmp(arg[i], "--pid") &&
			i < args - 1)
		{
			pid = atoi(arg[i+1]);
			break;
		}
	}
	
	if (pid < 0)
	{
		printf("Error: no pid given.\n");
		return -1;
	}
	
	if (pipe(in) == -1)
	{
		printf("parent: Failed to create stdin pipe");
		return false;
	}

	if (pipe(out) == -1)
	{
		printf("parent: Failed to create stdout pipe");
		return false;
	}

	auto child = fork();
	if (child)
	{
		// child process..
		char pidArg[64];
		snprintf(pidArg, sizeof(pidArg), "--pid=%i", pid);
		const char *args[] = { "gdb", pidArg, NULL };

		// Child shouldn't write to its stdin.
		if (close(in[hWrite]))
			printf("%s:%i - close failed.\n", _FL);

		// Child shouldn't read from its stdout.
		if (close(out[hRead]))
			printf("%s:%i - close failed.\n", _FL);

		// Redirect stdin and stdout for the child process.
		if (dup2(in[hRead], fileno(stdin)) == -1)
		{
			printf("%s:%i - child[pre-exec]: Failed to redirect stdin for child\n", _FL);
			return -1;
		}
		if (close(in[hRead]))
			printf("%s:%i - close failed.\n", _FL);
		
		if (dup2(out[hWrite], fileno(stdout)) == -1)
		{
			printf("%s:%i - child[pre-exec]: Failed to redirect stdout for child\n", _FL);
			return -1;
		}

		if (dup2(out[hWrite], fileno(stderr)) == -1)
		{
			printf("%s:%i - child[pre-exec]: Failed to redirect stderr for child\n", _FL);
			return -1;
		}
		close(out[hWrite]);

		auto child = execvp("gdb", args);
		
		printf("%s:%i - execvp failed.\n", _FL);
		return -1;
	}
	
	// parent process...
	char buf[512];
	size_t used = 0;
	while (loop)
	{
		auto rd = read(out[hRead], buf + used, sizeof(buf) - used);
		if (rd < 0)
			break;
		used += rd;
		
		// printf("rd=%i\nbuf='%.*s'\n\n", (int)rd, (int)used, buf);
		
		char *nl;
		while ((nl = strnchr(buf, '\n', used)) != NULL)
		{
			*nl++ = 0;
			OnLine(buf);
			
			auto end = buf + used;
			auto remaining = end - nl;
			
			if (remaining > 0)
			{
				memmove(buf, nl, remaining);
				used = remaining;
				// printf("rem=%i used=%i\n", (int)remaining, (int)used);
			}
			else
			{
				used = 0;
				// printf("break used=%i\n", (int)used);
				break;
			}
		}
		if (used > 0)
		{
			buf[used] = 0;
			OnLine(buf);
			used = 0;
		}
		// printf("\n");
	}
	
	if (crashLog)
		fclose(crashLog);

	return 0;
}
