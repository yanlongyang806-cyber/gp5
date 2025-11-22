/* File main.c for UpdateLocalizedText.exe
 *	This program is used to update the localized versions of the message stores once the 
 *	
 */


#include <stdio.h>
#include "GameLocale.h"
#include "File.h"
#include "Utils.h"
#include "HashTable.h"
#include "Array.h"
#include "MemoryPool.h"
#include <assert.h>

unsigned char UTF8BOM[] = {0xEF, 0xBB, 0xBF};
char* baseTextDir;
char defaultLocaleDirectory[512];

void processMessageFile(char* filename);
void printUsage();
FileScanAction MessageFileScanProcessor(char* dir, struct _finddata_t* data);

void printUsage(){
	printf("UpdateLocalizedText <base text directory>");
}

void main(int argc, char** argv){
	if(argc < 2){
		printUsage();
		return;
	}

	baseTextDir = argv[1];
	if(!dirExists(baseTextDir)){
		printf("Invalid base text directory: \"%s\"\n", baseTextDir);
		return;
	}

	{
		sprintf(defaultLocaleDirectory, "%s\\%s", baseTextDir, locGetName(DEFAULT_LOCALE_ID));

		// Walkthrough all files in the default locale directory.
		printf("Scanning directory \"%s\"\n\n", defaultLocaleDirectory);
		fileScanDirRecurseEx(defaultLocaleDirectory, MessageFileScanProcessor);
		printf("\nDone!\n", defaultLocaleDirectory);
	}
}

FileScanAction MessageFileScanProcessor(char* dir, struct _finddata_t* data){
	char* fullFilePath[1024];

	// Skip all files that have the .bak extension
	if(strstri(data->name, ".bak"))
		return FSA_EXPLORE_DIRECTORY;

	printf("\t%s\n", data->name);

	sprintf(fullFilePath, "%s\\%s", dir, data->name);
	processMessageFile(fullFilePath);
	
	return FSA_EXPLORE_DIRECTORY;
}


/***********************************************************************
 * Message
 *	A messageID + messageBody pair.
 *	Also recorded is the line number one which the message was found and
 *	whether it has been commented out.
 */

typedef struct{
	int		lineno;
	int		commented;
	char*	messageID;
	char*	messageBody;
	int		msgEndLocation;
} Message;

static MemoryPool messagePool;
Message* createMessage(){
	if(!messagePool){
		messagePool = createMemoryPool();
		initMemoryPool(messagePool, sizeof(Message), 1024);
	}
	return mpAlloc(messagePool);
}

void destroyMessage(Message* msg){
	mpFree(messagePool, msg);
}
/*
 * Message
 ***********************************************************************/

/***********************************************************************
 * Message
 *	Message collection.
 *	
 */
typedef struct{
	Array messages;				// Collection of messages as it is encountered
	HashTable messageBodyMap;	// MessageID->MessageBody map
} MessageCollection;

MessageCollection* createMessageCollection(){
	MessageCollection* collection;

	collection = calloc(1, sizeof(MessageCollection));

	collection->messageBodyMap = createHashTable();
	initHashTable(collection->messageBodyMap, 1024);
	hashSetMode(collection->messageBodyMap, CopyKeyNames | AllowDupicateHashVal | VerifyHashValUniqueness | CaseInsensitive);
	return collection;
}

void destroyMessageCollection(MessageCollection* collection){
	destroyHashTable(collection->messageBodyMap);
	free(collection);
}
/*
 *
 ***********************************************************************/

/***********************************************************************
 *
 */
typedef struct{
	char*	fileContents;
	int		fileLength;
	char*	readCursor;
	int		lineno;
} MessageFileReader;

MessageFileReader* createMessageFileReader(){
	return calloc(1, sizeof(MessageFileReader));
}

void destroyMessageFileReader(MessageFileReader* reader){
	if(reader->fileContents)
		free(reader->fileContents);
	free(reader);
}

/* Function initMessageFileReader()
 *	Tells the message file reader which file it is supposed to be reading from.
 *
 *	Returns:
 *		1 - message file reader initialized
 *		0 - message file reader initialization failed because the specified file cannot be read.
 */
int initMessageFileReader(MessageFileReader* reader, char* filename){
	reader->readCursor = reader->fileContents = fileAlloc(filename, &reader->fileLength);
	reader->lineno = 0;

	reader->readCursor+=3;
	if(!reader->readCursor)
		return 0;
	else
		return 1;
}

int readerEOF(MessageFileReader* reader){
	return(reader->readCursor >= reader->fileContents + reader->fileLength);
}

