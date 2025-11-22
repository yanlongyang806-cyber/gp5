#include "scrubutils.h"
#include "timing.h"
#include "earray.h"
#include "estring.h"
#include "file.h"
#include "utils.h"
#include "error.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "cmdparse.h"
#include "gimmeDLLWrapper.h"

void fileError(char *msg,char *src,char *dst)
{
	char	buf[1000];

	if (msg)
		printf("failed to %s\n",msg);
	if (src)
		printf("  src: %s\n",src);
	if (dst)
		printf("  dst %s\n",dst);
	printf("hit return to continue\n");
	gets(buf);
}

int stopLooking(char *fname)
{
	char	*name = getFileName(fname);

	if (name[0] == '_')
		return 1;
	if (stricmp(name,"MasterBackup")==0)
		return 1;
	return 0;
}

int isUseless(char *fname)
{
	char	*name = getFileName(fname);

	if (stricmp(name,"Naming_Conventions.txt")==0)
		return 1;
	if (stricmp(name,"blank.txt")==0)
		return 1;
	if (stricmp(name,"dummy.txt")==0)
		return 1;
	if (stricmp(name,"placeholder.txt")==0)
		return 1;
	if (strEndsWith(name,".bak"))
		return 1;
	return 0;
}

int isBinData(char *fname)
{
	char	*name = getFileName(fname);

	if (strEndsWith(name,".obj"))
		return 1;
	if (strEndsWith(name,".fev"))
		return 1;
	if (strEndsWith(name,".sct"))
		return 1;
	if (strEndsWith(name,".fsb"))
		return 1;
	if (strEndsWith(name,".hogg"))
		return 1;
	if (strEndsWith(name,".bin"))
		return 1;
	if (strEndsWith(name,".bgeo"))
		return 1;
	if (strEndsWith(name,".lgeo"))
		return 1;
	if (strEndsWith(name,".modelnames"))
		return 1;
	if (strEndsWith(name,".modelheader"))
		return 1;
	if (strEndsWith(name,".materialdeps"))
		return 1;
	if (strEndsWith(name,".bcn"))
		return 1;
	if (strEndsWith(name,".bcn.date"))
		return 1;

	if (strEndsWith(name,".atrk"))
		return 1;
	if (strEndsWith(name,".skel"))
		return 1;
	if (strEndsWith(name,".gz"))
		return 1;
	if (strEndsWith(name,".bin_header"))
		return 1;
	if (strEndsWith(name,".avi"))
		return 1;
	if (strEndsWith(name,".ilk"))
		return 1;
	if (strEndsWith(name,".tif"))
		return 1;
	if (strEndsWith(name,".ttp"))
		return 1;
	if (strEndsWith(name,".rar"))
		return 1;
	if (strEndsWith(name,".ztl"))
		return 1;
	if (strEndsWith(name,".tmp"))
		return 1;

#if 0
	if (strEndsWith(name,".contact"))
		return 1;
	if (strEndsWith(name,".encounter"))
		return 1;
	if (strEndsWith(name,".encounterlayer"))
		return 1;
	if (strEndsWith(name,".ms"))
		return 1;
	if (strEndsWith(name,".MaterialDeps"))
		return 1;
	if (strEndsWith(name,".modelheader"))
		return 1;
	if (strEndsWith(name,".layer.notes"))
		return 1;
	if (strEndsWith(name,".wrl"))
		return 1;
	if (strEndsWith(name,".ai"))
		return 1;
#endif

	if (strEndsWith(name,".psd"))
		return 1;
	if (strEndsWith(name,".jpg"))
		return 1;
	if (strEndsWith(name,".jpeg"))
		return 1;
	if (strEndsWith(name,".tmap"))
		return 1;
	if (strEndsWith(name,".tga"))
		return 1;
	if (strEndsWith(name,".xls"))
		return 1;
	if (strEndsWith(name,".ttf"))
		return 1;
	if (strEndsWith(name,".vsd"))
		return 1;
	if (strEndsWith(name,".bcn"))
		return 1;
	if (strEndsWith(name,"thumbs.db"))
		return 1;
	if (strEndsWith(name,".wtex"))
		return 1;
	if (strEndsWith(name,".shs"))
		return 1;
	if (strEndsWith(name,".doc"))
		return 1;
	if (!strchr(name,'.') && strlen(name)==8)
		return 1;
	if (strEndsWith(name,".ppt"))
		return 1;
	if (strEndsWith(name,".wav"))
		return 1;
	if (strEndsWith(name,".mpp"))
		return 1;
	if (strEndsWith(name,".exe"))
		return 1;
	if (strEndsWith(name,".dll"))
		return 1;
	if (strEndsWith(name,".bmp"))
		return 1;
	if (strEndsWith(name,".dmp"))
		return 1;
	if (strEndsWith(name,".mdmp"))
		return 1;
	if (strEndsWith(name,".zip"))
		return 1;
	if (strEndsWith(name,".reallyazip"))
		return 1;
	if (strEndsWith(name,".pdb"))
		return 1;
	if (strEndsWith(name,".profiler"))
		return 1;
#if 0
	if (strEndsWith(name,".max"))
		return 1;
#endif
	if (strEndsWith(name,".mat"))
		return 1;
	if (strEndsWith(name,".tiff"))
		return 1;
	if (strEndsWith(name,".tom"))
		return 1;
	if (strEndsWith(name,".clr"))
		return 1;
	if (strEndsWith(name,".alm"))
		return 1;
	if (strEndsWith(name,".hmp"))
		return 1;
	if (strEndsWith(name,".soil"))
		return 1;
	if (strEndsWith(name,".tlayer"))
		return 1;
	if (strEndsWith(name,".cache"))
		return 1;
	if (strEndsWith(name,".png"))
		return 1;
	if (strEndsWith(name,".gif"))
		return 1;
	if (strEndsWith(name,".ttc"))
		return 1;
	if (strEndsWith(name,".spt"))
		return 1;
	if (strEndsWith(name,".font"))
		return 1;
	if (strEndsWith(name,".log"))
		return 1;
	if (strEndsWith(name,".danim"))		// NOT SURE
		return 1;
	if (strEndsWith(name,".crt.manifest"))
		return 1;
	if (strEndsWith(name,".schema"))
		return 1;
	if (strstri(fname,"fightclub/docs/"))
		return 1;
	return 0;
}

int checkoutFile(char *fileName)
{
	int		ret;

	if (g_dryrun)
		chmod(fileName,00777);
	else
	{
		if (fileIsReadOnly(fileName))
		{
			ret = gimmeDLLDoOperation(fileName, GIMME_CHECKOUT, GIMME_QUIET);

			if (ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB)
			{
				const char *lockee;
				if (ret == GIMME_ERROR_ALREADY_CHECKEDOUT && (lockee = gimmeDLLQueryIsFileLocked(fileName))) {
					printf("File \"%s\" unable to be checked out, currently checked out by %s", fileName, lockee);
				} else {
					printf("File \"%s\" unable to be checked out (%s)", fileName, gimmeDLLGetErrorString(ret));
				}
				fileError("checkout",0,0);
				return false;
			}

			printf("Checked out and locked file %s\n", fileName);
		}
	}
	return true;
}
