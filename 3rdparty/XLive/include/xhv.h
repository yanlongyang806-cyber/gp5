/***************************************************************************
 *
 *  xhv.h -- This module defines the XBox High-Level Voice APIs
 *
 *  Copyright (C) Microsoft Corporation.  All Rights Reserved.
 *
 ***************************************************************************/

#ifndef __XHV_H__
#define __XHV_H__


#ifndef __DSOUND_INCLUDED__
    #error You must include dsound.h before xhv.h
#endif

#ifndef __XONLINE__
    #error You must include xonline.h before xhv.h
#endif


#ifdef __cplusplus
extern "C" {
#endif


//
// Constants
//

#define XHVINLINE                               FORCEINLINE

#define XHV_MAX_REMOTE_TALKERS                  (30)
#define XHV_MAX_LOCAL_TALKERS                   (1)

//
// At the moment, you may only have up to this many enabled processing modes
// for local and remote talkers (separately).
//

#define XHV_MAX_PROCESSING_MODES                (2)

//
// When setting playback priorities, talker pairs between
// XHV_PLAYBACK_PRIORITY_MAX and XHV_PLAYBACK_PRIORITY_MIN, inclusive, will be
// heard, while any other value will result in muting that pair.
//

#define XHV_PLAYBACK_PRIORITY_MAX               (0)
#define XHV_PLAYBACK_PRIORITY_MIN               (0xFFFF)
#define XHV_PLAYBACK_PRIORITY_NEVER             (0xFFFFFFFF)

//
// Each packet reported by voice chat mode is the following size (including the
// XHV_CODEC_HEADER)
//

#define XHV_VOICECHAT_MODE_PACKET_SIZE          (10)

//
// When supplying a buffer to GetLocalChatData, you won't have to supply a
// buffer any larger than the following number of packets (or
// XHV_MAX_VOICECHAT_PACKETS * XHV_VOICECHAT_MODE_PACKET_SIZE bytes)
//

#define XHV_MAX_VOICECHAT_PACKETS               (10)

//
// The microphone callback is given PCM data in this format.
//

#define XHV_PCM_BYTES_PER_SAMPLE                (2)
#define XHV_PCM_SAMPLE_RATE                     (8000)

//
// Data Ready Flags.  These flags are set when there is local data waiting to be
// consumed (e.g. through GetLocalChatData).  GetLocalDataFlags() allows you to
// get the current state of these flags without entering XHV's critical section.
// Each mask is 4 bits, one for each local talker.  The least significant bit in
// each section indicates data is available for user index 0, while the most
// significant bit indicates user index 3.
//

#define XHV_VOICECHAT_DATA_READY_MASK           (0xF)
#define XHV_VOICECHAT_DATA_READY_OFFSET         (0)


//
// Interfaces
//

typedef struct IXHVEngine                       *LPIXHVENGINE, *PIXHVENGINE;

//
// Typedefs, Enums and Structures
//

typedef DWORD                                   XHV_PROCESSING_MODE, *PXHV_PROCESSING_MODE;
typedef DWORD                                   XHV_PLAYBACK_PRIORITY;
typedef VOID(*PFNMICRAWDATAREADY)(
    IN  DWORD                                   dwUserIndex,
    IN  PVOID                                   pvData,
    IN  DWORD                                   dwSize,
    IN  PBOOL                                   pVoiceDetected
    );

typedef DWORD                                   XHV_LOCK_TYPE;

#define XHV_LOCK_TYPE_LOCK                      0
#define XHV_LOCK_TYPE_TRYLOCK                   1
#define XHV_LOCK_TYPE_UNLOCK                    2
#define XHV_LOCK_TYPE_COUNT                     3

//
// Supported processing modes
//

const XHV_PROCESSING_MODE XHV_LOOPBACK_MODE  = 0x00000001;
const XHV_PROCESSING_MODE XHV_VOICECHAT_MODE = 0x00000002;

//
// You must specify the following initialization parameters at creation time
//

typedef struct XHV_INIT_PARAMS
{
    DWORD                                       dwMaxRemoteTalkers;
    DWORD                                       dwMaxLocalTalkers;
    PXHV_PROCESSING_MODE                        localTalkerEnabledModes;
    DWORD                                       dwNumLocalTalkerEnabledModes;
    PXHV_PROCESSING_MODE                        remoteTalkerEnabledModes;
    DWORD                                       dwNumRemoteTalkerEnabledModes;
    BOOL                                        bCustomVADProvided;
    BOOL                                        bRelaxPrivileges;
    PFNMICRAWDATAREADY                          pfnMicrophoneRawDataReady;
    HWND                                        hwndFocus;
} XHV_INIT_PARAMS, *PXHV_INIT_PARAMS;

//
// This header appears at the beginning of each blob of data reported by voice
// chat mode
//

#pragma pack(push, 1)
typedef struct XHV_CODEC_HEADER
{
    WORD                                        bMsgNo :  4;
    WORD                                        wSeqNo : 11;
    WORD                                        bFriendsOnly : 1;
} XHV_CODEC_HEADER, *PXHV_CODEC_HEADER;
#pragma pack (pop)


//
//  IXHVEngine interface
//

#undef  INTERFACE
#define INTERFACE IXHVEngine

DECLARE_INTERFACE(IXHVEngine)
{
    STDMETHOD_(ULONG, AddRef)(
        __in  THIS
        )   PURE;

    STDMETHOD_(ULONG, Release)(
        __in  THIS
        )   PURE;

    //
    // Locking
    //

    STDMETHOD(Lock)(
        __in                THIS_
        __in XHV_LOCK_TYPE  lockType
        );

    //
    // Processing mode management
    //

    STDMETHOD(StartLocalProcessingModes)(
        __in                                                           THIS_
        __in                               DWORD                       dwUserIndex,
        __in_ecount(dwNumProcessingModes)  CONST PXHV_PROCESSING_MODE  processingModes,
        __in                               DWORD                       dwNumProcessingModes
        )   PURE;

    STDMETHOD(StopLocalProcessingModes)(
        __in                                                           THIS_
        __in                               DWORD                       dwUserIndex,
        __in_ecount(dwNumProcessingModes)  CONST PXHV_PROCESSING_MODE  processingModes,
        __in                               DWORD                       dwNumProcessingModes
        )   PURE;

    STDMETHOD(StartRemoteProcessingModes)(
        __in                                                           THIS_
        __in                               XUID                        xuidRemoteTalker,
        __in_ecount(dwNumProcessingModes)  CONST PXHV_PROCESSING_MODE  processingModes,
        __in                               DWORD                       dwNumProcessingModes
        )   PURE;

    STDMETHOD(StopRemoteProcessingModes)(
        __in                                                           THIS_
        __in                               XUID                        xuidRemoteTalker,
        __in_ecount(dwNumProcessingModes)  CONST PXHV_PROCESSING_MODE  processingModes,
        __in                               DWORD                       dwNumProcessingModes
        )   PURE;

    STDMETHOD(SetMaxDecodePackets)(
        __in         THIS_
        __in  DWORD  dwMaxDecodePackets
        )   PURE;

    //
    // Manage talkers
    //

    STDMETHOD(RegisterLocalTalker)(
        __in         THIS_
        __in  DWORD  dwUserIndex
        )   PURE;

    STDMETHOD(UnregisterLocalTalker)(
        __in         THIS_
        __in  DWORD  dwUserIndex
        )   PURE;

    STDMETHOD(RegisterRemoteTalker)(
        __in              THIS_
        __in      XUID    xuidRemoteTalker,
        __in_opt  LPVOID  pReservedRemoteTalkerFX,
        __in_opt  LPVOID  pReservedTalkerPairFX,
        __in_opt  LPVOID  pReservedOutputVoice 
        )   PURE;

    STDMETHOD(UnregisterRemoteTalker)(
        __in        THIS_
        __in  XUID  xuidRemoteTalker
        )   PURE;

    STDMETHOD(GetRemoteTalkers)(
        __in          THIS_
        __out PDWORD  pdwRemoteTalkersCount,
        __out PXUID   pxuidRemoteTalkers
        )   PURE;

    STDMETHOD_(BOOL, IsHeadsetPresent)(
        __in         THIS_
        __in  DWORD  dwUserIndex
        )   PURE;

    STDMETHOD_(BOOL, IsLocalTalking)(
        __in         THIS_
        __in  DWORD  dwUserIndex
        )   PURE;

    STDMETHOD_(BOOL, IsRemoteTalking)(
        __in        THIS_
        __in  XUID  xuidRemoteTalker
        )   PURE;


    STDMETHOD_(DWORD, GetDataReadyFlags)(
        __in  THIS
        )   PURE;

    //
    // Voice chat mode functions
    //

    STDMETHOD(GetLocalChatData)(
        __in                            THIS_
        __in                    DWORD   dwUserIndex,
        __out_bcount(*pdwSize)  PBYTE   pbData,
        __inout                 PDWORD  pdwSize,
        __out_opt               PDWORD  pdwPackets
        )   PURE;

    STDMETHOD(SetPlaybackPriority)(
        __in                         THIS_
        __in  XUID                   xuidRemoteTalker,
        __in  DWORD                  dwUserIndex,
        __in  XHV_PLAYBACK_PRIORITY  playbackPriority
        )   PURE;

    STDMETHOD(SubmitIncomingChatData)(
        __in                                THIS_
        __in                   XUID         xuidRemoteTalker,
        __in_bcount(*pdwSize)  CONST BYTE*  pbData,
        __inout                PDWORD       pdwSize
        )   PURE;
};

#pragma push_macro("VTBL")
#pragma push_macro("THIS")
#pragma push_macro("THIS_")

#undef  VTBL
#undef  THIS
#undef  THIS_

#if defined(__cplusplus) && !defined(CINTERFACE)
#define VTBL(p) (p)
#define THIS
#define THIS_
#else // defined(__cplusplus) && !defined(CINTERFACE)
#define VTBL(p) ((p)->lpVtbl)
#define THIS    pThis
#define THIS_   pThis,
#endif // defined(__cplusplus) && !defined(CINTERFACE)

XHVINLINE
STDMETHODIMP_(ULONG)
IXHVEngine_AddRef(
    __in  PIXHVENGINE  pThis
    )
{
    return VTBL(pThis)->AddRef(THIS);
}

XHVINLINE
STDMETHODIMP_(ULONG)
IXHVEngine_Release(
    __in  PIXHVENGINE  pThis
    )
{
    return VTBL(pThis)->Release(THIS);
}

XHVINLINE
STDMETHODIMP
IXHVEngine_Lock(
    __in  PIXHVENGINE    pThis,
    __in  XHV_LOCK_TYPE  lockType
    )
{
    return VTBL(pThis)->Lock(THIS_ lockType);
}

XHVINLINE
STDMETHODIMP
IXHVEngine_StartLocalProcessingModes(
    __in                               PIXHVENGINE                 pThis,
    __in                               DWORD                       dwUserIndex,
    __in_ecount(dwNumProcessingModes)  CONST PXHV_PROCESSING_MODE  processingModes,
    __in                               DWORD                       dwNumProcessingModes
    )
{
    return VTBL(pThis)->StartLocalProcessingModes(THIS_ dwUserIndex, processingModes, dwNumProcessingModes);
}

XHVINLINE
STDMETHODIMP
IXHVEngine_StopLocalProcessingModes(
    __in                               PIXHVENGINE                 pThis,
    __in                               DWORD                       dwUserIndex,
    __in_ecount(dwNumProcessingModes)  CONST PXHV_PROCESSING_MODE  processingModes,
    __in                               DWORD                       dwNumProcessingModes
    )
{
    return VTBL(pThis)->StopLocalProcessingModes(THIS_ dwUserIndex, processingModes, dwNumProcessingModes);
}

XHVINLINE
STDMETHODIMP
IXHVEngine_StartRemoteProcessingModes(
    __in                               PIXHVENGINE                 pThis,
    __in                               XUID                        Xuid,
    __in_ecount(dwNumProcessingModes)  CONST PXHV_PROCESSING_MODE  processingModes,
    __in                               DWORD                       dwNumProcessingModes
    )
{
    return VTBL(pThis)->StartRemoteProcessingModes(THIS_ Xuid, processingModes, dwNumProcessingModes);
}

XHVINLINE
STDMETHODIMP
IXHVEngine_StopRemoteProcessingModes(
    __in                               PIXHVENGINE                 pThis,
    __in                               XUID                        Xuid,
    __in_ecount(dwNumProcessingModes)  CONST PXHV_PROCESSING_MODE  processingModes,
    __in                               DWORD                       dwNumProcessingModes
    )
{
    return VTBL(pThis)->StopRemoteProcessingModes(THIS_ Xuid, processingModes, dwNumProcessingModes);
}

XHVINLINE
STDMETHODIMP
IXHVEngine_SetMaxDecodePackets(
    __in  PIXHVENGINE  pThis,
    __in  DWORD        dwMaxDecodePackets
    )
{
    return VTBL(pThis)->SetMaxDecodePackets(THIS_ dwMaxDecodePackets);
}

XHVINLINE
STDMETHODIMP
IXHVEngine_RegisterLocalTalker(
    __in  PIXHVENGINE  pThis,
    __in  DWORD        dwUserIndex
    )
{
    return VTBL(pThis)->RegisterLocalTalker(THIS_ dwUserIndex);
}

XHVINLINE
STDMETHODIMP
IXHVEngine_UnregisterLocalTalker(
    __in  PIXHVENGINE  pThis,
    __in  DWORD        dwUserIndex
    )
{
    return VTBL(pThis)->UnregisterLocalTalker(THIS_ dwUserIndex);
}

XHVINLINE
STDMETHODIMP
IXHVEngine_RegisterRemoteTalker(
    __in      PIXHVENGINE  pThis,
    __in      XUID         xuidRemoteTalker,
    __in_opt  LPVOID       pfxRemoteTalkerFX,
    __in_opt  LPVOID       pfxTalkerPairFX,
    __in_opt  LPVOID       pOutputVoice
    )
{
    return VTBL(pThis)->RegisterRemoteTalker(THIS_ xuidRemoteTalker, pfxRemoteTalkerFX, pfxTalkerPairFX, pOutputVoice);
}

XHVINLINE
STDMETHODIMP
IXHVEngine_UnregisterRemoteTalker(
    __in  PIXHVENGINE  pThis,
    __in  XUID         xuidRemoteTalker
    )
{
    return VTBL(pThis)->UnregisterRemoteTalker(THIS_ xuidRemoteTalker);
}

XHVINLINE
STDMETHODIMP
IXHVEngine_GetRemoteTalkers(
    __in  PIXHVENGINE  pThis,
    __out PDWORD       pdwRemoteTalkersCount,
    __out PXUID        pxuidRemoteTalkers
    )
{
    return VTBL(pThis)->GetRemoteTalkers(THIS_ pdwRemoteTalkersCount, pxuidRemoteTalkers);
}

XHVINLINE
STDMETHODIMP_(BOOL)
IXHVEngine_IsHeadsetPresent(
    __in  PIXHVENGINE  pThis,
    __in  DWORD        dwUserIndex
    )
{
    return VTBL(pThis)->IsHeadsetPresent(THIS_ dwUserIndex);
}

XHVINLINE
STDMETHODIMP_(BOOL)
IXHVEngine_IsLocalTalking(
    __in  PIXHVENGINE  pThis,
    __in  DWORD        dwUserIndex
    )
{
    return VTBL(pThis)->IsLocalTalking(THIS_ dwUserIndex);
}

XHVINLINE
STDMETHODIMP_(BOOL)
IXHVEngine_IsRemoteTalking(
    __in  PIXHVENGINE  pThis,
    __in  XUID         xuidRemoteTalker
    )
{
    return VTBL(pThis)->IsRemoteTalking(THIS_ xuidRemoteTalker);
}


XHVINLINE
STDMETHODIMP_(DWORD)
IXHVEngine_GetDataReadyFlags(
    __in  PIXHVENGINE  pThis
    )
{
    return VTBL(pThis)->GetDataReadyFlags(THIS);
}

// Supressing bogus warning caused by bug in prefast add in.
#ifndef _PREFAST_
#pragma warning(push)
#pragma warning(disable:4068)
#endif
XHVINLINE
STDMETHODIMP
IXHVEngine_GetLocalChatData(
    __in                    PIXHVENGINE  pThis,
    __in                    DWORD        dwUserIndex,
    __out_bcount(*pdwSize)  PBYTE        pbData,
    __inout                 PDWORD       pdwSize,
    __out_opt               PDWORD       pdwPackets
    )
{
#pragma prefast( suppress:22116 )
    return VTBL(pThis)->GetLocalChatData(THIS_ dwUserIndex, pbData, pdwSize, pdwPackets);
}
#ifndef _PREFAST_
#pragma warning(pop)
#endif

XHVINLINE
STDMETHODIMP
IXHVEngine_SetPlaybackPriority(
    __in  PIXHVENGINE            pThis,
    __in  XUID                   xuidRemoteTalker,
    __in  DWORD                  dwUserIndex,
    __in  XHV_PLAYBACK_PRIORITY  playbackPriority
    )
{
    return VTBL(pThis)->SetPlaybackPriority(THIS_ xuidRemoteTalker, dwUserIndex, playbackPriority);
}

XHVINLINE
STDMETHODIMP
IXHVEngine_SubmitIncomingChatData(
    __in                   PIXHVENGINE  pThis,
    __in                   XUID         xuidRemoteTalker,
    __in_bcount(*pdwSize)  CONST BYTE*  pbData,
    __inout                PDWORD       pdwSize
    )
{
    return VTBL(pThis)->SubmitIncomingChatData(THIS_ xuidRemoteTalker, pbData, pdwSize);
}

#pragma pop_macro("VTBL")
#pragma pop_macro("THIS")
#pragma pop_macro("THIS_")

//
// Use the following function to create an instance of the XHV engine
//

XBOXAPI
HRESULT
WINAPI
XHVCreateEngine(
    __in      PXHV_INIT_PARAMS                  pParams,
    __out_opt PHANDLE                           phWorkerThread,
    __out     PIXHVENGINE*                      ppEngine
    );

#ifdef __cplusplus
}
#endif

#endif // __XHV_H__

