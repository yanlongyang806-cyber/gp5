/***************************************************************************



***************************************************************************/

#include "error.h"
#include "Expression.h"
#include "gslLandmark.h"
#include "gslMission.h"
#include "gslVolume.h"
#include "gslWaypoint.h"
#include "oldencounter_common.h"
#include "StringCache.h"
#include "wlVolumes.h"
#include "../StaticWorld/WorldCellEntry.h"
#include "Quat.h"


static LandmarkData **s_eaLandmarkData = NULL;

ExprContext *g_pLandmarkContext = NULL;

// ----------------------------------------------------------------------------------
// Landmark Tracking
// ----------------------------------------------------------------------------------


void landmark_AddLandmarkVolume(WorldVolumeEntry *pEntry)
{
	LandmarkData *pData;
	int i;

	// Check for duplicate add
	for(i=eaSize(&s_eaLandmarkData)-1; i>=0; --i) {
		if (s_eaLandmarkData[i]->pEntry == pEntry) {
			Errorf("Attempted to add two copies of landmark volume.");
			return;
		}
	}

	// Add new entry
	pData = calloc(1, sizeof(LandmarkData));
	pData->pcIconName = allocAddString(pEntry->server_volume.landmark_volume_properties->icon_name);
	SET_HANDLE_FROM_REFERENT("Message", GET_REF(pEntry->server_volume.landmark_volume_properties->display_name_msg.hMessage), pData->hDisplayNameMsg);
	pData->pEntry = pEntry;
	pData->bHideUnlessRevealed = pEntry->server_volume.landmark_volume_properties->hide_unless_revealed;
	pData->bScaleToArea = pEntry->server_volume.landmark_volume_properties->scale_to_area;
	pData->iZOrder = pEntry->server_volume.landmark_volume_properties->z_order;

	if (pEntry->server_volume.landmark_volume_properties->visible_cond)
	{
		pData->pVisibleExpr = exprClone(pEntry->server_volume.landmark_volume_properties->visible_cond);
		exprGenerate(pData->pVisibleExpr, g_pLandmarkContext);
	}

	eaPush(&s_eaLandmarkData, pData);
	
	// Flag all players to refresh waypoints
	waypoint_FlagWaypointRefreshAllPlayers();
}


void landmark_RemoveLandmarkVolume(WorldVolumeEntry *pEntry)
{
	int i;

	// Remove the volume if present
	for(i=eaSize(&s_eaLandmarkData)-1; i>=0; --i) {
		if (s_eaLandmarkData[i]->pEntry == pEntry) {
			REMOVE_HANDLE(s_eaLandmarkData[i]->hDisplayNameMsg);
			free(s_eaLandmarkData[i]);
			eaRemove(&s_eaLandmarkData, i);

			// Flag all players to refresh waypoints
			waypoint_FlagWaypointRefreshAllPlayers();

			return;
		}
	}
}

LandmarkData **landmark_GetLandmarkData(void)
{
	return s_eaLandmarkData;
}


static const char *landmark_GetSystemName(LandmarkData *pData)
{
	const char *pcName = volume_NameFromWorldEntry(pData->pEntry);
	return pcName ? pcName : "(NoNameFound)";
}


bool landmark_GetCenterPoint(LandmarkData *pData, Vec3 vCenterPos)
{
	if (!pData->bCenterPosInitialized && pData->pEntry) {
		int i;

		for(i=eaSize(&pData->pEntry->eaVolumes)-1; i>=0; --i) {
			if (pData->pEntry->eaVolumes[i]) {
				// Get the center of the volume
				Vec3 v3World, v3Min, v3Max, v3Up;
				Quat qRot;
				pData->bCenterPosInitialized = true;
				wlVolumeGetVolumeWorldMid(pData->pEntry->eaVolumes[i], pData->vCenterPos);
				wlVolumeGetWorldPosRotMinMax(pData->pEntry->eaVolumes[i], v3World, qRot, v3Min, v3Max);
				pData->fXAxisRadius = (v3Max[0] - v3Min[0])/2;
				pData->fYAxisRadius = (v3Max[2] - v3Min[2])/2;
				quatToAxisAngle(qRot, v3Up, &pData->fRotation);
				if (v3Up[1] < 0)
					pData->fRotation *= -1;
			}
		}
	}

	copyVec3(pData->vCenterPos, vCenterPos);
	return pData->bCenterPosInitialized;
}


void landmark_MapValidate(void)
{
	int i;

	for(i=eaSize(&s_eaLandmarkData)-1; i>=0; --i) {
		LandmarkData *pData = s_eaLandmarkData[i];

		// Ensure that each landmark has a display message
		if (!GET_REF(pData->hDisplayNameMsg)) {
			if (REF_STRING_FROM_HANDLE(pData->hDisplayNameMsg)) {
				Errorf("Landmark '%s' has missing display name with key '%s'", landmark_GetSystemName(pData), REF_STRING_FROM_HANDLE(pData->hDisplayNameMsg));
			} else {
				//// TODO sdangelo: enable this once designers fix their data
				//Errorf("Landmark '%s' has no display name defined.", landmark_GetSystemName(pData));
			}
		}

		// Icon is optional
	}
}

ExprFuncTable* landmark_CreateExprFuncTable(void)
{
	static ExprFuncTable* s_LandmarkFuncTable = NULL;
	if (!s_LandmarkFuncTable) {
		s_LandmarkFuncTable = exprContextCreateFunctionTable("LandMark");
		exprContextAddFuncsToTableByTag(s_LandmarkFuncTable, "entity");
		exprContextAddFuncsToTableByTag(s_LandmarkFuncTable, "entityutil");
		exprContextAddFuncsToTableByTag(s_LandmarkFuncTable, "gameutil");
		exprContextAddFuncsToTableByTag(s_LandmarkFuncTable, "player");
		exprContextAddFuncsToTableByTag(s_LandmarkFuncTable, "PTECharacter");
		exprContextAddFuncsToTableByTag(s_LandmarkFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_LandmarkFuncTable, "encounter_action");
	}
	return s_LandmarkFuncTable;
}


AUTO_RUN;
void landmark_InitCommon(void)
{
	// Set up the interaction expression context
	g_pLandmarkContext = exprContextCreate();
	exprContextSetFuncTable(g_pLandmarkContext, landmark_CreateExprFuncTable());
	exprContextSetAllowRuntimePartition(g_pLandmarkContext);
	exprContextSetAllowRuntimeSelfPtr(g_pLandmarkContext);
}