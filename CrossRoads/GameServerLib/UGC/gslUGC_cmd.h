//// Container for all Client -> Server communication for UGC.
////
//// Let's have a real API here!
#pragma once

typedef struct Entity Entity;
typedef struct UGCFreezeProjectInfo UGCFreezeProjectInfo;
typedef struct UGCProjectData UGCProjectData;
typedef struct UGCProject UGCProject;
typedef struct TransactionReturnVal TransactionReturnVal;

extern ParseTable parse_UGCFreezeProjectInfo[];
#define TYPE_parse_UGCFreezeProjectInfo UGCFreezeProjectInfo

void gslUGC_PlayProjectNonEditor(Entity *pEntity, 
	const char *pcNamespace_unnormalized, 
	const char *pcCostumeOverride_unnormalized,
	const char *pcPetOverride_unnormalized,
	const char *pcBodyText_unnormalized,
	bool bPlayingAsBetaReviewer);
void QueryUGCProjectStatus(Entity *pEntity);
void SaveAndPublishUGCProject(Entity *pEntity, UGCProjectData *data);
void SaveAndPublishFail( Entity* pEntity, const char* strReason, bool alert );
void SaveAndPublishSuccess( Entity* pEntity );

//////////////////////////////////////////////////////////////////////
// Per-game functions exposed, in gslSTOUGC_cmd.c or gslNWUGC_cmd.c
void gslUGC_DoRespecCharacter( Entity* ent, int allegianceDefaultsIndex, const char* className, int levelValue );
void DoFreezeUGCProject(Entity *pEntity, UGCProjectData *data, UGCFreezeProjectInfo *pInfo);

U32 gslUGC_FeaturedTimeFromString( const char* timeStr );

void gslUGC_RequestAccountThrottled(Entity *pEntity);

char *UGCImport_Save(UGCProject *pUGCProject, UGCProjectData *pUGCProjectData, bool bPublish, TransactionReturnVal *pReturnVal);
