#ifndef _TABLES_H_
#define _TABLES_H_

#ifdef __cplusplus
extern "C" {
#endif

int StringToMailFlag(const char *s);
/*
int StringToMailFlag(const char *s)
{
	struct MailFlagMap *f = in_word_set(s, strlen(s));
	return f ? f->value : 0;
}
*/

#ifdef __cplusplus
}
#endif

#endif