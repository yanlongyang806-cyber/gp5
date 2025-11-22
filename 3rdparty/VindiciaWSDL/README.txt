JDRAGO - 2009/05/20:

These batch files generate new .h/.c files that stub out the SOAP conversations to Vindicia. To use:

(replace XX with the version you want to generate)

1. Run makevin_XX_part1.bat
2. (Optional) Prune anything out of \X.X\vindiciaStructsTemp.h that you don't want
3. Run makevin_XX_part2.bat
4. Replace the vindicia*.* files checked into the AccountServer with the matching X.X\vindicia*.* files
5. Compile, triage, enjoy.

