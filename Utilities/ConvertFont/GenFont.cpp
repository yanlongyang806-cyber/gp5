#include "GenFont.h"
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <direct.h>
#include <math.h>
#include <assert.h>

C_DECLARATIONS_BEGIN
#include "earray.h"
#include "file.h"
#include "mathutil.h"
#include "GfxFontStructs.h"
C_DECLARATIONS_END

#include "ImageHandler.h"

extern char *g_estrCommandLine;

bool InAnyRange(wchar_t c, Range **eaRanges)
{
	FOR_EACH_IN_EARRAY(eaRanges, Range, pRange)
	{
		if( pRange->minGlyph <= c && pRange->maxGlyph >= c )
			return true;
	}
	FOR_EACH_END
	return false;
}

int genFontChars(const char *font_name, const char* font_dir, int font_size, int dest_spread, bool bold, wchar_t* skipChars, wchar_t* addChars, wchar_t maxChar, wchar_t minChar, Range **eaIncludeRanges)
{
	// Make the font, and output a separate image for each character
	// into a subdirectory, along with a FontInfo file with spacing info

	char output_name_path[MAX_PATH];
	sprintf(output_name_path, "%s/", font_dir);
	mkdirtree(output_name_path);

	HDC hdc = CreateCompatibleDC(NULL);

	int nBMISize = sizeof(BITMAPINFO) + 256 * sizeof(RGBQUAD);
	BITMAPINFO *pBitmapInfo = (BITMAPINFO *) calloc(nBMISize, 1);

	pBitmapInfo->bmiHeader.biSize = sizeof(pBitmapInfo->bmiHeader);
	pBitmapInfo->bmiHeader.biWidth = font_size*2;
	pBitmapInfo->bmiHeader.biHeight = font_size*2;
	pBitmapInfo->bmiHeader.biPlanes = 1;
	pBitmapInfo->bmiHeader.biBitCount = 8;
	pBitmapInfo->bmiHeader.biCompression = BI_RGB;
	pBitmapInfo->bmiHeader.biSizeImage = 
		(pBitmapInfo->bmiHeader.biWidth * 
		pBitmapInfo->bmiHeader.biHeight * 
		pBitmapInfo->bmiHeader.biBitCount) / 8;
	pBitmapInfo->bmiHeader.biXPelsPerMeter = 3200;
	pBitmapInfo->bmiHeader.biYPelsPerMeter = 3200;
	pBitmapInfo->bmiHeader.biClrUsed = 256;
	pBitmapInfo->bmiHeader.biClrImportant = 256;

	for(int nColor = 0; nColor < 256; ++nColor)
	{
		pBitmapInfo->bmiColors[nColor].rgbBlue = pBitmapInfo->bmiColors[nColor].rgbGreen = pBitmapInfo->bmiColors[nColor].rgbRed
			= nColor; // (BYTE)((nColor > 128) ? 255 : 0);
		pBitmapInfo->bmiColors[nColor].rgbReserved = 0;
	}

	BYTE *pBitmapBits=NULL;
	HBITMAP hBitmap = CreateDIBSection(hdc, pBitmapInfo, DIB_RGB_COLORS, (PVOID *) &pBitmapBits, NULL, 0);

	HFONT hFont = CreateFont(font_size, 0,
		0, 0,
		bold ? FW_BOLD : FW_NORMAL,
		FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		NONANTIALIASED_QUALITY, // NONANTIALIASED_QUALITY  ANTIALIASED_QUALITY
		VARIABLE_PITCH, // VARIABLE_PITCH, DEFAULT_PITCH | FF_SWISS,
		font_name);

	SelectObject(hdc, hFont);
	SelectObject(hdc, hBitmap);
	SelectObject(hdc, GetStockObject(WHITE_PEN)); 
	SelectObject(hdc, GetStockObject(WHITE_BRUSH)); 

	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, RGB(0, 0, 0));
	SetBkColor(hdc, RGB(255, 255, 255));

	int nameLen = GetTextFace(hdc, 0, NULL);
	char* actualfontName = (char*)malloc(nameLen);
	GetTextFace(hdc, nameLen, actualfontName);
	if (stricmp(font_name, actualfontName))
	{
		printf("Warning: Cannot find font \"%s\", got \"%s\" instead", font_name, actualfontName);
	}
	free(actualfontName);

	DWORD glypSetSize = GetFontUnicodeRanges(hdc, NULL);
	LPGLYPHSET gs = (LPGLYPHSET)calloc(1, glypSetSize);
	gs->cbThis = glypSetSize;
	GetFontUnicodeRanges(hdc, gs);

	UINT metricSize = GetOutlineTextMetrics(hdc, 0, NULL);
	POUTLINETEXTMETRIC textMetric = (POUTLINETEXTMETRIC)calloc(1, metricSize);
	textMetric->otmSize = metricSize;
	GetOutlineTextMetrics(hdc, metricSize, textMetric);

	GfxFontData *font_info = (GfxFontData*)StructCreate(parse_GfxFontData);
	font_info->maxAscent = (int)(textMetric->otmAscent * 96.0/72.0/2.0);
	font_info->maxDescent = (int)(-textMetric->otmDescent * 96.0/72.0/2.0);
	for (DWORD j=0; j<gs->cRanges; j++)
	{
		WCRANGE *range = &gs->ranges[j];
		for (USHORT k=0; k<range->cGlyphs; k++)
		{
			wchar_t i = range->wcLow + k;
			
			// If we didn't specifically ask for it, and it's beyond the max or it's specifically skipped,
			//   skip it.
			if( i==0
				||
				(
					!wcschr(addChars, i) // Specifically asked for
					&& !InAnyRange(i, eaIncludeRanges) // In a range specifically asked for
					&& !(i >= minChar && i <= maxChar) // In the min/max range
				)
				||
				wcschr(skipChars, i) // Specifically skipped
				) 
			{
				continue; //skip this one
			}

			SIZE size = {0};
			wchar_t wtext[2] = {i, '\0'};
			GetTextExtentPointW(hdc, wtext, 1, &size);
			GfxFontGlyphData *char_info = (GfxFontGlyphData*)StructCreate(parse_GfxFontGlyphData);
			char_info->codePoint = i;
			char_info->xAdvance = size.cx;
			char_info->size[1] = size.cy;
			char_info->texPos[0] = font_size/2;
			char_info->texPos[1] = font_size/2;
			eaPush(&font_info->eaGlyphData, char_info);

			// Draw the character, save to a file
			Rectangle(hdc, 0, 0, pBitmapInfo->bmiHeader.biWidth, pBitmapInfo->bmiHeader.biHeight);
			TextOutW(hdc, char_info->texPos[0], char_info->texPos[1], wtext, 1);
			char char_filename[MAX_PATH];
			sprintf(char_filename, "%s/%u.tga", font_dir, i);
			Image image;
			image.data = pBitmapBits;
			image.size[0] = pBitmapInfo->bmiHeader.biWidth;
			image.size[1] = pBitmapInfo->bmiHeader.biHeight;
			image.format = IF_ALPHA;
			saveImage(char_filename, &image);
		}
	}

	free(gs);
	free(textMetric);

	DeleteObject(hFont);
	DeleteDC(hdc);

	font_info->fontSize = font_size;
	font_info->texSize[0] = pBitmapInfo->bmiHeader.biWidth;
	font_info->texSize[1] = pBitmapInfo->bmiHeader.biHeight;
	font_info->spread = dest_spread;

	char buf[MAX_PATH];
	sprintf(buf, "%s/fontInfo.font", font_dir);
	ParserWriteTextFileEx(buf, parse_GfxFontData, font_info, (WriteTextFlags)0, 0, 0);
	StructDestroy(parse_GfxFontData, font_info);

	return 0;
}

