/***************************************************************************



***************************************************************************/

#pragma once
GCC_SYSTEM

AUTO_STRUCT;
typedef struct ActivityLogDisplayEntry
{
	U32 entryID;			AST(KEY)
	U32 time;
	char *text;				AST(ESTRING)
} ActivityLogDisplayEntry;