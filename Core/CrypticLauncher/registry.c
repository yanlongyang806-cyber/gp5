#include "registry.h"
#include "GameDetails.h"
#include "History.h"

// UtilitiesLib
#include "RegistryReader.h"
#include "GlobalTypes.h"
#include "earray.h"
#include "StringUtil.h"

#define CRYPTIC_REG_KEY "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\%s"

typedef struct ReadRegStrData
{
	const char *name;
	char *outbuffer;
	int outlen;
	int ret;
}
ReadRegStrData;

bool readRegStrHistoryItemCallbackFunc(char *historyItem, void *userData)
{
	bool cont = true;

	if (historyItem)
	{
		char histProdName[64], *p;
		strcpy(histProdName, historyItem);
		p = strchr(histProdName, ':');
		if (p)
		{
			*p = '\0';
		}
		if (histProdName[0])
		{
			ReadRegStrData *readRegData = (ReadRegStrData*)userData;
			readRegData->ret = readRegStr(histProdName, readRegData->name, readRegData->outbuffer, readRegData->outlen, false);
			if (readRegData->ret)
			{
				cont = false;
			}
		}
	}

	return cont;
}

typedef struct ReadRegIntData
{
	const char *name;
	int *out;
	int ret;
}
ReadRegIntData;

bool readRegIntHistoryItemCallbackFunc(char *historyItem, void *userData)
{
	bool cont = true;

	if (historyItem)
	{
		char histProdName[64], *p;
		strcpy(histProdName, historyItem);
		p = strchr(histProdName, ':');
		if (p)
		{
			*p = '\0';
		}
		if (histProdName[0])
		{
			ReadRegIntData *readRegData = (ReadRegIntData*)userData;
			readRegData->ret = readRegInt(histProdName, readRegData->name, readRegData->out, false);
			if (readRegData->ret)
			{
				cont = false;
			}
		}
	}

	return cont;
}


int readRegStr(const char *prodName, const char *name, char *outbuffer, int outlen, bool useHistory)
{
	RegReader reader;
	const char *dispName;
	int ret;

	if (!prodName)
		prodName = GetProductName();

	dispName = gdGetDisplayName(gdGetIDByName(prodName));

	reader = createRegReader();
	if (!initRegReaderEx(reader, CRYPTIC_REG_KEY, dispName))
		return 0;
	ret = rrReadString(reader, name, outbuffer, outlen);
	if (!ret && useHistory)
	{
		ReadRegStrData readRegData;
		readRegData.name = name;
		readRegData.outbuffer = outbuffer;
		readRegData.outlen = outlen;
		readRegData.ret = ret;
		HistoryEnumerate(readRegStrHistoryItemCallbackFunc, &readRegData);
		ret = readRegData.ret;
	}
	destroyRegReader(reader);
	return ret;
}

int writeRegStr(const char *prodName, const char *name, const char *str)
{
	RegReader reader;
	const char *dispName;
	int ret;

	if (!prodName)
		prodName = GetProductName();

	dispName = gdGetDisplayName(gdGetIDByName(prodName));

	reader = createRegReader();
	initRegReaderEx(reader, CRYPTIC_REG_KEY, dispName);
	ret = rrWriteString(reader, name, str);
	destroyRegReader(reader);
	return ret;
}

int readRegInt(const char *prodName, const char *name, unsigned int *out, bool useHistory)
{
	RegReader reader;
	const char *dispName;
	int ret;

	if (!prodName)
		prodName = GetProductName();

	dispName = gdGetDisplayName(gdGetIDByName(prodName));

	reader = createRegReader();
	if (!initRegReaderEx(reader, CRYPTIC_REG_KEY, dispName))
		return 0;
	ret = rrReadInt(reader, name, out);
	if (!ret && useHistory)
	{
		ReadRegIntData readRegData;
		readRegData.name = name;
		readRegData.out = out;
		readRegData.ret = ret;
		HistoryEnumerate(readRegIntHistoryItemCallbackFunc, &readRegData);
		ret = readRegData.ret;
	}
	destroyRegReader(reader);
	return ret;
}

int writeRegInt(const char *prodName, const char *name, unsigned int val)
{
	RegReader reader;
	const char *dispName;
	int ret;

	if (!prodName)
		prodName = GetProductName();

	dispName = gdGetDisplayName(gdGetIDByName(prodName));

	reader = createRegReader();
	if (!initRegReaderEx(reader, CRYPTIC_REG_KEY, dispName))
		return 0;
	ret = rrWriteInt(reader, name, val);
	destroyRegReader(reader);
	return ret;
}

bool RegistryBackedStrSet(const char *productName, const char *keyName, const char *value, bool bUseHistory)
{
	char oldValue[1024];
	if (readRegStr(productName, keyName, oldValue, sizeof(oldValue), bUseHistory))
	{
		// there was a value in the registry already
		if (strcmp_safe(oldValue, value) != 0)
		{
			// it is different, so write the change, and return that it was different.
			writeRegStr(productName, keyName, value);
			return true;
		}
	}
	else
	{
		// there was NOT a value in the registry already - write it
		writeRegStr(productName, keyName, value);
	}

	// return that it wasn't different
	return false;
}

// returns true if there was a value in the registry (different from default), false if default utilized
// set default in value before call
bool RegistryBackedStrGet(const char *productName, const char *keyName, char *value, int valueMaxLength, bool bUseHistory)
{
	char currValue[1024];
	if (readRegStr(productName, keyName, currValue, sizeof(currValue), bUseHistory))
	{
		if (strcmp(currValue, value) != 0)
		{
			// there was a value in the registry - return it
			strncpy_s(value, valueMaxLength, currValue, strlen(currValue));
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		// there was NOT a value in the registry already - write it
		writeRegStr(productName, keyName, value);
		return false;
	}
}

bool RegistryBackedIntSet(const char *productName, const char *keyName, int value, bool bUseHistory)
{
	int oldValue;
	if (readRegInt(productName, keyName, &oldValue, bUseHistory))
	{
		// there was a value in the registry already
		if (value != oldValue)
		{
			// it is different, so write the change, and return that it was different.
			writeRegInt(productName, keyName, value);
			return true;
		}
	}
	else
	{
		// there was NOT a value in the registry already - write it
		writeRegInt(productName, keyName, value);
	}

	// return that it wasn't different
	return false;
}

// returns true if there was a value in the registry (different from default), false if default utilized
// *pValue contains the default to return
bool RegistryBackedIntGet(const char *productName, const char *keyName, int *pValue, bool bUseHistory)
{
	int currValue;
	if (readRegInt(productName, keyName, &currValue, bUseHistory))
	{
		if (currValue != *pValue)
		{
			// there was a value in the registry - return it
			*pValue = currValue;
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		// there was NOT a value in the registry already - write it
		writeRegInt(productName, keyName, *pValue);
		return false;
	}
}

// returns true if changed, false if not
bool RegistryBackedBoolSet(const char *productName, const char *keyName, bool bValue, bool bUseHistory)
{
	return RegistryBackedIntSet(productName, keyName, (int)bValue, bUseHistory);
}

// returns true if there was a value in the registry (different from default), false if default utilized
// *pbValue contains the default to return
bool RegistryBackedBoolGet(const char *productName, const char *keyName, bool *pbValue, bool bUseHistory)
{
	int value = *pbValue;
	bool retVal = RegistryBackedIntGet(productName, keyName, &value, bUseHistory);
	*pbValue = (bool)value;
	return retVal;
}