/* Function mfrReadMessage()
 *	This function reads just enough data from the file to generate a Message.
 *	The caller owns the Message.
 *	
 *	Returns:
 *		NULL - There are no more messages to be extracted from the file.
 *		valid Message pointer - The newly extracted message.
 */
typedef enum{
	MPS_BEGIN,
	MPS_FOUND_FIRST_COMMENT_CHAR,
	MPS_COMMENTED_LINE,
	MPS_EXTRACT_STRING,
	MPS_WAITING_END_QUOTE,
} MessageParseState;

Message* mfrReadMessage(MessageFileReader* reader){	
	static Message* msg = NULL;
	Message* newMsg;
	MessageParseState state = MPS_BEGIN;
	int stopParsing = 0;
	int potentialComment = 0;
	#define CUR_CHAR (*reader->readCursor)
	#define ADVANCE_CURSOR() (reader->readCursor++)

	if(!msg)
		msg = createMessage();
	else
		memset(msg, 0, sizeof(Message));
	msg->lineno = reader->lineno;

	
	while(!readerEOF(reader)){
		switch(state){
			case MPS_BEGIN:
				if(CUR_CHAR == '#'){
					// Found a line that starts with a comment token.
					// If neither the messageID nor the body have been extracted yet,
					// we're looking at a commented line, which potentially holds
					// commented string pairs.
					if(!msg->messageID && !msg->messageBody){
						potentialComment = 1;
					}


					if(msg->messageID && !msg->messageBody){
						assert(0);	// I'll deal with this case later.
					}
				}else if(CUR_CHAR == '\"'){
					// Found begin quote.
					state = MPS_EXTRACT_STRING;
				}

				break;
			case MPS_EXTRACT_STRING:
				if(!msg->messageID){
					msg->messageID = reader->readCursor;
				}else{
					msg->messageBody = reader->readCursor;
				}
				state = MPS_WAITING_END_QUOTE;
				break;

			case MPS_WAITING_END_QUOTE:
				// Found end quote?
				if(CUR_CHAR == '\"'){
					*reader->readCursor = '\0';
					if(msg->messageBody){
						msg->msgEndLocation = reader->readCursor+1;
						stopParsing = 1;
					}
					else
						state = MPS_BEGIN;
				}
				break;
				
		}

		if(CUR_CHAR == '\n'){
			reader->lineno++;
			potentialComment = 0;
			if(!msg->messageID || !msg->messageBody)
				msg->lineno = reader->lineno;
		}

		ADVANCE_CURSOR();
		if(stopParsing)
			break;
	}

	if(!msg->messageID || !msg->messageBody)
		return NULL;

	msg->commented = potentialComment;
	newMsg = msg;
	msg = NULL;
	return newMsg;
}

/* Function mfrReadMessaeg()
 *	This function reads just enough data from the file to generate a Message.
 *	The caller owns the Message.
 *	
 *	Returns:
 *		NULL - There are no more messages to be extracted from the file.
 *		valid Message pointer - The newly extracted message.
 */
MessageCollection* mfrReadMessageCollection(MessageFileReader* reader){
	Message* newMessage;
	MessageCollection* collection;
	collection = createMessageCollection();

	while(newMessage = mfrReadMessage(reader)){
		hashAddElement(collection->messageBodyMap, newMessage->messageID, newMessage);
		arrayPushBack(&collection->messages, newMessage);
	}

	return collection;
}
/*
 *
 ***********************************************************************/




