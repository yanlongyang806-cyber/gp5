#pragma once

// Self-Patch the launcher
// - method blocks until self patch of launcher itself is done

// returns errorlevel 0 if success
// returns errorlevel 1 if failed with single attempt
// returns errorlevel 2 if failed with multiple attempts
extern int LauncherSelfPatch(const char *productName);

extern void LauncherSpawnNewAndQuitCurrent(const char* exeName, const char *username, const char *consoleContext);

extern const char *getAutoPatchServer(void);
