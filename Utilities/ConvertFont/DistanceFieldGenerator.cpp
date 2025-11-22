C_DECLARATIONS_BEGIN
#include "stdtypes.h"
#include "mathutil.h"
#include "earray.h"
#include "EString.h"
#include "error.h"
#include "file.h"
C_DECLARATIONS_END

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <conio.h>
#include <windows.h>
#include <limits.h>

#include "ImageHandler.h"
#include "GenFont.h"
#include "GfxFontStructs.h"

int g_dest_spread=8;
char *g_image_name=NULL;
char *g_output_name=NULL;
char *g_image_dir=NULL;
int g_outSize[2] = {128, 128};
char *g_font_name=NULL;
int g_font_size=256;
int g_font_atlas_size[2] = {1024, 1024};
bool g_keep_temp_files = false;
bool g_bold = false;
wchar_t* g_exclude_char = L"";
wchar_t* g_include_char = L"";
char* g_font_subsitutions[3] = {0,0,0}; //bold, italic, bold+italic
float g_smoothing = 1.0f;
float g_density_offset = 0.0f;
int g_override_padding[2] = {-1, -1};
wchar_t g_max_glyph = 0xFFFF;
wchar_t g_min_glyph = 0x01; // Min is 1, since our stashtable can't store 0 as a key, and who would want to print '\0'?!
bool g_ignore_bold = false;
bool g_ignore_italic = false;
float g_spacing = 1.0f;
float g_outline_smoothing = -1.0f; //-1 == 1/2 g_smoothing
int g_vertical_shift = SHRT_MIN;
char *g_estrCommandLine = NULL;
Range **g_ea_include_range = NULL;

