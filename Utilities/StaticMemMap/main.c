//#include <string.h>
#include "wininclude.h"
#include "winutil.h"
#include "file.h"
#include "utils.h"
#include "earray.h"
#include "StashTable.h"
#include "qsortG.h"

typedef struct StaticMapObject StaticMapObject;

AUTO_STRUCT;
typedef struct StaticMapSymbol
{
	char *symbol_name;				AST( STRUCTPARAM )
	StaticMapObject *object;		NO_AST
	U32 address;					NO_AST
	U32 data_size;					AST( NAME(Bytes) )
	bool is_static;					AST( NAME(IsStatic) )
} StaticMapSymbol;

AUTO_STRUCT;
typedef struct StaticMapObject
{
	char *object_name;				AST( STRUCTPARAM )
	U32 data_size;					AST( NAME(Bytes) )
	StaticMapSymbol **symbols;		AST( NAME(Symbol) )
} StaticMapObject;

AUTO_STRUCT;
typedef struct StaticMapSection
{
	U32 start_address;				AST( NAME(StartAddress) )
	U32 length;						AST( NAME(Bytes) )
	bool is_code;					AST( NAME(Code) )
	bool is_readonly;				AST( NAME(ReadOnly) )
	StaticMapObject **objects;		AST( NAME(Object) )
	StaticMapSymbol **symbols;		NO_AST
	StashTable object_hash;			NO_AST
} StaticMapSection;

AUTO_STRUCT;
typedef struct StaticMapData
{
	StaticMapSection **sections;	AST( NAME(Section) )
} StaticMapData;

#include "AutoGen/main_c_ast.c"

static StaticMapSection *createSection(StaticMapData *map_data, int section_idx, U32 start_address)
{
	StaticMapSection *section = StructCreate(parse_StaticMapSection);
	section->object_hash = stashTableCreateWithStringKeys(256, StashDefault);
	section->start_address = start_address;
	eaSet(&map_data->sections, section, section_idx);
	return section;
}

static void addSymbol(StaticMapSection *section, const char *object_name, const char *symbol_name, U32 address, bool is_static)
{
	StaticMapObject *object;
	StaticMapSymbol *map_symbol = StructCreate(parse_StaticMapSymbol);

	if (!stashFindPointer(section->object_hash, object_name, &object))
	{
		object = StructCreate(parse_StaticMapObject);
		object->object_name = StructAllocString(object_name);
		eaPush(&section->objects, object);
		stashAddPointer(section->object_hash, object->object_name, object, false);
	}

	map_symbol->symbol_name = StructAllocString(symbol_name);
	map_symbol->address = address;
	map_symbol->object = object;
	map_symbol->is_static = is_static;
	eaPush(&object->symbols, map_symbol);
	eaPush(&section->symbols, map_symbol);
}

static int cmpObjects(const StaticMapObject **po1, const StaticMapObject **po2)
{
	int t = (int)(*po2)->data_size - (int)(*po1)->data_size;
	if (t)
		return t;
	return stricmp((*po1)->object_name, (*po2)->object_name);
}

static int cmpSymbols(const StaticMapSymbol **ps1, const StaticMapSymbol **ps2)
{
	int t = (int)(*ps2)->data_size - (int)(*ps1)->data_size;
	if (t)
		return t;
	return stricmp((*ps1)->symbol_name, (*ps2)->symbol_name);
}

static int cmpSymbolsAddress(const StaticMapSymbol **ps1, const StaticMapSymbol **ps2)
{
	int t = (int)(*ps1)->address - (int)(*ps2)->address;
	return t;
}

