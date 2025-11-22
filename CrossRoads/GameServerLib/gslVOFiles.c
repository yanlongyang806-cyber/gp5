// Remaining tasks:
//
// 6. Reload support
#include "gslVOFiles.h"

#include "AppLocale.h"
#include "ResourceManager.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "contact_common.h"
#include "error.h"
#include "file.h"
#include "message.h"
#include "mission_common.h"
#include "textparser.h"
#include "utilitiesLib.h"
#include "utils.h"

typedef struct VODisplayString VODisplayString;
typedef struct VORefTextOnly VORefTextOnly;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

/// Stores the data mapping a message key to an audio event.
AUTO_STRUCT AST_FIXUPFUNC( gslVOReference_Fixup );
typedef struct VOReference
{
	// The message key
	const char* astrKey;			AST( KEY POOL_STRING )

	// Filename -- currently always "translations/Everything.vo"
	//
	// Aug/10/2013 -- This is technically unneeded, but consumes an
	// extremely small amount of space.  ~4k on Neverwinter, ~7k on
	// STO.
 	const char* filename;			AST( CURRENTFILE )

	// The name of the audio event
	const char* astrAudioEvent;		AST( NAME(AudioEvent) POOL_STRING )

	// Extra metadata not needed at runtime
	VORefTextOnly* textOnly;		AST( NAME(TextOnly) )
} VOReference;
extern ParseTable parse_VOReference[];
#define TYPE_parse_VOReference VOReference

/// Stores all metadata about a VOReference that is not needed at
/// runtime.
///
/// This data is a mix of auto-deduced and user-editable data.
AUTO_STRUCT;
typedef struct VORefTextOnly
{
	// Per-language data (user editable, auto-deduced default)
	VODisplayString** eaText;		AST( NAME(Text) )

	// The source wave file name for English. (user editable)
	char* strWaveFileName;			AST( NAME(WaveFileName) )

	// The mission this is for (auto-deduced)
	const char* astrMissionName;	AST( NAME(MissionName) POOL_STRING )

	// The contact saying this (auto-deduced if bOnlyOneContactPerMission is set, user editable otherwise)
	const char* astrContactName;	AST( NAME(ContactName) POOL_STRING )

	// DisplayName from the contact saying this (auto-deduced if bOnlyOneContactPerMission is set, user editable otherwise)
	char* strContactDisplayName;	AST( NAME(ContactDisplayName) )

	// VO Comments from the contact file (auto-deduced if bOnlyOneContactPerMission is set, user editable otherwise)
	char* strContactVOComments;		AST( NAME(ContactVOComments) )
} VORefTextOnly;
extern ParseTable parse_VORefTextOnly[];
#define TYPE_parse_VORefTextOnly VORefTextOnly

/// The text that is read, stored per-language
AUTO_STRUCT;
typedef struct VODisplayString
{
	// The language of this string
	Language eLang;					AST( NAME(Lang) )

	// The text used by VO
	char* strVOText;				AST( NAME(VOText) )
} VODisplayString;
extern ParseTable parse_VODisplayString[];
#define TYPE_parse_VODisplayString VODisplayString

/// Extra struct used for writing out Everything.vo
AUTO_STRUCT AST_STARTTOK( "" ) AST_ENDTOK( "\n" );
typedef struct VOReferenceFile
{
	VOReference** eaVORefs;
} VOReferenceFile;
extern ParseTable parse_VOReferenceFile[];
#define TYPE_parse_VOReferenceFile VOReferenceFile

/// Metadata that is kept on a stack.  Things like the related
/// mission, the related contact, etc. go here.
AUTO_STRUCT;
typedef struct VOContext
{
	const char* strFilename;				AST( UNOWNED )
	const char* astrMissionName;			AST( UNOWNED )
	const char* astrContactName;			AST( UNOWNED )
} VOContext;
extern ParseTable parse_VOContext[];
#define TYPE_parse_VOContext VOContext

/// The dictionary of all VOReferences.  Uses message keys as their
/// key.
DictionaryHandle g_VODictionary = NULL;

/// Stack of VOContexts.  Should be empty at the top level
VOContext** g_eaVOContext = NULL;

static void gslAccumVOReferenceForMissionDef( VOReference*** peaVORef, MissionDef* missionDef );
static void gslAccumVOReferenceForContactDef( VOReference*** peaVORef, ContactDef* contactDef );
static void gslAccumVOReferenceForMissionOffer( VOReference*** peaVORef, ContactMissionOffer* missionOffer );
static void gslAccumVOReferenceForDialogBlock( VOReference*** peaVORef, DialogBlock* dialogBlock );
static void gslAccumVOReferenceForGameAction( VOReference*** peaVORef, WorldGameActionProperties* gameAction );
static void gslAccumVOReferenceForDisplayMessageWithVO( VOReference*** peaVORef, DisplayMessageWithVO* msg );
static VOContext* gslVOContextPush( void );
static void gslVOContextPop( void );
static Language gslVOLocGetLanguage( LocaleID loc );

