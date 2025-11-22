/*
 * public TransGaming API function definitions
 *
 * Copyright 2011 TransGaming Inc.
 */
#ifndef  __TG_PLAYER_H__
# define __TG_PLAYER_H__

# include "tg_enums.h"

# ifdef __cplusplus
extern "C" {
# endif

#define INVALID_PLAYER_HANDLE 0xffffffff

/*
 * TGPlayer: Controls for a full screen low level high definition video player
 *
 * Remarks:
 *     This API is currently only supported natively on the GTTV platform.
 *     Only H.264/AAC M4V videos are supported.
 */

/* TGPlayerCreate()      (USER32)
 *
 *  Create a new video player handle. Video player must be closed and destroyed
 *  once no longer in use to free resources. There is an upper limit on
 *  the number of players which can be in use at any given time. This limit
 *  is currently 8.
 *
 *  A new player should be created for each new video to be played.
 *
 *  Returns:
 *      A player handle on success, or INVALID_PLAYER_HANDLE on error
 */
DWORD WINAPI TGPlayerCreate();

/* TGPlayerLoadFromFile()      (USER32)
 *
 *  Load a video from the specified unix pathname. The file is specified
 *  relative to the current working directory of the process. TGPlayerPlay
 *  must be called to start the video playing. This function should be
 *  used only once per player.
 *
 *  Parameters:
 *      player [in]: player handle
 *      path [in]: video file source path
 *
 *  Returns:
 *      TRUE on success, FALSE otherwise
 */
BOOL WINAPI TGPlayerLoadFromFile(DWORD player, LPCSTR path);

/* TGPlayerStop()      (USER32)
 *
 *  Stop video player. Video can not be resumed after a stop. Must be followed by
 *  TGPlayerClose and TGPlayerDestroy.
 *
 *  Parameters:
 *      player [in]: player handle
 *
 *  Returns:
 *      TRUE on success, FALSE otherwise
 */
BOOL WINAPI TGPlayerStop(DWORD player);

/* TGPlayerPlayer()      (USER32)
 *
 *  Start video player
 *
 *  Parameters:
 *      player [in]: player handle
 *
 *  Returns:
 *      TRUE on success, FALSE otherwise
 */
BOOL WINAPI TGPlayerPlay(DWORD player);

/* TGPlayerSetDestRect()      (USER32)
 *
 *  Set video player display dimensions and position. Should be called
 *  immediately after create. Cannot be called after load. Dimensions
 *  specified should be within the bounds of the game's video resolution.
 *  The video will play in front of the game rendering plane. Game
 *  rendering will continue while the video plays.
 *
 *  Parameters:
 *      player [in]: player handle
 *      x [in]: x origin
 *      y [in]: y origin
 *      width [in]: width of video output (inclusive)
 *      height [in]: height of video output (inclusive)
 *
 *  Returns:
 *      TRUE on success, FALSE otherwise
 *
 *  Remarks:
 *      Must be used before calling TGPlayerLoadFromFile() and TGPlayerPlay()
 */
BOOL WINAPI TGPlayerSetDestRect(DWORD player, INT x, INT y, INT width, INT height);

/* TGPlayerGetState()      (USER32)
 *
 *  Get state of video player.
 *
 *  Parameters:
 *      player [in]: player handle
 *
 *  Returns:
 *      A value of TG_PLAYER_STATE enum
 */
TG_PLAYER_STATE WINAPI TGPlayerGetState(DWORD player);

/* TGPlayerClose()      (USER32)
 *
 *  Close video player stream. TGPlayerDestroy should be called subsequently.
 *
 *  Parameters:
 *      player [in]: player handle
 *
 *  Returns:
 *      TRUE on success, FALSE otherwise
 */
BOOL WINAPI TGPlayerClose(DWORD player);

/* TGPlayerDestroy()      (USER32)
 *
 *  Destroy player object. TGPlayerClose should be called previous to this.
 *
 *  Parameters:
 *      player [in]: player handle
 *
 *  Returns:
 *      TRUE on success, FALSE otherwise
 *
 *  Remarks:
 *      Should be called after TGPlayerClose() to free resources
 */
BOOL WINAPI TGPlayerDestroy(DWORD player);


/* prototype definitions for the functions in this header */
typedef DWORD (WINAPI *TYPEOF(TGPlayerCreate))();
typedef BOOL (WINAPI *TYPEOF(TGPlayerLoadFromFile))(DWORD, LPCSTR);
typedef BOOL (WINAPI *TYPEOF(TGPlayerStop))(DWORD);
typedef BOOL (WINAPI *TYPEOF(TGPlayerPlay))(DWORD);
typedef BOOL (WINAPI *TYPEOF(TGPlayerSetDestRect))(DWORD, INT, INT, INT, INT);
typedef TG_PLAYER_STATE (WINAPI *TYPEOF(TGPlayerGetState))(DWORD);
typedef BOOL (WINAPI *TYPEOF(TGPlayerClose))(DWORD);
typedef BOOL (WINAPI *TYPEOF(TGPlayerDestroy))(DWORD);


# ifdef __cplusplus
}
# endif
#endif
