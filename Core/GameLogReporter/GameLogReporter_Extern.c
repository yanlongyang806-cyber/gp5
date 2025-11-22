#include "cmdparse.h"
#include "Entity.h"
#include "GlobalEnums.h"
#include "GlobalTypeEnum.h"
#include "Guild.h"
#include "MapDescription.h"
#include "UGCProjectCommon.h"
#include "queue_common_structs.h"
#include "queue_common.h"
#include "character.h"
#include "CharacterClass.h"


#define	PHYSX_SRC_FOLDER "../../3rdparty"
#include "PhysicsSDK.h"

GuildEmblemList g_GuildEmblems;

Entity* entExternGetCommandEntity(CmdContext *context)
{
	return NULL;
}

#include "GlobalTypeEnum_h_ast.h"
#include "GlobalEnums_h_ast.c"
#include "MapDescription_h_ast.c"
#include "UGCProjectCommon_h_ast.c"
#include "queue_common_structs_h_ast.h"
#include "queue_common_h_ast.h"
#include "queue_common_h_ast.c"