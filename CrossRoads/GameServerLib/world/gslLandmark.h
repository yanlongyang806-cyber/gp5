/***************************************************************************



***************************************************************************/

#pragma once

#include "ReferenceSystem.h"


typedef struct Message Message;
typedef struct WorldVolumeEntry WorldVolumeEntry;


// Information about a Landmark on the current map
typedef struct LandmarkData {
	// The landmark display information
	const char *pcIconName;						AST( POOL_STRING )
	REF_TO(Message) hDisplayNameMsg;

	// For geometry layer landmarks
	WorldVolumeEntry *pEntry;

	// The center position
	bool bCenterPosInitialized;
	Vec3 vCenterPos;

	F32 fXAxisRadius;
	F32 fYAxisRadius;
	F32 fRotation;

	bool bScaleToArea;
	int iZOrder;

	Expression *pVisibleExpr;					AST( NAME("VisibleExpressionBlock") LATEBIND )

	bool bHideUnlessRevealed;
} LandmarkData;


// Maintain the landmark list
void landmark_AddLandmarkVolume(WorldVolumeEntry *pEntry);
void landmark_RemoveLandmarkVolume(WorldVolumeEntry *pEntry);

// Get the landmark list
LandmarkData **landmark_GetLandmarkData(void);
bool landmark_GetCenterPoint(LandmarkData *pData, Vec3 vCenterPos);

// Validation
void landmark_MapValidate(void);