static void dfgParseArgs(int argc, char* argv[])
{
	for (int i=0; i<argc;) {
		int iparams[4];
		char *sparams[ARRAY_SIZE(iparams)];
		float fparams[ARRAY_SIZE(iparams)];
		bool outOfParams=false;
		int nexti = 10000;
		int numParams=0;
		for (int j=1; j<ARRAY_SIZE(iparams)+1; j++) {
			if (outOfParams || i+j >= argc || argv[i+j][0]=='/') {
				iparams[j-1] = 1;
				fparams[j-1] = 1;
				sparams[j-1] = "";
				outOfParams = true;
				if (i+j<nexti)
					nexti = i+j; // First -something
			} else {
				numParams++;
				if( !sscanf(argv[i+j], "0x%x", &iparams[j-1]) )
					iparams[j-1] = atoi(argv[i+j]);
				fparams[j-1] = atof(argv[i+j]);
				sparams[j-1] = argv[i+j];
			}
		}
#define PARAM(s) (stricmp(argv[i]+1, s+1)==0)
		if (PARAM("-spread")) {
			g_dest_spread = iparams[0];
		} else if (PARAM("-img")) {
			g_image_name = strdup(sparams[0]);
		} else if (PARAM("-outfile")) {
			g_output_name = strdup(sparams[0]);
		} else if (PARAM("-imgdir")) {
			g_image_dir = strdup(sparams[0]);
		} else if (PARAM("-genfont")) {
			if (numParams ==2)
			{
				char buf[1024];
				sprintf(buf, "%s %s", sparams[0], sparams[1]);
				g_font_name = strdup(buf);
			} else
				g_font_name = strdup(sparams[0]);
		} else if (PARAM("-fontsize")) {
			g_font_size = iparams[0];
		} else if (PARAM("-outSize")) {
			g_outSize[0] = iparams[0];
			g_outSize[1] = iparams[1];
		}
		else if (PARAM("-atlasSize")) {
			g_font_atlas_size[0] = iparams[0];
			g_font_atlas_size[1] = iparams[1];
		}
		else if (PARAM("-keepTempFiles")) {
			g_keep_temp_files = true;
		}
		else if (PARAM("-bold")) {
			g_bold = true;
		}
		else if (PARAM("-excludeGlyph")) {
			char* tmp = strdup(sparams[0]);
			char* context = 0;
			char* token = strtok_s(tmp, " ", &context);
			g_exclude_char = (wchar_t*)malloc(sizeof(wchar_t)* 1024);
			g_exclude_char[0] = 0;
			wchar_t* curChar = g_exclude_char;
			while(token) {
				int num = 0;
				sscanf(token, "%i", &num);
				*curChar = num;
				curChar++;
				*curChar = 0;
				token = strtok_s(NULL, " ", &context);
			}
			
			free(tmp);
		}
		else if (PARAM("-includeGlyph")) {
			char* tmp = strdup(sparams[0]);
			char* context = 0;
			char* token = strtok_s(tmp, " ", &context);
			g_include_char = (wchar_t*)malloc(sizeof(wchar_t)* 1024);
			g_include_char[0] = 0;
			wchar_t* curChar = g_include_char;
			while(token) {
				int num = 0;
				sscanf(token, "%i", &num);
				*curChar = num;
				curChar++;
				*curChar = 0;
				token = strtok_s(NULL, " ", &context);
			}

			free(tmp);
		}
		else if (PARAM("-maxGlyph")) {
			g_max_glyph = iparams[0];
		}
		else if (PARAM("-minGlyph")) {
			g_min_glyph = iparams[0];
		}
		else if (PARAM("-includeRange")) {
			Range *r = (Range*)malloc(sizeof(Range));
			r->minGlyph = iparams[0];
			r->maxGlyph = iparams[1];
			eaPush(&g_ea_include_range, r);
		}
		else if (PARAM("-boldVersion")) {
			g_font_subsitutions[0] = sparams[0];
		}
		else if (PARAM("-italicVersion")) {
			g_font_subsitutions[1] = sparams[0];
		}
		else if (PARAM("-boldItalicVersion")) {
			g_font_subsitutions[2] = sparams[0];
		}
		else if (PARAM("-autoSub")) {
			int newStrLen = strlen(g_output_name)+5;
			g_font_subsitutions[0] = (char*)malloc(newStrLen);
			g_font_subsitutions[0][0] = 0;
			strcpy_s(g_font_subsitutions[0], newStrLen, g_output_name);
			strcat_s(g_font_subsitutions[0], newStrLen, "BD");

			g_font_subsitutions[1] = (char*)malloc(newStrLen);
			g_font_subsitutions[1][0] = 0;
			strcpy_s(g_font_subsitutions[1], newStrLen, g_output_name);
			strcat_s(g_font_subsitutions[1], newStrLen, "I");

			g_font_subsitutions[2] = (char*)malloc(newStrLen);
			g_font_subsitutions[2][0] = 0;
			strcpy_s(g_font_subsitutions[2], newStrLen, g_output_name);
			strcat_s(g_font_subsitutions[2], newStrLen, "BI");
		}
		else if (PARAM("-smoothing")) {
			g_smoothing = fparams[0];
		}
		else if (PARAM("-outlineSmoothing")) {
			g_outline_smoothing = fparams[0];
		}
		else if (PARAM("-densityOffset")) {
			g_density_offset = fparams[0];
		}
		else if (PARAM("-padding")) {
			g_override_padding[0] = iparams[0];
			g_override_padding[1] = iparams[1];
		}
		else if (PARAM("-ignoreBold")) {
			g_ignore_bold = true;
		}
		else if (PARAM("-ignoreItalic")) {
			g_ignore_italic = true;
		}
		else if (PARAM("-spacing")) {
			g_spacing = fparams[0];
		}
		else if (PARAM("-verticalShift")) {
			g_vertical_shift = iparams[0];
		}

		i = nexti;
	}
}

void generateDistanceFieldBrute(Image *image, Image *outimage, ImageFormat format)
{
	float xfact = image->size[0] / (float)outimage->size[0] / 2;
	float yfact = image->size[1] / (float)outimage->size[1] / 2;
	int srcmult = (format==IF_RGB)?3:(format==IF_ALPHA?1:4);
	int spread = (int)(image->size[0] * g_dest_spread / outimage->size[0]);
	for (int outy=0; outy<outimage->size[1]; outy++) {
		int srcy = (int)(yfact + outy * yfact*2);
		for (int outx=0; outx<outimage->size[0]; outx++) {
			int srcx = (int)(xfact + outx * xfact*2);
			bool bIsIn = image->data[(srcx + srcy*image->size[0]) * srcmult] < 127;
			int mindistsq=spread*spread*2;
			for (int dx=-spread; dx<=spread; dx++) {
				int xx = srcx + dx;
				if (xx<0 || xx>=image->size[0])
					continue;
				for (int dy=-spread; dy<=spread; dy++) {
					int yy = srcy + dy;
					if (yy<0 || yy>=image->size[1])
						continue;
					int dist = dx*dx + dy*dy;
					if (dist >= mindistsq)
						continue;
					if (bIsIn != image->data[(xx + yy*image->size[0]) * srcmult] < 127) {
						mindistsq = dist;
					}
				}
			}
			float dist = sqrt((float)mindistsq);
			float value = dist / spread; // 1/spread (0) - 1.0
			if (value < 0)
				value = 0;
			if (value > 1.0)
				value = 1.0;
			if (bIsIn)
				value = 0.5f + value*0.5f;
			else
				value = 0.5f - value*0.5f;
			U8 bytevalue = (int)(value * 255.0f + 0.5f);
			outimage->data[outx + outy*outimage->size[0]] = bytevalue;
		}
	}
}


