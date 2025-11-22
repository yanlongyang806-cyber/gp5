#include "file.h"
#include "utils.h"
#include <ctype.h>

bool isValidString(unsigned short *str, int len)
{
	int i;
	for (i=0; i<len; i++) {
		if (str[i] > 255 || !isprint(str[i]))
			return false;
	}
	return true;

}

int main(int argc, char *argv[])
{
	unsigned char *data;
	int len;
	U32 key = 0xFFFF0001;
	int idx=0;
	int cnt=0;
	int dlgnum=0;
	char newname[MAX_PATH];
	bool inblock=false;
	unsigned short *datatest;
	FILE *fout;
	if (argc!=2) {
		printf("Usage: res2c file.res\nMakes file.c\n");
		return 1;
	}
	data = fileAlloc(argv[1], &len);
	if (!data) {
		printf("Failed to open %s for reading\n", argv[1]);
		return 2;
	}
	changeFileExt(argv[1], ".c", newname);
	fout = fopen(newname, "w");
	if (!fout) {
		printf("Failed to open %s for writing\n", newname);
		return 2;
	}
	while (idx < len) {
		if (key == *(U32*)&data[idx]) {
			int i;
			char desc[128]="";
			// Beginning of new block!
			if (inblock) {
				fprintf(fout, "};\n");
			}
			for (i=0; i<48; i++) {
				datatest = (unsigned short*)&data[idx+i];
				if (isValidString(datatest, 6)) {
					for (i=0; i<127; i++) {
						desc[i] = datatest[i];
						if (!desc[i])
							break;
					}
					break;
				}
			}

			fprintf(fout, "unsigned char DialogResource%d[] = {%s%s\n", dlgnum++, desc[0]?" // ":"", desc);
			cnt=0;
			inblock=true;
		}
		if (inblock) {
			if (cnt==0) {
				fprintf(fout, "\t");
			}
			fprintf(fout, "0x%02X, ", (U32)data[idx]);
			cnt++;
			if (cnt==16) {
				fprintf(fout, "\n");
				cnt=0;
			}
		}
		idx++;
	}
	if (inblock) {
		fprintf(fout, "};\n");
	}
	fclose(fout);

	return 0;
}