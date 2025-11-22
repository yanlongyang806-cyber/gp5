#include "utils.h"
#include "patchtrivia.h"
#include "trivia.h"
#include "EString.h"
#include "fileWatch.h"
#include "gimmeDLLPublicInterface.h"
#include "logging.h"

#include "queue.h"
#include "db.h"
#include "AutoGen/db_h_ast.h"

AUTO_COMMAND;
void pre_switch_project(const char *path, const char *project)
{
	InstalledProject *inst;
	inst = getInstalledProject(path);
	estrCopy2(&inst->project, project);
}

AUTO_COMMAND;
void switch_project(const char *path, const char *project)
{
	TriviaList *list = triviaListGetPatchTriviaForFile(path, NULL, 0);
	char buf[1024], *tmp; 
	const char *old_project;
	char cmd[1024];
	char *extraFolders = getExtraFolders(path, strstri(path, "fix") != NULL);
	
	if(!list) { qcmdReturnFast(1); return; }
	old_project = triviaListGetValue(list, "PatchProject");
	if(!old_project) { qcmdReturnFast(2); return; }
	strcpy(buf, old_project);
	tmp = strstri(buf, "gimme");
	if(!tmp) { qcmdReturnFast(3); return; }
	tmp += 5;
	*tmp = '\0';
	triviaListPrintf(list, "PatchProject", "%s%s", buf, project);
	triviaListWritePatchTriviaToFile(list, path);
	triviaListDestroy(&list);

	sprintf(cmd, "C:/Night/tools/bin/gimme.exe %s -glvfold %s", extraFolders, path);
	estrDestroy(&extraFolders);
	filelog_printf("GimmeCtrl.log", "Running command \"%s\".", cmd);
	qcmdReturn(StartQueryableProcess(cmd, NULL, false, false, false, NULL));
}


AUTO_COMMAND;
void pre_switch_branch(const char *path, int branch)
{
	InstalledProject *inst = getInstalledProject(path);
	Project *proj;
	
	if(!inst)
		return;
	proj = inst->parent;

	if(strstri(path, "FixCore"))
		proj = proj->core;
	inst->branch = proj->branches[branch];
}

AUTO_COMMAND;
void switch_branch(const char *path, int branch)
{
	char cmd[1024];
	TriviaList *list;
	char *extraFolders = getExtraFolders(path, strstri(path, "fix") != NULL);

	// Make sure the folder we are looking at exists
	list = triviaListGetPatchTriviaForFile(path, NULL, 0);
	if(!list)
	{
		qcmdReturnFast(0);
		return;
	}
	triviaListDestroy(&list);

	sprintf(cmd, "C:/Night/tools/bin/gimme.exe %s -switchbranch %s %d", extraFolders, path, branch);
	estrDestroy(&extraFolders);
	filelog_printf("GimmeCtrl.log", "Running command \"%s\".", cmd);
	qcmdReturn(StartQueryableProcess(cmd, NULL, false, false, false, NULL));
}

AUTO_COMMAND;
void pre_install(const char *game, const char *project)
{
	Project *proj = getProject(game);
	if(!proj->local)
		proj->local = StructCreate(parse_InstalledProject);
	estrCopy2(&proj->local->project, project);
	proj->local->branch = proj->branches[proj->default_branch];
	estrCopy2(&proj->local->core_folder, "C:/Core");
	proj->local->parent = proj;
}

AUTO_COMMAND;
void install(const char *game, const char *project)
{
	QueryableProcessHandle *handle;
	char cmd[1024];
	TriviaList *list;
	char *extraFolders = getExtraFolders(game, false);
	
	// Check if the game is already installed
	sprintf(cmd, "C:/%s", game);
	list = triviaListGetPatchTriviaForFile(cmd, NULL, 0);
	if(list)
	{
		triviaListDestroy(&list);
		qcmdReturnFast(0);
		return;
	}

	sprintf(cmd, "C:/Night/tools/bin/gimme %s -install C:/%s %s %s", extraFolders, game, game, project);
	estrDestroy(&extraFolders);
	filelog_printf("GimmeCtrl.log", "Running command \"%s\".", cmd);
	handle = StartQueryableProcess(cmd, NULL, false, false, false, NULL);
	qcmdReturn(handle);
}