typedef struct Node {
	struct Node *next;
	int nearest[2];
	int pos[2];
	int distsq;
	bool bIsIn;
	bool bInList;
} Node;

Node *data;
Node *nodelist_head; // Nodes to be evaluated
Node *nodelist_tail;

static const int dxlist[] = {-1, 1, 0, 0};
static const int dylist[] = {0, 0, -1, 1};

void addToList(Node *node)
{
	assert(!nodelist_head == !nodelist_tail);
	if (!nodelist_head) {
		nodelist_tail = nodelist_head = node;
	} else {
		nodelist_tail->next = node;
		nodelist_tail = node;
	}
	node->bInList = true;
}

void addNeighborsToList(Node *node, Image *image)
{
	for (int i=0; i<ARRAY_SIZE(dxlist); i++) {
		int x = node->pos[0] + dxlist[i];
		int y = node->pos[1] + dylist[i];
		if (x<0 || y<0 || x>=image->size[0] || y>=image->size[1])
			continue;
		Node *node2 = &data[x + y * image->size[0]];
		if (node2->bInList)
			continue;
		if (node2->bIsIn != node->bIsIn)
			continue;
		if (node2->nearest[0]==node->nearest[0] && node2->nearest[1]==node->nearest[1])
			continue;
		addToList(node2);
	}
}

static void saveDebugImage(Image *image, int spread)
{
	// Save intermediate image
	Image temp;
	temp.size[0] = image->size[0];
	temp.size[1] = image->size[1];
	temp.data = (unsigned char*)malloc(image->size[0] * image->size[1]);
	for (int y=0; y<image->size[1]; y++) {
		for (int x=0; x<image->size[0]; x++) {
			Node *node = &data[x + y*image->size[0]];
			float dist = sqrt((float)node->distsq);;
			float value = dist / spread; // 1/spread (0) - 1.0
			//			if (value < 0)
			//				value = 0;
			//			if (value > 1.0)
			//				value = 1.0;
			//			if (node->bIsIn)
			//				value = 0.5f + value*0.5f;
			//			else
			//				value = 0.5f - value*0.5f;
			U8 bytevalue = (int)(value * 255.0f + 0.5f);
			temp.data[x + y*image->size[0]] = bytevalue;
		}
	}

	temp.format = IF_ALPHA;
	saveImage("debug_image.tga", &temp);

	free(temp.data);
}