int __stdcall WinMain(__in HINSTANCE hInstance, __in_opt HINSTANCE hPrevInstance, __in_opt LPSTR lpCmdLine, __in int nShowCmd)
{
	PROCESS_INFORMATION pi={0};
	STARTUPINFOA si={0};
	char buffer[2048], *args[64], *s, output_filename[MAX_PATH];
	int i, j, tokens;
	FILE *f;
	StaticMapData all_sections={0};
	StaticMapSection *section;
	enum { MODE_INIT, MODE_SECTIONS, MODE_PUBLIC_SYMBOLS, MODE_STATIC_SYMBOLS } mode = MODE_INIT;

	DO_AUTO_RUNS;

	fileAllPathsAbsolute(1);
	fileAutoDataDir();

	winRegisterMe("StaticMemMap", ".map");

	if (!lpCmdLine[0] || stricmp(lpCmdLine, "-register")==0)
		return 0;

	f = fopen(lpCmdLine, "rt");
	if (!f)
		return 0;

	// section 0 is a dummy linker section
	createSection(&all_sections, 0, 0);

	while (fgets(buffer, sizeof(buffer), f))
	{
		int len = (int)strlen(buffer);
		if (buffer[len-1] == '\n')
			buffer[--len] = 0;

		tokens = tokenize_line_safe(buffer, args, ARRAY_SIZE(args), NULL);
		if (!tokens)
		{
		}
		else if (mode == MODE_INIT)
		{
			if (strcmp(args[0], "Start") == 0)
			{
				mode = MODE_SECTIONS;
			}
		}
		else if (mode == MODE_SECTIONS)
		{
			if (strcmp(args[0], "Address") == 0)
			{
				mode = MODE_PUBLIC_SYMBOLS;
			}
			else if (tokens == 4)
			{
				// section:start length name class
				int section_idx;
				U32 address, size;
				s = strchr(args[0], ':');
				assert(s);
				*s = 0;
				s++;
				section_idx = atoi(args[0]);
				sscanf(s, "%x", &address);
				sscanf(args[1], "%xH", &size);

				section = eaGet(&all_sections.sections, section_idx);

				if (!section)
					section = createSection(&all_sections, section_idx, address);

				if (strcmp(args[3], "CODE")==0)
					section->is_code = true;
				if (args[2][1] == 'r')
					section->is_readonly = true;

				section->length += size;
			}
		}
		else if (mode == MODE_PUBLIC_SYMBOLS || mode == MODE_STATIC_SYMBOLS)
		{
			if (mode == MODE_PUBLIC_SYMBOLS && strcmp(args[0], "entry")==0)
			{
			}
			else if (mode == MODE_PUBLIC_SYMBOLS && strcmp(args[0], "Static") == 0)
			{
				mode = MODE_STATIC_SYMBOLS;
			}
			else if (tokens == 4 || tokens == 5)
			{
				// section:address symbol rva+base lib
				int section_idx;
				U32 address;
				s = strchr(args[0], ':');
				assert(s);
				*s = 0;
				s++;
				section_idx = atoi(args[0]);
				sscanf(s, "%x", &address);

				section = eaGet(&all_sections.sections, section_idx);
				addSymbol(section, args[tokens-1], args[1], address, mode==MODE_STATIC_SYMBOLS);
			}
		}
	}

	fclose(f);

	for (i = 0; i < eaSize(&all_sections.sections); ++i)
	{
		// sort symbols by address and calculate sizes
		if (eaSize(&all_sections.sections[i]->symbols))
		{
			eaQSortG(all_sections.sections[i]->symbols, cmpSymbolsAddress);
			for (j = 1; j < eaSize(&all_sections.sections[i]->symbols); ++j)
			{
				all_sections.sections[i]->symbols[j-1]->data_size = all_sections.sections[i]->symbols[j]->address - all_sections.sections[i]->symbols[j-1]->address;
				all_sections.sections[i]->symbols[j-1]->object->data_size += all_sections.sections[i]->symbols[j-1]->data_size;
			}
			if (all_sections.sections[i]->length > all_sections.sections[i]->symbols[j-1]->address)
				all_sections.sections[i]->symbols[j-1]->data_size = all_sections.sections[i]->length - all_sections.sections[i]->symbols[j-1]->address;
			all_sections.sections[i]->symbols[j-1]->object->data_size += all_sections.sections[i]->symbols[j-1]->data_size;
			eaDestroy(&all_sections.sections[i]->symbols);
		}

		// sort by size for writing to file
		eaQSortG(all_sections.sections[i]->objects, cmpObjects);
		for (j = 0; j < eaSize(&all_sections.sections[i]->objects); ++j)
		{
			eaQSortG(all_sections.sections[i]->objects[j]->symbols, cmpSymbols);
		}
		stashTableDestroy(all_sections.sections[i]->object_hash);
	}

	changeFileExt(lpCmdLine, ".smap", output_filename);
	ParserWriteTextFile(output_filename, parse_StaticMapData, &all_sections, 0, 0);
	StructReset(parse_StaticMapData, &all_sections);

	return 0;
}
