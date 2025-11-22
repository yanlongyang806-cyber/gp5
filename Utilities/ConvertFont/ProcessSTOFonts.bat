mkdir STOFonts

..\bin\ConvertFont.exe /imgdir STOFonts_Temp\Corbel /genfont "Corbel" /outsize 42 42 /atlasSize 1024 512 /spread 8 /outfile Corbel /maxGlyph 0x233
copy /Y STOFonts_Temp\Corbel\*.* STOFonts\

REM this font isnt supposed to have bold or italic according to the original setup in the old system
..\bin\ConvertFont.exe /imgdir STOFonts_Temp\Slider /genfont "Slider Regular" /outsize 42 42 /atlasSize 512 512 /spread 8 /outfile Slider /ignoreBold /ignoreItalic  /padding 1 2

copy /Y STOFonts_Temp\Slider\*.* STOFonts\
copy /Y STOFonts_Temp\Slider\Slider.font STOFonts\Game.font

..\bin\ConvertFont.exe /imgdir STOFonts_Temp\FuturaStd-Heavy /genfont "FuturaStd-Heavy" /outsize 42 42 /atlasSize 512 512 /spread 8 /outfile FuturaStd-Heavy /densityOffset 0.0225 /smoothing 1.25
..\bin\ConvertFont.exe /imgdir STOFonts_Temp\FuturaStd-Medium /genfont "FuturaStd-Medium" /outsize 42 42 /atlasSize 512 512 /spread 8 /outfile FuturaStd-Medium /densityOffset -0.0125 /smoothing 1.25 /padding 1 2
..\bin\ConvertFont.exe /imgdir STOFonts_Temp\FuturaStd-Condensed /genfont "FuturaStd-Condensed" /outsize 42 42 /atlasSize 512 512 /spread 8 /outfile FuturaStd-Condensed /densityOffset -0.0125 /smoothing 1.15 /padding 1 2

copy /Y STOFonts_Temp\FuturaStd-Heavy\*.* STOFonts\
copy /Y STOFonts_Temp\FuturaStd-Medium\*.* STOFonts\
copy /Y STOFonts_Temp\FuturaStd-Condensed\*.* STOFonts\

..\bin\ConvertFont.exe /fontsize 64 /imgdir STOFonts_Temp\FEB /genfont "FederationBold" /outsize 64 64 /atlasSize 512 512 /spread 8 /outfile FEB_____ /excludeGlyph "0x2030 0xF000"

copy /Y STOFonts_Temp\FEB\*.* STOFonts\

:End
rmdir /s /q STOFonts_Temp

