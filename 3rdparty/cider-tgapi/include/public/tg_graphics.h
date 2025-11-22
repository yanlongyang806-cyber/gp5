/*
 * public TransGaming API function definitions
 *
 * Copyright 2010 TransGaming Inc.
 */
#ifndef  __TG_GRAPHICS_H__
# define __TG_GRAPHICS_H__

# ifdef __cplusplus
extern "C" {
# endif


/* TGGetMTGL()      (USER32)
 *
 *  retrieves the current activation status of the multithreaded GL driver.
 *
 *  Parameters:
 *      none.
 *
 *  Returns:
 *      TRUE if the multithreaded GL driver is currently enabled.  FALSE if the 
 *      multithreaded GL driver is currently disabled or the call is unsupported.
 *
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGGetMTGL();


/* TGSetMTGL()      (USER32)
 *
 *  enables or disables the multithreaded GL driver.
 *
 *  Parameters
 *      state [in]: TRUE if the MTGL driver is to be enabled, FALSE otherwise.
 *
 *  Returns:
 *      no return value.
 *
 *  Remarks:
 *      This enables or disables the multithreaded GL driver.  This is a system
 *      level driver that can improve rendering performance in some applications.
 *      Toggling this on and off should not be done frequently as it causes a
 *      small pause in rendering performance.
 *
 *      This call is only supported on Apple.
 */
void WINAPI TGSetMTGL(BOOL state);


/* TGToggleMTGL()   (USER32)
 *
 *  toggles the current state of the multithreaded GL driver.
 *
 *  Parameters:
 *      none.
 *
 *  Returns:
 *      no return value.
 *
 *  Remarks:
 *      Toggles the multithreaded GL driver on or off.  The current state can be
 *      queried using the TGGetMTGL() function.
 *
 *      This call is only supported on Apple.
 */
void WINAPI TGToggleMTGL();


/* prototype definitions for the functions in this header */
typedef BOOL    (WINAPI *TYPEOF(TGGetMTGL))();
typedef void    (WINAPI *TYPEOF(TGSetMTGL))(BOOL);
typedef void    (WINAPI *TYPEOF(TGToggleMTGL))();


# ifdef __cplusplus
}
# endif
#endif
