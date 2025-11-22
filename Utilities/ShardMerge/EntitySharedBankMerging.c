#include "Entity.h"
#include "autogen/Entity_h_ast.h"
#include "EntitySharedBankMerging.h"
#include "earray.h"
#include "GlobalTypes.h"
#include "MailCommon.h"
#include "MailCommon_h_ast.h"
#include "objSchema.h"

AUTO_RUN_LATE;
void RegisterEntityContainers(void)
{
	int i;
	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (GlobalTypeParent(i) == GLOBALTYPE_ENTITY)
		{
			objRegisterNativeSchema(i,parse_Entity, NULL, NULL, NULL, NULL, NULL);
			RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(i), false, parse_Entity, false, false, NULL);
		}
	}
}

void InitializeEntitySharedBank(NOCONST(Entity) *lhs)
{
	if(!lhs)
		return;

	// There should not be any items in the EntitySharedBanks for Neverwinter, our first attempt at using ShardMerge.
	// If we are merging shards for another game, we may need to implement inventory merging.

	assert(!lhs->pInventoryV2);

	// Nothing needs initializing
}

static int EmailSort(const NOCONST(EmailV3Message) **lhs, const NOCONST(EmailV3Message) **rhs)
{
	if((*rhs)->uSent > (*lhs)->uSent)
		return -1;
	else if((*lhs)->uSent > (*rhs)->uSent)
		return 1;
	else
		return 0;
}

void MergeTwoEntitySharedBanks(NOCONST(Entity) *lhs, Entity *rhs)
{
	NOCONST(EmailV3Message) **ppTempMessages = NULL;
	int lastID = 0;
	if(!lhs || !rhs)
		return;

	assert(lhs->myContainerID == rhs->myContainerID);

	if(!rhs->pEmailV3 || !eaSize(&rhs->pEmailV3->eaMessages))
		return;

	if(!lhs->pEmailV3)
	{
		lhs->pEmailV3 = CONTAINER_NOCONST(EmailV3, StructClone(parse_EmailV3, rhs->pEmailV3));
		return;
	}

	if(!eaSize(&lhs->pEmailV3->eaMessages))
	{
		// If there are no messages on the accumulator, just move the ones from rhs
		FOR_EACH_IN_EARRAY_FORWARDS(rhs->pEmailV3->eaMessages, EmailV3Message, message);
		{
			eaPush(&lhs->pEmailV3->eaMessages, CONTAINER_NOCONST(EmailV3Message, StructClone(parse_EmailV3Message, message)));
		}
		FOR_EACH_END;
		return;
	}

	// At this point, we know that there are message in both containers, so we have to merge them. 
	// This must be done into a temporary, non-indexed array so that we can rebuild the ids

	FOR_EACH_IN_EARRAY_FORWARDS(rhs->pEmailV3->eaMessages, EmailV3Message, message);
	{
		eaPush(&ppTempMessages, CONTAINER_NOCONST(EmailV3Message, StructClone(parse_EmailV3Message, message)));
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS(lhs->pEmailV3->eaMessages, NOCONST(EmailV3Message), message);
	{
		eaPush(&ppTempMessages, StructCloneNoConst(parse_EmailV3Message, message));
	}
	FOR_EACH_END;

	eaQSort(ppTempMessages, EmailSort);

//	lhs->pEmailV3->iAttachmentsCount = 0;
//	lhs->pEmailV3->iMessageCount = 0;
	lhs->pEmailV3->uLastUsedID = 0;

	FOR_EACH_IN_EARRAY_FORWARDS(ppTempMessages, NOCONST(EmailV3Message), message);
	{
		lhs->pEmailV3->bUnreadMail |= !message->bRead;

//		if(eaSize(&message->ppItems))
//			++lhs->pEmailV3->iAttachmentsCount;
		
//		++lhs->pEmailV3->iMessageCount;

		++lhs->pEmailV3->uLastUsedID;
		message->uID = lhs->pEmailV3->uLastUsedID;
	}
	FOR_EACH_END;

	eaDestroy(&lhs->pEmailV3->eaMessages);
	lhs->pEmailV3->eaMessages = ppTempMessages;
}
