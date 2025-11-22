#if !PLATFORM_CONSOLE

#include "RegistryReader.h"
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct RegReaderImp{
	HKEY key;
	unsigned int keyOpened;
	unsigned int keyExists;
	char *keyName;
} RegReaderImp;

RegReader createRegReader(void){
	return calloc(1, sizeof(RegReaderImp));
}

void destroyRegReader(RegReaderImp* reader){
	rrClose(reader);
	if (reader->keyName) {
		free(reader->keyName);
	}
	free(reader);
}




typedef struct{
	char* keyName;
	HKEY key;
} PredefinedKey;

PredefinedKey predefinedKeys[] = {
	{"HKEY_CLASSES_ROOT", HKEY_CLASSES_ROOT},
	{"HKEY_CURRENT_CONFIG", HKEY_CURRENT_CONFIG},
	{"HKEY_CURRENT_USER", HKEY_CURRENT_USER},
	{"HKEY_LOCAL_MACHINE", HKEY_LOCAL_MACHINE},
	{"HKEY_USERS", HKEY_USERS},
};


PredefinedKey *findPredefinedKey(const char *keyName)
{
	PredefinedKey *predefKey;
	int	predefKeyNameLen;
	int i;

	// Look through all of known predefined keys.
	for(i = 0; i < sizeof(predefinedKeys)/sizeof(*predefinedKeys); i++){

		// Compare each predefined key names to the beginning of the key string.
		// If they match, we've found the correct predefined key to be used to open the given key.
		predefKey = &predefinedKeys[i];

		// Compare each predefined key names to the beginning of the key string.
		// If they match, we've found the correct predefined key to be used to open the given key.
		predefKeyNameLen = (int)strlen(predefKey->keyName);
		if(0 == strnicmp(predefKey->keyName, keyName, predefKeyNameLen))
			return predefKey;
	}

	return NULL;
}


int initRegReader(RegReaderImp* reader, const char* keyName){
	PredefinedKey* predefKey;

	// Seperate the predefined key name from the rest of the the key name.
	int predefKeyNameLen = 0;
	int regCreateResult;

	predefKey = findPredefinedKey(keyName);
	if (!predefKey)
		return 0;

	predefKeyNameLen = (int)strlen(predefKey->keyName);
	regCreateResult = RegOpenKeyEx(
		predefKey->key,
		keyName + predefKeyNameLen + 1,
		0,
		KEY_READ | KEY_WRITE | DELETE,
		&reader->key);

	if (ERROR_SUCCESS != regCreateResult) {
		reader->keyExists = 0;
		// It will be opened for writing at a later time!
		reader->keyName = strdup(keyName);
		return 1;
	}

	reader->keyExists = 1;
	reader->keyOpened = 1;
	
	return 1;
}

int rrLazyWriteInit(RegReaderImp* reader)
{
	PredefinedKey* predefKey;

	// Seperate the predefined key name from the rest of the the key name.
	int matchedKnownKey = 0;
	int predefKeyNameLen = 0;
	int regCreateResult;

	if (!reader->keyExists && reader->keyName)
	{
		predefKey = findPredefinedKey(reader->keyName);

		if (predefKey == 0)
			return 0;

		predefKeyNameLen = (int)strlen(predefKey->keyName);
		regCreateResult = RegCreateKeyEx(
			predefKey->key,					// handle to open key
			reader->keyName + predefKeyNameLen + 1,	// subkey name
			0,								// reserved
			NULL,							// class string
			REG_OPTION_NON_VOLATILE,		// special options
			KEY_READ | KEY_WRITE | DELETE,			// desired security access
			NULL,							// inheritance
			&reader->key,					// key handle 
			NULL							// disposition value buffer
			);
		
		if(ERROR_SUCCESS != regCreateResult)
			return 0;

		reader->keyOpened = 1;
		return 1;
	}
	return 1;
}

#undef initRegReaderEx
int initRegReaderEx(RegReaderImp* reader, const char* templateString, ...){
	va_list va;
	char buffer[1024];

	va_start(va, templateString);
	vsprintf(buffer, templateString, va);
	va_end(va);

	return initRegReader(reader, buffer);
}

