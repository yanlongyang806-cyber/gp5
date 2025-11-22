#include "ClipboardMonitor.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <direct.h>
#include "file.h"
#include "sysutil.h"
#include "utils.h"
#include "SuperAssert.h"
#include "timing.h"


#define REQUEST_STRING "SendFileRequest:"
#define REQUEST_ACK_STRING "Ack:"
#define REQUEST_DONE_STRING "Done:"
#define REQUEST_DATA_STRING "Data:"
#define EOM ":EOM"

static char dec_to_hex[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

// Server-state variables
static bool in_progress = false;
static char src_filename[MAX_PATH];
static char temp_filename[MAX_PATH];
static char dest_filename[MAX_PATH];
static bool waiting_for_filename;
static bool cancel_transfer;
static bool done_copying;
static U64 src_filesize;
static U64 file_progress;
static FILE *file_out=NULL;
#define BUFFER_SIZE 4096
static char cmdbuf[BUFFER_SIZE*2+32];
static int timer=0;
static F32 timeout = 0.5f;

U64 fileSize64(const char *filename)
{
	// NOT fileWatcher accelerated, use only in tools!
	struct __stat64 status;
	if(!_stat64(filename, &status)){
		return status.st_size;
	}
	return (U64)-1;
}

static void askForFileNameThread(void *param)
{
	OPENFILENAME theFileInfo;
	int		ret;
	char	base[_MAX_PATH];

	_getcwd(base,_MAX_PATH);
	memset(&theFileInfo,0,sizeof(theFileInfo));
	theFileInfo.lStructSize = sizeof(OPENFILENAME);
	theFileInfo.hwndOwner = compatibleGetConsoleWindow();
	theFileInfo.hInstance = NULL;
	theFileInfo.lpstrFilter = "*.*";
	theFileInfo.lpstrCustomFilter = NULL;
	strcpy(dest_filename, src_filename);
	theFileInfo.lpstrFile = dest_filename;
	theFileInfo.nMaxFile = 255;
	theFileInfo.lpstrFileTitle = NULL;
	theFileInfo.lpstrInitialDir = NULL;
	theFileInfo.lpstrTitle = NULL;
	theFileInfo.Flags = OFN_LONGNAMES | OFN_OVERWRITEPROMPT;
	theFileInfo.lpstrDefExt = NULL;

	ret = GetSaveFileName(&theFileInfo);
	_chdir(base);

	if (!ret) {
		cancel_transfer = true;
	}
	waiting_for_filename = false;
}

static void askForFileNameAsync(void)
{
	_beginthread(askForFileNameThread, 0, NULL);
}

static void checkTimer(void)
{
	if (in_progress && timerElapsed(timer) > timeout) {
		static int timeout_increase_timer=0;
		F32 newtimeout = timeout;
		if (!timeout_increase_timer) {
			timeout_increase_timer = timerAlloc();
			timerStart(timeout_increase_timer);
		} else if (timerElapsed(timeout_increase_timer) < timeout*3) {
			newtimeout = timeout * 2;
		}
		timerStart(timeout_increase_timer);
		if (newtimeout > 10) {
			newtimeout = 0.125f; // Cycle
		}
		printf("%1.3fs elapsed with no response, re-requesting.      \n", timeout);
		if (newtimeout != timeout) {
			printf("Too many frequent timeouts, changing timeout to %1.3fs.      \n", newtimeout);
		}
		timeout = newtimeout;
		sprintf(cmdbuf, REQUEST_ACK_STRING "%s:%I64d" EOM, src_filename, file_progress);
		winCopyToClipboard(cmdbuf);
		timerStart(timer);
	}
}

static void doneTransferring(void)
{
	DWORD ret;
	sprintf(cmdbuf, "Done transferring %s.  Would you like to open the folder containing it?", src_filename);
	ret = MessageBox(compatibleGetConsoleWindow(), cmdbuf, "CrypticRemote", MB_YESNO);
	if (ret == IDYES) {
		char temp[MAX_PATH];
		char *s;
		strcpy(temp, dest_filename);
		backSlashes(temp);
		s = strrchr(temp, '\\');
		if (s) {
			*s = 0;
		}
		ShellExecute ( NULL, "open", temp, NULL, NULL, SW_SHOW);
	}
}

bool clipboardMonitorActive(void)
{
	return in_progress || waiting_for_filename || done_copying;
}

void clipboardMonitor(void)
{
	const char *s;

	if (!timer) {
		printf("Monitoring for Clipboard copies...\n");
		timer = timerAlloc();
	}
	
	if (done_copying && !waiting_for_filename) {
		done_copying = false;
		if (cancel_transfer) {
			printf("Canceled\n");
			fileForceRemove(temp_filename);
		} else {
			printf("Moving to %s...\n", dest_filename);
			fileMove(temp_filename, dest_filename);
			printf("Done!\n");
			doneTransferring();
		}
	}
	
	s = winCopyFromClipboard();
	if (!s || !*s) {
		checkTimer();
		return;
	}
	if (strStartsWith(s, REQUEST_STRING) && strEndsWith(s, EOM))
	{
		char *s2;
		if (in_progress) {
			// Cancel existing transfer first!
			if (file_out) {
				fclose(file_out);
				file_out = NULL;
			}
		}
		Strncpyt(src_filename, s + strlen(REQUEST_STRING));
		src_filename[strlen(src_filename) - strlen(EOM)] = 0;
		s2 = strrchr(src_filename, ':');
		src_filesize = _atoi64(s2+1);
		file_progress = 0;
		*s2 = 0;
		printf("Got send file request for file \"%s\", size %I64d.\n", src_filename, src_filesize);
		sprintf(temp_filename, "./%s.temp", src_filename);
		if (fileExists(temp_filename)) {
			char msg[1024];
			DWORD ret;
			sprintf(msg, "%s appears to be partially downloaded, resume?", src_filename);
			ret = MessageBox(compatibleGetConsoleWindow(), msg, "CrypticRemote", MB_YESNO);
			if (ret == IDYES) {
				file_progress = fileSize64(temp_filename);
				file_out = fopen(temp_filename, "ab");
				if (!file_out) {
					printf("Error openning %s for appending\n", temp_filename);
					return;
				}
			} else {
				file_progress = 0;
				file_out = fopen(temp_filename, "wb");
			}
		} else {
			file_out = fopen(temp_filename, "wb");
		}
		if (!file_out) {
			printf("Error openning %s for writing\n", temp_filename);
			return;
		}

		sprintf(cmdbuf, REQUEST_ACK_STRING "%s:%I64d" EOM, src_filename, file_progress);
		winCopyToClipboard(cmdbuf);
		waiting_for_filename = true;
		cancel_transfer = false;
		done_copying = false;
		in_progress = true;
		askForFileNameAsync();
		timerStart(timer);
	} else if (file_out && strStartsWith(s, REQUEST_DONE_STRING) && strEndsWith(s, EOM)) {
		fclose(file_out);
		file_out = NULL;
		sprintf(cmdbuf, REQUEST_ACK_STRING "Done" EOM);
		winCopyToClipboard(cmdbuf);
		in_progress = false;
		if (cancel_transfer) {
			printf("Canceled                                                          \n");
			fileForceRemove(temp_filename);
		} else {
			if (waiting_for_filename) {
				printf("Done receiving file, waiting for filename...                      \n");
				done_copying = true;
			} else {
				printf("Done receiving file, moving to %s...                              \n", dest_filename);
				fileMove(temp_filename, dest_filename);
				printf("Done!\n");
				doneTransferring();
			}
		}
	} else if (file_out && strStartsWith(s, REQUEST_DATA_STRING) && strEndsWith(s, EOM)) {
		const char *data = s + strlen(REQUEST_DATA_STRING);
		U32 data_size = atol(data);
		U64 data_offset = _atoi64(strchr(data, ':')+1);
		U32 i;
		if (data_offset != file_progress) {
			// Bad data
			printf("Received bad data offset: %I64d; expected %I64d; re-requesting...  \n", data_offset, file_progress);
			sprintf(cmdbuf, REQUEST_ACK_STRING "%s:%I64d" EOM, src_filename, file_progress);
			winCopyToClipboard(cmdbuf);
			timerStart(timer);
		} else {
			// Good data
			data = strchr(data, ':') + 1;
			data = strchr(data, ':') + 1;
			if (data_size > strlen(data) / 2) {
				printf("Error: too large of a data_size sent!                         \n");
				in_progress = false;
				fclose(file_out);
				file_out = NULL;
				return;
			} else {
				for (i=0; i<data_size; i++) {
					char c1 = *data++;
					char c2 = *data++;
					int d1, d2;
					if (c1 >= 'A' && c1 <= 'F')
						d1 = c1 - 'A' + 10;
					else
						d1 = c1 - '0';
					if (c2 >= 'A' && c2 <= 'F')
						d2 = c2 - 'A' + 10;
					else
						d2 = c2 - '0';
					cmdbuf[i] = (d1 << 4) | d2;
				}
				fwrite(cmdbuf, 1, data_size, file_out);
				file_progress+=data_size;
				printf("Progress: %I64d/%I64d (%3.1f%%)   \r", file_progress, src_filesize, 100*(F32)file_progress/(F32)src_filesize);
				sprintf(cmdbuf, REQUEST_ACK_STRING "%s:%I64d" EOM, src_filename, file_progress);
				winCopyToClipboard(cmdbuf);
				timerStart(timer);
			}
		}
	}
	checkTimer();
}

void clipboardSend(const char *filename)
{
	FILE *file;
	U64 filesize;
	U64 fileoffs=0;
	bool sent_done = false;
	bool got_first_response = false;
	char *buffer = malloc(BUFFER_SIZE);
	printf("Sending %s...\n", filename);
	if (!fileExists(filename)) {
		printf("Error: cannot find file \"%s\".\n", filename);
		return;
	}
	file = fopen(filename, "rb");
	if (!file) {
		printf("Error: cannot open file \"%s\" for reading.\n", filename);
		return;
	}
	filesize = fileSize64(filename);
	if (filesize == (U64)-1) {
		printf("Error: cannot stat file \"%s\".\n", filename);
		return;
	}

	sprintf(cmdbuf, REQUEST_STRING "%s:%I64d" EOM, getFileNameConst(filename), filesize);
	winCopyToClipboard(cmdbuf);
	timer = timerAlloc();
	for (;;) {
		const char *s;
		Sleep(1);
		s = winCopyFromClipboard();
		if (s && strStartsWith(s, REQUEST_ACK_STRING) && strEndsWith(s, EOM)) {
			U64 new_fileoffs;
			got_first_response = true;
			// Other side is done with the data we sent
			s = s + strlen(REQUEST_ACK_STRING);
			if (strStartsWith(s, "Done:")) {
				printf("\n Done!\n");
				fclose(file);
				return;
			}
			// Otherwise assume a data request
			{
				char temp[1024];
				char *s2;
				Strncpyt(temp, s);
				// Check for position to start at
				s2 = strrchr(temp, ':');
				*s2 = 0;
				s2 = strrchr(temp, ':') + 1;
				new_fileoffs = _atoi64(s2);
				if (new_fileoffs == filesize) {
					//printf("Other side said they had the right sized file already!\n");
					sprintf(cmdbuf, REQUEST_DONE_STRING EOM);
					winCopyToClipboard(cmdbuf);
					sent_done = true;
					continue;
				} else if (new_fileoffs > filesize) {
					printf("Other side said they had too large of a partially downloaded file!\n");
					fclose(file);
					return;
				}
			}
			// Send next chunk of data
			if (new_fileoffs >= filesize) {
				// Done!
				sprintf(cmdbuf, REQUEST_DONE_STRING EOM);
				winCopyToClipboard(cmdbuf);
				sent_done = true;
			} else {
				// Send next chunk
				char *dataout;
				int i;
				U64 numread;
				if (fileoffs != new_fileoffs) {
					printf("Resuming from %I64d\n", new_fileoffs);
					fseek(file, new_fileoffs, SEEK_SET);
					fileoffs = new_fileoffs;
				}
				numread = fread(buffer, 1, BUFFER_SIZE, file);
				if (numread == 0) {
					printf("Error reading file, read 0 bytes!  Offs: %I64d  size: %I64d\n", fileoffs, filesize);
					fclose(file);
					return;
				}
				sprintf(cmdbuf, REQUEST_DATA_STRING "%I64d:%I64d:", numread, fileoffs);
				dataout = &cmdbuf[strlen(cmdbuf)];
				for (i=0; i<numread; i++) {
					U8 data = buffer[i];
					*dataout++ = dec_to_hex[data >> 4];
					*dataout++ = dec_to_hex[data & 0xf];
				}
				*dataout++ = '\0';
				strcat(cmdbuf, EOM);
				winCopyToClipboard(cmdbuf);
				fileoffs += numread;
			}
		} else {
			// Nothing interesting, check for retransmit
			if (timerElapsed(timer) > 1.0f && !got_first_response) {
				// Also happens when the dialog is up on the client
				//printf("Never received first response, retransmitting...\n");
				winCopyToClipboard(cmdbuf);
				timerStart(timer);
			}
		}
	}
}

