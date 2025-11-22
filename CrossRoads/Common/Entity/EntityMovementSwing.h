#pragma once
GCC_SYSTEM

/***************************************************************************



***************************************************************************/

#if !GAMESERVER && !GAMECLIENT
	#error No Movement code allowed here.
#endif

S32 mrSwingSetMaxSpeed(	MovementRequester* mr,
						F32 maxSpeed);

void mrSwingSetFx(MovementRequester* mr, const char * pcSwingingFx);