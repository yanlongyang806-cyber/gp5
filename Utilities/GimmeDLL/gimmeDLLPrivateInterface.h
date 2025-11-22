#include "GimmeDLL.h"
#include "gimmeDLLPublicInterface.h"

// JE: Exposing these functions because they include the wrappers for finding
//   the appropriate patchme functions, which is not always a very obvious thing.

GIMMEDLL_API GimmeErrorValue GimmeDoOperation(const char *fullpath, GIMMEOperation op, GimmeQuietBits quiet);
GIMMEDLL_API const char *GimmeQueryIsFileLocked(const char *fullpath);
GIMMEDLL_API int GimmeQueryIsFileLockedByMeOrNew(const char *fullpath);
GIMMEDLL_API const char *GimmeQueryLastAuthor(const char *fullpath);
GIMMEDLL_API const char *GimmeQueryUserName(void);
GIMMEDLL_API GimmeErrorValue GimmeDoCommand(const char *cmdline);
GIMMEDLL_API int GimmeQueryBranchNumber(const char *localpath);
GIMMEDLL_API const char *GimmeQueryBranchName(const char *localpath);
GIMMEDLL_API void GimmeForceManifest(bool force);
GIMMEDLL_API bool GimmeForceDirtyBit(const char *fullpath);