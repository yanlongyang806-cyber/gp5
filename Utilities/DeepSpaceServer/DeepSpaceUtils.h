/*
 * DeepSpaceServer utility functions
 */

#ifndef CRYPTIC_DEEPSPACEUTILS_H
#define CRYPTIC_DEEPSPACEUTILS_H

// Parse an info hash string like "2b31d7042bf17716ee1fc1bcfa27f06ca912be9c".
bool parseInfoHashString(U32 info_hash[8], const char *string);

// Get an estring that is the name of the LCID.
void getLcidName(char **estr, int lcid);

#endif  // CRYPTIC_DEEPSPACEUTILS_H
