/***************************************************************************



***************************************************************************/

#include "ReferenceSystem.h"

typedef struct CutsceneDef CutsceneDef;
typedef struct GfxCameraController GfxCameraController;
typedef struct GfxCameraView GfxCameraView;
typedef struct ItemDef ItemDef;
typedef struct Item Item;

// Clean up the current cutscene
void contactui_CleanUpCurrentCutscene(CutsceneDef *pNextCutScene);

// Resets the camera back to the game camera
void contactui_ResetToGameCamera(void);

// Contact cutscene camera function
void contactCutSceneCameraFunc(GfxCameraController *pCamera, GfxCameraView *pCameraView, F32 fElapsed, F32 fRealElapsed);