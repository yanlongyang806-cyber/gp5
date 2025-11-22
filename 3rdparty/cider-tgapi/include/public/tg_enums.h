/*
 * public TransGaming API function definitions
 *
 * Copyright 2010-2012 TransGaming Inc.
 */
#ifndef  __TG_ENUMS_H__
# define __TG_ENUMS_H__


# ifndef TYPEOF
#  define TYPEOF(name)   name##_Func
# endif


/* enum INSTALLROOT_STATUS: result codes for the TGInstallAsRoot() function. */
typedef enum
{
    /* the launch succeeded */
    INSTALLROOT_SUCCESS,

    /* passed a bad argument to the function (NULL command name or argument list) */
    INSTALLROOT_ERROR_BADARG,

    /* failed to allocate memory for the arguments */
    INSTALLROOT_ERROR_MALLOC,

    /* failed to create the authentication context (permission problem?) */
    INSTALLROOT_ERROR_AUTHCREATE,

    /* failed to copy authentication rights */
    INSTALLROOT_ERROR_AUTHCOPY,

    /* authorization failed (ie: cancelled, wrong password, etc) */
    INSTALLROOT_ERROR_AUTHEXEC,

    /* call not supported on this platform */
    INSTALLROOT_ERROR_UNSUPPORTED,
} INSTALLROOT_STATUS;


/* enum TG_ACTIVATION_STATE: state values passed in to a TGNOTIFY_ACTIVATION
     callback through the <state> parameter.  This is used to identify what
     type of activation event occurred. */
typedef enum
{
    /* no event occurred.  This will never be passed to the callback function
       and is just intended to be a placeholder for the value 0. */
    TGAS_NONE,

    /* the app gained user input focus */
    TGAS_GAINFOCUS,

    /* the app lost user input focus */
    TGAS_LOSEFOCUS,

    /* the app was minimized to the dock.  This can occur either by user
       action or programmatically. */
    TGAS_MINIMIZE,

    /* the game was toggled to windowed mode.  This can occur either by user
       action or programmatically. */
    TGAS_TOGGLE_WINDOWED,

    /* the game was toggled to fullscreen mode.  This can occur either by user
       action or programmatically. */
    TGAS_TOGGLE_FULLSCREEN,

    /* the app was restored from being minimized.  This can only occur either
       by user action or programmatically. */
    TGAS_RESTORE,
} TG_ACTIVATION_STATE;


/* enum TG_NOTIFY_TYPE: available types of notification callbacks.  The
     state values associated with each of these callbacks are defined
     in the enums above. */
typedef enum
{
    /* invalid notification type.  This is added to force this type to be signed. */
    TGNOTIFY_INVALID = -1,

    /* the callback should be delivering activation events.  The state
       values associated with this callback type are found in the enum
       TG_ACTIVATION_STATE. */
    TGNOTIFY_ACTIVATION = 0,

    TGNOTIFY_COUNT,
} TG_NOTIFY_TYPE;


/* enum TG_PLAYER_STATE: result codes for GetPlayerState() function. */
typedef enum
{
    TGPL_STATE_UNKNOWN = -1,

    TGPL_STATE_READY = 0,

    TGPL_STATE_PLAYING,

    TGPL_STATE_COMPLETE,
} TG_PLAYER_STATE;


/* enum TRACKPAD_ScrollMode: scroll event mode names. */
typedef enum
{
    /* deliver only the standard WM_MOUSEWHEEL messages from scroll events. */
    TRACKPAD_SCROLL_DEFAULT,

    /* deliver only the TG specific raw scroll events through the WM_RAWHSCROLL and WM_RAWVSCROLL
       messages. */
    TRACKPAD_SCROLL_RAW,

    /* deliver both the standard mouse wheel messages and TG specific raw scroll messages. */
    TRACKPAD_SCROLL_BOTH,
} TRACKPAD_ScrollMode;

/* enum TRACKPAD_ScrollFlags: flags for use in the trackpad API functions. */
typedef enum
{
    /* no special behavioural flags */
    TRACKPAD_SCROLL_FLAG_NONE =                 0x00,

    /* when delivering raw scroll events, flip events that arrive already flipped by the user's
       'natural scrolling' system preference.  This will restore the usual behaviour for scroll
       events where a scroll upward results in a positive scroll amount.  This will only affect
       the scroll amounts for the raw scroll events.  Standard mouse wheel messages are not
       affected. */
    TRACKPAD_SCROLL_FLAG_FLIP_NATURAL =         0x01,

    /* when processing raw scroll events, drop any events that arrive from devices that are
       marked as 'imprecise'.  This will prevent standard wheel mice from generating raw scroll
       events.  Trackpads (external or builtin), touch sensitive mice, and and some mice with
       scroll balls (instead of wheels) will all be considered 'precise'.  This feature is only
       reliable on Mac OS 10.7+.  If running on 10.6 or earlier, the categorization of the event
       as precise or imprecise will be a guess at best (though in testing it seems to be about
       95% accurate on 10.6.8 at least). */
    TRACKPAD_SCROLL_FLAG_DROP_IMPRECISE =       0x02,

    /* when processing raw scroll events, ignore events that come from imprecise devices and send
       them as standard mouse wheel events instead.  This will allow the 'raw' mode to be set for
       the trackpad, but still be able to receive standard mouse wheel events from standard wheel
       mice.  If in raw mode and a scroll event comes from a precise device (ie: a trackpad or
       magic mouse), it will still be sent as a raw scroll event and the corresponding standard
       wheel event will not be sent.  If in 'both' mode, this flag will behave the same as if the
       TRACKPAD_SCROLL_FLAG_DROP_IMPRECISE flag had been set.  This flag is useful if an app wants
       to receive raw scroll events only from precise devices but still maintain standard scroll
       behaviour on non-precise devices (without having to check for the device's type).  Note
       that this flag will override TRACKPAD_SCROLL_FLAG_DROP_IMPRECISE when in raw mode. */
    TRACKPAD_SCROLL_FLAG_IGNORE_RAW_IMPRECISE = 0x04,

    /* when processing raw scroll events, send them as magnify events instead if the CMD key is
       held down.  This is meant to add magnify support on devices that do not support the pinch
       gesture (ie: the magic mouse).  The raw scroll amount will be converted to a magnification
       factor based on the current scaling mode for the magnification events.  This scaling mode
       only applies when a scroll event is converted to magnify event, not for all magnify events.
       It is recommended that the TRACKPAD_SCROLL_FLAG_IGNORE_RAW_IMPRECISE also be used when this
       flag is used so that the key combo does not also affect events from imprecise devices.
       Only raw vertical scroll events will be converted to magnify events.  Any horizontal scroll
       events will be dropped as long as the CMD key is being held down. */
    TRACKPAD_SCROLL_FLAG_SEND_MAGNIFY_ON_CMD =  0x08,

    /* this flag offers the same behaviour as the TRACKPAD_SCROLL_FLAG_SEND_MAGNIFY_ON_CMD flag
       except that the scroll events are converted when the OPTION key is held down.  All of the
       same restrictions and recommendations apply.  This flag may be used in combination with
       the TRACKPAD_SCROLL_FLAG_SEND_MAGNIFY_ON_CMD flag, though that will allow both OPTION and
       CMD to act as shortcut keys simultaneously. */
    TRACKPAD_SCROLL_FLAG_SEND_MAGNIFY_ON_OPT =  0x10,
} TRACKPAD_ScrollFlags;

#endif