void generateDistanceField(Image *image, Image *outimage, int format)
{
	int srcmult = (format==IF_RGB)?3:(format==IF_ALPHA?1:4);
	int spread = (int)(image->size[0] * g_dest_spread / outimage->size[0]);
	data = (Node*)malloc(sizeof(Node) * image->size[0] * image->size[1]);
	// Setup all nodes
	unsigned char *imagedata = image->data;
	Node *walk = data;
	for (int y=0; y<image->size[1]; y++) {
		for (int x=0; x<image->size[0]; x++) {
			walk->next = NULL;
			walk->nearest[0] = -1;
			setVec2(walk->pos, x, y);
			walk->distsq = spread * spread;
			walk->bIsIn = *imagedata < 127;
			walk->bInList = false;
			walk++;
			imagedata+=srcmult;
		}
	}

	// Setup initial edge list and neighbors into list
	walk = data;
	for (int j=0; j<image->size[1]*image->size[0]; j++) {
			for (int i=0; i<ARRAY_SIZE(dxlist); i++) {
				int curx = walk->pos[0] + dxlist[i];
				int cury = walk->pos[1] + dylist[i];
				if (curx<0 || cury<0 || curx>=image->size[0] || cury>=image->size[1])
					continue;
				Node *node2 = &data[curx + cury * image->size[0]];
				if (walk->bIsIn != node2->bIsIn) {
					copyVec2(node2->pos, walk->nearest);
					walk->distsq = 1;
					addNeighborsToList(walk, image);
					break;
				}
			}
			walk++;
	}

	// Spread distances
	while (nodelist_head) {
		Node *node = nodelist_head;
		nodelist_head = nodelist_head->next;
		if (!nodelist_head)
			nodelist_tail = NULL;
		node->next = NULL;
		node->bInList = false;
		bool gotBetter=false;
		// See if the distance from any neighbor's closest is better than my distance
		for (int i=0; i<ARRAY_SIZE(dxlist); i++) {
			int x = node->pos[0] + dxlist[i];
			int y = node->pos[1] + dylist[i];
			if (x<0 || y<0 || x>=image->size[0] || y>=image->size[1])
				continue;
			Node *node2 = &data[x + y * image->size[0]];
			if (node2->nearest[0]==-1)
				continue;
			if (node2->bIsIn != node->bIsIn)
				continue;
			int dx = node->pos[0] - node2->nearest[0];
			int dy = node->pos[1] - node2->nearest[1];
			int dist = dx*dx + dy*dy;
			if (dist < node->distsq) {
				gotBetter = true;
				node->distsq = dist;
				copyVec2(node2->nearest, node->nearest);
			}
		}
		if (gotBetter) {
			// Add all neighbors, which are not in the list, to the list
			addNeighborsToList(node, image);
		}
	}

	if (0) {
		saveDebugImage(image, spread);
	}

	// Make final image
	float xfact = image->size[0] / (float)outimage->size[0] / 2;
	float yfact = image->size[1] / (float)outimage->size[1] / 2;
	for (int outy=0; outy<outimage->size[1]; outy++) {
		int srcy = (int)(yfact + outy * yfact*2);
		for (int outx=0; outx<outimage->size[0]; outx++) {
			int srcx = (int)(xfact + outx * xfact*2);
			Node *node = &data[srcx + srcy*image->size[0]];
			float dist = sqrt((float)node->distsq);;
			float value = (dist - 0.5f) / spread; // 1/spread (0) - 1.0
			if (value < 0)
				value = 0;
			if (value > 1.0)
				value = 1.0;
			if (node->bIsIn)
				value = 0.5f + value*0.5f;
			else
				value = 0.5f - value*0.5f;
			U8 bytevalue = (int)(value * 255.0f + 0.5f);
			outimage->data[outx + outy*outimage->size[0]] = bytevalue;
		}
	}
	free(data);
}

void genDistanceField(const char *image_name, const char *output_name, Image& outimage)
{
	if( ::GetFileAttributes( output_name ) != INVALID_FILE_ATTRIBUTES )
	{
		// outfile already exists, so don't do the processing. This lets us resume failed builds.
		return;
	}

	double aspect;
	ImageFormat format = ImageFormat_COUNT;
	assertmsg(image_name, "Missing -img argument");
	Image *image = getImage(image_name, &aspect, &format);
	assertmsg(image, "Error loading image");
	
	generateDistanceField(image, &outimage, format); // 128: 8176ms down to 2704ms  512: 12965 ms.
	//generateDistanceFieldBrute(image, &outimage, format); // 128: 25198 ms  512: 492710 ms
	outimage.format = IF_ALPHA;
	saveImage(output_name, &outimage);

	free(image->data);
	free(image);
}

void genDistanceField(const char *image_name, const char *output_name)
{
	Image outimage;
	outimage.size[0] = g_outSize[0];
	outimage.size[1] = g_outSize[1];
	outimage.data = (unsigned char *)malloc(g_outSize[0] * g_outSize[1]);
	genDistanceField(image_name, output_name, outimage);
	free(outimage.data);
}

