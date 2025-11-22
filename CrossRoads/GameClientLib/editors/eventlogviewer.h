#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

typedef struct EventLogViewer EventLogViewer;

EventLogViewer* eventlogviewer_Create(void);
void eventlogviewer_Destroy(EventLogViewer *logviewer);

#endif