#include "TextFilter.h"

#include "EString.h"
#include "GlobalTypes.h"
#include "StashTable.h"
#include "error.h"
#include "textparser.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_STRUCT;
typedef struct FilterEntry
{
	char* strString;			AST(STRUCTPARAM)
	bool bIgnoreSplitTokens;	AST(NAME(IgnoreSplitTokens))
	bool bIgnore1337Coalesce;	AST(NAME(Ignore1337Coalesce))
} FilterEntry;
extern ParseTable parse_FilterEntry[];
#define TYPE_parse_FilterEntry FilterEntry

AUTO_STRUCT;
typedef struct FilterFile
{
	Language language;			AST(NAME(Language))
	FilterEntry** eaEntries;	AST(NAME(Entry))
} FilterFile;
extern ParseTable parse_FilterFile[];
#define TYPE_parse_FilterFile FilterFile

AUTO_STRUCT AST_FIXUPFUNC( fixupFilterFileList );
typedef struct FilterFileList
{
	FilterFile** eaFiles;	AST(NAME(Entries))
} FilterFileList;
extern ParseTable parse_FilterFileList[];
#define TYPE_parse_FilterFileList FilterFileList

TextParserResult fixupFilterFileList( FilterFileList* FilterFileList, enumTextParserFixupType eType, void *pExtraData )
{
	switch( eType ) {
		xcase FIXUPTYPE_POST_ALL_TEXT_READING_AND_INHERITANCE_DURING_LOADFILES:
			FOR_EACH_IN_EARRAY( FilterFileList->eaFiles, FilterFile, file ) {
				FOR_EACH_IN_EARRAY( file->eaEntries, FilterEntry, entry ) { 
					char* estr = NULL;

					tf_InternString( entry->strString, &estr );
					StructCopyString( &entry->strString, estr );

					estrDestroy( &estr );
				} FOR_EACH_END;
			} FOR_EACH_END; 
	}
	return 1;
}

void TextFilterAddToTrie( FilterFileList* FilterFileList, FilterTrieNode* pTrie )
{
	FOR_EACH_IN_EARRAY( FilterFileList->eaFiles, FilterFile, file ) {
		if( file->language == LANGUAGE_DEFAULT || langIsSupportedThisShard( file->language )) {
			FOR_EACH_IN_EARRAY( file->eaEntries, FilterEntry, entry ) {
				FilterTrieNode* node = tf_AddStringAlreadyNormalizedAndReduced( pTrie, entry->strString, true );
				if( node ) {
					node->ignoreSplitTokens = entry->bIgnoreSplitTokens;
					node->ignore1337Coalesce = entry->bIgnore1337Coalesce;
				}
			} FOR_EACH_END;
		}
	} FOR_EACH_END;
}

AUTO_STARTUP(AS_TextFilter) ASTRT_DEPS(AS_SupportedLanguages);
void TextFilterLoad(void)
{
	FilterFileList filterFileList = {0};

	loadstart_printf("Loading TextFilters...");

	if ( !s_ProfanityTrie ) {
		ParserLoadFiles( NULL, "defs/filters/Profanity/Profanity_All.def", "cebsnar.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, parse_FilterFileList, &filterFileList );
		s_ProfanityTrie = tf_Create();
		TextFilterAddToTrie( &filterFileList, s_ProfanityTrie );
		StructReset( parse_FilterFileList, &filterFileList );
	}

	if ( !s_RestrictedTrie ) {
		ParserLoadFiles( NULL, "defs/filters/ReservedNamesPartial.def", "reservednames.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, parse_FilterFileList, &filterFileList );
		s_RestrictedTrie = tf_Create();
		TextFilterAddToTrie( &filterFileList, s_RestrictedTrie );
		StructReset( parse_FilterFileList, &filterFileList );
	}

	if ( !s_DisallowedNameTrie ) {
		ParserLoadFiles( NULL, "defs/filters/ReservedNamesFull.def", "disallowednames.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, parse_FilterFileList, &filterFileList );
		s_DisallowedNameTrie = tf_Create();
		TextFilterAddToTrie( &filterFileList, s_DisallowedNameTrie );
		StructReset( parse_FilterFileList, &filterFileList );
	}

	loadend_printf( " done." );
}

void TextFilterReload(void)
{	
	tf_Free( s_ProfanityTrie );
	s_ProfanityTrie = NULL;
	tf_Free( s_RestrictedTrie );
	s_RestrictedTrie = NULL;
	tf_Free( s_DisallowedNameTrie );
	s_DisallowedNameTrie = NULL;

	TextFilterLoad();
}

#include "AutoGen/TextFilterCommon_c_ast.c"
