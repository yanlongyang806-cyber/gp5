#pragma once

typedef enum enumZiteType
{
	ZITE_NORMAL,
	ZITE_SORTED_INTS,
	ZITE_COMMA_SEPARATED,
} enumZiteType;

void InvokeZoomedInTextEditor(char *pComment, char **ppInOutString, enumZiteType eType);