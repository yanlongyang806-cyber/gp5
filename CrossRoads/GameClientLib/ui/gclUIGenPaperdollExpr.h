#pragma once
GCC_SYSTEM

#include "gclUIGenPaperdoll.h"

// This structure maps to the Guild emblem data in the Guild structure.
AUTO_STRUCT;
typedef struct GuildEmblemData
{
	S32 iVersion;
	const char *pcEmblem; AST(POOL_STRING) // Name of a PCTextureDef in CostumeTexture dictionary
	U32 iEmblemColor0;
	U32 iEmblemColor1;
	F32 fEmblemRotation; // [-PI, PI)
	const char *pcEmblem2; AST(POOL_STRING) // Name of a PCTextureDef in CostumeTexture dictionary
	U32 iEmblem2Color0;
	U32 iEmblem2Color1;
	F32 fEmblem2Rotation; // [-PI, PI)
	F32 fEmblem2X; // -100 to 100
	F32 fEmblem2Y; // -100 to 100
	F32 fEmblem2ScaleX; // 0 to 100
	F32 fEmblem2ScaleY; // 0 to 100
	const char *pcEmblem3; AST(POOL_STRING) // Name of a PCTextureDef in CostumeTexture dictionary (Detail)
} GuildEmblemData;

SA_RET_OP_VALID PlayerCostume *gclCreateGuildEmblemCostume(ContainerID iGuildID, const char *pchSkeleton, const char *pchEmblemBone, GuildEmblemData *pEmblemData, bool bNoBackground, bool bNoDetail);
extern void gclPaperdollFlushCostumeGenCache(void);

