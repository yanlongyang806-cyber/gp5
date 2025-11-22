

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 7.00.0555 */
/* at Fri Sep 20 18:04:47 2013
 */
/* Compiler settings for PatchShellMenu.idl:
    Oicf, W1, Zp8, env=Win64 (32b run), target_arch=AMD64 7.00.0555 
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#pragma warning( disable: 4049 )  /* more than 64k source lines */


/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif // __RPCNDR_H_VERSION__

#ifndef COM_NO_WINDOWS_H
#include "windows.h"
#include "ole2.h"
#endif /*COM_NO_WINDOWS_H*/

#ifndef __PatchShellMenu_h__
#define __PatchShellMenu_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __IPatchShellMenuExt_FWD_DEFINED__
#define __IPatchShellMenuExt_FWD_DEFINED__
typedef interface IPatchShellMenuExt IPatchShellMenuExt;
#endif 	/* __IPatchShellMenuExt_FWD_DEFINED__ */


#ifndef __PatchShellMenuExt_FWD_DEFINED__
#define __PatchShellMenuExt_FWD_DEFINED__

#ifdef __cplusplus
typedef class PatchShellMenuExt PatchShellMenuExt;
#else
typedef struct PatchShellMenuExt PatchShellMenuExt;
#endif /* __cplusplus */

#endif 	/* __PatchShellMenuExt_FWD_DEFINED__ */


/* header files for imported files */
#include "oaidl.h"
#include "ocidl.h"

#ifdef __cplusplus
extern "C"{
#endif 


#ifndef __IPatchShellMenuExt_INTERFACE_DEFINED__
#define __IPatchShellMenuExt_INTERFACE_DEFINED__

/* interface IPatchShellMenuExt */
/* [unique][helpstring][uuid][object] */ 


EXTERN_C const IID IID_IPatchShellMenuExt;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("0785862B-66DE-4B0B-B348-36657C90FCA9")
    IPatchShellMenuExt : public IUnknown
    {
    public:
    };
    
#else 	/* C style interface */

    typedef struct IPatchShellMenuExtVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IPatchShellMenuExt * This,
            /* [in] */ REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IPatchShellMenuExt * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IPatchShellMenuExt * This);
        
        END_INTERFACE
    } IPatchShellMenuExtVtbl;

    interface IPatchShellMenuExt
    {
        CONST_VTBL struct IPatchShellMenuExtVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IPatchShellMenuExt_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IPatchShellMenuExt_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IPatchShellMenuExt_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IPatchShellMenuExt_INTERFACE_DEFINED__ */



#ifndef __PatchShellMenuLib_LIBRARY_DEFINED__
#define __PatchShellMenuLib_LIBRARY_DEFINED__

/* library PatchShellMenuLib */
/* [helpstring][version][uuid] */ 


EXTERN_C const IID LIBID_PatchShellMenuLib;

EXTERN_C const CLSID CLSID_PatchShellMenuExt;

#ifdef __cplusplus

class DECLSPEC_UUID("FFF0A0BE-677B-454C-B712-6949FB082279")
PatchShellMenuExt;
#endif
#endif /* __PatchShellMenuLib_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


