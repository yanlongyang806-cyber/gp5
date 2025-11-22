#include "gimmeUserGroup.h"
#include "gimmeUtil.h"
#include "textparser.h"
#include "stashtable.h"
#include "error.h"

#include "gimmeUserGroup_h_ast.h"

// All users and their groups in memory, so we can only load them once
static UGroupData g_UserGroupDataFromFile = { /*groupInfo=*/NULL };

typedef struct UserGroupInfo
{
	const char *const *all_groups;
	const char *const *all_users;
	StashTable groups_for_user;
} UserGroupInfo;

static UserGroupInfo g_UserGroupInfo = { /*all_groups=*/NULL, /*all_users=*/NULL, /*groups_for_user=*/NULL };

// Historical note: This file was formerly located on a network drive, and cached locally.  This caused various
// problems, because the file was not under source control or versioning, and it made Gimme require access to
// this specific network drive.  When Old Gimme (the less old Old Gimme) was eliminated, this was changed to
// just use the "Cryptic" Gimme database.
// Old location: N:/revisions/groups.txt
#define NETWORK_DRIVE_REVISIONS_FILE_PATH	"c:/cryptic/config/groups.txt"

static void EnsureUserGroupDataFromFileIsLoaded()
{
	if(!g_UserGroupDataFromFile.groupInfo)
	{
		char buf[MAX_PATH];

		gimmeCacheLock();
		gimmeGetCachedFilename(NETWORK_DRIVE_REVISIONS_FILE_PATH, SAFESTR(buf));

		ParserLoadFiles(0, buf, 0, 0, parse_UGroupData, &g_UserGroupDataFromFile);

		gimmeCacheUnlock();
	}
}

const char *const *LoadUserGroupInfoFromFileFindUsers()
{
	if(!g_UserGroupInfo.all_users)
	{
		char **ret = NULL;
		int i = 0, j = 0;
		int foundUser = 0;
		StashElement elem;
		StashTableIterator iter;
		StashTable table = stashTableCreateWithStringKeys(10, StashDefault);

		EnsureUserGroupDataFromFileIsLoaded();

		for( i = 0; i < eaSize(&g_UserGroupDataFromFile.groupInfo); ++i )
		{
			stashAddInt(table, g_UserGroupDataFromFile.groupInfo[i]->userName, 1, 0);
		}

		stashGetIterator(table, &iter);
		while(stashGetNextElement(&iter, &elem))
		{
			eaPush(&ret, strdup(stashElementGetStringKey(elem)));
		}
		g_UserGroupInfo.all_users = ret;
	}

	return g_UserGroupInfo.all_users;
}

const char *const *LoadUserGroupInfoFromFileFindGroups()
{
	if(!g_UserGroupInfo.all_groups)
	{
		char **ret = NULL;
		int i = 0, j = 0;
		StashElement elem;
		StashTableIterator iter;
		StashTable table = stashTableCreateWithStringKeys(10, StashDefault);

		EnsureUserGroupDataFromFileIsLoaded();

		for( i = 0; i < eaSize(&g_UserGroupDataFromFile.groupInfo); ++i )
		{
			for( j = 0; j < eaSize(&g_UserGroupDataFromFile.groupInfo[i]->groupNames); ++j )
			{
				stashAddInt(table, g_UserGroupDataFromFile.groupInfo[i]->groupNames[j], 1, 0);
			}
		}

		stashGetIterator(table, &iter);
		while(stashGetNextElement(&iter, &elem))
		{
			eaPush(&ret, strdup(stashElementGetStringKey(elem)));
		}
		g_UserGroupInfo.all_groups = ret;
	}

	return g_UserGroupInfo.all_groups;
}

const char *const *LoadUserGroupInfoFromFile(const char *username)
{
	const char *const *groups = NULL;

	if(!g_UserGroupInfo.groups_for_user)
		g_UserGroupInfo.groups_for_user = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);

	stashFindPointer(g_UserGroupInfo.groups_for_user, username, (void**)&groups);

	if(!groups)
	{
		char **ret = NULL;
		int i = 0, j = 0;
		int foundUser = 0;
		char *temp = NULL;

		EnsureUserGroupDataFromFileIsLoaded();

		eaCreate(&ret);

		// we only need to keep the group names of the current user
		for( i = 0; i < eaSize(&g_UserGroupDataFromFile.groupInfo); ++i )
		{
			if ( ! stricmp( username, g_UserGroupDataFromFile.groupInfo[i]->userName ) )
			{
				for ( j = 0; j < eaSize(&g_UserGroupDataFromFile.groupInfo[i]->groupNames); ++j )
				{
					temp = strdup(g_UserGroupDataFromFile.groupInfo[i]->groupNames[j]);
					eaPush(&ret, g_UserGroupDataFromFile.groupInfo[i]->groupNames[j]);
				}
				groups = ret;
				foundUser = 1;
				stashAddPointer(g_UserGroupInfo.groups_for_user, username, groups, true);
				break;
			}
		}

		// this user does not exist in the group info file
		if (!foundUser && eaSize(&g_UserGroupDataFromFile.groupInfo))
		{
			ErrorFilenameDeferredf(NETWORK_DRIVE_REVISIONS_FILE_PATH, "User \"%s\" is not in any Gimme groups.  Please contact your administrator and have him add you.", username);
		}
	}

	return groups;
}

#include "gimmeUserGroup_h_ast.c"
