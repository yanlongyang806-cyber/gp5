/*
 * public TransGaming API function definitions
 *
 * Copyright 2010 TransGaming Inc.
 */
#ifndef  __TG_KBD_H__
# define __TG_KBD_H__

# ifdef __cplusplus
extern "C" {
# endif

/* This enum represents a button attribute index */
typedef enum
{
    GTTV_DEAD_INDEX = 0,   /* 'dead key' button attribute index */
    GTTV_LATCH_INDEX = 1,  /* 'latch key' button attribute index */
    GTTV_TOGGLE_INDEX = 2, /* 'toggle key' button attribute index */
    GTTV_STOP_INDEX = 3,   /* 'stop key' button attribute index */

    GTTV_ATTR_FLAG_ALL = 0x8000, /* 'affect all buttons' flag */
} GTTV_ATTRIBUTE_INDEX;

/* This enum represents a character filtering category */
typedef enum
{
    GTTV_FILTER_CUSTOM =            0x00,   /* Filter requested characters (case insensitive)
                                               and symbols only */
    GTTV_FILTER_ALL_CHARACTERS =    0x01,   /* Filter all characters.  Note that this implies
                                               GTTV_FILTER_ALL_POPUPS as well. */
    GTTV_FILTER_ALL_SYMBOLS =       0x02,   /* Filter all symbols */
    GTTV_FILTER_ALL_POPUPS =        0x04,   /* Filter all accented characters on popups */
} GTTV_FILTER_TYPE;


/* TGGTTVKbdGetDimensions()    (USER32)
 *
 *  gets the dimensions of the virtual keyboard.
 *
 *  Parameters:
 *      width [out]: current virtual keyboard width.
 *      height [out]: current virtual keyboard height.
 *
 *  Returns:
 *      no return value.
 *
 *  Remarks:
 *      This call is only supported on GTTV.
 */
void WINAPI TGGTTVKbdGetDimensions(UINT *width,
                                   UINT *height);


/* TGGTTVKbdSetPos()    (USER32)
 *
 *  sets the position of the top left corner of the virtual keyboard on the window.
 *
 *  Parameters:
 *      x [in]: specifies a number of pixels from left edge of the window.
 *      y [in]: specifies a number of pixels from the top of the window.
 *
 *  Returns:
 *      no return value.
 *
 *  Remarks:
 *      Regardless of the requested location, the final position of the keyboard is
 *      bounded such that it remains entirely within the window.
 *
 *      This call is only supported on GTTV.
 */
void WINAPI TGGTTVKbdSetPos(LONG x,
                            LONG y);


/* TGGTTVKbdSetPosFloat()    (USER32)
 *
 *  set the position of the center of the keyboard on the current windows,
 *  given the position as fractions of the screen width and height.
 *
 *  Parameters:
 *      x_fraction [in]: scales the distance from the left edge of the window.
 *      y_fraction [in]: scales the distance from the top edge of the window.
 *
 *  Returns:
 *      no return value.
 *
 *  Remarks:
 *      Parameter values should be greater than or equal to 0.0 and less than 1.0
 *      other values just force the image against an edge.
 *
 *      If the set position is so that the keyboard is out of bound, it will be
 *      clamped to the edges of the screen.
 *
 *      This call is only supported on GTTV.
 */
void WINAPI TGGTTVKbdSetPosFloat(FLOAT x_fraction,
                                 FLOAT y_fraction);


/* TGGTTVKbdShow()      (USER32)
 *
 *  activates or deactivates the virtual keyboard.  While active, the virtual keyboard
 *  displays an internally defined cursor.
 *
 *  Parameters:
 *      state [in]: TRUE to show the virtual keyboard, FALSE to hide it.
 *      kbdCloseMsg [in]: Windows message to be posted to all top-level windows when the
                          virtual keyboard is dismissed. Any value < WM_USER will be ignored.
 *
 *  Returns:
 *      no return value.
 *
 *  Remarks:
 *      This call is only supported on GTTV.
 */
void WINAPI TGGTTVKbdShow(BOOL state, UINT kbdCloseMsg);

/* TGGTTVKbdSetCharacterFilter()      (USER32)
 *
 * Sets a keyboard character filter.
 *
 *  Parameters:
 *      string [in]: The filter string.  This string can be NULL or empty.  The contents
 *          of this string will be overridden by the current default filter string (set
 *          using the TGGTTVKbdSetDefaultCharacterFilter() function).
 *      flags [in]: The filter flags.  This can either be GTTV_FILTER_CUSTOM (filter only
 *          the requested characters in the string <string>), or it can be a combination
 *          of one or more of the following:
 *              GTTV_FILTER_ALL_CHARACTERS: Filter all characters.  This also implicitly
 *                                          disables all accented characters on popups.
 *              GTTV_FILTER_ALL_SYMBOLS: Filter all symbols.
 *              GTTV_FILTER_ALL_POPUPS: Filter all accented characters on popups.
 *
 *  Returns:
 *      no return value.
 *
 *  Remarks:
 *      If GTTV_FILTER_CUSTOM is used with a NULL string, any current keyboard filters
 *      gets cleared.  The default filter string will still remain active however.  If
 *      one of the GTTV_FILTER_ALL_* flags is to be used without a filter string, NULL
 *      must be passed for the <string> parameter.
 *
 *      This call is only supported on GTTV.
 */
void WINAPI TGGTTVKbdSetCharacterFilter(const WCHAR * string, UINT flags);

/* TGGTTVKbdSetDefaultCharacterFilter()      (USER32)
 *
 * Sets the default keyboard character filter.
 *
 *  Parameters:
 *      string [in]: The filter string.  This string can be NULL or empty.  The contents
 *          of this string will be overridden by the current default filter string (set
 *          using the TGGTTVKbdSetDefaultCharacterFilter() function).
 *      flags [in]: The filter flags.  This can either be GTTV_FILTER_CUSTOM (filter only
 *          the requested characters in the string <string>), or it can be a combination
 *          of one or more of the following:
 *              GTTV_FILTER_ALL_CHARACTERS: Filter all characters.  This also implicitly
 *                                          disables all accented characters on popups.
 *              GTTV_FILTER_ALL_SYMBOLS: Filter all symbols.
 *              GTTV_FILTER_ALL_POPUPS: Filter all accented characters on popups.
 *
 *  Returns:
 *      no return value.
 *
 *  Remarks:
 *      This function sets the default character filter string for the keyboard.   This
 *      filter string will always be set for the keyboard even if another filter string
 *      is set using TGGTTVKbdSetCharacterFilter() that doesn't include the characters
 *      in this filter string.  The default filter string and flags will be set each
 *      time the keyboard is opened, and each time a new filter string is set (using
 *      the TGGTTVKbdSetCharacterFilter() function).
 *
 *      To clear the default filter string, NULL should be passed for <string> and
 *      GTTV_FILTER_CUSTOM for <flags>.  If one of the GTTV_FILTER_ALL_* flags is to
 *      be used without a filter string, NULL must be passed for the <string> parameter.
 *
 *      By default, the default filter string is empty and the default flags are set
 *      to GTTV_FILTER_CUSTOM.  This indicates no special default filter.  Note that
 *      setting the default string will clear any current filter string that may be
 *      set.  The current filter string must be set again each time the default filter
 *      string changes.
 *
 *      This call is only supported on GTTV.
 */
void WINAPI TGGTTVKbdSetDefaultCharacterFilter(const WCHAR * string, UINT flags);

/* TGGTTVKbdGetKeyAttribute()      (USER32)
 *
 *  Get a virtual keyboard button attribute.
 *
 *  Parameters:
 *      VKey [in]: Windows Virtual-Key code
 *      index [in]: the attribute index value being queried
 *          GTTV_DEAD_INDEX : checks whether the requested button has the 'dead key'
 *              attribute set for it.  A dead key will act similarly to a latch key
 *              but will modify the resulting character differently.  This attribute
 *              is not currently used and may be deprecated shortly.
 *          GTTV_LATCH_INDEX : checks whether the requested button has the 'latch key'
 *              attribute set for it.  A latch key will remain pressed until the next
 *              non-latch key is pressed or the same latch key is pressed again.
 *          GTTV_TOGGLE_INDEX : checks whether the requested button has the 'toggle
 *              key' attribute set for it.  A toggle key will remain pressed until the
 *              user clicks on it again.
 *          GTTV_STOP_INDEX : checks whether the requested button has the 'stop key'
 *              attribute set for it.  A stop button will close the virtual keyboard
 *              when pressed and enabled.
 *
 *  Returns:
 *      TRUE if attribute is set, FALSE if not.
 *
 *  Remarks:
 *      VK_OEM_CLEAR is the designated key for the "Exit" button.
 *
 *      This call is only supported on GTTV.
 */
BOOL WINAPI TGGTTVKbdGetKeyAttribute(UINT VKey, USHORT index);

/* TGGTTVKbdSetKeyAttribute()      (USER32)
 *
 *  Set a virtual keyboard button attribute.
 *
 *  Parameters:
 *      VKey [in]: Windows Virtual-Key code
 *      index [in]: Button attribute index
 *          GTTV_DEAD_INDEX : sets or clears the 'dead key' attribute for the requested
 *              button.  A dead key will act similarly to a latch key but will modify
 *              the resulting character differently.  This attribute is not currently
 *              used and may be deprecated shortly.
 *          GTTV_LATCH_INDEX : sets or clears the 'latch key' attribute for the requested
 *              button.  A latch key will remain pressed until the next non-latch key is
 *              pressed or the same latch key is pressed again.
 *          GTTV_TOGGLE_INDEX : sets or clears the 'toggle key' attribute for the requested
 *              button.  A toggle key will remain pressed until the user clicks on it again.
 *          GTTV_STOP_INDEX : sets or clears the 'stop key' attribute for the requested
 *              button.  A stop button will close the virtual keyboard when pressed and
 *              enabled.
 *          GTTV_ATTR_FLAG_ALL : when combined with one of the above attribute names, the
 *              <VKey> parameter will be ignored and the attribute will be modified for
 *              all buttons.  See the remarks section for more information on its behaviour.
 *              This value is not valid without another attribute index.
 *      state [in]: TRUE to set attribute, FALSE to unset it.
 *
 *  Returns:
 *      no return value.
 *
 *  Remarks:
 *      VK_OEM_CLEAR is the designated key for the "Exit" button.
 *      When the GTTV_ATTR_FLAG_ALL flag is combined with an attribute index,
 *      the <code> parameter will be ignored and the behaviour will depend on
 *      the <state> parameter value:
 *        - If <state> is FALSE, the requested attribute will be cleared on all
 *          buttons on the keyboard.
 *        - If <state> is TRUE, the requested attribute will be restored to its
 *          original value on all buttons on the keyboard. 
 *      If the GTTV_ATTR_FLAG_ALL flag is not used, all attributes for the
 *      requested button are restored to their original state.
 *
 *      This call is only supported on GTTV.
 */
void WINAPI TGGTTVKbdSetKeyAttribute(UINT VKey, USHORT index, BOOL state);

/* TGGTTVKbdGetLockStatus()      (USER32)
 *
 * Get keyboard position lock status.
 *
 *  Returns:
 *      The lock status. If the status is TRUE, it means the keyboard can't be
 *      dragged. If the status is FALSE, it can be dragged using a pointing
 *      device.
 *
 *  Remarks:
 *      A locked keyboard doesn't prevent it to be moved with an API call.
 *
 *      This call is only supported on GTTV.
 */
BOOL WINAPI TGGTTVKbdGetLockStatus(void);
 
/** TGGTTVKbdSetLockStatus()      (USER32)
 *
 * Set keyboard position lock status.
 *
 *  Parameters:
 *      lock [in]: Requested lock status. If the status is TRUE, it means the
 *                 keyboard can't be dragged. If the status is FALSE, it can be
 *                 dragged using a pointing device.
 *
 *  Returns:
 *      no return value.
 *
 *  Remarks:
 *      A locked keyboard doesn't prevent it to be moved with an API call.
 *
 *      This call is only supported on GTTV.
 */
void WINAPI TGGTTVKbdSetLockStatus(BOOL lock);


/* prototype definitions for the functions in this header */
typedef void (WINAPI *TYPEOF(TGGTTVKbdGetDimensions))(UINT *, UINT *);
typedef void (WINAPI *TYPEOF(TGGTTVKbdSetPos))(LONG, LONG);
typedef void (WINAPI *TYPEOF(TGGTTVKbdSetPosFloat))(FLOAT, FLOAT);
typedef void (WINAPI *TYPEOF(TGGTTVKbdShow))(BOOL, UINT);
typedef void (WINAPI *TYPEOF(TGGTTVKbdSetCharacterFilter))(const WCHAR *, UINT);
typedef void (WINAPI *TYPEOF(TGGTTVKbdSetDefaultCharacterFilter))(const WCHAR *, UINT);
typedef BOOL (WINAPI *TYPEOF(TGGTTVKbdGetKeyAttribute))(UINT, USHORT);
typedef void (WINAPI *TYPEOF(TGGTTVKbdSetKeyAttribute))(UINT, USHORT, BOOL);
typedef BOOL (WINAPI *TYPEOF(TGGTTVKbdGetLockStatus))(void);
typedef void (WINAPI *TYPEOF(TGGTTVKbdSetLockStatus))(BOOL);


# ifdef __cplusplus
}
# endif
#endif
