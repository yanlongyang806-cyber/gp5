#ifndef _CRYPTIC_H_
#define _CRYPTIC_H_

#ifdef __cplusplus
extern "C" {
#endif

// Versions
// 1 :
//typedef struct cef_cryptic_override_t {
//int									cef_cryptic_version;		// CRYPTIC_CEF_OVERRIDE_VERSION
//_cef_cryptic_file_mapper_callback		cef_cryptic_file_mapper;	// Memory-mapped file override
//}
// 2:
//typedef struct cef_cryptic_override_t {
//	int									cef_cryptic_version;		// CRYPTIC_CEF_OVERRIDE_VERSION
//	_cef_cryptic_file_mapper_callback	cef_cryptic_file_mapper;	// Memory-mapped file override
//	_malloc_override					cef_cryptic_malloc_override;
//	_free_override						cef_cryptic_free_override;
//} cef_cryptic_override_t;
// 3:
//typedef struct cef_cryptic_override_t {
//	int									cef_cryptic_version;		// CRYPTIC_CEF_OVERRIDE_VERSION
//	_cef_cryptic_file_mapper_callback	cef_cryptic_file_mapper;	// Memory-mapped file override
//  _realloc_override         cef_cryptic_realloc_override;
//	_malloc_override					cef_cryptic_malloc_override;
//	_free_override						cef_cryptic_free_override;
//  _msize_override           cef_cryptic_msize_override;
//} cef_cryptic_override_t;
// 4:
//typedef struct cef_cryptic_override_t {
//	int                                   cef_cryptic_version;		// CRYPTIC_CEF_OVERRIDE_VERSION
//	_cef_cryptic_file_mapper_callback     cef_cryptic_file_mapper;	// Memory-mapped file override
//	_malloc_override                      cef_cryptic_malloc_override;
//	_realloc_override                     cef_cryptic_realloc_override;
//	_free_override                        cef_cryptic_free_override;
//	_msize_override                       cef_cryptic_msize_override;
//	_cef_cryptic_file_release_callback    cef_cryptic_file_release;	// Memory mapped file override, free to match the above alloc
//} cef_cryptic_override_t;
// 5:
//typedef struct cef_cryptic_override_t {
//  int                                   cef_cryptic_version;		// CRYPTIC_CEF_OVERRIDE_VERSION
//  _cef_cryptic_file_mapper_callback     cef_cryptic_file_mapper;	// Memory-mapped file override
//  _malloc_override                      cef_cryptic_malloc_override;
//  _realloc_override                     cef_cryptic_realloc_override;
//  _free_override                        cef_cryptic_free_override;
//  _msize_override                       cef_cryptic_msize_override;
//  _cef_cryptic_file_release_callback    cef_cryptic_file_release;	// Memory mapped file override, free to match the above alloc
//  _cef_cryptic_crash_callback           cef_cryptic_report_crash;	// Sends a thread crash on to CrypticError
//} cef_cryptic_override_t;

typedef unsigned char *(*_cef_cryptic_file_mapper_callback)(const wchar_t *filename, size_t *length);

typedef void* (*_malloc_override)(size_t size);
typedef void* (*_realloc_override)(void *ptr, size_t size);
typedef void (*_free_override)(void *ptr);
typedef size_t (*_msize_override)(void *ptr);

typedef void (*_cef_cryptic_file_release_callback)(void *ptr);

typedef int (*_cef_cryptic_crash_callback)(unsigned long exc_code, struct _EXCEPTION_POINTERS *exc_info);

///
// Cryptic extension
// This struct is used for Cryptic-specific application extensions to CEF.
///
typedef struct cef_cryptic_override_t {
  int                                   cef_cryptic_version;		// CRYPTIC_CEF_OVERRIDE_VERSION
  _cef_cryptic_file_mapper_callback     cef_cryptic_file_mapper;	// Memory-mapped file override
  _malloc_override                      cef_cryptic_malloc_override;
  _realloc_override                     cef_cryptic_realloc_override;
  _free_override                        cef_cryptic_free_override;
  _msize_override                       cef_cryptic_msize_override;
  _cef_cryptic_file_release_callback    cef_cryptic_file_release;	// Memory mapped file override, free to match the above alloc
  _cef_cryptic_crash_callback           cef_cryptic_report_crash;	// Sends a thread crash on to CrypticError
} cef_cryptic_override_t;

// called internally in libcef - not for external use
cef_cryptic_override_t* __cdecl get_cryptic_override(void);
void set_cryptic_override(HMODULE exeModuleHandle);

// your .exe needs to implement a C function of the name "cef_cryptic_override" of the type "cef_cryptic_override_func_ptr_t".
// this function must be exported, as it is retrieved by GetProcAddress(exeModuleHandle, "cef_cryptic_override")
typedef void (*cef_cryptic_override_func_ptr_t)(cef_cryptic_override_t *override);

#ifdef __cplusplus
} // extern "C"
#endif

#endif