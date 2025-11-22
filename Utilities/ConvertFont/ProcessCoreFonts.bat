mkdir CoreFonts

..\bin\ConvertFont.exe /imgdir Temp\VeraMono /genfont "Bitstream Vera Sans Mono" /outsize 42 42 /atlasSize 1024 512 /spread 8 /outfile VeraMono /boldVersion VeraMonoBD /densityOffset 0.0125
..\bin\ConvertFont.exe /imgdir Temp\VeraMonoBD /genfont "Bitstream Vera Sans Mono Bold" /outsize 46 46 /atlasSize 1024 512 /spread 8 /outfile VeraMonoBD /densityOffset 0.0125

copy /Y Temp\VeraMono\*.* CoreFonts\
copy /Y Temp\VeraMonoBD\*.* CoreFonts\

REM These are not autosubbed to try and reduce memory usage, This way the alternate versions are only used if they are explicity requested, eg texwords.

..\bin\ConvertFont.exe /imgdir Temp\FreeSans /genfont "FreeSans" /outsize 42 42 /atlasSize 1024 512 /spread 8 /outfile FreeSans /maxGlyph 0x233 /spacing 1.15 /smoothing 1.25 /densityOffset 0.01
..\bin\ConvertFont.exe /imgdir Temp\FreeSansBD /genfont "Free Sans Bold" /outsize 40 40 /atlasSize 1024 512 /spread 8 /outfile FreeSansBD /maxGlyph 0x233 /spacing 1.15  /smoothing 1.25
..\bin\ConvertFont.exe /imgdir Temp\FreeSansI /genfont "Free Sans Oblique" /outsize 40 40 /atlasSize 1024 512 /spread 8 /outfile FreeSansI /maxGlyph 0x233 /spacing 1.15  /smoothing 1.25 /densityOffset 0.01
..\bin\ConvertFont.exe /imgdir Temp\FreeSansBI /genfont "Free Sans Bold Oblique" /outsize 40 40 /atlasSize 1024 512 /spread 8 /outfile FreeSansBI /maxGlyph 0x233 /spacing 1.15  /smoothing 1.25

copy /Y Temp\FreeSans\FreeSans.font CoreFonts\Game.font
copy /Y Temp\FreeSans\*.* CoreFonts\
copy /Y Temp\FreeSansBD\*.* CoreFonts\
copy /Y Temp\FreeSansI\*.* CoreFonts\
copy /Y Temp\FreeSansBI\*.* CoreFonts\

..\bin\ConvertFont.exe /imgdir Temp\FreeSerif /genfont "Free Serif" /outsize 40 40 /atlasSize 1024 512 /spread 8 /outfile FreeSerif /maxGlyph 0x233  /smoothing 1.25 /spacing 1.15 /densityOffset 0.01
..\bin\ConvertFont.exe /imgdir Temp\FreeSerifBD /genfont "Free Serif Bold" /outsize 40 40 /atlasSize 1024 512 /spread 8 /outfile FreeSerifBD /maxGlyph 0x233 /smoothing 1.25 /spacing 1.15
..\bin\ConvertFont.exe /imgdir Temp\FreeSerifI /genfont "Free Serif Italic" /outsize 40 40 /atlasSize 1024 512 /spread 8 /outfile FreeSerifI /maxGlyph 0x233 /smoothing 1.25 /spacing 1.15 /densityOffset 0.01
..\bin\ConvertFont.exe /imgdir Temp\FreeSerifBI /genfont "Free Serif Bold Italic" /outsize 40 40 /atlasSize 1024 512 /spread 8 /outfile FreeSerifBI /maxGlyph 0x233 /smoothing 1.25 /spacing 1.15

copy /Y Temp\FreeSerif\*.* CoreFonts\
copy /Y Temp\FreeSerifBD\*.* CoreFonts\
copy /Y Temp\FreeSerifI\*.* CoreFonts\
copy /Y Temp\FreeSerifBI\*.* CoreFonts\

rmdir /s /q Temp
