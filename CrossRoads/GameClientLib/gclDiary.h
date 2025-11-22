/***************************************************************************



***************************************************************************/

#pragma once
GCC_SYSTEM

#include "DiaryEnums.h"

AUTO_STRUCT;
typedef struct TagListRow
{
	int bitNum;
	bool permanent;
	STRING_POOLED String;				AST(POOL_STRING)
} TagListRow;

AUTO_STRUCT;
typedef struct DiaryEntryTypeItem
{
	const char *message;
	int value;
} DiaryEntryTypeItem;