#pragma once
GCC_SYSTEM

/***************************************************************************



***************************************************************************/

#if !GAMESERVER && !GAMECLIENT
	#error No Movement code allowed here.
#endif

typedef struct MovementRequesterMsg MovementRequesterMsg;
typedef struct MovementRequester MovementRequester;

void	mmPlatformMovementMsgHandler(const MovementRequesterMsg* msg);

S32		mmPlatformMovementSetSpeed(	MovementRequester* mr,
									F32 speed);

S32		mmPlatformMovementSetVelocity(	MovementRequester* mr,
										const Vec3 vel);
