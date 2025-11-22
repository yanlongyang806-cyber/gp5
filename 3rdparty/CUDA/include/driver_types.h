/*
 * Copyright 1993-2008 NVIDIA Corporation.  All rights reserved.
 *
 * NOTICE TO USER:   
 *
 * This source code is subject to NVIDIA ownership rights under U.S. and 
 * international Copyright laws.  Users and possessors of this source code 
 * are hereby granted a nonexclusive, royalty-free license to use this code 
 * in individual and commercial software.
 *
 * NVIDIA MAKES NO REPRESENTATION ABOUT THE SUITABILITY OF THIS SOURCE 
 * CODE FOR ANY PURPOSE.  IT IS PROVIDED "AS IS" WITHOUT EXPRESS OR 
 * IMPLIED WARRANTY OF ANY KIND.  NVIDIA DISCLAIMS ALL WARRANTIES WITH 
 * REGARD TO THIS SOURCE CODE, INCLUDING ALL IMPLIED WARRANTIES OF 
 * MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE.
 * IN NO EVENT SHALL NVIDIA BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL, 
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS 
 * OF USE, DATA OR PROFITS,  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE 
 * OR OTHER TORTIOUS ACTION,  ARISING OUT OF OR IN CONNECTION WITH THE USE 
 * OR PERFORMANCE OF THIS SOURCE CODE.  
 *
 * U.S. Government End Users.   This source code is a "commercial item" as 
 * that term is defined at  48 C.F.R. 2.101 (OCT 1995), consisting  of 
 * "commercial computer  software"  and "commercial computer software 
 * documentation" as such terms are  used in 48 C.F.R. 12.212 (SEPT 1995) 
 * and is provided to the U.S. Government only as a commercial end item.  
 * Consistent with 48 C.F.R.12.212 and 48 C.F.R. 227.7202-1 through 
 * 227.7202-4 (JUNE 1995), all U.S. Government End Users acquire the 
 * source code with only those rights set forth herein. 
 *
 * Any use of this source code in individual and commercial software must 
 * include, in the user documentation and internal comments to the code,
 * the above Disclaimer and U.S. Government End Users Notice.
 */

#if !defined(__DRIVER_TYPES_H__)
#define __DRIVER_TYPES_H__

/*******************************************************************************
*                                                                              *
*  TYPE DEFINITIONS USED BY RUNTIME API                                        *
*                                                                              *
*******************************************************************************/

#if !defined(__CUDA_INTERNAL_COMPILATION__)

#include <limits.h>
#include <stddef.h>

#endif /* !__CUDA_INTERNAL_COMPILATION__ */

/*******************************************************************************
*                                                                              *
*                                                                              *
*                                                                              *
*******************************************************************************/

/*DEVICE_BUILTIN*/
enum cudaError
{
  cudaSuccess = 0,
  cudaErrorMissingConfiguration,
  cudaErrorMemoryAllocation,
  cudaErrorInitializationError,
  cudaErrorLaunchFailure,
  cudaErrorPriorLaunchFailure,
  cudaErrorLaunchTimeout,
  cudaErrorLaunchOutOfResources,
  cudaErrorInvalidDeviceFunction,
  cudaErrorInvalidConfiguration,
  cudaErrorInvalidDevice,
  cudaErrorInvalidValue,
  cudaErrorInvalidPitchValue,
  cudaErrorInvalidSymbol,
  cudaErrorMapBufferObjectFailed,
  cudaErrorUnmapBufferObjectFailed,
  cudaErrorInvalidHostPointer,
  cudaErrorInvalidDevicePointer,
  cudaErrorInvalidTexture,
  cudaErrorInvalidTextureBinding,
  cudaErrorInvalidChannelDescriptor,
  cudaErrorInvalidMemcpyDirection,
  cudaErrorAddressOfConstant,
  cudaErrorTextureFetchFailed,
  cudaErrorTextureNotBound,
  cudaErrorSynchronizationError,
  cudaErrorInvalidFilterSetting,
  cudaErrorInvalidNormSetting,
  cudaErrorMixedDeviceExecution,
  cudaErrorCudartUnloading,
  cudaErrorUnknown,
  cudaErrorNotYetImplemented,
  cudaErrorMemoryValueTooLarge,
  cudaErrorInvalidResourceHandle,
  cudaErrorNotReady,
  cudaErrorInsufficientDriver,
  cudaErrorSetOnActiveProcess,
  cudaErrorStartupFailure = 0x7f,
  cudaErrorApiFailureBase = 10000
};