int rrReadString(RegReaderImp* reader, const char* valueName, char* outBuffer, int bufferSize){
	DWORD valueType;
	int regQueryResult;

	if(!reader->keyOpened)
		return 0;
	
	regQueryResult = RegQueryValueEx(
		reader->key,	// handle to key
		valueName,		// value name
		NULL,			// reserved
		&valueType,		// type buffer
		outBuffer,		// data buffer
		&bufferSize		// size of data buffer
		);
	
	// Does the value exist?
	if(ERROR_SUCCESS != regQueryResult){
		return 0;
	}
	
	// If the stored value is not a string, it is not possible to retrieve it.
	if(REG_SZ != valueType && REG_EXPAND_SZ != valueType){
		return 0;
	}else{
		outBuffer[bufferSize] = '\0';
		return 1;
	}
}

int rrWriteString(RegReaderImp* reader, const char* valueName, const char* str){
	char outBuffer[2048];
	int bufferSize = sizeof(outBuffer)/sizeof(outBuffer);
	DWORD valueType;
	int regSetResult;
	int regQueryResult;
	DWORD type = REG_SZ;

	rrLazyWriteInit(reader);

	if(!reader->keyOpened)
		return 0;

	regQueryResult = RegQueryValueEx(
		reader->key,	// handle to key
		valueName,		// value name
		NULL,			// reserved
		&valueType,		// type buffer
		outBuffer,		// data buffer
		&bufferSize		// size of data buffer
		);

	// Does the value exist?
	if(ERROR_SUCCESS == regQueryResult) {
		if (valueType == REG_EXPAND_SZ)
			type = REG_EXPAND_SZ; // Maintain this type!
	}

	regSetResult = RegSetValueEx(
		reader->key,	// handle to key
		valueName,		// value name
		0,				// reserved
		type,			// value type
		str,			// value data
		(int)strlen(str)		// size of value data
		);

	if(ERROR_SUCCESS != regSetResult)
		return 0;

	return 1;
}

int rrReadInt(RegReaderImp* reader, const char* valueName, unsigned int* value){
	DWORD valueType;
	int valueSize;
	int regQueryResult;

	if (!value)
		return 0;
	if(!reader->keyOpened) {
		*value = 0;
		return 0;
	}
	
	valueSize = sizeof(*value);

	regQueryResult = RegQueryValueEx(
		reader->key,	// handle to key
		valueName,		// value name
		NULL,			// reserved
		&valueType,		// type buffer
		(unsigned char*)value,	// data buffer
		&valueSize				// size of data buffer
		);
	
	// Does the value exist?
	if(ERROR_SUCCESS != regQueryResult){
		return 0;
	}
	
	// If the stored value is not a string, it is not possible to retrieve it.
	if(REG_DWORD != valueType){
		return 0;
	}else
		return 1;

}

int rrReadInt64(RegReaderImp* reader, const char* valueName, __int64* value){
	DWORD valueType;
	int valueSize;
	int regQueryResult;

	if (!value)
		return 0;
	if(!reader->keyOpened) {
		*value = 0;
		return 0;
	}

	valueSize = sizeof(*value);

	regQueryResult = RegQueryValueEx(
		reader->key,	// handle to key
		valueName,		// value name
		NULL,			// reserved
		&valueType,		// type buffer
		(unsigned char*)value,	// data buffer
		&valueSize				// size of data buffer
		);

	// Does the value exist?
	if(ERROR_SUCCESS != regQueryResult){
		return 0;
	}

	// If the stored value is not a string, it is not possible to retrieve it.
	if(REG_QWORD != valueType){
		return 0;
	}else
		return 1;

}

int rrWriteInt(RegReaderImp* reader, const char* valueName, unsigned int value){
	int regSetResult;

	rrLazyWriteInit(reader);

	if(!reader->keyOpened || !valueName)
		return 0;

	regSetResult = RegSetValueEx(
		reader->key,	// handle to key
		valueName,		// value name
		0,				// reserved
		REG_DWORD,		// value type
		(unsigned char*)&value,			// value data
		sizeof(unsigned int)	// size of value data
		);

	if(ERROR_SUCCESS != regSetResult)
		return 0;

	return 1;
}


