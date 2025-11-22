#include "wininclude.h"
#include "resource.h"
#include "file.h"
#include "FolderCache.h"
#include "utils.h"
#include "strings_opt.h"
#include "../../libs/GraphicsLib/TexOpts.h"
#include "textparser.h"
#include "assert.h"
#include "earray.h"
#include "gimmeDLLWrapper.h"
#include "SharedMemory.h"
#include <direct.h>
#include "winutil.h"
#include "version/appregcache.h"
#include "GlobalTypes.h"
#include "mathutil.h"
#include "UTF8.h"

TexOpt glob_texopt = {0};
TexOpt glob_texopt_orig = {0};
char glob_filename[MAX_PATH];
ParseTable *glob_tpi = parse_tex_opt;
ParseTable *glob_tpi_root = parse_TexOptList;
void *glob_struct = &glob_texopt;
void *glob_struct_orig = &glob_texopt_orig;
size_t glob_struct_size = sizeof(glob_texopt);
char glob_message[1024];
int g_ret;



void makeSimpleMaterialSub(HWND hDlg, const char *mat_template)
{
	char material_filename[MAX_PATH];
	Strcpy(material_filename, glob_filename);
	strstriReplace(material_filename, "/texture_library/", "/materials/");
	changeFileExt(material_filename, ".Material", material_filename);
	if (fileExists(material_filename)) {
		MessageBox(hDlg, L"A material file already exists for this texture.", L"Material already exists", MB_OK);
	} else {
		char material_name[MAX_PATH];
		char *s;
		FILE *f;
		Strcpy(material_name, getFileNameConst(material_filename));
		if (s=strrchr(material_name, '.'))
			*s = 0;
		mkdirtree(material_filename);
		f = fopen(material_filename, "w");
		fprintf(f, FORMAT_OK(mat_template), material_name, material_name, material_name, material_name);
		fclose(f);
		MessageBox(hDlg, L"Simple material created.", L"Success", MB_OK);
	}
}

void makeSimpleMaterial(HWND hDlg)
{
	static char *mat_template = "\
Material\n\
	Template SingleTexture\n\
	SoundProfile Default\n\
	OperationValue\n\
		LOD 0\n\
		OpName Texture1\n\
		SpecificValue Texture\n\
			SValue %s\n\
		EndSpecificValue\n\
	EndOperationValue\n\
EndMaterial\n\
";
	makeSimpleMaterialSub(hDlg, mat_template);
}

void makeSimpleMaterialWithBump(HWND hDlg)
{
	static char *mat_template = "\
Material\n\
	Template SimpleBump\n\
	SoundProfile Default\n\
	OperationValue\n\
		LOD 0\n\
		OpName Texture1\n\
		SpecificValue Texture\n\
			SValue %s.tga\n\
		EndSpecificValue\n\
	EndOperationValue\n\
	OperationValue\n\
		LOD 0\n\
		OpName TextureNormal1\n\
		SpecificValue Texture\n\
			SValue %s_bump.tga\n\
		EndSpecificValue\n\
	EndOperationValue\n\
EndMaterial\n";
	makeSimpleMaterialSub(hDlg, mat_template);
}


#define LINE_HEIGHT 16
#define EDIT_WIDTH 100
#define EDIT_WIDTH_COUNT(n) ((n==1)?100:(n==2)?100:45)
#define LIST_HEIGHT 500
#define EDIT_HEIGHT LINE_HEIGHT
#define STATIC_WIDTH 80
#define STATIC_HEIGHT LINE_HEIGHT
#define CHECK_WIDTH 150
#define CHECK_HEIGHT LINE_HEIGHT

static HFONT getFont(void)
{
	static HFONT ret=NULL;
	if (!ret) {
		ret = GetStockObject(ANSI_VAR_FONT);
	}
	return ret;
}

void CreateCheckBox(HWND hDlg, const char *text, int x, int y, int *id)
{
	HWND hwndTemp;
	hwndTemp = CreateWindow_UTF8( 
		"BUTTON",   // predefined class 
		text,       // button text 
		WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX ,  // styles 
		x, y, CHECK_WIDTH, CHECK_HEIGHT,
		hDlg,       // parent window 
		(HMENU) *id,
		(HINSTANCE) GetWindowLong(hDlg, GWL_HINSTANCE), 
		NULL);      // pointer not needed 
	SendMessage(hwndTemp, WM_SETFONT, (WPARAM)getFont(), (LPARAM)TRUE);
	(*id)++;
}

void CreateLabel(HWND hDlg, const char *text, int x, int y, int *id)
{
	HWND hwndTemp;
	hwndTemp = CreateWindow_UTF8("STATIC",      // predefined class 
		NULL,        // no window title 
		WS_CHILD | WS_VISIBLE , 
		x, y, STATIC_WIDTH, STATIC_HEIGHT,
		hDlg,        // parent window 
		(HMENU) *id,   // edit control ID 
		(HINSTANCE) GetWindowLong(hDlg, GWL_HINSTANCE), 
		NULL);       // pointer not needed 
	SendMessage(hwndTemp, WM_SETTEXT, 0, (LPARAM) text);
	SendMessage(hwndTemp, WM_SETFONT, (WPARAM)getFont(), (LPARAM)TRUE);
	(*id)++;
}

