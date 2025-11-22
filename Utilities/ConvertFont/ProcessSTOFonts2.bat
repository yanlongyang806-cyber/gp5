mkdir STOFonts


REM ..\bin\ConvertFont.exe /imgdir STOFonts_Temp\FuturaStd-Bold /genfont "FuturaStd-Bold" /outsize 40 40 /atlasSize 512 512 /spread 8 /outfile FuturaStd-Bold /densityOffset 0.0225 /smoothing 1.25
..\bin\ConvertFont.exe /imgdir STOFonts_Temp\FuturaStd-MediumOblique /genfont "FuturaStd-MediumOblique" /outsize 44 44 /atlasSize 512 512 /spread 8 /outfile FuturaStd-MediumOblique /densityOffset -0.0125 /smoothing 1.25 /padding 1 2

copy /Y STOFonts_Temp\FuturaStd-Bold\*.* STOFonts\
copy /Y STOFonts_Temp\FuturaStd-MediumOblique\*.* STOFonts\

:End
rmdir /s /q STOFonts_Temp

