#pragma once

// Utility for logging errors during login.
void aslLogin2_Log(char *format, ...);

char **aslLogin2_GetRecentLogLines(void);
int aslLogin2_GetNextLogIndex(void);