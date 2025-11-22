/************************************************************************
*                                                                       *
*   Xam.h -- This module defines the system APIs                        *
*                                                                       *
*   Copyright (c) Microsoft Corp. All rights reserved.                  *
*                                                                       *
************************************************************************/
#ifndef __XAM_H__
#define __XAM_H__


#include <sal.h>



#ifdef __cplusplus
extern "C" {
#endif



#define VKBD_DEFAULT                    0x00000000
#define VKBD_LATIN_PASSWORD             0x00000080
#define VKBD_HIGHLIGHT_TEXT             0x20000000
#define VKBD_MULTILINE                  0x40000000
#define VKBD_ENABLEIME                  0x80000000





//------------------------------------------------------------------------------
// Notification apis
//------------------------------------------------------------------------------

//  Notification ids are 32 bit values layed out as follows:
//
//   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
//   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
//  +-+-----------+-----------------+-------------------------------+
//  |R|    Area   |    Version      |            Index              |
//  +-+-----+-----+-----------------+-------------------------------+
//
//  where
//
//      R - is a reserved bit (internal)
//
//      Area - is the area ranging from 0 - 63 (6 bits)
//
//      Version - is used to ensure that new notifications are not sent to
//          titles built to previous XDKs
//
//      Index - is the id of the notification within the area.  Each area can
//           have up to 65535 notifications starting at 1 (zero being invalid).
//

#define XNID(Version, Area, Index)      (DWORD)( (WORD)(Area) << 25 | (WORD)(Version) << 16 | (WORD)(Index))
#define XNID_VERSION(msgid)             (((msgid) >> 16) & 0x1FF)
#define XNID_AREA(msgid)                (((msgid) >> 25) & 0x3F)
#define XNID_INDEX(msgid)               ((msgid) & 0xFFFF)


//
// Notification Areas
//

#define XNOTIFY_SYSTEM                  (0x00000001)
#define XNOTIFY_LIVE                    (0x00000002)
#define XNOTIFY_FRIENDS                 (0x00000004)
#define XNOTIFY_CUSTOM                  (0x00000008)
#define XNOTIFY_XMP                     (0x00000020)
#define XNOTIFY_ALL                     (XNOTIFY_SYSTEM | XNOTIFY_LIVE | XNOTIFY_FRIENDS | XNOTIFY_CUSTOM | XNOTIFY_XMP)

//
// Bit numbers of each area (bit 0 is the least significant bit)
//

#define _XNAREA_SYSTEM                  (0)
#define _XNAREA_LIVE                    (1)
#define _XNAREA_FRIENDS                 (2)
#define _XNAREA_CUSTOM                  (3)
#define _XNAREA_XMP                     (5)

//
// System notifications
//

#define XN_SYS_FIRST                    XNID(0, _XNAREA_SYSTEM, 0x0001)
#define XN_SYS_UI                       XNID(0, _XNAREA_SYSTEM, 0x0009)
#define XN_SYS_SIGNINCHANGED            XNID(0, _XNAREA_SYSTEM, 0x000a)
#define XN_SYS_STORAGEDEVICESCHANGED    XNID(0, _XNAREA_SYSTEM, 0x000b)
#define XN_SYS_PROFILESETTINGCHANGED    XNID(0, _XNAREA_SYSTEM, 0x000e)
#define XN_SYS_MUTELISTCHANGED          XNID(0, _XNAREA_SYSTEM, 0x0011)
#define XN_SYS_INPUTDEVICESCHANGED      XNID(0, _XNAREA_SYSTEM, 0x0012)
#define XN_SYS_INPUTDEVICECONFIGCHANGED XNID(1, _XNAREA_SYSTEM, 0x0013)
#define XN_SYS_XLIVETITLEUPDATE         XNID(0, _XNAREA_SYSTEM, 0x0015)
#define XN_SYS_XLIVESYSTEMUPDATE        XNID(0, _XNAREA_SYSTEM, 0x0016)
#define XN_SYS_LAST                     XNID(0, _XNAREA_SYSTEM, 0x0017)


//
// Live notifications
//

#define XN_LIVE_FIRST                   XNID(0, _XNAREA_LIVE, 0x0001)
#define XN_LIVE_CONNECTIONCHANGED       XNID(0, _XNAREA_LIVE, 0x0001)
#define XN_LIVE_INVITE_ACCEPTED         XNID(0, _XNAREA_LIVE, 0x0002)
#define XN_LIVE_LINK_STATE_CHANGED      XNID(0, _XNAREA_LIVE, 0x0003)
#define XN_LIVE_CONTENT_INSTALLED       XNID(0, _XNAREA_LIVE, 0x0007)
#define XN_LIVE_MEMBERSHIP_PURCHASED    XNID(0, _XNAREA_LIVE, 0x0008)
#define XN_LIVE_VOICECHAT_AWAY          XNID(0, _XNAREA_LIVE, 0x0009)
#define XN_LIVE_PRESENCE_CHANGED        XNID(0, _XNAREA_LIVE, 0x000A)
#define XN_LIVE_LAST                    XNID(0, _XNAREA_LIVE, 0x0010)

//
// Friends notifications
//

#define XN_FRIENDS_FIRST                XNID(0, _XNAREA_FRIENDS, 0x0001)
#define XN_FRIENDS_PRESENCE_CHANGED     XNID(0, _XNAREA_FRIENDS, 0x0001)
#define XN_FRIENDS_FRIEND_ADDED         XNID(0, _XNAREA_FRIENDS, 0x0002)
#define XN_FRIENDS_FRIEND_REMOVED       XNID(0, _XNAREA_FRIENDS, 0x0003)
#define XN_FRIENDS_LAST                 XNID(0, _XNAREA_FRIENDS, 0x0008)

//
// Custom notifications
//

#define XN_CUSTOM_FIRST                 XNID(0, _XNAREA_CUSTOM, 0x0001)
#define XN_CUSTOM_GAMEBANNERPRESSED     XNID(0, _XNAREA_CUSTOM, 0x0002)
#define XN_CUSTOM_ACTIONPRESSED         XNID(0, _XNAREA_CUSTOM, 0x0003)
#define XN_CUSTOM_GAMERCARD             XNID(1, _XNAREA_CUSTOM, 0x0004)
#define XN_CUSTOM_LAST                  XNID(0, _XNAREA_CUSTOM, 0x0005)


HANDLE
WINAPI
XNotifyCreateListener(
    __in      ULONGLONG                   qwAreas
    );


BOOL
WINAPI
XNotifyGetNext(
    __in      HANDLE                    hNotification,
    __in      DWORD                     dwMsgFilter,
    __out     PDWORD                    pdwId,
    __out_opt PULONG_PTR                pParam
    );

//
// Popup Notifications
//
#define XNOTIFYUI_POS_TOP               1
#define XNOTIFYUI_POS_BOTTOM            2
#define XNOTIFYUI_POS_LEFT              4
#define XNOTIFYUI_POS_RIGHT             8
#define XNOTIFYUI_POS_VCENTER           0
#define XNOTIFYUI_POS_HCENTER           0

#define XNOTIFYUI_POS_TOPLEFT           (XNOTIFYUI_POS_TOP     | XNOTIFYUI_POS_LEFT)
#define XNOTIFYUI_POS_TOPCENTER         (XNOTIFYUI_POS_TOP     | XNOTIFYUI_POS_HCENTER)
#define XNOTIFYUI_POS_TOPRIGHT          (XNOTIFYUI_POS_TOP     | XNOTIFYUI_POS_RIGHT)
#define XNOTIFYUI_POS_CENTERLEFT        (XNOTIFYUI_POS_VCENTER | XNOTIFYUI_POS_LEFT)
#define XNOTIFYUI_POS_CENTER            (XNOTIFYUI_POS_VCENTER | XNOTIFYUI_POS_HCENTER)
#define XNOTIFYUI_POS_CENTERRIGHT       (XNOTIFYUI_POS_VCENTER | XNOTIFYUI_POS_RIGHT)
#define XNOTIFYUI_POS_BOTTOMLEFT        (XNOTIFYUI_POS_BOTTOM  | XNOTIFYUI_POS_LEFT)
#define XNOTIFYUI_POS_BOTTOMCENTER      (XNOTIFYUI_POS_BOTTOM  | XNOTIFYUI_POS_HCENTER)
#define XNOTIFYUI_POS_BOTTOMRIGHT       (XNOTIFYUI_POS_BOTTOM  | XNOTIFYUI_POS_RIGHT)

#define XNOTIFYUI_POS_VALIDBITS         (XNOTIFYUI_POS_TOP | XNOTIFYUI_POS_BOTTOM | XNOTIFYUI_POS_LEFT | XNOTIFYUI_POS_RIGHT)

VOID
WINAPI
XNotifyPositionUI(
    __in    DWORD                       dwPosition
    );

DWORD
WINAPI
XNotifyDelayUI(
    __in    ULONG                       ulMilliSeconds
    );





#ifdef __cplusplus
};
#endif



#endif // __XAM_H__