void genFontCheckIntermediate(const char *font_dir, int dest_spread, int outW, int outH)
{
	GfxFontData *font_info = (GfxFontData*)StructCreate(parse_GfxFontData);
	char buf[MAX_PATH];
	sprintf(buf, "%s/fontInfo.font", font_dir);
	if (!fileExists(buf))
		return;
	ParserReadTextFile(buf, parse_GfxFontData, (void*)font_info, 0);

	float scaleW = outW / (float)font_info->texSize[0];
	float scaleH = outH / (float)font_info->texSize[1];
	for (int i=0; i<eaSize(&font_info->eaGlyphData); i++) {
		GfxFontGlyphData *char_info = font_info->eaGlyphData[i];
		char_info->xAdvance = (int)ceil((float)char_info->xAdvance * scaleW);
		char_info->size[1] = (int)ceil((float)char_info->size[1] * scaleH);
		char_info->texPos[0] = (int)ceil((float)char_info->texPos[0] * scaleW);
		char_info->texPos[1] = (int)ceil((float)char_info->texPos[1] * scaleH);
	}
	font_info->texSize[0] = outW;
	font_info->texSize[1] = outH;
	font_info->spread = dest_spread;
	font_info->maxAscent = (int)ceil((float)font_info->maxAscent * scaleH);
	font_info->maxDescent = (int)ceil((float)font_info->maxDescent * scaleH);
	sprintf(buf, "%s/outputFontInfo.font", font_dir);
	ParserWriteTextFileEx(buf, parse_GfxFontData, font_info, (WriteTextFlags)0, 0, 0);
	StructDestroy(parse_GfxFontData, font_info);
}

