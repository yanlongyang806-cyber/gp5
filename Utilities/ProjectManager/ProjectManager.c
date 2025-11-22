#include "XMLParsing.h"
#include "file.h"
#include "utilitiesLib.h"
#include "sysutil.h"
#include "gimmeDLLWrapper.h"
#include "cmdparse.h"
#include "textparser.h"
#include "earray.h"
#include "EString.h"
#include "StringUtil.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "fileWatch.h"

bool bDidSomething=false;
bool bWriteModeVS2010=false;
int globalret=0;

typedef struct GenericXMLStruct GenericXMLStruct;
AUTO_STRUCT;
typedef struct GenericXMLStruct
{
	XML_Parser parser; NO_AST
	GenericXMLStruct *parentState; NO_AST // Don't destroy parents recursively
	GenericXMLStruct **children;
	char *el;
	char *characters;
	char **attrKeys;
	char **attrValues;
	bool bNonEmpty;
	bool bMultiLine; // multiple input lines
} GenericXMLStruct;

#include "AutoGen/ProjectManager_c_ast.c"

static void genericXMLStart(GenericXMLStruct *state, const char *el, const char **attr)
{
	int i;
	GenericXMLStruct *newState = StructCreate(parse_GenericXMLStruct);
	newState->el = strdup(el);
	newState->parentState = state;
	newState->parser = state->parser;
	for (i=0; *attr; i++, attr+=2)
	{
		eaPush(&newState->attrKeys, strdup(attr[0]));
		eaPush(&newState->attrValues,  strdup(attr[1]));
	}

	state->bNonEmpty = true;
	eaPush(&state->children, newState);

	XML_SetUserData(state->parser, newState);
}

static void genericXMLEnd(GenericXMLStruct *state, const char *el)
{
	if (state)
	{
		XML_SetUserData(state->parser, state->parentState);
	}
}


static void genericXMLCharacters (GenericXMLStruct *state, const XML_Char *s, int len)
{
	if (state)
	{
		bool bAllWhiteSpace=true;
		const char *s2;
		int count;
		for (s2=s, count=len; count; s2++, count--)
			if (!IS_WHITESPACE(*s2))
				bAllWhiteSpace = false;
		if (!bAllWhiteSpace)
		{
			char *buffer;
			if (state->characters)
			{
				size_t oldlen = strlen(state->characters);
				buffer = realloc(state->characters, oldlen + len+2);
				buffer[oldlen] = '\n';
				strncpy_s(buffer+oldlen+1, oldlen+len+2, s, len);
				buffer[oldlen+len+1] = 0;
				state->characters = buffer;
				state->bMultiLine = true;
			} else {
				buffer = malloc(len+1);
				strncpy_s(buffer, len+1, s, len);
				buffer[len] = 0;
				state->characters = buffer;
			}
		}
		state->bNonEmpty = true;
	}
}

static char *xmlEscape(const char *str, char *out, int out_size, bool bDoQuotes)
{
	strcpy_s(SAFESTR2(out), str);
	ReplaceStrings(SAFESTR2(out), "&", "&amp;", false);
	if (bDoQuotes)
		ReplaceStrings(SAFESTR2(out), "\"", "&quot;", false);
	ReplaceStrings(SAFESTR2(out), "<", "&lt;", false);
	ReplaceStrings(SAFESTR2(out), ">", "&gt;", false);
	ReplaceStrings(SAFESTR2(out), "\n", "&#x0A;", false);
	ReplaceStrings(SAFESTR2(out), "\r", "&#x0D;", false);
	return out;
}

static void printTabs(FILE *fout, int num)
{
	int i;
	for (i=0; i<num; i++)
		fprintf(fout, "%s", (bWriteModeVS2010?"  ":"\t"));
}

