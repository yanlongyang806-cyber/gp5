
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndTargetClass);
typedef struct TargetList
{
	char *name; AST(STRUCTPARAM)
	char **target; AST(NAME(Target))
} TargetList;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct TargetFallback
{
	const char *src; AST(STRUCTPARAM)
	const char *dst; AST(STRUCTPARAM)
} TargetFallback;

typedef struct Project Project;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndProject) AST_ENDTOK(EndProjectClass);
typedef struct Project
{
	char *name; AST(STRUCTPARAM)
	char *path; AST(NAME(Path))
	// load time:
	char **target_class_names; AST(NAME(TargetClass))
	char **project_dep_names; AST(NAME(ProjectDep))
	char **project_class_names; AST(NAME(ProjectClass)) // to inherit from
	AST_STOP
	// run time:
	char **target_classes; // Expanded list from target_class_names
	Project **project_deps; // Pointers form project_dep_names
	char *guid; // from .vcproj
	char **available_targets; // form .vcproj
	AST_START
} Project;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndSolution) AST_ENDTOK(EndSolutionClass);
typedef struct Solution
{
	char *name; AST(STRUCTPARAM)
	char *path; AST(NAME(Path))
	bool has_StructParserStub; AST(NAME(HasStructParserStub))
	// load time:
	char **project_names; AST(NAME(Project))
	char **solution_class_names; AST(NAME(SolutionClass))
	// run time:
	Project **projects; NO_AST
} Solution;


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct SolutionGen
{
	TargetList **target_classes; AST(NAME(TargetClass))
	TargetFallback **target_fallbacks; AST(NAME(TargetFallback))
	char **auto_libs; AST(NAME(Lib))
	Project **projects; AST(NAME(Project))
	Project **project_classes; AST(NAME(ProjectClass)) // other projects can inherit from these
	Solution **solutions; AST(NAME(Solution))
	Solution **solution_classes; AST(NAME(SolutionClass))
	char **target_blocks; AST(NAME(TargetBlock))
} SolutionGen;

