#include "db.h"
#include "sysutil.h"
#include "file.h"
#include "fileWatch.h"
#include "net.h"
#include "pcl_client.h"
#include "patchtrivia.h"
#include "earray.h"
#include "EString.h"

#include "AutoGen/db_h_ast.h"

#define PCL_DO_WAIT(f) {assert((f)==PCL_SUCCESS); assert(pclWait(client)==PCL_SUCCESS);}

Project **g_projects = NULL;

static int defaultBranch(const char *game)
{
#define CASE(s, b) if(stricmp(game, (s))==0) return (b);
	CASE("Core", 12);
	CASE("FightClub", 15);
	CASE("StarTrek", 5);
	CASE("Night", 0);
	CASE("Creatures", 2);
	CASE("Bronze", 0);
	CASE("ProjectZ", 0);
#undef CASE
	return 0;
}

static const char *fixProjCase(const char *project)
{
	if(stricmp(project, "datatools")==0)
		return "DataTools";
	if(stricmp(project, "datasrctools")==0)
		return "DataSrcTools";
	if(stricmp(project, "sound")==0)
		return "Sound";
	if(stricmp(project, "costumeartist")==0)
		return "CostumeArtist";
	if(stricmp(project, "environmentartist")==0)
		return "EnvironmentArtist";
	if(stricmp(project, "fxartist")==0)
		return "FxArtist";
	if(stricmp(project, "systemartist")==0)
		return "SystemArtist";
	if(stricmp(project, "uiartist")==0)
		return "UIArtist";
	return project;
}

static void projectListCB(char ** projects, int * max_branch, int * no_upload, int count, PCL_ErrorCode error, const char *error_details, void * userData)
{
	int i;
	eaClearStruct(&g_projects, parse_Project);
	for(i=0; i<count; i++)
	{
		if(strstri(projects[i], "sentry")) continue;
		if(strstri(projects[i], "gimme")==NULL)
		{
			Project *proj = StructCreate(parse_Project);
			strcpy(proj->name, projects[i]);
			proj->max_branches = max_branch[i];
			eaPush(&g_projects, proj);
		}
	}
	for(i=0; i<count; i++)
	{
		if(strstri(projects[i], "sentry")) continue;
		if(strstri(projects[i], "gimme")!=NULL)
		{
			FOR_EACH_IN_EARRAY(g_projects, Project, proj)
				if(strStartsWith(projects[i], proj->name))
					eaPush(&proj->projects, strdup(fixProjCase(projects[i]+strlen(proj->name)+5)));
			FOR_EACH_END
		}
	}
}

static void branchInfoCB(const char *name, int parent_branch, const char *warning, PCL_ErrorCode error, const char *error_details, Project *proj)
{
	Branch *branch = StructCreate(parse_Branch);
	strcpy(branch->name, name);
	branch->core_branch = parent_branch;
	branch->num = eaSize(&proj->branches);
	eaPush(&proj->branches, branch);
}

static void parseTrivia(const char *path, InstalledProject **ppInst, Project *proj, Branch **branches)
{
	char buf[1024], *name;
	int branch;
	InstalledProject *inst;

	// Check if this path has anything valid on it
	if(!triviaGetPatchTriviaForFile(SAFESTR(buf), path, "PatchProject"))
	{
		if(*ppInst)
			StructDestroy(parse_InstalledProject, *ppInst);
		*ppInst = NULL;
		return;
	}
	
	// Parse the project
	inst = StructCreate(parse_InstalledProject);
	if(name = strstri(buf, "Gimme"))
	{
		name += 5;
	}
	else
		name = buf;
	estrCopy2(&inst->project, fixProjCase(name));

	// Parse the branch
	triviaGetPatchTriviaForFile(SAFESTR(buf), path, "PatchBranch");
	branch = strtol(buf, &name, 10);
	assertmsgf(buf != name, "Can't parse %s as a branch number", buf);
	inst->branch = eaGet(&branches, branch);
	assertmsgf(inst->branch, "Can't find branch %d", branch);

	// Parent pointer
	inst->parent = proj;

	// Compute the core folder
	if(proj->core)
	{
		if(strstri(path, "fix"))
			estrPrintf(&inst->core_folder, "%sCore", path);
		else
			estrCopy2(&inst->core_folder, "C:/Core");
	}


	*ppInst = inst;
}

