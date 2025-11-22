#pragma once


typedef struct GimmeNode GimmeNode;

void gimmeSetBranchConfigRootFromLocal(const char *localdir);
void gimmeSetBranchConfigRoot(const char *lockdir);
int gimmeGetBranchNumber(const char *localpath);
void gimmeSetBranchNumber(const char *localpath, int number);
const char *gimmeGetBranchName(int branch);
int gimmeQueryBranchNumber(const char *localdir);
const char *gimmeQueryBranchName(const char *localdir);
int gimmeQueryCoreBranchNumForDir(const char *localdir);
const char *gimmeGetBranchPrevName(int branch);
const char *gimmeGetBranchLaterName(int branch);
const char *gimmeGetCheckinWarning(int branch);
int gimmeGetWarnOnLinkBreak(int branch);
int gimmeGetWarnOnLinkBroken(int branch);
void gimmeDisableWarnOverride(int val);
int gimmeIsNodeLinkedPrev(GimmeNode *node, int branch);
int gimmeIsNodeLinkBroken(GimmeNode *node, int branch);
int gimmeGetMaxBranchNumber(void);
int gimmeGetMinBranchNumber(void);
int gimmeArePatchNotesRequired(void);

int gimmeSetBranchNumberOverride(int branch); // Returns 0 on success

typedef struct GimmeDir GimmeDir;

// if a node is frozen on a branch, this returns the branch,
// otherwise, returns -1
int getFreezeBranch(GimmeDir *gimme_dir, const char * relpath);
