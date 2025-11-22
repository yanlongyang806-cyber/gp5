
/*===============================================================*/
/*  Copyright (C) Microsoft Corporation.  All Rights Reserved.   */
/*===============================================================*/
/*
    WinLive.h
        - Live public header
*/

#ifndef __WINLIVE__
#define __WINLIVE__


#if defined (_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif


#define LIVE_ON_WINDOWS

#include <wtypes.h>
#include <unknwn.h>
#include <xtl.h>


#ifndef XUSER_INDEX_FOCUS
#define XUSER_INDEX_FOCUS               0x000000FD
#endif
#ifndef XUSER_INDEX_NONE
#define XUSER_INDEX_NONE                0x000000FE
#endif
#ifndef XUSER_INDEX_ANY
#define XUSER_INDEX_ANY                 0x000000FF
#endif

#define E_DEBUGGER_PRESENT MAKE_HRESULT(1, 4, 0x317)

#ifdef __cplusplus
extern "C" {
#endif

typedef void* XLIVE_PROTECTED_BUFFER;
typedef void** XLIVE_PROTECTED_BUFFER_PTR;

HRESULT WINAPI XLivePBufferAllocate(__in ULONG ulSize, __deref_out XLIVE_PROTECTED_BUFFER_PTR pxebBuffer);
HRESULT WINAPI XLivePBufferFree(__in XLIVE_PROTECTED_BUFFER xebBuffer);
HRESULT WINAPI XLivePBufferGetByte(__in XLIVE_PROTECTED_BUFFER xebBuffer, __in ULONG ulOffset, __out UCHAR* pucValue);
HRESULT WINAPI XLivePBufferSetByte(__in XLIVE_PROTECTED_BUFFER xebBuffer, __in ULONG ulOffset, __in UCHAR ucValue);
HRESULT WINAPI XLivePBufferGetDWORD(__in XLIVE_PROTECTED_BUFFER xebBuffer, __in ULONG ulOffset, __out DWORD* pdwValue);
HRESULT WINAPI XLivePBufferSetDWORD(__in XLIVE_PROTECTED_BUFFER xebBuffer, __in ULONG ulOffset, __in DWORD dwValue);
HRESULT WINAPI XLivePBufferGetByteArray(__in XLIVE_PROTECTED_BUFFER xebBuffer, __in ULONG ulOffset, __out_ecount(dwSize) BYTE* pbValues, __in DWORD dwSize);
HRESULT WINAPI XLivePBufferSetByteArray(__in XLIVE_PROTECTED_BUFFER xebBuffer, __in ULONG ulOffset, __in_ecount(dwSize) BYTE* pbValues, __in DWORD dwSize);
HRESULT WINAPI XNetGetCurrentAdapter(__out_ecount_opt(*pcchBuffer) PCHAR pszAdapter, __inout ULONG* pcchBuffer);
HRESULT WINAPI XLiveLoadLibraryEx(__in PWSTR pszModuleFileName, __out HMODULE* phModule, __in DWORD dwFlags);
HRESULT WINAPI XLiveFreeLibrary(__in HMODULE hModule);


#define XLIVE_INITFLAG_USE_ADAPTER_NAME          0x00000001
#define XLIVE_INITFLAG_NO_AUTO_LOGON             0x00000002

typedef struct _XLIVE_INITIALIZE_INFO
{
    UINT cbSize;
    DWORD dwFlags;
    IUnknown* pD3D;
    VOID* pD3DPP;
    LANGID langID;  //Language the Game UI is in.  Live UI will map to this if we support it, otherwise default to English
    WORD wReserved1;
    PCHAR pszAdapterName;
    WORD wLivePortOverride;
    WORD wReserved2;
} XLIVE_INITIALIZE_INFO;

typedef struct _XLIVE_INPUT_INFO
{
    UINT cbSize;
    HWND hWnd;
    UINT uMsg;
    WPARAM wParam;
    LPARAM lParam;
    BOOL fHandled;
    LRESULT lRet;
} XLIVE_INPUT_INFO;

HRESULT WINAPI XLiveInitialize(__in XLIVE_INITIALIZE_INFO* pXii);
void WINAPI XLiveUnInitialize();
HRESULT WINAPI XLiveRender();
BOOL WINAPI XLivePreTranslateMessage(__in const LPMSG lpMsg);
HRESULT WINAPI XLiveInput(__inout XLIVE_INPUT_INFO* pXii);
HRESULT WINAPI XLiveOnCreateDevice(__in IUnknown* pD3D, __in VOID* pD3DPP);
HRESULT WINAPI XLiveOnDestroyDevice();
HRESULT WINAPI XLiveOnResetDevice(__in VOID* pD3DPP);
BOOL WINAPI XCloseHandle(__in HANDLE hObject);
HRESULT WINAPI XLiveRegisterDataSection(__in PCWSTR pszDataModuleName, __in LPVOID lpvBaseAddress, __in SIZE_T nSize);
HRESULT WINAPI XLiveUnregisterDataSection(__in PCWSTR pszDataModuleName);
HRESULT WINAPI XLiveUpdateHashes(__in PCWSTR pszFilename, __in BOOL fAppend);


typedef struct _XLIVEUPDATE_INFORMATION {
    DWORD cbSize;        // on input, must be set to sizeof(XLIVEUPDATE_INFORMATION)
    BOOL  bSystemUpdate; // TRUE for system update, FALSE for title update
    DWORD dwFromVersion; // version of system/title updated by this title update package
    DWORD dwToVersion;   // version that the system/title will be updated to
    WCHAR szUpdateDownloadPath[MAX_PATH];        // directory containing update
} XLIVEUPDATE_INFORMATION, *PXLIVEUPDATE_INFORMATION;

HRESULT
WINAPI
XLiveGetUpdateInformation(
    __inout PXLIVEUPDATE_INFORMATION pXLiveUpdateInfo
);

HRESULT
WINAPI
XLiveUpdateSystem (
    __in_opt LPCWSTR lpszRelaunchCmdLine
);

typedef enum _XLIVE_DEBUG_LEVEL
{
    XLIVE_DEBUG_LEVEL_OFF = 0,      // No debug output
    XLIVE_DEBUG_LEVEL_ERROR,        // Error only debug output
    XLIVE_DEBUG_LEVEL_WARNING,      // Warnings and error debug output
    XLIVE_DEBUG_LEVEL_INFO,         // Info, warning and error debug output
    XLIVE_DEBUG_LEVEL_DEFAULT,      // Reset level to whatever is in registry
}XLIVE_DEBUG_LEVEL;

HRESULT WINAPI XLiveSetDebugLevel(__in XLIVE_DEBUG_LEVEL xdlLevel, __out_opt XLIVE_DEBUG_LEVEL* pxdlOldLevel);


#define LIVEID_MEMBERNAME_MAX       113
#define LIVEID_PASSWORD_MAX         16

#define XLMGRCREDS_FLAG_SAVE                0x00000001
#define XLMGRCREDS_FLAG_DELETE              0x00000002

#define XLSIGNIN_FLAG_SAVECREDS             0x00000001
#define XLSIGNIN_FLAG_ALLOWTITLEUPDATES     0x00000002
#define XLSIGNIN_FLAG_ALLOWSYSTEMUPDATES    0x00000004

//Warning: Once the credentials have been used the memory containing the strings will be Zeroed
HRESULT WINAPI XLiveManageCredentials(
    __nullterminated LPWSTR lpszLiveIdName,
    __nullterminated LPWSTR lpszLiveIdPassword,
    __in DWORD dwCredFlags,
    __inout_opt PXOVERLAPPED pXOverlapped);

//Warning: Once the credentials have been used the memory containing the strings will be Zeroed
HRESULT WINAPI XLiveSignin(
    __nullterminated LPWSTR lpszLiveIdName,
    __nullterminated LPWSTR lpszLiveIdPassword,
    __in DWORD dwFlags,
    __inout_opt PXOVERLAPPED pXOverlapped);

HRESULT WINAPI XLiveSignout (__inout_opt PXOVERLAPPED pXOverlapped);


HRESULT WINAPI XLiveGetLiveIdError(
    __out HRESULT *phrAuthState,
    __out HRESULT *phrRequestState,
    __out_ecount_part(*pdwUrlLen,*pdwUrlLen) LPWSTR   wszWebFlowUrl,
    __inout LPDWORD  pdwUrlLen);

#define MID_SPONSOR_TOKEN_SIZE             29

HRESULT WINAPI XLiveSetSponsorToken(
    __in_ecount(MID_SPONSOR_TOKEN_SIZE) LPCWSTR pwzToken,
    __in DWORD dwTitleId);

HRESULT WINAPI XLiveUninstallTitle(DWORD dwTitleId);

HRESULT WINAPI XLiveGetLocalOnlinePort(__out WORD *pwPort);

#ifndef NONET

//-----------------------------------------------------------
// LocatorService for dedicated servers
//-----------------------------------------------------------

// Predefined dedicated server types
#define XLOCATOR_SERVERTYPE_PUBLIC          0   // dedicated server is for all players.
#define XLOCATOR_SERVERTYPE_GOLD_ONLY       1   // dedicated server is for Gold players only.
#define XLOCATOR_SERVERTYPE_PEER_HOSTED     2   // dedicated server is a peer-hosted game server.
#define XLOCATOR_SERVICESTATUS_PROPERTY_START     0x100

// Property IDs for locator service status

// Total online servers.
#define X_PROPERTY_SERVICESTATUS_SERVERCOUNT_TOTAL          XPROPERTYID(1, XUSER_DATA_TYPE_INT32,  XLOCATOR_SERVICESTATUS_PROPERTY_START + 1)

// public servers (allow any user to play)
#define X_PROPERTY_SERVICESTATUS_SERVERCOUNT_PUBLIC         XPROPERTYID(1, XUSER_DATA_TYPE_INT32,  XLOCATOR_SERVICESTATUS_PROPERTY_START + 2)

// gold-only servers (only allow gold users to play)
#define X_PROPERTY_SERVICESTATUS_SERVERCOUNT_GOLDONLY       XPROPERTYID(1, XUSER_DATA_TYPE_INT32,  XLOCATOR_SERVICESTATUS_PROPERTY_START + 3)

// peer hosted servers (for invite only)
#define X_PROPERTY_SERVICESTATUS_SERVERCOUNT_PEERHOSTED     XPROPERTYID(1, XUSER_DATA_TYPE_INT32,  XLOCATOR_SERVICESTATUS_PROPERTY_START + 4)

// Compare operators for dedicated server search.
typedef enum _XTS_FILTER_COMPARE_OPERATOR
{
    XTS_FILTER_COMPARE_OPERATOR_None = 0,               // this operator is reserved.
    XTS_FILTER_COMPARE_OPERATOR_Equals = 1,             // property = value
    XTS_FILTER_COMPARE_OPERATOR_NotEquals = 2,          // property <> value
    XTS_FILTER_COMPARE_OPERATOR_GreaterThan = 4,        // property > value
    XTS_FILTER_COMPARE_OPERATOR_GreaterEqualThan = 8,   // property >= value
    XTS_FILTER_COMPARE_OPERATOR_LessThan = 16,          // property < value
    XTS_FILTER_COMPARE_OPERATOR_LessEqualThan = 32,     // property <= value
    XTS_FILTER_COMPARE_OPERATOR_Contains = 64,          // CONTAINS(property, value)
}XTS_FILTER_COMPARE_OPERATOR;

// A filter for dedicated server search.
typedef struct _XLOCATOR_FILTER {
    XUSER_PROPERTY userProperty;                        // The property ID and the value are combined with eComparator
    XTS_FILTER_COMPARE_OPERATOR eComparator;
} XLOCATOR_FILTER, *PXLOCATOR_FILTER;

// A group of filters for dedicated server search.
// The filters in one filter group are combined with 'OR' operator.
// Each filter group are combined with other groups with 'AND' operator.
typedef struct _XLOCATOR_FILTER_GROUP {
    UINT cFilterCount;                                  // Number of filters in this group
    PXLOCATOR_FILTER pFilters;
} XLOCATOR_FILTER_GROUP, *PXLOCATOR_FILTER_GROUP;

#define XLOCATOR_SORT_ASCENDING     0       // sorted in ascending order, from lowest value to highest value
#define XLOCATOR_SORT_DESCENDING    1       // sorted in descending order, from highest value to lowest value

typedef struct _XLOCATOR_SORTER {
    DWORD dwPropertyId;                     // id of the field to be sorted.
    DWORD dwSortDirection;                  // should be XLOCATOR_SORT_ASCENDING or XLOCATOR_SORT_DESCENDING
} XLOCATOR_SORTER, *PXLOCATOR_SORTER;

// result of a dedicated server search.
typedef struct _XLOCATOR_SEARCHRESULT {
    ULONGLONG serverID;                     // the ID of dedicated server
    DWORD dwServerType;                     // see XLOCATOR_SERVERTYPE_PUBLIC, etc
    XNADDR serverAddress;                   // the address dedicated server
    XNKID xnkid;
    XNKEY xnkey;
    DWORD dwMaxPublicSlots;
    DWORD dwMaxPrivateSlots;
    DWORD dwFilledPublicSlots;
    DWORD dwFilledPrivateSlots;
    DWORD cProperties;                      // number of custom properties.
    PXUSER_PROPERTY pProperties;            // an array of custom properties.
} XLOCATOR_SEARCHRESULT, *PXLOCATOR_SEARCHRESULT;

#define SERVER_MASK_MAX         32
typedef struct _XLOCATOR_INIT_INFO
{
    DWORD dwTitleId;
    DWORD dwServiceId;
    WORD  wDefaultPort;
    char  szServiceMask[SERVER_MASK_MAX];
}XLOCATOR_INIT_INFO, *PXLOCATOR_INIT_INFO;

// fields that supports search in a dedicated server search
enum XTS_SEARCH_FIELD
{
    XTS_SEARCH_FIELD_ServerIdentity = 1,
    XTS_SEARCH_FIELD_ServerType = 2,
    XTS_SEARCH_FIELD_MaxPublicSlots = 3,
    XTS_SEARCH_FIELD_MaxPrivateSlots = 4,
    XTS_SEARCH_FIELD_AvailablePublicSlots = 5,
    XTS_SEARCH_FIELD_AvailablePrivateSlots = 6,
    XTS_SEARCH_FIELD_ReservedInt1 = 7,
    XTS_SEARCH_FIELD_ReservedInt2 = 8,
    XTS_SEARCH_FIELD_ReservedInt3 = 9,
    XTS_SEARCH_FIELD_ReservedInt4 = 10,
    XTS_SEARCH_FIELD_ReservedInt5 = 11,
    XTS_SEARCH_FIELD_ReservedInt6 = 12,
    XTS_SEARCH_FIELD_ReservedInt7 = 13,
    XTS_SEARCH_FIELD_ReservedInt8 = 14,
    XTS_SEARCH_FIELD_ReservedInt9 = 15,
    XTS_SEARCH_FIELD_ReservedInt10 = 16,
    XTS_SEARCH_FIELD_ReservedInt11 = 17,
    XTS_SEARCH_FIELD_ReservedInt12 = 18,
    XTS_SEARCH_FIELD_ReservedInt13 = 19,
    XTS_SEARCH_FIELD_ReservedInt14 = 20,
    XTS_SEARCH_FIELD_ReservedInt15 = 21,
    XTS_SEARCH_FIELD_ReservedInt16 = 22,
    XTS_SEARCH_FIELD_ReservedInt17 = 23,
    XTS_SEARCH_FIELD_ReservedInt18 = 24,
    XTS_SEARCH_FIELD_ReservedInt19 = 25,
    XTS_SEARCH_FIELD_ReservedInt20 = 26,
    XTS_SEARCH_FIELD_ReservedLongLong1 = 27,
    XTS_SEARCH_FIELD_ReservedLongLong2 = 28,
    XTS_SEARCH_FIELD_ReservedLongLong3 = 29,
    XTS_SEARCH_FIELD_ReservedLongLong4 = 30,
    XTS_SEARCH_FIELD_ReservedLongLong5 = 31,
    XTS_SEARCH_FIELD_ReservedLongLong6 = 32,
    XTS_SEARCH_FIELD_ReservedLongLong7 = 33,
    XTS_SEARCH_FIELD_ReservedLongLong8 = 34,
    XTS_SEARCH_FIELD_ReservedLongLong9 = 35,
    XTS_SEARCH_FIELD_ReservedLongLong10 = 36,
    XTS_SEARCH_FIELD_ReservedString1 = 37,
    XTS_SEARCH_FIELD_ReservedString2 = 38,
    XTS_SEARCH_FIELD_ReservedString3 = 39,
    XTS_SEARCH_FIELD_ReservedString4 = 40,
    XTS_SEARCH_FIELD_ReservedString5 = 41,
    XTS_SEARCH_FIELD_ReservedString6 = 42,
    XTS_SEARCH_FIELD_ReservedString7 = 43,
    XTS_SEARCH_FIELD_ReservedString8 = 44,
    XTS_SEARCH_FIELD_ReservedString9 = 45,
    XTS_SEARCH_FIELD_ReservedString10 = 46,
    XTS_SEARCH_FIELD_OwnerXuid = 47,
    XTS_SEARCH_FIELD_OwnerGamerTag = 48,
    XTS_SEARCH_FIELD_RegionID = 49,
    XTS_SEARCH_FIELD_LanguageID = 50,
    XTS_SEARCH_FIELD_FilledPublicSlots = 51,
    XTS_SEARCH_FIELD_FilledPrivateSlots = 52,
};

#define XLOCATOR_DEDICATEDSERVER_PROPERTY_START     0x200

// These properties are used for search only.
// The search result header should already contains the information, and the query should not request these properties again.
#define X_PROPERTY_DEDICATEDSERVER_IDENTITY             XPROPERTYID(1, XUSER_DATA_TYPE_INT64,  XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ServerIdentity)   // server id. supports '=' operator only.
#define X_PROPERTY_DEDICATEDSERVER_TYPE                 XPROPERTYID(1, XUSER_DATA_TYPE_INT32,  XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ServerType)
#define X_PROPERTY_DEDICATEDSERVER_MAX_PUBLIC_SLOTS     XPROPERTYID(1, XUSER_DATA_TYPE_INT32,  XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_MaxPublicSlots)
#define X_PROPERTY_DEDICATEDSERVER_MAX_PRIVATE_SLOTS    XPROPERTYID(1, XUSER_DATA_TYPE_INT32,  XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_MaxPrivateSlots)
#define X_PROPERTY_DEDICATEDSERVER_AVAILABLE_PUBLIC_SLOTS   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,  XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_AvailablePublicSlots)
#define X_PROPERTY_DEDICATEDSERVER_AVAILABLE_PRIVATE_SLOTS  XPROPERTYID(1, XUSER_DATA_TYPE_INT32,  XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_AvailablePrivateSlots)
#define X_PROPERTY_DEDICATEDSERVER_FILLED_PUBLIC_SLOTS      XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_FilledPublicSlots)
#define X_PROPERTY_DEDICATEDSERVER_FILLED_PRIVATE_SLOTS     XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_FilledPrivateSlots)

// These properties are used for both advertise and search.
// To retrieve these properties, please set the property IDs in the pRequiredPropertyIDs parameter.
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT1   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt1)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT2   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt2)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT3   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt3)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT4   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt4)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT5   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt5)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT6   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt6)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT7   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt7)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT8   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt8)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT9   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt9)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT10   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt10)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT11   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt11)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT12   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt12)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT13   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt13)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT14   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt14)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT15   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt15)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT16   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt16)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT17   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt17)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT18   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt18)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT19   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt19)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_INT20   XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedInt20)