/*DEVICE_BUILTIN*/
enum cudaChannelFormatKind
{
  cudaChannelFormatKindSigned,
  cudaChannelFormatKindUnsigned,
  cudaChannelFormatKindFloat,
  cudaChannelFormatKindNone
};

/*DEVICE_BUILTIN*/
struct cudaChannelFormatDesc
{
  int                        x;
  int                        y;
  int                        z;
  int                        w;
  enum cudaChannelFormatKind f;
};

/*DEVICE_BUILTIN*/
struct cudaArray;

/*DEVICE_BUILTIN*/
enum cudaMemcpyKind
{
  cudaMemcpyHostToHost = 0,
  cudaMemcpyHostToDevice,
  cudaMemcpyDeviceToHost,
  cudaMemcpyDeviceToDevice
};

/*DEVICE_BUILTIN*/
struct cudaPitchedPtr
{
  void   *ptr;
  size_t  pitch;
  size_t  xsize;
  size_t  ysize;
};

/*DEVICE_BUILTIN*/
struct cudaExtent
{
  size_t width;
  size_t height;
  size_t depth;
};

/*DEVICE_BUILTIN*/
struct cudaPos
{
  size_t x;
  size_t y;
  size_t z;
};

/*DEVICE_BUILTIN*/
struct cudaMemcpy3DParms
{
  struct cudaArray      *srcArray;
  struct cudaPos         srcPos;
  struct cudaPitchedPtr  srcPtr;

  struct cudaArray      *dstArray;
  struct cudaPos         dstPos;
  struct cudaPitchedPtr  dstPtr;

  struct cudaExtent      extent;
  enum cudaMemcpyKind    kind;
};

/*DEVICE_BUILTIN*/
struct cudaDeviceProp
{
  char   name[256];
  size_t totalGlobalMem;
  size_t sharedMemPerBlock;
  int    regsPerBlock;
  int    warpSize;
  size_t memPitch;
  int    maxThreadsPerBlock;
  int    maxThreadsDim[3];
  int    maxGridSize[3]; 
  int    clockRate;
  size_t totalConstMem; 
  int    major;
  int    minor;
  size_t textureAlignment;
  int    deviceOverlap;
  int    multiProcessorCount;
  int    kernelExecTimeoutEnabled;
  int    __cudaReserved[39];
};

#define cudaDevicePropDontCare                             \
        {                                                  \
          {'\0'},    /* char   name[256];               */ \
          0,         /* size_t totalGlobalMem;          */ \
          0,         /* size_t sharedMemPerBlock;       */ \
          0,         /* int    regsPerBlock;            */ \
          0,         /* int    warpSize;                */ \
          0,         /* size_t memPitch;                */ \
          0,         /* int    maxThreadsPerBlock;      */ \
          {0, 0, 0}, /* int    maxThreadsDim[3];        */ \
          {0, 0, 0}, /* int    maxGridSize[3];          */ \
          0,         /* int    clockRate;               */ \
          0,         /* size_t totalConstMem;           */ \
          -1,        /* int    major;                   */ \
          -1,        /* int    minor;                   */ \
          0,         /* size_t textureAlignment;        */ \
          -1,        /* int    deviceOverlap;           */ \
          0,         /* int    multiProcessorCount;     */ \
          0          /* int    kernelExecTimeoutEnabled */ \
        }

/*******************************************************************************
*                                                                              *
*  SHORTHAND TYPE DEFINITION USED BY RUNTIME API                               *
*                                                                              *
*******************************************************************************/

/*DEVICE_BUILTIN*/
typedef enum cudaError cudaError_t;

/*DEVICE_BUILTIN*/
typedef int cudaStream_t;

/*DEVICE_BUILTIN*/
typedef int cudaEvent_t;

#endif /* !__DRIVER_TYPES_H__ */