static void genericXMLWrite(FILE *fout, GenericXMLStruct *state, int tabs)
{
	int i;
	static char buf1[32768];
	bool bOneLineData=false;
	if (state->el)
	{
		printTabs(fout, tabs);
		if (bWriteModeVS2010)
		{
			fprintf(fout, "<%s%s", state->el,
				eaSize(&state->attrKeys)?" ":"");
			for (i=0; i<eaSize(&state->attrKeys); i++)
			{
				fprintf(fout, "%s=\"%s\"%s", state->attrKeys[i], xmlEscape(state->attrValues[i], SAFESTR(buf1), true),
					(i == eaSize(&state->attrKeys)-1)?"":" ");
			}
		} else {
			if (eaSize(&state->attrKeys))
			{
				fprintf(fout, "<%s\n", state->el);
			} else {
				fprintf(fout, "<%s", state->el);
			}
			for (i=0; i<eaSize(&state->attrKeys); i++)
			{
				printTabs(fout, tabs+1);
				fprintf(fout, "%s=\"%s\"\n", state->attrKeys[i], xmlEscape(state->attrValues[i], SAFESTR(buf1), true));
			}
		}
		if (!state->bNonEmpty)
		{
			if (!bWriteModeVS2010)
				if (eaSize(&state->attrKeys))
					printTabs(fout, tabs);
			if (bWriteModeVS2010)
				fprintf(fout, " />\n");
			else
				fprintf(fout, "/>\n");
		} else {
			if (!bWriteModeVS2010)
				if (eaSize(&state->attrKeys))
					printTabs(fout, tabs+1);
			if (bWriteModeVS2010 && eaSize(&state->children)==0 && state->characters && (strchr(state->characters, '\n')==0 || state->bMultiLine))
				bOneLineData = true;

			if (bOneLineData)
				fprintf(fout, ">");
			else
				fprintf(fout, ">\n");
		}
	} else {
		assert(!eaSize(&state->attrKeys));
	}
	for (i=0; i<eaSize(&state->children); i++)
		genericXMLWrite(fout, state->children[i], tabs+1);
	if (state->characters)
	{
		if (!bOneLineData)
			printTabs(fout, tabs+1);
		if (state->bMultiLine && bWriteModeVS2010)
		{
			fprintf(fout, "%s%s", state->characters, strEndsWith(state->characters, "\n")?"":"\n");
		} else
			fprintf(fout, "%s", xmlEscape(state->characters, SAFESTR(buf1), false));
	}
	if (state->el)
	{
		if (state->bNonEmpty)
		{
			if (!bOneLineData)
				printTabs(fout, tabs);
			if (tabs == 0) // closing the very last thing, no CR
				fprintf(fout, "</%s>", state->el);
			else
				fprintf(fout, "</%s>\n", state->el);
		}
	}
}

int cmpImportGroup(const GenericXMLStruct *e1, const GenericXMLStruct *e2)
{
	int i1, i2;
	static const char *ranked[] = {
		"..\\..\\PropertySheets\\GeneralSettings.props",
		"..\\..\\PropertySheets\\StructParser.props",
		"..\\..\\PropertySheets\\WarningLevel4.props",
		"..\\..\\PropertySheets\\CrypticApplication.props",
		"..\\..\\PropertySheets\\GameClient.props",
		"..\\..\\PropertySheets\\LinkerOptimizations.props",
		"..\\..\\PropertySheets\\x64.props",
		"..\\..\\PropertySheets\\NoAnalyze.props",
	};
	assert(eaSize(&e1->attrKeys)==1 && eaSize(&e2->attrKeys)==1);
	assert(eaSize(&e1->attrValues)==1 && eaSize(&e2->attrValues)==1);

	for (i1=0; i1<ARRAY_SIZE(ranked); i1++)
	{
		if (stricmp(ranked[i1], e1->attrValues[0])==0)
			break;
	}
	if (i1 == ARRAY_SIZE(ranked))
		printf("Warning: referencing unknown/unranked property sheet: %s\n", e1->attrValues[0]);

	for (i2=0; i2<ARRAY_SIZE(ranked); i2++)
	{
		if (stricmp(ranked[i2], e2->attrValues[0])==0)
			break;
	}
	if (i2 == ARRAY_SIZE(ranked))
		printf("Warning: referencing unknown/unranked property sheet: %s\n", e2->attrValues[0]);

	return i1 - i2;

}