typedef enum MakeFixState 
{
	MFS_START,
	MFS_INSTALLFIX,
	MFS_INSTALLCOREFIX,
	MFS_FIXFILEWATCHER,
	MFS_END,
} MakeFixState;

typedef struct MakeFixArgs
{
	Project *proj;
	MakeFixState state;
} MakeFixArgs;

AUTO_COMMAND;
void pre_make_fix_branch(const char *game)
{
	Project *proj = getProject(game);
	int core_branch = proj->branches[proj->local->branch->num-1]->core_branch;
	// Setup the DB to match the incoming changes
	if(!proj->fix)
	{
		proj->fix = StructCreate(parse_InstalledProject);
		estrPrintf(&proj->fix->core_folder, "C:/%sFixCore", proj->name);
		proj->fix->parent = proj;
	}
	estrCopy(&proj->fix->project, &proj->local->project);
	proj->fix->branch = proj->branches[proj->local->branch->num-1];
	if(proj->core && core_branch != GIMME_BRANCH_UNKNOWN)
	{
		if(!proj->fixcore)
		{
			proj->fixcore = StructCreate(parse_InstalledProject);
			proj->fixcore->parent = proj;
		}
		estrCopy(&proj->fixcore->project, &proj->core->local->project);
		proj->fixcore->branch = proj->core->branches[core_branch];
	}
}

static int make_fix_cb(int rv, MakeFixArgs *args)
{
	Project *proj = args->proj;
	char path[MAX_PATH], cmd[1024];
	FWStatType statbuf = {0};
	int core_branch;
	char *extraFolders = getExtraFolders(proj->name, true);

	// Yes, the fallthrough is intentional. This whole thing is a structured goto more or less.
	switch(args->state)
	{
	case MFS_START:

		sprintf(cmd, "C:/Night/tools/bin/gimme %s -installfix C:/%sFix C:/%s %d", extraFolders, proj->name, proj->name, proj->local->branch->num-1);
		args->state = MFS_INSTALLFIX;
		break;
	case MFS_INSTALLFIX:

		core_branch = proj->branches[proj->local->branch->num-1]->core_branch;
		if(proj->core && core_branch != GIMME_BRANCH_UNKNOWN)
		{
			
			sprintf(cmd, "C:/Night/tools/bin/gimme %s -installfix C:/%sFixCore C:/Core %d", extraFolders, proj->name, core_branch);
			args->state = MFS_INSTALLCOREFIX;
			break;
		}

	case MFS_INSTALLCOREFIX:

		{
			TriviaList *list;
			sprintf(path, "C:/%sFix", proj->name);
			list = triviaListGetPatchTriviaForFile(path, NULL, 0);
			if(!list)
				rv = 1;
			else
			{
				triviaListRemoveEntry(list, "AddedToFilewatcher");
				triviaListWritePatchTriviaToFile(list, path);
				triviaListDestroy(&list);
				args->state = MFS_FIXFILEWATCHER;
			}
		}
	case MFS_FIXFILEWATCHER:

	case MFS_END:
		SAFE_FREE(args);
		return rv;
	default:
		assert(0);
	}
	estrDestroy(&extraFolders);
	filelog_printf("GimmeCtrl.log", "Running command \"%s\".", cmd);
	qcmdReturn(StartQueryableProcess(cmd, NULL, false, false, false, NULL));
	qcmdCallback(make_fix_cb, args);
	return rv;
}

AUTO_COMMAND;
void make_fix_branch(const char *game)
{
	Project *proj = getProject(game);
	MakeFixArgs *args;
	TriviaList *list;
	char buf[MAX_PATH];
	// Validation
	assertmsgf(proj->local, "Game %s isn't installed", game);
	assertmsgf(proj->local->branch->num > 0, "Game %s is still on branch 0", proj->name);
	if(proj->core)
		assertmsgf(proj->core->local, "Game %s requires core, but Core isn't installed to make a fix", proj->name); // If this ever happens, shoot the person

	// Check if the fix branch is already installed
	sprintf(buf, "C:/%sFix", game);
	list = triviaListGetPatchTriviaForFile(buf, NULL, 0);
	if(list)
	{
		triviaListDestroy(&list);
		qcmdReturnFast(0);
		return;
	}

	// Setup the state machine
	args = calloc(1, sizeof(MakeFixArgs));
	args->state = MFS_START;
	args->proj = proj;

	// Run the first state
	make_fix_cb(0, args);
}