#include "SolutionGen.h"
#include "AutoGen/SolutionGen_h_ast.c"
#include "utilitiesLib.h"
#include "sysutil.h"
#include "file.h"
#include "qsortG.h"
#include <conio.h>


#define VS2010

char root[MAX_PATH];

bool eaPushUniqueString(char ***ea, char *s)
{
	FOR_EACH_IN_EARRAY(*ea, char, s2)
	{
		if (stricmp(s, s2)==0)
			return false;
	}
	FOR_EACH_END;
	eaPush(ea, s);
	return true;
}

Project *findProjectClass(SolutionGen *sg, const char *project_class)
{
	FOR_EACH_IN_EARRAY(sg->project_classes, Project, proj)
	{
		if (stricmp(proj->name, project_class)==0)
			return proj;
	}
	FOR_EACH_END;
	return NULL;
}

Solution *findSolutionClass(SolutionGen *sg, const char *solution_class)
{
	FOR_EACH_IN_EARRAY(sg->solution_classes, Solution, sln)
	{
		if (stricmp(sln->name, solution_class)==0)
			return sln;
	}
	FOR_EACH_END;
	return NULL;
}

TargetList *findTargetClass(SolutionGen *sg, const char *target_class_name)
{
	FOR_EACH_IN_EARRAY(sg->target_classes, TargetList, target_class)
	{
		if (stricmp(target_class->name, target_class_name)==0)
			return target_class;
	}
	FOR_EACH_END;
	return NULL;
}

Project *findProject(SolutionGen *sg, const char *project_name)
{
	FOR_EACH_IN_EARRAY(sg->projects, Project, proj)
	{
		if (stricmp(proj->name, project_name)==0)
			return proj;
	}
	FOR_EACH_END;
	return NULL;
}

Solution *findSolution(SolutionGen *sg, const char *solution_name)
{
	FOR_EACH_IN_EARRAY(sg->solutions, Solution, sln)
	{
		if (stricmp(sln->name, solution_name)==0)
			return sln;
	}
	FOR_EACH_END;
	return NULL;
}