int cmpElem(const GenericXMLStruct **e1, const GenericXMLStruct **e2)
{
	int ret;
	bool bIsImportGroup = (stricmp((*e1)->el, "Import")==0);
	if (!(*e1)->el)
	{
		if (!(*e2)->el)
			return 0;
		return -1;
	} else if (!(*e2)->el)
		return 1;
	ret = stricmp((*e1)->el, (*e2)->el);
	if (ret)
		return ret;
	if (bIsImportGroup)
	{
		ret = cmpImportGroup(*e1, *e2);
		if (ret)
			return ret;
	}

	// Check attribute keys
	{
		int s1 = eaSize(&(*e1)->attrKeys);
		int s2 = eaSize(&(*e2)->attrKeys);
		int i;
		for (i=0; i<MIN(s1, s2); i++)
		{
			ret = stricmp((*e1)->attrKeys[i], (*e2)->attrKeys[i]);
			if (ret)
				return ret;
			ret = stricmp((*e1)->attrValues[i], (*e2)->attrValues[i]);
			if (ret)
				return ret;
		}
		if (s1 != s2)
			return s2 - s1;
	}
	// Check children
	{
		int s1 = eaSize(&(*e1)->children);
		int s2 = eaSize(&(*e2)->children);
		int i;
		for (i=0; i<MIN(s1, s2); i++)
		{
			ret = cmpElem(&(*e1)->children[i], &(*e2)->children[i]);
			if (ret)
				return ret;
		}
		if (s1 != s2)
			return s2 - s1;
	}
	return 0;
}

void sort(GenericXMLStruct *elem)
{
	int i;
	int starti=0;
	//eaQSort(elem->children, cmpElem);
	// Sort only like children
	for (i=0; i<eaSize(&elem->children); i++)
	{
		if (!elem->children[starti]->el)
			starti = i;
		else {
			if (0!=stricmp(elem->children[starti]->el, elem->children[i]->el))
			{
				qsort(&elem->children[starti], i - starti, sizeof(elem->children[0]), cmpElem);
				starti = i;
			}
		}
		sort(elem->children[i]);
	}
	if (starti < eaSize(&elem->children)-1)
		qsort(&elem->children[starti], eaSize(&elem->children) - starti, sizeof(elem->children[0]), cmpElem);
}

void stripXboxSub(GenericXMLStruct *elem)
{
	int i;
	for (i = eaSize(&elem->children)-1; i>=0; i--)
	{
		GenericXMLStruct *e2 = elem->children[i];
		if (stricmp(e2->el, "FileConfiguration")==0 || stricmp(e2->el, "Platform")==0 || stricmp(e2->el, "Configuration")==0)
		{
			if (eaSize(&e2->attrKeys) && stricmp(e2->attrKeys[0], "Name")==0 &&
				(strstri(e2->attrValues[0], "Xbox") || strstri(e2->attrValues[0], "Profile")))
			{
				// remove it
				eaRemove(&elem->children, i);
				continue;
			}
		}
		stripXboxSub(e2);
	}
}

typedef void (*ProcessFunc)(GenericXMLStruct *elem);