void CreateEditBox(HWND hDlg, const char *initialtext, int x, int y, int w, int *id)
{
	HWND hwndTemp;
	hwndTemp = CreateWindow_UTF8("EDIT",      // predefined class 
		NULL,        // no window title 
		WS_CHILD | WS_VISIBLE | 
		ES_LEFT, 
		x, y, w, EDIT_HEIGHT,
		hDlg,        // parent window 
		(HMENU) *id,   // edit control ID 
		(HINSTANCE) GetWindowLong(hDlg, GWL_HINSTANCE), 
		NULL);       // pointer not needed 
	SendMessage(hwndTemp, WM_SETFONT, (WPARAM)getFont(), (LPARAM)TRUE);
	(*id)++;
}

void SelectInListBox(HWND hDlg, StaticDefineInt *defines, int curvalue, int id)
{
	const char *stringToSelect=NULL;

	for (; defines->key!=(char*)DM_END; defines++) {
		if (defines->key < (char*)10)
			continue;
		if (defines->value == curvalue)
			stringToSelect = defines->key;
	}

	if (stringToSelect) {
		SendMessage_SelectString_UTF8(GetDlgItem(hDlg, id), stringToSelect);
	}
}

void CreateListBox(HWND hDlg, StaticDefineInt *defines, int curvalue, int x, int y, int *id)
{
	HWND hwndTemp;
	char *stringToSelect=NULL;
	hwndTemp = CreateWindow_UTF8("COMBOBOX",      // predefined class 
		NULL,        // no window title 
		WS_CHILD | WS_VISIBLE | 
		CBS_DROPDOWNLIST, 
		x, y, EDIT_WIDTH, LIST_HEIGHT,
		hDlg,        // parent window 
		(HMENU) *id,   // edit control ID 
		(HINSTANCE) GetWindowLong(hDlg, GWL_HINSTANCE), 
		NULL);       // pointer not needed 
	SendMessage(hwndTemp, WM_SETFONT, (WPARAM)getFont(), (LPARAM)TRUE);

	for (; defines->key!=(char*)DM_END; defines++) {
		LRESULT lResult;
		if (defines->key < (char*)10)
			continue;
		lResult = SendMessage_AddString_UTF8(hwndTemp, defines->key);
		SendMessage(hwndTemp, CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)defines->value);
	}

	(*id)++;
}

