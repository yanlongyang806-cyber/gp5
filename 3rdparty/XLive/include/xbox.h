/************************************************************************
*                                                                       *
*   Xbox.h -- This module defines the Xbox APIs                         *
*                                                                       *
*   Copyright (c) Microsoft Corp. All rights reserved.                  *
*                                                                       *
************************************************************************/
#pragma once

#ifndef _XBOX_H_
#define _XBOX_H_


//
// Define API decoration for direct importing of DLL references.
//

#include <sal.h>

#define XBOXAPI

#ifdef __cplusplus
extern "C" {
#endif

#include <xconfig.h>

#define XUSER_NAME_SIZE                 16
#define XUSER_MAX_NAME_LENGTH           (XUSER_NAME_SIZE - 1)

typedef ULONGLONG                       XUID;
typedef XUID                            *PXUID;

#define INVALID_XUID                    ((XUID) 0)

XBOXAPI
FORCEINLINE
BOOL
WINAPI
IsEqualXUID(
    IN      XUID                        xuid1,
    IN      XUID                        xuid2
    )
{
    return (xuid1 == xuid2);
}

XBOXAPI
FORCEINLINE
BOOL
WINAPI
IsTeamXUID(
    XUID xuid
    )
{
    return (xuid & 0xFF00000000000000) == 0xFE00000000000000;
}


#ifndef _NTOS_
#define XGetLanguage    XTLGetLanguage
#endif

XBOXAPI
DWORD
WINAPI
XGetLanguage(
    VOID
    );

XBOXAPI
DWORD
WINAPI
XGetLocale(
    VOID
    );














    
typedef struct _XOVERLAPPED             XOVERLAPPED, *PXOVERLAPPED;




// Xbox-specific Overlapped

typedef
VOID
(WINAPI *PXOVERLAPPED_COMPLETION_ROUTINE)(
    IN      DWORD                       dwErrorCode,
    IN      DWORD                       dwNumberOfBytesTransfered,
    IN OUT  PXOVERLAPPED                pOverlapped
    );


typedef struct _XOVERLAPPED {
    ULONG_PTR                           InternalLow;
    ULONG_PTR                           InternalHigh;
    ULONG_PTR                           InternalContext;
    HANDLE                              hEvent;
    PXOVERLAPPED_COMPLETION_ROUTINE     pCompletionRoutine;
    DWORD_PTR                           dwCompletionContext;
    DWORD                               dwExtendedError;
} XOVERLAPPED, *PXOVERLAPPED;

#define XHasOverlappedIoCompleted(lpOverlapped) \
    (*((volatile ULONG_PTR*)(&(lpOverlapped)->InternalLow)) != ERROR_IO_PENDING)

XBOXAPI
DWORD
WINAPI
XGetOverlappedResult(
    __in        PXOVERLAPPED        pOverlapped,
    __out_opt   LPDWORD             pdwResult,
    __in        BOOL                bWait
    );

XBOXAPI
DWORD
WINAPI
XGetOverlappedExtendedError(
    __in_opt    PXOVERLAPPED        pOverlapped
    );

XBOXAPI
DWORD
WINAPI
XCancelOverlapped(
    __in      PXOVERLAPPED                pOverlapped
    );

//  Content type ranges




XBOXAPI
DWORD
WINAPI
XEnumerate(
    __in         HANDLE                      hEnum,
    __inout_bcount(cbBuffer)  PVOID          pvBuffer,
    __in         DWORD                       cbBuffer,
    __out_opt    PDWORD                      pcItemsReturned,
    __inout_opt  PXOVERLAPPED                pOverlapped
    );

XBOXAPI
DWORD
WINAPI
XEnumerateBack(
    __in         HANDLE                      hEnum,
    __inout_bcount(cbBuffer)  PVOID          pvBuffer,
    __in         DWORD                       cbBuffer,
    __out_opt    PDWORD                      pcItemsReturned,
    __inout_opt  PXOVERLAPPED                pOverlapped
    );


//-----------------------------------------------------------------------------
// Game instrumentation errors              = 0x8056F0XX
//-----------------------------------------------------------------------------

#define SPA_E_CORRUPT_FILE          _HRESULT_TYPEDEF_(0x8056F001)
#define SPA_E_NOT_LOADED            _HRESULT_TYPEDEF_(0x8056F002)
#define SPA_E_BAD_TITLEID           _HRESULT_TYPEDEF_(0x8056F003)


// User/Profile/Account APIs

typedef enum _XUSER_SIGNIN_STATE
{
    eXUserSigninState_NotSignedIn,
    eXUserSigninState_SignedInLocally,
    eXUserSigninState_SignedInToLive
} XUSER_SIGNIN_STATE;

XBOXAPI
DWORD
WINAPI
XUserGetXUID(
    __in      DWORD                       dwUserIndex,
    __out     PXUID                       pxuid
    );

XBOXAPI
DWORD
WINAPI
XUserGetName(
    __in                        DWORD                       dwUserIndex,
    __out_ecount(cchUserName)   LPSTR                       szUserName,
    __in                        DWORD                       cchUserName
    );

XBOXAPI
XUSER_SIGNIN_STATE
WINAPI
XUserGetSigninState(
    __in      DWORD                       dwUserIndex
    );

XBOXAPI
FLOAT
WINAPI
XUserGetReputationStars(
    __in FLOAT fGamerRating
    );

#define XUSER_INFO_FLAG_LIVE_ENABLED    0x00000001
#define XUSER_INFO_FLAG_GUEST           0x00000002

typedef struct _XUSER_SIGNIN_INFO
{
   XUID                 xuid;
   DWORD                dwInfoFlags;
   XUSER_SIGNIN_STATE   UserSigninState;
   DWORD                dwGuestNumber;
   DWORD                dwSponsorUserIndex;
   CHAR                 szUserName[XUSER_NAME_SIZE];
} XUSER_SIGNIN_INFO, * PXUSER_SIGNIN_INFO;

#define XUSER_GET_SIGNIN_INFO_ONLINE_XUID_ONLY      0x00000001
#define XUSER_GET_SIGNIN_INFO_OFFLINE_XUID_ONLY     0x00000002

XBOXAPI
DWORD
WINAPI
XUserGetSigninInfo(
    __in      DWORD                        dwUserIndex,
    __in_opt  DWORD                        dwFlags,
    __out     PXUSER_SIGNIN_INFO           pSigninInfo
    );


typedef enum _XPRIVILEGE_TYPE
{
    XPRIVILEGE_MULTIPLAYER_SESSIONS              = 254, // on|off

    XPRIVILEGE_COMMUNICATIONS                    = 252, // on (communicate w/everyone) | off (check _FO)
    XPRIVILEGE_COMMUNICATIONS_FRIENDS_ONLY       = 251, // on (communicate w/friends only) | off (blocked)

    XPRIVILEGE_PROFILE_VIEWING                   = 249, // on (viewing allowed) | off (check _FO)
    XPRIVILEGE_PROFILE_VIEWING_FRIENDS_ONLY      = 248, // on (view friend’s only) | off (no details)

    XPRIVILEGE_USER_CREATED_CONTENT              = 247, // on (allow viewing of UCC) | off (check _FO)
    XPRIVILEGE_USER_CREATED_CONTENT_FRIENDS_ONLY = 246, // on (view UCC from friends only) | off (blocked)

    XPRIVILEGE_PURCHASE_CONTENT                  = 245, // on (allow purchase) | off (blocked)

    XPRIVILEGE_PRESENCE                          = 244, // on (share presence info) | off (check _FO)
    XPRIVILEGE_PRESENCE_FRIENDS_ONLY             = 243, // on (share w/friends only | off (don’t share)

    XPRIVILEGE_TRADE_CONTENT                     = 238, // on (allow trading) | off (blocked)

    XPRIVILEGE_VIDEO_COMMUNICATIONS              = 235, // on (communicate w/everyone) | off (check _FO)
    XPRIVILEGE_VIDEO_COMMUNICATIONS_FRIENDS_ONLY = 234, // on (communicate w/friends only) | off (blocked)
} XPRIVILEGE_TYPE;


XBOXAPI
DWORD
WINAPI
XUserAreUsersFriends(
    __in            DWORD           dwUserIndex,
    __in            PXUID           pXuids,
    __in            DWORD           dwXuidCount,
    __out_opt       PBOOL           pfResult,
    __out_opt       PXOVERLAPPED    pOverlapped
    );

XBOXAPI
DWORD
WINAPI
XUserCheckPrivilege(
    __in      DWORD                       dwUserIndex,
    __in      XPRIVILEGE_TYPE             PrivilegeType,
    __out     PBOOL                       pfResult
    );

// Context and Property definitions

typedef struct _XUSER_DATA
{
    BYTE                                type;

    union
    {
        LONG                            nData;     // XUSER_DATA_TYPE_INT32
        LONGLONG                        i64Data;   // XUSER_DATA_TYPE_INT64
        double                          dblData;   // XUSER_DATA_TYPE_DOUBLE
        struct                                     // XUSER_DATA_TYPE_UNICODE
        {
            DWORD                       cbData;    // Includes null-terminator
            LPWSTR                      pwszData;
        } string;
        FLOAT                           fData;     // XUSER_DATA_TYPE_FLOAT
        struct                                     // XUSER_DATA_TYPE_BINARY
        {
            DWORD                       cbData;
            PBYTE                       pbData;
        } binary;
        FILETIME                        ftData;    // XUSER_DATA_TYPE_DATETIME
    };
} XUSER_DATA, *PXUSER_DATA;

typedef struct _XUSER_PROPERTY
{
    DWORD                               dwPropertyId;
    XUSER_DATA                          value;
} XUSER_PROPERTY, *PXUSER_PROPERTY;

typedef struct _XUSER_CONTEXT
{
    DWORD                               dwContextId;
    DWORD                               dwValue;
} XUSER_CONTEXT, *PXUSER_CONTEXT;

// Context and Property APIs

XBOXAPI
DWORD
WINAPI
XUserGetContext(
    __in            DWORD               dwUserIndex,
    __inout         XUSER_CONTEXT*      pContext,
    __inout_opt     PXOVERLAPPED        pOverlapped
    );

XBOXAPI
VOID
WINAPI
XUserSetContext(
    __in      DWORD                       dwUserIndex,
    __in      DWORD                       dwContextId,
    __in      DWORD                       dwContextValue
    );

XBOXAPI
DWORD
WINAPI
XUserSetContextEx(
    __in        DWORD                       dwUserIndex,
    __in        DWORD                       dwContextId,
    __in        DWORD                       dwContextValue,
    __inout_opt PXOVERLAPPED                pOverlapped
    );

XBOXAPI
DWORD
WINAPI
XUserGetProperty(
    __in            DWORD                       dwUserIndex,
    __inout         DWORD*                      pcbActual,
    __inout_bcount(pcbActual) XUSER_PROPERTY*   pProperty,
    __inout_opt     PXOVERLAPPED                pOverlapped
    );

XBOXAPI
VOID
WINAPI
XUserSetProperty(
    __in                    DWORD                       dwUserIndex,
    __in                    DWORD                       dwPropertyId,
    __in                    DWORD                       cbValue,
    __in_bcount(cbValue)    CONST VOID*                 pvValue
    );

XBOXAPI
DWORD
WINAPI
XUserSetPropertyEx(
    __in                    DWORD                       dwUserIndex,
    __in                    DWORD                       dwPropertyId,
    __in                    DWORD                       cbValue,
    __in_bcount(cbValue)    CONST VOID*                 pvValue,
    __inout_opt             PXOVERLAPPED                pOverlapped
    );

// System-defined contexts and properties

#define X_PROPERTY_TYPE_MASK            0xF0000000
#define X_PROPERTY_SCOPE_MASK           0x00008000
#define X_PROPERTY_ID_MASK              0x00007FFF


#define XPROPERTYID(global, type, id)   ((global ? X_PROPERTY_SCOPE_MASK : 0) | ((type << 28) & X_PROPERTY_TYPE_MASK) | (id & X_PROPERTY_ID_MASK))
#define XCONTEXTID(global, id)          XPROPERTYID(global, XUSER_DATA_TYPE_CONTEXT, id)
#define XPROPERTYTYPEFROMID(id)         ((id >> 28) & 0xf)
#define XISSYSTEMPROPERTY(id)           (id & X_PROPERTY_SCOPE_MASK)

// Predefined contexts
#define X_CONTEXT_PRESENCE              XCONTEXTID(1, 0x1)
#define X_CONTEXT_GAME_TYPE             XCONTEXTID(1, 0xA)
#define X_CONTEXT_GAME_MODE             XCONTEXTID(1, 0xB)
#define X_CONTEXT_SESSION_JOINABLE      XCONTEXTID(1, 0xC)

#define X_PROPERTY_RANK                 XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   0x1)
#define X_PROPERTY_GAMERNAME            XPROPERTYID(1, XUSER_DATA_TYPE_UNICODE, 0x2)
#define X_PROPERTY_SESSION_ID           XPROPERTYID(1, XUSER_DATA_TYPE_INT64,   0x3)

// System attributes used in matchmaking queries
#define X_PROPERTY_GAMER_ZONE           XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   0x101)
#define X_PROPERTY_GAMER_COUNTRY        XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   0x102)
#define X_PROPERTY_GAMER_LANGUAGE       XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   0x103)
#define X_PROPERTY_GAMER_RATING         XPROPERTYID(1, XUSER_DATA_TYPE_FLOAT,   0x104)
#define X_PROPERTY_GAMER_MU             XPROPERTYID(1, XUSER_DATA_TYPE_DOUBLE,  0x105)
#define X_PROPERTY_GAMER_SIGMA          XPROPERTYID(1, XUSER_DATA_TYPE_DOUBLE,  0x106)
#define X_PROPERTY_GAMER_PUID           XPROPERTYID(1, XUSER_DATA_TYPE_INT64,   0x107)
#define X_PROPERTY_AFFILIATE_SCORE      XPROPERTYID(1, XUSER_DATA_TYPE_INT64,   0x108)
#define X_PROPERTY_GAMER_HOSTNAME       XPROPERTYID(1, XUSER_DATA_TYPE_UNICODE, 0x109)

// Properties used to write to skill leaderboards
#define X_PROPERTY_RELATIVE_SCORE                   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   0xA)
#define X_PROPERTY_SESSION_TEAM                     XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   0xB)

// Properties written at the session level to override TrueSkill parameters
#define X_PROPERTY_PLAYER_PARTIAL_PLAY_PERCENTAGE   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   0xC)
#define X_PROPERTY_PLAYER_SKILL_UPDATE_WEIGHTING_FACTOR XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   0xD)
#define X_PROPERTY_SESSION_SKILL_BETA               XPROPERTYID(1, XUSER_DATA_TYPE_DOUBLE,  0xE)
#define X_PROPERTY_SESSION_SKILL_TAU                XPROPERTYID(1, XUSER_DATA_TYPE_DOUBLE,  0xF)
#define X_PROPERTY_SESSION_SKILL_DRAW_PROBABILITY   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   0x10)

// Attachment size is written to a leaderboard when the entry qualifies for
// a gamerclip.  The rating can be retrieved via XUserEstimateRankForRating.
#define X_PROPERTY_ATTACHMENT_SIZE                  XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   0x11)


// Values for X_CONTEXT_GAME_TYPE
#define X_CONTEXT_GAME_TYPE_RANKED      0
#define X_CONTEXT_GAME_TYPE_STANDARD    1

// Achievement APIs

typedef struct
{
    DWORD                               dwUserIndex;
    DWORD                               dwAchievementId;
} XUSER_ACHIEVEMENT, *PXUSER_ACHIEVEMENT;

XBOXAPI
DWORD
WINAPI
XUserWriteAchievements(
    __in                            DWORD                       dwNumAchievements,
    __in_ecount(dwNumAchievements)  CONST XUSER_ACHIEVEMENT*    pAchievements,
    __inout_opt                     PXOVERLAPPED                pOverlapped
    );

typedef struct
{
    DWORD                               dwId;
    LPWSTR                              pwszLabel;
    LPWSTR                              pwszDescription;
    LPWSTR                              pwszUnachieved;
    DWORD                               dwImageId;
    DWORD                               dwCred;
    FILETIME                            ftAchieved;
    DWORD                               dwFlags;
} XACHIEVEMENT_DETAILS, *PXACHIEVEMENT_DETAILS;

// These lengths include the NULL-terminator
#define XACHIEVEMENT_MAX_LABEL_LENGTH   32
#define XACHIEVEMENT_MAX_DESC_LENGTH    100
#define XACHIEVEMENT_MAX_UNACH_LENGTH   100

#define XACHIEVEMENT_SIZE_BASE          (sizeof(XACHIEVEMENT_DETAILS))
#define XACHIEVEMENT_SIZE_STRINGS       (sizeof(WCHAR) * (XACHIEVEMENT_MAX_LABEL_LENGTH  + XACHIEVEMENT_MAX_DESC_LENGTH + XACHIEVEMENT_MAX_UNACH_LENGTH))
#define XACHIEVEMENT_SIZE_FULL          (XACHIEVEMENT_SIZE_BASE + XACHIEVEMENT_SIZE_STRINGS)

#define XACHIEVEMENT_INVALID_ID         ((DWORD)0xFFFFFFFF)

// XACHIEVEMENT_DETAILS::dwFlags can be manipulated with these defines and macros
#define XACHIEVEMENT_DETAILS_MASK_TYPE          0x00000007
#define XACHIEVEMENT_DETAILS_SHOWUNACHIEVED     0x00000008
#define XACHIEVEMENT_DETAILS_ACHIEVED_ONLINE    0x00010000
#define XACHIEVEMENT_DETAILS_ACHIEVED           0x00020000


#define AchievementType(dwFlags)           (dwFlags & XACHIEVEMENT_DETAILS_MASK_TYPE)
#define AchievementShowUnachieved(dwFlags) (dwFlags & XACHIEVEMENT_DETAILS_SHOWUNACHIEVED ? TRUE : FALSE)
#define AchievementEarnedOnline(dwFlags)   (dwFlags & XACHIEVEMENT_DETAILS_ACHIEVED_ONLINE ? TRUE : FALSE)
#define AchievementEarned(dwFlags)         (dwFlags & XACHIEVEMENT_DETAILS_ACHIEVED ? TRUE : FALSE)

// Types returned from AchievementType macro

#define XACHIEVEMENT_TYPE_COMPLETION            1
#define XACHIEVEMENT_TYPE_LEVELING              2
#define XACHIEVEMENT_TYPE_UNLOCK                3
#define XACHIEVEMENT_TYPE_EVENT                 4
#define XACHIEVEMENT_TYPE_TOURNAMENT            5
#define XACHIEVEMENT_TYPE_CHECKPOINT            6
#define XACHIEVEMENT_TYPE_OTHER                 7

#define XACHIEVEMENT_DETAILS_ALL                0xFFFFFFFF
#define XACHIEVEMENT_DETAILS_LABEL              0x00000001
#define XACHIEVEMENT_DETAILS_DESCRIPTION        0x00000002
#define XACHIEVEMENT_DETAILS_UNACHIEVED         0x00000004
#define XACHIEVEMENT_DETAILS_TFC                0x00000020

XBOXAPI
DWORD
WINAPI
XUserCreateAchievementEnumerator(
    __in      DWORD                       dwTitleId,
    __in      DWORD                       dwUserIndex,
    __in      XUID                        xuid,
    __in      DWORD                       dwDetailFlags,
    __in      DWORD                       dwStartingIndex,
    __in      DWORD                       cItem,
    __out     PDWORD                      pcbBuffer,
    __out     PHANDLE                     ph
    );

//
// Pictures
//

XBOXAPI
DWORD
WINAPI
XUserReadAchievementPicture(
    __in                             DWORD         dwUserIndex,
    __in                             DWORD         dwTitleId,
    __in                             DWORD         dwPictureId,
    __inout_bcount(dwPitch*dwHeight) BYTE*         pbTextureBuffer,
    __in                             DWORD         dwPitch,
    __in                             DWORD         dwHeight,
    __inout_opt                      PXOVERLAPPED  pOverlapped
    );



XBOXAPI
DWORD
WINAPI
XUserReadGamerPicture(
    __in                                DWORD           dwUserIndex,
    __in                                BOOL            fSmall,
    __inout_bcount(dwPitch*dwHeight)    PBYTE           pbTextureBuffer,
    __in                                DWORD           dwPitch,
    __in                                DWORD           dwHeight,
    __inout_opt                         PXOVERLAPPED    pOverlapped
    );


XBOXAPI
DWORD
WINAPI
XUserReadGamerPictureByKey(
    __in                                CONST PXUSER_DATA   pGamercardPictureKey,
    __in                                BOOL                fSmall,
    __inout_bcount(dwPitch*dwHeight)    PBYTE               pbTextureBuffer,
    __in                                DWORD               dwPitch,
    __in                                DWORD               dwHeight,
    __inout_opt                         PXOVERLAPPED        pOverlapped
    );


XBOXAPI
DWORD
WINAPI
XUserAwardGamerPicture(
    __in        DWORD                       dwUserIndex,
    __in        DWORD                       dwPictureId,
    __in        DWORD                       dwReserved,
    __inout_opt PXOVERLAPPED                pXOverlapped
    );


// Stats APIs

#define XUSER_STATS_ATTRS_IN_SPEC       64

typedef struct _XUSER_STATS_COLUMN
{
    WORD                                wColumnId;
    XUSER_DATA                          Value;
} XUSER_STATS_COLUMN, *PXUSER_STATS_COLUMN;

typedef struct _XUSER_STATS_ROW
{
    XUID                                xuid;
    DWORD                               dwRank;
    LONGLONG                            i64Rating;
    CHAR                                szGamertag[XUSER_NAME_SIZE];
    DWORD                               dwNumColumns;
    PXUSER_STATS_COLUMN                 pColumns;
} XUSER_STATS_ROW, *PXUSER_STATS_ROW;

typedef struct _XUSER_STATS_VIEW
{
    DWORD                               dwViewId;
    DWORD                               dwTotalViewRows;
    DWORD                               dwNumRows;
    PXUSER_STATS_ROW                    pRows;
} XUSER_STATS_VIEW, *PXUSER_STATS_VIEW;

typedef struct _XUSER_STATS_READ_RESULTS
{
    DWORD                               dwNumViews;
    PXUSER_STATS_VIEW                   pViews;
} XUSER_STATS_READ_RESULTS, *PXUSER_STATS_READ_RESULTS;

typedef struct _XUSER_STATS_SPEC
{
    DWORD                               dwViewId;
    DWORD                               dwNumColumnIds;
    WORD                                rgwColumnIds[XUSER_STATS_ATTRS_IN_SPEC];
} XUSER_STATS_SPEC, *PXUSER_STATS_SPEC;

XBOXAPI
DWORD
WINAPI
XUserResetStatsView(
    __in        DWORD                       dwUserIndex,
    __in        DWORD                       dwViewId,
    __inout_opt PXOVERLAPPED                pOverlapped
    );

XBOXAPI
DWORD
WINAPI
XUserResetStatsViewAllUsers(
    __in        DWORD                       dwViewId,
    __inout_opt PXOVERLAPPED                pOverlapped
    );

XBOXAPI
DWORD
WINAPI
XUserReadStats(
    __in                            DWORD                       dwTitleId,
    __in                            DWORD                       dwNumXuids,
    __inout_ecount(dwNumXuids)      CONST XUID*                 pXuids,
    __in                            DWORD                       dwNumStatsSpecs,
    __in_ecount(dwNumStatsSpecs)    CONST XUSER_STATS_SPEC*     pSpecs,
    __inout                         PDWORD                      pcbResults,
    __out_bcount_opt(pcbResults)    PXUSER_STATS_READ_RESULTS   pResults,
    __inout_opt                     PXOVERLAPPED                pOverlapped
    );


XBOXAPI
DWORD
WINAPI
XUserCreateStatsEnumeratorByRank(
    __in                            DWORD                       dwTitleId,
    __in                            DWORD                       dwRankStart,
    __in                            DWORD                       dwNumRows,
    __in                            DWORD                       dwNumStatsSpecs,
    __in_ecount(dwNumStatsSpecs)    CONST XUSER_STATS_SPEC*     pSpecs,
    __out                           PDWORD                      pcbBuffer,
    __out                           PHANDLE                     ph
    );

XBOXAPI
DWORD
WINAPI
XUserCreateStatsEnumeratorByXuid(
    __in                            DWORD                       dwTitleId,
    __in                            XUID                        XuidPivot,
    __in                            DWORD                       dwNumRows,
    __in                            DWORD                       dwNumStatsSpecs,
    __in_ecount(dwNumStatsSpecs)    CONST XUSER_STATS_SPEC*     pSpecs,
    __out                           PDWORD                      pcbBuffer,
    __out                           PHANDLE                     ph
    );


XBOXAPI
DWORD
WINAPI
XUserCreateStatsEnumeratorByRating(
    __in                            DWORD                       dwTitleId,
    __in                            LONGLONG                    i64Rating,
    __in                            DWORD                       dwNumRows,
    __in                            DWORD                       dwNumStatsSpecs,
    __in_ecount(dwNumStatsSpecs)    CONST XUSER_STATS_SPEC*     pSpecs,
    __out                           PDWORD                      pcbBuffer,
    __out                           PHANDLE                     ph
    );


// System defined leaderboard columns
#define X_STATS_COLUMN_ATTACHMENT_SIZE          ((WORD)0xFFFA)

// Column ids for skill leaderboards (STATS_VIEW_SKILL_* views)
#define X_STATS_COLUMN_SKILL_SKILL              61
#define X_STATS_COLUMN_SKILL_GAMESPLAYED        62
#define X_STATS_COLUMN_SKILL_MU                 63
#define X_STATS_COLUMN_SKILL_SIGMA              64

#define X_STATS_SKILL_SKILL_DEFAULT             1
#define X_STATS_SKILL_MU_DEFAULT                3.0
#define X_STATS_SKILL_SIGMA_DEFAULT             1.0


// Signin UI API

// XShowSiginUI flags.
#define XSSUI_FLAGS_LOCALSIGNINONLY                 0x00000001
#define XSSUI_FLAGS_SHOWONLYONLINEENABLED           0x00000002

XBOXAPI
DWORD
WINAPI
XEnableGuestSignin(
    IN      BOOL                        fEnable
    );

XBOXAPI
DWORD
WINAPI
XShowSigninUI(
    __in      DWORD                       cPanes,
    __in      DWORD                       dwFlags
    );

XBOXAPI
DWORD
WINAPI
XShowGuideUI(
    __in      DWORD                       dwUserIndex
    );

XBOXAPI
DWORD
WINAPI
XShowFriendsUI(
    __in      DWORD                       dwUserIndex
    );

XBOXAPI
DWORD
WINAPI
XShowPlayersUI(
    __in      DWORD                       dwUserIndex
    );

XBOXAPI
DWORD
WINAPI
XShowMessagesUI(
    __in      DWORD                       dwUserIndex
    );

XBOXAPI
DWORD
WINAPI
XShowMessageComposeUI(
    __in                            DWORD           dwUserIndex,
    __in_ecount_opt(cRecipients)    const XUID*     pXuidRecipients,
    __in                            DWORD           cRecipients,
    __in_opt                        LPCWSTR         pszText
    );

// Deprecated. Use XCUSTOMACTION_FLAG_CLOSES_GUIDE instead.
#define CUSTOMACTION_FLAG_CLOSESUI      1

typedef enum
{
    XMSG_FLAG_DISABLE_EDIT_RECIPIENTS   = 0x00000001
} XMSG_FLAGS;

#define XMSG_MAX_CUSTOM_IMAGE_SIZE      (36*1024)

typedef enum
{
    XCUSTOMACTION_FLAG_CLOSES_GUIDE     = 0x00000001,
    XCUSTOMACTION_FLAG_DELETES_MESSAGE  = 0x00000002
} XCUSTOMACTION_FLAGS;

#define XMSG_MAX_CUSTOMACTION_TRANSLATIONS      11


typedef struct
{
    DWORD                               dwActionId;
    WCHAR                               wszEnActionText[23];
    WORD                                wReserved;
    DWORD                               dwFlags;
    struct
    {
        DWORD                           dwLanguageId;
        WCHAR                           wszActionText[23];
        WORD                            wReserved;
    } rgTranslations[XMSG_MAX_CUSTOMACTION_TRANSLATIONS];
} XMSG_CUSTOMACTION;

#define XCUSTOMACTION_MAX_PAYLOAD_SIZE  1024

XBOXAPI
DWORD
WINAPI
XShowCustomMessageComposeUI(
    IN      DWORD                       dwUserIndex,
    IN      CONST XUID*                 pXuidRecipients     OPTIONAL,
    IN      DWORD                       cRecipients,
    IN      DWORD                       dwFlags,
    IN      LPCWSTR                     wszTitle,
    IN      LPCWSTR                     wszGameMessage,
    IN      LPCWSTR                     wszPlayerMessage    OPTIONAL,
    IN      CONST BYTE*                 pbImage             OPTIONAL,
    IN      DWORD                       cbImage,
    IN      CONST XMSG_CUSTOMACTION*    pCustomActions,
    IN      DWORD                       cCustomActions,
    IN      CONST BYTE*                 pbCustomPayload     OPTIONAL,
    IN      DWORD                       cbCustomPayload,
    IN      DWORD                       dwExpireMinutes     OPTIONAL,
    IN OUT  XOVERLAPPED*                pOverlapped         OPTIONAL
    );

XBOXAPI
DWORD
WINAPI
XShowGameInviteUI(
    __in                        DWORD           dwUserIndex,
    __in_ecount(cRecipients)    const XUID*     pXuidRecipients,
    __in                        DWORD           cRecipients,
    __in_opt                    LPCWSTR         pszText
    );

XBOXAPI
DWORD
WINAPI
XShowFriendRequestUI(
    __in      DWORD                       dwUserIndex,
    __in      XUID                        xuidUser
    );

#define XPLAYERLIST_CUSTOMTEXT_MAX_LENGTH   31
#define XPLAYERLIST_TITLE_MAX_LENGTH        36
#define XPLAYERLIST_DESCRIPTION_MAX_LENGTH  83
#define XPLAYERLIST_IMAGE_MAX_SIZE          36864
#define XPLAYERLIST_MAX_PLAYERS             100
#define XPLAYERLIST_BUTTONTEXT_MAX_LENGTH   23

typedef struct
{
    XUID        xuid;
    WCHAR       wszCustomText[XPLAYERLIST_CUSTOMTEXT_MAX_LENGTH];
} XPLAYERLIST_USER;

typedef struct
{
    XUID        xuidSelected;
    DWORD       dwKeyCode;
} XPLAYERLIST_RESULT;

typedef enum
{
    XPLAYERLIST_BUTTON_TYPE_PLAYERREVIEW         = 1,
    XPLAYERLIST_BUTTON_TYPE_GAMEINVITE,
    XPLAYERLIST_BUTTON_TYPE_MESSAGE,
    XPLAYERLIST_BUTTON_TYPE_FRIENDREQUEST,
    XPLAYERLIST_BUTTON_TYPE_TITLECUSTOMGLOBAL,
    XPLAYERLIST_BUTTON_TYPE_TITLECUSTOMINDIVIDUAL,
} XPLAYERLIST_BUTTON_TYPE;

typedef struct
{
    DWORD dwType;
    WCHAR wszCustomText[XPLAYERLIST_BUTTONTEXT_MAX_LENGTH];
} XPLAYERLIST_BUTTON;


typedef enum
{
    XPLAYERLIST_FLAG_CUSTOMTEXT             = 0x00000001,
} XPLAYERLIST_FLAGS;


XBOXAPI
DWORD
WINAPI
XShowCustomPlayerListUI(
    __in                        DWORD                       dwUserIndex,
    __in                        DWORD                       dwFlags,    
    __in                        LPCWSTR                     pszTitle,
    __in                        LPCWSTR                     pszDescription,
    __in_bcount_opt(cbImage)    CONST BYTE*                 pbImage,
    __in                        DWORD                       cbImage,
    __in_ecount(cPlayers)       CONST XPLAYERLIST_USER*     rgPlayers,
    __in                        DWORD                       cPlayers,
    __in_opt                    CONST XPLAYERLIST_BUTTON*   pXButton,
    __in_opt                    CONST XPLAYERLIST_BUTTON*   pYButton,
    __out_opt                   XPLAYERLIST_RESULT*         pResults,
    __inout_opt                 XOVERLAPPED*                pOverlapped
    );

XBOXAPI
DWORD
WINAPI
XShowKeyboardUI(
    __in                        DWORD         dwUserIndex,
    __in                        DWORD         dwFlags,
    __in_opt                    LPCWSTR       wseDefaultText,
    __in_opt                    LPCWSTR       wszTitleText,
    __in_opt                    LPCWSTR       wszDescriptionText,
    __out_ecount(cchResultText) LPWSTR        wszResultText,
    __in                        DWORD         cchResultText,
    __inout                     PXOVERLAPPED  pOverlapped
    );

XBOXAPI
DWORD
WINAPI
XShowGamerCardUI(
    __in      DWORD                       dwUserIndex,
    __in      XUID                        XuidPlayer
    );

XBOXAPI
DWORD
WINAPI
XShowAchievementsUI(
    __in      DWORD                       dwUserIndex
    );



// API to show error and informational messages
#define XMB_NOICON                      0x00000000
#define XMB_ERRORICON                   0x00000001
#define XMB_WARNINGICON                 0x00000002
#define XMB_ALERTICON                   0x00000003
#define XMB_PASSCODEMODE                0x00010000
#define XMB_VERIFYPASSCODEMODE          0x00020000

#define XMB_MAXBUTTONS                  3
#define XMB_CANCELID                    -1

typedef struct _MESSAGEBOX_RESULT
{
    union
    {
        DWORD                           dwButtonPressed;
        WORD                            rgwPasscode[4];
    };
} MESSAGEBOX_RESULT, *PMESSAGEBOX_RESULT;

XBOXAPI
DWORD
WINAPI
XShowMessageBoxUI(
    __in                                              DWORD               dwUserIndex,
    __in_ecount(ARRAYSIZE(MESSAGEBOX_PARAMS.szTitle)) LPCWSTR             wszTitle,
    __in_ecount(ARRAYSIZE(MESSAGEBOX_PARAMS.szText))  LPCWSTR             wszText,
    __in                                              DWORD               cButtons,
    __in_opt                                          LPCWSTR*            pwszButtons,
    __in                                              DWORD               dwFocusButton,
    __in                                              DWORD               dwFlags,
    __out                                             PMESSAGEBOX_RESULT  pResult,
    __inout                                           PXOVERLAPPED        pOverlapped
    );

XBOXAPI
DWORD
WINAPI
XShowPlayerReviewUI(
    __in      DWORD                       dwUserIndex,
    __in      XUID                        XuidFeedbackTarget
    );


//  UI Extensibility API
#define GAMEBANNER_WIDTH                384
#define GAMEBANNER_HEIGHT               128

XBOXAPI
DWORD
WINAPI
XCustomSetBannerImage(
    IN      PVOID                       pvTexture,
    IN      DWORD                       dwFlags
    );

XBOXAPI
VOID
WINAPI
XCustomSetAction(
    __in       DWORD                       dwActionIndex,
    __in_opt   LPCWSTR                     szActionText,
    __in       DWORD                       dwFlags
    );

typedef struct
{
    WORD                                wActionId;
    WCHAR                               wszActionText[23];
    DWORD                               dwFlags;
} XCUSTOMACTION;

XBOXAPI
VOID
WINAPI
XCustomRegisterDynamicActions(
    VOID
    );

XBOXAPI
VOID
WINAPI
XCustomUnregisterDynamicActions(
    VOID
    );

XBOXAPI
BOOL
WINAPI
XCustomGetCurrentGamercard(
    __out DWORD*              pdwUserIndex,
    __out XUID*               pXuid
    );

XBOXAPI
DWORD
WINAPI
XCustomSetDynamicActions(
    __in                            DWORD                   dwUserIndex,
    __in                            XUID                    xuid,
    __in_ecount(cCustomActions)     CONST XCUSTOMACTION*    pCustomActions,
    __in                            WORD                    cCustomActions
    );

XBOXAPI
BOOL
WINAPI
XCustomGetLastActionPress(
    __out_opt     DWORD*                      pdwUserIndex,
    __out_opt     DWORD*                      pdwActionIndex,
    __out_opt     XUID*                       pXuid
    );

XBOXAPI
DWORD
WINAPI
XCustomGetLastActionPressEx(
    __out_opt                       DWORD*          pdwUserIndex,
    __out_opt                       DWORD*          pdwActionId,
    __out_opt                       XUID*           pXuid,
    __out_bcount_opt(pcbPayload)    BYTE*           pbPayload,
    __inout_opt                     WORD*           pcbPayload
    );



#ifdef __cplusplus
}
#endif


#endif // _XBOX_H_

