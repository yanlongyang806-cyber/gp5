#pragma once

C_DECLARATIONS_BEGIN

typedef struct BasicTexture BasicTexture;
typedef struct StashTableImp *StashTable;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("EndGlyph");
typedef struct GfxFontGlyphData
{
	U16 codePoint; AST( NAME(CodePoint) )
	U16	texPos[2]; AST( NAME(TexPos) )
	U16	size[2]; AST( NAME(Size) )
	U16 xAdvance; AST( NAME(XAdvance) )
} GfxFontGlyphData;

typedef struct GfxFontData GfxFontData;

AUTO_STRUCT AST_STARTTOK("Font") AST_ENDTOK("EndFont");
typedef struct GfxFontData
{
	const char* filename; AST(CURRENTFILE KEY)
	const char* commandLine; AST( ESTRING NAME("CommandLine")) // The command line used to generate this font
	const char* fontTextureFile; AST( ESTRING NAME("Texture") )
	const char* substitutionBoldFile; AST( ESTRING NAME("SubstitutionBold") )
	const char* substitutionItalicFile; AST( ESTRING NAME("SubstitutionItalic") )
	const char* substitutionBoldItalicFile; AST( ESTRING NAME("SubstitutionBoldItalic") )
	U16		fontSize; AST( NAME(FontSize))
	U16		spread;	  AST( NAME(Spread))
	U16		padding[2];	  AST( NAME(Padding) )
	U16		texSize[2];  AST( NAME(TexSize) )
	U16		maxAscent; AST( NAME(MaxAscent) )
	U16		maxDescent; AST( NAME(MaxDescent) )
	S16		verticalShift; AST( NAME(VerticalShift) DEFAULT(SHRT_MIN))
	U8		ignoreBold : 1; AST( NAME(IgnoreBoldStyle) DEFAULT(0))
	U8		ignoreItalic : 1; AST( NAME(IgnoreItalicStyle) DEFAULT(0))
	float	densityOffset; AST( NAME(DensityOffset) DEFAULT(0))
	float	smoothingAmt; AST( NAME(Smoothing) DEFAULT(1))
	float	outlineSmoothingAmt; AST( NAME(OutlineSmoothing) DEFAULT(-1)) //negative one = 1/2 of smoothingAmt (which was the old behavior)
	float	spacingAdjustment; AST( NAME(SpacingAdjustment) DEFAULT(1))
	//HACK: this is never read, its just used to write out an include file reference for the settings
	const char* includeFileHack; AST(  NAME("Include") ESTRING )
	GfxFontGlyphData**	eaGlyphData; AST( NAME(Glyph) )
	BasicTexture* pFontTexture; NO_AST
	StashTable	stFontGlyphs; NO_AST
	GfxFontData* substitutionBold; NO_AST
	GfxFontData* substitutionItalic; NO_AST
	GfxFontData* substitutionBoldItalic; NO_AST
} GfxFontData;

#include "GfxFontStructs_h_ast.h" 

void call_clang_auto_runs();

C_DECLARATIONS_END