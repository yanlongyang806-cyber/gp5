#include "RegistryReader.h"
#include <wininclude.h>
#include <stdlib.h>
#include <stdio.h>

#pragma warning(push)
#pragma warning(disable:4028) // parameter differs from declaration
#pragma warning(disable:4024) // related
#pragma warning(disable:4022) // related

#pragma warning(disable:4047) // return type differs in lvl of indirection warning
#pragma warning(disable:4002) // too manay macro parameters warning

typedef struct{
	HKEY key;
	unsigned int keyOpened;
} RegReaderImp;

RegReader createRegReader(){
	return calloc(1, sizeof(RegReaderImp));
}

void destroyRegReader(RegReaderImp* reader){
	rrClose(reader);
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


int initRegReader(RegReaderImp* reader, char* keyName){
	PredefinedKey* predefKey;

	// Seperate the predefined key name from the rest of the the key name.
	int matchedKnownKey = 0;
	int predefKeyNameLen;
	int regCreateResult;
	
	// Look through all of known predefined keys.
	for(predefKey = predefinedKeys; predefKey < predefinedKeys + sizeof(predefinedKeys); predefKey++){
		
		// Compare each predefined key names to the beginning of the key string.
		// If they match, we've found the correct predefined key to be used to open the given key.
		predefKeyNameLen = strlen(predefKey->keyName);
		if(0 == strnicmp(predefKey->keyName, keyName, predefKeyNameLen)){
			matchedKnownKey = 1;
			break;
		}
	}
	
	if(!matchedKnownKey)
		return 0;
	
	regCreateResult = RegCreateKeyEx(
		predefKey->key,					// handle to open key
		keyName + predefKeyNameLen + 1,	// subkey name
		0,								// reserved
		NULL,							// class string
		REG_OPTION_NON_VOLATILE,		// special options
		KEY_READ | KEY_WRITE ,			// desired security access
		NULL,							// inheritance
		&reader->key,					// key handle 
		NULL							// disposition value buffer
		);
	
	if(ERROR_SUCCESS != regCreateResult)
		return 0;

	reader->keyOpened = 1;
	return 1;
}

int initRegReaderEx(RegReaderImp* reader, char* templateString, ...){
	va_list va;
	char buffer[1024];

	va_start(va, templateString);
	vsprintf(buffer, templateString, va);
	va_end(va);

	return initRegReader(reader, buffer);
}

int rrReadString(RegReaderImp* reader, char* valueName, char* outBuffer, int bufferSize){
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
	if(REG_SZ != valueType){
		return 0;
	}else{
		outBuffer[bufferSize] = '\0';
		return 1;
	}
}

int rrWriteString(RegReaderImp* reader, char* valueName, char* str){
	int regSetResult;
	if(!reader->keyOpened)
		return 0;

	regSetResult = RegSetValueEx(
		reader->key,	// handle to key
		valueName,		// value name
		0,				// reserved
		REG_SZ,			// value type
		str,			// value data
		strlen(str)		// size of value data
		);

	if(ERROR_SUCCESS != regSetResult)
		return 0;

	return 1;
}

int rrReadInt(RegReaderImp* reader, char* valueName, unsigned int* value){
	DWORD valueType;
	int valueSize;
	int regQueryResult;

	if(!reader->keyOpened || !value)
		return 0;
	
	valueSize = sizeof(int);

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

int rrWriteInt(RegReaderImp* reader, char* valueName, unsigned int value){
	int regSetResult;
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

int rrFlush(RegReaderImp* reader){
	if(!reader->keyOpened)
		return 0;

	if(ERROR_SUCCESS != RegFlushKey(reader->key))
		return 0;

	return 1;
}

int rrDelete(RegReaderImp* reader, char* valueName){
	if(!reader->keyOpened)
		return 0;

	return RegDeleteValue(reader->key, valueName);
}

int rrClose(RegReaderImp* reader){
	if(reader->keyOpened){
		RegCloseKey(reader->key);
		reader->keyOpened = 0;
	}

	return 1;
}
#pragma warning(pop)