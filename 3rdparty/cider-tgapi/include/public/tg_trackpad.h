/*
 *  File: trackpad messaging extensions ('trackpad.h')
 *  Copyright 2010-2012 TransGaming Inc.
 *  Date: August 2, 2010
 *
 *  this provides an interface for the windows messaging extensions offered through the Cider framework.
 *  There are four trackpad mouse gestures that are commonly supported on recent Macbook hardware.  The
 *  supported gestures are:
 *      1. two-finger pinch - generates a magnification event.
 *      2. two-finger rotate - generates a rotation event.
 *      3. two-finger swipe - generates a scroll event.
 *      4. three-finger swipe - generates a next-page type event.
 * 
 *  This functionality is available on all Macbook and Macbook Pro computers after the Macbook Air.  This
 *  also requires Mac OS X 10.5.2 (Leopard) or higher to function.  These events should not be depended
 *  on as core input requirements as they may not be available on all hardware that the application can
 *  run on (ie: a MacPro will not generate the events, nor will a Macbook when using an external mouse).
 *  These events should only be offered as supplementary input events for the application.
 *
 *  This provides all the definitions and macros needed to receive and process the messages related to
 *  the common trackpad gesture events.
 *
 */
#ifndef  __TRACKPAD_H__
# define __TRACKPAD_H__

# if defined(__WINE__) || defined(__GNUC__)
#  include "winbase.h"
#  include "winuser.h"
# else
#  ifndef _WIN32_WINNT
#   define _WIN32_WINNT 0x0501
#  endif
#  include <windows.h>
# endif
# include "tg_enums.h"

