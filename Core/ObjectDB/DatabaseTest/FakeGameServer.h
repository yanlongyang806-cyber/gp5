/***************************************************************************



***************************************************************************/

#pragma once

typedef struct FrameLockedTimer FrameLockedTimer;

typedef struct FakeGameServerState
{
	FrameLockedTimer* flt;
} FakeGameServerState;

extern FakeGameServerState gFGSState;

void fgsRunTest(void);
void RunLocalTransactionTests(void);
void fgsInit(void);
void fgsTick(void);
void fgsShutdown(void);

#define FGSSTATE_WAITING "fgsStateWaiting"
#define FGSSTATE_TESTING "fgsStateTesting"