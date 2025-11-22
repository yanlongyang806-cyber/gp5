/*************************************************
*      Perl-Compatible Regular Expressions       *
*************************************************/

/* PCRE is a library of functions to support regular expressions whose syntax
and semantics are as close as possible to those of the Perl 5 language.

                       Written by Philip Hazel
           Copyright (c) 1997-2008 University of Cambridge

-----------------------------------------------------------------------------
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    * Neither the name of the University of Cambridge nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------------
*/


/* This is a freestanding support program to generate a file containing
character tables for PCRE. The tables are built according to the current
locale. Now that pcre_maketables is a function visible to the outside world, we
make use of its code from here in order to be consistent. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>

#include "pcre_internal.h"

#define DFTABLES          /* pcre_maketables.c notices this */
#include "pcre_maketables.c"


int main(int argc, char **argv)
{
FILE *f;
int i = 1;
const unsigned char *tables;
const unsigned char *base_of_tables;

/* By default, the default C locale is used rather than what the building user
happens to have set. However, if the -L option is given, set the locale from
the LC_xxx environment variables. */

if (argc > 1 && strcmp(argv[1], "-L") == 0)
  {
  setlocale(LC_ALL, "");        /* Set from environment variables */
  i++;
  }

if (argc < i + 1)
  {
  fprintf(stderr, "dftables: one filename argument is required\n");
  return 1;
  }

tables = pcre_maketables();
base_of_tables = tables;

f = fopen(argv[i], "wb");
if (f == NULL)
  {
  fprintf(stderr, "dftables: failed to open %s for writing\n", argv[1]);
  return 1;
  }

/* There are several fprintf() calls here, because gcc in pedantic mode
complains about the very long string otherwise. */

fprintf(f,
  "/*************************************************\n"
  "*      Perl-Compatible Regular Expressions       *\n"
  "*************************************************/\n\n"
  "/* This file was automatically written by the dftables auxiliary\n"
  "program. It contains character tables that are used when no external\n"
  "tables are passed to PCRE by the application that calls it. The tables\n"
  "are used only for characters whose code values are less than 256.\n\n");
fprintf(f,
  "The following #includes are present because witrary memory buffer of size %d\n", info.uncompressed_size);
				}

				unzClose(zipfile);
				return ret;
			}
		} while (unzGoToNextFile(zipfile)==UNZ_OK);
	}
	printf("Could not find a .mdmp file in the .zip file: %s\n", zipfilename);

	unzClose(zipfile);
	return ret;
}

int removeFromZip(char *zipfilename, char *srcfilename)
{
	char tempname[MAX_PATH];
	zipFile tempfile=0;
	unzFile zipfile;
	int ret = 1;
	char *data=NULL;
	bool didWork=false;

	strcpy(tempname, zipfilename);
	*strrchr(tempname, '.')='\0';
	strcat(tempname, "_temp.zip");

	// Open input file
	zipfile = unzOpen(zipfilename);
	if (!zipfile) {
		printf("Error openging %s\n", zipfilename);
		goto fail;
	}

	// Open temporary output file
	tempfile = zipOpen(tempname, APPEND_STATUS_CREATE);
	if (!tempfile) {
		printf("Error opening %s\n", tempname);
		goto fail;
	}

	if (UNZ_OK == unzGoToFirstFile(zipfile)) {
		do {
			char filename[MAX_PATH];
			unz_file_info info;
			unzGetCurrentFileInfo(zipfile,
				&info,
				filename,
				sizeof(filename),
				NULL,
				0,
				NULL,
				0);

			if (_stricmp(filename, srcfilename)!=0) {
				// Found an appropriate file!
				// Extract it
				data = (char*)malloc(info.uncompressed_size+1);
				if (data) {
					if (UNZ_OK==unzOpenCurrentFile(zipfile)) {
						int numread = unzReadCurrentFile(zipfile, data, info.uncompressed_size+1);
						if (numread == info.uncompressed_size) {
							// Write data to file and free!
							if (ZIP_OK != zipOpenNewFileInZip(tempfile,
								filename,
								NULL,
								NULL,
								0,
								NULL,
								0,
								NULL,
								Z_DEFLATED,
								Z_DEFAULT_COMPRESSION))
							{
								printf("Failure to open new file in zip\n");
								goto fail;
							}

							if (ZIP_OK != zipWriteInFileInZip(tempfile, data, info.uncompressed_size)) {
								printf("Error writing  file to zip\n");
								goto fail;
							}

							zipCloseFileInZip(tempfile);

						} else {
							printf("Failed to read entire minidump file\n");
							goto fail;
						}
					}
					free(data);
					data = NULL;
				} else {
					printf("Error allocating temporary memory buffer of size %d\n", info.uncompressed_size);
					goto fail;
				}

			} else {
				didWork = true;
			}
		} while (unzGoToNextFile(zipfile)==UNZ_OK);
	} else {
		printf("No files found in %s\n", zipfilename);
	}

	if (tempfile) {
		zipClose(tempfile, NULL);
		tempfile = NULL;
	}
	if (zipfile) {
		unzClose(zipfile);
		zipfile = NULL;
	}
	// Rename files around
	if (!didWork) {
		remove(tempname);
	} else {
		remove(zipfilename);
		rename(tempname, zipfilename);
	}

	ret = 0;

fail:
	if (tempfile)
		zipClose(tempfile, NULL);
	if (zipfile)
		unzClose(zipfile);
	if (data)
		free(data);
	return ret;
}

int addto