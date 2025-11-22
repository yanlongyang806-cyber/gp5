/*
 * public TransGaming API function definitions
 *
 * Copyright 2010 TransGaming Inc.
 */
#ifndef  __TG_PROCESS_H__
# define __TG_PROCESS_H__


# ifdef __cplusplus
extern "C" {
# endif


/* TGProcessLaunchNativeCmdline()  (NTDLL)
 *
 *  Launches a Mac application specified by an absolute path.
 *
 *  Parameters:
 *      wszNativePath  [in]: the name of the native Mac application to launch.
 *      bWaitUntilExit [in]: whether to wait for the application to finish
 *          before returning
 *      wszCmdLine     [in]: a string containing the arguments to pass to the
 *          application
 *      ppProcess     [out]: a handle of the process created.
 *
 *  Returns:
 *      TRUE if the native app is successfully found and launched.  FALSE if the
 *      app could not be found or could not be launched.
 *
 *  Remarks:
 *      There is no communication set up between the calling process and the
 *      new app. The command line argument string can contain escaped quotes.
 *      The process handle parameter may be NULL if not needed and if it is
 *      returned, it must be closed with TGProcessClose() when done with it
 *      (even after the external app terminates).
 *
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGProcessLaunchNativeCmdline(LPCWSTR wszNativePath, BOOL bWaitUntilExit,
                                         LPCWSTR wszCmdLine, LPVOID* ppProcess);

/* TGProcessIsRunning()  (NTDLL)
 *
 * Returns whether or not the process is still running.
 *
 *  Parameters:
 *      pProcess      [in]: a handle of the process.
 *
 *  Returns:
 *      TRUE if the native app is still running.  FALSE if the
 *      app has stopped running or if the handle is NULL.
 *
 *  Remarks:
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGProcessIsRunning(LPVOID pProcess);

/* TGProcessGetExitCode()  (NTDLL)
 *
 * Returns a process's exit code.
 *
 *  Parameters:
 *      pProcess      [in]: a handle of the process.
 *
 *  Returns:
 *      The process's exit code or -1 if the handle
 *      is NULL or the app is still running.
 *
 *  Remarks:
 *      This call is only supported on Apple.
 */
int  WINAPI TGProcessGetExitCode(LPVOID pProcess);

/* TGProcessTerminate()  (NTDLL)
 *
 * Terminates a process.
 *
 *  Parameters:
 *      pProcess      [in]: a handle of the process.
 *
 *  Returns:
 *      TRUE if the native app was successfully terminated.
 *      FALSE if the app wasn't successfully terminated or
 *      if the handle is NULL.
 *
 *  Remarks:
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGProcessTerminate(LPVOID pProcess);

/* TGProcessClose()  (NTDLL)
 *
 * Closes a process handle.
 *
 *  Parameters:
 *      pProcess      [in]: a handle of the process.
 *
 *  Returns:
 *      TRUE if the handle was closed.  FALSE if the
 *      handle was not closed or if the handle is NULL.
 *
 *  Remarks:
 *      Closing the process handle does not terminate the
 *      external app.
 *
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGProcessClose(LPVOID pProcess);

/* prototype definitions for the functions in this header */
typedef BOOL                (WINAPI *TYPEOF(TGProcessLaunchNativeCmdline))(LPCWSTR, BOOL, LPCWSTR, LPVOID*);
typedef BOOL                (WINAPI *TYPEOF(TGProcessIsRunning))(LPVOID);
typedef int                 (WINAPI *TYPEOF(TGProcessGetExitCode))(LPVOID);
typedef BOOL                (WINAPI *TYPEOF(TGProcessTerminate))(LPVOID);
typedef BOOL                (WINAPI *TYPEOF(TGProcessClose))(LPVOID);


# ifdef __cplusplus
}
# endif
#endif