int rrWriteInt64(RegReaderImp* reader, const char* valueName, __int64 value){
	int regSetResult;

	rrLazyWriteInit(reader);

	if(!reader->keyOpened || !valueName)
		return 0;

	regSetResult = RegSetValueEx(
		reader->key,	// handle to key
		valueName,		// value name
		0,				// reserved
		REG_QWORD,		// value type
		(unsigned char*)&value,			// value data
		sizeof(value)	// size of value data
		);

	if(ERROR_SUCCESS != regSetResult)
		return 0;

	return 1;
}


int rrFlush(RegReaderImp* reader){
	if(!reader->keyOpened)
		return 0;

	if(ERROR_SUCCESS != RegFlushKey(reader->key))
		return 0;

	return 1;
}

int rrDelete(RegReaderImp* reader, const char* valueName){
	if(!reader->keyOpened)
		return 0;

	return RegDeleteValue(reader->key, valueName);
}

int rrDeleteKey(RegReader reader, const char* subkey){
	if(!reader->keyOpened)
		return 0;

	return RegDeleteKey(reader->key, subkey);
}

int rrClose(RegReaderImp* reader){
	if(reader->keyOpened){
		RegCloseKey(reader->key);
		reader->keyOpened = 0;
	}

	return 1;
}


// rrEnumStrings
//
// Return values:
//	<0: Index is too high, you're done.
//  0:	Check *inOutNameLen:
//		>= 0:	Not a string value.
//		<0:		Buffer overflow.
//  >0:	A string value.

int rrEnumStrings(RegReaderImp* reader, int index, char* outName, int* inOutNameLen, char* outValue, int* inOutValueLen){
	DWORD valueType;
	DWORD retVal;
	
	if(!reader->keyOpened){
		return 0;
	}
	
	retVal = RegEnumValue(reader->key, index, outName, inOutNameLen, NULL, &valueType, outValue, inOutValueLen);

	switch(retVal){
		case ERROR_SUCCESS:{
			if(	valueType == REG_SZ ||
				valueType == REG_EXPAND_SZ)
			{
				outName[*inOutNameLen] = 0;
				outValue[*inOutValueLen] = 0;
				
				if(stricmp(outName, "(default)")){
					return 1;
				}
			}
			
			return 0;
		}
		break;
		case ERROR_NO_MORE_ITEMS:{
			return -1;
		}
		break;
		default:{
			*inOutNameLen = -1;
			
			return 0;
		}
	}
}



// From MSDN:
//*************************************************************
//
//  RegDelnodeRecurse()
//
//  Purpose:    Deletes a registry key and all it's subkeys / values.
//
//  Parameters: hKeyRoot    -   Root key
//              lpSubKey    -   SubKey to delete
//
//  Return:     TRUE if successful.
//              FALSE if an error occurs.
//
//*************************************************************
static BOOL RegDelnodeRecurse (HKEY hKeyRoot, char *lpSubKey)
{
	LPTSTR lpEnd;
	LONG lResult;
	DWORD dwSize;
	TCHAR szName[MAX_PATH];
	HKEY hKey;
	FILETIME ftWrite;

	// First, see if we can delete the key without having
	// to recurse.

	lResult = RegDeleteKey(hKeyRoot, lpSubKey);

	if (lResult == ERROR_SUCCESS) 
		return TRUE;

	lResult = RegOpenKeyEx (hKeyRoot, lpSubKey, 0, KEY_READ, &hKey);

	if (lResult != ERROR_SUCCESS) 
	{
		if (lResult == ERROR_FILE_NOT_FOUND) {
			//printf("Key not found.\n");
			return TRUE;
		} 
		else {
			//printf("Error opening key.\n");
			return FALSE;
		}
	}

	// Check for an ending slash and add one if it is missing.

	lpEnd = lpSubKey + lstrlen(lpSubKey);

	if (*(lpEnd - 1) != TEXT('\\')) 
	{
		*lpEnd =  TEXT('\\');
		lpEnd++;
		*lpEnd =  TEXT('\0');
	}

	// Enumerate the keys

	dwSize = MAX_PATH;
	lResult = RegEnumKeyEx(hKey, 0, szName, &dwSize, NULL,
		NULL, NULL, &ftWrite);

	if (lResult == ERROR_SUCCESS) 
	{
		do {

			strcpy_s(lpEnd, MAX_PATH*2 - (lpEnd - lpSubKey), szName);

			if (!RegDelnodeRecurse(hKeyRoot, lpSubKey)) {
				break;
			}

			dwSize = MAX_PATH;

			lResult = RegEnumKeyEx(hKey, 0, szName, &dwSize, NULL,
				NULL, NULL, &ftWrite);

		} while (lResult == ERROR_SUCCESS);
	}

	lpEnd--;
	*lpEnd = TEXT('\0');

	RegCloseKey (hKey);

	// Try again to delete the key.

	lResult = RegDeleteKey(hKeyRoot, lpSubKey);

	if (lResult == ERROR_SUCCESS) 
		return TRUE;

	return FALSE;
}
static BOOL RegDelnode (HKEY hKeyRoot, const char *lpSubKey)
{
	char szDelKey[2 * MAX_PATH];

	strcpy(szDelKey, lpSubKey);
	return RegDelnodeRecurse(hKeyRoot, szDelKey);

}