#define X_PROPERTY_DEDICATEDSERVER_RESERVED_LONGLONG1   XPROPERTYID(1, XUSER_DATA_TYPE_INT64,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedLongLong1)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_LONGLONG2   XPROPERTYID(1, XUSER_DATA_TYPE_INT64,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedLongLong2)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_LONGLONG3   XPROPERTYID(1, XUSER_DATA_TYPE_INT64,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedLongLong3)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_LONGLONG4   XPROPERTYID(1, XUSER_DATA_TYPE_INT64,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedLongLong4)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_LONGLONG5   XPROPERTYID(1, XUSER_DATA_TYPE_INT64,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedLongLong5)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_LONGLONG6   XPROPERTYID(1, XUSER_DATA_TYPE_INT64,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedLongLong6)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_LONGLONG7   XPROPERTYID(1, XUSER_DATA_TYPE_INT64,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedLongLong7)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_LONGLONG8   XPROPERTYID(1, XUSER_DATA_TYPE_INT64,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedLongLong8)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_LONGLONG9   XPROPERTYID(1, XUSER_DATA_TYPE_INT64,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedLongLong9)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_LONGLONG10  XPROPERTYID(1, XUSER_DATA_TYPE_INT64,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedLongLong10)

#define X_PROPERTY_DEDICATEDSERVER_RESERVED_STRING1   XPROPERTYID(1, XUSER_DATA_TYPE_UNICODE,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedString1)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_STRING2   XPROPERTYID(1, XUSER_DATA_TYPE_UNICODE,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedString2)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_STRING3   XPROPERTYID(1, XUSER_DATA_TYPE_UNICODE,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedString3)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_STRING4   XPROPERTYID(1, XUSER_DATA_TYPE_UNICODE,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedString4)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_STRING5   XPROPERTYID(1, XUSER_DATA_TYPE_UNICODE,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedString5)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_STRING6   XPROPERTYID(1, XUSER_DATA_TYPE_UNICODE,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedString6)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_STRING7   XPROPERTYID(1, XUSER_DATA_TYPE_UNICODE,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedString7)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_STRING8   XPROPERTYID(1, XUSER_DATA_TYPE_UNICODE,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedString8)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_STRING9   XPROPERTYID(1, XUSER_DATA_TYPE_UNICODE,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedString9)
#define X_PROPERTY_DEDICATEDSERVER_RESERVED_STRING10   XPROPERTYID(1, XUSER_DATA_TYPE_UNICODE,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_ReservedString10)

