/*
 * public TransGaming API function definitions
 *
 * Copyright 2010 TransGaming Inc.
 */
#ifndef  __TG_LAUNCH_H__
# define __TG_LAUNCH_H__

# include "tg_enums.h"


# ifdef __cplusplus
extern "C" {
# endif


/* type APPHANDLE: opaque handle to a running unix application.  This is used in the
     TGLaunchUNIXApp() and TGUNIXApp*() functions to manage access to a running unix
     process. */
typedef LPVOID APPHANDLE;


/* TGInstallAsRoot()    (USER32)
 *
 *  attempts to execute a unix command with root permissions.
 *
 *  Parameters:
 *      command [in]: the unix command to attempt to execute.  This must be a fully
 *          qualified unix command.  Root permissions will be requested from the
 *          user even if the command line is incorrect.  The string must be in UTF-16
 *          encoding.  This parameter may not be NULL.
 *      argList [in]: NULL terminated list of arguments strings to be passed to the
 *          unix application.  Each separate argument must be provided in its own
 *          string.  The last entry in the list must be NULL.  The strings must all
 *          be in UTF-16 encoding.  This parameter may be NULL if no arguments are
 *          required.  See the remarks section for more information on using this
 *          parameter.
 *
 *  Returns:
 *      On success, the function returns INSTALLROOT_SUCCESS.  On failure, the function
 *      returns an INSTALLROOT_* error code indicating the reason why it failed.
 *
 *  Remarks:
 *      This function allows the user to authenticate the requested process to run with
 *      root permissions.  It is entirely up to the user to determine whether the command
 *      should be allowed to run - they may cancel the authentication at any time.  Any
 *      game that makes use of this function should appropriately handle the case of it
 *      failing.  Once authenticated, the unix command will be executed with the provided
 *      permissions.
 *
 *      The argument list is to be provided as a NULL terminated string list.  Each
 *      argument should be separated out to its own UTF-16 encoded string in the list.
 *      The last item in the list must always be NULL to indicate the end.  For example,
 *      if the unix command "/bin/ls -l -a -F" were to be executed, the <argList>
 *      parameter would be created as:
 *
 *          const WCHAR argList[] = {L"-l", L"-a", L"-F", NULL};
 *
 *      and the function would be called as:
 *
 *          INSTALLROOT_STATUS status = TGInstallAsRoot(L"/bin/ls", argList);
 *
 *      This call is only supported on Apple.
 */
INSTALLROOT_STATUS WINAPI TGInstallAsRoot(LPCWSTR   command,
                                          LPCWSTR * argList);


/* TGLaunchUNIXApp()   (NTDLL)
 *
 *  launches a native unix app and returns a handle to a communications pipe for it.
 *
 *  Parameters:
 *      pPath [in]: path indicating the unix command to be executed.  This string
 *          includes all command line arguments.  This path may not be NULL.
 *      pMode [in]: the communications mode to open the pipe to the unix app in.
 *          This may be either "r" for reading, "w" for writing, or "r+" for both
 *          reading and writing.
 *
 *  Returns:
 *      On success, an opaque handle to the communications port for the unix app.
 *      On failure, NULL is returned.
 *
 *  Remarks:
 *      This function launches a unix app and opens a communications port between
 *      it and the calling process.  The handle that is returned is used to facilitate
 *      communication to the app through the TGUNIXAppWriteLine() and TGUNIXAppReadLine()
 *      functions.  When all communication is complete, the handle must be closed using
 *      the TGUNIXAppClose() function.
 *
 *      This function may not be used to attach communications to a unix app that
 *      is already executing.
 */
APPHANDLE WINAPI TGLaunchUNIXApp(LPCSTR pPath,
                                 LPCSTR pMode);


/* TGUNIXAppWriteLine()     (NTDLL)
 *
 *  writes a line of text to an open unix app.
 *
 *  Parameters:
 *      appId [in]: handle to the open unix app to send the text to.  This handle
 *          must have been previously returned by a call to TGLaunchUNIXApp().
 *      pLine [in]: null terminated line of text to be sent to the unix app.
 *
 *  Returns:
 *      TRUE if the line of text was successfully written to the app.  FALSE if
 *      the app has quit or the handle has been closed.
 *
 *  Remarks:
 *      This function is used to send input to the external unix app.  The line
 *      of text will be received by the app on its <stdin> file handle.  There
 *      will always be a newline appended to the provided text before sending.
 *      The input is always assumed to be plain ASCII text.  There is currently
 *      no way to send binary data to the app.
 *
 *      The function will fail if the app quits before this function is called.
 *      When this occurs, the handle should be closed immediately with a call to
 *      TGUNIXAppClose().
 */
BOOL WINAPI TGUNIXAppWriteLine(APPHANDLE    appId,
                               LPCSTR       pLine);


/* TGUNIXAppReadLine()     (NTDLL)
 *
 *  reads a line of text from an open unix app.
 *
 *  Parameters:
 *      appId [in]: handle to the open unix app to read the text from.  This
 *          handle must have been previously returned by a call to
 *          TGLaunchUNIXApp().
 *      pBuf [out]: buffer to store the read data in.
 *      bufSize [in]: size of the buffer <pBuf> in characters.
 *
 *  Returns:
 *      TRUE if the line of text was successfully read from the app.  FALSE if
 *      the app has quit or the handle has been closed.
 *
 *  Remarks:
 *      This function is used to read output from the external unix app.  The
 *      line of text will be read from the app on its <stdout> file handles.
 *      The final newline will always be stripped from the string before return.
 *      The output is always assumed to be plain ASCII text.  There is currently
 *      no way to receive binary data from the app.
 *
 *      The function will fail if the app quits before this function is called.
 *      When this occurs, the handle should be closed immediately with a call to
 *      TGUNIXAppClose().
 */
BOOL WINAPI TGUNIXAppReadLine(APPHANDLE appId,
                              LPSTR     pBuf,
                              SIZE_T    bufSize);


/* TGUNIXAppClose()     (NTDLL)
 *
 *  closes an open unix app handle.
 *
 *  Parameters:
 *      appId [in]: handle to the open unix app to close.
 *
 *  Returns:
 *      TRUE if the handle is successfully closed.  FALSE if the handle could not
 *      be closed or if the handle is invalid.
 *
 *  Remarks:
 *      This function must be used to clean up the handle to the unix app that is
 *      opened by a previous call to TGLaunchUNIXApp().  Each handle must be closed
 *      in order to prevent a resource leak.
 */
BOOL WINAPI TGUNIXAppClose(APPHANDLE appId);


/* TGLaunchNativeApplication()  (NTDLL)
 *
 *  launches a Mac application that is stored in the game's package.
 *
 *  Parameters:
 *      pNativePath [in]: the name of the native Mac application to launch.  This
 *          app must exist in the game package's MacOS folder, and may be either
 *          a mac package or a native unix executable.
 *
 *  Returns:
 *      TRUE if the native app is successfully found and launched.  FALSE if the
 *      app could not be found or could not be launched.
 *
 *  Remarks:
 *      This function may be used to launch an auxillary native Mac app that is
 *      stored in the game's MacOS folder.  Only executables and packages located
 *      in the MacOS folder will be considered.  An absolute path may not be given.
 *      There is no communication set up between the calling process and the new
 *      app, and command line arguments may not be provided.  The 'cider' executable
 *      may not be launched in this manner.  It must be launched implicitly by using
 *      a call to CreateProcess() or any ShellExecute*() function.
 *
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGLaunchNativeApplication(LPCSTR pNativePath);


/* TGLaunchNativeCmdline()  (NTDLL)
 *
 *  Launches a Mac application specified by an absolute path.
 *
 *  Parameters:
 *      wszNativePath  [in]: the name of the native Mac application to launch.
 *      bWaitUntilExit [in]: whether to wait for the application to finish
 *          before returning
 *      wszCmdLine     [in]: a string containing the arguments to pass to the
 *          application
 *
 *  Returns:
 *      TRUE if the native app is successfully found and launched.  FALSE if the
 *      app could not be found or could not be launched.
 *
 *  Remarks:
 *      There is no communication set up between the calling process and the
 *      new app. The command line argument string can contain escaped quotes.
 *
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGLaunchNativeCmdline(LPCWSTR wszNativePath, BOOL bWaitUntilExit, LPCWSTR wszCmdLine);


/* prototype definitions for the functions in this header */
typedef INSTALLROOT_STATUS  (WINAPI *TYPEOF(TGInstallAsRoot))(LPCWSTR, LPCWSTR *);
typedef APPHANDLE           (WINAPI *TYPEOF(TGLaunchUNIXApp))(LPCSTR, LPCSTR);
typedef BOOL                (WINAPI *TYPEOF(TGUNIXAppWriteLine))(APPHANDLE, LPCSTR);
typedef BOOL                (WINAPI *TYPEOF(TGUNIXAppReadLine))(APPHANDLE, LPSTR, SIZE_T);
typedef BOOL                (WINAPI *TYPEOF(TGUNIXAppClose))(APPHANDLE);
typedef BOOL                (WINAPI *TYPEOF(TGLaunchNativeApplication))(LPCSTR);
typedef BOOL                (WINAPI *TYPEOF(TGLaunchNativeCmdline))(LPCWSTR, BOOL, LPCWSTR);


# ifdef __cplusplus
}
# endif
#endif
