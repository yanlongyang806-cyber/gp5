#pragma once

typedef struct Range
{
	wchar_t minGlyph;
	wchar_t maxGlyph;
} Range;

int genFontChars(const char *font_name, const char* font_dir, int font_size, int dest_spread, bool bold, wchar_t* skipChars, wchar_t* addChars, wchar_t maxChar, wchar_t minChar, Range **eaIncludeRanges);
void genFontCheckIntermediate(const char *font_dir, int dest_spread, int outW, int outH);
void genFontFinal(const char *font_dir, const char* out_file, int image_size[2]);

