/* File GameLocale.h
 *	Contains mappings between locale ID and string.  There is probably an ANSI way to do this.
 *	
 */

#ifndef GAME_LOCALE_H
#define GAME_LOCALE_H

#define DEFAULT_LOCALE_ID 0

char* locGetName(int localeID);
int locGetID(char* ID);
int locGetMaxLocaleCount();
int locIDIsValid(int localID);

int locGetIDInRegistry();
void locSetIDInRegistry(int localeID);

#endif