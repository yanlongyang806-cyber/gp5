#pragma once
GCC_SYSTEM

/***************************************************************************



***************************************************************************/

#if !GAMESERVER && !GAMECLIENT
	#error No Movement code allowed here.
#endif

typedef struct MovementManager		MovementManager;
typedef struct MovementRequester	MovementRequester;
typedef struct MovementRequesterMsg MovementRequesterMsg;

S32		mrFlightCreate(	MovementManager* mm,
						MovementRequester** mrOut);

bool	mrFlightGetEnabled(MovementRequester* mr);

bool	mrFlightSetEnabled(	MovementRequester* mr,
							bool enabled);

bool	mrFlightSetFakeRoll(MovementRequester* mr,
							bool enabled);
bool	mrFlightSetGlide(MovementRequester* mr,
							bool enabled);

bool	mrFlightSetPointAndDirectionRotationsIgnorePitch(MovementRequester* mr,
													     bool ignore);

bool	mrFlightSetAllRotationTypesIgnorePitch(MovementRequester* mr,
											   bool ignore);

bool	mrFlightSetUseJumpBit(MovementRequester* mr,
							  bool useJumpBit);

bool	mrFlightSetMaxSpeed(MovementRequester* mr,
							F32 maxSpeed);

bool	mrFlightSetGravity(	MovementRequester* mr, 
							F32 gravityUp,
							F32 gravityDown);

bool	mrFlightSetGlideDecent(MovementRequester* mr,
							F32 fDecent);

bool	mrFlightSetTraction(MovementRequester* mr,
							F32 traction);

bool	mrFlightSetFriction(MovementRequester* mr,
							F32 friction);

bool	mrFlightSetTurnRate(MovementRequester* mr,
							F32 turnRate);

bool	mrFlightGetTurnRate(MovementRequester* mr,
							F32* turnRateOut);

bool	mrFlightSetBankRate(MovementRequester* mr,
							F32 bankRate);

bool	mrFlightGetBankRate(MovementRequester* mr,
							F32* bankRateOut);

bool	mrFlightSetMaxBankAngle(MovementRequester* mr,
								F32 maxBankAngle);

bool	mrFlightGetMaxBankAngle(MovementRequester* mr,
								F32* maxBankAngleOut);

bool	mrFlightSetMaxPitchAngle(MovementRequester* mr,
								F32 maxPitchAngle);

bool	mrFlightGetMaxPitchAngle(MovementRequester* mr,
								F32* maxPitchAngleOut);

bool	mrFlightSetThrottle(MovementRequester* mr,
							F32 throttle);

bool	mrFlightGetThrottle(MovementRequester* mr,
							F32* throttleOut);

bool	mrFlightSetUseThrottle(	MovementRequester* mr,
								bool useThrottle);

bool	mrFlightSetUseOffsetRotation(	MovementRequester* mr,
										bool useOffsetRotation);

bool	mrFlightSetIsStrafing(MovementRequester* mr,
							  bool isStrafing);

AUTO_STRUCT;
typedef struct MMROffsetConstant {
	F32 rotationOffset;
} MMROffsetConstant;

S32		mmrOffsetResourceCreateBG(	const MovementRequesterMsg* msg,
									const MMROffsetConstant* constant,
									U32* handleOut);

S32		mmrOffsetResourceDestroyBG(	const MovementRequesterMsg* msg,
									U32* handleInOut);