// the following properties only support XTS_FILTER_COMPARE_OPERATOR_Equals operator
#define X_PROPERTY_DEDICATEDSERVER_OWNER_XUID           XPROPERTYID(1, XUSER_DATA_TYPE_INT64,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_OwnerXuid)
#define X_PROPERTY_DEDICATEDSERVER_OWNER_GAMERTAG       XPROPERTYID(1, XUSER_DATA_TYPE_UNICODE,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_OwnerGamerTag)
#define X_PROPERTY_DEDICATEDSERVER_REGIONID             XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_RegionID)
#define X_PROPERTY_DEDICATEDSERVER_LANGUAGEID           XPROPERTYID(1, XUSER_DATA_TYPE_INT32,   XLOCATOR_DEDICATEDSERVER_PROPERTY_START + XTS_SEARCH_FIELD_LanguageID)

// Advertise a dedicated server to locator service.
HRESULT WINAPI XLocatorServerAdvertise(
    __in DWORD dwUserIndex,
    __in DWORD dwServerType,             // see XLOCATOR_SERVERTYPE_PUBLIC, etc
    __in XNKID xnkid,
    __in XNKEY xnkey,
    __in DWORD dwMaxPublicSlots,
    __in DWORD dwMaxPrivateSlots,
    __in DWORD dwFilledPublicSlots,
    __in DWORD dwFilledPrivateSlots,
    __in DWORD cProperties,                                     // number of custom properties
    __in_ecount_opt(cProperties) PXUSER_PROPERTY pProperties,   // array of properties
    __inout_opt PXOVERLAPPED pXOverlapped
);

