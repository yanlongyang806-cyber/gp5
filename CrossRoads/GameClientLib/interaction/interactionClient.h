#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct CBox CBox;

void gclDrawStuffOverObjects(void);
void gclWaypoint_UpdateGens(void);

F32 objGetScreenDist(WorldInteractionNode *pNode);
void objGetScreenBoundingBox(WorldInteractionNode *pNode, CBox *pBox, F32 *pfDistance, bool bClipToScreen, bool bIgnoreDimensionsAndUseCenterPoint);