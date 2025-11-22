/*
 * public TransGaming API function definitions
 *
 * Copyright 2010 TransGaming Inc.
 */
#ifndef  __TG_PAUSE_H__
# define __TG_PAUSE_H__

# include "tg_enums.h"

# ifdef __cplusplus
extern "C" {
# endif


/* TG_PAUSE_COOKIE
 *
 * detection cookie value used to determine if a WM_ACTIVATEAPP message
 * arrived as the result of a pause event.  This value will only appear
 * on GTTV platforms.
 */
# define TG_PAUSE_COOKIE    ((LPARAM)0xcafebeef)


/* TGGTTVShowPauseMenu()      (USER32)
 *
 *  Show the GTTV system pause menu if it is not already active
 *
 *  Returns:
 *      TRUE on success, FALSE otherwise
 *
 *  Remarks:
 *      This call is only supported on GTTV.  When the app is successfully
 *      paused, it will receive the WM_ACTIVATEAPP windows message with
 *      wParam set to FALSE.  The lParam value will be TG_PAUSE_COOKIE.
 */
BOOL WINAPI TGGTTVShowPauseMenu();

/* TGGTTVRemovePauseMenu()      (USER32)
 *
 *  Remove the GTTV system pause menu if it is active
 *
 *  Returns:
 *      TRUE on success, FALSE otherwise
 *
 *  Remarks:
 *      This call is only supported on GTTV.  When the app is successfully
 *      unpaused, it will receive the WM_ACTIVATEAPP windows message with
 *      wParam set to TRUE.  The lParam value will be TG_PAUSE_COOKIE.
 */
BOOL WINAPI TGGTTVRemovePauseMenu();


/* prototype definitions for the functions in this header */
typedef BOOL (WINAPI *TYPEOF(TGGTTVShowPauseMenu))();
typedef BOOL (WINAPI *TYPEOF(TGGTTVRemovePauseMenu))();


# ifdef __cplusplus
}
# endif
#endif