// Unadvertise a server from locator service.
HRESULT WINAPI XLocatorServerUnAdvertise(
    __in        DWORD           dwUserIndex,
    __inout_opt PXOVERLAPPED    pXOverlapped
);

HRESULT WINAPI XLocatorGetServiceProperty(
    DWORD dwUserIndex,
    DWORD cNumProperties,
    __inout_ecount(cNumProperties) PXUSER_PROPERTY pProperties, // an array of user properties.
                                                                // pass in the property id like X_PROPERTY_SERVICESTATUS_SERVERCOUNT_TOTAL etc
                                                                // and returns the property value
    __inout_opt PXOVERLAPPED pXOverlapped
);

// Create an enumerator for dedicated servers search.
// The filters in one filter group are combined with 'OR' operator,
// and then the filter groups are combined with 'AND' operator.
DWORD WINAPI XLocatorCreateServerEnumerator(
    __in DWORD dwUserIndex,
    __in DWORD cItems,
    __in DWORD cRequiredPropertyIDs,
    __in_ecount_opt(cRequiredPropertyIDs) DWORD* pRequiredPropertyIDs,
    __in DWORD cFilterGroupItems,
    __in_ecount_opt(cFilterGroupItems) PXLOCATOR_FILTER_GROUP pxlFilterGroups,
    __in DWORD cSorterItems,
    __in_ecount_opt(cSorterItems) PXLOCATOR_SORTER pxlSorters,
    __out_opt DWORD* pcbBuffer,
    __deref_out PHANDLE phEnum
);

// Search the dedicated servers by ID.
DWORD WINAPI XLocatorCreateServerEnumeratorByIDs(
    __in DWORD dwUserIndex,
    __in DWORD cItems,
    __in DWORD cRequiredPropertyIDs,
    __in_ecount_opt(cRequiredPropertyIDs) DWORD* pRequiredPropertyIDs,
    __in DWORD cIDS,
    __in_ecount(cIDS) PULONGLONG pIDs,
    __out_opt DWORD* pcbBuffer,
    __deref_out PHANDLE phEnum
);

// Initialize locator service.
HRESULT WINAPI XLocatorServiceInitialize(
    __in XLOCATOR_INIT_INFO * pXii,
    __deref_out PHANDLE phLocatorService
);

HRESULT WINAPI XLocatorServiceUnInitialize(
    __in HANDLE hLocatorService
);

HRESULT WINAPI XLocatorCreateKey(__inout XNKID * pxnkid,
                                 __inout XNKEY * pxnkey);

#endif  // NONET


#ifdef __cplusplus
}
#endif // __cplusplus

#endif  /* __WINLIVE__ */