int registryDeleteTree(const char *keyName) // Deletes a key and all of it's sub-keys and values
{
	int predefKeyNameLen;
	const char *subkey;
	PredefinedKey *predefKey = findPredefinedKey(keyName);
	if (!predefKey)
		return 0;

	predefKeyNameLen = (int)strlen(predefKey->keyName);
	subkey = keyName + predefKeyNameLen + 1;

	return RegDelnode(predefKey->key, subkey);
}


int registryWriteInt(const char *keyName, const char *valueName, unsigned int value)
{
	HKEY key;
	PredefinedKey* predefKey;

	// Separate the predefined key name from the rest of the the key name.
	int predefKeyNameLen = 0;
	int result;

	predefKey = findPredefinedKey(keyName);
	if (predefKey == 0)
		return 0;

	predefKeyNameLen = (int)strlen(predefKey->keyName);
	result = RegCreateKeyEx(
		predefKey->key,					// handle to open key
		keyName + predefKeyNameLen + 1,	// subkey name
		0,								// reserved
		NULL,							// class string
		REG_OPTION_NON_VOLATILE,		// special options
		KEY_READ | KEY_WRITE ,			// desired security access
		NULL,							// inheritance
		&key,							// key handle 
		NULL							// disposition value buffer
		);

	if(ERROR_SUCCESS != result)
		return 0;

	result = RegSetValueEx(
		key,	// handle to key
		valueName,		// value name
		0,				// reserved
		REG_DWORD,		// value type
		(unsigned char*)&value,			// value data
		sizeof(unsigned int)	// size of value data
		);

	if(ERROR_SUCCESS != result) {
		RegCloseKey(key);
		return 0;
	}

	RegCloseKey(key);

	return 1;

}

#else

// Stubbed out for now. Should be a wrapper around the xbox registry equivalent

#include "RegistryReader.h"

RegReader createRegReader(void)
{
	return NULL;
}
void destroyRegReader(RegReader reader)
{

}

int initRegReader(RegReader reader, const char* key)
{
	return 0;
}
#undef initRegReaderEx
int initRegReaderEx(RegReader reader, const char* templateString, ...)
{
	return 0;
}

int rrReadString(RegReader reader, const char* valueName, char* outBuffer, int bufferSize)
{
	return 0;
}
int rrWriteString(RegReader reader, const char* valueName, const char* str)
{
	return 0;
}
int rrReadInt64(RegReader reader, const char* valueName, S64* value)
{
	return 0;
}
int rrReadInt(RegReader reader, const char* valueName, unsigned int* value)
{
	return 0;
}
int rrWriteInt64(RegReader reader, const char* valueName, S64 value)
{
	return 0;
}
int rrWriteInt(RegReader reader, const char* valueName, unsigned int value)
{
	return 0;
}
int rrFlush(RegReader reader)
{
	return 0;
}
int rrDelete(RegReader reader, const char* valueName)
{
	return 0;
}

int rrClose(RegReader reader)
{
	return 0;
}

int rrEnumStrings(RegReader reader, int index, char* outName, int* inOutNameLen, char* outValue, int* inOutValueLen)
{
	return 0;
}
int registryWriteInt(const char *keyName, const char *valueName, unsigned int value)
{
	return 0;
}

#endif
