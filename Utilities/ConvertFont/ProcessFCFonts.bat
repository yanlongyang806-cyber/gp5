mkdir FCFonts

..\bin\ConvertFont.exe /imgdir "FCFonts_Temp\Qulaar" /genfont "Qulaar" /outsize 42 42 /atlasSize 512 512 /spread 8 /outfile Qulaar /smoothing 0.7
..\bin\ConvertFont.exe /imgdir "FCFonts_Temp\Oklahoma" /genfont "Oklahoma" /outsize 42 42 /atlasSize 512 512 /spread 8 /outfile Oklahoma /excludeGlyph "0x5c 0x5e"

copy /Y FCFonts_Temp\Qulaar\*.* FCFonts\
copy /Y FCFonts_Temp\Oklahoma\*.* FCFonts\

..\bin\ConvertFont.exe /imgdir FCFonts_Temp\CCDaveGibbons /genfont "CCDaveGibbonsLower" /outsize 39 39 /atlasSize 512 512 /spread 8 /excludeGlyph "0xF8FF" /outfile CCDaveGibbons /autoSub /smoothing 0.9 /densityOffset 0.0125 /padding 1 2
REM  Note: You must not have BI or I installed to convert BD (its a bug in the font)
REM ..\bin\ConvertFont.exe /imgdir FCFonts_Temp\CCDaveGibbonsBD /genfont "CCDaveGibbonsLower" /outsize 39 39 /atlasSize 512 512 /spread 8 /excludeGlyph "0xF8FF" /outfile CCDaveGibbonsBD /bold /smoothing 0.9 /densityOffset 0.0125 /padding 1 2
..\bin\ConvertFont.exe /imgdir FCFonts_Temp\CCDaveGibbonsBI /genfont "CCDaveGibbonsLower Bold Italic" /outsize 39 39 /atlasSize 512 512 /spread 8 /excludeGlyph "0xF8FF" /outfile CCDaveGibbonsBI /smoothing 0.9 /densityOffset 0.0125 /padding 1 2
..\bin\ConvertFont.exe /imgdir FCFonts_Temp\CCDaveGibbonsI /genfont "CCDaveGibbonsLower Italic" /outsize 39 39 /atlasSize 512 512 /spread 8 /excludeGlyph "0xF8FF" /outfile CCDaveGibbonsI /smoothing 0.9 /densityOffset 0.0125 /padding 1 2

copy /Y FCFonts_Temp\CCDaveGibbons\*.* FCFonts\
REM copy /Y FCFonts_Temp\CCDaveGibbonsBD\*.* FCFonts\
copy /Y FCFonts_Temp\CCDaveGibbonsBI\*.* FCFonts\
copy /Y FCFonts_Temp\CCDaveGibbonsI\*.* FCFonts\
copy /Y FCFonts_Temp\CCDaveGibbons\CCDaveGibbons.font FCFonts\Game.font

 ..\bin\ConvertFont.exe /imgdir FCFonts_Temp\BlambotFXPro-700 /genfont "Blambot FXPro Light BB" /outsize 42 42 /atlasSize 512 512 /spread 8 /outfile BlambotFXPro-700 /autoSub /densityOffset 0.0125 /padding 1 2
 ..\bin\ConvertFont.exe /imgdir FCFonts_Temp\BlambotFXPro-700BD /genfont "Blambot FXPro Heavy BB" /outsize 80 80 /atlasSize 1024 512 /spread 8 /outfile BlambotFXPro-700BD /densityOffset 0.0125 /padding 1 2
 ..\bin\ConvertFont.exe /imgdir FCFonts_Temp\BlambotFXPro-700I /genfont "Blambot FXPro Light BB Italic" /outsize 42 42 /atlasSize 512 512 /spread 8 /outfile BlambotFXPro-700I /densityOffset 0.0125 /padding 1 2
 ..\bin\ConvertFont.exe /imgdir FCFonts_Temp\BlambotFXPro-700BI /genfont "Blambot FXPro Heavy BB Italic" /outsize 80 80 /atlasSize 1024 512 /spread 8 /outfile BlambotFXPro-700BI /densityOffset 0.0125 /padding 1 2

copy /Y FCFonts_Temp\BlambotFXPro-700\*.* FCFonts\
copy /Y FCFonts_Temp\BlambotFXPro-700BD\*.* FCFonts\
copy /Y FCFonts_Temp\BlambotFXPro-700I\*.* FCFonts\
copy /Y FCFonts_Temp\BlambotFXPro-700BI\*.* FCFonts\

rmdir /s /q FCFonts_Temp

