#pragma once
GCC_SYSTEM

/***************************************************************************



***************************************************************************/

#if !GAMESERVER && !GAMECLIENT
	#error No Movement code allowed here.
#endif

typedef struct MovementRequester	MovementRequester;

// TestMovement.

void mmTestSetDoTest(	MovementRequester* mr,
						U32 startTime);