void applyFinalParams(GfxFontData* fonData, const char* settingsFile, const char* settingsFileFinalPath);

void genFontFinal(const char *font_dir, const char* out_file, int image_size[2])
{
	GfxFontData *font_info = (GfxFontData*)StructCreate(parse_GfxFontData);
	char buf[MAX_PATH];
	sprintf(buf, "%s/outputFontInfo.font", font_dir);
	ParserReadTextFile(buf, parse_GfxFontData, (void*)font_info, 0);

	// Generate composite image from all of the individual fonts
	GfxFontData *new_font_info = (GfxFontData*)StructCreate(parse_GfxFontData);
	setVec2(new_font_info->texSize, image_size[0], image_size[1]);
	new_font_info->spread = font_info->spread;
	new_font_info->maxAscent = font_info->maxAscent;
	new_font_info->maxDescent = font_info->maxDescent;

	// Space things in the new image
	int curX=0, curY=0, curImgIdx=0;
	int maxH=0, overalHeight = 0;
	for (int i=0; i<eaSize(&font_info->eaGlyphData); i++) {

		char char_name[MAX_PATH];
		sprintf(char_name, "%s/output_%u.tga", font_dir, font_info->eaGlyphData[i]->codePoint);
		if(!fileExists(char_name)) {
			GfxFontGlyphData* glyph = font_info->eaGlyphData[i];
			eaRemove((cEArrayHandle*)&font_info->eaGlyphData, i);
			StructDestroy(parse_GfxFontGlyphData, glyph);
			i--;
			continue;
		}

		GfxFontGlyphData *new_char_info = (GfxFontGlyphData*)StructCreate(parse_GfxFontGlyphData);
		GfxFontGlyphData *char_info = font_info->eaGlyphData[i];

		// determine the true font width by finding the first and last nonblack pixel
		double aspect=0;
		ImageFormat format = ImageFormat_COUNT;
		Image *char_image = getImage(char_name, &aspect, &format);
		int mult = (format == IF_RGBA)?4:(format == IF_ALPHA)?1:3;
		int xHigh = 0;
		int background = char_image->data[0]; // Background color isn't necessarily pure black
		// Find last nonblack pixel
		for (int x=char_image->size[0]-1; x > 0; --x) {
			for (int y=char_image->size[1]-1; y > 0; --y) {
				if (char_image->data[(x + y * char_image->size[0]) * mult] != background) {
					xHigh = x - font_info->spread;
					break;
				}
			}
			if (xHigh)
				break;
		}

		char_info->size[0] = MAX((xHigh-char_info->texPos[0]), char_info->xAdvance);
		free(char_image->data);
		free(char_image);
		int w = char_info->size[0] + font_info->spread*2;
		int h = char_info->size[1] + font_info->spread*2;
		if (curX + w > new_font_info->texSize[0]) {
			curX = 0;
			curY += maxH;
			maxH = 0;
		}
		if (curY + h > new_font_info->texSize[1]) {
			char errMsg[1024];
			curX = 0;
			curY = 0;
			maxH = 0;
			curImgIdx++;
			sprintf(errMsg, "Too many glyphs to fit in atlas!: %i of %i done.", i, (int)eaSize(&font_info->eaGlyphData));
			assertmsg(0, errMsg);
			break;
		}
		*new_char_info = *char_info;
		new_char_info->texPos[0] = curX + font_info->spread;
		new_char_info->texPos[1] = curY + font_info->spread;
		//new_char_info->imgIdx = curImgIdx;
		maxH = MAX(h, maxH);
		MAX1(overalHeight, new_char_info->size[1]);
		curX += w;
		eaPush(&new_font_info->eaGlyphData, new_char_info);
	}
	//curY += maxH;
	//new_font_info->imageH = curY;

	// Create the new image
	Image final_image = {0};
	copyVec2(new_font_info->texSize, final_image.size);
	final_image.data = (unsigned char *)calloc(final_image.size[0]*final_image.size[1], 1);
	curImgIdx=0;

	for (int i=0; i<eaSize(&new_font_info->eaGlyphData); i++) {
		GfxFontGlyphData *char_info = font_info->eaGlyphData[i];
		GfxFontGlyphData *new_char_info = new_font_info->eaGlyphData[i];
		char char_name[MAX_PATH];
		double aspect=0;
		ImageFormat format = ImageFormat_COUNT;
		/*if(new_char_info->imgIdx > curImgIdx)
		{
		sprintf(buf, "%s/final_%02d.tga", font_dir, curImgIdx);
		final_image.format = IF_ALPHA;
		saveImage(buf, &final_image);
		free(final_image.data);
		final_image.data = (unsigned char *)calloc(final_image.size[0]*final_image.size[1], 1);
		curImgIdx++;
		}*/
		sprintf(char_name, "%s/output_%u.tga", font_dir, char_info->codePoint);
		Image *char_image = getImage(char_name, &aspect, &format);
		assert(char_image);
		assertmsg(font_info->spread <= char_info->texPos[0], "Font did not have enough padding to fit the desired spread");
		assertmsg(char_info->texPos[1] == char_info->texPos[0], "Intermediate images are not square!");
		int effw = font_info->spread*2 + char_info->size[0];
		int effh = font_info->spread*2 + char_info->size[1];
		assertmsg(char_image->size[0] >= effw, "Intermediate images are too small!");
		assertmsg(char_image->size[1] >= effh, "Intermediate images are too small!");
		int outy = new_char_info->texPos[1] - font_info->spread;
		int srcy = char_info->texPos[1] - font_info->spread;
		int mult = (format == IF_RGBA)?4:(format == IF_ALPHA)?1:3;
		for (int y=0; y<effh; y++, outy++, srcy++) {
			int outx = new_char_info->texPos[0] - font_info->spread;
			int srcx = char_info->texPos[0] - font_info->spread;
			for (int x=0; x<effw; x++, outx++, srcx++) {
				final_image.data[outx + (final_image.size[1]-outy-1)*final_image.size[0]] = 
					char_image->data[(srcx + (char_image->size[1]-srcy-1)*char_image->size[0])*mult];
			}
		}
		free(char_image->data);
		free(char_image);
	}

	//make the font boxes slightly larger, we don't need more room on the texture since the gaps double up between characters so we can reuse them for each adjacent one
	for (int i=0; i<eaSize(&new_font_info->eaGlyphData); i++) {
		GfxFontGlyphData *new_char_info = new_font_info->eaGlyphData[i];
		new_char_info->xAdvance += new_font_info->spread/2;
		new_char_info->size[0] += new_font_info->spread/2;
		new_char_info->size[1] += new_font_info->spread/2;
		new_char_info->texPos[0] -= new_font_info->spread/4;
		new_char_info->texPos[1] -= new_font_info->spread/4;
	}

	new_font_info->commandLine = g_estrCommandLine;
	new_font_info->fontTextureFile = estrCreateFromStr(out_file);
	new_font_info->fontSize = overalHeight;
	
	char buf2[MAX_PATH];
	sprintf(buf2, "fonts/%s.fontsettings", out_file);
	sprintf(buf, "%s/%s.fontsettings", font_dir, out_file);
	applyFinalParams(new_font_info, buf, buf2);

	sprintf(buf, "%s/%s.font", font_dir, out_file);
	ParserWriteTextFileEx(buf, parse_GfxFontData, new_font_info, (WriteTextFlags)0, 0, 0);

	sprintf(buf, "%s/%s.tga", font_dir, out_file);
	final_image.format = IF_ALPHA;

	//flip the final image, our tga code flips things but since we keep saving and reloading the intermediate images its ok until now
	//note: this code assumes format == IF_ALPHA
	unsigned char* tmpLine = (unsigned char*)malloc(final_image.size[0]);
	for (int y = 0; y < final_image.size[1]/2; y++)
	{
		//swap the rows
		memcpy(tmpLine, final_image.data + y * final_image.size[0], final_image.size[0]);
		memcpy(final_image.data + y * final_image.size[0], final_image.data + (final_image.size[1]-y-1) * final_image.size[0], final_image.size[0]);
		memcpy(final_image.data + (final_image.size[1]-y-1) * final_image.size[0], tmpLine, final_image.size[0]);
	}
	free(tmpLine);
	saveImage(buf, &final_image);
	free(final_image.data);

	StructDestroy(parse_GfxFontData, new_font_info);
	StructDestroy(parse_GfxFontData, font_info);
}