char * getExtraFolders(const char *path, bool fix)
{
	char *folders = NULL;
	char relevantFolders[MAX_PATH] = "";
	char nonRelevantFolders[MAX_PATH] = "";

	FOR_EACH_IN_EARRAY(g_projects, Project, proj)
		if (proj->local)
		{
			if (strstri(path, proj->name))
			{
				if(fix)
				{
					strcatf(relevantFolders, " -extra C:/%s", proj->name);
				}
				else if (proj->fix)
				{
					strcatf(relevantFolders, " -extra C:/%sFix", proj->name);
				}
			}
			else if (strstri(proj->name, "Core") || strstri(proj->name, "Cryptic"))
			{
				strcatf(relevantFolders, " -extra C:/%s", proj->name);
				if (proj->fix)
				{
					strcatf(relevantFolders, " -extra C:/%sFix", proj->name);
				}
			}
			else
			{
				strcatf(nonRelevantFolders, " -extra C:/%s", proj->name);
				if (proj->fix)
				{
					strcatf(nonRelevantFolders, " -extra C:/%sFix", proj->name);
				}
			}
		}
	FOR_EACH_END

	estrPrintf(&folders, "-fileOverlay%s%s", relevantFolders, nonRelevantFolders);
	return folders;
}

//void scanFSProject(const char *project)
//{
//	Project *proj = getProject(project);
//	char buf[MAX_PATH];
//	assertmsgf(proj, "Trying to scan invalid project %s", project);
//
//	sprintf(buf, "C:/%s", proj->name);
//	parseTrivia(buf, &proj->local, proj, proj->branches);
//
//	if(proj->core)
//	{
//		// Only do for !=Core
//		sprintf(buf, "C:/%sFix", proj->name);
//		parseTrivia(buf, &proj->fix, proj, proj->branches);
//
//		sprintf(buf, "C:/%sFixCore", proj->name);
//		parseTrivia(buf, &proj->fixcore, proj, proj->core->branches);
//	}
//}

static void scanFS(void)
{
	char buf[MAX_PATH];
	FOR_EACH_IN_EARRAY(g_projects, Project, proj)
		sprintf(buf, "C:/%s", proj->name);
		parseTrivia(buf, &proj->local, proj, proj->branches);

		if(proj->core)
		{
			// Only do for !=Core
			sprintf(buf, "C:/%sFix", proj->name);
			parseTrivia(buf, &proj->fix, proj, proj->branches);

			sprintf(buf, "C:/%sFixCore", proj->name);
			parseTrivia(buf, &proj->fixcore, proj, proj->core->branches);
		}
	FOR_EACH_END
}

static void scanGimme(void)
{
	int i;
	Project *core_project = NULL;
	PCL_Client *client = NULL;
	PCL_ErrorCode error = pclConnectAndCreate(&client, "assetmaster", DEFAULT_PATCHSERVER_PORT, 60, commDefault(), NULL, "", NULL, NULL, NULL);
	assert(error == PCL_SUCCESS && client);
	error = pclWait(client);
	assert(error != PCL_LOST_CONNECTION);

	PCL_DO_WAIT(pclGetProjectList(client, projectListCB, NULL));

	FOR_EACH_IN_EARRAY(g_projects, Project, proj)
		PCL_DO_WAIT(pclSetViewLatest(client, proj->name, 0, NULL, false, false, NULL, NULL));
		for(i=0; i<=proj->max_branches; i++)
		{
			PCL_DO_WAIT(pclGetBranchInfo(client, i, branchInfoCB, proj));
		}

		// Find the core project
		if(stricmp(proj->name, "Core")==0)
			core_project = proj;
		
		// Set the default branch
		proj->default_branch = defaultBranch(proj->name);
	FOR_EACH_END
	pclDisconnectAndDestroy(client);

	assertmsg(core_project, "Couldn't find Core project, something is very wrong");

	// Set all the core pointers
	FOR_EACH_IN_EARRAY(g_projects, Project, proj)
		if(stricmp(proj->name, "Core")!=0)
			proj->core = core_project;
	FOR_EACH_END
}

void populateDB(void)
{
	if(!g_projects)
		scanGimme();
	scanFS();
}

Project *getProject(const char *name)
{
	FOR_EACH_IN_EARRAY(g_projects, Project, proj)
		if(stricmp(proj->name, name)==0)
			return proj;
	FOR_EACH_END
	return NULL;
}

InstalledProject *getInstalledProject(const char *path)
{
	char buf[MAX_PATH], *tmp;
	bool fix=false, core=false;
	Project *proj;

	// Trim off the drive letter
	tmp = strstri(path, ":/");
	if(tmp)
		path = tmp + 2;
	else
	{
		tmp = strstri(path, ":\\");
		if(tmp)
			path = tmp + 2;
	}

	// Copy to a buffer
	strcpy(buf, path);
	forwardSlashes(buf);

	// Trim after the first /
	tmp = strchr(buf, '/');
	if(tmp) *tmp = '\0';

	tmp = strstri(buf, "core");
	if(tmp && tmp != buf) // The != is so that we don't flag on C:/Core
	{
		core = true;
		*tmp = '\0';
	}

	tmp = strstri(buf, "fix");
	if(tmp)
	{
		fix = true;
		*tmp = '\0';
	}

	proj = getProject(buf);
	if(!proj)
		return NULL;

	if(fix && core)
		return proj->fixcore;
	else if(fix)
		return proj->fix;
	else
		return proj->local;
}

#include "AutoGen/db_h_ast.c"