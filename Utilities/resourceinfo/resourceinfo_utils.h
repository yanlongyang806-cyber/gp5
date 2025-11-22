#pragma once
GCC_SYSTEM

// My own implementation of fileAlloc that doesn't drag in all the game's file insanity.
char *fileAlloc(char *fname, int *lenp);

const char *parse_resource_id(const char *str);
char *create_resource_from_type_and_file(const char *type, char *filedata, int filedatalen, int *resdatalen);
