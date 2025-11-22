#include "file.h"
#include "crypt.h"
#include "blowfish.h"
#include "strings_opt.h"
#include "netio_core.h"
#include "utils.h"
#include "endian.h"

#define PAD(num) ((num +7) & ~7)


#ifdef _XBOX
#pragma comment(lib, "\\Program Files\\Microsoft Xbox 360 SDK\\lib\\xbox\\Xonline.lib")
#pragma comment(lib, "\\Program Files\\Microsoft Xbox 360 SDK\\lib\\xbox\\xboxkrnl.lib")
#pragma comment(lib, "\\Program Files\\Microsoft Xbox 360 SDK\\lib\\xbox\\xbdm.lib")
#endif

int fileAddSearchPath(char* path);

int main(int argc, char **argv)
{
	int i;

#ifdef _XBOX
	char	*argv2[1000],*s,delim[] = " \t",buf[1000] = "",line2[1000]="";
	int		noExceptionHandler=0;
	FILE	*file;

	file = fopen("./cmdline.txt","rt");
	if (file)
	{
		fgets(buf,sizeof(buf),file);
		s = buf + strlen(buf) - 1;
		if (*s == '\n')
			*s = 0;
		fgets(line2,sizeof(line2),file);
	}
	argc = 1 + tokenize_line_quoted_safe(buf,&argv2[1],ARRAY_SIZE(argv2)-1,0);
	argv = argv2;
#endif

	if (argc<2) {
		printf("makekey <command>\n");
		printf("   -pair > file.pair\n");
		printf("   -encrypt <source.pair> <dest.pair> <data file name.txt> <output file name.key>\n");
		printf("	     private key entry in dest.pair can be all 0s\n");
		printf("   -decrypt <source.pair> <dest.pair> <data file name.key> <output file name.txt>\n");
		printf("	     private key entry in source.pair can be all 0s\n");
		printf("   -test <data.key>\n");
		printf("Expected usage example:\n");
		printf("   Put \"CRYPTIC 9 ATI\" into c:\\temp.txt\n");
		printf("   From c:\\game\\tools\\shortcuts\\programmer\\makekey\\, run:\n");
		printf("   makekey -encrypt server.pair client.pair c:\\temp.txt c:\\devrel.key\n");
		printf("   makekey -test c:\\devrel.key\n");
		printf("   Then send devrel.key to ATI (or whoever).  The source data needs to\n");
		printf("   be in the form of \"CRYPTIC <accesslevel> <WhoIssuedTo>\n");
		return 0;
	}
	fileAddSearchPath("./");
	cryptInit();
	if (stricmp(argv[1], "-pair")==0) {
		U32 public_key[16];
		U32 private_key[16];
		do{
			cryptMakeKeyPair(private_key,public_key);
		}while(public_key[0]==0);
		for (i=0; i<ARRAY_SIZE(public_key); i++)
			printf("%08X", public_key[i]);
		printf(" ");
		for (i=0; i<ARRAY_SIZE(private_key); i++)
			printf("%08X", private_key[i]);
	} else if (stricmp(argv[1], "-encrypt")==0) {
		char *source = fileAlloc(argv[2], NULL);
		char *dest = fileAlloc(argv[3], NULL);
		char *outfilename = argv[5];
		U32 my_public_key[16]={0};
		U32 my_private_key[16]={0};
		U32 their_public_key[16]={0};
		U32 shared_secret[16]={0};
		U32	md5_buf[4]={0};
		BLOWFISH_CTX blowfish_ctx;
		int datalen;
		char *data = fileAlloc(argv[4], &datalen);
		char *paddeddata;
		datalen++; // Add null terminator (added via fileAlloc())
		paddeddata = calloc(PAD(datalen),1);
		memcpy(paddeddata, data, datalen);
		for (i=0; i<ARRAY_SIZE(my_public_key); i++)
		{
			sscanf(source+i*8, "%08X", &my_public_key[i]);
			my_public_key[i] = endianSwapIfBig(U32, my_public_key[i]);
		}
		for (i=0; i<ARRAY_SIZE(my_private_key); i++)
		{
			sscanf(source+i*8 +1+ARRAY_SIZE(my_public_key)*8, "%08X", &my_private_key[i]);
			my_private_key[i] = endianSwapIfBig(U32, my_private_key[i]);
		}
		for (i=0; i<ARRAY_SIZE(their_public_key); i++)
		{
			sscanf(dest+i*8, "%08X", &their_public_key[i]);
			their_public_key[i] = endianSwapIfBig(U32, their_public_key[i]);
		}

		cryptMakeSharedSecret(shared_secret,my_private_key,their_public_key);
		cryptMD5Update((U8*)shared_secret,512/8);
		cryptMD5Final(md5_buf);
		cryptBlowfishInitU32(&blowfish_ctx,md5_buf,ARRAY_SIZE(md5_buf));
		//printf(" U32 key[4] = {");
		//for (i=0; i<4; i++) {
		//	printf("0x%08X", md5_buf[i]);
		//	if (i!=3)
		//		printf(", ");
		//}
		//printf("};\n");

		cryptBlowfishEncrypt(&blowfish_ctx,paddeddata, PAD(datalen));

		{
			FILE *fout = fopen(outfilename, "wb");
			datalen = endianSwapIfBig(U32, datalen);
			fwrite(&datalen, sizeof(datalen), 1, fout);
			datalen = endianSwapIfBig(U32, datalen);
			fwrite(paddeddata, 1, PAD(datalen), fout);
			fclose(fout);
		}
		printf("Encrypted.\n");
	} else if (stricmp(argv[1], "-decrypt")==0) {
		char *source = fileAlloc(argv[2], NULL);
		char *dest = fileAlloc(argv[3], NULL);
		int datalen;
		char *paddeddata = fileAlloc(argv[4], &datalen);
		char *outfilename = argv[5];
		U32 my_public_key[16]={0};
		U32 my_private_key[16]={0};
		U32 their_public_key[16]={0};
		U32 shared_secret[16]={0};
		U32	md5_buf[4]={0};
		BLOWFISH_CTX blowfish_ctx;
		for (i=0; i<ARRAY_SIZE(their_public_key); i++)
			sscanf(source+i*8, "%08X", &their_public_key[i]);
		for (i=0; i<ARRAY_SIZE(my_public_key); i++)
			sscanf(dest+i*8, "%08X", &my_public_key[i]);
		for (i=0; i<ARRAY_SIZE(my_private_key); i++)
			sscanf(dest+i*8 +1+ARRAY_SIZE(my_public_key)*8, "%08X", &my_private_key[i]);

		cryptMakeSharedSecret(shared_secret,my_private_key,their_public_key);
		cryptMD5Update((U8*)shared_secret,512/8);
		cryptMD5Final(md5_buf);
		cryptBlowfishInitU32(&blowfish_ctx,md5_buf,ARRAY_SIZE(md5_buf));
		//printf(" U32 key[4] = {");
		//for (i=0; i<4; i++) {
		//	printf("0x%08X", md5_buf[i]);
		//	if (i!=3)
		//		printf(", ");
		//}
		//printf("};\n");

		datalen = *((U32*)paddeddata);
		cryptBlowfishDecrypt(&blowfish_ctx,paddeddata+4, PAD(datalen));

		{
			FILE *fout = fopen(outfilename, "wb");
			fwrite(paddeddata+4, 1, datalen, fout);
			fclose(fout);
		}
		printf("Decrypted.\n");
	} else if (stricmp(argv[1], "-test")==0) {
		U32 key[4] = {0x220CAE0A, 0xE5E0B992, 0xBC7A3D03, 0x6D83E9E7};
		int datalen;
		char *data = fileAlloc(argv[2], NULL);
		char *args[4];
		int numargs;
		BLOWFISH_CTX blowfish_ctx;
		cryptBlowfishInitU32(&blowfish_ctx,key,ARRAY_SIZE(key));
		datalen = *((U32*)data);
		cryptBlowfishDecrypt(&blowfish_ctx,data+4, PAD(datalen));
		numargs = tokenize_line_safe(data+4, args, ARRAY_SIZE(args), NULL);
		if (numargs!=3) {
			printf("Invalid string format\n");
		} else {
			if (stricmp(args[0], "CRYPTIC")!=0) {
				printf("First token not \"CRYPTIC\"\n");
			} else {
				int accesslevel = atoi(args[1]);
				char *issuedTo = args[2];
				printf("Successfully read key, accesslevel %d, issued to %s\n", accesslevel, issuedTo);
			}
		}
	} else {
		printf("Invalid command line parameter\n");
	}
	return 0;
}
