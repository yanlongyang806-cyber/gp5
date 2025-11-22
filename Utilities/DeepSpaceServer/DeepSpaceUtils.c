/*
 * DeepSpaceServer utility functions
 */

#include "crypt.h"
#include "DeepSpaceUtils.h"
#include "EString.h"
#include "wininclude.h"
#include "UTF8.h"

// Parse an info hash string like "2b31d7042bf17716ee1fc1bcfa27f06ca912be9c".
bool parseInfoHashString(U32 info_hash[8], const char *string)
{
	return decodeHexString(string, strlen(string), (char *)info_hash, sizeof(U32)*8) > 0;
}

// Get an estring that is the name of the LCID.
void getLcidName(char **estr, int lcid)
{
	char *pLangName = NULL;
	char *pCtryName = NULL;

	if (GetLocaleInfo_UTF8(lcid, LOCALE_SISO639LANGNAME, &pLangName) 
		&& GetLocaleInfo_UTF8(lcid, LOCALE_SISO3166CTRYNAME, &pCtryName))
	{
		estrPrintf(estr, "%s-%s", pLangName, pCtryName);
	}
	else
	{
		estrCopy2(estr, "Unknown");
	}

	estrDestroy(&pLangName);
	estrDestroy(&pCtryName);
}