void processFile(const char *projectname, ProcessFunc func)
{
	int len;
	char *data = fileAlloc(projectname, &len);
	char *freeme = data;
	const static char *header1 = "<?xml version=\"1.0\" encoding=\"Windows-1252\"?>";
	const static char *header2 = "<?xml version=\"1.0\" encoding=\"shift_jis\"?>";
	static U8 UTF8BOM[] = {0xEF, 0xBB, 0xBF};
	const static char *headerVS2010 = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
	bool bHeader1=false;
	bool bHeader2=false;
	bool bHeaderVS2010=false;
	bool bHasUTF8header=false;
	char *headerData = data;
	if (!data)
	{
		printf("File '%s' not found\n", projectname);
		return;
	}

	if (memcmp(headerData, UTF8BOM, 3)==0)
	{
		headerData += 3;
		bHasUTF8header = true;
	}
	if (strStartsWith(headerData, header1))
	{
		// Handled by expat form JDrago's change
// 		memmove(strstr(data, "Windows-1252\""),
// 							 "iso-8859-1\"  ",
// 					  strlen("iso-8859-1\"  "));

		bHeader1 = true;
	}
	if (strStartsWith(headerData, headerVS2010))
	{
		bHeaderVS2010 = true;
		bWriteModeVS2010 = true;
	}
	if (strStartsWith(headerData, header2))
	{
		int size = len+100;
		char *newbuf = malloc(size);
		strcpy_s(newbuf, size, data);
		free(data);
		headerData = freeme = data = newbuf;
		strstriReplace_s(newbuf, size, "shift_jis", "iso-8859-1");
		len = (int)strlen(newbuf);

		bHeader2 = true;
	}

	bDidSomething = true;

	{
		GenericXMLStruct root = {0};
		XML_Parser p = XML_ParserCreate(NULL);
		int xmlRet;
		assert(p);

		root.parser = p;
		XML_SetUserData(p, &root);

		XML_SetElementHandler(p, genericXMLStart, genericXMLEnd);
		XML_SetCharacterDataHandler(p, genericXMLCharacters);
		// XML_SetXmlDeclHandler if we need to save/write the encoding as well

		xmlRet = XML_Parse(p, data, len, true);
		if (xmlRet == 0)
		{
			globalret = -1;
			printf("XML Parsing error: %s\n", XML_ErrorString(	XML_GetErrorCode(p) ));
		}
		XML_ParserFree(p);

		// Sort
		func(&root);

		// Write
		if (xmlRet != 0)
		{
			int r;
			char dest[MAX_PATH];
			char bakname[MAX_PATH];
			FILE *fout;
			changeFileExt(projectname, ".tmp", dest);
			changeFileExt(projectname, ".vcproj.bak", bakname);
			fout = fopen(dest, "wt");
			if (bHasUTF8header)
				fwrite(UTF8BOM, 1, 3, fout);
			if (bHeader1)
				fprintf(fout, "%s\n", header1);
			if (bHeader2)
				fprintf(fout, "%s\n", header2);
			if (bHeaderVS2010)
				fprintf(fout, "%s\n", headerVS2010);
			genericXMLWrite(fout, &root, -1);
			fclose(fout);
			if (fileCompare(projectname, dest)==0)
			{
				fileForceRemove(dest);
			} else {
				fileRenameToBak(projectname);
				do {
					r = rename(dest, projectname);
					if (r!=0) {
						printf("Trying to rename '%s' to '%s'...\n",
							dest, projectname);
						fileRenameToBak(projectname);
						Sleep(100);
					}
				} while (r != 0);
				fileForceRemove(bakname);
			}
		}

		StructDeInit(parse_GenericXMLStruct, &root);
	}

	fileFree(freeme);
}


AUTO_COMMAND;
void massage(const char *projectname)
{
	processFile(projectname, sort);
}

AUTO_COMMAND;
void stripXbox(const char *projectname)
{
	processFile(projectname, stripXboxSub);
}


int main(int argc, char **argv)
{
	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS
	fileWatchSetDisabled(true);
	dontLogErrors(true);
	gimmeDLLDisable(true);
	fileAllPathsAbsolute(true);
	utilitiesLibStartup();
	cmdParseCommandLine(argc, argv);

	if (!bDidSomething)
	{
		printf("Usage:\n"
			"  -massage file.vcproj     consistifies a project file\n");
		return 1;
	}

	return globalret;
}