static void getData(HWND hDlg)
{
	ParseTable *tpi;
	int id=2000;
	char *pBuf = NULL;
	estrStackCreate(&pBuf);


	for (tpi=glob_tpi; tpi->type || (tpi->name && tpi->name[0]); tpi++) {
		switch(TOK_GET_TYPE(tpi->type)) {
		xcase TOK_F32_X:
		{
			F32 *data = (F32*) ((char*)glob_struct + tpi->storeoffset);
			int i, count;
			id++; //CreateLabel(hDlg, tpi->name, x, y, &id);

			if (tpi->type == (TOK_FIXED_ARRAY | TOK_F32_X)) // VEC2
			{
				count = tpi->param;
			} else if (tpi->type == TOK_F32_X) { // F32
				count = 1;
			} else {
				assert(0);
			}
			for (i=0; i<count; i++)
			{
				GetDlgItemText_UTF8(hDlg, id, &pBuf);
				sscanf(pBuf, "%f", &data[i]);
				id++; //CreateEditBox(hDlg, buf, x, y, &id);

			}
		}
		xcase TOK_U8_X:
		{
			U8 *data = (U8*) ((char*)glob_struct + tpi->storeoffset);
			int i, count;
			id++; //CreateLabel(hDlg, tpi->name, x, y, &id);

			if (tpi->type == (TOK_FIXED_ARRAY | TOK_U8_X)) // RGBA, etc
			{
				count = tpi->param;
			} else if (tpi->type == TOK_U8_X) { // U8
				count = 1;
			} else {
				assert(0);
			}
			for (i=0; i<count; i++)
			{
				int t;
				GetDlgItemText_UTF8(hDlg, id, &pBuf);
				sscanf(pBuf, "%d", &t);
				data[i] = t;
				id++; //CreateEditBox(hDlg, buf, x, y, &id);
			}
		}
		xcase TOK_INT16_X:
		{
			S16 *data = (S16*) ((char*)glob_struct + tpi->storeoffset);
			int i, count;
			id++; //CreateLabel(hDlg, tpi->name, x, y, &id);

			if (tpi->type == (TOK_FIXED_ARRAY | TOK_INT16_X)) // RGBA, etc
			{
				count = tpi->param;
			} else if (tpi->type == TOK_INT16_X) { // U8
				count = 1;
			} else {
				assert(0);
			}
			for (i=0; i<count; i++)
			{
				int t;
				GetDlgItemText_UTF8(hDlg, id, &pBuf);
				sscanf(pBuf, "%d", &t);
				data[i] = t;
				id++; //CreateEditBox(hDlg, buf, x, y, &id);
			}
		}
	/*	xcase TOK_FLAGS_X:
		{
			U32 *data = (U32*)((char*)glob_struct + tpi->storeoffset);
			StaticDefineInt *flag_list = (StaticDefineInt *)tpi->subtable;
			assert(flag_list->key == (char*)DM_INT);
			flag_list++;
			id++; //CreateLabel(hDlg, tpi->name, x, y, &id);
			for (; flag_list->key!=(char*)DM_END; flag_list++) {
				if (flag_list->value == 0)
					continue;
				if (IsDlgButtonChecked(hDlg, id)==BST_CHECKED) {
					*data |= flag_list->value;
				} else {
					*data &=~ flag_list->value;
				}
				id++; //CreateCheckBox(hDlg, flag_list->key, x+10, y, &id);
			}
		}*/
		xcase TOK_INT_X:
		{
			if (TOK_GET_FORMAT_OPTIONS(tpi->format) == TOK_FORMAT_FLAGS)
			{
				U32 *data = (U32*)((char*)glob_struct + tpi->storeoffset);
				StaticDefineInt *flag_list = (StaticDefineInt *)tpi->subtable;
				assert(flag_list->key == (char*)DM_INT);
				flag_list++;
				id++; //CreateLabel(hDlg, tpi->name, x, y, &id);
				for (; flag_list->key!=(char*)DM_END; flag_list++) {
					if (flag_list->value == 0)
						continue;
					if (stricmp(flag_list->key, "TrueColor")==0)
						continue; // Skip this one, it's deprecated

					if (IsDlgButtonChecked(hDlg, id)==BST_CHECKED) {
						*data |= flag_list->value;
					} else {
						*data &=~ flag_list->value;
					}
					id++; //CreateCheckBox(hDlg, flag_list->key, x+10, y, &id);
				}
			}
			else
			{

				if (tpi->subtable) {
					LRESULT lResult;
					U32 *data = (U32*)((char*)glob_struct + tpi->storeoffset);
					// List box
					id++; //CreateLabel(hDlg, tpi->name, x, y, &id);
					lResult = SendMessage(GetDlgItem(hDlg, id), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
					if (lResult != CB_ERR) {
						lResult = SendMessage(GetDlgItem(hDlg, id), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
						if (lResult != CB_ERR) {
							*data = lResult;
						}
					}
					//CreateListBox(hDlg, tpi->subtable, *data, x, y, &id);
					id++;
				}
			}
		}
		}
	}

	estrDestroy(&pBuf);
}

static bool g_in_putdata=false;
static void putData(HWND hDlg)
{
	ParseTable *tpi;
	int id = 2000;
	char buf[1024];
	g_in_putdata = true;
	for (tpi=glob_tpi; tpi->type || (tpi->name && tpi->name[0]); tpi++) {
		switch(TOK_GET_TYPE(tpi->type)) {
		xcase TOK_F32_X:
		{
			F32 *data = (F32*) ((char*)glob_struct + tpi->storeoffset);
			int count;
			int i;
			id++; //CreateLabel(hDlg, tpi->name, x, y, &id);

			if (tpi->type == (TOK_FIXED_ARRAY | TOK_F32_X)) // VEC2
			{
				count = tpi->param;
			} else if (tpi->type == TOK_F32_X) { // F32
				count = 1;
			} else {
				assert(0);
			}
			for (i=0; i<count; i++)
			{
				sprintf_s(SAFESTR(buf), "%1.2f", data[i]);
				SetDlgItemText_UTF8(hDlg, id, buf);
				id++; // CreateEditBox(hDlg, buf, x, y, &id);
			}
		}
		xcase TOK_U8_X:
		{
			U8 *data = (U8*) ((char*)glob_struct + tpi->storeoffset);
			int count;
			int i;
			id++; //CreateLabel(hDlg, tpi->name, x, y, &id);

			if (tpi->type == (TOK_FIXED_ARRAY | TOK_U8_X)) // RGBA, etc
			{
				count = tpi->param;
			} else if (tpi->type == TOK_U8_X) { // U8
				count = 1;
			} else {
				assert(0);
			}
			for (i=0; i<count; i++)
			{
				sprintf_s(SAFESTR(buf), "%d", data[i]);
				SetDlgItemText_UTF8(hDlg, id, buf);
				id++; // CreateEditBox(hDlg, buf, x, y, &id);
			}
		}
       	xcase TOK_INT16_X:
		{
			S16 *data = (S16*) ((char*)glob_struct + tpi->storeoffset);
			int count;
			int i;
			id++; //CreateLabel(hDlg, tpi->name, x, y, &id);

			if (tpi->type == (TOK_FIXED_ARRAY | TOK_INT16_X)) // RGBA, etc
			{
				count = tpi->param;
			} else if (tpi->type == TOK_INT16_X) { // INT16
				count = 1;
			} else {
				assert(0);
			}
			for (i=0; i<count; i++)
			{
				sprintf_s(SAFESTR(buf), "%d", data[i]);
				SetDlgItemText_UTF8(hDlg, id, buf);
				id++; // CreateEditBox(hDlg, buf, x, y, &id);
			}
		}
/*		xcase TOK_FLAGS_X:
			{
				U32 *data = (U32*)((char*)glob_struct + tpi->storeoffset);
				StaticDefineInt *flag_list = (StaticDefineInt *)tpi->subtable;
				assert(flag_list->key == (char*)DM_INT);
				flag_list++;
				id++; // CreateLabel(hDlg, tpi->name, x, y, &id);
				for (; flag_list->key!=(char*)DM_END; flag_list++) {
					if (flag_list->value == 0)
						continue;

					CheckDlgButton(hDlg, id, (*data&flag_list->value)?BST_CHECKED:BST_UNCHECKED);
					id++; // CreateCheckBox(hDlg, flag_list->key, x+10, y, &id);
				}
			}*/
		xcase TOK_INT_X:
			{
				if (TOK_GET_FORMAT_OPTIONS(tpi->format) == TOK_FORMAT_FLAGS)
				{
					U32 *data = (U32*)((char*)glob_struct + tpi->storeoffset);
					StaticDefineInt *flag_list = (StaticDefineInt *)tpi->subtable;
					assert(flag_list->key == (char*)DM_INT);
					flag_list++;
					id++; // CreateLabel(hDlg, tpi->name, x, y, &id);
					for (; flag_list->key!=(char*)DM_END; flag_list++) {
						if (flag_list->value == 0)
							continue;
						if (stricmp(flag_list->key, "TrueColor")==0)
							continue; // Skip this one, it's deprecated

						CheckDlgButton(hDlg, id, (*data&flag_list->value)?BST_CHECKED:BST_UNCHECKED);
						id++; // CreateCheckBox(hDlg, flag_list->key, x+10, y, &id);
					}
				}
				else
				{


					if (tpi->subtable) {
						U32 *data = (U32*)((char*)glob_struct + tpi->storeoffset);
						// List box
						id++; // CreateLabel(hDlg, tpi->name, x, y, &id);

						SelectInListBox(hDlg, tpi->subtable, *data, id);
						id++; // CreateListBox(hDlg, tpi->subtable, *data, x, y, &id);
					}
				}
			}
		}
	}
	g_in_putdata = false;
}

int findFieldID(HWND hDlg, const char *name, int *count)
{
	ParseTable *tpi;
	int id = 2000;
	for (tpi=glob_tpi; tpi->type || (tpi->name && tpi->name[0]); tpi++) {
		switch(TOK_GET_TYPE(tpi->type)) {
			xcase TOK_F32_X:
			{
				int lcount;
				if (tpi->type == (TOK_FIXED_ARRAY | TOK_F32_X)) // VEC2
				{
					lcount = tpi->param + 1;
				} else if (tpi->type == TOK_F32_X) { // F32
					lcount = 2;
				} else {
					assert(0);
				}

				if (stricmp(tpi->name, name)==0) {
					*count = lcount;
					return id;
				}
				id+=lcount;
			}
            xcase TOK_INT16_X:
			{
				int lcount;
				if (tpi->type == (TOK_FIXED_ARRAY | TOK_INT16_X)) // VEC2
				{
					lcount = tpi->param + 1;
				} else if (tpi->type == TOK_INT16_X) { // INT16
					lcount = 2;
				} else {
					assert(0);
				}

				if (stricmp(tpi->name, name)==0) {
					*count = lcount;
					return id;
				}
				id+=lcount;
			}
			xcase TOK_U8_X:
			{
				int lcount;
				if (tpi->type == (TOK_FIXED_ARRAY | TOK_U8_X)) // VEC2
				{
					lcount = tpi->param + 1;
				} else if (tpi->type == TOK_U8_X) { // U8
					lcount = 2;
				} else {
					assert(0);
				}

				if (stricmp(tpi->name, name)==0) {
					*count = lcount;
					return id;
				}
				id+=lcount;
			}
/*			xcase TOK_FLAGS_X:
			{
				StaticDefineInt *flag_list = (StaticDefineInt *)tpi->subtable;
				assert(flag_list->key == (char*)DM_INT);
				flag_list++;
				id++; // CreateLabel(hDlg, tpi->name, x, y, &id);
				for (; flag_list->key!=(char*)DM_END; flag_list++) {
					if (flag_list->value == 0)
						continue;
					if (stricmp(flag_list->key, name)==0) {
						*count = 1;
						return id;
					}

					id++; // CreateCheckBox(hDlg, flag_list->key, x+10, y, &id);
				}
			}*/
			xcase TOK_INT_X:
			{
				if (TOK_GET_FORMAT_OPTIONS(tpi->format) == TOK_FORMAT_FLAGS)
				{
					StaticDefineInt *flag_list = (StaticDefineInt *)tpi->subtable;
					assert(flag_list->key == (char*)DM_INT);
					flag_list++;
					id++; // CreateLabel(hDlg, tpi->name, x, y, &id);
					for (; flag_list->key!=(char*)DM_END; flag_list++) {
						if (flag_list->value == 0)
							continue;
						if (stricmp(flag_list->key, "TrueColor")==0)
							continue; // Skip this one, it's deprecated

						if (stricmp(flag_list->key, name)==0) {
							*count = 1;
							return id;
						}

						id++; // CreateCheckBox(hDlg, flag_list->key, x+10, y, &id);
					}

				}
				else
				{
					if (tpi->subtable) {
						// List box
						if (stricmp(tpi->name, name)==0) {
							*count = 2;
							return id;
						}
						id++; // CreateLabel(hDlg, tpi->name, x, y, &id);
						id++; // CreateListBox(hDlg, tpi->subtable, *data, x, y, &id);
					}
				}
			}
		}
	}
	return -1;
}

void enableField(HWND hDlg, const char *name, bool enable)
{
	int id, count;
	int i;
	id = findFieldID(hDlg, name, &count);
	if (id==-1)
		return;
	for (i=0; i<count; i++) {
		EnableWindow(GetDlgItem(hDlg, id+i), enable);
	}
}

void verifyData(HWND hDlg)
{
	bool needDataUpdate=false;
	if (glob_texopt.flags & TEXOPT_JPEG) {
		if (!(glob_texopt.flags & TEXOPT_NOMIP)) {
			glob_texopt.flags |= TEXOPT_NOMIP;
			needDataUpdate = true;
		}
	}

	enableField(hDlg, "Crunch", false);

	if (glob_texopt.flags & (TEXOPT_JPEG|TEXOPT_ALPHABORDER))
		enableField(hDlg, "ColorBorder", false);
	else
		enableField(hDlg, "ColorBorder", true);

	if (glob_texopt.flags & TEXOPT_COLORBORDER)
		enableField(hDlg, "ColorBorderLegacy", true);
	else
		enableField(hDlg, "ColorBorderLegacy", false);

	if (glob_texopt.flags & (TEXOPT_NOMIP))
	{
		if (glob_texopt.mip_filter != MIP_KAISER) {
			glob_texopt.mip_filter = MIP_KAISER;
			needDataUpdate = true;
		}
		if (glob_texopt.flags & TEXOPT_REVERSED_MIPS) {
			glob_texopt.flags &= ~TEXOPT_REVERSED_MIPS;
			needDataUpdate = true;
		}
		if (glob_texopt.mip_sharpening != SHARPEN_NONE) {
			glob_texopt.mip_sharpening = SHARPEN_NONE;
			needDataUpdate = true;
		}
		// disable mipfilter
		enableField(hDlg, "MipFilter", false);
		enableField(hDlg, "MipSharpening", false);
		enableField(hDlg, "MinLeveLSplit", false);
		enableField(hDlg, "HighLevelSize", false);
	} else {
		// Has mipmapping
		if (glob_texopt.flags & (TEXOPT_CLAMPS|TEXOPT_CLAMPT))
		{
			// Clamping needs cubic (or box?)
			if (glob_texopt.mip_filter != MIP_CUBIC)
			{
				// set to cubic
				glob_texopt.mip_filter = MIP_CUBIC;
				needDataUpdate = true;
			}
			// disable mipfilter
			enableField(hDlg, "MipFilter", false);
		} else {
			// enable mipfilter
			enableField(hDlg, "MipFilter", true);
		}
		enableField(hDlg, "MipSharpening", true);
		enableField(hDlg, "MinLeveLSplit", true);
		enableField(hDlg, "HighLevelSize", true);
	}
	if (glob_texopt.flags & (TEXOPT_COLORBORDER|TEXOPT_NOMIP))
	{
		enableField(hDlg, "AlphaBorder", false);
		enableField(hDlg, "AlphaBorderLR", false);
		enableField(hDlg, "AlphaBorderTB", false);
	} else {
		enableField(hDlg, "AlphaBorder", true);
		enableField(hDlg, "AlphaBorderLR", true);
		enableField(hDlg, "AlphaBorderTB", true);
	}
	if (glob_texopt.flags & (TEXOPT_COLORBORDER))
	{
		static bool switchedOnce3=false;
		static bool lastWasLegacy=false;
		if (!switchedOnce3)
		{
			if (glob_texopt.border_color.integer_for_equality_only == 0)
			{
				setVec4(glob_texopt.border_color.rgba, 127, 127, 255, 255);
				needDataUpdate = true;
			}
			switchedOnce3 = true;
		}
		if (!(glob_texopt.flags & TEXOPT_COLORBORDER_LEGACY))
		{
			Color c565;
			c565.r = round(glob_texopt.border_color.r*31.f/255.f);
			c565.g = round(glob_texopt.border_color.g*63.f/255.f);
			c565.b = round(glob_texopt.border_color.b*31.f/255.f);
			glob_texopt.border_color.r = round(c565.r*255.f/31.f);
			glob_texopt.border_color.g = round(c565.g*255.f/63.f);
			glob_texopt.border_color.b = round(c565.b*255.f/31.f);
			if (lastWasLegacy) // Can't do this always, stops typing from working!
				needDataUpdate = true;
			lastWasLegacy = false;
		} else {
			lastWasLegacy = true;
		}
		enableField(hDlg, "BorderColor", true);
	} else
		enableField(hDlg, "BorderColor", false);

	switch (glob_texopt.compression_type) {
	case COMPRESSION_AUTO:
	case COMPRESSION_DXT1:
	case COMPRESSION_DXT5:
	case COMPRESSION_DXT5NM:
	case COMPRESSION_DXT_IF_LARGE:
		enableField(hDlg, "Quality", true);
	xdefault:
		enableField(hDlg, "Quality", false);
	}

	if (texoptShouldCrunch(&glob_texopt, glob_texopt.flags)) {
		if (!(glob_texopt.flags & TEXOPT_CRUNCH)) {
			glob_texopt.flags |= TEXOPT_CRUNCH;
			needDataUpdate = true;
		}
	} else {
		if (glob_texopt.flags & TEXOPT_CRUNCH) {
			glob_texopt.flags &= ~TEXOPT_CRUNCH;
			needDataUpdate = true;
		}
	}

	// crunched textures always have reversed mips so make sure the flag is set
	enableField(hDlg, "ReversedMips", (glob_texopt.flags & (TEXOPT_CRUNCH | TEXOPT_NOMIP)) == 0);

	if ((glob_texopt.flags & (TEXOPT_CRUNCH | TEXOPT_NOMIP | TEXOPT_REVERSED_MIPS)) == TEXOPT_CRUNCH) {
		glob_texopt.flags |= TEXOPT_REVERSED_MIPS;
		needDataUpdate = true;
	}

	if (glob_texopt.compression_type == COMPRESSION_TRUECOLOR)
	{
		enableField(hDlg, "Compression", true);
		enableField(hDlg, "FixAlphaMIPs", false);
		enableField(hDlg, "AlphaMIPThreshold", false);
	} else if (glob_texopt.flags & TEXOPT_FIX_ALPHA_MIPS)
	{
		static bool switchedOnce1=false;
		static bool switchedOnce2=false;
		if (glob_texopt.compression_type != COMPRESSION_DXT1 && !switchedOnce1) {
			glob_texopt.compression_type = COMPRESSION_DXT1;
			needDataUpdate = true;
			switchedOnce1 = true;
		}
		if (glob_texopt.alpha_mip_threshold == 0 && !switchedOnce2) {
			glob_texopt.alpha_mip_threshold = 0.95f;
			needDataUpdate = true;
			switchedOnce2 = true;
		}
		enableField(hDlg, "Compression", true);
		enableField(hDlg, "FixAlphaMIPs", true);
		enableField(hDlg, "AlphaMIPThreshold", true);
	} else {
		enableField(hDlg, "Compression", true);
		enableField(hDlg, "FixAlphaMIPs", true);
		enableField(hDlg, "AlphaMIPThreshold", false);
	}

	if (needDataUpdate) {
		putData(hDlg);
	}
}


// Returns 0 on success
static int saveData(HWND hDlg)
{
	// Checkout, etc
	if (gimmeDLLQueryIsFileLockedByMeOrNew(glob_filename)) {
		// Already checked out
	} else {
		// Not checked out
		int ret = gimmeDLLDoOperation(glob_filename, GIMME_CHECKOUT, 0);
		if (GIMME_ERROR_NOT_IN_DB == ret) {
			// New file, do nothing
		} else if (NOERROR == ret) {
			char buf[1024];
			sprintf_s(SAFESTR(buf), "Checked out %s", glob_filename);
			MessageBox_UTF8(hDlg, buf, "Gimme Checkout", MB_OK);
		} else {
			char buf[1024];
			sprintf_s(SAFESTR(buf), "FAILED to check out %s.  %s.", glob_filename, gimmeDLLGetErrorString(ret));
			MessageBox_UTF8(hDlg, buf, "Gimme Checkout FAILED", MB_OK);
			return 1;
		}
	}
	{
		// Save data
		void **eatemp=NULL;

		// Clear the flags set implicitly
		glob_texopt.flags &= ~(TEXOPT_COMPRESSION_MASK);
		// Clear the flags not written to disk
		glob_texopt.flags &= ~(TEXOPT_NORMALMAP | TEXOPT_BUMPMAP);

		eaPush(&eatemp, glob_struct);
		ParserWriteTextFile(glob_filename, glob_tpi_root, &eatemp, 0, 0);
		eaDestroy(&eatemp);

		ParserLoadFiles(NULL, glob_filename, NULL, 0, glob_tpi, glob_struct);
		// re-fill in "orig" structure (and also check for file errors)
		memset(glob_struct_orig, 0, glob_struct_size);
		ParserLoadFiles(NULL, glob_filename, NULL, 0, glob_tpi, glob_struct_orig);
		return 0; // success
	}
}

static void updateButtons(HWND hDlg)
{
	static bool enabled = true;
	if (StructCompare(glob_tpi, glob_struct, glob_struct_orig, 0, 0, 0)==0) {
		if (enabled) {
			EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
			EnableWindow(GetDlgItem(hDlg, IDSAVE), FALSE);
			SetDlgItemText(hDlg, IDCANCEL, L"Exit");
			enabled = false;
		}
	} else {
		if (!enabled) {
			EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
			EnableWindow(GetDlgItem(hDlg, IDSAVE), TRUE);
			SetDlgItemText(hDlg, IDCANCEL, L"Cancel");
			enabled = true;
		}
	}
}


static BOOL CALLBACK texOptEditorDlgProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	char buf[1024];
	switch (iMsg) {
	case WM_INITDIALOG:
		{
			ParseTable *tpi;
			int id = 2000;
			int x=10;
			int y=10;

			gimmeDLLDoOperation(glob_filename, GIMME_GLV, 0); // Get latest before editting!
			if (fileExists(glob_filename)) {
				ParserLoadFiles(NULL, glob_filename, NULL, 0, glob_tpi, glob_struct);
				memset(glob_struct_orig, 0, glob_struct_size);
				ParserLoadFiles(NULL, glob_filename, NULL, 0, glob_tpi, glob_struct_orig);
			}
			for (tpi=glob_tpi; tpi->type || (tpi->name && tpi->name[0]); tpi++) {
				x = 10;
				switch(TOK_GET_TYPE(tpi->type)) {
				xcase TOK_F32_X:
				{
					F32 *data = (F32*) ((char*)glob_struct + tpi->storeoffset);
					int i, count;
					CreateLabel(hDlg, tpi->name, x, y, &id);
					x+= STATIC_WIDTH+10;

					if (tpi->type == (TOK_FIXED_ARRAY | TOK_F32_X)) // VEC2, etc
					{
						count = tpi->param;
					} else if (tpi->type == TOK_F32_X) { // F32
						count = 1;
					} else {
						assert(0);
					}
					for (i=0; i<count; i++)
					{
						sprintf_s(SAFESTR(buf), "%1.2f", data[i]);
						CreateEditBox(hDlg, buf, x, y, EDIT_WIDTH_COUNT(count), &id);
						x+=EDIT_WIDTH_COUNT(count)+10;
					}

					y+=EDIT_HEIGHT+2;
				}
				xcase TOK_U8_X:
				{
					int i, count;
					U8 *data = (U8*) ((char*)glob_struct + tpi->storeoffset);
					CreateLabel(hDlg, tpi->name, x, y, &id);
					x+= STATIC_WIDTH+10;

					if (tpi->type == (TOK_FIXED_ARRAY | TOK_U8_X)) // RGBA, etc
					{
						count = tpi->param;
					} else if (tpi->type == TOK_U8_X) { // U8
						count = 1;
					} else {
						assert(0);
					}
					for (i=0; i<count; i++)
					{
						sprintf_s(SAFESTR(buf), "%d", data[i]);
						CreateEditBox(hDlg, buf, x, y, EDIT_WIDTH_COUNT(count), &id);
						x+=EDIT_WIDTH_COUNT(count)+10;
					}

					y+=EDIT_HEIGHT+2;
				}
				xcase TOK_INT16_X:
				{
					int i, count;
					S16 *data = (S16*) ((char*)glob_struct + tpi->storeoffset);
					CreateLabel(hDlg, tpi->name, x, y, &id);
					x+= STATIC_WIDTH+10;

					if (tpi->type == (TOK_FIXED_ARRAY | TOK_INT16_X)) // RGBA, etc
					{
						count = tpi->param;
					} else if (tpi->type == TOK_INT16_X) { // INT16
						count = 1;
					} else {
						assert(0);
					}
					for (i=0; i<count; i++)
					{
						sprintf_s(SAFESTR(buf), "%d", data[i]);
						CreateEditBox(hDlg, buf, x, y, EDIT_WIDTH_COUNT(count), &id);
						x+=EDIT_WIDTH_COUNT(count)+10;
					}

					y+=EDIT_HEIGHT+2;
				}
			/*	xcase TOK_FLAGS_X:
					{
						U32 *data = (U32*)((char*)glob_struct + tpi->storeoffset);
						StaticDefineInt *flag_list = (StaticDefineInt *)tpi->subtable;
						assert(flag_list->key == (char*)DM_INT);
						flag_list++;
						CreateLabel(hDlg, tpi->name, x, y, &id);
						y+=EDIT_HEIGHT;
						for (; flag_list->key!=(char*)DM_END; flag_list++) {
							if (flag_list->value == 0)
								continue;
							CreateCheckBox(hDlg, flag_list->key, x+10, y, &id);
							y+=CHECK_HEIGHT;
						}
					}
					break;*/
				xcase TOK_INT_X:
					{
						if (TOK_GET_FORMAT_OPTIONS(tpi->format) == TOK_FORMAT_FLAGS)
						{
							U32 *data = (U32*)((char*)glob_struct + tpi->storeoffset);
							StaticDefineInt *flag_list = (StaticDefineInt *)tpi->subtable;
							int col = 0;
							assert(flag_list->key == (char*)DM_INT);
							flag_list++;
							CreateLabel(hDlg, tpi->name, x, y, &id);
							y+=EDIT_HEIGHT;
							for (; flag_list->key!=(char*)DM_END; flag_list++) {
								if (flag_list->value == 0)
									continue;
								if (stricmp(flag_list->key, "TrueColor")==0)
									continue; // Skip this one, it's deprecated

								CreateCheckBox(hDlg, flag_list->key, x+col*CHECK_WIDTH+10, y, &id);
								if (col) 
                                    y+=CHECK_HEIGHT;
								col ^= 1;
							}
						}
						else
						{
							if (tpi->subtable) {
								U32 *data = (U32*)((char*)glob_struct + tpi->storeoffset);
								// List box
								CreateLabel(hDlg, tpi->name, x, y, &id);
								x+= STATIC_WIDTH+10;
								CreateListBox(hDlg, tpi->subtable, *data, x, y, &id);
								y+=EDIT_HEIGHT+10;
							}
						}
					}
				}
			}

			putData(hDlg);

			// General init
			SetDlgItemText_UTF8(hDlg, IDC_MESSAGE, glob_message);

			verifyData(hDlg);
			updateButtons(hDlg);
		}
		break;
	case WM_NOTIFY:
		break;
	case WM_COMMAND:
		if (g_in_putdata)
			break;
		switch (LOWORD (wParam))
		{
		case IDOK: //Save and exit
			// Syncronize Data and exit
			getData(hDlg);
			if (0==saveData(hDlg)) {
				g_ret = 1;
				EndDialog(hDlg, 0);
			} else {
				updateButtons(hDlg);
			}
			break;
		case IDSAVE:
			// Save, do not exit
			getData(hDlg);
			saveData(hDlg);
			updateButtons(hDlg);
			break;
		case IDCANCEL:
			// Ignore data and exit
			g_ret = 0;
			EndDialog(hDlg, 0);
			break;
		case IDC_MAKE_SIMPLE_MATERIAL:
			makeSimpleMaterial(hDlg);
			break;
		case IDC_MAKE_SIMPLE_MATERIAL_WITH_BUMP:
			makeSimpleMaterialWithBump(hDlg);
			break;
		}
		{
			int id, count;
			int idTB, idLR;
			id = findFieldID(hDlg, "AlphaBorder", &count);
			if (LOWORD(wParam)==id)
			{
				if (IsDlgButtonChecked(hDlg, id)==BST_CHECKED) {
					CheckDlgButton(hDlg, findFieldID(hDlg, "AlphaBorderTB", &count), BST_CHECKED);
					CheckDlgButton(hDlg, findFieldID(hDlg, "AlphaBorderLR", &count), BST_CHECKED);
				} else {
					CheckDlgButton(hDlg, findFieldID(hDlg, "AlphaBorderTB", &count), BST_UNCHECKED);
					CheckDlgButton(hDlg, findFieldID(hDlg, "AlphaBorderLR", &count), BST_UNCHECKED);
				}
			}
			idTB = findFieldID(hDlg, "AlphaBorderTB", &count);
			idLR = findFieldID(hDlg, "AlphaBorderLR", &count);
			if (LOWORD(wParam)==idTB || LOWORD(wParam)==idLR)
			{
				if (IsDlgButtonChecked(hDlg, idTB)==BST_CHECKED && IsDlgButtonChecked(hDlg, idLR)==BST_CHECKED)
				{
					CheckDlgButton(hDlg, findFieldID(hDlg, "AlphaBorder", &count), BST_CHECKED);
				} else {
					CheckDlgButton(hDlg, findFieldID(hDlg, "AlphaBorder", &count), BST_UNCHECKED);
				}
			}
		}
		getData(hDlg);
		verifyData(hDlg);
		updateButtons(hDlg);
		break;
	}
	return FALSE;
}



int CALLBACK wWinMain(IN HINSTANCE hInstance, IN HINSTANCE hPrevInstance, WCHAR*    pWideCmdLine, IN int nShowCmd )
{
	char path_to_file[MAX_PATH];
	char *lpCmdLine = UTF16_to_UTF8_CommandLine(pWideCmdLine);	
	if (strStartsWith(lpCmdLine, "\"") && strEndsWith(lpCmdLine, "\"")) {
		lpCmdLine++;
		lpCmdLine[strlen(lpCmdLine)-1]='\0';
	}
	SetAppGlobalType(GLOBALTYPE_CLIENT);
	DO_AUTO_RUNS
	Strcpy(path_to_file, lpCmdLine);

	FolderCacheSetMode(FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC);

	winRegisterMe("Open", ".wtex");
	winRegisterMe("TexOptEditor", ".wtex");
	winRegisterMe("TexOptEditor", ".texopt");
	winRegisterMe("TexOptEditor", ".tga");

	if (stricmp(lpCmdLine, "-register")==0) // silent
		return 0;

	regSetAppName("TexOptEditor");
// This message lies, we love the stand-alone editor too much.
// 	if (!regGetAppInt("DisplayedMoveWarning", 0)) {
// 		regPutAppInt("DisplayedMoveWarning", 1);
// 		MessageBox(NULL, "The TexOptEditor is now accessible through the in-game AssetBrowser in the editor.  New features will be added there first, and the stand-alone version (what you're running now) will probably be removed.", "Stand-alone TexOptEditor Deprecated", MB_OK);
// 	}

	if (!lpCmdLine[0]) {
		MessageBox(NULL, L"TexOptEditor right click hooks registered", L"TexOptEditor", MB_OK);
		return 0;
	}

	assertmsg(0==_chdir(getDirectoryName(path_to_file)), "Error calling _chdir");
		
	fileAutoDataDir();
	if (!fileExists(lpCmdLine)) {
		MessageBox(NULL, L"Error: must specify file to edit on command line or file not found", L"TexOptEditor", MB_OK);
		return 1;
	}
	Strcpy(glob_filename, lpCmdLine);
	forwardSlashes(glob_filename);
	strstriReplace(glob_filename, "/src/", "/data/");
	strstriReplace(glob_filename, "/CoreSrc/", "/CoreData/");
	if (strEndsWith(glob_filename, ".9.tga"))
		glob_filename[strlen(glob_filename)-4] = '\0'; // remove .9
	changeFileExt(glob_filename, ".wtex", glob_filename);
	if (!fileExists(glob_filename)) {
// This will happen when editing TexOpts in Core
// 		char buf[1024];
// 		sprintf_s(SAFESTR(buf), "Error: could not find output texture file: %s", glob_filename);
// 		MessageBox(NULL, buf, "TexOptEditor", MB_OK);
// 		return 2;
	}

	sharedMemorySetMode(SMM_DISABLED);
	texoptLoad();

	changeFileExt(glob_filename, ".texopt", glob_filename);

	if (fileExists(glob_filename)) {
		ParserLoadFiles(NULL, glob_filename, NULL, 0, parse_tex_opt, &glob_texopt);
	} else {
		// Set flags/values from any folder texopt
		TexOpt *tex_opt = texoptFromTextureName(glob_filename, &glob_texopt.flags);
		if (tex_opt) {
			glob_texopt = *tex_opt;
			glob_texopt.file_name = NULL;
			glob_texopt.texture_name = NULL;
			glob_texopt.is_folder = 0;
		} else {
			glob_texopt.mip_filter = MIP_KAISER;
			glob_texopt.quality = QUALITY_PRODUCTION;
			glob_texopt.compression_type = (glob_texopt.flags & TEXOPT_COMPRESSION_MASK) >> TEXOPT_COMPRESSION_SHIFT;
		}
	}

	{
		char shortFileName[MAX_PATH];
		char *s;
		s = strstri(glob_filename, "texture_library/");
		if (s) {
			Strcpy(shortFileName, s+strlen("texture_library/"));
		} else {
			Strcpy(shortFileName, glob_filename);
		}
		sprintf_s(SAFESTR(glob_message), "Editing: %s", shortFileName);
	}

	DialogBox (hInstance, MAKEINTRESOURCE (IDD_TEXOPTEDITOR), NULL, (DLGPROC)texOptEditorDlgProc);

	return 0;
}