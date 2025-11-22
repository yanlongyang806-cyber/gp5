#ifndef REGISTRYREADER_H
#define REGISTRYREADER_H

typedef long RegReader;

RegReader createRegReader();
void destroyRegReader(RegReader reader);
int initRegReader(RegReader reader, char* key);
int initRegReaderEx(RegReader reader, char* templateString, ...);

int rrReadString(RegReader reader, char* valueName, char* outBuffer, int bufferSize);
int rrWriteString(RegReader reader, char* valueName, char* str);
int rrReadInt(RegReader reader, char* valueName, unsigned int* value);
int rrWriteInt(RegReader reader, char* valueName, unsigned int value);
int rrFlush(RegReader reader);
int rrDelete(RegReader reader, char* valueName);

int rrClose(RegReader reader);
#endif