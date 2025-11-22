#include "AccountStub.h"
#include "autogen/AccountStub_h_ast.h"
#include "AccountStubMerging.h"
#include "earray.h"

void InitializeAccountStub(NOCONST(AccountStub) *lhs)
{
	if(!lhs)
		return;

	// Nothing needs initializing
}

void MergeTwoAccountStubs(NOCONST(AccountStub) *lhs, AccountStub *rhs)
{
	if(!lhs || !rhs)
		return;

	assert(lhs->iAccountID == rhs->iAccountID);

	FOR_EACH_IN_EARRAY(rhs->eaOfflineCharacters, CharacterStub, characterStub);
	{
		if(characterStub)
		{
			int lhsIndex = eaIndexedFindUsingInt(&lhs->eaOfflineCharacters, characterStub->iContainerID);
			if(lhsIndex == -1)
			{
				// Not here, just add it
				eaIndexedAdd(&lhs->eaOfflineCharacters, StructClone(parse_CharacterStub, characterStub));
			}
			else
			{
				// A match. This should not happen. For now, just fail.
				devassert(0);
			}
		}
	}
	FOR_EACH_END;

	// This feature is not currently on so we shouldn't need to support this case
#if 0
	FOR_EACH_IN_EARRAY(rhs->eaOfflineAccountWideContainers, AccountWideContainerStub, containerStub);
	{
		if(characterStub)
		{
			int lhsIndex = eaIndexedFindUsingInt(&lhs->eaOfflineAccountWideContainers, containerStub->containerType);
			if(lhsIndex == -1)
			{
				// Not here, just add it
				eaIndexedAdd(&lhs->eaOfflineAccountWideContainers, StructClone(parse_AccountWideContainerStub, containerStub));
			}
			else
			{
				// A match. This should not happen. For now, just fail.
				devassert(0);
			}
		}
	}
	FOR_EACH_END;
#endif
}
