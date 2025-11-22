/*
 * public TransGaming API function definitions
 *
 * Copyright 2010 TransGaming Inc.
 */
#ifndef  __TG_OS_H__
# define __TG_OS_H__

# include "tg_enums.h"


# ifdef __cplusplus
extern "C" {
# endif


#define TGAPI_PLATFORMINFO_VERSION  1

/* struct TGAPI_PlatformInfo: struct used in the return value of TGGetSystemInfo(). */
typedef struct
{
    /* version of this struct.  This must be set to TGAPI_PLATFORMINFO_VERSION before
       calling TGGetSystemInfo(). */
    DWORD   version;

    /* type of machine */
    CHAR    type[64];

    /* major and minor version numbers */
    DWORD   major_version;
    DWORD   minor_version;

    /* extra info for the platform */
    CHAR    extra[128];

    /* distribution specific information */
    CHAR    distro[1024];

    /* 32 or 64 bit distribution */
    DWORD   bitcount;
} TGAPI_PlatformInfo;



/* IsTransgaming()      (NTDLL)
 *
 *  tests whether the game is running under the TG SDK.
 *
 *  Parameters:
 *      none.
 *
 *  Returns:
 *      TRUE if the game is currently running on a TransGaming platform (ie: MacOSX,
 *      or Linux).  Returns FALSE otherwise.
 */
BOOL WINAPI IsTransgaming();


/* TGGetOS()    (NTDLL)
 * 
 *  returns a string describing the platform the game is actually running on.
 *
 *  Parameters:
 *      none.
 *
 *  Returns:
 *      a pointer to a string describing the platform that the game is running
 *      on.  Currently, this can be either be "MacOSX", "GTTV" or "Linux".
 *
 *  Remarks:
 *      the return value of this function can be used to make runtime changes
 *      to a game's behaviour.  Other platform names may be added in the future.
 */
LPCSTR WINAPI TGGetOS();


/* TGGetSystemInfo()    (NTDLL)
 *
 *  retrieves some information about the underlying platform.  This includes the
 *  platform version, bit count (ie: 32 or 64 bit), and some distribution specific
 *  information.
 *
 *  Parameters:
 *      info [out]: pointer to a struct to be filled in with the platform information.
 *                  The struct must have its <version> member set to the value
 *                  TGAPI_PLATFORMINFO_VERSION before calling this function.
 *
 *  Returns:
 *      TRUE if the platform information was successfully retrieved, and FALSE
 *      otherwise.
 *
 *  Remarks:
 *      if the function fails, the contents of the <info> struct are undefined.
 */
BOOL WINAPI TGGetSystemInfo(TGAPI_PlatformInfo *info);


/* TGGetVersion()       (NTDLL)
 *
 *  retrieves a string indicating the current build number of Cider.
 *
 *  Parameters:
 *      pBuf [out]: pointer to a buffer to store the build number in.
 *      BufLen [in]: size, in characters, of the buffer <pBuf>.
 *
 *  Returns:
 *      TRUE if the build number was successfully retrieved, and FALSE if it could
 *      not be retrieved or is unsupported on the platform.
 *
 *  Remarks:
 *      This call is only valid when running under Cider on MacOSX.  If the function
 *      fails, the contents of the buffer are undefined.
 *
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGGetVersion(LPSTR  pBuf,
                         SIZE_T BufLen);


/* TGMACOSGetVersion() (NTDLL)
 *
 *  retrieves the current version of Mac OS X.
 *
 *  Parameters:
 *      major_version [out]: receives the major version number (ie: 10).
 *      minor_version [out]: receives the minor version number.
 *      extra [out]: receives extra version information.
 *
 *  Returns:
 *      TRUE if the version information was successfully retrieved.  FALSE if the
 *      version number could not be retrieved or the function is unsupported on
 *      the platform.
 *
 *  Remarks:
 *      this function can be used to retrieve the current version of Mac OS X that
 *      the game is running on.  For example, when running on a Snow Leopard system,
 *      this function may return 10 for <major_version>, 6 for <minor_version>, and
 *      4 for <extra>.  If the function fails, the contents of the buffer are
 *      undefined.
 *
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGMACOSGetVersion(DWORD *major_version,
                              DWORD *minor_version,
                              DWORD *extra);


/* TGMACOSGetBundleIdentifier() (NTDLL)
 *
 *  Retrieves the bundle identifier (e.g. com.transgaming.<game name>).
 *
 *  Parameters:
 *      pOutBuffer [out]: buffer to receive the bundle identifier.
 *      nBufferLength [in]: length, in characters, of the output buffer <pOutBuffer>.
 *
 *  Returns:
 *      TRUE if the bundle identifier was successfully retrieved. FALSE if the
 *      bundle identifier could not be retrieved or the function is unsupported on
 *      the platform.
 *
 *  Remarks:
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGMACOSGetBundleIdentifier(LPSTR pOutBuffer,
                                       DWORD nBufferLength);


/* TGMACOSGetBundleVersion() (NTDLL)
 *
 *  Retrieves the bundle's version number string, a.k.a. CFBundleShortVersionString
 *
 *  Parameters:
 *      pOutBuffer [out]: buffer to receive the bundle version number.
 *      nBufferLength [in]: length, in characters, of the output buffer <pOutBuffer>.
 *
 *  Returns:
 *      TRUE if the bundle version number was successfully retrieved. FALSE if the
 *      bundle version number could not be retrieved or the function is unsupported on
 *      the platform.
 *
 *  Remarks:
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGMACOSGetBundleVersion(LPSTR pOutBuffer,
                                    DWORD nBufferLength);


/* TGGetNativePath() (NTDLL)
 *
 *  converts a windows path to a unix path.
 *
 *  Parameters:
 *      pDosPath [in]: windows path name to be converted.
 *      pOutBuffer [out]: buffer to receive the converted path name.
 *      nBufferLength [in]: length, in characters, of the output buffer <pOutBuffer>.
 *
 *  Returns:
 *      TRUE if the version information was successfully retrieved.  FALSE if the
 *      version number could not be retrieved or the function is unsupported on
 *      the platform.
 *
 *  Remarks:
 *      Some games may require direct access to a unix file name.  Though this is
 *      not recommended, it may be done by converting a windows path to a unix path.
 *      If the function fails, the contents of the buffer are undefined.
 */
BOOL WINAPI TGGetNativePath(LPCSTR pDosPath,
                            LPSTR  pOutBuffer,
                            DWORD  nBufferLength);


/* TGUseSysInfoTool() (NTDLL)
 *
 *  checks if Cider's system info tool is present in the package.
 *
 *  Parameters:
 *      none.
 *
 *  Returns:
 *      TRUE if the system info tool is present in the game package.  FALSE if the
 *      system info tool is not present or the call is unsupported on the platform.
 *
 *  Remarks:
 *      The system info tool is used to gather information from the user's system
 *      to send along with a crash report.  The tool is completely self contained
 *      and will submit information at the user's request and approval.
 *
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGUseSysInfoTool();


/* TGLaunchSysInfoTool() (NTDLL)
 *
 *  launches Cider's system info tool.
 *
 *  Parameters:
 *      logName [in]: windows path identifying the location of a game log file.
 *          This parameter may be NULL.
 *      crashReport [in]: windows path identifying the location of the generated
 *          crash report file.  This parameter may be NULL.
 *      vmmapName [in]: windows path identifying the location of VM map file to
 *          generate.  This file will be created internally, it just needs a name.
 *          This parameter may be NULL.
 *
 *  Returns:
 *      no return value.
 *
 *  Remarks:
 *      This launches the Cider system info tool and allows it to gather system
 *      information to be presented to the user.  The user is responsible for
 *      approving the crash report before it is sent to TransGaming.  The user
 *      may cancel at any time without the system information having been stored
 *      or sent anywhere.
 *
 *      This call is only supported on Apple.
 */
void WINAPI TGLaunchSysInfoTool(LPCSTR logName,
                                LPCSTR crashReport,
                                LPCSTR vmmapName);


/* TGDumpExceptionInfo()    (NTDLL)
 *
 *  generates Cider's standard crash report files.
 *
 *  Parameters:
 *      epointers [in]: an EXCEPTION_POINTERS structure containing the exception
 *          record and context record for the exception that occurred.  This block
 *          is delivered to an exception filter function or can be built up
 *          manually using GetThreadContext().
 *      crashReportName [in]: filename and location to write the crash report to.
 *          The crash report is in plain text and includes the exception information,
 *          module listing, ID of the crashing thread, and as much of a call stack
 *          as can be retrieved.  This parameter may be NULL if the crash report
 *          file is not needed.
 *      minidumpName [in]: filename and location to write the minidump to.  This is
 *          a standard minidump that can be opened using the windbg tool on windows.
 *          By default it will include all information in the process, module, thread,
 *          callstack of the crashing thread, processor, platform, and some memory
 *          streams.  A change to the game's config file can also force it to do full
 *          process memory dumps as well.  This parameter may be NULL if the minidump
 *          file is not needed.
 *
 *  Returns:
 *      no return value.
 *
 *  Remarks:
 *      This function exposes Cider's standard crash report functionality.  By default
 *      this function will be called when ntdll catches an unhandled exception and no
 *      user defined unhandled exception filter is installed (ie: by using the
 *      SetUnhandledExceptionFilter() function).  In most cases it is not necessary to
 *      call this function.  However, if a game wants to generate its own crash report
 *      through another means or would also like to generate the standard Cider crash
 *      report files alongside of its crash report, this function can be called to
 *      provide that functionality.
 *
 *      This can be called at any time an EXCEPTION_POINTERS structure is available,
 *      but it is discouraged unless the game is intending on terminating itself
 *      immediately after handling the exception.  That said, this can actually be
 *      a useful [debug-time] debugging tool to get stack snapshots when certain
 *      exceptions occur (ie: by calling it in a filter function).
 */
void WINAPI TGDumpExceptionInfo(EXCEPTION_POINTERS *epointers,
                                LPCSTR              crashReportName,
                                LPCSTR              minidumpName);


/* prototype definitions for the functions in this header */
typedef BOOL    (WINAPI *TYPEOF(IsTransgaming))();
typedef LPCSTR  (WINAPI *TYPEOF(TGGetOS))();
typedef BOOL    (WINAPI *TYPEOF(TGGetSystemInfo))(LPSTR, SIZE_T, DWORD *, DWORD *, LPSTR, SIZE_T, LPSTR, SIZE_T, DWORD *);
typedef BOOL    (WINAPI *TYPEOF(TGGetVersion))(LPSTR, SIZE_T);
typedef BOOL    (WINAPI *TYPEOF(TGMACOSGetVersion))(DWORD *, DWORD *, DWORD *);
typedef BOOL    (WINAPI *TYPEOF(TGMACOSGetBundleIdentifier))(LPSTR, DWORD);
typedef BOOL    (WINAPI *TYPEOF(TGMACOSGetBundleVersion))(LPSTR, DWORD);
typedef BOOL    (WINAPI *TYPEOF(TGGetNativePath))(LPCSTR, LPSTR, DWORD);
typedef BOOL    (WINAPI *TYPEOF(TGUseSysInfoTool))();
typedef void    (WINAPI *TYPEOF(TGLaunchSysInfoTool))(LPCSTR, LPCSTR, LPCSTR);
typedef void    (WINAPI *TYPEOF(TGDumpExceptionInfo))(EXCEPTION_POINTERS *, LPCSTR, LPCSTR);


# ifdef __cplusplus
}
# endif
#endif