static bool gslVOWriteCSVFile( VOReference** eaVORef, LocaleID locale, const char* filename );
static bool gslVOReadAndMergeCSVFile( VOReference*** out_peaVORef, LocaleID* out_locale, const char* filename );
static VODisplayString* gslVOReferenceGetDisplayString( VOReference* voRef, Language lang );

int gslVOReference_Validate( enumResourceValidateType eType, const char* pcDictName, const char* pcResourceName, VOReference* voRef, U32 userID )
{
	switch( eType ) {
		xcase RESVALIDATE_CHECK_REFERENCES: {
			return VALIDATE_HANDLED;
		}
	}

	return VALIDATE_NOT_HANDLED;
}

TextParserResult gslVOReference_Fixup( VOReference* voRef, enumTextParserFixupType eType, void* pExtraData )
{
	switch( eType ) {
		xcase FIXUPTYPE_POST_BIN_READ: {
			if( isProductionMode() ) {
				StructDestroySafe( parse_VORefTextOnly, &voRef->textOnly );
			}
		}
	}

	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void gslVORegisterDictionary( void )
{
	g_VODictionary = RefSystem_RegisterSelfDefiningDictionary( "VO", false, parse_VOReference, true, false, NULL );
	resDictManageValidation( g_VODictionary, gslVOReference_Validate );
	resDictSetUseExtendedName( g_VODictionary, true );
}

AUTO_STARTUP( VO );
void gslVOLoadDictionary( void )
{
	loadstart_printf( "Loading VO..." );
	resLoadResourcesFromDisk( g_VODictionary, "translations/", ".vo", NULL, PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );
	loadend_printf( " done." );
}

/// Debug command to make .vo.txt files as if the builder was running.
/// Should only be run in dev mode for testing.
AUTO_COMMAND ACMD_NAME( MakeVOTXTFiles );
void gslMakeVOTXTFilesCommand( void )
{
	// Hardcoded to English, French, German for testing
	eaiClear( &giAllLocalesToMakeAndExit );
	eaiPush( &giAllLocalesToMakeAndExit, LOCALE_ID_ENGLISH );

	gslMakeVOTXTFiles();
}

void gslMakeVOTXTFiles( void )
{
	VOReference** eaVORefs = NULL;
	assert( eaSize( &g_eaVOContext ) == 0 );

	eaIndexedEnable( &eaVORefs, parse_VOReference );

	// Find all VO on missions, contacts
	{
		FOR_EACH_IN_REFDICT( g_MissionDictionary, MissionDef, missionDef ) {
			gslAccumVOReferenceForMissionDef( &eaVORefs, missionDef );
		} FOR_EACH_END;
		FOR_EACH_IN_REFDICT( g_ContactDictionary, ContactDef, contactDef ) {
			gslAccumVOReferenceForContactDef( &eaVORefs, contactDef );
		} FOR_EACH_END;
		assert( eaSize( &g_eaVOContext ) == 0 );

		// Write out each language's .vo.txt file
		{
			int it;
			for( it = 0; it != eaiSize( &giAllLocalesToMakeAndExit ); ++it ) {
				LocaleID locale = giAllLocalesToMakeAndExit[ it ];
				char filename[ MAX_PATH ];
				sprintf( filename, "%s/translations/%s.vo.txt", fileDataDir(), locGetName( locale ));
				if( !gslVOWriteCSVFile( eaVORefs, locale, filename )) {
					ErrorFilenamef( filename, "Unable to write to file." );
				}
			}
		}
	}

	// Write out a deleted .vo.txt file
	{
		char filename[ MAX_PATH ];
		VOReference** eaDeletedVORefs = NULL;

		sprintf( filename, "%s/translations/Deleted.vo.txt", fileDataDir() );

		FOR_EACH_IN_REFDICT( g_VODictionary, VOReference, voRef ) {
			if( !eaIndexedGetUsingString( &eaVORefs, voRef->astrKey )) {
				eaPush( &eaDeletedVORefs, StructClone( parse_VOReference, voRef ));
			}
		} FOR_EACH_END;
		
		if( !gslVOWriteCSVFile( eaDeletedVORefs, LOCALE_ID_ENGLISH, filename )) {
			ErrorFilenamef( filename, "Unable to write to file." );
		}
		eaDestroyStruct( &eaDeletedVORefs, parse_VOReference );
	}

	eaDestroyStruct( &eaVORefs, parse_VOReference );
}

AUTO_COMMAND ACMD_NAME( ApplyVOTXTFile );
void gslApplyVOTXTFile( const char* filename )
{
	VOReference** eaVORefs = NULL;
	ResourceActionList actions = { 0 };
	LocaleID localeID;
	Language lang;
	
	if( !gslVOReadAndMergeCSVFile( &eaVORefs, &localeID, filename )) {
		ErrorFilenamef( filename, "Unable to read file." );
		return;
	}
	lang = gslVOLocGetLanguage( localeID );

	sharedMemoryEnableEditorMode();
	resSetDictionaryEditMode( g_VODictionary, true );
	FOR_EACH_IN_EARRAY_FORWARDS( eaVORefs, VOReference, voRef ) {
		resAddRequestLockResource( &actions, g_VODictionary, voRef->astrKey, NULL );
		resAddRequestSaveResource( &actions, g_VODictionary, voRef->astrKey, voRef );
	} FOR_EACH_END;
	resRequestResourceActions( &actions );
	StructReset( parse_ResourceActionList, &actions );

	ParserWriteTextFileFromDictionary( "translations/Everything.vo", g_VODictionary, 0, 0 );
}

AUTO_COMMAND ACMD_NAME( DeleteVOTXTFile );
void gslDeleteVOTXTFile( const char* filename )
{
	VOReference** eaVORefs = NULL;
	ResourceActionList actions = { 0 };
	LocaleID localeID;
	Language lang;
	
	if( !gslVOReadAndMergeCSVFile( &eaVORefs, &localeID, filename )) {
		ErrorFilenamef( filename, "Unable to read file." );
		return;
	}
	lang = gslVOLocGetLanguage( localeID );

	sharedMemoryEnableEditorMode();
	resSetDictionaryEditMode( g_VODictionary, true );
	FOR_EACH_IN_EARRAY_FORWARDS( eaVORefs, VOReference, voRef ) {
		resAddRequestLockResource( &actions, g_VODictionary, voRef->astrKey, NULL );
		resAddRequestSaveResource( &actions, g_VODictionary, voRef->astrKey, NULL );
	} FOR_EACH_END;
	resRequestResourceActions( &actions );
	StructReset( parse_ResourceActionList, &actions );

	ParserWriteTextFileFromDictionary( "translations/Everything.vo", g_VODictionary, 0, 0 );
}

static ContactDef* gslMissionOfferingContact( MissionDef* missionDef )
{
	// There needs to be only one offering contact per mission.  If
	// this is not done, it is impossible to know who gives mission
	// offer text.
	if( !gConf.bOnlyOneContactPerMission ) {
		return NULL;
	}

	if( missionDef->astrContactForOfferVO ) {
		return RefSystem_ReferentFromString( g_ContactDictionary, missionDef->astrContactForOfferVO );
	}
		
	FOR_EACH_IN_EARRAY_FORWARDS( missionDef->ppMissionOfferOverrides, MissionOfferOverride, pOverride ) {
		if(   pOverride->pMissionOffer->allowGrantOrReturn == ContactMissionAllow_GrantOnly
			  || pOverride->pMissionOffer->allowGrantOrReturn == ContactMissionAllow_GrantAndReturn ) {
			return RefSystem_ReferentFromString( g_ContactDictionary, pOverride->pcContactName );
		}
	} FOR_EACH_END;

	return NULL;
}

void gslAccumVOReferenceForMissionDef( VOReference*** peaVORef, MissionDef* missionDef )
{
	VOContext* voContext = gslVOContextPush();
	ContactDef* offerContact = gslMissionOfferingContact( missionDef );
	voContext->strFilename = missionDef->filename;
	voContext->astrMissionName = missionDef->name;

	{
		VOContext* offerContext = gslVOContextPush();
		offerContext->astrContactName = SAFE_MEMBER( offerContact, name );

		gslAccumVOReferenceForDisplayMessageWithVO( peaVORef, &missionDef->detailStringMsg );
		FOR_EACH_IN_EARRAY_FORWARDS( missionDef->eaMissionDisplayOverride, MissionDisplayOverride, displayOverride ) {
			gslAccumVOReferenceForDisplayMessageWithVO( peaVORef, &displayOverride->detailStringMsg );
		} FOR_EACH_END;

		gslVOContextPop();
	}

	FOR_EACH_IN_EARRAY_FORWARDS( missionDef->ppSpecialDialogOverrides, SpecialDialogOverride, dialogOverride ) {
		VOContext* contactContext = gslVOContextPush();
		contactContext->astrContactName = dialogOverride->pcContactName;

		FOR_EACH_IN_EARRAY_FORWARDS( dialogOverride->pSpecialDialog->dialogBlock, DialogBlock, dialogBlock ) {
			gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
		} FOR_EACH_END;

		gslVOContextPop();
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( missionDef->ppMissionOfferOverrides, MissionOfferOverride, offerOverride ) {
		VOContext* contactContext = gslVOContextPush();
		contactContext->astrContactName = offerOverride->pcContactName;

		gslAccumVOReferenceForMissionOffer( peaVORef, offerOverride->pMissionOffer );

		gslVOContextPop();
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( missionDef->ppOnStartActions, WorldGameActionProperties, gameAction ) {
		gslAccumVOReferenceForGameAction( peaVORef, gameAction );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( missionDef->ppSuccessActions, WorldGameActionProperties, gameAction ) {
		gslAccumVOReferenceForGameAction( peaVORef, gameAction );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( missionDef->ppFailureActions, WorldGameActionProperties, gameAction ) {
		gslAccumVOReferenceForGameAction( peaVORef, gameAction );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( missionDef->ppOnReturnActions, WorldGameActionProperties, gameAction ) {
		gslAccumVOReferenceForGameAction( peaVORef, gameAction );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( missionDef->subMissions, MissionDef, childDef ) {
		gslAccumVOReferenceForMissionDef( peaVORef, childDef );
	} FOR_EACH_END;

	gslVOContextPop();
}

void gslAccumVOReferenceForContactDef( VOReference*** peaVORef, ContactDef* contactDef )
{
	VOContext* voContext = gslVOContextPush();
	voContext->strFilename = contactDef->filename;
	voContext->astrContactName = contactDef->name;

	FOR_EACH_IN_EARRAY_FORWARDS( contactDef->specialDialog, SpecialDialogBlock, specialDialog ) {
		FOR_EACH_IN_EARRAY_FORWARDS( specialDialog->dialogBlock, DialogBlock, dialogBlock ) {
			gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
		} FOR_EACH_END;
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( contactDef->generalCallout, DialogBlock, dialogBlock ) {
		gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( contactDef->missionCallout, DialogBlock, dialogBlock ) {
		gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( contactDef->rangeCallout, DialogBlock, dialogBlock ) {
		gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( contactDef->greetingDialog, DialogBlock, dialogBlock ) {
		gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( contactDef->infoDialog, DialogBlock, dialogBlock ) {
		gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( contactDef->defaultDialog, DialogBlock, dialogBlock ) {
		gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( contactDef->missionListDialog, DialogBlock, dialogBlock ) {
		gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( contactDef->noMissionsDialog, DialogBlock, dialogBlock ) {
		gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( contactDef->missionExitDialog, DialogBlock, dialogBlock ) {
		gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( contactDef->exitDialog, DialogBlock, dialogBlock ) {
		gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( contactDef->eaMissionSearchDialog, DialogBlock, dialogBlock ) {
		gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( contactDef->offerList, ContactMissionOffer, missionOffer ) {
		gslAccumVOReferenceForMissionOffer( peaVORef, missionOffer );
	} FOR_EACH_END;

	gslVOContextPop();
}

void gslAccumVOReferenceForMissionOffer( VOReference*** peaVORef, ContactMissionOffer* missionOffer )
{
	MissionDef* missionDef = GET_REF( missionOffer->missionDef );
	VOContext* voContext = gslVOContextPush();
	voContext->astrMissionName = SAFE_MEMBER( missionDef, name );
	
	FOR_EACH_IN_EARRAY_FORWARDS( missionOffer->greetingDialog, DialogBlock, dialogBlock ) {
		gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( missionOffer->offerDialog, DialogBlock, dialogBlock ) {
		gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( missionOffer->inProgressDialog, DialogBlock, dialogBlock ) {
		gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( missionOffer->completedDialog, DialogBlock, dialogBlock ) {
		gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( missionOffer->failureDialog, DialogBlock, dialogBlock ) {
		gslAccumVOReferenceForDialogBlock( peaVORef, dialogBlock );
	} FOR_EACH_END;

	gslVOContextPop();
}

void gslAccumVOReferenceForDialogBlock( VOReference*** peaVORef, DialogBlock* dialogBlock )
{
	gslAccumVOReferenceForDisplayMessageWithVO( peaVORef, &dialogBlock->displayTextMesg );
}

void gslAccumVOReferenceForGameAction( VOReference*** peaVORef, WorldGameActionProperties* gameAction )
{
	if( gameAction->pSendNotificationProperties ) {
		gslAccumVOReferenceForDisplayMessageWithVO( peaVORef, &gameAction->pSendNotificationProperties->notifyMsg );
	}
}

static void StructCopyString_StripNewlinesAndTabs( char** pstrOut, const char* str )
{
	char* estr = NULL;

	if( str ) {
		estr = estrCreateFromStr( str );
		estrReplaceOccurrences( &estr, "\r", "" );
		estrReplaceOccurrences( &estr, "\n", "" );
		estrReplaceOccurrences( &estr, "\t", "" );
	}
	StructCopyString( pstrOut, estr );
	estrDestroy( &estr );
}

static int stricmp_SkipNewlinesAndTabsAndEdgeWhitespace( const char* str1, const char* str2 )
{
	bool b1Exists = str1 && str1[0];
	bool b2Exists = str2 && str2[0];

	if( b1Exists != b2Exists ) {
		return 1;
	}
	if( !b1Exists ) {
		return 0;
	}
	if( str1 == str2 ) {
		return 0;
	}

	// Skip leading whitespace
	while( IS_WHITESPACE( *str1 )) {
		++str1;
	}
	while( IS_WHITESPACE( *str2 )) {
		++str2;;
	}
	
	while( true ) {
		while( *str1 == '\n' || *str1 == '\t' || *str1 == '\r' ) {
			++str1;
		}
		while( *str2 == '\n' || *str2 == '\t' || *str2 == '\r' ) {
			++str2;
		}

		if( !*str1 || !*str2 || *str1 != *str2 ) {
			break;
		}

		++str1;
		++str2;
	}

	// Skip trailing whitespace
	if( StringIsAllWhiteSpace( str1 ) && StringIsAllWhiteSpace( str2 )) {
		return 0;
	}

	return *str1 - *str2;
}

void gslAccumVOReferenceForDisplayMessageWithVO( VOReference*** peaVORef, DisplayMessageWithVO* msg )
{
	const VOContext* voContext = eaTail( &g_eaVOContext );

	if( IS_HANDLE_ACTIVE( msg->msg.hMessage ) && msg->bHasVO ) {
		const VOReference* curVORef = RefSystem_ReferentFromString( g_VODictionary, REF_STRING_FROM_HANDLE( msg->msg.hMessage ));
		VOReference* accum = StructCreate( parse_VOReference );
		accum->textOnly = StructCreate( parse_VORefTextOnly );
		accum->astrKey = REF_STRING_FROM_HANDLE( msg->msg.hMessage );
		accum->filename = allocAddFilename( "translations/Everything.vo" );
		eaIndexedAdd( peaVORef, accum );

		if( curVORef ) {
			StructCopy( parse_VOReference, curVORef, accum, 0, 0, 0 );
		}

		// The list of supported languages may have changed, fix it up!
		{
			int it;
			for( it = eaSize( &accum->textOnly->eaText ) - 1; it >= 0; --it ) {
				VODisplayString* text = accum->textOnly->eaText[ it ];
				if( text->eLang != LANGUAGE_DEFAULT && eaiFind( &giAllLocalesToMakeAndExit, text->eLang ) < 0 ) {
					StructDestroy( parse_VODisplayString, text );
					eaRemove( &accum->textOnly->eaText, it );
				}
			}

			if( !gslVOReferenceGetDisplayString( accum, LANGUAGE_DEFAULT )) {
				VODisplayString* text = StructCreate( parse_VODisplayString );
				text->eLang = LANGUAGE_DEFAULT;
				eaPush( &accum->textOnly->eaText, text );
			}
			for( it = 0; it != eaiSize( &giAllLocalesToMakeAndExit ); ++it ) {
				Language lang = gslVOLocGetLanguage( giAllLocalesToMakeAndExit[ it ]);
				if( !gslVOReferenceGetDisplayString( accum, lang )) {
					VODisplayString* text = StructCreate( parse_VODisplayString );
					text->eLang = lang;
					eaPush( &accum->textOnly->eaText, text );
				}
			}
		}

		if( nullStr( accum->astrAudioEvent )) {
			accum->astrAudioEvent = allocAddString( msg->astrLegacyAudioEvent );
		}
		FOR_EACH_IN_EARRAY_FORWARDS( accum->textOnly->eaText, VODisplayString, text ) {
			if( nullStr( text->strVOText )) {
				StructCopyString_StripNewlinesAndTabs( &text->strVOText, langTranslateDisplayMessage( text->eLang, msg->msg ));
			}
		} FOR_EACH_END;

		if(   SAFE_MEMBER( voContext, astrMissionName )
			  && RefSystem_ReferentFromString( g_MissionDictionary, voContext->astrMissionName )) {
			accum->textOnly->astrMissionName = voContext->astrMissionName;
		}
		if(   SAFE_MEMBER( voContext, astrContactName )
			  && RefSystem_ReferentFromString( g_ContactDictionary, voContext->astrContactName )
			  && gConf.bOnlyOneContactPerMission ) {
			ContactDef* contactDef = RefSystem_ReferentFromString( g_ContactDictionary, voContext->astrContactName );
			accum->textOnly->astrContactName = contactDef->name;
			StructCopyString_StripNewlinesAndTabs( &accum->textOnly->strContactDisplayName, langTranslateDisplayMessage( LANGUAGE_DEFAULT, contactDef->displayNameMsg ));
			StructCopyString_StripNewlinesAndTabs( &accum->textOnly->strContactVOComments, contactDef->strVOComments );
		}
	}
}

VOContext* gslVOContextPush( void )
{
	if( eaSize( &g_eaVOContext )) {
		eaPush( &g_eaVOContext, StructClone( parse_VOContext, eaTail( &g_eaVOContext )));
	} else {
		eaPush( &g_eaVOContext, StructCreate( parse_VOContext ));
	}

	return eaTail( &g_eaVOContext );
}

void gslVOContextPop( void )
{
	if( eaSize( &g_eaVOContext )) {
		StructDestroy( parse_VOContext, eaTail( &g_eaVOContext ));
		eaPop( &g_eaVOContext );
	}
}

Language gslVOLocGetLanguage( LocaleID loc )
{
	Language lang = locGetLanguage( loc );
	if( lang == LANGUAGE_ENGLISH ) {
		lang = LANGUAGE_DEFAULT;
	}

	return lang;
}

bool gslVOWriteCSVFile( VOReference** eaVORef, LocaleID locale, const char* filename )
{
	Language lang = gslVOLocGetLanguage( locale );
	FILE* out = fopen( filename, "wt" );
	if( !out ) {
		return false;
	}

	fprintf( out, "VER\t%d\tLOCALE\t%s\n", 2, locGetName( locale ));
	fprintf( out, "Key\tMission (Logical)\tMission (Display)\tContact (Logical)\tContact (Display)\tContact (Comments)\tGame Line (English)\tVO Line (English)\tGame Line (Translated)\tVO Line (Translated)\t\tChanged\tSound Event Path\tWave File Path\n" );
	FOR_EACH_IN_EARRAY_FORWARDS( eaVORef, VOReference, voRef ) {
		VODisplayString* defaultStr = gslVOReferenceGetDisplayString( voRef, LANGUAGE_DEFAULT );
		VODisplayString* langStr = gslVOReferenceGetDisplayString( voRef, lang );
		MissionDef* missionDef = RefSystem_ReferentFromString( g_MissionDictionary, voRef->textOnly->astrMissionName );
		char* defaultGameStr = NULL;
		char* langGameStr = NULL;
		
		StructCopyString_StripNewlinesAndTabs( &defaultGameStr, langTranslateMessageKey( LANGUAGE_DEFAULT, voRef->astrKey ));
		StructCopyString_StripNewlinesAndTabs( &langGameStr, langTranslateMessageKey( lang, voRef->astrKey ));
		
		fprintf( out, "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%d\t%s\t%s\n",
				 voRef->astrKey,
				 NULL_TO_EMPTY( SAFE_MEMBER( missionDef, name )),
				 (missionDef ? langTranslateDisplayMessage( LANGUAGE_DEFAULT, missionDef->displayNameMsg ) : ""),
				 NULL_TO_EMPTY( voRef->textOnly->astrContactName ),
				 NULL_TO_EMPTY( voRef->textOnly->strContactDisplayName ),
				 NULL_TO_EMPTY( voRef->textOnly->strContactVOComments ),
				 NULL_TO_EMPTY( defaultGameStr ),
				 NULL_TO_EMPTY( SAFE_MEMBER( defaultStr, strVOText )),
				 NULL_TO_EMPTY( langGameStr ),
				 NULL_TO_EMPTY( SAFE_MEMBER( langStr, strVOText )),
				 stricmp_SkipNewlinesAndTabsAndEdgeWhitespace( SAFE_MEMBER( langStr, strVOText ), langTranslateMessageKey( lang, voRef->astrKey )) != 0,
				 NULL_TO_EMPTY( voRef->astrAudioEvent ),
				 NULL_TO_EMPTY( voRef->textOnly->strWaveFileName ));

		StructFreeStringSafe( &defaultGameStr );
		StructFreeStringSafe( &langGameStr );
	} FOR_EACH_END;

	fclose( out );
	return true;
}

bool gslVOReadAndMergeCSVFile( VOReference*** out_peaVORef, LocaleID* out_locale, const char* filename )
{
	char* buf = fileAlloc( filename, NULL );
	char* context = NULL;
	char* lineIt;
	int version;
	int line;
	if( !buf ) {
		return false;
	}

	// Version, Language line
	line = 1;
	lineIt = strtok_s( buf, "\r\n", &context );
	if( strStartsWith( lineIt, "\xEF\xBB\xBF" )) {
		lineIt += 3;
	}
	{
		char** eaFields = NULL;
		DivideString( lineIt, "\t", &eaFields, DIVIDESTRING_RESPECT_SIMPLE_QUOTES );
		if( eaSize( &eaFields ) < 4 ) {
			eaDestroyEx( &eaFields, NULL );
			free( buf );
			return false;
		}
		version = atoi( eaFields[ 1 ]);
		*out_locale = locGetIDByName( eaFields[ 3 ]);

		eaDestroyEx( &eaFields, NULL );
	}

	if( version != 1 || *out_locale == LOCALE_ID_INVALID ) {
		free( buf );
		return false;
	}

	// Header line
	++line;
	lineIt = strtok_s( NULL, "\r\n", &context );

	// Content lines
	while( ++line, lineIt = strtok_s( NULL, "\r\n", &context )) {
		switch( version ) {
			xcase 1: {
				char** eaFields = NULL;
				VOReference* curVORef;
				VOReference* voRef = NULL;
				VODisplayString* text = NULL;

				// Microsoft escapes quotes by doubling them.  We nede
				// to respect quotes and also handle this case
				DivideString( lineIt, "\t", &eaFields, DIVIDESTRING_RESPECT_SIMPLE_QUOTES | DIVIDESTRING_POSTPROCESS_ESTRINGS );
				{
					int it;
					for( it = 0; it != eaSize( &eaFields ); ++it ) {
						estrReplaceOccurrences( &eaFields[ it ], "\"\"", "\"" );
					}
				}

				// Format is (with some extra spaces for clarity)):
				//
				// Key \t Mission (Logical) \t Mission (Display) \t Contact (Logical) \t Contact (Display) \t Contact (Comments) \t Line (English) \t Line (Translated) \t Changed \t Sound Event Path \t Wave File Path
				if( eaSize( &eaFields ) != 11 ) {
					ErrorFilenamef( filename, "Line: %d -- Expected exactly eleven elements, only found %d",
									line, eaSize( &eaFields ));
					
					eaDestroyEString( &eaFields );
					continue;
				}
				curVORef = RefSystem_ReferentFromString( g_VODictionary, eaFields[ 0 ]);

				voRef = StructCreate( parse_VOReference );
				voRef->textOnly = StructCreate( parse_VORefTextOnly );
				eaPush( out_peaVORef, voRef );
				voRef->astrKey = allocAddString( eaFields[ 0 ]);
				voRef->filename = allocAddFilename( "translations/Everything.vo" );


				if( curVORef ) {
					StructCopy( parse_VOReference, curVORef, voRef, 0, 0, 0 );
				}

				// skip mission logical (1) -- it is always auto deduced
				// skip mission display (2) -- it is always auto deduced

				voRef->textOnly->astrContactName = allocAddString( eaFields[ 3 ]);
				StructCopyString( &voRef->textOnly->strContactDisplayName, eaFields[ 4 ]);
				StructCopyString( &voRef->textOnly->strContactVOComments, eaFields[ 5 ]);

				text = gslVOReferenceGetDisplayString( voRef, LANGUAGE_DEFAULT );
				if( !text ) {
					text = StructCreate( parse_VODisplayString );
					text->eLang = LANGUAGE_DEFAULT;
					eaPush( &voRef->textOnly->eaText, text );
				}
				StructCopyString( &text->strVOText, eaFields[ 6 ]);

				text = gslVOReferenceGetDisplayString( voRef, gslVOLocGetLanguage( *out_locale ));
				if( !text ) {
					text = StructCreate( parse_VODisplayString );
					text->eLang = gslVOLocGetLanguage( *out_locale );
					eaPush( &voRef->textOnly->eaText, text );
				}
				StructCopyString( &text->strVOText, eaFields[ 7 ]);

				// skip changed (8) -- it is always auto deduced

				voRef->astrAudioEvent = allocAddString( eaFields[ 9 ]);
				voRef->textOnly->strWaveFileName = StructAllocString( eaFields[ 10 ]);

				eaDestroyEString( &eaFields );
			}

			xcase 2: {
				char** eaFields = NULL;
				VOReference* curVORef;
				VOReference* voRef = NULL;
				VODisplayString* text = NULL;

				// Microsoft escapes quotes by doubling them.  We nede
				// to respect quotes and also handle this case
				DivideString( lineIt, "\t", &eaFields, DIVIDESTRING_RESPECT_SIMPLE_QUOTES | DIVIDESTRING_POSTPROCESS_ESTRINGS );
				{
					int it;
					for( it = 0; it != eaSize( &eaFields ); ++it ) {
						estrReplaceOccurrences( &eaFields[ it ], "\"\"", "\"" );
					}
				}

				// Format is (with some extra spaces for clarity)):
				//
				// 0      1                    2                    3                    4                    5                     6                      7                    8                         9                       10         11                  12
				// Key \t Mission (Logical) \t Mission (Display) \t Contact (Logical) \t Contact (Display) \t Contact (Comments) \t Game Line (English) \t VO Line (English) \t Game Line (Translated) \t VO Line (Translated) \t Changed \t Sound Event Path \t Wave File Path
				if( eaSize( &eaFields ) != 13 ) {
					ErrorFilenamef( filename, "Line: %d -- Expected exactly thirteen elements, only found %d",
									line, eaSize( &eaFields ));
					
					eaDestroyEString( &eaFields );
					continue;
				}
				curVORef = RefSystem_ReferentFromString( g_VODictionary, eaFields[ 0 ]);

				voRef = StructCreate( parse_VOReference );
				voRef->textOnly = StructCreate( parse_VORefTextOnly );
				eaPush( out_peaVORef, voRef );
				voRef->astrKey = allocAddString( eaFields[ 0 ]);
				voRef->filename = allocAddFilename( "translations/Everything.vo" );


				if( curVORef ) {
					StructCopy( parse_VOReference, curVORef, voRef, 0, 0, 0 );
				}

				// skip mission logical (1) -- it is always auto deduced
				// skip mission display (2) -- it is always auto deduced

				voRef->textOnly->astrContactName = allocAddString( eaFields[ 3 ]);
				StructCopyString( &voRef->textOnly->strContactDisplayName, eaFields[ 4 ]);
				StructCopyString( &voRef->textOnly->strContactVOComments, eaFields[ 5 ]);

				// skip game line english (6) -- it is always auto deduced
				
				text = gslVOReferenceGetDisplayString( voRef, LANGUAGE_DEFAULT );
				if( !text ) {
					text = StructCreate( parse_VODisplayString );
					text->eLang = LANGUAGE_DEFAULT;
					eaPush( &voRef->textOnly->eaText, text );
				}
				StructCopyString( &text->strVOText, eaFields[ 7 ]);

				// skip game line translated (8) -- it is always auto deduced

				text = gslVOReferenceGetDisplayString( voRef, gslVOLocGetLanguage( *out_locale ));
				if( !text ) {
					text = StructCreate( parse_VODisplayString );
					text->eLang = gslVOLocGetLanguage( *out_locale );
					eaPush( &voRef->textOnly->eaText, text );
				}
				StructCopyString( &text->strVOText, eaFields[ 9 ]);

				// skip changed (10) -- it is always auto deduced

				voRef->astrAudioEvent = allocAddString( eaFields[ 11 ]);
				voRef->textOnly->strWaveFileName = StructAllocString( eaFields[ 12 ]);

				eaDestroyEString( &eaFields );
			}

			xdefault:
				free( buf );
				return false;
		}
	}

	free( buf );
	return true;
}

VODisplayString* gslVOReferenceGetDisplayString( VOReference* voRef, Language lang )
{
	FOR_EACH_IN_EARRAY_FORWARDS( voRef->textOnly->eaText, VODisplayString, voStr ) {
		if( voStr->eLang == lang ) {
			return voStr;
		}
	} FOR_EACH_END;

	return NULL;
}

const char* gslVOGetAudioEvent( const DisplayMessageWithVO* msg )
{
	const char* key = REF_STRING_FROM_HANDLE( msg->msg.hMessage );
	VOReference* voRef = RefSystem_ReferentFromString( g_VODictionary, key );
	return SAFE_MEMBER( voRef, astrAudioEvent );
}

void gslVOGetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio)
{
	const char** eaastrAudioEvents = NULL;
	*ppcType = strdup("VO");

	FOR_EACH_IN_REFDICT( g_VODictionary, VOReference, voRef ) {
		++*puiNumData;
		if( voRef->astrAudioEvent ) {
			eaPushUnique( &eaastrAudioEvents, voRef->astrAudioEvent );
			++*puiNumDataWithAudio;
		}
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS( eaastrAudioEvents, const char, str ) {
		eaPush( peaStrings, strdup( str ));
	} FOR_EACH_END;
}

#include "gslVOFiles_c_ast.c"
