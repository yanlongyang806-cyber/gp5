#pragma once

// Need pre-def for the .core self reference
typedef struct Project Project;

AUTO_STRUCT;
typedef struct Branch
{
	int num;
	char name[64];
	int core_branch;
} Branch;

AUTO_STRUCT;
typedef struct InstalledProject
{
	Branch *branch; AST(UNOWNED)
	char *project; AST(ESTRING)
	char *core_folder; AST(ESTRING)
	Project *parent; NO_AST
} InstalledProject;

AUTO_STRUCT;
typedef struct Project
{
	// Data from the server
	char name[64];
	int max_branches;
	Branch **branches;
	char **projects;

	// Data about the local FS
	InstalledProject *local;
	InstalledProject *fix;
	InstalledProject *fixcore;

	// Pointer to the Core project because you need it often
	Project *core; AST(UNOWNED)

	// Default branch for new installs
	int default_branch;
} Project;

extern Project **g_projects;

void populateDB(void);
char * getExtraFolders(const char *path, bool fix);
Project *getProject(const char *name);
InstalledProject *getInstalledProject(const char *path);