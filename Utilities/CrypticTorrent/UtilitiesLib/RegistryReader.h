#ifndef REGISTRYREADER_H
#define REGISTRYREADER_H
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RegReaderImp *RegReader;

RegReader createRegReader(void);
void destroyRegReader(RegReader reader);
int initRegReader(RegReader reader, const char* key);
int initRegReaderEx(RegReader reader, const char* templateString, ...);
#define initRegReaderEx(reader, templateString, ...) initRegReaderEx(reader, templateString, __VA_ARGS__)

int rrReadString(RegReader reader, const char* valueName, char* outBuffer, int bufferSize);
int rrWriteString(RegReader reader, const char* valueName, const char* str);
int rrReadInt64(RegReader reader, const char* valueName, __int64* value);
int rrReadInt(RegReader reader, const char* valueName, unsigned int* value);
int rrWriteInt64(RegReader reader, const char* valueName, __int64 value);
int rrWriteInt(RegReader reader, const char* valueName, unsigned int value);
int rrFlush(RegReader reader);
int rrDelete(RegReader reader, const char* valueName);
int rrDeleteKey(RegReader reader, const char* subkey);

int rrClose(RegReader reader);

int rrEnumStrings(RegReader reader, int index, char* outName, int* inOutNameLen, char* outValue, int* inOutValueLen);

// This writes a single value without doing any CRT heap operations (used in crash reporting)
int registryWriteInt(const char *keyName, const char *valueName, unsigned int value);

int registryDeleteTree(const char *key); // Deletes a key and all of it's sub-keys and values

#ifdef __cplusplus
}  // extern "C"
#endif


#endif
