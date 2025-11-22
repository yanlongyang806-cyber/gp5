#include "Character.h"
#include "cmdParse.h"
#include "ControlScheme.h"
#include "../../libs/WorldLib/dynFx.h"
#include "file.h"
#include "GameClientLib.h"
#include "GameStringFormat.h"
#include "gclCamera.h"
#include "gclControlScheme.h"
#include "gclEntity.h"
#include "gclOptions.h"
#include "gclUIGen.h"
#include "GfxLCD.h"
#include "GfxLightOptions.h"
#include "gfxSettings.h"
#include "GlobalTypes.h"
#include "GraphicsLib.h"
#include "RenderLib.h"
#include "RdrShader.h"
#include "RdrState.h"


#ifndef _XBOX
#include "resource1.h"
#endif

extern int gClickSize;
extern int gDoubleClickSize;

AUTO_RUN_FIRST;
void InitGlobalConfig(void)
{
#ifndef _XBOX
	gGCLState.logoResource = IDR_CRYPTIC_LOGO;
#endif
	gfxSetTargetHighlight(true);

	gfxSetFeatures(GFEATURE_POSTPROCESSING|GFEATURE_DOF|GFEATURE_SHADOWS|GFEATURE_WATER);

	gfxSetDefaultLightingOptions();
	gfx_lighting_options.enableDOFCameraFade = true;

	gfxSettingsSetScattering(GSCATTER_LOW_QUALITY);
	gfxSettingsSetHDRMaxLuminanceMode(true);
	rdrSetDisableToneCurve10PercentBoost(true);

	gConf.bHideChatWindow = 1;
	gConf.iFontSmallSize = 12;
	gConf.iFontMediumSize = 16;
	gConf.iFontLargeSize = 20;

	gClickSize = 12;
	gDoubleClickSize = 4;
}

AUTO_RUN_LATE;
void OverrideConfig(void)
{
	rdrShaderSetGlobalDefine(0, "TINT_SHADOW"); // Allows shadow color tinting
	gfxEnableDiffuseWarp(1);
	//rdrShaderSetGlobalDefine(2, "SIDE_AS_RIMLIGHT"); // Allows a harsh, directional rimlight driven by the atmospheric side light color

	rdr_state.alphaInDOF = 1;
}


static bool showAlways(OptionSetting *setting)
{
	return false;
}

void OVERRIDE_LATELINK_schemes_ControlSchemeOptionInit(const char* pchCategory)
{
	// always show
	options_HideOption(pchCategory, "AutoAttackDelay", showAlways);
	options_HideOption(pchCategory, "DelayAutoAttackUntilCombat", showAlways);
}