# ifdef __cplusplus
extern "C" {
# endif


/* SendInput() extensions: these extra flags are extensions to the SendInput() API.  They allow trackpad
     events to be injected by the application using SendInput().  These values should not conflict with
     those already defined in the platform SDK. */
# define MOUSEEVENTF_MAGNIFY    0x10000000
# define MOUSEEVENTF_ROTATE     0x20000000
# define MOUSEEVENTF_SWIPE      0x40000000
# define MOUSEEVENTF_HSCROLL    0x80000000
# define MOUSEEVENTF_VSCROLL    0x08000000


/* messaging extensions: the trackpad events are received through the standard windows messaging mechanism.
     The messages will be sent to a window's window procedure when the event is received.  The following
     new message names have been defined to handle these new events:
        WM_MAGNIFY: this will be received when a trackpad two-finger pinch gesture event occurs.
            wParam: (DWORD) the magnification scaling factor expressed as a normalized DWORD value.  The
                    actual scaling factor is a floating point value that is encoded in the message's WPARAM
                    value by multiplying it by MAGNIFY_SCALE.  The value can be converted back to a floating
                    point scale factor using the GET_MAGNIFY_FACTOR_WPARAM() macro.  The scaling factor will
                    be relative to 1.0f (ie: > 1.0f for a magnification, and < 1.0f for a minification).
            lParam: the current cursor position packed into a single DWORD.  The low order word will be the
                    cursor's current X position, and the high order word will be the cursor's current Y
                    position.

        WM_ROTATE: this will be received when a trackpad two-finger rotate gesture event occurs.
            wParam: (DWORD) the rotation angle expressed as a normalized DWORD value.  The actual rotation
                    angle is a floating point value that is encoded in the message's WPARAM value by multiplying
                    it by ROTATE_SCALE.  The value can be converted back to a floating point angle using the
                    GET_ROTATE_ANGLE_WPARAM() macro.  The angle is expressed in degrees clockwise.
            lParam: the current cursor position packed into a single DWORD.  The low order word will be the
                    cursor's current X position, and the high order word will be the cursor's current Y
                    position.

        WM_SWIPE: this will be received when a trackpad three-finger swipe gesture event occurs.
            wParam: the swipe directions amounts packed into a single DWORD.  The low order word will be the
                    swipe amount in the X direction (-1 for a swipe right, 1 for a swipe left).  The high order
                    word will be the swipe amount in the Y direction (-1 for a swipe down, 1 for a swipe up).
                    These values can be retrieved by using the GET_SWIPE_AMOUNT_*_WPARAM() macros.
            lParam: the current cursor position packed into a single DWORD.  The low order word will be the
                    cursor's current X position, and the high order word will be the cursor's current Y
                    position.

        WM_RAWHSCROLL: this will be received when a trackpad two-finger scroll gesture event occurs in the 
                    horizontal direction.
            wParam: (DWORD) the raw horizontal scroll value expressed as a normalized DWORD value.  The
                    actual scroll amount is a floating point value that is encoded in the message's WPARAM
                    value by multipling it by SCROLL_SCALE.  The value can be converted back to a floating
                    point value using the GET_SCROLL_WPARAM() macro.  The scroll value will be negative for
                    a left scroll and positive for a right scroll.
            lParam: the current cursor position packed into a single DWORD.  The low order word will be the
                    cursor's current X position, and the high order word will be the cursor's current Y
                    position.

        WM_RAWVSCROLL: this will be received when a trackpad two-finger scroll gesture event occurs in the 
                    vertical direction.
            wParam: (DWORD) the raw vertical scroll value expressed as a normalized DWORD value.  The
                    actual scroll amount is a floating point value that is encoded in the message's WPARAM
                    value by multipling it by SCROLL_SCALE.  The value can be converted back to a floating
                    point value using the GET_SCROLL_WPARAM() macro.  The scroll value will be negative for
                    a downward scroll and positive for an upward scroll.
            lParam: the current cursor position packed into a single DWORD.  The low order word will be the
                    cursor's current X position, and the high order word will be the cursor's current Y
                    position.
*/
/* helper macros */
/* magnification scaling value.  Since the magnification value needs to be passed as a DWORD value, it is
   multiplied by MAGNIFY_SCALE before being stored in the message's WPARAM value.  The value can be converted
   back to a floating point scaling factor by using the GET_MAGNIFY_FACTOR_WPARAM() macro. */
# define MAGNIFY_SCALE  (16777216.0f)
# define GET_MAGNIFY_FACTOR_WPARAM(wParam)      ((float)((((signed int)wParam)) / MAGNIFY_SCALE))

/* rotation angle value.  Since the rotation angle value needs to be passed as a DWORD value, it is multiplied
   by ROTATE_SCALE before being stored in the message's WPARAM value.  The value can be converted back to a
   floating point angle by using the GET_ROTATE_ANGLE_WPARAM() macro. */
# define ROTATE_SCALE   (16777216.0f)
# define GET_ROTATE_ANGLE_WPARAM(wParam)        ((float)(((signed int)(wParam)) / ROTATE_SCALE))

/* scroll scaling value.  Since the scroll value needs to be passed as a DWORD value, it is multiplied by
   SCROLL_SCALE before being stored in the message's WPARAM value.  The value can be converted back to a
   floating point scroll amount by using the GET_SCROLL_WPARAM() macro. */
# define SCROLL_SCALE   (16777216.0f)
# define GET_SCROLL_WPARAM(wParam)              ((float)(((signed int)(wParam)) / SCROLL_SCALE))

/* swipe amount values.  The swipe amount values are stored in a packed DWORD value in WPARAM.  The low order
   word stores the swipe amount in the X direction (-1 for a swipe right, 1 for a swipe left).  The high order
   word stores the swipe amount in the Y direction (-1 for a swipe down, 1 for a swipt up).  These values can
   be retrieved from the WPARAM value using the GET_SWIPE_AMOUNT_*_WPARAM() macros. */
# define GET_SWIPE_AMOUNT_X_WPARAM(wParam)      ((int)(signed short)LOWORD(wParam))
# define GET_SWIPE_AMOUNT_Y_WPARAM(wParam)      ((int)(signed short)HIWORD(wParam))


/* define this symbol if the windows messaging functionality is not required.  This is mostly to prevent
   a compiler warning for files that include this header but do not need to call TRACKPAD_Init() (ie: 
   "TRACKPAD_Init() defined but not used"). */
# ifndef TRACKPAD_NO_MESSAGING

/* message values */
static UINT WM_MAGNIFY = 0;
static UINT WM_ROTATE = 0;
static UINT WM_SWIPE = 0;
static UINT WM_RAWHSCROLL = 0;
static UINT WM_RAWVSCROLL = 0;

static UINT WM_TRACKPAD_FIRST = 0;
static UINT WM_TRACKPAD_LAST = 0;

/* TRACKPAD_Init(): initializes the trackpad messaging extension message values.  This function must be
     called at least once in each compile unit that includes this file before the message names can be
     used properly.  Calling the function multiple times has no effect.  Returns TRUE if all the message
     names were successfully registered and FALSE otherwise. */
static BOOL TRACKPAD_Init()
{
    /* already initialized => ignore */
    if (WM_MAGNIFY != 0 && WM_ROTATE != 0 && WM_SWIPE != 0 && WM_RAWHSCROLL != 0 && WM_RAWVSCROLL != 0)
        return TRUE;
    
    /* register the message names */
    WM_MAGNIFY =    RegisterWindowMessageA("TG_TRACKPAD_MAGNIFY");
    WM_ROTATE =     RegisterWindowMessageA("TG_TRACKPAD_ROTATE");
    WM_SWIPE =      RegisterWindowMessageA("TG_TRACKPAD_SWIPE");
    WM_RAWHSCROLL = RegisterWindowMessageA("TG_TRACKPAD_RAWHSCROLL");
    WM_RAWVSCROLL = RegisterWindowMessageA("TG_TRACKPAD_RAWVSCROLL");

    /* keep track of the first and last of the messages for range checking */
    WM_TRACKPAD_FIRST = min(WM_MAGNIFY, min(WM_ROTATE, min(WM_SWIPE, min(WM_RAWHSCROLL, WM_RAWVSCROLL))));
    WM_TRACKPAD_LAST =  max(WM_MAGNIFY, max(WM_ROTATE, max(WM_SWIPE, max(WM_RAWHSCROLL, WM_RAWVSCROLL))));
    
    return (WM_MAGNIFY != 0) && (WM_ROTATE != 0) && (WM_SWIPE != 0) &&
           (WM_RAWHSCROLL != 0) && (WM_RAWVSCROLL != 0);
}

# endif  /* !TRACKPAD_NO_MESSAGING */

/* TGTrackpadSetScrollMode() (USER32)
 *
 *  sets the current trackpad scroll event mode.
 *
 *  Parameters:
 *      mode [in]: the new scroll event mode to set.
 *      flags [in]: flags that determine the behaviour of the raw scroll events.  Valid flags are
 *          defined in the TRACKPAD_ScrollFlags enum.
 *
 *  Returns:
 *      The previous scroll event mode.
 *
 *  Remarks:
 *      The current scroll mode determines how mouse scroll events will be sent to the application.
 *      The standard mouse wheel events get their information from the same source as the scroll
 *      events, so they could confuse an application if both sets of messages are processed in
 *      different ways.  Note that the WM_RAW*SCROLL events will also be sent for scroll events from
 *      a standard mouse and not just from a trackpad.  The default mode will only deliver the
 *      standard mouse wheel messages (ie: TRACKPAD_SCROLL_DEFAULT).
 */
TRACKPAD_ScrollMode WINAPI TGTrackpadSetScrollMode(TRACKPAD_ScrollMode mode, DWORD flags);

/* TGTrackpadGetScrollMode() (USER32)
 *
 *  retrieves the current scroll mode and flags.
 *
 *  Parameters:
 *      currentFlags [out]: receives the current raw scroll mode flags.  This parameter may be
 *          NULL if the current flags are not needed.
 *
 *  Returns:
 *      The current scroll mode and associated flags.
 *
 *  Remarks:
 *      This retrieves the current scroll mode and flags (if needed).  The flags return may be
 *      ignored by passing NULL for the <currentFlags> parameter.
 */
TRACKPAD_ScrollMode WINAPI TGTrackpadGetScrollMode(DWORD *currentFlags);

/* TGTrackpadSetMagnifyConversionScale() (USER32)
 *
 *  sets the current scroll-to-magnify conversion scale factor.
 *
 *  Parameters:
 *      scale [in]: the new scale factor to apply to converted magnify events.  This parameter
 *          should be 1.0 to leave the magnification scaling at default.  A value larger than
 *          1.0 will lead to 'faster' magnification, while a value smaller than 1.0 will lead
 *          to 'slower' magnification.
 *
 *  Returns:
 *      No return value.
 *
 *  Remarks:
 *      This sets the new scroll-to-magnify event conversion factor.  This scaling factor will
 *      only apply to scroll events that are being converted to magnify events, not all magnify
 *      events.  This mode must be enabled using either TRACKPAD_SCROLL_FLAG_SEND_MAGNIFY_ON_CMD
 *      or TRACKPAD_SCROLL_FLAG_SEND_MAGNIFY_ON_OPT in the <flags> parameter in a previous call
 *      to the TGTrackpadSetScrollMode() function.  This scaling factor may be set at any time,
 *      even if the TRACKPAD_SCROLL_FLAG_SEND_MAGNIFY_ON_* flags are not being used.  The default
 *      scaling factor is 1.0.  The scale factor should not be any larger than 2.0 and should
 *      never be negative.
 */
void WINAPI TGTrackpadSetMagnifyConversionScale(FLOAT scale);

/* TGTrackpadGetMagnifyConversionScale() (USER32)
 *
 *  retrieves the current scroll-to-magnify conversion scale factor.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      The current scaling factor value being used to convert scroll events to magnify events.
 *
 *  Remarks:
 *      This retrieves the current scroll-to-magnify scaling factor being used to convert scroll
 *      events to magnification events.  This scaling factormay be retrieved at any time even if
 *      the TRACKPAD_SCROLL_FLAG_SEND_MAGNIFY_ON_* flags are not currently being used.  The
 *      default value is 1.0.
 */
FLOAT WINAPI TGTrackpadGetMagnifyConversionScale();

/* prototype definitions for the functions in this header */
typedef TRACKPAD_ScrollMode (WINAPI *TYPEOF(TGTrackpadSetScrollMode))(TRACKPAD_ScrollMode mode, DWORD flags);
typedef TRACKPAD_ScrollMode (WINAPI *TYPEOF(TGTrackpadGetScrollMode))(DWORD *currentFlags);
typedef void                (WINAPI *TYPEOF(TGTrackpadSetMagnifyConversionScale))(FLOAT scale);
typedef FLOAT               (WINAPI *TYPEOF(TGTrackpadGetMagnifyConversionScale))();

# ifdef __cplusplus
}
# endif
#endif
