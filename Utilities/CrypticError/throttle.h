#ifndef CRYPTICERROR_THROTTLE_H
#define CRYPTICERROR_THROTTLE_H

void throttleEnable(bool bEnabled);

void throttleReset(size_t uTotalBytesToSend);
void throttleProgress(size_t uTotalSentBytes);

#endif
