/*---------------------
// 
// Project:     GluaX   
//
// Purpose:     Interfacing C code to LUA scripting language.
//
// License:     
//    Zlib license (same as original 'glua10', see 'license.txt')
//
// Credits:
//    Based on 'glua10' package by Enrico Colombini.
//
// Notes:
//    Apart from the 'argn' and 'tag' parameters (which should be constant
//    values, without arithmetics), the macros within this header file have been
//    designed to be without side effects. This especially applies to the 'push'
//    macros, where it is often handy to place arithmetic operations or
//    function calls right within the macro parameters.
//
// Bugs:
//    (none known)
//
----------------------*/
/*
  GluaX: Copyright (c) 2002-03 Asko Kauppi. All rights reserved.

  Glua 1.0: Copyright (c) 2002 Enrico Colombini. All rights reserved.

  This software, including its documentation, is provided 'as-is',
  without any express or implied warranty. In no event will the author
  be held liable for any damages arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.

  2. Altered source versions must be plainly marked as such, and must not
     be misrepresented as being the original software.

  3. This notice may not be removed or altered from any source distribution.

  http://www.opensource.org/licenses/zlib-license.php
*/

#ifndef GLUAX_H
#define GLUAX_H

/* CRYPTIC SETTINGS */
#define GLUA_STATIC
#define PLATFORM_WIN32

#include "timing_profiler.h"