int genDistanceFields(const char* folder_name)
{
	WIN32_FIND_DATA findData = {0};
	char searchPattern[MAX_PATH];
	sprintf(searchPattern, "%s\\*.tga", folder_name);
	int items = 0;

	Image outimage;
	outimage.size[0] = g_outSize[0];
	outimage.size[1] = g_outSize[1];
	outimage.data = (unsigned char *)malloc(g_outSize[0] * g_outSize[1]);

	char filename[MAX_PATH];
	char outname[MAX_PATH];

	HANDLE h = FindFirstFile(searchPattern, &findData);
	if (h != INVALID_HANDLE_VALUE) do
	{
		if (strStartsWith(findData.cFileName, "output") || strStartsWith(findData.cFileName, "final"))
			continue;

		sprintf(filename, "%s/%s", folder_name, findData.cFileName);
		sprintf(outname, "%s/output_%s", folder_name, findData.cFileName);
		genDistanceField(filename, outname, outimage);
		items++;
		if( items % 100 == 0) printf("genDistanceFields %d\n", items);

	} while (FindNextFile(h, &findData));

	if (h != INVALID_HANDLE_VALUE) FindClose(h);

	free(outimage.data);
	
	return items;
}

void clearTempFiles(const char* folder_name, const char* out_font_name)
{
	const char* patterns[] = {"*.tga", "*.font"};
	
	for (int i = 0; i < ARRAY_SIZE(patterns); i++)
	{
		WIN32_FIND_DATA findData = {0};
		char searchPattern[MAX_PATH];
		sprintf(searchPattern, "%s\\%s", folder_name, patterns[i]);

		HANDLE h = FindFirstFile(searchPattern, &findData);
		if (h != INVALID_HANDLE_VALUE) do
		{
			if (strStartsWith(findData.cFileName, out_font_name))
				continue;

			char filename[MAX_PATH];
			sprintf(filename, "%s/%s", folder_name, findData.cFileName);
			DeleteFile(filename);
			
		} while (FindNextFile(h, &findData));

		if (h != INVALID_HANDLE_VALUE) FindClose(h);
	}

}

//this way we can avoid passing forward a million parameters
void applyFinalParams(GfxFontData* new_font_info, const char* settingsFile, const char* settingsFileFinalPath)
{
	FILE* f = (FILE*)fopen(settingsFile, "w");
	
	new_font_info->substitutionBoldFile = g_font_subsitutions[0] ? estrCreateFromStr(g_font_subsitutions[0]) : 0;
	new_font_info->substitutionItalicFile = g_font_subsitutions[1] ? estrCreateFromStr(g_font_subsitutions[1]) : 0;
	new_font_info->substitutionBoldItalicFile = g_font_subsitutions[2] ? estrCreateFromStr(g_font_subsitutions[2]) : 0;
	
	//write them to the settings file instead

	fprintf(f, "# These settings will override the ones in the .font file\n\n");

	fprintf(f, "FontSize %i\n", new_font_info->fontSize);
	fprintf(f, "Spread %i\n", new_font_info->spread);
	
	int tmp1 = g_override_padding[0] != -1 ? g_override_padding[0] :  new_font_info->spread/4;
	int tmp2 = g_override_padding[1] != -1 ? g_override_padding[1] :  new_font_info->spread/4;
	fprintf(f, "Padding %i %i\n", tmp1, tmp2);
	new_font_info->padding[0] = 0;
	new_font_info->padding[1] = 0;

	fprintf(f, "MaxAscent %i\n", new_font_info->maxAscent);
	fprintf(f, "MaxDescent %i\n", new_font_info->maxDescent);

	fprintf(f, "VerticalShift %i\n", g_vertical_shift);
	new_font_info->verticalShift = SHRT_MIN; //set to default so it wont get written out

	fprintf(f, "IgnoreBoldStyle %i\n", g_ignore_bold ? 1 : 0);
	new_font_info->ignoreBold = 0;

	fprintf(f, "IgnoreItalicStyle %i\n", g_ignore_italic ? 1 : 0);
	new_font_info->ignoreItalic = 0;

	fprintf(f, "DensityOffset %f\n", g_density_offset);
	new_font_info->densityOffset = 0;

	fprintf(f, "Smoothing %f\n", g_smoothing);
	new_font_info->smoothingAmt = 1.0f; 

	fprintf(f, "OutlineSmoothing %f\n", g_outline_smoothing);
	new_font_info->outlineSmoothingAmt = -1.0f; 

	fprintf(f, "SpacingAdjustment %f\n", g_spacing);
	new_font_info->spacingAdjustment = 1.0f; 
	
	fclose(f);

	new_font_info->includeFileHack = estrCreateFromStr(settingsFileFinalPath);
}

