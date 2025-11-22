#include <time.h>

int gimmeUtilPurgeByDate(int gimme_dir_num, time_t date);
int gimmeUtilPurgeByBranch(int gimme_dir_num, int branchToRemove);
int gimmeUtilPrune(int gimme_dir_num);
int gimmeUtilPurgeFolder(const char *folder, int add);
