#include "xmppShared.h"
#include "AutoGen/xmppTypes_h_ast.h"

void xmpp_escape_jid(char **estr, const char *jid)
{
	const char *c = jid;
	if (!jid)
		return;
	if (!*jid)
	{
		estrCopy2(estr, "");
		return;
	}
	estrClear(estr);
	
	while (*c)
	{
		switch (*c) {
		case ' ':
		case '\"':
		case '&':
		case '\'':
		case '/':
		case ':':
		case '<':
		case '>':
		case '@':
		case '\\':
			estrConcatf(estr, "\\%2hhx", *c);
		xdefault:
			estrConcatChar(estr, *c);
		}
		c++;
	}
}

void xmpp_unescape_jid(char **estr, const char *jid)
{
	const char *c = jid;
	if (!jid)
		return;
	if (!*jid)
	{
		estrCopy2(estr, "");
		return;
	}
	estrClear(estr);
	estrReserveCapacity(estr, (int) strlen(jid));
	while (*c)
	{
		if (*c == '\\')
		{
			bool bValid = false;
			if (*(c+1) && isxdigit((unsigned char) *(c+1)) && 
				*(c+2) && isxdigit((unsigned char) *(c+2)))
			{
				U32 uc = 0;
				bValid = true;
				sscanf(c+1,"%2x",&uc); // get the short unicode value
				switch (uc)
				{
				case (0x20): // <space>
					estrConcatChar(estr, ' ');
				xcase (0x22): // "
					estrConcatChar(estr, '\"');
				xcase (0x26): // &
					estrConcatChar(estr, '&');
				xcase (0x27): // '
					estrConcatChar(estr, '\'');
				xcase (0x2f): // /
					estrConcatChar(estr, '/');
				xcase (0x3a): // :
					estrConcatChar(estr, ':');
				xcase (0x3c): // <
					estrConcatChar(estr, '<');
				xcase (0x3e): // >
					estrConcatChar(estr, '>');
				xcase (0x40): // @
					estrConcatChar(estr, '@');
				xcase (0x5c): /* \ */
					estrConcatChar(estr, '\\');
				xdefault:
					bValid = false;
				}
			}
			if (bValid)
				c += 2;
			else
				estrConcatChar(estr, *c);
		}
		else
		{
			estrConcatChar(estr, *c);
		}
		c++;
	}
}

#include "AutoGen/xmppShared_h_ast.c"