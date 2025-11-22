#pragma once

AUTO_STRUCT;
typedef struct UGroupInfo
{
	char *userName;			AST(STRUCTPARAM)
	char **groupNames;		AST(NAME(Groups))
} UGroupInfo;

AUTO_STRUCT;
typedef struct UGroupData
{
	UGroupInfo **groupInfo;	AST( NAME(User) )
} UGroupData;

const char *const *LoadUserGroupInfoFromFileFindUsers();
const char *const *LoadUserGroupInfoFromFileFindGroups();
const char *const *LoadUserGroupInfoFromFile(const char *username);