void processMessageFile(char* filename){
	MessageFileReader* defaultReader;
	MessageCollection* defaultCollection;
	int i;

	// Assume that the default locale text files are always up to date.

	// Perform error checking on the file under examination.
	// Do not update the corresponding files in other locales if the file is not valid.
	//	Skip this for now.

	// Load the file under examination in the default locale directory.
	defaultReader = createMessageFileReader();
	initMessageFileReader(defaultReader, filename);
	defaultCollection = mfrReadMessageCollection(defaultReader);

	// For each valid locale...
	for(i = 1; i < locGetMaxLocaleCount(); i++){
	//		Load corresponding file in the locale.

		FILE* origFile;
		FILE* outputFile;
		FILE* localeFile;
		FILE* newFile;

		char origBuffer[1024];			// File that holds the default locale data.
		char localeFilename[512];		// File that holds the current locale specific data.
		char outputFilename[512];		// File that holds the new locale specific data.
		char newFilename[512];			// File that holds new fields that are added to locale specific files.

		int j;
		int lineno = 0;
		int readSize;


		MessageFileReader* localeReader;
		MessageCollection* localeCollection;

		// Figure out where the relavent files are and prepare them for reading and writing.
		//		Open the default locale mesasge file again.  Besides all the message pairs we've read in,
		//		we also want to output any comments that are in the original message files.
		origFile = fopen(filename, "rt");
		fseek(origFile, 3, SEEK_SET);

		//		What is the locale specific file we want to open?
		sprintf(localeFilename, "%s\\%s\\%s", baseTextDir, locGetName(i), filename + strlen(defaultLocaleDirectory) + 1);
		localeReader = createMessageFileReader();
		initMessageFileReader(localeReader, localeFilename);
		localeCollection = mfrReadMessageCollection(localeReader);
		

		//		Given the current locale, what is the file we want to use as output?
		sprintf(outputFilename, "%s.tmp", localeFilename);
		outputFile = fopen(outputFilename, "wt");
		fwrite(UTF8BOM, 1, sizeof(UTF8BOM), outputFile);

		//		Where do we want dump the new messages that are added to the file?
		sprintf(newFilename, "%s.new", localeFilename);
		newFile = fopen(newFilename, "wt");
		fwrite(UTF8BOM, 1, sizeof(UTF8BOM), newFile);


		// Construct the new locale specific message file and a list of new messages that translators ought to look at.
		//		Go through all messages in the default locale...
		for(j = 0; j < defaultCollection->messages.size; j++){
			Message* msg;
			int linesSkipped = 0;
			msg = defaultCollection->messages.storage[j];

			// Output all contents in the orig file that appears before the line where the
			// current message line appears.
			// These lines should contain comments only.
			while(fgets(origBuffer, 1024, origFile) && lineno < msg->lineno){
				fprintf(outputFile, "%s", origBuffer);
				lineno++;
				linesSkipped = 1;
			}

			// Account for the line that contains the ID + message in the default file.
			lineno++;

			// If a message is commented out, comment it out in the output file as well.
			if(msg->commented)
				fprintf(outputFile, "#");

			
			{
				Message* localizedMessage;
				Message* outputMessage;

				localizedMessage = hashFindValue(localeCollection->messageBodyMap, msg->messageID);

				// If a localized version of the message exists, output the localized version.
				if(localizedMessage)
					outputMessage = localizedMessage;
				else
					outputMessage = msg;

				fprintf(outputFile, "\"%s\": \"%s\"\n", outputMessage->messageID, outputMessage->messageBody);

				
				// Decide whether to output this ID + message pair to the new messages file.
				//		The message is new if:
				//			1) a localized version does not exist at all.
				//			2) a localized version exists but is identical to the default version.
				//				This means that the message has not been translated yet.  It should stay in the new messages
				//				file.
				if(!localizedMessage || (localizedMessage && 0 == strcmp(msg->messageBody, localizedMessage->messageBody))){
					fprintf(newFile, "\"%s\": \"%s\"\n", msg->messageID, msg->messageBody);
				}
			}
		}

		fclose(origFile);
		fclose(outputFile);
		fclose(newFile);
		destroyMessageCollection(localeCollection);
		destroyMessageFileReader(localeReader);

		remove(localeFilename);
		rename(outputFilename, localeFilename);
	}

	destroyMessageCollection(defaultCollection);
	destroyMessageFileReader(defaultReader);
}


//void printUsage(){
//	printf("UpdateLocalizedText <base text directory>");
//}
//
//
//FileScanAction MessageFileScanProcessor(char* dir, struct _finddata_t* data){
//	char* fullFilePath[1024];
//
//	// Skip all files that have the .bak extension
//	if(strstri(data->name, ".bak"))
//		return FSA_EXPLORE_DIRECTORY;
//
//	if(strstri(data->name, ".old"))
//		return FSA_EXPLORE_DIRECTORY;
//
//	printf("\t%s\n", data->name);
//
//	sprintf(fullFilePath, "%s\\%s", dir, data->name);
//	processMessageFile(fullFilePath);
//	
//	return FSA_EXPLORE_DIRECTORY;
//}
//
//void main(int argc, char** argv){
//	if(argc < 2){
//		printUsage();
//		return;
//	}
//
//	baseTextDir = argv[1];
//	if(!dirExists(baseTextDir)){
//		printf("Invalid base text directory: \"%s\"\n", baseTextDir);
//		return;
//	}
//
//	{
//		char defaultLocaleDirectory[1024];
//
//		sprintf(defaultLocaleDirectory, "%s\\%s", baseTextDir, locGetName(DEFAULT_LOCALE_ID));
//
//		// Walkthrough all files in the default locale directory.
//		printf("Scanning directory \"%s\"\n\n", defaultLocaleDirectory);
//		fileScanDirRecurseEx(defaultLocaleDirectory, MessageFileScanProcessor);
//		printf("\nDone!\n", defaultLocaleDirectory);
//	}
//}