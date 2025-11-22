/*
 * public TransGaming API function definitions
 *
 * Copyright 2010 TransGaming Inc.
 */

#ifndef  __TG_NOTIFY_H__
# define __TG_NOTIFY_H__

# include "tg_enums.h"


# ifdef __cplusplus
extern "C" {
# endif


/*********************************************************************
 *                  TGRegisterNotificationCallback    (USER32)
 *
 * allows a callback function to be registered that will notify the
 * caller about changes in focus, activation, and app visibility.
 * The callback function will provide an enum value indicating what
 * state has changed and a user context value.  The callback function
 * only needs to be registered once and can be unregistered by passing
 * NULL for the function pointer.  The callback function is purely
 * for notification purposes and cannot be used to control or prevent
 * the state changes.  Returns TRUE if the callback was successfully
 * registered, and FALSE otherwise.
 *
 * There is no guarantee which thread the callback function will be
 * called from.  The callback function should manage its own access
 * to shared resources while executing.
 *
 * the TGRegisterNotificationCallback() function is found in 'user32.dll'
 * and needs to be imported through GetProcAddress() before it can be
 * called.
 *
 */

/************************ example code *******************************
    // ... some time on startup ...
    BOOL result;

    // register for activation callbacks
    result = TGRegisterNotificationCallback(TGNOTIFY_ACTIVATION,
                                            tgNotificationCallbackFunction,
                                            (LPVOID)(DWORD_PTR)0xdeadbeef);


    // ... run the game normally ...
    // ... when an activation event occurs, the callback function 
    // ... tgNotificationCallbackFunction will be called with a state value
    // ... indicating what type of activation event occurred. 


    // unregister for activation callbacks before shutdown or unload.
    result = TGRegisterNotificationCallback(TGNOTIFY_ACTIVATION, NULL, NULL);
*/


/* TGNotifyCallback_Func():
 *
 *  prototype for the user provided callback function for a notification.
 *
 *  Parameters:
 *      type [in]: the type of notification event (ie: TGNOTIFY_ACTIVATION)
 *          that occurred.
 *      state [in]: the state of the event.  The value passed here is 
 *          dependent on the callback type.
 *      data [in]: state specific data.  Not all states provide data.  In
 *          these cases, this value will be 0.
 *      context [in]: unmodified user-specified context value associated
 *          with the callback.
 *
 *  Returns:
 *      no return value.
 *
 *  Remarks:
 *      none.
 */
typedef void (WINAPI *TGNotifyCallback_Func)(TG_NOTIFY_TYPE type,
                                             DWORD          state,
                                             INT            data,
                                             LPVOID         context);


/* TGRegisterNotificationCallback()     (USER32)
 *
 *  registers a callback function for special event notifications.
 *
 *  Parameters:
 *      type [in]: TGNOTIFY_* type enum indicating which type of callback is
 *          being registered.
 *      callback [in]: address of the callback function to be associated with
 *          the requested notification type.  One callback function may be
 *          registered for each type.  This parameter may be NULL to disable
 *          the notifications of type <type>.
 *      context [in]: user-specified context value that is passed to the 
 *          callback function in its <context> parameter.
 *
 *  Returns:
 *      TRUE if the callback function was successfully registered.  FALSE if
 *      an error occurred or the call is unsupported.
 *
 *  Remarks:
 *      This function is used to register an application callback for a certain
 *      type of special event.  Only a single callback may be registered for each
 *      type at any given time.  Changing the callback function will replace any
 *      previously registered callback for that type.  If the context value is a
 *      pointer, its memory must remain valid until the callback is unregistered.
 *
 *      If the context value needs to be changed for a callback, the callback
 *      function must be re-registered with the new context value.
 *
 *      This call is only supported on Apple.
 */
BOOL WINAPI TGRegisterNotificationCallback(TG_NOTIFY_TYPE           type,
                                           TGNotifyCallback_Func    callback, 
                                           LPVOID                   context);


/* prototype definitions for the functions in this header */
typedef BOOL (WINAPI *TYPEOF(TGRegisterNotificationCallback))(TG_NOTIFY_TYPE type, TGNotifyCallback_Func callback, LPVOID context);


# ifdef __cplusplus
}
# endif
#endif
