/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

Module Name:

    XLiveRpl.h

Abstract:

    Public header file for a XLive Ripple Launcher library.

--*/

#ifndef __INC_XLIVERPL__
#define __INC_XLIVERPL__

#ifdef __cplusplus
extern "C" {
#endif

//
// Routines for transmitting data over the conduit.
//
HRESULT
WINAPI
XLiveRplInitialize(
    __in BOOL fCreator,
    __in_opt HANDLE hShutdownEvent,
    __out HANDLE *phRplInstance
    );

VOID
WINAPI
XLiveRplUninitialize(
    __in HANDLE hRplInstance
    );

HRESULT
WINAPI
XLiveRplWaitForConnect(
    __in HANDLE hRplInstance,
    __in DWORD dwMillisecondsToWait
    );

HRESULT
WINAPI
XLiveRplStartRead(
    __in HANDLE hRplInstance,
    __out_ecount(cbReadBuffer) PBYTE pReadBuffer,
    __in DWORD cbReadBuffer
    );

HRESULT
WINAPI
XLiveRplCheckReadStatus(
    __in HANDLE hRplInstance,
    __out PDWORD pcbReadBufferContents,
    __in DWORD dwMillisecondsToWait
    );

HRESULT
WINAPI
XLiveRplStartWrite(
    __in HANDLE hRplInstance,
    __in_ecount(cbWriteBuffer) PBYTE pWriteBuffer,
    __in DWORD cbWriteBuffer
    );

HRESULT
WINAPI
XLiveRplCheckWriteStatus(
    __in HANDLE hRplInstance,
    __out PDWORD pcbWriteBufferContents,
    __in DWORD dwMillisecondsToWait
    );

//
// Maximum size of entire frame (for either challenge or response) is 1KB.
//
#define RPL_CONDUIT_BUFFER_MAX_SIZE  1024

//
// Routines for interacting with base challenge/response mechanism.
//
HRESULT
WINAPI
XLiveRplGetBaseChallengeSize(
    __in HANDLE hRplInstance,
    __in DWORD cbCustomChallengeData,
    __out PDWORD pcbBaseChallengeSize
    );

HRESULT
WINAPI
XLiveRplGetBaseResponseSize(
    __in HANDLE hRplInstance,
    __in DWORD cbCustomResponseData,
    __out PDWORD pcbBaseResponseSize
    );

//
// Define callback used by ripple launcher process (i.e., the responder) to
// handle custom data embedded within base challenge and response buffers.
//
typedef HRESULT (WINAPI *PRPL_LAUNCHER_INNER_CALLBACK_PROC)(
    __in_bcount(cbCustomChallenge) PVOID pCustomChallenge,
    __in DWORD cbCustomChallenge,
    __out_bcount_part(*pcbCustomResponse, *pcbCustomResponse) PVOID pCustomResponse,
    __inout PDWORD pcbCustomResponse,
    __inout_opt PVOID pInnerCallbackContext
    );

HRESULT
WINAPI
XLiveRplGetResponse(
    __in HANDLE hRplInstance,
    __in_bcount(cbBaseChallengeData) PVOID pBaseChallengeData,
    __in DWORD cbBaseChallengeData,
    __out_bcount_part(*pcbBaseResponseData, *pcbBaseResponseData) PVOID pBaseResponseData,
    __inout PDWORD pcbBaseResponseData,
    __in_opt PRPL_LAUNCHER_INNER_CALLBACK_PROC pInnerCallback,
    __inout_opt PVOID pInnerCallbackContext
    );

//
// Define callbacks used by title process (i.e., the challenger) to generate
// the custom challenge and evaluate the custom response embedded within the
// base challenge and response buffers.
//
typedef HRESULT (WINAPI *PRPL_CHALLENGER_INNER_CONSTRUCT_CALLBACK_PROC)(
    __out_bcount_part(*pcbCustomChallenge, *pcbCustomChallenge) PVOID pCustomChallenge,
    __inout PDWORD pcbCustomChallenge,
    __inout_opt PVOID pInnerCallbackContext
    );

HRESULT
WINAPI
XLiveRplGetChallenge(
    __in HANDLE hRplInstance,
    __out_bcount_part(*pcbBaseChallengeData, *pcbBaseChallengeData) PVOID pBaseChallengeData,
    __inout PDWORD pcbBaseChallengeData,
    __in_opt PRPL_CHALLENGER_INNER_CONSTRUCT_CALLBACK_PROC pInnerCallback,
    __inout_opt PVOID pInnerCallbackContext
    );


typedef HRESULT (WINAPI *PRPL_CHALLENGER_INNER_EVALUATE_CALLBACK_PROC)(
    __in_bcount(cbCustomResponse) PVOID pCustomResponse,
    __in DWORD cbCustomResponse,
    __inout_opt PVOID pInnerCallbackContext
    );

HRESULT
WINAPI
XLiveRplCheckResponse(
    __in HANDLE hRplInstance,
    __in_bcount(cbBaseResponseData) PVOID pBaseResponseData,
    __in DWORD cbBaseResponseData,
    __in_opt PRPL_CHALLENGER_INNER_EVALUATE_CALLBACK_PROC pInnerCallback,
    __inout_opt PVOID pInnerCallbackContext
    );

#ifdef __cplusplus
}
#endif // __cplusplus

#endif  /* __INC_XLIVERPL__ */