bool solutionGenValidate(SolutionGen *sg)
{
	bool bRet = true;
	// Fill out libs into projects
	FOR_EACH_IN_EARRAY_FORWARDS(sg->auto_libs, char, libname)
	{
		Project *proj = calloc(sizeof(Project), 1);
		proj->name = strdup(libname);
		proj->path = strdupf("libs/%s/%s.vcproj", proj->name, proj->name);
		eaPush(&sg->projects, proj);
	}
	FOR_EACH_END;

	// Verify classes
	FOR_EACH_IN_EARRAY_FORWARDS(sg->project_classes, Project, proj)
	{
		if (findProjectClass(sg, proj->name) != proj)
		{
			printf("Error: Duplicate ProjectClasses named '%s'\n", proj->name);
			bRet = false;
		}
		if (proj->path)
		{
			printf("Error: ProjectClass '%s' specifies a path (classes cannot have paths)\n", proj->name);
			bRet = false;
		}
		if (eaSize(&proj->project_class_names))
		{
			printf("Error: ProjectClass '%s' specifies another ProjectClass (recursive classing not implemented)\n", proj->name);
			bRet = false;
		}
	}
	FOR_EACH_END;
	FOR_EACH_IN_EARRAY_FORWARDS(sg->solution_classes, Solution, sln)
	{
		if (findSolutionClass(sg, sln->name) != sln)
		{
			printf("Error: Duplicate SolutionClasses named '%s'\n", sln->name);
			bRet = false;
		}
		if (sln->path)
		{
			printf("Error: SolutionClass '%s' specifies a path (classes cannot have paths)\n", sln->name);
			bRet = false;
		}
		if (eaSize(&sln->solution_class_names))
		{
			printf("Error: SolutionClass '%s' specifies another SolutionClass (recursive classing not implemented)\n", sln->name);
			bRet = false;
		}
	}
	FOR_EACH_END;

	// Verify projects
	FOR_EACH_IN_EARRAY_FORWARDS(sg->projects, Project, proj)
	{
		// .vcproj must exist
		char path[MAX_PATH];
		forwardSlashes(proj->path);
#ifdef VS2010
		strcpy(path, proj->path);
		changeFileExt(path, ".vcxproj", path);
		free(proj->path);
		proj->path = strdup(path);
#endif
		sprintf(path, "%s/%s", root, proj->path);
		if (!fileExists(path))
		{
			printf("Error: Project '%s' does not exist at '%s'\n", proj->name, proj->path);
			bRet = false;
		}
		if (findProject(sg, proj->name) != proj)
		{
			printf("Error: Duplicate projects named '%s'\n", proj->name);
			bRet = false;
		}
		// fill in from class
		FOR_EACH_IN_EARRAY_FORWARDS(proj->project_class_names, char, proj_class_name)
		{
			Project *proj_class = findProjectClass(sg, proj_class_name);
			if (proj_class)
			{
				// Merge in project class
				eaPushEArray(&proj->target_class_names, &proj_class->target_class_names);
				eaPushEArray(&proj->project_dep_names, &proj_class->project_dep_names);
			} else {
				printf("Error: cannot find ProjectClass '%s'\n", proj_class_name);
				bRet = false;
			}
		}
		FOR_EACH_END;
		// Verify all target classes exist
		FOR_EACH_IN_EARRAY_FORWARDS(proj->target_class_names, char, target_class_name)
		{
			TargetList *target_class = findTargetClass(sg, target_class_name);
			if (target_class)
			{
				FOR_EACH_IN_EARRAY_FORWARDS(target_class->target, char, s)
				{
					eaPushUniqueString(&proj->target_classes, s);
				}
				FOR_EACH_END;
			}
			else
			{
				printf("Error: cannot find TargetClass '%s' required by project '%s'\n",
					target_class_name, proj->name);
				bRet = false;
			}
		}
		FOR_EACH_END;
		// Verify all deps exist
		FOR_EACH_IN_EARRAY_FORWARDS(proj->project_dep_names, char, proj_name)
		{
			Project *proj_dep = findProject(sg, proj_name);
			if (proj_dep)
			{
				if (proj_dep == findProject(sg, "StructParserStub"))
				{
					// We would have to ensure we don't write out the dependency twice if someone manually lists StructParserStub
					printf("Error: Project '%s' lists StructParserStub as a dependency, but this is handled automatically.\n",
						proj->name);
					bRet = false;
				} else {
					eaPushUnique(&proj->project_deps, proj_dep);
				}
			}
			else
			{
				printf("Error: cannot find Project '%s' listed as a dependency for '%s'\n",
					proj_name, proj->name);
				bRet = false;
			}
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	// Verify solutions
	FOR_EACH_IN_EARRAY_FORWARDS(sg->solutions, Solution, sln)
	{
		// Verify solution directories exist (okay if .sln is not there)
		char path[MAX_PATH];
		forwardSlashes(sln->path);
		sprintf(path, "%s/%s", root, sln->path);
		getDirectoryName(path);
		if (!dirExists(path))
		{
			printf("Error: Solution '%s', cannot find containing folder '%s'\n", sln->name, sln->path);
			bRet = false;
		}
		if (findSolution(sg, sln->name) != sln)
		{
			printf("Error: Duplicate solutions named '%s'\n", sln->name);
			bRet = false;
		}
		// Merge in solution classes
		FOR_EACH_IN_EARRAY_FORWARDS(sln->solution_class_names, char, solution_class_name)
		{
			Solution *sln_class = findSolutionClass(sg, solution_class_name);
			if (sln_class)
			{
				eaPushEArray(&sln->project_names, &sln_class->project_names);
				if (sln_class->has_StructParserStub)
					sln->has_StructParserStub = sln_class->has_StructParserStub;
			} else {
				printf("Error: cannot find SolutionClass '%s'\n", solution_class_name);
				bRet = false;
			}
		}
		FOR_EACH_END;
		// Verify contained projects
		FOR_EACH_IN_EARRAY_FORWARDS(sln->project_names, char, proj_name)
		{
			Project *proj = findProject(sg, proj_name);
			if (proj)
			{
				eaPushUnique(&sln->projects, proj);
			}
			else
			{
				printf("Error: cannot find project '%s' referenced by solution '%s'\n", proj_name, sln->name);
				bRet = false;
			}
		}
		FOR_EACH_END;
		if (eaSize(&sln->projects)==0)
		{
			printf("Error: Solution '%s' contains no valid projects\n", sln->name);
			bRet = false;
		}
		if (sln->has_StructParserStub)
		{
			Project *proj = findProject(sg, "StructParserStub");
			if (proj)
			{
				eaPushUnique(&sln->projects, proj);
			} else {
				printf("Error: cannot find project 'StructParserStub' referenced by solution '%s'\n", sln->name);
				bRet = false;
			}
		}
	}
	FOR_EACH_END;

	return bRet;
}

bool solutionGenParseProjects(SolutionGen *sg)
{
	bool bRet = true;
	FOR_EACH_IN_EARRAY(sg->projects, Project, proj)
	{
		char path[MAX_PATH];
		char buf[1024];
		char *data;
		char *s;
		char *context=NULL;
		int data_len;
		sprintf(path, "%s/%s", root, proj->path);
		data = fileAlloc(path, &data_len);
		if (!data)
		{
			printf("Error opening '%s' for reading\n", path);
			bRet = false;
			continue;
		}
		// Find GUID
#ifdef VS2010
#define GUID_SEARCH_STRING "<ProjectGuid>{"
#else
#define GUID_SEARCH_STRING "ProjectGUID=\"{"
#endif
		s = strstr(data, GUID_SEARCH_STRING);
		if (!s)
		{
			printf("Error reading '%s': could not find GUID\n", path);
			bRet = false;
			fileFree(data);
			continue;
		}
		strncpy(buf, s + strlen(GUID_SEARCH_STRING), 36);
		proj->guid = strdup(buf);
		// Find available targets
#define DELIMS "\t\n\r\"<>="
		s = strtok_s(data, DELIMS, &context);
		while ( s && (s = strtok_s(NULL, DELIMS, &context)) )
		{
#ifdef VS2010
#define PROJECT_TOK1 "ProjectConfiguration Include"
#define PROJECT_TOK2 0
#else
#define PROJECT_TOK1 "Configuration"
#define PROJECT_TOK2 "Name"
#endif
			if (stricmp(s, PROJECT_TOK1)==0)
			{
				if (PROJECT_TOK2)
					s = strtok_s(NULL, DELIMS, &context);
#pragma warning(disable:6286)
				if (!PROJECT_TOK2 || (s && stricmp(s, PROJECT_TOK2)==0))
				{
					s = strtok_s(NULL, DELIMS, &context);
					if (s)
					{
						FOR_EACH_IN_EARRAY(proj->available_targets, char, other_target)
						{
							if (stricmp(s, other_target)==0)
							{
								printf("Error reading '%s': found two configurations named '%s'\n", path, s);
								bRet = false;
							}
						}
						FOR_EACH_END;

						eaPush(&proj->available_targets, strdup(s));
					}
				}
			}
		}
		if (eaSize(&proj->available_targets) > 20)
		{
			printf("Warning: Found unexpected large number of configurations (%d) while parsing '%s'\n", eaSize(&proj->available_targets), path);
		}
		if (!eaSize(&proj->available_targets))
		{
			printf("Error reading '%s': found no project configurations\n", path);
			bRet = false;
			fileFree(data);
			continue;
		}

		eaQSort(proj->available_targets, strCmp);

		fileFree(data);
	}
	FOR_EACH_END;

	return bRet;
}

int cmpProjects(const void *data, const void **_a, const void **_b)
{
	Project *a = *(Project**)_a;
	Project *b = *(Project**)_b;
	if (a == data)
		return -1;
	if (b == data)
		return 1;
	return stricmp(a->name, b->name);
}

char *findClosestTarget(SolutionGen *sg, char **targets, Project *proj, char *target, bool *bCanBuild)
{
	char *best=NULL;
	assert(eaSize(&proj->available_targets));

	FOR_EACH_IN_EARRAY(sg->target_blocks, char, t)
	{
		if (stricmp(t, target)==0)
		{
			// This is a target that should never be built
			// Don't even want it referenced, use the first available target instead (should be Debug|Win32)
			*bCanBuild = false;
			return proj->available_targets[0];
		}
	}
	FOR_EACH_END;

	*bCanBuild = true;
	
	if (eaSize(&proj->target_classes))
	{
		bool bFound=false;
		// Project specifies targets, only build if in this project's list
		FOR_EACH_IN_EARRAY(proj->target_classes, char, t)
		{
			if (stricmp(t, target)==0)
				bFound = true;
		}
		FOR_EACH_END;
		if (!bFound)
			*bCanBuild = false;
	}

	FOR_EACH_IN_EARRAY(proj->available_targets, char, t)
	{
		if (stricmp(t, target)==0)
		{
			return target;
		}
		FOR_EACH_IN_EARRAY(sg->target_fallbacks, TargetFallback, fallback)
		{
			if (stricmp(fallback->src, target)==0 &&
				stricmp(fallback->dst, t)==0)
			{
				best = t;
			}
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
	if (best)
	{
		return best;
	}
	*bCanBuild = false;
	return proj->available_targets[0];
}

bool sameText(char **estr, char *file, int file_size)
{
	char *c1 = *estr;
	int s1 = estrLength(estr);
	char *c2 = file;
	int s2 = file_size;
	while (s1 && s2)
	{
		if ((*c1=='\r' || *c1=='\n') &&
			(*c2=='\r' || *c2=='\n'))
		{
			while (*c1=='\r' || *c1=='\n')
			{
				c1++;
				s1--;
			}
			while (*c2=='\r' || *c2=='\n')
			{
				c2++;
				s2--;
			}
		} else if (*c1 == *c2) {
			s1--;
			c1++;
			s2--;
			c2++;
		} else
			return false;
	}
	return !s1 && !s2;
}

bool solutionGenOutputSolution(SolutionGen *sg, Solution *sln, int VSVersion)
{
	bool bRet = true;
	Project *firstProject=sln->projects[0];
	Project **projects=NULL;
	char **targets=NULL;
	char **targets_undesired=NULL;
	char *output=NULL;
	char pathToRoot[MAX_PATH];

	// Gather all projects and their dependencies
	FOR_EACH_IN_EARRAY_FORWARDS(sln->projects, Project, proj)
	{
		eaPushUnique(&projects, proj);
	}
	FOR_EACH_END;
	do 
	{
		int oldSize=eaSize(&projects);
		FOR_EACH_IN_EARRAY_FORWARDS(projects, Project, proj)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(proj->project_deps, Project, dep)
			{
				eaPushUnique(&projects, dep);
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
		if (oldSize == eaSize(&projects))
			break;
	} while (true);
	assert(eaSize(&projects));

	// Build target list
	FOR_EACH_IN_EARRAY_FORWARDS(projects, Project, proj)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(proj->target_classes, char, target_name)
		{
			eaPushUniqueString(&targets, target_name);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	// Build list of target permutations that must be in the sln file but we do not want to build
	{
		char **platforms=NULL;
		char **configs=NULL;
		FOR_EACH_IN_EARRAY(targets, char, target)
		{
			char buf[1024];
			char *s;
			strcpy(buf, target);
			s = strchr(buf, '|');
			assert(s);
			*s = '\0';
			s = strdup(s+1);
			if (!eaPushUniqueString(&platforms, s))
				free(s);
			s = strdup(buf);
			if (!eaPushUniqueString(&configs, s))
				free(s);
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY(platforms, char, platform)
		{
			FOR_EACH_IN_EARRAY(configs, char, config)
			{
				char target[1024];
				char *s;
				sprintf(target, "%s|%s", config, platform);
				s = strdup(target);
				if (eaPushUniqueString(&targets, s))
				{
					eaPush(&targets_undesired, s);
				} else {
					free(s);
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;

		eaDestroyEx(&platforms, NULL);
		eaDestroyEx(&configs, NULL);
	}

	// Sort for consistency
	eaQSort_s(projects, cmpProjects, firstProject);
	eaQSort(targets, strCmp);

	{
		char *s=sln->path;
		strcpy(pathToRoot, "");
		while (s=strchr(s, '/'))
		{
			s++;
			strcat(pathToRoot, "../");
		}
	}

	estrCreate(&output);
	{
		static const U8 UTF8BOM[] = {0xEF, 0xBB, 0xBF};
		estrConcat(&output, UTF8BOM, 3);
	}
	// Write out project block
	estrConcatf(&output, "\n");
	if (VSVersion == 10)
	{
		estrConcatf(&output, "Microsoft Visual Studio Solution File, Format Version 11.00\n");
		estrConcatf(&output, "# Visual Studio 2010\n");
	} else {
		estrConcatf(&output, "Microsoft Visual Studio Solution File, Format Version 9.00\n");
		estrConcatf(&output, "# Visual Studio 2005\n");
	}
	estrConcatf(&output, "# Generated by SolutionGen\n");
	FOR_EACH_IN_EARRAY_FORWARDS(projects, Project, proj)
	{
		char relpath[MAX_PATH];
		sprintf(relpath, "%s%s", pathToRoot, proj->path);
		backSlashes(relpath);
		if (VSVersion == 10)
		{
			changeFileExt(relpath, ".vcxproj", relpath);
		}
		estrConcatf(&output, "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"%s\", \"%s\", \"{%s}\"\n",
			proj->name, relpath, proj->guid);

		if (VSVersion == 8) // 10's dependencies are in project files, I guess
		{
			if (eaSize(&proj->project_deps) || sln->has_StructParserStub)
			{
				estrConcatf(&output, "	ProjectSection(ProjectDependencies) = postProject\n");
				FOR_EACH_IN_EARRAY_FORWARDS(proj->project_deps, Project, dep)
				{
					estrConcatf(&output, "		{%s} = {%s}\n", dep->guid, dep->guid);
				}
				FOR_EACH_END;
				if (sln->has_StructParserStub && (stricmp(proj->name, "StructParserStub")!=0))
				{
					Project *dep = findProject(sg, "StructParserStub");
					estrConcatf(&output, "		{%s} = {%s}\n", dep->guid, dep->guid);
				}
				estrConcatf(&output, "	EndProjectSection\n");
			}
		}
		estrConcatf(&output, "EndProject\n");
	}
	FOR_EACH_END;

	// Output build configuration
	estrConcatf(&output, "Global\n");
	// Available targets
	estrConcatf(&output, "	GlobalSection(SolutionConfigurationPlatforms) = preSolution\n");
	FOR_EACH_IN_EARRAY_FORWARDS(targets, char, target)
	{
		estrConcatf(&output, "		%s = %s\n", target, target);
	}
	FOR_EACH_END;
	estrConcatf(&output, "	EndGlobalSection\n");

	// Project active configurations
	estrConcatf(&output, "	GlobalSection(ProjectConfigurationPlatforms) = postSolution\n");
	FOR_EACH_IN_EARRAY_FORWARDS(projects, Project, proj)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(targets, char, target)
		{
			bool bCanBuild;
			char *closest = findClosestTarget(sg, targets, proj, target, &bCanBuild);
			bool bUndesired=false;
			FOR_EACH_IN_EARRAY(targets_undesired, char, undesired)
			{
				if (stricmp(target, undesired)==0)
					bUndesired = true;
			}
			FOR_EACH_END;
			estrConcatf(&output, "		{%s}.%s.ActiveCfg = %s\n",
				proj->guid, target, closest);
			if (bCanBuild && !bUndesired)
			{
				estrConcatf(&output, "		{%s}.%s.Build.0 = %s\n",
					proj->guid, target, closest);
			}
		}
		FOR_EACH_END
	}
	FOR_EACH_END;

	estrConcatf(&output, "	EndGlobalSection\n");
	estrConcatf(&output, "	GlobalSection(SolutionProperties) = preSolution\n");
	estrConcatf(&output, "		HideSolutionNode = FALSE\n");
	estrConcatf(&output, "	EndGlobalSection\n");
	estrConcatf(&output, "EndGlobal\n");

	// Write file if needed
	{
		FILE *fout;
		char path[MAX_PATH];
		int data_len;
		char *data;
		bool bDirty=true;
		sprintf(path, "%s/%s", root, sln->path);
		// Could do this if we want them side-by-side
		if (VSVersion == 8)
			changeFileExt(path, "_vs2005.sln", path);
		data = fileAlloc(path, &data_len);
		if (data)
		{
			if (sameText(&output, data, data_len))
				bDirty = false;
			fileFree(data);
		}
		if (bDirty)
		{
			fout = fopen(path, "w");
			if (!fout)
			{
				printf("Error opening '%s' for writing\n", path);
				bRet = false;
			} else {
				fwrite(output, 1, estrLength(&output), fout);
				fclose(fout);
			}
		}
	}
	estrDestroy(&output);
	eaDestroy(&projects);
	eaDestroy(&targets);
	eaDestroy(&targets_undesired);
	return bRet;
}

bool solutionGenOutput(SolutionGen *sg)
{
	bool bRet = true;
	FOR_EACH_IN_EARRAY(sg->solutions, Solution, sln)
	{
#ifndef VS2010
		bRet &= solutionGenOutputSolution(sg, sln, 8);
#else
		bRet &= solutionGenOutputSolution(sg, sln, 10);
#endif
	}
	FOR_EACH_END;

	return bRet;
}

void pak(void)
{
	char c;
	printf("Press any key to continue...\n");
	while (_kbhit())
		c = _getch();
	c = _getch();
	while (_kbhit())
		c = _getch();
}


int wmain(int argc, S16** argv_wide)
{
	int ret=0;
	char *s;
	int i;
	SolutionGen sg = {0};
	char path[MAX_PATH];
	char **argv;

	setCavemanMode();

	ARGV_WIDE_TO_ARGV

	DO_AUTO_RUNS;

	getExecutableDir(root); // x:/folder/Utilities/bin
	forwardSlashes(root);
	for (i=0; i<2; i++)
	{
		s = strrchr(root, '/');
		if (!s)
		{
			printf("error parsing path\n");
			pak();
			return -3;
		}
		*s = '\0';
	}
	// root is now x:/folder

	sprintf(path, "%s/MasterSolution/SolutionGen.txt", root);

	if (!ParserLoadFiles(NULL, path, NULL, 0, parse_SolutionGen, &sg))
	{
		printf("Error parsing solution gen file, exiting.\n\n");
		pak();
		return -1;
	} else {
		printf("\nParsed SolutionGen file successfully.\n  ");
		// Validate
		if (!solutionGenValidate(&sg))
		{
			printf("Validation error(s) detected, exiting.\n\n");
			pak();
			return -2;
		}
		printf("%d ProjectClasses, ", eaSize(&sg.project_classes));
		printf("%d Projects (%d Libs), ", eaSize(&sg.projects), eaSize(&sg.auto_libs));
		printf("%d SolutionClasses, ", eaSize(&sg.solution_classes));
		printf("%d Solutions, ", eaSize(&sg.solutions));
		printf("%d TargetClasses, ", eaSize(&sg.target_classes));
		printf("%d TargetFallbacks", eaSize(&sg.target_fallbacks));
		printf("\n");

		// Read info from project files (GUIDs, valid targets)
		if (!solutionGenParseProjects(&sg))
		{
			printf("Error(s) parsing projects, exiting.\n\n");
			pak();
			return -4;
		}

		// Generate output solution files
		if (!solutionGenOutput(&sg))
		{
			printf("Error(s) writing solutions, exiting.\n\n");
			pak();
			return -5;
		}
	}
	printf("\n");
	return ret;
}