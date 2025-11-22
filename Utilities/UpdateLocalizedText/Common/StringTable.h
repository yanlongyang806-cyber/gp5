/* File StringTable.h
 *	A string table is basically a buffer space that holds tightly packed string.  The table
 *	grows in large memory chunks whenever it runs out of space to store more strings.
 *
 *	Create and using a string table:
 *		First, call the createStringTable function to get a valid StringTable handle.  Then
 *		call the initStringTable function with the handle and the desired "chunkSize".  This
 *		size tells the string table how much memory to allocate everytime the string table
 *		runs out of space to hold the string being inserted.
 *
 *	Inserting strings into the string table:
 *		Call the strTableAddString function with a the handle of the table to add the string
 *		to and the string to be added.  The index of the string will be returned.
 *
 *	Making a string table indexable:
 *		Call the strTableSetMode function with the "Indexable" mode value before any strings
 *		are inserted.  If there are already some strings present in the table, attempts to
 *		swtich into the "indexable" mode will fail.
 *
 */

#ifndef STRINGTABLE_H
#define STRINGTABLE_H

typedef long StringTable;
typedef int (*StringProcessor)(char*);

/* Enum StringTableMode
 *	Defines several possible HashTable operation modes.
 *	
 *	Default:
 *		Makes the table non-indexable.
 *
 *	Indexable:
 *		Allow all the strings in the table to be accessed via some index.
 *		The index reflects the order of string insertion into the table and
 *		will be static over the lifetime of the StringTable.
 *
 *		Making the strings indexable requires that the string table use 
 *		additional memory to keep track of the index-to-string relationship.  
 *		Currently, this overhead is at 4 bytes per string.
 */
typedef enum{
	Indexable =				1,
	WideString =			2
} StrinTableMode;


// Constructor + Destructor
StringTable createStringTable();
void initStringTable(StringTable table, unsigned int chunkSize);
void destroyStringTable(StringTable table);


const void* strTableAddString(StringTable table, const void* str);
void strTableClear(StringTable table);

// String enumeration
const char* strTableGetString(StringTable table, int i);
void strTableForEachString(StringTable table, StringProcessor processor);


// StringTable mode query/alteration
StrinTableMode strTableGetMode(StringTable table);
int strTableSetMode(StringTable table, StrinTableMode mode);

void testStringTable();


#endif