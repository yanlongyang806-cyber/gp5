#ifndef _SCRUBUTILS_H
#define _SCRUBUTILS_H

#define SINGLE_FILE_CHECKOUT 0

typedef struct FInfo
{
	char	*fname;
	char	*new_fname;
	char	*name;
	char	*new_name;
	U32		size;
	struct FInfo	*f2;
	char	**refs;
	int		printed;
	char	**src_names;
	char	**dst_names;
} FInfo;

FInfo **file_names,**def_names,**ignores;
int g_dryrun;



int checkoutFile(char *fileName);
int isBinData(char *fname);
int isUseless(char *fname);
int stopLooking(char *fname);
void fileError(char *msg,char *src,char *dst);

#endif