#ifdef __cplusplus
  extern "C" {
#endif

#include <limits.h>     // 'MAXINT' or 'INT_MAX'
#include <stdlib.h>	// 'abort()'
#include <stdarg.h>     // 'va_list'
#include <assert.h>

//---
// Declare 'PLATFORM_xxx' and 'COMPILER_xxx' defines:
//
// Note: Is there a way to autodetect WinCE from regular Windows?
//
#ifdef _WIN32_WCE  // WinCE
  #define PLATFORM_WINCE
  #define PLATFORM_NAME "wince"
#elif (defined _WIN32)
  #define PLATFORM_WIN32
  #define PLATFORM_NAME "Win32"
#elif (defined __linux__)
  #define PLATFORM_LINUX
  #define PLATFORM_NAME "linux"
#elif (defined __APPLE__) && (defined __MACH__)
  #define PLATFORM_DARWIN
  #define PLATFORM_NAME "darwin"
#elif (defined __NetBSD__) || (defined __FreeBSD__) || (defined BSD)
  #define PLATFORM_BSD
  #define PLATFORM_NAME "bsd"
#elif (defined __QNX__)
  #define PLATFORM_QNX
  #define PLATFORM_NAME "qnx"
#else
  #error "Unknown platform!"
  #define PLATFORM_NAME NULL
#endif

#ifdef PLATFORM_WINCE   // Embedded Visual C++
  #define COMPILER_MSC
  #define COMPILER_NAME "evc"
  #include <wtypes.h>   // WCHAR
  #include <assert.h>
  #undef _HAS_VA_ARGS   // :(
  //
#elif defined(_MSC_VER)   // Visual C++
  #define COMPILER_MSC
  #define COMPILER_NAME "msc"
  #undef _HAS_VA_ARGS   // no __VA_ARGS__ or anything similar on MSC, right?
  //
#elif (defined __GNUC__)    // Gcc
  #if (__GNUC__ < 3)
    #error "gcc 3.x preferred!  Comment out this line and read Release Notes to proceed."
	//#undef _HAS_VA_ARGS
  #else
  	#define _HAS_VA_ARGS
  #endif
  //
  #define COMPILER_GCC
  #define COMPILER_NAME "gcc"
  //
#else   // Other compilers (not officially supported!)
  #error "Unknown compiler! (proceed at your own risk)"
  #define COMPILER_NAME "unknown"

  #if (defined __STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)  // C99
    #define _HAS_VA_ARGS	// Variadic preprocessor support (#define macro(...))
  #endif
#endif

#ifndef MAXINT
  #define MAXINT INT_MAX
  //
  #ifndef MAXINT
    #error "'MAXINT' or 'INT_MAX' needs to be #defined!"
  #endif
#endif

// Using 'bool_int' instead of plain 'bool' (which is a built-in type in C++)
// may be useful, since we are using the type in the binary interface.
//
typedef unsigned int bool_int;   // for TRUE and FALSE contents
typedef unsigned char bool8;

#ifndef TRUE
  #define TRUE  1
  #define FALSE 0
#endif

typedef unsigned char uint8;

#if (defined PLATFORM_WIN32) || (defined PLATFORM_WINCE) || (defined PLATFORM_QNX)
  typedef unsigned int uint;
  typedef unsigned long ulong;
#else
  #include <sys/types.h>
  #if (defined PLATFORM_DARWIN) || (defined __FreeBSD__)
    typedef unsigned long ulong;  // Linux has this in 'sys/types.h'
  #endif
#endif

#ifdef COMPILER_GCC
  typedef long long longlong;
  typedef unsigned long long ulonglong;
#elif defined( COMPILER_MSC )
  typedef __int64 longlong;
  typedef unsigned __int64 ulonglong;
#endif

typedef int enum_int;

// This trick should differ tag usage from general integers at compile time?
//
struct s_tagint;
typedef const struct s_tagint* tag_int;  // In reality, 1..N (16-bit)

#ifdef USE_FLOAT
  typedef float glua_num_t;
  #define GLUA_NUM_ZERO (0.0f)
  #define GLUA_NUM_ONE (1.0f)
#else
  typedef double glua_num_t;
  #define GLUA_NUM_ZERO (0.0)
  #define GLUA_NUM_ONE (1.0)
#endif

//---
// Default mode is 'GLUA_DYNAMIC' (unless application #defines 'GLUA_STATIC').
//
#ifdef GLUA_STATIC  // from project options
  #undef GLUA_DYNAMIC
#else
  #define GLUA_DYNAMIC  // default: building a dynamic module
#endif

//---
// To enable dynamic modules, we do NOT #include "lua.h" here.
// All interfacing to Lua engine happens via 'gluax.c'.
//
#ifdef GLUA_DYNAMIC
  //
  typedef struct lua_State lua_State;
  typedef int (*lua_CFunction) (lua_State *L);
  //
  typedef struct lua_Debug lua_Debug;   // gluaport only
  typedef const char * (*lua_Chunkreader) (lua_State *L, void *ud, size_t *sz);  // gluaport only
  //
#else
  // Gcc 2.96 complains if we use both one-line definitions (above) and 
  // include 'lua.h' later. That's why we include it here, although the above
  // definitions would be sufficient.
  //
  #include "lua.h"  
  
  // Differ between the version 4.0 and 5.0:
  //
  #ifdef LUA_ANYTAG	    // (only defined in Lua 4.0)
    #define LUA_V40
  #else
    #define LUA_V50
    #ifdef LUA_GCSTOP       // Lua 5.1-work0 (or later)
      #define LUA51_WORK0
      #ifdef LUA_GCSTEP     // Lua 5.1-work1 (or later)
        #define LUA51_WORK1 
        #ifdef LUA_GCSETPACE   // Lua 5.1-work4 (or later)
          #define LUA51_WORK4
        #endif
      #endif
    #endif
  #endif
  //
#endif

#ifdef __cplusplus
  #define _GLUA_EXTERN_C extern "C"
#else
  #define _GLUA_EXTERN_C /*nothing*/
#endif

//---
// Critical section management:
//
//  static CRITICAL_SECTION my_cs;
//
//  CRITICAL_SECTION_INIT(&my_cs);  // within 'Gluahost()' function, normally
//
//  CRITICAL_SECTION_START(&my_cs)
//      ...
//  CRITICAL_SECTION_END(&my_cs)
//
#if (defined PLATFORM_WIN32) || (defined PLATFORM_WINCE)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    // CRITICAL_SECTION defined by Win32
    #define CRITICAL_SECTION_INIT(ref)    InitializeCriticalSection(ref)
    #define CRITICAL_SECTION_START(ref)   { CRITICAL_SECTION* _cs_ref=(ref); EnterCriticalSection(_cs_ref);
    #define CRITICAL_SECTION_END          LeaveCriticalSection(_cs_ref); }
#else
    #include <pthread.h>
    #define CRITICAL_SECTION pthread_mutex_t
    #define CRITICAL_SECTION_INIT(ref)    pthread_mutex_init(ref,NULL)
    #define CRITICAL_SECTION_START(ref)   { pthread_mutex_t* _cs_ref=(ref); pthread_mutex_lock(_cs_ref);
    #define CRITICAL_SECTION_END          pthread_mutex_unlock(_cs_ref); }
#endif


/*--- Macros etc. ----------------------------------------------------*/

#define /*int abs_index*/ STACK_ABS( L, index ) \
	( (index) >= 0 ? (index) /*absolute*/ : (_glua_gettop(L) +(index) +1) )

#ifdef GLUA_OPT_NOCHECK     // All runtime checks removed
  //
  #define ASSUME(c)         /*nothing*/
  #define ASSUME_L(c)       /*nothing*/
  #define ASSUME_NONZERO(v) /*nothing*/
  #define ASSUME_NEVER()    /*nothing*/     // could also be 'abort()'
  //
  #define STACK_CHECK(L)    /*nothing*/
  #define STACK_END(c)      /*nothing*/
  //
#else   // Runtime safety checks:
  //
  void _glua_assert( lua_State* L, uint line, const char* file, const char* cond_str );

  #define _ASSUME_X(lua,c)  { if (!(c)) _glua_assert( (lua), __LINE__, __FILE__, #c ); }

  #define ASSUME(c)    _ASSUME_X(_lua_,c)   // works within GLUA_FUNC
  #define ASSUME_L(c)  _ASSUME_X(L,c)       // works within gluax.c code
  //
  #define ASSUME_NONZERO    ASSUME
  #define ASSUME_NEVER()    ASSUME(0)
  //
  #define STACK_CHECK(L)     { lua_State* _luastack_= (L); \
                               int _oldtop_= _glua_gettop(L);
  #define STACK_END(change)  { if (_glua_gettop(_luastack_) != _oldtop_+(change)) \
                                  _glua_assert( _luastack_, __LINE__, __FILE__, "STACK_END(" #change ")" ); }}
#endif

#ifndef NEVER
  #define NEVER 0		// 'return NEVER;' etc.
#endif

#ifndef IGNORE_PARAM
  #define IGNORE_PARAM(var) ( (void)(var) )
#endif

//---
// WARNING! Notes on 'strncpy()' and 'strncat()':
//
// If a buffer is filled with 'strncpy()', it does _not_ add the termination
// character in the last slot.
//
// The size param to 'strncat()' is the count of chars to append, _not_ the
// size of the buffer! Curiously enough, this function _does_ terminate the
// buffer at fill-ups.
//
// For these reason, we have our own variants instead of the standard 
// 'strncpy()' and 'strncat()'. These are defined as functions (not macros)
// to keep clear of side-effects.
//
// Note: Also modules using 'gluax.h' may use these for their own purposes.
//
char* _strcpy_safe( char* buf, const char* src, uint buflen );
char* _strcat_safe( char* buf, const char* src, uint buflen );

#if (defined UNICODE) && (defined __cplusplus)
  WCHAR* _wstrcpy_safe( WCHAR* buf, const WCHAR* src, uint buflen );
  WCHAR* _wstrcat_safe( WCHAR* buf, const WCHAR* src, uint buflen );
  //
  } // extern "C"
  inline WCHAR* strcpy_safe( WCHAR* buf, const WCHAR* src, uint buflen ) { return _wstrcpy_safe( buf, src, buflen ); }
  inline WCHAR* strcat_safe( WCHAR* buf, const WCHAR* src, uint buflen ) { return _wstrcat_safe( buf, src, buflen ); }
  inline char* strcpy_safe( char* buf, const char* src, uint buflen ) { return _strcpy_safe( buf, src, buflen ); }
  inline char* strcat_safe( char* buf, const char* src, uint buflen ) { return _strcat_safe( buf, src, buflen ); }
  extern "C" {
#else
  #define strcpy_safe( buf, src, buflen ) _strcpy_safe( buf, src, buflen )
  #define strcat_safe( buf, src, buflen ) _strcat_safe( buf, src, buflen )
#endif


/*--- Data structures ----------------------------------------------------*/

//---
// These constants don't match the 'LUA_Txxx' constants in values. They use
// mask bits to allow easy testing against 'either real or integer number',
// or 'either string or nil' and so on...
//
// DON'T CHANGE THESE VALUES or you'll break backwards compatibility with
// existing modules!
//
enum e_Glua_Type
{
	GLUA_TYPE_NIL=			0x0001,	  // 'nil' or none (empty, default value)
	//
	// Note: 'GLUA_TYPE_NIL' covers both Lua types 'NIL' and 'NONE'.

	GLUA_TYPE_BOOLEAN=		0x0002,	  // (only by Lua 5.0 engine)

	GLUA_TYPE_NUMBER=	    0x0004,	  // floating-point number
	GLUA_TYPE_INT=		    0x0008,   // integer number

	GLUA_TYPE_STRING=		0x0010,	  // text string (may be numeric)
	GLUA_TYPE_TABLE=		0x0020,
	GLUA_TYPE_FUNCTION=		0x0040,
	GLUA_TYPE_ENUM=         0x0080,   // enumeration (uin32 bitfield)  //AK(13-Mar-05)

	GLUA_TYPE_USERDATA=     0x0100,   // userdata matching our tag
	GLUA_TYPE_USERDATA_UNK= 0x0200,   // userdata NOT matching our tag
	
};

//-----
// Usage by different gluax macros (within 'GLUA_DECL' block):
//
// glua_func:       name, func, NULL, NULL, 0.0
// glua_str:        name, NULL, str, NULL, 0.0
// glua_const:      name, NULL, NULL, NULL, num
// glua_tag:        name, NULL, NULL, tag_ref, 0.0
// glua_enum:       name, NULL, NULL, tag_ref, 1.0
// glua_tagmethod:  name, func, str, tag_ref, 0.0
// glua_wrap:       name, NULL, data, NULL, len 
// glua_info:       (like glua_str)
//
// Note: the 'num' field is double despite LuaX number format. This allows
//		 carrying full int32 range in it, and the overhead (if floats were
//		 quicker) only happens at system init.
//
// Warn: If changing that (using 'glua_num_t' for 'num') make sure that
//		 the struct itself is same width regardless of float/double selection.
//
struct s_GluaItem
{
    const char* name;
    int (*func)(lua_State *);   // function entries only
    const char* str;            // constant strings (if 'func' == NULL)
	tag_int* tag_ref; 		// tag entries: where to store the created tag.
    double num_d;           // constant numbers (if others are NULL): always double
};

// Note: 's_GluaItem' needs to be same size, regardless of number type. Otherwise,
//       cross-using float/double modules would fail.
//
#define GLUAITEM_SIZECHECK() \
    ( ( sizeof(void*) == 4 ) && \
      ( sizeof(enum_int) == 4 ) && \
      ( sizeof(float) == 4 ) && \
      ( sizeof(double) == 8 ) && \
      ( sizeof(struct s_GluaItem) == 24 ) )


/*--- Host interface functions -----------------------------*/

void _glua_error( lua_State* L, const char* msg );

void _glua_errorN( lua_State* L, const char* msg, ... );
void _glua_errorL( lua_State* L, const char* msg, va_list args );

int /*host_ver*/ _glua_ver( int module_ver );

#define _glua_type( L, argn )  ( (enum e_Glua_Type)_glua_type2( L, argn, 0 ) )
uint /*e_Glua_Type*/ _glua_type2( lua_State* L, int argn, tag_int ud_tag );

bool_int    _glua_toboolean( lua_State* L, int argn );
double      _glua_tonumber_raw_d( lua_State* L, int argn );
float       _glua_tonumber_raw_f( lua_State* L, int argn );
double      _glua_tonumber_safe_d( lua_State* L, int argn );
float       _glua_tonumber_safe_f( lua_State* L, int argn );
long        _glua_tointeger_raw( lua_State* L, int argn );
ulong       _glua_tounsigned_raw( lua_State* L, int argn );
const char* _glua_tolstring_raw( lua_State* L, int argn, uint* /* size_t* */ len_ref );
const char* _glua_tolstring_safe( lua_State* L, int argn, uint* /* size_t* */ len_ref );
void*       _glua_touserdata( lua_State* L, int argn, tag_int* tagref );
enum_int    _glua_toenum( lua_State* L, int argn, tag_int* tagref );

#define _glua_tostring_safe( L, argn )  _glua_tolstring_safe( L, argn, NULL )
#define _glua_tostring_raw( L, argn )  _glua_tolstring_raw( L, argn, NULL )

/**
long        _glua_toint32( lua_State* L, int argn );
#define _glua_touint32(L,argn)  ( (ulong)_glua_toint32(L,argn) )
longlong _glua_toint64( lua_State* L, int argn );
#define _glua_touint64(L,argn)  ( (ulonglong)_glua_toint64(L,argn) )
**/

void _glua_pushboolean( lua_State* L, bool_int v );
void _glua_pushnumber_d( lua_State* L, double n );
void _glua_pushnumber_f( lua_State* L, float n );
void _glua_pushinteger( lua_State* L, long v );
void _glua_pushunsigned( lua_State* L, ulong v );

/**
void _glua_pushint32( lua_State* L, long v );
void _glua_pushuint32( lua_State* L, unsigned long v );
void _glua_pushint64( lua_State* L, longlong v );
void _glua_pushuint64( lua_State* L, ulonglong v );
void _glua_pushnumber_int( lua_State* L, int v );
**/

void _glua_pushclstring( lua_State* L, const char* s, int len );

#ifdef USE_FLOAT
  #define /*num*/ _glua_tonumber_raw(L,argn)    _glua_tonumber_raw_f(L,argn)
  #define /*num*/ _glua_tonumber_safe(L,argn)   _glua_tonumber_safe_f(L,argn)
  #define /*void*/ _glua_pushnumber(L,v)        _glua_pushnumber_f(L,v)
#else
  #define /*num*/ _glua_tonumber_raw(L,argn)    _glua_tonumber_raw_d(L,argn)
  #define /*num*/ _glua_tonumber_safe(L,argn)   _glua_tonumber_safe_d(L,argn)
  #define /*void*/ _glua_pushnumber(L,v)        _glua_pushnumber_d(L,v)
#endif

#if (defined UNICODE) && (defined __cplusplus)
  void _glua_pushwlstring( lua_State* L, const WCHAR* s, uint len );
  //
  } // extern "C"
  inline void _glua_pushstring( lua_State* L, const char* s )               { _glua_pushclstring( L, s, -1 ); }
  inline void _glua_pushstring( lua_State* L, const WCHAR* s )              { _glua_pushwlstring( L, s, -1 ); }
  inline void _glua_pushlstring( lua_State* L, const char* s, uint len )    { _glua_pushclstring( L, s, len ); }
  inline void _glua_pushlstring( lua_State* L, const WCHAR* s, uint len )   { _glua_pushwlstring( L, s, len ); }
  extern "C" {
#else
  #define _glua_pushstring( L, s )       _glua_pushclstring( L, s, -1 )
  #define _glua_pushlstring( L, s, len ) _glua_pushclstring( L, s, len )
#endif

//AK(13-Dec-04): Changed implementation so that also NULL is pushed as userdata (not 'nil').
//               This may theoretically change behaviour of earlier compiled binary modules.
//
void* _glua_pushuserdata_raw( lua_State* lua, tag_int tag, 
						      const void* p, uint /*size_t*/ size, uint8 mode );

// Original version (pushes p==NULL, size==0 as 'nil'):
void* _glua_pushuserdata( lua_State* lua, tag_int tag, 
						  const void* p, size_t size,
                          lua_CFunction gc_func );

// 'mode': 0= metatable from registry (works always, but slow)
//         1= metatable from upvalue (usable by metamethods themselves)
//         2= metatable from tos [-1] (metatable already pushed, will be eaten)
//
#define _PUSHUD_MODE_REG  0
#define _PUSHUD_MODE_UPV  1
//#define _PUSHUD_MODE_TOS  2   // not used?

void _glua_pushenum( lua_State* lua, tag_int tag, enum_int val, uint8 mode );

#define GLUA_INTARRAY_U8     1
#define GLUA_INTARRAY_S8   (-1)
#define GLUA_INTARRAY_U16    2
#define GLUA_INTARRAY_S16  (-2)
#define GLUA_INTARRAY_U32    4
#define GLUA_INTARRAY_S32  (-4)
//
void _glua_pushintarray( lua_State* lua, void *p, uint items, int style );

#define _glua_pushliteral(L, s) \
	_glua_pushlstring( L, "" s, (sizeof(s)/sizeof(char))-1 )

#define _glua_pushnil(L)  _glua_pushclstring(L,NULL,-1)

void _glua_newtable( lua_State *L );    // was: _glua_pushtable()
void _glua_totable( lua_State *L );

int  _glua_gettop (lua_State *L);

void _glua_dump(lua_State* L);  // dumping Lua stack to stderr (for debug)

void _glua_settop( lua_State *L, int n );
void _glua_setglobal( lua_State *L, const char* s );
bool_int _glua_checkstack( lua_State* L, int extra );
void _glua_pushregtbl( lua_State* L );
tag_int _glua_newtag( lua_State* L );

void _glua_gettable( lua_State *L, int argn );
bool_int _glua_next( lua_State *L, int argn );
void _glua_remove( lua_State* L, int argn );
void _glua_pushvalue( lua_State* L, int argn );

int _glua_exec( const char* cmd_str, int stack_size );
#define glua_exec( cmd_str, stack_size )  _glua_exec( cmd_str, stack_size )

void _glua_modenum( lua_State* L, int argn, enum_int val, uint mask );

tag_int _glua_regtag2( lua_State *L, const char *tagname, bool_int isenum );
const char *_glua_tagname( lua_State *L, tag_int tag );


/*--- Internal Functions -----------------------------*/

extern void _glua_register2(lua_State *lua, 
                            struct s_GluaItem list[], uint items, 
                            int tbl_index );

// Returns: <0: GLUA_VERSION_ERR_...
//           0: static link (ok)
//          >0: host version + feature bits
//
int _glua_linktohost( lua_State* lua,
                      const void* v_func_tbl, 
                      uint func_tbl_size );
// 'linktohost' errors:
#define GLUA_VERSION_ERR_NOFUNC   (-1)      // hole in function table (should not happen)
#define GLUA_VERSION_ERR_OLDHOST  (-2)      // module thinks host is too old
#define GLUA_VERSION_ERR_ALIGN    (-3)      // module data alignment wrong (should be 4 bytes or less)
#define GLUA_VERSION_ERR_OLDMODULE (-4)     // module too old (please, recompile :)
//...

// Feature bits (part of ABI, do not change!):
//
#define GLUA_VERSION_BIT_FLOAT    0x04000000   // using 'float' for Lua numbers (not 'double')
#define GLUA_VERSION_BIT_INT32    0x02000000   // using int32 patch (ability to store full int32 range)
#define GLUA_VERSION_BIT_ENUM     0x01000000   // using Lua core enum support (not userdata)
#define GLUA_VERSION_BIT_NOTSDL   0x00800000   // host without linked-in SDL support (need 'luax_gui')
//...
#define GLUA_VERSION_YYMMDD_MASK  0x0007ffff   // valid till 2052

int _glua_getn( lua_State* L, int argn );

void _glua_concat( lua_State* L, int argn );

void _glua_call( lua_State* L, int argn, int retn /*-1=LUA_MULTRET*/ );

void _glua_insert( lua_State* L, int argn );

void _glua_pushcclosure( lua_State* L, lua_CFunction fn, uint n );

void _setL( lua_State* L );
lua_State* _getL(void);

#ifdef _HAS_VA_ARGS   // Gcc 3.x etc.
  #define _setT(argn)  (_tbl_argn_=(argn))
  #define _getT()      _tbl_argn_
  //
#else   // MSC, QNX (gcc 2.95) etc.
  int _setT( int argn );  // returns 'argn'
  int _getT(void);
#endif


/*--- Compatibility functions (for Lua C API emulation) -----*/

#if ((defined GLUA_STATIC) || (defined GLUAPORT))
  //  
  int _gluaport_strlen( lua_State* L, int argn );
  int _gluaport_tagofuserdata( lua_State* L, int index );
  void _gluaport_rawset( lua_State* L, int argn );
  void _gluaport_rawseti( lua_State* L, int argn, int n );
  void _gluaport_call( lua_State* L, int nargs, int nresults );
  void _gluaport_settable( lua_State* L, int argn );
  void _gluaport_settag( lua_State* L, int argn, int tag );
  void _gluaport_settagmethod( lua_State* L, int tag, const char* event );

  //AKP(12-Jan-04): Striving for full 'lauxlib' compatibility:
  //
  int _gluaport_getstack( lua_State *L, int level, lua_Debug *ar );
  int _gluaport_getinfo( lua_State *L, const char *what, lua_Debug *ar );
  int _gluaport_type( lua_State* L, int argn );
  int _gluaport_pcall( lua_State *L, int nargs, int nresults, int errfunc );
  int _gluaport_load( lua_State *L, lua_Chunkreader reader, void *dt, const char *chunkname );
  const char *_gluaport_pushvfstring( lua_State *L, const char *fmt, va_list argp );
  void _gluaport_rawget( lua_State *L, int idx );
  void _gluaport_rawgeti( lua_State *L, int idx, int n );
  int _gluaport_setmetatable( lua_State *L, int objindex );
  int _gluaport_getmetatable( lua_State *L, int objindex );

  // AKP(24-Mar-04): Latest luaSocket needs this:
  void _gluaport_replace( lua_State* L, int argn );

  //...
#endif


/*--- Public macros -----------------------------------------*/

#define glua_error(msg)  _glua_error( _lua_, (msg) )

#ifdef _HAS_VA_ARGS	// gcc etc.
  #define glua_errorN(...)  _glua_errorN( _lua_, __VA_ARGS__ )
#else
  void glua_errorN( const char* msg, ... );   // needs '_getL()'!
#endif

#define glua_argn()      _glua_gettop(_lua_)
#define glua_type(argn)	 _glua_type(_lua_,argn)

//AK(11-Dec-04):
const char* _glua_typename( enum e_Glua_Type type );
#define glua_typename(argn)  _glua_typename( glua_type(argn) )

//AK(13-Dec-04):
#define glua_dump()  _glua_dump(_lua_)

//---
// 'glua_is...())' macros:
//
// These can be used to query the precise types of input parameters. This is
// needed in variable parameter list functions and also if strict non-converting
// parameter lists are wanted to be used. E.g. 'glua_getString()' will 
// convert numbers automatically to strings. To avoid this, 'glua_isString()'
// returns TRUE only for actual strings (and 'nil'), not for numbers.
//
// Note: 'isNumber()' gives TRUE for both real and integer numbers.
//       'isInteger()' gives TRUE only for integers.
//
// Note: 'nil' (NULL) is accepted as a string and/or userdata value.
//
#define glua_isInteger(argn)  ( _glua_type(_lua_,argn) & (GLUA_TYPE_INT) )
#define glua_isNumber(argn)	  ( _glua_type(_lua_,argn) & (GLUA_TYPE_NUMBER | GLUA_TYPE_INT) )
#define glua_isString(argn)	  ( _glua_type(_lua_,argn) &  GLUA_TYPE_STRING )
#define glua_isUserdata(argn,tag) ( _glua_type2(_lua_,argn,tag) & GLUA_TYPE_USERDATA )
#define glua_isTable(argn)	  ( _glua_type(_lua_,argn) &  GLUA_TYPE_TABLE )
#define glua_isNil(argn)	  ( _glua_type(_lua_,argn) == GLUA_TYPE_NIL )
#define glua_isAny(argn)      ( _glua_type(_lua_,argn) != GLUA_TYPE_NIL )

//#define glua_isInt64(argn)    ( _glua_type(_lua_,argn) & (GLUA_TYPE_INT | GLUA_TYPE_INT32 | GLUA_TYPE_INT64) )
//#define glua_isNone(n)        ( glua_argn() < (n) )
#define glua_isEnum(argn,tag)   glua_isUserdata(argn,tag)
#define glua_isClosure(argn)  ( _glua_type(_lua_,argn) == GLUA_TYPE_FUNCTION )

// Regular parameters have 'argn' != 0.
// Table fields have 'argn'==0.
//
glua_num_t _glua_getNumber(lua_State* L, int argn, glua_num_t def);
long   _glua_getInteger(lua_State* L, int argn, long def);
ulong _glua_getUnsigned(lua_State* L, int argn, ulong def);
const char* _glua_getLString(lua_State* L, int argn, size_t* len_ref, const char* def );
void* _glua_getUserdata(lua_State* L, int argn, tag_int tag, void* def );
enum_int _glua_getEnum(lua_State* L, int argn, tag_int tag, enum_int def );
bool_int _glua_getBoolean(lua_State* L, int argn, bool_int def );
int _glua_getTable(lua_State* L, int argn);

/**
long   _glua_getInt32(lua_State* L, int argn, long def);
ulong _glua_getUint32(lua_State* L, int argn, ulong def);
longlong _glua_getInt64(lua_State* L, int argn, long long def);
ulonglong _glua_getUint64(lua_State* L, int argn, unsigned long long def);

#define /_*longlong*_/ _glua_getInteger64( L, argn, def ) \
    ( _glua_type(L,argn)==GLUA_TYPE_INT ? _glua_getInteger(L,argn,def) \
                                        : _glua_getInt64(L,argn,def) )
#define /_*ulonglong*_/ _glua_getUnsigned64( L, argn, def ) \
    ( _glua_type(L,argn)==GLUA_TYPE_INT ? _glua_getUnsigned(L,argn,def) \
                                        : _glua_getUint64(L,argn,def) )
**/

#define glua_getUserdata(argn, tag)     _glua_getUserdata(_lua_,argn,tag,NULL)
#define glua_getLString(argn,len_ref)   _glua_getLString(_lua_,argn,len_ref,NULL)

#ifdef UNICODE
  const WCHAR* _glua_getWLString( lua_State* L, int argn, size_t* len_ref, const char* def );
  //
  #define glua_getWString(argn)           _glua_getWLString(_lua_,argn,NULL,NULL)
  #define glua_getWLString(argn,len_ref)  _glua_getWLString(_lua_,argn,len_ref,NULL)
  #define glua_getTString(argn)         glua_getWString(argn)
  #define glua_getTLString(argn,lref)   glua_getWLString(argn,lref)
#else
  #define glua_getTString(argn)         glua_getString(argn)
  #define glua_getTLString(argn,lref)   glua_getLString(argn,lref)
#endif

// TRUE for a table (even empty ones), FALSE for 'nil':
#define /* bool_int */ glua_getTable(argn)\
  ( (bool_int)_setT(_glua_getTable(_lua_,argn)) )

// Do 'getTable' but return FALSE for non-table entries (s.a. userdata)
#define glua_getTable2(argn) \
    ( glua_isTable(argn) ? (bool_int/*TRUE*/)glua_getTable(argn) : FALSE )

// Allow the 'self' parameter to be either a userdata or table's "._ud" field:
#define glua_getObj(i,tag) ( glua_getTable2(i) ? glua_getTable_ud("_ud",tag) \
                                               : glua_getUserdata(i,tag) )

// AK(1-Sep-03): Allow C++ progs to provide defaults:
// AK(9-Dec-04): Add '_def' macros for C, too (C++ does not need 'em).
//
#ifndef __cplusplus
  #define glua_getNumber(argn)    _glua_getNumber(_lua_,argn,GLUA_NUM_ZERO)
  #define glua_getInteger(argn)   _glua_getInteger(_lua_,argn,0)
  #define glua_getUnsigned(argn)  _glua_getUnsigned(_lua_,argn,0)
  /*
  #define glua_getInteger64(argn)   _glua_getInteger64(_lua_,argn,0)
  #define glua_getUnsigned64(argn)  _glua_getUnsigned64(_lua_,argn,0)
  */
  #define glua_getString(argn)    _glua_getLString(_lua_,argn,NULL,NULL)
  #define glua_getEnum(argn,tag)  _glua_getEnum(_lua_,argn,tag,0)
  #define glua_getBoolean(argn)   _glua_getBoolean(_lua_,argn,FALSE)
  /*
  #define glua_getInt32(argn)     _glua_getInt32(_lua_,argn,0)
  #define glua_getUint32(argn)    _glua_getUint32(_lua_,argn,0)
  #define glua_getInt64(argn)     _glua_getInt64(_lua_,argn,0)
  #define glua_getUint64(argn)    _glua_getUint64(_lua_,argn,0)
  */
  #define glua_getNumber_def(argn,def)    _glua_getNumber(_lua_,argn,def)
  #define glua_getInteger_def(argn,def)   _glua_getInteger(_lua_,argn,def)
  #define glua_getUnsigned_def(argn,def)  _glua_getUnsigned(_lua_,argn,def)
  //#define glua_getInteger64_def(argn,def)   _glua_getInteger64(_lua_,argn,def)
  //#define glua_getUnsigned64_def(argn,def)  _glua_getUnsigned64(_lua_,argn,def)
  #define glua_getString_def(argn,def)    _glua_getLString(_lua_,argn,NULL,def)
  #define glua_getEnum_def(argn,tag,def)  _glua_getEnum(_lua_,argn,tag,def)
  #define glua_getBoolean_def(argn,def)   _glua_getBoolean(_lua_,argn,def)
  /*
  #define glua_getInt32_def(argn,def)     _glua_getInt32(_lua_,argn,def)
  #define glua_getUint32_def(argn,def)    _glua_getUint32(_lua_,argn,def)
  #define glua_getInt64_def(argn,def)     _glua_getInt64(_lua_,argn,def)
  #define glua_getUint64_def(argn,def)    _glua_getUint64(_lua_,argn,def)
  */
  //
#else
  inline glua_num_t glua_getNumber( int argn, glua_num_t def=GLUA_NUM_ZERO )
    { return _glua_getNumber( _getL(), argn, def ); }
  inline long glua_getInteger( int argn, long def=0 )
    { return _glua_getInteger( _getL(), argn, def ); }
  inline ulong glua_getUnsigned( int argn, ulong def=0 )
    { return _glua_getUnsigned( _getL(), argn, def ); }
  /*
  inline longlong glua_getInteger64( int argn, longlong def=0 )
    { return _glua_getInteger64( _getL(), argn, def ); }
  inline ulonglong glua_getUnsigned64( int argn, ulonglong def=0 )
    { return _glua_getUnsigned64( _getL(), argn, def ); }
    */
  inline const char* glua_getString( int argn, const char* def= NULL )
    { return _glua_getLString( _getL(), argn, NULL, def ); }
  inline int glua_getEnum( int argn, tag_int tag, int def=0 )
    { return _glua_getEnum( _getL(), argn, tag, def ); }
  inline bool_int glua_getBoolean( int argn, bool_int def= FALSE )
    { return _glua_getBoolean( _getL(), argn, def ); }
    /*
  inline long glua_getInt32( int argn, long def=0 )
    { return _glua_getInt32( _getL(), argn, def ); }
  inline ulong glua_getUint32( int argn, ulong def=0 )
    { return _glua_getUint32( _getL(), argn, def ); }
  inline longlong glua_getInt64( int argn, longlong def=0 )
    { return _glua_getInt64( _getL(), argn, def ); }
  inline ulonglong glua_getUint64( int argn, ulonglong def=0 )
    { return _glua_getUint64( _getL(), argn, def ); }
    */
#endif  // C++

//-----
// Note: The 'do...while(0)'s are dummies that eat up semicolons after them.
//       This helps using the macros in if-else constructs.
//
#define glua_pushNumber(v)      ( _glua_pushnumber(_lua_, v), _retcount_++ )
#define glua_pushString(s)      ( _glua_pushstring(_lua_,s), _retcount_++ )
#define glua_pushLString(s,len) ( _glua_pushlstring(_lua_,s,len), _retcount_++ )
#define glua_pushBoolean(b)     ( _glua_pushboolean(_lua_,b), _retcount_++ )
#define glua_pushTable()        ( _glua_newtable(_lua_), _retcount_++ )
#define glua_pushNil()          ( _glua_pushnil(_lua_), _retcount_++ )
//
#define glua_pushInteger(v)     ( _glua_pushinteger(_lua_,v), _retcount_++ )
#define glua_pushUnsigned(v)    ( _glua_pushunsigned(_lua_,v), _retcount_++ )
//
#define glua_pushChar(c)        glua_pushLString(&(c),1)

/*
// Special case that pushes a Lua number (not 'int32' enum) but 
// checks it against integer range (error if accuracy would be lost)
//
#define glua_pushNumberInt(v)  ( _glua_pushnumber_int(_lua_,v), _retcount_++ )
*/

//---
#define /* void* */ glua_pushUserdata_raw( ptr, tag ) \
  ( _retcount_++, _glua_pushuserdata_raw( _lua_, tag, ptr, 0 /*size*/ ), 0 /*mode*/ )

#define /* void* */ glua_pushUserdata_ptr( ptr, tag ) \
  ( _retcount_++, _glua_pushuserdata( _lua_, tag, ptr, 0 /*size*/, NULL ) )
  
#define /* void* */ glua_pushUserdata_size( size, tag ) \
  ( _retcount_++, _glua_pushuserdata_raw( _lua_, tag, NULL, size, 0 /*mode*/ ) )
  
#define /* void* */ glua_pushUserdata_copy( ref, tag ) \
  ( _retcount_++, _glua_pushuserdata_raw( _lua_, tag, ref, sizeof(*(ref)), 0 /*mode*/ ) )
  
#define /* void* */ glua_pushUserdata_ptrgc( ptr, tag, gc_func ) \
  ( _retcount_++, _glua_pushuserdata( _lua_, tag, ptr, 0 /*size*/, gc_func ) )
  
#define /* void* */ glua_pushUserdata_sizegc( size, tag, gc_func ) \
  ( _retcount_++, _glua_pushuserdata( _lua_, tag, NULL, size, gc_func ) )

#define glua_pushUserdata(p,tag)  glua_pushUserdata_ptr(p,tag)

#define glua_pushIntArray(p,items,style) \
  ( _retcount_++, _glua_pushintarray(_lua_,p,items,style) )

// The 'fast' variant can be used within metamethods (s.a. 'concat') that
// have the userdata's metatable ref as an upvalue.
//
#define glua_pushEnum(val,tag) \
  ( _retcount_++, _glua_pushenum(_lua_,tag,val,_PUSHUD_MODE_REG) )

#define glua_pushEnum_mt(val,tag) \
  ( _retcount_++, _glua_pushenum(_lua_,tag,val,_PUSHUD_MODE_UPV) )

/*
#define glua_pushInt32(v)       ( _glua_pushint32(_lua_,v), _retcount_++ )
#define glua_pushUint32(v)      ( _glua_pushint32(_lua_,(long)(v)), _retcount_++ )
#define glua_pushInt64(v)       ( _glua_pushint64(_lua_,v), _retcount_++ )
#define glua_pushUint64(v)      ( _glua_pushint64(_lua_,(ulonglong)(v)), _retcount_++ )
*/

//-----
// Attachments to userdata entries (provided by their associated pointers)
// may be required to pass data between the creator & releaser of an object.
// This system uses Lua registry internally, and allows a single pointer
// to be attached to each userdata. New settings override older ones.
//
void _glua_getreg( lua_State* L );
void _glua_setreg( lua_State* L );

void _glua_settagmethod( lua_State* L, tag_int tag, const char* event,
									   lua_CFunction func );

void* _glua_attach( lua_State* L, const void* obj, void* attachment );

#define glua_setAttachment( obj_ptr, attachment_ptr ) \
		(void)_glua_attach( _lua_, obj_ptr, attachment_ptr )

#define /* void* */ glua_getAttachment( obj_ptr, remove ) \
		_glua_attach( _lua_, obj_ptr, (remove) ? NULL:(void*)(-1) ) \


//-----
// Functions for adding entries into a table (which is at top of stack).
//
void _glua_tblkey_i( lua_State* L, int i );
void _glua_tblkey_k( lua_State* L, const char* k );
void _glua_tblkey_ijk( lua_State* L, int i, int j, const char* k );
//
#define _glua_tblkey_ik(L,i,k)  _glua_tblkey_ijk(L,i,0,k)
#define _glua_tblkey_ij(L,i,j)  _glua_tblkey_ijk(L,i,j,NULL)

#ifndef __cplusplus  // ANSI C only gets the basic forms:
  //
  #define glua_tblKey(k)   ( _glua_tblkey_k(_lua_,k), _retcount_-- )
  #define glua_tblKey_i(i) ( _glua_tblkey_i(_lua_,i), _retcount_-- )
  //
  #define glua_setTable_num(k,v)    ( glua_pushNumber(v), glua_tblKey(k) )
  #define glua_setTable_str(k,s)    ( glua_pushString(s), glua_tblKey(k) )
  #define glua_setTable_lstr(k,s,l) ( glua_pushLString(s,l), glua_tblKey(k) )
  #define glua_setTable_ud(k,p,tag) ( glua_pushUserdata(p,tag), glua_tblKey(k) )
  #define glua_setTable_enum(k,v,etag) ( glua_pushEnum(v,etag), glua_tblKey(k) )
  #define glua_setTable_bool(k,v)   ( glua_pushBoolean(v), glua_tblKey(k) )
  #define glua_setTable_int(k,v)    ( glua_pushInteger(v), glua_tblKey(k) )
  #define glua_setTable_uint(k,v)   ( glua_pushUnsigned(v), glua_tblKey(k) )
  //
  #define glua_setTable_i_num(i,v)    ( glua_pushNumber(v), glua_tblKey_i(i) )
  #define glua_setTable_i_str(i,s)    ( glua_pushString(s), glua_tblKey_i(i) )
  #define glua_setTable_i_lstr(i,s,l) ( glua_pushLString(s,l), glua_tblKey_i(i) )
  #define glua_setTable_i_int(i,v)    ( glua_pushInteger(v), glua_tblKey_i(i) )
  #define glua_setTable_i_uint(i,v)   ( glua_pushUnsigned(v), glua_tblKey_i(i) )
  //
#else   // C++ can use inline funcs & parameter overloading :) 
  //
  } // extern "C"

  #ifdef _HAS_VA_ARGS
    //
    inline void _glua_tblkey( lua_State* L, int i )                { _glua_tblkey_i(L,i); }
    inline void _glua_tblkey( lua_State* L, const char* k )        { _glua_tblkey_k(L,k); }
    inline void _glua_tblkey( lua_State* L, int i, const char* k ) { _glua_tblkey_ijk(L,i,0,k); }
    inline void _glua_tblkey( lua_State* L, int i, int j )         { _glua_tblkey_ijk(L,i,j,NULL); }
    //
    // Note: Only for apps that want to do "glua_pushXxx(), glua_tblKey()" themselves:
    //
    #define glua_tblKey( ... )  ( _glua_tblkey( _lua_, __VA_ARGS__ ), _retcount_-- )
    //
  #else   // MSC 6.0
    //
    inline void glua_tblKey( int i )                { _glua_tblkey_i(_getL(),i); }
    inline void glua_tblKey( const char* k )        { _glua_tblkey_k(_getL(),k); }
    inline void glua_tblKey( int i, const char* k ) { _glua_tblkey_ik(_getL(),i,k); }
    inline void glua_tblKey( int i, int j )         { _glua_tblkey_ij(_getL(),i,j); }
    //
  #endif

  #ifdef _HAS_VA_ARGS
    // Note: separate 'int' and 'uint' funcs are needed for 'float' accuracy check.
    //
    inline void _glua_setTable_num( lua_State* L, const char* k, glua_num_t v )     { _glua_pushnumber(L,v); _glua_tblkey(L,k); }
    inline void _glua_setTable_num( lua_State* L, int i, glua_num_t v )             { _glua_pushnumber(L,v); _glua_tblkey(L,i); }
    inline void _glua_setTable_num( lua_State* L, int i, const char* k, glua_num_t v )  { _glua_pushnumber(L,v); _glua_tblkey(L,i,k); }
    inline void _glua_setTable_num( lua_State* L, int i, int j, glua_num_t v )      { _glua_pushnumber(L,v); _glua_tblkey(L,i,j); }
    //
    inline void _glua_setTable_int( lua_State* L, const char* k, long v )       { _glua_pushinteger(L,v); _glua_tblkey(L,k); }
    inline void _glua_setTable_int( lua_State* L, int i, long v )               { _glua_pushinteger(L,v); _glua_tblkey(L,i); }
    inline void _glua_setTable_int( lua_State* L, int i, const char* k, long v )  { _glua_pushinteger(L,v); _glua_tblkey(L,i,k); }
    inline void _glua_setTable_int( lua_State* L, int i, int j, long v )        { _glua_pushinteger(L,v); _glua_tblkey(L,i,j); }
    //
    inline void _glua_setTable_uint( lua_State* L, const char* k, ulong v )     { _glua_pushunsigned(L,v); _glua_tblkey(L,k); }
    inline void _glua_setTable_uint( lua_State* L, int i, ulong v )             { _glua_pushunsigned(L,v); _glua_tblkey(L,i); }
    inline void _glua_setTable_uint( lua_State* L, int i, const char* k, ulong v )  { _glua_pushunsigned(L,v); _glua_tblkey(L,i,k); }
    inline void _glua_setTable_uint( lua_State* L, int i, int j, ulong v )      { _glua_pushunsigned(L,v); _glua_tblkey(L,i,j); }
    //
    inline void _glua_setTable_str( lua_State* L, const char* k, const char* s )    { _glua_pushstring(L,s); _glua_tblkey(L,k); }
    inline void _glua_setTable_str( lua_State* L, int i, const char* s )            { _glua_pushstring(L,s); _glua_tblkey(L,i); }
    inline void _glua_setTable_str( lua_State* L, int i, const char* k, const char* s )  { _glua_pushstring(L,s); _glua_tblkey(L,i,k); }
    inline void _glua_setTable_str( lua_State* L, int i, int j, const char* s )     { _glua_pushstring(L,s); _glua_tblkey(L,i,j); }
    //
    inline void _glua_setTable_lstr( lua_State* L, const char* k, const char* s, uint l )    { _glua_pushlstring(L,s,l); _glua_tblkey(L,k); }
    inline void _glua_setTable_lstr( lua_State* L, int i, const char* s, uint l )            { _glua_pushlstring(L,s,l); _glua_tblkey(L,i); }
    inline void _glua_setTable_lstr( lua_State* L, int i, const char* k, const char* s, uint l )  { _glua_pushlstring(L,s,l); _glua_tblkey(L,i,k); }
    inline void _glua_setTable_lstr( lua_State* L, int i, int j, const char* s, uint l )     { _glua_pushlstring(L,s,l); _glua_tblkey(L,i,j); }
    //
    inline void _glua_setTable_ud( lua_State* L, const char* k, void* p, tag_int tag )  { _glua_pushuserdata(L,tag,p,0,NULL); _glua_tblkey(L,k); }
    inline void _glua_setTable_ud( lua_State* L, int i, void* p, tag_int tag )          { _glua_pushuserdata(L,tag,p,0,NULL); _glua_tblkey(L,i); }
    inline void _glua_setTable_ud( lua_State* L, int i, const char* k, void* p, tag_int tag )  { _glua_pushuserdata(L,tag,p,0,NULL); _glua_tblkey(L,i,k); }
    inline void _glua_setTable_ud( lua_State* L, int i, int j, void* p, tag_int tag )   { _glua_pushuserdata(L,tag,p,0,NULL); _glua_tblkey(L,i,j); }
    //
    inline void _glua_setTable_enum( lua_State* L, const char* k, int v, tag_int tag )  { _glua_pushenum(L,tag,v,0); _glua_tblkey(L,k); }
    inline void _glua_setTable_enum( lua_State* L, int i, int v, tag_int tag )          { _glua_pushenum(L,tag,v,0); _glua_tblkey(L,i); }
    inline void _glua_setTable_enum( lua_State* L, int i, const char* k, int v, tag_int tag )  { _glua_pushenum(L,tag,v,0); _glua_tblkey(L,i,k); }
    inline void _glua_setTable_enum( lua_State* L, int i, int j, int v, tag_int tag )   { _glua_pushenum(L,tag,v,0); _glua_tblkey(L,i,j); }
    //
    // Note: Here we can use 'bool' (not 'bool_int') since we're definately in C++.
    //
    inline void _glua_setTable_bool( lua_State* L, const char* k, bool b )          { _glua_pushboolean(L,b); _glua_tblkey(L,k); }
    inline void _glua_setTable_bool( lua_State* L, int i, bool b )                  { _glua_pushboolean(L,b); _glua_tblkey(L,i); }
    inline void _glua_setTable_bool( lua_State* L, int i, const char* k, bool b )   { _glua_pushboolean(L,b); _glua_tblkey(L,i,k); }
    inline void _glua_setTable_bool( lua_State* L, int i, int j, bool b )           { _glua_pushboolean(L,b); _glua_tblkey(L,i,j); }

    // Note: support for variable arglist in preprocessor required!
    //       (at least gcc has this, but is it standard...?)
    //
    #define glua_setTable_int( ... ) _glua_setTable_int(_lua_, __VA_ARGS__)   // (int->num)
    #define glua_setTable_uint( ... ) _glua_setTable_uint(_lua_, __VA_ARGS__)   // (uint->num)
    #define glua_setTable_num( ... ) _glua_setTable_num(_lua_, __VA_ARGS__)
    #define glua_setTable_str( ... ) _glua_setTable_str(_lua_, __VA_ARGS__)
    #define glua_setTable_lstr( ... ) _glua_setTable_lstr(_lua_, __VA_ARGS__)
    #define glua_setTable_ud( ... )  _glua_setTable_ud(_lua_, __VA_ARGS__)
    #define glua_setTable_enum( ... ) _glua_setTable_enum(_lua_, __VA_ARGS__)
    #define glua_setTable_bool( ... ) _glua_setTable_bool(_lua_, __VA_ARGS__)
    //
  #else   // MSC & eVC++ (no __VA_ARGS__ support)
    //
    // Using '_getL()' incurs a slight runtime penalty (thread specific data).
    // That's why we don't use it for other compilers.
    //
    inline void glua_setTable_num( const char* k, glua_num_t v )     { lua_State* L=_getL(); _glua_pushnumber(L,v); _glua_tblkey_k(L,k); }
    inline void glua_setTable_num( int i, glua_num_t v )             { lua_State* L=_getL(); _glua_pushnumber(L,v); _glua_tblkey_i(L,i); }
    inline void glua_setTable_num( int i, const char* k, glua_num_t v )  { lua_State* L=_getL(); _glua_pushnumber(L,v); _glua_tblkey_ik(L,i,k); }
    inline void glua_setTable_num( int i, int j, glua_num_t v )      { lua_State* L=_getL(); _glua_pushnumber(L,v); _glua_tblkey_ij(L,i,j); }
    //
    inline void glua_setTable_int( const char* k, int v )			 { lua_State* L=_getL(); _glua_pushinteger(L,v); _glua_tblkey_k(L,k); }
    inline void glua_setTable_int( int i, int v )                    { lua_State* L=_getL(); _glua_pushinteger(L,v); _glua_tblkey_i(L,i); }
    inline void glua_setTable_int( int i, const char* k, int v )	 { lua_State* L=_getL(); _glua_pushinteger(L,v); _glua_tblkey_ik(L,i,k); }
    inline void glua_setTable_int( int i, int j, int v )			 { lua_State* L=_getL(); _glua_pushinteger(L,v); _glua_tblkey_ij(L,i,j); }
	//
    inline void glua_setTable_uint( const char* k, uint v )			 { lua_State* L=_getL(); _glua_pushunsigned(L,v); _glua_tblkey_k(L,k); }
    inline void glua_setTable_uint( int i, uint v )                  { lua_State* L=_getL(); _glua_pushunsigned(L,v); _glua_tblkey_i(L,i); }
    inline void glua_setTable_uint( int i, const char* k, uint v )	 { lua_State* L=_getL(); _glua_pushunsigned(L,v); _glua_tblkey_ik(L,i,k); }
    inline void glua_setTable_uint( int i, int j, uint v )			 { lua_State* L=_getL(); _glua_pushunsigned(L,v); _glua_tblkey_ij(L,i,j); }
	//
    inline void glua_setTable_str( const char* k, const char* s )    { lua_State* L=_getL(); _glua_pushstring(L,s); _glua_tblkey_k(L,k); }
    inline void glua_setTable_str( int i, const char* s )            { lua_State* L=_getL(); _glua_pushstring(L,s); _glua_tblkey_i(L,i); }
    inline void glua_setTable_str( int i, const char* k, const char* s )  { lua_State* L=_getL(); _glua_pushstring(L,s); _glua_tblkey_ik(L,i,k); }
    inline void glua_setTable_str( int i, int j, const char* s )     { lua_State* L=_getL(); _glua_pushstring(L,s); _glua_tblkey_ij(L,i,j); }
    //
    inline void glua_setTable_lstr( const char* k, const char* s, uint l )    { lua_State* L=_getL(); _glua_pushlstring(L,s,l); _glua_tblkey_k(L,k); }
    inline void glua_setTable_lstr( int i, const char* s, uint l )            { lua_State* L=_getL(); _glua_pushlstring(L,s,l); _glua_tblkey_i(L,i); }
    inline void glua_setTable_lstr( int i, const char* k, const char* s, uint l )  { lua_State* L=_getL(); _glua_pushlstring(L,s,l); _glua_tblkey_ik(L,i,k); }
    inline void glua_setTable_lstr( int i, int j, const char* s, uint l )     { lua_State* L=_getL(); _glua_pushlstring(L,s,l); _glua_tblkey_ij(L,i,j); }
    //
    inline void glua_setTable_ud( const char* k, void* p, tag_int tag )  { lua_State* L=_getL(); _glua_pushuserdata(L,tag,p,0,NULL); _glua_tblkey_k(L,k); }
    inline void glua_setTable_ud( int i, void* p, tag_int tag )          { lua_State* L=_getL(); _glua_pushuserdata(L,tag,p,0,NULL); _glua_tblkey_i(L,i); }
    inline void glua_setTable_ud( int i, const char* k, void* p, tag_int tag )  { lua_State* L=_getL(); _glua_pushuserdata(L,tag,p,0,NULL); _glua_tblkey_ik(L,i,k); }
    inline void glua_setTable_ud( int i, int j, void* p, tag_int tag )   { lua_State* L=_getL(); _glua_pushuserdata(L,tag,p,0,NULL); _glua_tblkey_ij(L,i,j); }
    //
    inline void glua_setTable_enum( const char* k, int v, tag_int tag )  { lua_State* L=_getL(); _glua_pushenum(L,tag,v,0); _glua_tblkey_k(L,k); }
    inline void glua_setTable_enum( int i, int v, tag_int tag )          { lua_State* L=_getL(); _glua_pushenum(L,tag,v,0); _glua_tblkey_i(L,i); }
    inline void glua_setTable_enum( int i, const char* k, int v, tag_int tag )  { lua_State* L=_getL(); _glua_pushenum(L,tag,v,0); _glua_tblkey_ik(L,i,k); }
    inline void glua_setTable_enum( int i, int j, int v, tag_int tag )   { lua_State* L=_getL(); _glua_pushenum(L,tag,v,0); _glua_tblkey_ij(L,i,j); }
    //
    inline void glua_setTable_bool( const char* k, bool b )          { lua_State* L=_getL(); _glua_pushboolean(L,b); _glua_tblkey_k(L,k); }
    inline void glua_setTable_bool( int i, bool b )                  { lua_State* L=_getL(); _glua_pushboolean(L,b); _glua_tblkey_i(L,i); }
    inline void glua_setTable_bool( int i, const char* k, bool b )   { lua_State* L=_getL(); _glua_pushboolean(L,b); _glua_tblkey_ik(L,i,k); }
    inline void glua_setTable_bool( int i, int j, bool b )           { lua_State* L=_getL(); _glua_pushboolean(L,b); _glua_tblkey_ij(L,i,j); }
    //
  #endif
  
  extern "C" {
#endif  // C++

//-----
// Functions for reading in table fields from an input parameter table.
//
// NOTE: These calls need to be made _immediately_after_ a 'getTable()'
//       that has 'opened' the table for access. Once other 'get' functions
//       are called, these are no longer valid!
//
// Subkeys (s.a. "obj.color.red") can be placed directly within the 'key'
// string itself or provided in separate 'index' and 'key' entries which
// is practical to browse through arrays.
//
void _glua_usekey_i( lua_State* L, int argn, int i );
void _glua_usekey_k( lua_State* L, int argn, const char* k );
void _glua_usekey_ij( lua_State* L, int argn, int i, int j );
void _glua_usekey_ijk( lua_State* L, int argn, int i, int j, const char* k );
//
#define _glua_usekey_ik(L,a,i,k) _glua_usekey_ijk( L, a, i,0, k )

#ifndef __cplusplus   // ANSI C only gets the basic forms:
  //
  #define glua_useKey(k)   _glua_usekey_k(_lua_,_tbl_argn_,k)
  #define glua_useKey_i(i) _glua_usekey_i(_lua_,_tbl_argn_,i)
  //
  #define glua_getTable_int(k)    ( glua_useKey(k), _glua_getInteger(_lua_,0,0) )
  #define glua_getTable_num(k)    ( glua_useKey(k), _glua_getNumber(_lua_,0,GLUA_NUM_ZERO) )
  #define glua_getTable_str(k)    ( glua_useKey(k), _glua_getLString(_lua_,0,NULL,NULL) )
  #define glua_getTable_lstr(k,lref) ( glua_useKey(k), _glua_getLString(_lua_,0,lref,NULL) )
  #define glua_getTable_wstr(k)   ( glua_useKey(k), _glua_getWLString(_lua_,0,NULL,NULL) )
  #define glua_getTable_wlstr(k,lref) ( glua_useKey(k), _glua_getWLString(_lua_,0,lref,NULL) )
  #define glua_getTable_ud(k,tag) ( glua_useKey(k), _glua_getUserdata(_lua_,0,tag,NULL) )
  #define glua_getTable_enum(k,tag) ( glua_useKey(k), _glua_getEnum(_lua_,0,tag,0) )
  #define glua_getTable_bool(k)   ( glua_useKey(k), _glua_getBoolean(_lua_,0,FALSE) )
  #define glua_getTable_uint(k)   ( glua_useKey(k), _glua_getUnsigned(_lua_,0,0) )
  #define glua_getTable_type(k)   ( glua_useKey(k), _glua_type(_lua_,-1) )
  //
  #define glua_getTable_i_int(i)    ( glua_useKey_i(i), _glua_getInteger(_lua_,0,0) )
  #define glua_getTable_i_num(i)    ( glua_useKey_i(i), _glua_getNumber(_lua_,0,GLUA_NUM_ZERO) )
  #define glua_getTable_i_str(i)    ( glua_useKey_i(i), _glua_getLString(_lua_,0,NULL,NULL) )
  #define glua_getTable_i_lstr(i,lref) ( glua_useKey_i(i), _glua_getLString(_lua_,0,lref,NULL) )
  #define glua_getTable_i_wstr(i)   ( glua_useKey_i(i), _glua_getWLString(_lua_,0,NULL,NULL) )
  #define glua_getTable_i_wlstr(i,lref) ( glua_useKey_i(i), _glua_getWLString(_lua_,0,lref,NULL) )
  #define glua_getTable_i_ud(i,tag) ( glua_useKey_i(i), _glua_getUserdata(_lua_,0,tag,NULL) )
  #define glua_getTable_i_enum(i,tag) ( glua_useKey_i(i), _glua_getEnum(_lua_,0,tag,0) )
  #define glua_getTable_i_bool(i)   ( glua_useKey_i(i), _glua_getBoolean(_lua_,0,FALSE) )
  #define glua_getTable_i_uint(i)   ( glua_useKey_i(i), _glua_getUnsigned(_lua_,0,0) )
  #define glua_getTable_i_type(i)   ( glua_useKey_i(i), _glua_type(_lua_,-1) )
  //
#else   // C++
  } // extern "C"

  #ifdef _HAS_VA_ARGS
    //
    inline void _glua_usekey( lua_State* L, int argn, int i )                { _glua_usekey_i(L,argn,i); }
    inline void _glua_usekey( lua_State* L, int argn, const char* k )        { _glua_usekey_k(L,argn,k); }
    inline void _glua_usekey( lua_State* L, int argn, int i, const char* k ) { _glua_usekey_ik(L,argn,i,k); }
    inline void _glua_usekey( lua_State* L, int argn, int i, int j )         { _glua_usekey_ij(L,argn,i,j); }
    //
    #define glua_useKey(...)    _glua_usekey( _lua_, _tbl_argn_, __VA_ARGS__ )
    #define glua_useKey_i(...)  _glua_usekey( _lua_, _tbl_argn_, __VA_ARGS__ )
    //
    inline long _glua_getTable_int( lua_State* L, int a, const char* k )        { _glua_usekey(L,a,k); return _glua_getInteger(L,0,0); }
    inline long _glua_getTable_int( lua_State* L, int a, int i )                { _glua_usekey(L,a,i); return _glua_getInteger(L,0,0); }
    inline long _glua_getTable_int( lua_State* L, int a, int i, const char* k ) { _glua_usekey(L,a,i,k); return _glua_getInteger(L,0,0); }
    inline long _glua_getTable_int( lua_State* L, int a, int i, int j )         { _glua_usekey(L,a,i,j); return _glua_getInteger(L,0,0); }
    //
    inline ulong _glua_getTable_uint( lua_State* L, int a, const char* k )        { _glua_usekey(L,a,k); return _glua_getUnsigned(L,0,0); }
    inline ulong _glua_getTable_uint( lua_State* L, int a, int i )                { _glua_usekey(L,a,i); return _glua_getUnsigned(L,0,0); }
    inline ulong _glua_getTable_uint( lua_State* L, int a, int i, const char* k ) { _glua_usekey(L,a,i,k); return _glua_getUnsigned(L,0,0); }
    inline ulong _glua_getTable_uint( lua_State* L, int a, int i, int j )         { _glua_usekey(L,a,i,j); return _glua_getUnsigned(L,0,0); }
    //
    inline glua_num_t _glua_getTable_num( lua_State* L, int a, const char* k )     { _glua_usekey(L,a,k); return _glua_getNumber(L,0,GLUA_NUM_ZERO); }
    inline glua_num_t _glua_getTable_num( lua_State* L, int a, int i )             { _glua_usekey(L,a,i); return _glua_getNumber(L,0,GLUA_NUM_ZERO); }
    inline glua_num_t _glua_getTable_num( lua_State* L, int a, int i, const char* k ) { _glua_usekey(L,a,i,k); return _glua_getNumber(L,0,GLUA_NUM_ZERO); }
    inline glua_num_t _glua_getTable_num( lua_State* L, int a, int i, int j )      { _glua_usekey(L,a,i,j); return _glua_getNumber(L,0,GLUA_NUM_ZERO); }
    //
    /**
    inline const char* _glua_getTable_str( lua_State* L, int a, const char* k ) { _glua_usekey(L,a,k); return _glua_getLString(L,0,NULL,NULL); }
    inline const char* _glua_getTable_str( lua_State* L, int a, int i )         { _glua_usekey(L,a,i); return _glua_getLString(L,0,NULL,NULL); }
    inline const char* _glua_getTable_str( lua_State* L, int a, int i, const char* k ) { _glua_usekey(L,a,i,k); return _glua_getLString(L,0,NULL,NULL); }
    inline const char* _glua_getTable_str( lua_State* L, int a, int i, int j )  { _glua_usekey(L,a,i,j); return _glua_getLString(L,0,NULL,NULL); }
    **/
    //
    inline const char* _glua_getTable_str( lua_State* L, int a, const char* k, size_t* lref=NULL )
        { _glua_usekey(L,a,k); return _glua_getLString(L,0,lref,NULL); }
    inline const char* _glua_getTable_str( lua_State* L, int a, int i, size_t* lref=NULL )
        { _glua_usekey(L,a,i); return _glua_getLString(L,0,lref,NULL); }
    inline const char* _glua_getTable_str( lua_State* L, int a, int i, const char* k, size_t* lref=NULL )
        { _glua_usekey(L,a,i,k); return _glua_getLString(L,0,lref,NULL); }
    inline const char* _glua_getTable_str( lua_State* L, int a, int i, int j, size_t* lref=NULL )
        { _glua_usekey(L,a,i,j); return _glua_getLString(L,0,lref,NULL); }
    //
    inline void* _glua_getTable_ud( lua_State* L, int a, const char* k, tag_int tag ) { _glua_usekey(L,a,k); return _glua_getUserdata(L,0,tag,NULL); }
    inline void* _glua_getTable_ud( lua_State* L, int a, int i, tag_int tag )         { _glua_usekey(L,a,i); return _glua_getUserdata(L,0,tag,NULL); }
    inline void* _glua_getTable_ud( lua_State* L, int a, int i, const char* k, tag_int tag ) { _glua_usekey(L,a,i,k); return _glua_getUserdata(L,0,tag,NULL); }
    inline void* _glua_getTable_ud( lua_State* L, int a, int i, int j, tag_int tag )  { _glua_usekey(L,a,i,j); return _glua_getUserdata(L,0,tag,NULL); }
    //
    inline int _glua_getTable_enum( lua_State* L, int a, const char* k, tag_int tag ) { _glua_usekey(L,a,k); return _glua_getEnum(L,0,tag,0); }
    inline int _glua_getTable_enum( lua_State* L, int a, int i, tag_int tag )         { _glua_usekey(L,a,i); return _glua_getEnum(L,0,tag,0); }
    inline int _glua_getTable_enum( lua_State* L, int a, int i, const char* k, tag_int tag ) { _glua_usekey(L,a,i,k); return _glua_getEnum(L,0,tag,0); }
    inline int _glua_getTable_enum( lua_State* L, int a, int i, int j, tag_int tag )  { _glua_usekey(L,a,i,j); return _glua_getEnum(L,0,tag,0); }
    //
    inline bool _glua_getTable_bool( lua_State* L, int a, const char* k )     { _glua_usekey(L,a,k); return _glua_getBoolean(L,0,FALSE); }
    inline bool _glua_getTable_bool( lua_State* L, int a, int i )             { _glua_usekey(L,a,i); return _glua_getBoolean(L,0,FALSE); }
    inline bool _glua_getTable_bool( lua_State* L, int a, int i, const char* k ) { _glua_usekey(L,a,i,k); return _glua_getBoolean(L,0,FALSE); }
    inline bool _glua_getTable_bool( lua_State* L, int a, int i, int j )      { _glua_usekey(L,a,i,j); return _glua_getBoolean(L,0,FALSE); }
    //
    inline enum e_Glua_Type _glua_getTable_type( lua_State* L, int a, const char* k )        { _glua_usekey(L,a,k); return _glua_type(L,-1); }
    inline enum e_Glua_Type _glua_getTable_type( lua_State* L, int a, int i )                { _glua_usekey(L,a,i); return _glua_type(L,-1); }
    inline enum e_Glua_Type _glua_getTable_type( lua_State* L, int a, int i, const char* k ) { _glua_usekey(L,a,i,k); return _glua_type(L,-1); }
    inline enum e_Glua_Type _glua_getTable_type( lua_State* L, int a, int i, int j )         { _glua_usekey(L,a,i,j); return _glua_type(L,-1); }
    //
    #define glua_getTable_int( ... ) _glua_getTable_int(_lua_, _tbl_argn_, __VA_ARGS__)
    #define glua_getTable_uint( ... ) _glua_getTable_uint(_lua_, _tbl_argn_, __VA_ARGS__)
    #define glua_getTable_num( ... ) _glua_getTable_num(_lua_, _tbl_argn_, __VA_ARGS__)
    #define glua_getTable_str( ... ) _glua_getTable_str(_lua_, _tbl_argn_, __VA_ARGS__)
    #define glua_getTable_ud( ... )  _glua_getTable_ud(_lua_, _tbl_argn_, __VA_ARGS__)
    #define glua_getTable_enum( ... )  _glua_getTable_enum(_lua_, _tbl_argn_, __VA_ARGS__)
    #define glua_getTable_bool( ... ) _glua_getTable_bool(_lua_, _tbl_argn_, __VA_ARGS__)
    #define glua_getTable_type( ... ) _glua_getTable_type(_lua_, _tbl_argn_, __VA_ARGS__)
    //
  #else   // Visual C++ 6.0, eVC++ 4.0
    //
    // These inline funcs return 'L' pointer for our use. 
    // Applications should regard them as 'void' functions, though.
    //
    // Note: MSC has '_tbl_argn_' zero, it'll be fetched from thread specific storage.
    //
    inline lua_State* glua_useKey( const char* key )  
        { lua_State* L=_getL(); _glua_usekey_k(L,0,key); return L; }
    //
    inline lua_State* glua_useKey( int i )
        { lua_State* L=_getL(); _glua_usekey_i(L,0,i); return L; }
    //
    inline lua_State* glua_useKey( int i, const char* k )
        { lua_State* L=_getL(); _glua_usekey_ik(L,0,i,k); return L; }
    //
    inline lua_State* glua_useKey( int i, int j )
        { lua_State* L=_getL(); _glua_usekey_ij(L,0,i,j); return L; }
    //
    #define glua_useKey_i(i) glua_useKey(i)
    //
    inline long glua_getTable_int( const char* k )        { return _glua_getInteger( glua_useKey(k), 0, 0 ); }
    inline long glua_getTable_int( int i )                { return _glua_getInteger( glua_useKey(i), 0, 0 ); }
    inline long glua_getTable_int( int i, const char* k ) { return _glua_getInteger( glua_useKey(i,k), 0, 0 ); }
    inline long glua_getTable_int( int i, int j )         { return _glua_getInteger( glua_useKey(i,j), 0, 0 ); }
    //
    inline glua_num_t glua_getTable_num( const char* k )     { return _glua_getNumber( glua_useKey(k), 0, GLUA_NUM_ZERO ); }
    inline glua_num_t glua_getTable_num( int i )             { return _glua_getNumber( glua_useKey(i), 0, GLUA_NUM_ZERO ); }
    inline glua_num_t glua_getTable_num( int i, const char* k ) { return _glua_getNumber( glua_useKey(i,k), 0, GLUA_NUM_ZERO ); }
    inline glua_num_t glua_getTable_num( int i, int j )      { return _glua_getNumber( glua_useKey(i,j), 0, GLUA_NUM_ZERO ); }
    //
    /***
    inline const char* glua_getTable_str( const char* k ) { return _glua_getLString( glua_useKey(k), 0, NULL,NULL ); }
    inline const char* glua_getTable_str( int i )         { return _glua_getLString( glua_useKey(i), 0, NULL,NULL ); }
    inline const char* glua_getTable_str( int i, const char* k ) { return _glua_getLString( glua_useKey(i,k), 0, NULL,NULL ); }
    inline const char* glua_getTable_str( int i, int j )  { return _glua_getLString( glua_useKey(i,j), 0, NULL,NULL ); }
    ***/
    //
    inline const char* glua_getTable_str( const char* k, size_t* lref=NULL )    
        { return _glua_getLString( glua_useKey(k), 0, lref,NULL ); }
    inline const char* glua_getTable_str( int i, size_t* lref=NULL )
        { return _glua_getLString( glua_useKey(i), 0, lref,NULL ); }
    inline const char* glua_getTable_str( int i, const char* k, size_t* lref=NULL )
        { return _glua_getLString( glua_useKey(i,k), 0, lref,NULL ); }
    inline const char* glua_getTable_str( int i, int j, size_t* lref=NULL )
        { return _glua_getLString( glua_useKey(i,j), 0, lref,NULL ); }
    //
    #ifdef UNICODE
      // Note: Not sure if the 'lref' approach (getting length of the incoming string in bytes)
      //       is really applicable (or, useful) for Unicode strings.
      //
      inline const WCHAR* glua_getTable_wstr( const char* k ) { return _glua_getWLString( glua_useKey(k), 0, NULL,NULL ); }
      inline const WCHAR* glua_getTable_wstr( int i )         { return _glua_getWLString( glua_useKey(i), 0, NULL,NULL ); }
      inline const WCHAR* glua_getTable_wstr( int i, const char* k ) { return _glua_getWLString( glua_useKey(i,k), 0, NULL,NULL ); }
      inline const WCHAR* glua_getTable_wstr( int i, int j )  { return _glua_getWLString( glua_useKey(i,j), 0, NULL,NULL ); }
    #endif
    //
    inline void* glua_getTable_ud( const char* k, tag_int tag ) { return _glua_getUserdata( glua_useKey(k), 0, tag, NULL ); }
    inline void* glua_getTable_ud( int i, tag_int tag )         { return _glua_getUserdata( glua_useKey(i), 0, tag, NULL ); }
    inline void* glua_getTable_ud( int i, const char* k, tag_int tag ) { return _glua_getUserdata( glua_useKey(i,k), 0, tag, NULL ); }
    inline void* glua_getTable_ud( int i, int j, tag_int tag )  { return _glua_getUserdata( glua_useKey(i,j), 0, tag, NULL ); }
    //
    inline int glua_getTable_enum( const char* k, tag_int tag ) { return _glua_getEnum( glua_useKey(k), 0, tag, 0 ); }
    inline int glua_getTable_enum( int i, tag_int tag )         { return _glua_getEnum( glua_useKey(i), 0, tag, 0 ); }
    inline int glua_getTable_enum( int i, const char* k, tag_int tag ) { return _glua_getEnum( glua_useKey(i,k), 0, tag, 0 ); }
    inline int glua_getTable_enum( int i, int j, tag_int tag )  { return _glua_getEnum( glua_useKey(i,j), 0, tag, 0 ); }
    //
    inline bool_int glua_getTable_bool( const char* k )     { return _glua_getBoolean( glua_useKey(k), 0, FALSE ); }
    inline bool_int glua_getTable_bool( int i )             { return _glua_getBoolean( glua_useKey(i), 0, FALSE ); }
    inline bool_int glua_getTable_bool( int i, const char* k ) { return _glua_getBoolean( glua_useKey(i,k), 0, FALSE ); }
    inline bool_int glua_getTable_bool( int i, int j )      { return _glua_getBoolean( glua_useKey(i,j), 0, FALSE ); }
    //
    inline ulong glua_getTable_uint( const char* k )        { return _glua_getUnsigned( glua_useKey(k), 0, 0 ); }
    inline ulong glua_getTable_uint( int i )                { return _glua_getUnsigned( glua_useKey(i), 0, 0 ); }
    inline ulong glua_getTable_uint( int i, const char* k ) { return _glua_getUnsigned( glua_useKey(i,k), 0, 0 ); }
    inline ulong glua_getTable_uint( int i, int j )         { return _glua_getUnsigned( glua_useKey(i,j), 0, 0 ); }
    //
    inline enum e_Glua_Type glua_getTable_type( const char* k )        { return _glua_type( glua_useKey(k), -1 ); }
    inline enum e_Glua_Type glua_getTable_type( int i )                { return _glua_type( glua_useKey(i), -1 ); }
    inline enum e_Glua_Type glua_getTable_type( int i, const char* k ) { return _glua_type( glua_useKey(i,k), -1 ); }
    inline enum e_Glua_Type glua_getTable_type( int i, int j )         { return _glua_type( glua_useKey(i,j), -1 ); }
    //
  #endif
  extern "C" {
#endif  // C++

//---
// These are jointly for all compilers:
//
bool_int _glua_gettablekey( lua_State* L,
		    			    uint argn,
                            const char** key_ref, 
                            enum e_Glua_Type* type_ref );

#define /*bool_int*/ glua_tableKey( key_ref, type_ref ) \
        _glua_gettablekey( _getL(), _getT(), (key_ref), (type_ref) )

#define /*uint*/ glua_tableN() \
        ( (_getT()>0) ? _glua_getn( _getL(), _getT() ) : 0 )  // ('nil' table)

//---
void _glua_setudref( lua_State* L, void* ptr, int n );
int _glua_getudref( lua_State* L, void* ptr, const char* member );

#define glua_setudref( ptr, n )   /*void*/ _glua_setudref( _lua_, ptr, n )
#define glua_getudref( ptr, key ) \
    ( (_retcount_= /*0/1/2*/ _glua_getudref( _lua_, ptr, key )) != 0)


//---
// Dynamic linkage support (for modules):
//
void _ll_unloadlib( void* lib );
void* _ll_load( lua_State* L, const char* path );
lua_CFunction _ll_sym( lua_State* L, void* lib, const char* sym );


/*--- Asynchronous callbacks -----------*/
//
// Async callbacks (those provided in a separate thread, s.a. SDL_Mixer events)
// need to be passed to Lua through a queue. Lua state may not be manipulated
// by the alien threads.

struct s_Glua_Queue;     // opaque implementation details

enum e_Glua_QueuePolicy
{
    // Note: don't change the values, they're part of module ABI!
    GLUA_QUEUE_KEEP_ALL= 0,    // default: keep all data (nothing lost)
    GLUA_QUEUE_KEEP_FIRST= 1,  // keep first data (lose later until buffer read)
    GLUA_QUEUE_KEEP_LAST= 2,   // keep last data (lose earlier, non-read data)
};

// Array style values are the same as SDL_Mixer's Uint16 'audio_format':
//
#define GLUA_QUEUE_ARRAY_SIGNED (0x8000)
//#define GLUA_QUEUE_ARRAY_TWISTED (0x1000)   // MSB/LBS twist

#if (defined PLATFORM_WIN32) || (defined PLATFORM_WINCE)
  #define GLUA_QUEUE_ARRAY_MESSEDUP (0x0800)  // special flag to fix an SDL_Mixer bug?
#endif

#define GLUA_QUEUE_ARRAY_S8   (8|GLUA_QUEUE_ARRAY_SIGNED)
#define GLUA_QUEUE_ARRAY_U8   8
#define GLUA_QUEUE_ARRAY_S16  (16|GLUA_QUEUE_ARRAY_SIGNED)
#define GLUA_QUEUE_ARRAY_U16  16
#define GLUA_QUEUE_ARRAY_S32  (32|GLUA_QUEUE_ARRAY_SIGNED)
#define GLUA_QUEUE_ARRAY_U32  32

void _glua_queue_attach( lua_State *L, 
                         struct s_Glua_Queue** qref,
                         int closure_argn, 
                         uint /*enum e_Glua_QueuePolicy*/ policy );

bool_int _glua_queue_add( struct s_Glua_Queue* queue, 
                          void* p, 
                          uint /*size_t*/ bytes, 
                          uint style );

bool_int _glua_queue_run( lua_State *L, 
                          struct s_Glua_Queue* queue );

#define glua_queue_attach( qref, argn, policy ) \
       _glua_queue_attach( _lua_, qref, argn, policy )

#define glua_queue_detach( qref ) \
       _glua_queue_attach( _lua_, qref, 0, 0 )

#define glua_queue_add( q ) \
       _glua_queue_add( q, NULL, 0, 0 )

// TBD: Here might be a danger of MSB/LSB mixup?  Should we do a special
//      'glua_queue_add_int()' function?  Hope not.
//
#define glua_queue_add_int( q, v ) \
       _glua_queue_add( q, &(v), sizeof(int), GLUA_QUEUE_ARRAY_S32 )

#define glua_queue_add_intarray   _glua_queue_add

#define glua_queue_run(q)  _glua_queue_run( _lua_, (q) )


/*---------------------
// GLUA_FUNC & GLUA_END macros
//
// Usage:   GLUA_FUNC( BitAnd )
//          {   // <-- (allows matching braces as in normal functions)
//              int a = glua_getInteger(1);
//              int b = glua_getInteger(2);
//
//              glua_pushNumber( a & b );
//              // <-- (no 'return' required, is calculated automatically)
//          }
//          GLUA_END
//
// Note:    The braces in the above sample are just for the "looks" - they
//          are not strictly required since the actual braces of the function
//          are within the macros. However, this makes the code look more 
//          like 'normal' C functions.
//
----------------------*/

#define GLUA_FUNC_DECLARE(fname) \
	static int fn_##fname(lua_State* _lua_);

#define GLUA_FUNC(fname) \
    static int fn_##fname(lua_State* _lua_) \
    { \
    uint _retcount_= 0; \
    uint _tbl_argn_= 0; \
	_setL(_lua_); \
	PERFINFO_AUTO_START("API:"#fname, 1); \
	do {	// this 'do' allows 'break' to be used to get into 'GLUA_END'

#define GLUA_HELPER_FUNC_DECLARE(fname, ...) \
	int fnh_##fname(lua_State* _lua_, __VA_ARGS__);

#define GLUA_HELPER_FUNC(fname, ...) \
	int fnh_##fname(lua_State* _lua_, __VA_ARGS__) \
    { \
    uint _retcount_= 0; \
    uint _tbl_argn_= 0; \
	_setL(_lua_); \
	PERFINFO_AUTO_START("API Helper:"#fname, 1); \
	do {	// this 'do' allows 'break' to be used to get into 'GLUA_END'

#define GLUA_HELPER_CALL(fname, ...) \
	_retcount_ = fnh_##fname(_lua_, __VA_ARGS__)

#define GLUA_END \
	} while(0); /* 'break' within function body jumps here */ \
    /* (dummy casts make sure we never get "unused variable" warnings) */ \
	PERFINFO_AUTO_STOP(); \
    (void)_tbl_argn_; \
    return _retcount_; \
    }
   // <-- matches the brace within 'GLUA_FUNC' macro

#define GLUA_ARG_COUNT_CHECK(fname, min_argn, max_argn) \
	if(glua_argn() < min_argn) { glua_error(#fname": requires at least "#min_argn" arguments\n"); break; } \
	if(glua_argn() > max_argn) { glua_error(#fname": can accept at most "#max_argn" arguments\n"); break; }

#define GLUA_ARG_CHECK(fname, arg, arg_type, arg_opt, err_msg) \
	if(glua_argn() > (arg-1) && !(glua_type(arg) & (arg_type))) { \
		char errorbufohgoddontreusethisname[1024]; \
		sprintf(errorbufohgoddontreusethisname, "%s: %s argument %d got %s, expected %s", #fname, arg_opt?"optional":"required", arg, glua_typename(arg), err_msg); \
		glua_error(errorbufohgoddontreusethisname); \
		break; }

#define GLUA_ASSERT(fname, test, err_msg) \
	if(!(test)) { glua_error(#fname": "err_msg); break; }

#define glua_Forward( fname ) \
    _retcount_= fn_##fname( _lua_ )

/*--- Cryptic Object Support ------------------------------------------*/

typedef enum GluaObjFuncEntryType
{
	GLUAOBJFUNCENTRYTYPE_FUNCTION = 0,
	GLUAOBJFUNCENTRYTYPE_VARIABLE,

	GLUAOBJFUNCENTRYTYPE_COUNT
} GluaObjFuncEntryType;

typedef int (*GluaVarFunc)(lua_State *L, void *obj, const char *member);

#define GLUA_VAR_SET_INDEX 3
#define GLUA_VAR_FUNC_DECLARE(funcName) int fn_var_##funcName(lua_State *_lua_, void *obj, const char *member)
#define GLUA_VAR_FUNC(funcName) int fn_var_##funcName(lua_State *_lua_, void *obj, const char *member) {\
    uint _retcount_= 0; \
    uint _tbl_argn_= 0; \
	_setL(_lua_); \
	do {	// this 'do' allows 'break' to be used to get into 'GLUA_END'

typedef struct GluaObjFuncEntry
{
	GluaObjFuncEntryType type;
	const char *name;
	void *funcPtr;
	GluaVarFunc getFuncPtr;
	GluaVarFunc setFuncPtr;
} GluaObjFuncEntry;

int gluaObjFuncDispatch(lua_State *_lua_, tag_int tag, GluaObjFuncEntry *entries, int count, int bSetTable);

#define GLUA_TAG_DEFINE( TAGNAME ) \
	static tag_int TAGNAME; \
	GLUA_FUNC_DECLARE( TAGNAME##_get_dispatch ); \
	GLUA_FUNC_DECLARE( TAGNAME##_set_dispatch ); \
	static GluaObjFuncEntry TAGNAME##_funclist_[] = 

#define GLUA_TAG_FUNCTION( name, func )             { GLUAOBJFUNCENTRYTYPE_FUNCTION, name, fn_##func, NULL, NULL }
#define GLUA_TAG_VARIABLE( name, getFunc, setFunc ) { GLUAOBJFUNCENTRYTYPE_VARIABLE, name, NULL, fn_var_##getFunc, fn_var_##setFunc }

#define GLUA_TAG_END( TAGNAME ) ; \
	GLUA_FUNC( TAGNAME##_get_dispatch ) { _retcount_ = gluaObjFuncDispatch(_lua_, TAGNAME, TAGNAME##_funclist_, (sizeof(TAGNAME##_funclist_)/sizeof(*TAGNAME##_funclist_)), false); } GLUA_END \
	GLUA_FUNC( TAGNAME##_set_dispatch ) { _retcount_ = gluaObjFuncDispatch(_lua_, TAGNAME, TAGNAME##_funclist_, (sizeof(TAGNAME##_funclist_)/sizeof(*TAGNAME##_funclist_)), true ); } GLUA_END

GLUA_VAR_FUNC_DECLARE(GLUA_VAR_READONLY);
GLUA_VAR_FUNC_DECLARE(GLUA_VAR_WRITEONLY);

//#define GLUA_TAG_PUSH(TAG, OBJPTR, PERFORMCOPY) ; \
//	{ int _idx_; int _count_ = sizeof(TAG##funclist_)/sizeof(*TAG##funclist_); \
//	lua_createtable(_lua_, 0, _count_+1); \
//	lua_pushnumber(_lua_, 0); \
//	if(PERFORMCOPY) { glua_pushUserdata_copy(OBJPTR, TAG); } /* Increments _returnval_ for us */ \
//	else            { glua_pushUserdata_ptr (OBJPTR, TAG); } /* Increments _returnval_ for us */ \
//	lua_settable(_lua_, -3); \
//	for(_idx_=0; _idx_<_count_; _idx_++) { \
//		lua_pushstring(_lua_, TAG##funclist_[_idx_].funcName); \
//		lua_pushcclosure(_lua_, TAG##funclist_[_idx_].funcPtr, 0); \
//		lua_settable(_lua_, -3); \
//	} }
//
//#define GLUA_TAG_GET(argn, tag) (glua_getTable(1) ? (glua_getTable_i_ud(0, tag)) : NULL)

/*--- Dynamic extension support ------------------------------------------*/
//
// Note: We use 'uint' instead of 'enum' because this is the interface 
//		 to dynamic modules and size of 'enum' may vary between compilers.
//
typedef int   luafunc_i_i( int );
typedef int   luafunc_i_si( const char*, int );
typedef void  luafunc_v_L( lua_State* );
typedef void  luafunc_v_Lb( lua_State*, bool_int );
typedef void  luafunc_v_Ld( lua_State*, double );
typedef void  luafunc_v_Lf( lua_State*, float );
typedef void  luafunc_v_Ls( lua_State*, const char* );
typedef void  luafunc_v_Lsi( lua_State*, const char*, int );
typedef int   luafunc_i_Lsi( lua_State*, const char*, int );
typedef void* luafunc_V_LtVuu8( lua_State*, tag_int, const void*, uint, uint8 );
typedef int   luafunc_i_L( lua_State* );
typedef int   luafunc_i_Li( lua_State*, int );
typedef bool_int luafunc_b_Li( lua_State*, int );
typedef double luafunc_d_Li( lua_State*, int );
typedef float luafunc_f_Li( lua_State*, int );
typedef const char* luafunc_s_Li( lua_State*, int );
typedef void  luafunc_v_Li( lua_State*, int );
typedef void  luafunc_v_Lii( lua_State*, int, int );
typedef int   luafunc_i_Liii( lua_State*, int, int, int );
typedef void  luafunc_v_Lteu8( lua_State*, tag_int, enum_int, uint8 );
typedef void  luafunc_v_Lieu( lua_State*, int, enum_int, uint );
typedef void* luafunc_V_Li( lua_State*, int );
typedef void* luafunc_V_LiT( lua_State*, int, tag_int* );
typedef enum_int luafunc_e_LiT( lua_State*, int, tag_int* );
typedef uint /*e_GluaType*/ luafunc_u_Lit( lua_State*, int, tag_int );
typedef void  luafunc_v_LtsF( lua_State*, tag_int, const char*, lua_CFunction );
typedef void  luafunc_v_LFu( lua_State*, lua_CFunction, uint );
typedef ulong luafunc_u_Li( lua_State*, int );
typedef void  luafunc_v_Lis( lua_State*, int, const char* );
typedef const char* luafunc_s_LiU( lua_State*, int, uint* );
typedef void* luafunc_V_s( const char* );
typedef void  luafunc_v_V( void* );
typedef void* luafunc_V_Vs( void*, const char* );
typedef const char* luafunc_s_Su( char*, uint );
typedef int   luafunc_i_LiD( lua_State*, int, lua_Debug* );
typedef int   luafunc_i_LsD( lua_State*, const char*, lua_Debug* );
typedef int   luafunc_i_LRVs( lua_State*, lua_Chunkreader, void*, const char* );
typedef const char* luafunc_s_LsA( lua_State*, const char*, va_list argp );
typedef void  luafunc_v_Ll( lua_State*, long );
typedef void  luafunc_v_Lu( lua_State*, ulong );
typedef long  luafunc_l_Li( lua_State*, int );
typedef tag_int luafunc_t_L( lua_State* );
typedef tag_int luafunc_t_Lsb( lua_State*, const char*, bool_int );
typedef void* luafunc_V_Ls( lua_State*, const char* );
typedef lua_CFunction luafunc_F_LVs( lua_State*, void*, const char* );
typedef const char* luafunc_s_Lt( lua_State*, tag_int );
typedef void luafunc_v_LQiu( lua_State*, struct s_Glua_Queue**, int, uint /*e_Glua_QueuePolicy*/ );
typedef bool_int luafunc_b_qVuu( struct s_Glua_Queue*, void* p, uint /*size_t*/, uint );
typedef bool_int luafunc_b_Lq( lua_State*, struct s_Glua_Queue* );
//...

//-----
// Function table used by the Lua host environment to pass some of
// its functions on to GluaX extension modules.
//
// NOTE: Existing entries MAY NOT BE REMOVED OR REORDERED since doing so would
//       mess up already compiled hosts & extension modules! New entries may
//       be added at the end if more interaction with the Lua engine is needed.
//
struct s_GluaFuncs
{
	// Note: These field names are the same as actual GluaX function names.
	//       This makes static/dynamic linkage macros more simple.
	//
    luafunc_v_Ls    *_glua_error;
    luafunc_i_i     *_glua_ver;
	luafunc_u_Lit	*_glua_type2;
	luafunc_b_Li	*_glua_toboolean;
    luafunc_d_Li    *_glua_tonumber_raw_d;
    luafunc_f_Li    *_glua_tonumber_raw_f;
    luafunc_d_Li    *_glua_tonumber_safe_d;
    luafunc_f_Li    *_glua_tonumber_safe_f;
    luafunc_V_LiT   *_glua_touserdata;    
    luafunc_e_LiT   *_glua_toenum;
    luafunc_v_Lb    *_glua_pushboolean;
    luafunc_v_Ld    *_glua_pushnumber_d;
    luafunc_v_Lf    *_glua_pushnumber_f;
    luafunc_V_LtVuu8 *_glua_pushuserdata_raw;
    luafunc_v_Lteu8 *_glua_pushenum;
    luafunc_v_L     *_glua_newtable;    // was: glua_pushtable()
    luafunc_v_L     *_glua_totable;
    luafunc_i_L     *_glua_gettop;
    luafunc_v_Li    *_glua_settop;
    luafunc_v_Ls    *_glua_setglobal;
    luafunc_b_Li    *_glua_checkstack;
    luafunc_t_L     *_glua_newtag;

    // Added 26-Sep-02 to support 'getTable()':
    //
    luafunc_v_Li    *_glua_gettable;    // _glua_fromtable
    luafunc_b_Li    *_glua_next;
	luafunc_v_Li    *_glua_remove;
	luafunc_v_Li    *_glua_pushvalue;
    
	// Added 18-Nov-02 to support multithreading:
	//
	luafunc_i_si	*_glua_exec;

    // Added 2-Dec-02:
    //
    luafunc_i_Li    *_glua_getn;
	luafunc_v_Li	*_glua_concat;

	// Added 13-Dec-02:
	//
	luafunc_v_L		*_glua_getreg;
	luafunc_v_L		*_glua_setreg;
	luafunc_v_LtsF	*_glua_settagmethod;

    // Added 14-Feb-03:
    //
	// Note: Gluaport functions needed for classic Lua C API compatibility,
    //       not GluaX itself.
	//
    luafunc_i_Li    *_gluaport_strlen;
    luafunc_v_Lsi   *_glua_pushclstring;
    luafunc_v_LFu   *_glua_pushcclosure;
	luafunc_v_Li	*_gluaport_rawset;
	luafunc_v_Lii	*_gluaport_rawseti;
	luafunc_v_Li    *_glua_insert;
	luafunc_v_Lii   *_gluaport_call;
	luafunc_v_Li	*_gluaport_settable;
    luafunc_v_Lii   *_gluaport_settag;
	luafunc_v_Lis	*_gluaport_settagmethod;

    // Added 16-Mar-03:
    //
    luafunc_s_LiU   *_glua_tolstring_raw;

    // Added 20-Jan-05:
    //
    luafunc_v_V     *_ll_unloadlib;
    luafunc_V_Ls    *_ll_load;
    luafunc_F_LVs   *_ll_sym;

    // Added 2-Sep-03:
    luafunc_v_Lii   *_glua_call;

    // Added 9-Oct-03:
    luafunc_i_Lsi   *_glua_create_tbl;

    // Added 12-Jan-04:     Full lauxlib support :)
    //
    luafunc_i_LiD   *_gluaport_getstack;
    luafunc_i_LsD   *_gluaport_getinfo;
    luafunc_i_Li    *_gluaport_type;
    luafunc_i_Liii  *_gluaport_pcall;
    luafunc_i_LRVs  *_gluaport_load;
    luafunc_s_LsA   *_gluaport_pushvfstring;
    luafunc_v_Li    *_gluaport_rawget;
    luafunc_v_Lii   *_gluaport_rawgeti;
    luafunc_i_Li    *_gluaport_setmetatable;
    luafunc_i_Li    *_gluaport_getmetatable;

    // Added 24-Mar-04:     Lua 5.1-work0 support
    //
    luafunc_v_Ll    *_glua_pushinteger;
    luafunc_v_Lu    *_glua_pushunsigned;
    luafunc_l_Li    *_glua_tointeger_raw;
    luafunc_u_Li    *_glua_tounsigned_raw;  // 28-Dec-04
	luafunc_v_Li    *_gluaport_replace;

    // Added 22-Dec-04:
    luafunc_v_Lieu  *_glua_modenum;
    
    // Added 28-Dec-04:
    luafunc_t_Lsb   *_glua_regtag2;

    // Added 14-Mar-05:
    luafunc_s_Lt    *_glua_tagname;

    // Added 5-Apt-05:
    luafunc_v_LQiu  *_glua_queue_attach;
    luafunc_b_qVuu  *_glua_queue_add;
    luafunc_b_Lq    *_glua_queue_run;

    //...(can be continued)...
};

#ifdef GLUA_STATIC
  // 
  // This macro is used by 'gluahost.c' (or other host application) to fill in
  // the host-side 's_GluaFuncs' structure.
  //
  #define GLUA_LINKS \
	_glua_error,\
	_glua_ver,\
	_glua_type2,\
	_glua_toboolean,\
    _glua_tonumber_raw_d,\
    _glua_tonumber_raw_f,\
    _glua_tonumber_safe_d,\
    _glua_tonumber_safe_f,\
    _glua_touserdata,\
    _glua_toenum,\
	_glua_pushboolean,\
    _glua_pushnumber_d,\
    _glua_pushnumber_f,\
    _glua_pushuserdata_raw,\
    _glua_pushenum,\
    _glua_newtable,\
    _glua_totable,\
    _glua_gettop,\
    _glua_settop,\
    _glua_setglobal,\
    _glua_checkstack,\
    _glua_newtag,\
	_glua_gettable,\
	_glua_next,\
	_glua_remove,\
	_glua_pushvalue,\
	_glua_exec,\
    _glua_getn,\
	_glua_concat,\
	_glua_getreg,\
	_glua_setreg,\
	_glua_settagmethod,\
	\
	_gluaport_strlen,\
	_glua_pushclstring,\
	_glua_pushcclosure,\
	_gluaport_rawset,\
	_gluaport_rawseti,\
	_glua_insert,\
	_gluaport_call,\
	_gluaport_settable,\
	_gluaport_settag,\
	_gluaport_settagmethod,\
    \
    _glua_tolstring_raw,\
    \
    _ll_unloadlib,\
    _ll_load,\
    _ll_sym,\
    \
    _glua_call,\
    _glua_create_tbl,\
    \
    _gluaport_getstack,\
    _gluaport_getinfo,\
    _gluaport_type,\
    _gluaport_pcall,\
    _gluaport_load,\
    _gluaport_pushvfstring,\
    _gluaport_rawget,\
    _gluaport_rawgeti,\
    _gluaport_setmetatable,\
    _gluaport_getmetatable,\
    \
    _glua_pushinteger,\
    _glua_pushunsigned,\
    _glua_tointeger_raw,\
    _glua_tounsigned_raw,\
    _gluaport_replace,\
    \
    _glua_modenum,\
    _glua_regtag2,\
    _glua_tagname,\
    \
    _glua_queue_attach,\
    _glua_queue_add,\
    _glua_queue_run,\
    //
  #define glua_link( L, funcs_s, gluamodule_ptr, filename, tbl_idx ) \
          (*gluamodule_ptr)( L, &(funcs_s), sizeof(funcs_s), filename, tbl_idx )
  //
#endif  // static

#if (defined PLATFORM_WIN32) || (defined PLATFORM_WINCE)
  #ifdef GLUA_DYNAMIC
    #define GLUA_EXPORT  __declspec(dllexport)
  #else
    #define GLUA_EXPORT /* nothing (static link) */
  #endif
  #define GLUA_STDCALL __stdcall
  //
#else
  #define GLUA_EXPORT   /* nothing */
  #define GLUA_STDCALL  /* nothing */
#endif


// Prototype of the module initialisation ('GluaModule()') function:
//
// NOTE: We fake the 'lua_State*' pointer as a 'void*' for CVI 5.5 compiler
//       because the ODL Type Library would otherwise have problems with it.
//       Only 'GluaModule()' is affected by this since it is the only 
//       function we need to export.
//
#ifdef _CVI_	// LabWindows/CVI 5.5 compiler
  #define LuaState_PTR void*
#else
  #define LuaState_PTR lua_State*
#endif

typedef int (GLUA_STDCALL t_GluaModuleFunc)( LuaState_PTR lua,
											 const void* v_func_tbl,
											 uint func_tbl_size,
											 const char* my_dll_path,
											 int my_tbl_index );

int _glua_create_tbl( lua_State* lua, 
                      const char* table_name,
                      int parent_tbl /* 0=create as global */ );

#define glua_create_tbl( name, parent )  _glua_create_tbl( _lua_, name, parent )

#if ((defined _MSC_VER) && (defined GLUA_DYNAMIC))
  //
  // MSC did not like the declaration below (without it, C++ compilations 
  // do name mangling)
  //
#else
  _GLUA_EXTERN_C 
  t_GluaModuleFunc GluaModule;   // Plain 'GluaModule()' always declared
#endif

// 'GLUA_MODULE()' definition may be overridden in projects where several
// modules are statically linked (to avoid function name collisions).
//
// It can also be used for cutting huge modules' source code into separate
// compilation units.
//
#ifndef GLUA_MODULE_NAME
  #define GLUA_MODULE_NAME GluaModule  // default
#endif

#ifndef _SET_GLUAPORT_NAMESPACE   // defined in 'gluaport.h'
  #define _SET_GLUAPORT_NAMESPACE(var)  /*nothing*/
#endif

// Returns: GLUA_VERSION_ERR_... or 0 for success
//
#define _GLUA_MODULE( mod_name, path_var, tbl_var ) \
	\
    _GLUA_EXTERN_C /*avoid C++ name mangling*/ \
    int GLUA_EXPORT GLUA_STDCALL \
    mod_name ( LuaState_PTR _lua_, \
                const void* _ftbl_, \
                uint _ftbl_size_, \
                const char* path_var, \
                int tbl_var /* >0 (table) or 0 (global namespace) */ ) \
    { \
    const char* _mypath_= (path_var); \
    int _mytbl_= (tbl_var); \
    (void)_mypath_; (void)_mytbl_; /*need not be used*/ \
    _SET_GLUAPORT_NAMESPACE(tbl_var); \
	{ int _host_ver_= _glua_linktohost( _lua_, _ftbl_, _ftbl_size_ ); \
    \
	if (_host_ver_ < 0)  /* error? */ \
        return _host_ver_;  // (function table mismatch, newer gluahost needed!)
	// ...continues with { + function body...

#define GLUA_MODULE( path_var, tbl_var ) \
        _GLUA_MODULE( GLUA_MODULE_NAME, path_var, tbl_var )

#define GLUA_MODULE_END \
	} _SET_GLUAPORT_NAMESPACE(0); /*namespace no longer available*/ \
    return 0; /*init successful*/ }

//---
#define GLUA_SUBMODULE( name, tbl_var ) \
        _GLUA_MODULE( name, _nopath_, tbl_var )

#define GLUA_SUBMODULE_END \
        GLUA_MODULE_END

#define GLUA_SUBMODULE_DECL( funcname ) \
  _GLUA_EXTERN_C t_GluaModuleFunc funcname

#define GLUA_SUBMODULE_INIT( funcname, tbl ) \
    /*int*/ ( funcname ( _lua_, _ftbl_, _ftbl_size_, NULL, tbl ) )


/*---------------------
// GLUA_DECL macros
//
// Usage:   GLUA_DECL( tbl_index )
//              { 
//              glua_func(...),
//              ...
//              glua_const(...),
//              ...
//              glua_str(...)
//              }
//          GLUA_DECL_END
//
----------------------*/

#define GLUA_DECL( tbl_index ) \
  { int _mytbl_= (tbl_index); \
    static struct s_GluaItem _items_[]=   //... (list of items follows)

  // function entries:
  #define glua_func( func_name )            { #func_name, fn_##func_name, NULL, NULL, GLUA_NUM_ZERO }
  #define glua_func2( lua_name, func_name ) { #lua_name, fn_##func_name, NULL, NULL, GLUA_NUM_ZERO }

  // string constants:
  #define glua_str( str_name )              { #str_name, NULL, str_name, NULL, GLUA_NUM_ZERO }
  #define glua_str2( lua_name, str_value )  { #lua_name, NULL, str_value, NULL, GLUA_NUM_ZERO }

  // number constants:
  #define glua_const( var_name )            { #var_name, NULL, NULL, NULL, var_name }
  #define glua_const2( lua_name, num_value ) { #lua_name, NULL, NULL, NULL, num_value }

  // userdata type tags:
  #define glua_tag( name, tag_ref )         { name, NULL, NULL, tag_ref, GLUA_NUM_ZERO }
  #define glua_enum( name, tag_ref )        { name, NULL, NULL, tag_ref, GLUA_NUM_ONE }

  #define glua_tagmethod( tag, event_str, func_name ) \
                        { NULL, fn_##func_name, event_str, &tag, GLUA_NUM_ZERO }

  #define glua_tagdispatch( TAG ) \
		glua_tagmethod( TAG, "gettable", TAG##_get_dispatch), \
		glua_tagmethod( TAG, "settable", TAG##_set_dispatch)

  // module info:
  #define glua_info( label, value )         { "_info." label, NULL, value, NULL, GLUA_NUM_ZERO }

  // wrapper code (optional):
  #define glua_wrap( label, data, len )     { "_wrap." label, NULL, data, NULL, len }

// Note: Keep 'lua' parameter named as it is (plain "lua") to allow usage
//       from both statically linked extensions and those using 'GLUA_MODULE'.
//
#define GLUA_DECL_END \
   ; \
   _glua_register2( _lua_, _items_, sizeof(_items_)/sizeof(*_items_), _mytbl_ );\
   }


/*---------------------
// GLUA_CALLBACK macros  (for calling Lua code from within a C/C++ function)
//
// Usage:   GLUA_CALLBACK
//              {
//              if (!glua_getudref( this, "eventName" ))
//                  break;  // no handler
//
//              glua_pushxx(...);
//              ...
//              glua_call();
//              ...
//              glua_getxx(...);
//              }
//          GLUA_CALLBACK_END
//
// Note: Userdata must be tied to its wrapper object by 'glua_setudref()'.
----------------------*/

#define GLUA_CALLBACK \
    { \
    uint _retcount_ = 0; /* will be set by 'glua_getudref()' call */ \
    uint _tbl_argn_= 0; \
	lua_State* _lua_= _getL(); \
	do {	/* 'break' will take to 'GLUA_CALLBACK_END' */ \
        int _tos_= _glua_gettop(_lua_); \

// '_retcount_' tells how many 'glua_pushxx()' calls there have been
// '-1' reduces the function entry itself (see 'glua_getudref')
#define glua_call() \
        _glua_call( _lua_, _retcount_-1 /*arguments pushed*/, -1 /*LUA_MULTRET*/ )

#define GLUA_CALLBACK_END \
        _glua_settop(_lua_,_tos_); /* pop away the results */ \
    \
	} while(0); /* 'break' jumps here */ \
    /* (dummy casts make sure we never get "unused variable" warnings) */ \
    (void)_tbl_argn_; \
    }


/*--- 'glua_50.c' etc. functions (implementation details) -----------*/
//
#ifdef GLUA_STATIC
  //
  #ifdef LUA_V40	  // Lua 4.0 static
    //
    void* Loc40_PushUserdata( lua_State*, const void*, int, lua_CFunction );
    //
  #else     // 5.0 and later
    //
    bool8 Loc50_PushMetatableRef( lua_State*, tag_int tag, bool8* may_create );
    void* Loc50_PushUserdataExt( lua_State*, const void* ptr, int size, tag_int tag, uint8 mode );
    bool8 Loc50_SetTagMethod( lua_State*, tag_int tag, const char* event, bool8 push_old_func );
    void* Loc50_ToUserdata( lua_State* L, int argn, tag_int* tagref );
    #ifndef ENUM_PATCH
      enum_int Loc50_ToEnum( lua_State* L, int argn, tag_int* tagref );
	  void Loc50_ModEnum( lua_State* L, int argn, enum_int val, uint mask );
      void Loc50_InitCs(void);
    #endif
	void Loc50_SetTag( lua_State* L, int argn, tag_int tag );
	glua_num_t Loc50_ConvToNumber( lua_State* L, int argn );
  #endif
#endif  // GLUA_STATIC


/*--- Compatibility with Glua 1.0 apps ------------------------*/
//
// Note: There's been so many changes that this compatibility may no longer work.
//
#ifdef GLUA10_COMPATIBLE
  //
  typedef struct s_GluaItem glua_fitem;
  typedef struct s_GluaItem glua_citem;

  #define glua_regFunctions(flist)\
    _glua_register(lua, flist, (sizeof(flist) / sizeof(glua_fitem)), NULL)

  #define glua_regConstants(clist)  glua_regFunctions(clist)

  #define glua_fn(fname)            glua_func(fname)
  #define glua_function(fname)      GLUA_FUNC(fname)  
  #define glua_getNone(argn)        _glua_getDone(_lua_,argn)
  //
  int _glua_regtag( lua_State *lua, const char *tagname );
  #define glua_newtag(tagname)		_glua_regtag( lua, tagname )
  //
#endif  // GLUA10_COMPATIBLE

#ifdef __cplusplus
  } // extern "C"
#endif

#endif  // GLUAX_H