int main(int argc, char *argv[])
{
	call_clang_auto_runs();

	if (argc > 1)
	{
		int i;

		dfgParseArgs(argc-1, argv+1); //remove the exe name


		// Gobble up and save the command line to be output in the .font file.
		for(i=1; i<argc; i++)
		{
			if(argv[i][0]=='/' || argv[i][0]=='-')
			{
				estrConcatf(&g_estrCommandLine, " %s", argv[i]);
			}
			else
			{
				estrConcatf(&g_estrCommandLine, " \"%s\"", argv[i]);
			}
		}
	}


	if (g_image_dir && g_image_name)
	{
		printf("You must either convert a font or make a distance field, not both!\n");
		//show usage
		g_image_dir = 0;
		g_image_name = 0;
		g_output_name = 0;
	}
	
	if (g_image_dir) {
		
		if (g_font_name) {
			loadstart_printf("Converting font %s into glyph images...", g_font_name);
			genFontChars(g_font_name, g_image_dir, g_font_size, g_dest_spread, g_bold, g_exclude_char, g_include_char, g_max_glyph, g_min_glyph, g_ea_include_range);
			loadend_printf("done");
		}

		loadstart_printf("Computing distance fields for glyph images...");
		genFontCheckIntermediate(g_image_dir, g_dest_spread, g_outSize[0], g_outSize[1]);
		int items  = genDistanceFields(g_image_dir);
		loadend_printf("done (%d glyphs)", items);

		char out_font_name[MAX_PATH];
		sprintf(out_font_name, "%s_%d", g_font_name, g_font_size);

		if (!g_output_name) g_output_name = out_font_name;

		loadstart_printf("Combining final texture...");
		genFontFinal(g_image_dir, g_output_name, g_font_atlas_size);
		loadend_printf("done");

		if(!g_keep_temp_files)
		{
			clearTempFiles(g_image_dir, g_output_name);
		}
	}

	if (g_image_name) {
		if (!g_output_name) g_output_name = "output.tga";

		loadstart_printf("Computing distance field for %s...", g_image_name);
		genDistanceField(g_image_name, g_output_name);
		loadend_printf("done (%s written)", g_output_name);
	}

	if (!g_font_name && !g_image_dir && !g_image_name) {
		printf("Usage: %s <options>\n", argv[0]);
		printf("\t Distance Fields:\n");
		printf("\t -img <distance field source file>\n");
		printf("\t -outfile <final output file>\n");
		printf("\t -outSize <width> <height>\n");
		printf("\n");
		printf("\t Font Conversion:\n");
		printf("\t -spread <spead value>\n");
		printf("\t -imgdir <path>\n");
		printf("\t -genfont <font name>\n");
		printf("\t -fontsize <font size>\n");
		printf("\t -outSize <per-glyph width> <per-glyph height>\n");
		printf("\t -atlasSize <width> <height>\n");
		printf("\t -keepTempFiles\n");
		printf("\t -bold\n");
		printf("\t -excludeGlyph \"0x0000 0x00FF etc\"\n");
		printf("\t -includeGlyph \"0x0000 0x00FF etc\"\n");
		printf("\t -maxGlyph <hex glyph number>");
		printf("\t -smoothing <smoothing factor> (1.0 is default)\n");
		printf("\t -outlineSmoothing <smoothing factor for outlined fonts> (1.0 is default)\n");
		printf("\t -densityOffset <density offset> (0.0 is default, adjust if the font is too thin or thick)\n");
		printf("\t -padding <w> <h>\n");
		printf("\t -spacing <scale> (1.0 is default, adjust if the letters are too squished together or too far apart)\n");
		printf("\t -verticalShift <pixels> (This defaults to half the height of the descender and allows you to tweak the center point)\n");
		
		printf("Font Subsitutions:\n");
		printf("\t -boldVersion <alternate font> (NOTE: already converted, not ttf)\n");
		printf("\t -italicVersion <alternate font> (NOTE: already converted, not ttf)\n");
		printf("\t -boldItalicVersion <alternate font> (NOTE: already converted, not ttf)\n");
		printf("\t -ignoreBold (Ignores the bold flag if there is no substitution available)\n");
		printf("\t -ignoreItalic (Ignores the italic flag if there is no substitution available)\n");
		printf("\t * OR *\n");
		printf("\t -autoSub (assumes the substitutions are <fontname>BD, <fontname>I, and <fontname>BI)\n");
		printf("\n");
	}
	return 0;
}
