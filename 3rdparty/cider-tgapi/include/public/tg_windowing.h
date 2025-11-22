/*
 * public TransGaming API function definitions
 *
 * Copyright 2010 TransGaming Inc.
 */
#ifndef  __TG_WINDOWING_H__
# define __TG_WINDOWING_H__


# ifdef __cplusplus
extern "C" {
# endif


/* TGSetFullscreen()        (USER32)
 *
 *  sets the current fullscreen state of the app.
 *
 *  Parameters:
 *      fullscreen [in]: TRUE if the app should be put into fullscreen mode, and FALSE
 *          if it should be put into windowed mode.
 *
 *  Returns:
 *      TRUE if the app's display mode was successfully changed or was already in the
 *      requested mode.  FALSE if the call is unsupported.
 *
 *  Remarks:
 *      This function is used to switch the app into windowed mode or fullscreen.  On
 *      the Windows side of the app, the game will always be running in fullscreen mode,
 *      but on the mac side, it can be programmatically toggled using this function.
 *
 *      If the app is already in the requested mode, the call is simply ignored.  Note
 *      that this call can take several seconds to return as it waits for the mode change
 *      operation to complete.
 *
 *      Note that when switching into fullscreen mode, the game window MUST be at a valid
 *      fullscreen resolution first.  The list of supported resolutions in the system can
 *      be retrieved using EnumDisplaySettings().  If a valid fullscreen resolution is not
 *      currently set, this function will fail and the window will remain in windowed mode.
 *
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGSetFullscreen(BOOL fullscreen);

/* TGSetSkipFullScreenStateSave()       (USER32)
 *
 *  sets the flag to indicate that the next fullscreen change should not be saved.
 *
 *  Parameters:
 *      none.
 *
 *  Returns:
 *      No return value.
 *
 *  Remarks:
 *      This sets the flag that indicates that the next fullscreen mode change state should
 *      not be saved to the registry.  This is used internally to prevent the fullscreen state
 *      from being saved during temporary mode changes (ie: minimizing, dialog boxes, etc).
 *      This saving normally occurs each time a successful fullscreen state change completes.
 *      This is useful for situations such as temporary switches back to windowed mode or
 *      minimizing the app from fullscreen.  This prevents the temporary state from being saved
 *      so that the app doesn't always start in windowed mode.
 *
 *      This call is only supported on Apple.
 */
void WINAPI TGSetSkipFullScreenStateSave();

/* TGStartFullscreen()      (USER32)
 *
 *  sets the initial fullscreen status of the app.
 *
 *  Parameters:
 *      fullscreen [in]: TRUE if the app should start up in fullscreen mode, and FALSE
 *          if the app should start up in windowed mode.
 *
 *  Returns:
 *      TRUE if the startup state was successfully set.  FALSE if the call is unsupported.
 *
 *  Remarks:
 *      This function may be used to set the initial fullscreen or windowed mode state
 *      of the game.  This will override the state that is stored in the game's config
 *      file (Mac config file).  This call must be made before the game creates a graphics
 *      context and opens its first window.  If the call is made after the graphics
 *      context is created, it will have no effect.
 *
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGStartFullscreen(BOOL fullscreen);


/* TGIsFullscreen()         (USER32)
 *
 *  queries the fullscreen state of the game's window.
 *
 *  Parameters:
 *      none.
 *
 *  Returns:
 *      TRUE if the game is currently in fullscreen mode.  FALSE if the game is currently
 *      in windowed mode or the call is unsupported.
 *
 *  Remarks:
 *      This function may be used to query whether the game is currently in fullscreen or
 *      windowed mode.  The return value will always be FALSE if the game does not have a
 *      window associated with it (ie: no open graphics context).
 *
 *      Note that if the last switch to fullscreen mode failed, this function will continue
 *      to return FALSE.
 *
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGIsFullscreen();


/* TGMinimizeApp()          (USER32)
 *
 *  minimizes (hides) the game's window.
 *
 *  Parameters:
 *      none.
 *
 *  Returns:
 *      TRUE if the game window was successfully minimized.  FALSE if the window is already
 *      minimized or the call is unsupported.
 *
 *  Remarks:
 *      This function may be used to force the game window to be minimized.  The call will
 *      be ignored if the window is already minimized.  There is no way to programmatically
 *      un-minimize the window - that task is left up to the user.  This operation will
 *      work whether in windowed mode or fullscreen.  Note that this function may take 
 *      several seconds to return because it waits for the minimize operation to complete.
 *
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGMinimizeApp();


/* TGSwitchFocusToExecutable()  (USER32)
 *
 *  switches the application focus to a running windows executable.
 *
 *  Parameters:
 *      exeName [in]: windows executable name to switch focus to.  This can be either the
 *          full path for the executable module, or just its base name.
 *
 *  Returns:
 *      no return value.
 *
 *  Remarks:
 *      This function may be used to bring the window for a specified windows app to the
 *      front of the window order.  The executable process must already be running and
 *      should be in the same process space as the calling process (ie: running out of
 *      the same game package).  This cannot be used to launch a new process.  If the
 *      executable with the specified name is not found or it doesn't have an open window,
 *      the call is ignored.
 *
 *      This call is only supported on Apple.
 */
void WINAPI TGSwitchFocusToExecutable(LPCSTR exeName);



/* prototype definitions for the functions in this header */
typedef BOOL    (WINAPI *TYPEOF(TGSetFullscreen))(BOOL);
typedef void    (WINAPI *TYPEOF(TGSetSkipFullScreenStateSave))();
typedef BOOL    (WINAPI *TYPEOF(TGStartFullscreen))(BOOL);
typedef BOOL    (WINAPI *TYPEOF(TGIsFullscreen))();
typedef BOOL    (WINAPI *TYPEOF(TGMinimizeApp))();
typedef void    (WINAPI *TYPEOF(TGSwitchFocusToExecutable))(LPCSTR);


# ifdef __cplusplus
}
# endif
#endif
