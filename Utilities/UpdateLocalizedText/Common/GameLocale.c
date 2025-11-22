#include "GameLocale.h"
#include <string.h>
#include "stdtypes.h"
#include "RegistryReader.h"

char* LocaleTable[] = {
	"English",		// Do not move this entry.  Index 0 is used as the default locale.
	"Test",
	"Chinese",
	"Korean",
	"Japanese"
};
#define DEFAULT_LOCALE_ID 0

char* locGetName(int localeID){
	// If the given locale ID runs off of the table, just return a default.
	// English is the default locale.
	if(!locIDIsValid(localeID))
		return DEFAULT_LOCALE_ID;

	return LocaleTable[localeID];
}

int locGetID(char* ID){
	int i;
	for(i = 0; i < ARRAY_SIZE(LocaleTable); i++){
		if(stricmp(LocaleTable[i], ID) == 0)
			return i;
	}

	return DEFAULT_LOCALE_ID;
}

int locGetMaxLocaleCount(){
	return ARRAY_SIZE(LocaleTable);
}

int locIDIsValid(int localeID){
	if(localeID > ARRAY_SIZE(LocaleTable) || localeID < 0)
		return 0;
	else
		return 1;
}

int locGetIDInRegistry(){
	int localeID;
	RegReader reader;

	// Get the locale setting from the registry.
	reader = createRegReader();
	initRegReader(reader, "HKEY_CURRENT_USER\\SOFTWARE\\Cryptic\\COH");
	rrReadInt(reader, "Locale", &localeID);
	destroyRegReader(reader);

	if(!locIDIsValid(localeID))
		return DEFAULT_LOCALE_ID;
	else
		return localeID;
}

void locSetIDInRegistry(int localeID){
	RegReader reader;

	// Get the locale setting from the registry.
	reader = createRegReader();
	initRegReader(reader, "HKEY_CURRENT_USER\\SOFTWARE\\Cryptic\\COH");
	rrWriteInt(reader, "Locale", localeID);
	destroyRegReader(reader);
}