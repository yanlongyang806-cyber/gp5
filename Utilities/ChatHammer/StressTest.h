#pragma once

typedef struct NetLink NetLink;

void InitializeStressTests(NetLink *link, int timer);
void RunStressTests(NetLink *link, int timer);