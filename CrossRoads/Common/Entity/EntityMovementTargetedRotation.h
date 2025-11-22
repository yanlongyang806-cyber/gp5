#pragma once
GCC_SYSTEM

/***************************************************************************



***************************************************************************/

#if !GAMESERVER && !GAMECLIENT
	#error No Movement code allowed here.
#endif

typedef struct MovementRequester	MovementRequester;
typedef U32							EntityRef;

S32		mmTargetedRotationMovementSetEnabled(	MovementRequester* mr,
												S32 enabled,
												EntityRef erTarget);
