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

// ----------------------------------------------------------------------------
// 
// Main public header file for the CompUte Device Api
//
// ----------------------------------------------------------------------------

#ifndef __cuda_cuda_h__
#define __cuda_cuda_h__


/* CUDA API version number */
#define CUDA_VERSION 2010 /* 2.1 */

#ifdef __cplusplus
extern "C" {
#endif
    typedef unsigned int CUdeviceptr; 

    typedef int CUdevice; 
    typedef struct CUctx_st *CUcontext;
    typedef struct CUmod_st *CUmodule;
    typedef struct CUfunc_st *CUfunction;
    typedef struct CUarray_st *CUarray;
    typedef struct CUtexref_st *CUtexref;
    typedef struct CUevent_st *CUevent;
    typedef struct CUstream_st *CUstream;

/************************************
 **
 **    Enums
 **
 ***********************************/

//
// context creation flags
//
typedef enum CUctx_flags_enum {
    CU_CTX_SCHED_AUTO  = 0,
    CU_CTX_SCHED_SPIN  = 1,
    CU_CTX_SCHED_YIELD = 2,
    CU_CTX_SCHED_MASK  = 0x3,
    CU_CTX_FLAGS_MASK  = CU_CTX_SCHED_MASK
} CUctx_flags;

//
// array formats
//
typedef enum CUarray_format_enum {
    CU_AD_FORMAT_UNSIGNED_INT8  = 0x01,
    CU_AD_FORMAT_UNSIGNED_INT16 = 0x02,
    CU_AD_FORMAT_UNSIGNED_INT32 = 0x03,
    CU_AD_FORMAT_SIGNED_INT8    = 0x08,
    CU_AD_FORMAT_SIGNED_INT16   = 0x09,
    CU_AD_FORMAT_SIGNED_INT32   = 0x0a,
    CU_AD_FORMAT_HALF           = 0x10,
    CU_AD_FORMAT_FLOAT          = 0x20
} CUarray_format;

//
// Texture reference addressing modes
//
typedef enum CUaddress_mode_enum {
    CU_TR_ADDRESS_MODE_WRAP = 0,
    CU_TR_ADDRESS_MODE_CLAMP = 1,
    CU_TR_ADDRESS_MODE_MIRROR = 2,
} CUaddress_mode;

//
// Texture reference filtering modes
//
typedef enum CUfilter_mode_enum {
    CU_TR_FILTER_MODE_POINT = 0,
    CU_TR_FILTER_MODE_LINEAR = 1
} CUfilter_mode;

//
// Device properties
//
typedef enum CUdevice_attribute_enum {
    CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK = 1,
    CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X = 2,
    CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y = 3,
    CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z = 4,
    CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X = 5,
    CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y = 6,
    CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z = 7,
    CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK = 8,
    CU_DEVICE_ATTRIBUTE_SHARED_MEMORY_PER_BLOCK = 8,      // Deprecated, use CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK
    CU_DEVICE_ATTRIBUTE_TOTAL_CONSTANT_MEMORY = 9,
    CU_DEVICE_ATTRIBUTE_WARP_SIZE = 10,
    CU_DEVICE_ATTRIBUTE_MAX_PITCH = 11,
    CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_BLOCK = 12,
    CU_DEVICE_ATTRIBUTE_REGISTERS_PER_BLOCK = 12,         // Deprecated, use CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_BLOCK
    CU_DEVICE_ATTRIBUTE_CLOCK_RATE = 13,
    CU_DEVICE_ATTRIBUTE_TEXTURE_ALIGNMENT = 14,

    CU_DEVICE_ATTRIBUTE_GPU_OVERLAP = 15,
    CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT = 16,
    CU_DEVICE_ATTRIBUTE_KERNEL_EXEC_TIMEOUT = 17
} CUdevice_attribute;

//
// Legacy device properties
//
typedef struct CUdevprop_st {
    int maxThreadsPerBlock;
    int maxThreadsDim[3];
    int maxGridSize[3]; 
    int sharedMemPerBlock;
    int totalConstantMemory;
    int SIMDWidth;
    int memPitch;
    int regsPerBlock;
    int clockRate;
    int textureAlign;
} CUdevprop;

//
// Memory types
//
typedef enum CUmemorytype_enum {
    CU_MEMORYTYPE_HOST = 0x01,
    CU_MEMORYTYPE_DEVICE = 0x02,
    CU_MEMORYTYPE_ARRAY = 0x03
} CUmemorytype;


//
// Online compiler options
//
typedef enum CUjit_option_enum
{
    // CU_JIT_MAX_REGISTERS - Max number of registers that a thread may use.
    CU_JIT_MAX_REGISTERS            = 0,

    // CU_JIT_THREADS_PER_BLOCK -
    // IN: Specifies minimum number of threads per block to target compilation for
    // OUT: Returns the number of threads the compiler actually targeted.  This
    // restricts the resource utilization fo the compiler (e.g. max registers) such
    // that a block with the given number of threads should be able to launch based
    // on register limitations.  Note, this option does not currently take into
    // account any other resource limitations, such as shared memory utilization.
    CU_JIT_THREADS_PER_BLOCK,

    // CU_JIT_WALL_TIME - returns a float value in the option of the wall clock
    // time, in milliseconds, spent creating the cubin
    CU_JIT_WALL_TIME,

    // CU_JIT_INFO_LUG_BUFFER - pointer to a buffer in which to print any log
    // messsages from PTXAS that are informational in nature
    CU_JIT_INFO_LOG_BUFFER,

    // CU_JIT_INFO_LOG_BUFFER_SIZE_BYTES -
    // IN: Log buffer size in bytes.  Log messages will be capped at this size
    // (including null terminator)
    // OUT: Amount of log buffer filled with messages
    CU_JIT_INFO_LOG_BUFFER_SIZE_BYTES,

    // CU_JIT_ERROR_LOG_BUFFER - pointer to a buffer in which to print any log
    // messages from PTXAS that reflect errors
    CU_JIT_ERROR_LOG_BUFFER,

    // CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES -
    // IN: Log buffer size in bytes.  Log messages will be capped at this size
    // (including null terminator)
    // OUT: Amount of log buffer filled with messages
    CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES,

    // CU_JIT_OPTIMIZATION_LEVEL - level of optimizations to apply to generated
    // code (0 - 4), with 4 being the default and highest level of optimizations.
    CU_JIT_OPTIMIZATION_LEVEL,

    // CU_JIT_TARGET_FROM_CU_CONTEXT - no option value required.  Determines
    // the target based on the current attached context (default)
    CU_JIT_TARGET_FROM_CUCONTEXT,

    // CU_JIT_TARGET - target is chosen based on supplied CUjit_target_enum.
    CU_JIT_TARGET,

    // CU_JIT_FALLBACK_STRATEGY - specifies choice of fallback strategy if
    // matching cubin is not found.  Choice is based on supplied 
    // CUjit_fallback_enum.
    CU_JIT_FALLBACK_STRATEGY
    
} CUjit_option;

//
// Online compilation targets
//
typedef enum CUjit_target_enum
{
    CU_TARGET_COMPUTE_10            = 0,
    CU_TARGET_COMPUTE_11,
    CU_TARGET_COMPUTE_12,
    CU_TARGET_COMPUTE_13
} CUjit_target;

//
// Cubin matching fallback strategies
//
typedef enum CUjit_fallback_enum
{
    // prefer to compile ptx
    CU_PREFER_PTX                   = 0,

    // prefer to fall back to compatible binary code
    CU_PREFER_BINARY

} CUjit_fallback;

/************************************
 **
 **    Error codes
 **
 ***********************************/

typedef enum cudaError_enum {

    CUDA_SUCCESS                    = 0,
    CUDA_ERROR_INVALID_VALUE        = 1,
    CUDA_ERROR_OUT_OF_MEMORY        = 2,
    CUDA_ERROR_NOT_INITIALIZED      = 3,
    CUDA_ERROR_DEINITIALIZED        = 4,

    CUDA_ERROR_NO_DEVICE            = 100,
    CUDA_ERROR_INVALID_DEVICE       = 101,

    CUDA_ERROR_INVALID_IMAGE        = 200,
    CUDA_ERROR_INVALID_CONTEXT      = 201,
    CUDA_ERROR_CONTEXT_ALREADY_CURRENT = 202,
    CUDA_ERROR_MAP_FAILED           = 205,
    CUDA_ERROR_UNMAP_FAILED         = 206,
    CUDA_ERROR_ARRAY_IS_MAPPED      = 207,
    CUDA_ERROR_ALREADY_MAPPED       = 208,
    CUDA_ERROR_NO_BINARY_FOR_GPU    = 209,
    CUDA_ERROR_ALREADY_ACQUIRED     = 210,
    CUDA_ERROR_NOT_MAPPED           = 211,

    CUDA_ERROR_INVALID_SOURCE       = 300,
    CUDA_ERROR_FILE_NOT_FOUND       = 301,

    CUDA_ERROR_INVALID_HANDLE       = 400,

    CUDA_ERROR_NOT_FOUND            = 500,

    CUDA_ERROR_NOT_READY            = 600,

    CUDA_ERROR_LAUNCH_FAILED        = 700,
    CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES = 701,
    CUDA_ERROR_LAUNCH_TIMEOUT       = 702,
    CUDA_ERROR_LAUNCH_INCOMPATIBLE_TEXTURING = 703,

    CUDA_ERROR_UNKNOWN              = 999
} CUresult;

#ifdef _WIN32
#define CUDAAPI __stdcall
#else
#define CUDAAPI 
#endif

    /*********************************
     ** Initialization
     *********************************/
    CUresult  CUDAAPI cuInit(unsigned int Flags);

    /************************************
     **
     **    Device management
     **
     ***********************************/
   
    CUresult  CUDAAPI cuDeviceGet(CUdevice *device, int ordinal);
    CUresult  CUDAAPI cuDeviceGetCount(int *count);
    CUresult  CUDAAPI cuDeviceGetName(char *name, int len, CUdevice dev);
    CUresult  CUDAAPI cuDeviceComputeCapability(int *major, int *minor, CUdevice dev);
    CUresult  CUDAAPI cuDeviceTotalMem(unsigned int *bytes, CUdevice dev);
    CUresult  CUDAAPI cuDeviceGetProperties(CUdevprop *prop, CUdevice dev);
    CUresult  CUDAAPI cuDeviceGetAttribute(int *pi, CUdevice_attribute attrib, CUdevice dev);
        
    /************************************
     **
     **    Context management
     **
     ***********************************/

    CUresult  CUDAAPI cuCtxCreate(CUcontext *pctx, unsigned int flags, CUdevice dev );
    CUresult  CUDAAPI cuCtxDestroy( CUcontext ctx );
    CUresult  CUDAAPI cuCtxAttach(CUcontext *pctx, unsigned int flags);
    CUresult  CUDAAPI cuCtxDetach(CUcontext ctx);
    CUresult  CUDAAPI cuCtxPushCurrent( CUcontext ctx );
    CUresult  CUDAAPI cuCtxPopCurrent( CUcontext *pctx );
    CUresult  CUDAAPI cuCtxGetDevice(CUdevice *device);
    CUresult  CUDAAPI cuCtxSynchronize(void);


    /************************************
     **
     **    Module management
     **
     ***********************************/
    
    CUresult  CUDAAPI cuModuleLoad(CUmodule *module, const char *fname);
    CUresult  CUDAAPI cuModuleLoadData(CUmodule *module, const void *image);
    CUresult  CUDAAPI cuModuleLoadDataEx(CUmodule *module, const void *image, unsigned int numOptions, CUjit_option *options, void **optionValues);
    CUresult  CUDAAPI cuModuleLoadFatBinary(CUmodule *module, const void *fatCubin);
    CUresult  CUDAAPI cuModuleUnload(CUmodule hmod);
    CUresult  CUDAAPI cuModuleGetFunction(CUfunction *hfunc, CUmodule hmod, const char *name);
    CUresult  CUDAAPI cuModuleGetGlobal(CUdeviceptr *dptr, unsigned int *bytes, CUmodule hmod, const char *name);
    CUresult  CUDAAPI cuModuleGetTexRef(CUtexref *pTexRef, CUmodule hmod, const char *name);
    
    /************************************
     **
     **    Memory management
     **
     ***********************************/
    
    CUresult CUDAAPI cuMemGetInfo(unsigned int *free, unsigned int *total);

    CUresult CUDAAPI cuMemAlloc( CUdeviceptr *dptr, unsigned int bytesize);
    CUresult CUDAAPI cuMemAllocPitch( CUdeviceptr *dptr, 
                                      unsigned int *pPitch,
                                      unsigned int WidthInBytes, 
                                      unsigned int Height, 
                                      // size of biggest r/w to be performed by kernels on this memory
                                      // 4, 8 or 16 bytes
                                      unsigned int ElementSizeBytes
                                     );
    CUresult CUDAAPI cuMemFree(CUdeviceptr dptr);
    CUresult CUDAAPI cuMemGetAddressRange( CUdeviceptr *pbase, unsigned int *psize, CUdeviceptr dptr );

    CUresult CUDAAPI cuMemAllocHost(void **pp, unsigned int bytesize);
    CUresult CUDAAPI cuMemFreeHost(void *p);

    /************************************
     **
     **    Synchronous Memcpy
     **
     ** Intra-device memcpy's done with these functions may execute in parallel with the CPU,
     ** but if host memory is involved, they wait until the copy is done before returning.
     **
     ***********************************/

    // 1D functions
        // system <-> device memory
        CUresult  CUDAAPI cuMemcpyHtoD (CUdeviceptr dstDevice, const void *srcHost, unsigned int ByteCount );
        CUresult  CUDAAPI cuMemcpyDtoH (void *dstHost, CUdeviceptr srcDevice, unsigned int ByteCount );

        // device <-> device memory
        CUresult  CUDAAPI cuMemcpyDtoD (CUdeviceptr dstDevice, CUdeviceptr srcDevice, unsigned int ByteCount );

        // device <-> array memory
        CUresult  CUDAAPI cuMemcpyDtoA ( CUarray dstArray, unsigned int dstIndex, CUdeviceptr srcDevice, unsigned int ByteCount );
        CUresult  CUDAAPI cuMemcpyAtoD ( CUdeviceptr dstDevice, CUarray hSrc, unsigned int SrcIndex, unsigned int ByteCount );

        // system <-> array memory
        CUresult  CUDAAPI cuMemcpyHtoA( CUarray dstArray, unsigned int dstIndex, const void *pSrc, unsigned int ByteCount );
        CUresult  CUDAAPI cuMemcpyAtoH( void *dstHost, CUarray srcArray, unsigned int srcIndex, unsigned int ByteCount );

        // array <-> array memory
        CUresult  CUDAAPI cuMemcpyAtoA( CUarray dstArray, unsigned int dstIndex, CUarray srcArray, unsigned int srcIndex, unsigned int ByteCount );

    // 2D memcpy

        typedef struct CUDA_MEMCPY2D_st {

            unsigned int srcXInBytes, srcY;
            CUmemorytype srcMemoryType;
                const void *srcHost;
                CUdeviceptr srcDevice;
                CUarray srcArray;
                unsigned int srcPitch; // ignored when src is array

            unsigned int dstXInBytes, dstY;
            CUmemorytype dstMemoryType;
                void *dstHost;
                CUdeviceptr dstDevice;
                CUarray dstArray;
                unsigned int dstPitch; // ignored when dst is array

            unsigned int WidthInBytes;
            unsigned int Height;
        } CUDA_MEMCPY2D;
        CUresult  CUDAAPI cuMemcpy2D( const CUDA_MEMCPY2D *pCopy );
        CUresult  CUDAAPI cuMemcpy2DUnaligned( const CUDA_MEMCPY2D *pCopy );

    // 3D memcpy

        typedef struct CUDA_MEMCPY3D_st {

            unsigned int srcXInBytes, srcY, srcZ;
            unsigned int srcLOD;
            CUmemorytype srcMemoryType;
                const void *srcHost;
                CUdeviceptr srcDevice;
                CUarray srcArray;
                void *reserved0;        // must be NULL
                unsigned int srcPitch;  // ignored when src is array
                unsigned int srcHeight; // ignored when src is array; may be 0 if Depth==1

            unsigned int dstXInBytes, dstY, dstZ;
            unsigned int dstLOD;
            CUmemorytype dstMemoryType;
                void *dstHost;
                CUdeviceptr dstDevice;
                CUarray dstArray;
                void *reserved1;        // must be NULL
                unsigned int dstPitch;  // ignored when dst is array
                unsigned int dstHeight; // ignored when dst is array; may be 0 if Depth==1

            unsigned int WidthInBytes;
            unsigned int Height;
            unsigned int Depth;
        } CUDA_MEMCPY3D;
        CUresult  CUDAAPI cuMemcpy3D( const CUDA_MEMCPY3D *pCopy );

    /************************************
     **
     **    Asynchronous Memcpy
     **
     ** Any host memory involved must be DMA'able (e.g., allocated with cuMemAllocHost).
     ** memcpy's done with these functions execute in parallel with the CPU and, if
     ** the hardware is available, may execute in parallel with the GPU.
     ** Asynchronous memcpy must be accompanied by appropriate stream synchronization.
     **
     ***********************************/

    // 1D functions
        // system <-> device memory
        CUresult  CUDAAPI cuMemcpyHtoDAsync (CUdeviceptr dstDevice, 
            const void *srcHost, unsigned int ByteCount, CUstream hStream );
        CUresult  CUDAAPI cuMemcpyDtoHAsync (void *dstHost, 
            CUdeviceptr srcDevice, unsigned int ByteCount, CUstream hStream );

        // system <-> array memory
        CUresult  CUDAAPI cuMemcpyHtoAAsync( CUarray dstArray, unsigned int dstIndex, 
            const void *pSrc, unsigned int ByteCount, CUstream hStream );
        CUresult  CUDAAPI cuMemcpyAtoHAsync( void *dstHost, CUarray srcArray, unsigned int srcIndex, 
            unsigned int ByteCount, CUstream hStream );

        // 2D memcpy
        CUresult  CUDAAPI cuMemcpy2DAsync( const CUDA_MEMCPY2D *pCopy, CUstream hStream );

        // 3D memcpy
        CUresult  CUDAAPI cuMemcpy3DAsync( const CUDA_MEMCPY3D *pCopy, CUstream hStream );

    /************************************
     **
     **    Memset
     **
     ***********************************/
        CUresult  CUDAAPI cuMemsetD8( CUdeviceptr dstDevice, unsigned char uc, unsigned int N );
        CUresult  CUDAAPI cuMemsetD16( CUdeviceptr dstDevice, unsigned short us, unsigned int N );
        CUresult  CUDAAPI cuMemsetD32( CUdeviceptr dstDevice, unsigned int ui, unsigned int N );

        CUresult  CUDAAPI cuMemsetD2D8( CUdeviceptr dstDevice, unsigned int dstPitch, unsigned char uc, unsigned int Width, unsigned int Height );
        CUresult  CUDAAPI cuMemsetD2D16( CUdeviceptr dstDevice, unsigned int dstPitch, unsigned short us, unsigned int Width, unsigned int Height );
        CUresult  CUDAAPI cuMemsetD2D32( CUdeviceptr dstDevice, unsigned int dstPitch, unsigned int ui, unsigned int Width, unsigned int Height );

    /************************************
     **
     **    Function management
     **
     ***********************************/


    CUresult CUDAAPI cuFuncSetBlockShape (CUfunction hfunc, int x, int y, int z);
    CUresult CUDAAPI cuFuncSetSharedSize (CUfunction hfunc, unsigned int bytes);

    /************************************
     **
     **    Array management 
     **
     ***********************************/
   
    typedef struct
    {
        //
        // dimensions
        //            
            unsigned int Width;
            unsigned int Height;
            
        //
        // format
        //
            CUarray_format Format;
        
            // channels per array element
            unsigned int NumChannels;
    } CUDA_ARRAY_DESCRIPTOR;

    CUresult  CUDAAPI cuArrayCreate( CUarray *pHandle, const CUDA_ARRAY_DESCRIPTOR *pAllocateArray );
    CUresult  CUDAAPI cuArrayGetDescriptor( CUDA_ARRAY_DESCRIPTOR *pArrayDescriptor, CUarray hArray );
    CUresult  CUDAAPI cuArrayDestroy( CUarray hArray );

    typedef struct
    {
        //
        // dimensions
        //
            unsigned int Width;
            unsigned int Height;
            unsigned int Depth;
        //
        // format
        //
            CUarray_format Format;
        
            // channels per array element
            unsigned int NumChannels;
        //
        // flags
        //
            unsigned int Flags;

    } CUDA_ARRAY3D_DESCRIPTOR;
    CUresult  CUDAAPI cuArray3DCreate( CUarray *pHandle, const CUDA_ARRAY3D_DESCRIPTOR *pAllocateArray );
    CUresult  CUDAAPI cuArray3DGetDescriptor( CUDA_ARRAY3D_DESCRIPTOR *pArrayDescriptor, CUarray hArray );

    /************************************
     **
     **    Texture reference management
     **
     ***********************************/
    CUresult  CUDAAPI cuTexRefCreate( CUtexref *pTexRef );
    CUresult  CUDAAPI cuTexRefDestroy( CUtexref hTexRef );
    
    CUresult  CUDAAPI cuTexRefSetArray( CUtexref hTexRef, CUarray hArray, unsigned int Flags );
        // override the texref format with a format inferred from the array
        #define CU_TRSA_OVERRIDE_FORMAT 0x01
    CUresult  CUDAAPI cuTexRefSetAddress( unsigned int *ByteOffset, CUtexref hTexRef, CUdeviceptr dptr, unsigned int bytes );
    CUresult  CUDAAPI cuTexRefSetFormat( CUtexref hTexRef, CUarray_format fmt, int NumPackedComponents );
    
    CUresult  CUDAAPI cuTexRefSetAddressMode( CUtexref hTexRef, int dim, CUaddress_mode am );
    CUresult  CUDAAPI cuTexRefSetFilterMode( CUtexref hTexRef, CUfilter_mode fm );
    CUresult  CUDAAPI cuTexRefSetFlags( CUtexref hTexRef, unsigned int Flags );
        // read the texture as integers rather than promoting the values
        // to floats in the range [0,1]
        #define CU_TRSF_READ_AS_INTEGER         0x01

        // use normalized texture coordinates in the range [0,1) instead of [0,dim)
        #define CU_TRSF_NORMALIZED_COORDINATES  0x02

    CUresult  CUDAAPI cuTexRefGetAddress( CUdeviceptr *pdptr, CUtexref hTexRef );
    CUresult  CUDAAPI cuTexRefGetArray( CUarray *phArray, CUtexref hTexRef );
    CUresult  CUDAAPI cuTexRefGetAddressMode( CUaddress_mode *pam, CUtexref hTexRef, int dim );
    CUresult  CUDAAPI cuTexRefGetFilterMode( CUfilter_mode *pfm, CUtexref hTexRef );
    CUresult  CUDAAPI cuTexRefGetFormat( CUarray_format *pFormat, int *pNumChannels, CUtexref hTexRef );
    CUresult  CUDAAPI cuTexRefGetFlags( unsigned int *pFlags, CUtexref hTexRef );

    /************************************
     **
     **    Parameter management
     **
     ***********************************/

    CUresult  CUDAAPI cuParamSetSize (CUfunction hfunc, unsigned int numbytes);
    CUresult  CUDAAPI cuParamSeti    (CUfunction hfunc, int offset, unsigned int value);
    CUresult  CUDAAPI cuParamSetf    (CUfunction hfunc, int offset, float value);
    CUresult  CUDAAPI cuParamSetv    (CUfunction hfunc, int offset, void * ptr, unsigned int numbytes);
    CUresult  CUDAAPI cuParamSetTexRef(CUfunction hfunc, int texunit, CUtexref hTexRef);
        // for texture references loaded into the module,
        // use default texunit from texture reference
        #define CU_PARAM_TR_DEFAULT -1

    /************************************
     **
     **    Launch functions
     **
     ***********************************/

    CUresult CUDAAPI cuLaunch ( CUfunction f );
    CUresult CUDAAPI cuLaunchGrid (CUfunction f, int grid_width, int grid_height);
    CUresult CUDAAPI cuLaunchGridAsync( CUfunction f, int grid_width, int grid_height, CUstream hStream );

    /************************************
     **
     **    Events
     **
     ***********************************/
    CUresult CUDAAPI cuEventCreate( CUevent *phEvent, unsigned int Flags );
    CUresult CUDAAPI cuEventRecord( CUevent hEvent, CUstream hStream );
    CUresult CUDAAPI cuEventQuery( CUevent hEvent );
    CUresult CUDAAPI cuEventSynchronize( CUevent hEvent );
    CUresult CUDAAPI cuEventDestroy( CUevent hEvent );
    CUresult CUDAAPI cuEventElapsedTime( float *pMilliseconds, CUevent hStart, CUevent hEnd );

    /************************************
     **
     **    Streams
     **
     ***********************************/
    CUresult CUDAAPI  cuStreamCreate( CUstream *phStream, unsigned int Flags );
    CUresult CUDAAPI  cuStreamQuery( CUstream hStream );
    CUresult CUDAAPI  cuStreamSynchronize( CUstream hStream );
    CUresult CUDAAPI  cuStreamDestroy( CUstream hStream );

#ifdef __cplusplus
}
#endif

#endif /* __cuda_cuda_h__ */

