/*---------------------
//
// GLUAX.C              Copyright (c) 2002-05, asko.kauppi@sci.fi
//
// This module needs to be compiled into any program using 'gluax.h'.
//
// License: Zlib (see license.txt)
//
// To-do:   Lua 4.0 compatibility not checked lately, may need fixes.
//
----------------------*/

#include "gluax.h"

#include <string.h>		// strlen() etc.
#include <stdio.h>		// vsprintf()
#include <stdlib.h>		// min()
#include <ctype.h>		// isdigit()
#include <stdarg.h>
#include <math.h>		// floor()

//----
// AK(5-Aprr-05): Added 'glua_queue_...' functions.
// AK(14-Mar-05): Changed 'regtag2' behaviour to allow enum tags within Lua core.
// AK(14-Dec-04): Changed to using data values here instead (version numbers were imaginary).
// AK(13-Dec-04): 0x0231: change to 'glua_pushUserdata()' may break some old modules.
//
// Note: The version numbers are release dates in "YMMDD" format (this allows us to use
//       high order bits of int32 for something else, see GLUA_VERSION_BIT_...).
//       19 bits (see GLUA_VERSION_YYMMDD_MASK) is valid until 521231 (end of 2052).
//
#define GLUA_VERSION         50405      // Version of this GluaX host/module

#ifdef GLUA_DYNAMIC
  #define HOST_VERSION_REQ   50405      // Oldest host we can still live with
#else
  #define MODULE_VERSION_REQ 50322      // Oldest modules we can still support
#endif

#ifdef GLUA_STATIC
  #define TAG_PREFIX  "glua_tag_"
  #ifndef ENUM_PATCH
    #define ENUM_PREFIX "glua_enum_"
  #endif
#endif

//----
// First Aid for non-__VA_ARGS__ compilers:
// (Thread Specific Data needed or multithreading banned):
//
#if (defined PLATFORM_WIN32) && (defined COMPILER_MSC)  // not WinCE!
  //
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>	// Thread Local Storage
  //
  #define _TLS_UNINITIALIZED ((uint)(-1))
  //
  static DWORD tls_L= _TLS_UNINITIALIZED;
  static DWORD tls_T= _TLS_UNINITIALIZED;
  //  
  void _setL( lua_State* L ) { TlsSetValue(tls_L,L); }
  lua_State* _getL(void)	 { void* p= TlsGetValue(tls_L); assert(p); return (lua_State*)p; }
  int _setT( int argn )	 { TlsSetValue(tls_T,(void*)argn); return argn; }
  int _getT(void)			 { void* p= TlsGetValue(tls_T); return (int)p; }
  //
#else     // non-Win32-MSC
  //
  static lua_State* _LL= NULL;
  
  void _setL( lua_State* lua ) { if (!_LL) _LL=lua; assert(_LL==lua); }
  lua_State* _getL(void)	 { assert(_LL); return _LL; }

  #ifndef _HAS_VA_ARGS  // WinCE, QNX (2.95)
    static int _T= 0;
    int _setT(int argn)		 { _T= argn; return argn; }
    int _getT(void)			 { return _T; }
  #endif
#endif

#ifdef GLUA_STATIC
  #ifndef LUA_V40
    static CRITICAL_SECTION _glua_newtag_cs;
    extern CRITICAL_SECTION _glua_modenum_cs;    // gluax_50.c
  #endif
  static CRITICAL_SECTION _glua_regtag_cs = {0};
  static CRITICAL_SECTION _glua_queue_add_cs = {0};
#endif

//---
// IEEE 'float' range is 23 bits mantissa (http://www.math.byu.edu/~schow/work/IEEEFloatingPoint.htm)
//
// Cut execution if there's a chance that integer<->float conversions would lose accuracy
// (with 'double' mode there won't be any such limitation, unless we _want_ to do that for 
// portability).
//
// Note: The same limit applies for both signed and unsigned values, since a float has
//       separate sign bit.
// 
#if (defined GLUA_STATIC) && (defined USE_FLOAT) && (!defined INT32_PATCH)
  #error "Shouldn't need this any more -> use INT32_PATCH!"
  #define MANTISSA_23BITS( /*long*/ vl ) \
    if ( labs(vl) >= 16777216L /* 2^24 */  ) \
        _glua_errorN( L, "Integer <-> float conversion risks losing accuracy (%ld >= 2^24)\n", vl );
#endif

//---
// Enums can be done in two ways, userdata or Lua core patch.  Patching has
// the great benefit that enum values can be used as table keys. For this
// along, using ENUM_PATCH is recommended.  Performance-wise, there's not
// much difference.
//
#if (defined GLUA_STATIC) && (!defined ENUM_PATCH)
//  #error "Use ENUM_PATCH - why not?"
#endif

//----
char* _strcpy_safe( char* buf, const char* src, uint buflen )
{
    // Make sure the buffer is always terminated, even at fill-ups:
    //
    buf[buflen-1]= '\0';

    return strncpy( buf, src, buflen-1 /*max charcount*/ );
}

char* _strcat_safe( char* buf, const char* src, uint buflen )
{
    // 'strncat()' always terminates the buffer but the size param
    // is charcount of 'src', not the size of the buffer!
    //
    return strncat( buf, src, buflen -strlen(buf) -1 );
}

#ifdef UNICODE
  WCHAR* _wstrcpy_safe( WCHAR* buf, const WCHAR* src, uint buflen )
  {
    uint i;
    assert( buf && src );
    for( i=0; i<buflen; i++ )
        { buf[i]= src[i]; }
    buf[i]= (WCHAR)0;
    return buf;
  }
  //        
  WCHAR* _wstrcat_safe( WCHAR* buf, const WCHAR* src, uint buflen )
  {
    uint n;
    assert( buf && src );
    n= wcslen(buf);     // current length
    if (buflen > n+1)
        _wstrcpy_safe( &buf[n], src, buflen-n );
    return buf;
  }
#endif  // UNICODE


/*--- Interface to Lua engine ---------------------------------------------*/

//----
// Notes:
//		'lua_tonumber()' converts a numeric string parameter into actual number
//		type on Lua 4.0 but _not_ (necessarily) on Lua 5.0 and later. Do not 
//		rely on this behaviour!
//
//		'lua_tostring()' converts a number into string type (modifying the 
//		actual stack item) on both versions.
//----

// lua_...() functions can be used as such in statically linked code.
// HOST_CALL...() works over the DLL gate from dynamic modules.
//
#ifdef GLUA_DYNAMIC     // Accessing Lua host via a function table.
  //
  static const struct s_GluaFuncs* _lua_gate_= NULL;
  //
  #define HOST_CALL_1( func, a )		  ((_lua_gate_->func)(a))
  #define HOST_CALL_2( func, a,b )		  ((_lua_gate_->func)(a,b))
  #define HOST_CALL_4( func, a,b,c,d )    ((_lua_gate_->func)(a,b,c,d))
  #define HOST_CALL_L( func, L )          ((_lua_gate_->func)(L))
  #define HOST_CALL_L1( func, L,a )       ((_lua_gate_->func)(L,a))
  #define HOST_CALL_L2( func, L,a,b )     ((_lua_gate_->func)(L,a,b))
  #define HOST_CALL_L3( func, L,a,b,c )   ((_lua_gate_->func)(L,a,b,c))
  #define HOST_CALL_L4( func, L,a,b,c,d ) ((_lua_gate_->func)(L,a,b,c,d))
  //
#else   // Static linking, all comes from 'lua.h' directly.
  //
  #include "lauxlib.h"	// 'lua_dostring()' etc. compatibility macros for v5.0.
  #include "lualib.h"	// 'lua_baselibopen()' etc.
  //
  int fn_GcHub( lua_State* );
  //
  #ifdef LUA_V40
    static int fn_GcFree( lua_State* );
  #endif
  //
#endif  // GLUA_DYNAMIC


/*--- Debugging functions ---------------------------------------------*/

// The '_glua_assert()' function never returns. It can be called either from 
// within 'GLUA_FUNC' blocks via 'ASSERT' macro.
//
void _glua_assert( lua_State* L, uint line, const char* file, const char* condition )
{
const char* msg= "'ASSUME(%s)' failed at '%s' line %u.\n";  // ,condition,file,line
int i;

    if (!file) file="";
    if (!condition) condition="";
    
    // Path part in 'file' is not so important and better be skipped:
    //
    for( i=strlen(file)-1; i>=0; i-- )
        {
        if (strchr( "/\\", file[i] ))    // Is 'file[i]' a slash?
            { file= &file[i+1]; break; }
        }

    if (L)
        _glua_errorN( L, msg, condition, file, line );  // byebye..

    fprintf( stderr, msg, condition, file, line );
    
#ifdef PLATFORM_WINCE
    exit(-99);
#else
    abort();
#endif
}

static const char* Loc_Typename( enum e_Glua_Type type )
{
	switch( type )
		{
		//case GLUA_TYPE_NONE:	 return "none";
		case GLUA_TYPE_NIL:		 return "nil";
		case GLUA_TYPE_BOOLEAN:  return "bool";
		case GLUA_TYPE_INT:		 return "int";
		case GLUA_TYPE_NUMBER:   return "num";
		case GLUA_TYPE_STRING:	 return "str";
		case GLUA_TYPE_TABLE:	 return "tbl";
		case GLUA_TYPE_FUNCTION: return "func";
	    case GLUA_TYPE_ENUM:     return "enum";    //AK(13-Mar-05)
		case GLUA_TYPE_USERDATA:
		case GLUA_TYPE_USERDATA_UNK: return "usr";
        //
        default: break;
		}

	return "???";
}

#define DEBUG_STACK(L) _glua_dump(L)

void _glua_dump(lua_State* L)
{
int top= _glua_gettop(L);
enum e_Glua_Type type;
int i;

	fprintf( stderr, "\n\tDEBUG STACK:\n" );

	if (top==0)
		fprintf( stderr, "\t(none)\n" );

	for( i=1; i<=top; i++ )
		{
		type= _glua_type( L, i );

		fprintf( stderr, "\t[%d]= (%s) ", i, Loc_Typename(type) );

		// Print item contents here... (for some types)
		//
		switch( type )
			{
			case GLUA_TYPE_INT:
				fprintf( stderr, "%ld", (long)_glua_tointeger_raw(L,i) );
				break;
			
			case GLUA_TYPE_NUMBER:
                #ifdef USE_FLOAT
				  fprintf( stderr, "%f", (float)_glua_tonumber_raw(L,i) );
                #else
				  fprintf( stderr, "%lf", (double)_glua_tonumber_raw(L,i) );
                #endif
				break;

/**
            case GLUA_TYPE_INT32:
		        fprintf( stderr, "%ld", (long)_glua_toint32(L,i) );
				break;

            case GLUA_TYPE_INT64:
              #ifdef COMPILER_GCC
		        fprintf( stderr, "%lld", (long long)_glua_toint64(L,i) );
		      #elif defined(COMPILER_MSC)
		        fprintf( stderr, "%lld", (__int64)_glua_toint64(L,i) );
		      #endif
				break;
**/
			case GLUA_TYPE_STRING:
				fprintf( stderr, "'%s'", _glua_tostring_raw(L,i) );
				break;

			case GLUA_TYPE_BOOLEAN:
				fprintf( stderr, _glua_toboolean(L,i) ? "TRUE":"FALSE" );
				break;

			default:
				// (don't print the other types' contents)
				break;
			}

		fprintf( stderr, "\n" );
		}

	fprintf( stderr, "\n" );
}


/*--- Local help functions ---------------------------------------------*/

static bool_int Loc_IsNumeric( const char* str )
{
    char* end;
    strtod( str, &end );
    return (*end == '\0');  // FALSE if there was a non-numeric char
}

static void Loc_PushMerged_ptr( lua_State* L, const char* str, const void* ptr )
{
	_glua_pushstring( L, str );     // e.g. "glua_attach_"
    _glua_pushunsigned( L, (ulong)ptr );
	_glua_concat( L, 2 );
}

#ifdef GLUA_STATIC
  static void Loc_PushMerged_int( lua_State* L, const char* str, int v )
  {
	_glua_pushstring( L, str );     // e.g. "glua_attach_"
    _glua_pushinteger( L, v );
	_glua_concat( L, 2 );           // merges them into one string
  }
  //
  static void Loc_PushMerged_str( lua_State* L, const char* a, const char* b )
  {
	_glua_pushstring( L, a );
    _glua_pushstring( L, b );
	_glua_concat( L, 2 );
  }
#endif


#ifdef GLUA_STATIC
  #ifdef LUA_V40	// 4.0 actually converts in-location
    #define Loc_ConvToNumber(L,argn)  lua_tonumber(L,argn)
  #else
	// 5.0 does normally no conversion so we need to help a bit...
    #define Loc_ConvToNumber(L,argn)  Loc50_ConvToNumber(L,argn)
  #endif
#endif


//----
// Event names differ by Lua engine version:
//
//		GluaX:		Lua 4.0:		Lua 5.0:
//
//		"gettable"	"gettable"		"__index"
//		"settable"	"settable"		"__newindex"
//		"function"	"function"		"__call"
//		"gc"		"gc"			"__gc"
//		"add"		"add"			"__add"
//		"sub"		"sub"			"__sub"
//		"mul"		"mul"			"__mul"
//		"div"		"div"			"__div"
//		"pow"		"pow"			"__pow"
//		"unm"		"unm"			"__unm"
//		"eq"		"eq"			"__eq"
//		"lt"		"lt"			"__lt"
//		"le"		(none)			"__le"		(*
//		"concat"	"concat"		"__concat"
//		"tostring"	(none?)		    "__tostring"
//
//		"index"		"gettable"		"__index"   (**
//		"newindex"	"settable"		"__newindex"
//		"call"		"function"		"__call"
//
// *)  'le' not available on Lua 4.0 engine.
//
// **) There is also a native Lua 4.0 'index' method but due to keeping
//     things simple (= easily documentable) that is _not_ used by gluax.
//     This way, we can treat 'gettable' and 'index' as totally the same.
//
#ifdef GLUA_STATIC
//
struct s_EventNameLookup { const char* gluax; 
                           const char* lua;
                           uint8 opt;       // 0=no upvalue
                                            // 1=metatable as upvalue (Lua 5)
                         };
//
#ifdef LUA_V40
  #define _LOOKUP( gluax, lua4, lua5, opt )    { gluax, lua4, opt }   // ignore lua5
#else
  #define _LOOKUP( gluax, lua4, lua5, opt )    { gluax, lua5, opt }   // ignore lua4
#endif
//
static const struct s_EventNameLookup lookup[]= {
    
    _LOOKUP( "gettable", "gettable", "index", 1 ),
    _LOOKUP( "settable", "settable", "newindex", 0 ),
    _LOOKUP( "function", "function", "call", 1 ),
    _LOOKUP( "gc",       "gc",       "gc", 2 ),
    _LOOKUP( "add",      "add",      "add", 1 ),
    _LOOKUP( "sub",	     "sub",      "sub", 1 ),
    _LOOKUP( "mul",	     "mul",      "mul", 1 ),
    _LOOKUP( "div",	     "div",      "div", 1 ),
    _LOOKUP( "pow",      "pow",      "pow", 1 ),
    _LOOKUP( "unm",      "unm",      "unm", 1 ),
    _LOOKUP( "eq",       "eq",       "eq", 0 ),    //AK(12-Dec-2004): must have forgotten this?
    _LOOKUP( "lt",       "lt",       "lt", 0 ),
    _LOOKUP( "le",       NULL,       "le", 0 ),    // does not exist for 4.0!
    _LOOKUP( "concat",   "concat",   "concat", 1 ),
    _LOOKUP( "tostring", NULL,       "tostring", 0 ),
    //
    _LOOKUP( "index",    "gettable", "index", 1 ),     // same as "gettable"
    _LOOKUP( "newindex", "settable", "newindex", 0 ),  // same as "settable"
    _LOOKUP( "call",     "function", "call", 1 ),      // same as "function"
    //
    { NULL }  // terminating entry
};
//
static const char* EVENT_NAMECHANGE( lua_State* L, const char* event, uint8* opt_ref )
{
uint i;

    // Skip underscores if the application uses them:
    //
    while(event[0]=='_') event++;

    for( i=0; lookup[i].gluax != NULL; i++ )
        {
        if (strcmp( lookup[i].gluax, event ) == 0)
            {
            if (opt_ref)
                *opt_ref= lookup[i].opt;
            return lookup[i].lua;     // Native, version dependent name.
            }
        }

    _glua_errorN( L, "Unknown event '%s'!", event );
    return NULL;    // never gets here
}
#endif	// static


#ifdef LUA_V40
  static void Loc40_SetTagMethod( lua_State* L, int tag, const char* op )
    {
        if (strcmp(op,"gc")==0)
            {
            // "gc" method may need to be set as upvalue for 'GcFree'
            //
            STACK_START(L)
                {
                lua_CFunction old_gc= lua_gettagmethod( L, tag, "gc" );
    
                if (old_gc == fn_GcFree )
                    {
                    // [-1]: new (custom) gc method
                    //
                    lua_pushcclosure( L, fn_GcFree, 1 );  // GcFree & custom method = closure
                    }
                }
            STACK_END(0)
            }

        lua_settagmethod( L, tag, op );     // normal
    }
#endif


/*--- Lua interfacing stubs ----------------------------------------------*/

void _glua_error( lua_State* L, const char* msg )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _glua_error, L, msg );
	//
#elif (defined LUA_V40)   // v4.0 static
	//
	lua_error( L, msg );
    //
#else	// v5.0 static
	//
    lua_pushstring( L, msg );
	lua_error( L );
    //
#endif
}

//-----
// Note: We cannot use Lua 5.0 'lua_pushvfstring()' since we might be 
//       dynamically linked. Passing 'args' further to the host...
//       perhaps different compilers... Seems too risky!
//
//       const char* tmp= lua_pushvfstring(L, msg, args);
//
static
const char* _glua_vsprintf( lua_State* L, const char* msg, va_list args )
{
char* buf;
uint bufsize= 9999;  // !! Should make an estimate based on 'msg' & 'args' !!

    ASSUME_L( msg );
    
    // By pushing a userdata, we allocate memory that will later be
    // freed by Lua automatically (since there's no ref to it).
    //
    STACK_CHECK(L)
        {
        buf= (char*)_glua_pushuserdata_raw( L, 0 /*no tag*/, NULL, bufsize, 0 /*no mt*/ );
        
        _glua_settop( L, -2 );  // pops the reference (but pointer will be 
                                // valid until we return from the C side)
        }
    STACK_END(0)

    ASSUME_L( buf );
        
    vsprintf( buf, msg, args );
    
    return buf;
}

void _glua_errorL( lua_State* L, const char* msg, va_list args )
{
const char* buf= _glua_vsprintf( L, msg, args );

    _glua_error( L, buf );   // never returns
}

void _glua_errorN( lua_State* L, const char* msg, ... )
{
const char* buf;
va_list args;

    va_start(args,msg);
        {
        buf= _glua_vsprintf( L, msg, args );
        }
    va_end(args);
    
    _glua_error( L, buf );  // never returns
}

#ifndef _HAS_VA_ARGS
  //
  void glua_errorN( const char* msg, ... )
    {
    lua_State* L= _getL();
    const char* buf;
    va_list args;

      va_start(args,msg);
        {
        buf= _glua_vsprintf( L, msg, args );
        }
      va_end(args);
    
      _glua_error( L, buf );  // never returns
    }
#endif

int _glua_ver( int module_ver )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_1( _glua_ver, module_ver );
	//
#else	// static (host side)
	{
	int ver= module_ver & GLUA_VERSION_YYMMDD_MASK;    // i.e. 41221
    int ret= 0;
    
	if (ver < MODULE_VERSION_REQ)
		return GLUA_VERSION_ERR_OLDMODULE;	// too old module!
    
  #ifdef USE_FLOAT
    if ((module_ver & GLUA_VERSION_BIT_FLOAT) ==0)
  #else
    if ((module_ver & GLUA_VERSION_BIT_FLOAT) !=0)
  #endif
        {
        // The module uses different float/double mode than we do. So what. :)
        }

  #ifdef USE_FLOAT
    ret |= GLUA_VERSION_BIT_FLOAT;
  #endif
  #ifdef INT32_PATCH
    ret |= GLUA_VERSION_BIT_INT32;
  #endif
  #ifdef ENUM_PATCH
    ret |= GLUA_VERSION_BIT_ENUM;
  #endif
  
  // For Win32, PocketPC and OS X there are two gluahosts, with or without SDL graphics support.
  //
  #if (defined PLATFORM_WIN32) || (defined PLATFORM_WINCE) || (defined PLATFORM_DARWIN)
    #ifndef GLUAHOST_SDLMAIN
      ret |= GLUA_VERSION_BIT_NOTSDL;   // this host is without SDL support
    #endif
  #endif

	return ret | GLUA_VERSION;     // host version & identity bits (also module may disapprove us)
    }
#endif
}

//---
// Note: Do NOT return 'enum' directly since its size may be compiler specific.
//
uint /*e_Glua_Type*/ _glua_type2( lua_State *L, int argn, tag_int ud_tag )
{
#ifdef GLUA_DYNAMIC
	return HOST_CALL_L2( _glua_type2, L, argn, ud_tag );
	//
#else
	int items, tmp;
	enum e_Glua_Type ret;

	// Native Lua type enumerations are different for Lua 4.0 and 5.0.
	//
	enum e_Glua_Type lookup[]=
		//
		#ifdef LUA_V40
		{ GLUA_TYPE_USERDATA,	//#define LUA_TUSERDATA	0
		  GLUA_TYPE_NIL,		//#define LUA_TNIL	    1
		  GLUA_TYPE_NUMBER,		//#define LUA_TNUMBER	2
		  GLUA_TYPE_STRING,		//#define LUA_TSTRING	3
		  GLUA_TYPE_TABLE,		//#define LUA_TTABLE	4
		  GLUA_TYPE_FUNCTION	//#define LUA_TFUNCTION	5
		  // no GLUA_TYPE_BOOLEAN for 4.0
		};	//#define LUA_TNONE (-1)

          #if (! ((LUA_TUSERDATA==0) && (LUA_TNIL==1) && (LUA_TNUMBER==2) && \
                  (LUA_TSTRING==3) && (LUA_TTABLE==4) && (LUA_TFUNCTION==5)))
            #error "Wrong assumption on Lua types!"
          #endif
        //
		#else	// 5.0 beta:
		{ GLUA_TYPE_NIL,		//#define LUA_TNIL	    0
		  GLUA_TYPE_BOOLEAN,	//#define LUA_TBOOLEAN	1
		  GLUA_TYPE_USERDATA,	//#define LUA_TLIGHTUSERDATA 2
		  GLUA_TYPE_NUMBER,		//#define LUA_TNUMBER	3
		  GLUA_TYPE_STRING,		//#define LUA_TSTRING	4

		  GLUA_TYPE_TABLE,		//#define LUA_TTABLE	5
		  GLUA_TYPE_FUNCTION,	//#define LUA_TFUNCTION	6
		  GLUA_TYPE_USERDATA,	//#define LUA_TUSERDATA	7
		  //GLUA_TYPE_THREAD,	//#define LUA_TTHREAD	8
		  
		  // NOTE: Both normal 'userdata' and 'light userdata' map into
		  //       same GLUA type. (could be separate type, too, if needed)
		  //
		  #ifdef LUA_TNUM     // TEMPORARY
		    #define LUA_TNUMBER LUA_TNUM
		  #endif
		  
          #if (! ((LUA_TNIL==0) && (LUA_TBOOLEAN==1) && (LUA_TLIGHTUSERDATA==2) &&\
                  (LUA_TNUMBER==3) && (LUA_TSTRING==4) && (LUA_TTABLE==5) && \
                  (LUA_TFUNCTION==6) && (LUA_TUSERDATA==7) && (LUA_TTHREAD==8)))
            #error "Wrong assumption on Lua types!"
          #endif

		};	//#define LUA_TNONE (-1)
		#endif

	items= (sizeof(lookup) / sizeof(lookup[0]));

	tmp= lua_type( L, argn );

  #ifdef ENUM_PATCH
    if (tmp<=-3)  // LUA_TENUM (-3..-N)
        return GLUA_TYPE_ENUM;
  #endif
  
	// Make 'none' type seem like 'nil' to the caller. This helps us handle
	// variable parameter lists and default parameters:
	//
	//		func( a, nil )	--> 2nd param is 'LUA_TNIL'
	//		func( a )		--> now it is 'LUA_TNONE'
	//
	// However, we do not need to differ between these two, and can return
	// 'GLUA_TYPE_NIL' for both of them. So this function never returns 'none'.
	//
	if (tmp<0)	// LUA_TNONE
		return GLUA_TYPE_NIL;

	ASSUME_L( (tmp>=0) && (tmp<items) );

	ret= lookup[tmp];

	// Add possible extra flags or do some tests.
	//
	switch( ret )
		{
		case GLUA_TYPE_NUMBER:
			{
      #ifdef INT32_PATCH   // Lua 5.1w4 with 32-bit integer patch
            if (lua_isinteger(L,argn))
                ret= GLUA_TYPE_INT;
      #else
			glua_num_t n= lua_tonumber(L,argn);	 // may lose accuracy (if int > 23 bits, and using float)

          #if (defined USE_FLOAT) && (!defined _MSC_VER)
            if (n == floorf(n))
          #else
            if (n == floor(n))
          #endif
				ret= GLUA_TYPE_INT;		// no fractional part
      #endif
			}
			break;

		case GLUA_TYPE_USERDATA:
			{
            tag_int tag2;   // tag of our userdata
            #ifdef LUA_V40
                tag2= (tag_int)lua_tag(L,argn);
            #else
                Loc50_ToUserdata(L,argn,&tag2);
            #endif

			if (ud_tag && (ud_tag != tag2))
    			ret = GLUA_TYPE_USERDATA_UNK;	// tags don't match!
			}
			break;

		default:
			break;	// 'ret' good as it is
		}

	return (uint)ret;
	//
#endif	// GLUA_DYNAMIC
}

//---
bool_int _glua_toboolean( lua_State* L, int argn )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L1( _glua_toboolean, L,argn );
    //
#elif (defined LUA_V40)
    {
    // Lua 4.0: anything non-nil goes as 'true':
	return (_glua_type( L, argn ) != GLUA_TYPE_NIL);
	}
#else   // 5.0 statically linked:
	return lua_toboolean( L, argn );
#endif
}

//---
// Note: May convert numeric strings to number type! (on Lua 4.0)
//       Use 'glua_tonumber_safe()' to avoid conversion (but slower).
//
//		 Never rely on the conversion - 5.0 does not do it!
//
double _glua_tonumber_raw_d( lua_State* L, int argn )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L1( _glua_tonumber_raw_d, L,argn );
#else
	{
    enum e_Glua_Type t= _glua_type(L,argn);
    
    if (t <= GLUA_TYPE_NIL)
        return 0.0;

  #if (defined USE_FLOAT) && (defined INT32_PATCH)
    // Pass int32's unaffected (double can carry them) 
    if (lua_isinteger( L, argn ))
        return (double)lua_tointeger( L, argn );
  #endif

	return (double)lua_tonumber( L, argn );
	}
#endif
}

float _glua_tonumber_raw_f( lua_State* L, int argn )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L1( _glua_tonumber_raw_f, L,argn );
#else
	{
    enum e_Glua_Type t= _glua_type(L,argn);
    
    if (t <= GLUA_TYPE_NIL)
        return 0.0f;

    // No special treatment for int32's - caller wants 'float'
    // so accuracy would be lost anyhow.
    //
	return (float)lua_tonumber( L, argn );
	}
#endif
}

double _glua_tonumber_safe_d( lua_State* L, int argn )
{
#ifdef LUA_V40	// Lua 4.0 converts numeric strings to actually
	{			// become numbers, if 'lua_tonumber()' is used.
	double n;

	_glua_pushvalue( L, argn );		// Makes [-1] a local copy
		{
		n= _glua_tonumber_raw_d(L,-1);	// may convert str->num.
		}
	_glua_remove( L, -1 );			// Removes the copy from stack

	return n;
	}
#else	// 5.0 static
	{
	// Lua 5.0 'lua_tonumber()' no longer modifies the entries:
	// 'raw' conversion can be used always!
	//
	return _glua_tonumber_raw_d(L,argn);
	}
#endif
}

// Similar to above, but with floats
//
float _glua_tonumber_safe_f( lua_State* L, int argn )
{
#ifdef LUA_V40
	float n;
	_glua_pushvalue( L, argn );		// Makes [-1] a local copy
		n= _glua_tonumber_raw_f(L,-1);	// may convert str->num.
	_glua_remove( L, -1 );			// Removes the copy from stack
    return n;
#else	// 5.0 static
	return _glua_tonumber_raw_f(L,argn);
#endif
}

//---
// Integer access added in Lua 5.1-work0
//
long _glua_tointeger_raw( lua_State* L, int argn )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L1( _glua_tointeger_raw, L,argn );
#else
    {
    long ret;
    enum e_Glua_Type t= _glua_type(L,argn);
    
    if (t <= GLUA_TYPE_NIL)
        return 0;   // 'nil' is okay - 0 is default

  #ifdef INT32_PATCH
    (void)ret;
    return lua_tointeger( L, argn );    // always safe
  #else
    #ifdef LUA51_WORK0
      ret= lua_tointeger( L, argn );    // >= Lua 5.1-work0
    #else
      ret= (long)lua_tonumber( L, argn );   // 5.0.1 and before
    #endif
    #ifdef MANTISSA_23BITS
      MANTISSA_23BITS(ret);
    #endif
    return ret;
  #endif
    }
#endif
}

// Unsigned access so we can check accuracy if host has 'float'
// (otherwise, a mere 'tonumber' could have been used).
//
ulong _glua_tounsigned_raw( lua_State* L, int argn )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L1( _glua_tounsigned_raw, L,argn );
#else
    {
    ulong ret;
    enum e_Glua_Type t= _glua_type(L,argn);
    
    if (t <= GLUA_TYPE_NIL)
        return 0;   // 'nil' is okay - 0 is default

  #ifdef INT32_PATCH
    (void)ret;
    return (ulong)lua_tointeger( L, argn );    // always safe
  #else
    #ifdef LUA51_WORK0
      ret= (ulong)lua_tointeger( L, argn );    // >= Lua 5.1-work0
    #else
      ret= (long)lua_tonumber( L, argn );   // 5.0.1 and before
    #endif
    #ifdef MANTISSA_23BITS
      MANTISSA_23BITS(ret);
    #endif
    return ret;
  #endif
    }
#endif
}

//---
// Note: Converts numbers to strings!
//       Use 'glua_tostring_safe()' to avoid conversion (but slower).
//
const char* _glua_tolstring_raw( lua_State* L, int argn, uint* /* size_t* */ len_ref )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L2( _glua_tolstring_raw, L,argn, len_ref );
#else
	const char* p= lua_tostring( L, argn ); // may convert stack[argn]

    if (len_ref) *len_ref= lua_strlen( L, argn );

    return p;
#endif
}

void* _glua_touserdata( lua_State* L, int argn, tag_int* tagref )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L2( _glua_touserdata, L,argn, tagref );
	//
#elif (defined LUA_V40)
	{
	void* ret= lua_touserdata(L,argn);
	if (ret && tagref) *tagref= (tag_int)lua_tag(L,argn);
	return ret;
	}
#else   // v.5.0 static
    return Loc50_ToUserdata(L,argn,tagref);
	//
#endif
}

enum_int _glua_toenum( lua_State* L, int argn, tag_int* tagref )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L2( _glua_toenum, L,argn, tagref );
	//
#elif (defined LUA_V40)
    if (tagref) *tagref= (tag_int)(-1);
    return (enum_int)_glua_touserdata(L,argn,tagref);  // the same thing
	//
#elif (defined ENUM_PATCH)  // Lua core enum patch
    {
    lua_EnumTag tag= lua_enumtag( L, argn );   // 0 if not an enum

    if (tagref) *tagref= (tag_int)tag;   // 0 / -3..INT_MIN
    return (tag!=0) ? (enum_int)lua_toenum( L, argn, tag ) : 0;
    }
#else   // v.5.0 static
    if (tagref) *tagref= (tag_int)(0);  // was: -1
    return Loc50_ToEnum(L,argn,tagref);     // slightly different / optimized
#endif
}

void _glua_pushboolean( lua_State* L, bool_int v )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _glua_pushboolean, L,v );
	//
#elif (defined LUA_V40)
	//
    if (v) lua_pushnumber( L, 1.0 /*was 'v'*/ );
    else   lua_pushnil( L );
	//
#else	// 5.0 static
	//
	lua_pushboolean( L, v );
	//
#endif
}

void _glua_pushnumber_d( lua_State *L, double v )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _glua_pushnumber_d, L,v );
#else
	lua_pushnumber( L, (glua_num_t)v );
#endif
}

void _glua_pushnumber_f( lua_State *L, float v )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _glua_pushnumber_f, L,v );
#else
	lua_pushnumber( L, (glua_num_t)v );
#endif
}

/**
void _glua_pushnumber_int( lua_State *L, int v )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _glua_pushnumber_int, L,v );
#else
    #ifdef MANTISSA_23BITS
      MANTISSA_23BITS(v);    // creates an error if mantissa > 23 bits
    #endif

    // Don't use 'lua_pushinteger()' since it might push 'int32' enum
    //
	lua_pushnumber( L, (glua_num_t)v );
#endif
}
**/

void _glua_pushinteger( lua_State *L, long v )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _glua_pushinteger, L,v );
    //
#elif (defined INT32_PATCH)
    lua_pushinteger( L, v );    // pushes a real 32-bit integer (even on 'float')
#else
    #ifdef MANTISSA_23BITS
      MANTISSA_23BITS(v);    // creates an error if mantissa > 23 bits
    #endif
    #ifdef LUA51_WORK0
      lua_pushinteger( L, v );
    #else
      lua_pushnumber( L, (glua_num_t)v );   // 4.0, 5.0
    #endif
#endif
}

void _glua_pushunsigned( lua_State *L, ulong v )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _glua_pushunsigned, L,v );
    //
#elif (defined INT32_PATCH)
    lua_pushinteger( L, (long)v );
#else
    #ifdef MANTISSA_23BITS
      MANTISSA_23BITS(v);    // creates an error if mantissa > 23 bits
    #endif
    #ifdef LUA51_WORK0
      lua_pushinteger( L, (long)v );
    #else
      lua_pushnumber( L, (glua_num_t)((long)v) );
    #endif
#endif
}

void _glua_pushclstring( lua_State *L, const char *s, int len )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L2( _glua_pushclstring, L,s,len );
#else
    if (!s) lua_pushnil(L);     // 'glua_pushstring(NULL)' = push 'nil'
    else    lua_pushlstring( L, s, (len>=0) ? len : strlen(s) );
#endif
}

#ifdef UNICODE
  void _glua_pushwlstring( lua_State *L, const TCHAR *s, int len )
  {
  char* buf;
  
    if (!s) _glua_pushstring(L,NULL);

    if (len<0) len= wcslen(s);
    buf= malloc( len+1 );
    
#if 1   // does this work?
    sprintf( buf, "%S", s );    // Capital 'S' should mean wide string (MSDN).
#else
    { int i;
    for( i=0; i<len; i++ )  
        {
        TCHAR w= s[i];
        buf[i]= (w<256) ? (char)w : '.';
        }
    }
#endif
        
    _glua_pushclstring( L, buf, len );
    
    free(buf);
  }
#endif

//---
// '_glua_pushuserdata_raw' pushes also 'NULL' as userdata (important for enums).
//
void* _glua_pushuserdata_raw( lua_State *L, tag_int tag, 
						      const void *vp, uint /*size_t*/ size, uint8 mode )
{
#ifdef GLUA_DYNAMIC
	//
    return HOST_CALL_L4( _glua_pushuserdata_raw, L, tag, vp, size, mode );
	//
#else
    void* ret;

    STACK_CHECK(L)
        {
      #ifdef LUA_V40
        void* ptr= (void*)vp;
    
        (void)mode;   // not used

    	if (size>0)
    		{
    		ptr= malloc( size );
    		ASSUME_L( ptr );
    		}
    
        lua_pushusertag( L, ptr, (int)tag );

        if (size>0)  // associate 'GcFree' with it (no upvalue here, yet)
            {
            lua_pushcclosure( L, fn_GcFree, 0 );
		    lua_settagmethod( L, (int)tag, "gc" );
            }

        ret= ptr;
        //
      #else
    	// Lua 5.0 userdata model is totally different from 4.0. We try to 
    	// 'emulate' some of the earlier 4.0 model to allow transparency of
    	// the underlying engine.
        //
        ret= Loc50_PushUserdataExt( L, vp, size, tag, mode );  // mode: metatable as upvalue?
        //
      #endif
        }
    STACK_END(+1)   // should have pushed one
    
    if (size>0)   // Using Lua-allocated memory block?
        {
        if (vp) memcpy( ret, vp, size );
        else    memset( ret, 0, size );
        }

    return ret;
#endif
}

void* _glua_pushuserdata( lua_State *L, tag_int tag, 
						  const void *vp, size_t size,
						  lua_CFunction gc_func )
{
void* ret;

	if ((!vp) && (!size))	// NULL userdata
		{
		_glua_pushnil( L );
		return NULL;
		}
    
    ret= _glua_pushuserdata_raw( L, tag, vp, size, 0 /*no mt*/ );

    if (gc_func)
        _glua_settagmethod( L, tag, "gc", gc_func );

    return ret;
}

//---
// Optimized for enum's (no need for gc method etc.)
//
void _glua_pushenum( lua_State *L, tag_int tag, enum_int val, uint8 mode )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L3( _glua_pushenum, L, tag, val, mode );
    //
#elif (defined LUA_V40)
    if (mode==_PUSHENUM_MODE_PUSHED)
        lua_remove(L,-1);    // don't need this, but must eat.

    lua_pushusertag( L, (void*)val, (int)tag );
    //
#elif (defined ENUM_PATCH)
    lua_pushenum( L, (lua_Enum)val, (lua_EnumTag)tag );
#else
    Loc50_PushUserdataExt( L, (void*)val, 0 /*size*/, tag, mode );
#endif
}

//---
// Modifying an enum 'in location'.  EXPERIMENTAL.
//
// Note: This cannot be implemented for Lua 4.0 (since userdata pointers are read-only)
//
void _glua_modenum( lua_State *L, int argn, enum_int val, uint mask )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L3( _glua_modenum, L, argn, val, mask );
    //
#elif (defined LUA_V40)
    _glua_error( L, "Changing enum not possible on Lua 4.0" );
    //
#elif (defined ENUM_PATCH)
    _glua_error( L, "Not needed if ENUM_PATCH is there (could be removed)" );
#else
    Loc50_ModEnum( L, argn, val, mask );
#endif
}

/*
//---
// Pushes upvalue (1..N), 'nil' if empty.
//
void _glua_pushupvalue( lua_State *L, int n )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L1( _glua_pushupvalue, L, n );
#else
	lua_pushvalue( L, lua_upvalueindex(n) );
#endif
}
*/

void _glua_newtable( lua_State *L )   // was: pushtable
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L( _glua_newtable, L );
#else
	lua_newtable( L );	// Creates an empty table at top of stack
#endif
}

#ifdef GLUA_STATIC
//---
// Modify '[tbl_i]' so that it contains its own subtable, name or
// number of which is in '[key_i]'. Leave stack as it was.
//
// N.B. 'tbl_i' is the stack index, where the table in/out is.
//       So "leaving stack as it was" (above) means nothing is
//       pushed, or popped, but that particular index is modified.
//
static void
Loc_TableDive( lua_State* L, int tbl_i, 
			   const char* key, uint key_len )
{
	// Make our table index absolute so that it is tolerant of 
	// pushing/popping stuff on the stack.
	//
	if (tbl_i<0) tbl_i= lua_gettop(L) +1 +tbl_i;  // -1 => top

    STACK_CHECK(L)		// Check that stack remains balanced
		{	
		// Make a temporary string that's the starting part of key.
		//
		lua_pushlstring( L, key, key_len );  // temporary str at [-1]
		
		Loc_ConvToNumber(L,-1);	// in place type conversion

		// Now, is there such a subtable already?
		//
		lua_pushvalue(L,-1);	// make a duplicate of our index
								// for 'gettable()' to eat (*yum-yum*)
		lua_rawget(L,tbl_i);

		// Stack now:
		//    [tbl_i] is still the table we're diving to.
		//	  [-2] is the index we're trying to find
		//	  [-1] is 'nil' or a valid table entry
		//
		if (!lua_istable(L,-1))	
			{
			// Subtable wasn't there (or it was some basic type?!?)
			// Create an empty subtable & attach it to the mother.
			//
			lua_remove(L,-1);	// remove 'nil' or whatever it was

			lua_newtable(L);	// two copies of the same empty table
			lua_pushvalue(L,-1);
			lua_insert(L,-3);

			// Stack now:
			//	[tbl_i] still the table we're diving to.
			//	[-3] is a 2nd (backup) reference to our created table
			//  [-2] is the index name / number
			//	[-1] is the empty table we created

			lua_rawset(L,tbl_i);	// eats up [-1] and [-2] implicitly,
									// sets them as a new subtable.
			// Stack now:
			//  [tbl_i] still there...
			//  [-1] is the backup ref of the created (empty) table
			}
		else
			{
			// We got the subtable already, can remove the index entry.
			//
			lua_remove( L, -2 );

			// Stack now:
			//  [tbl_i] still there...
			//  [-1] is the subtable item we fetched
			}

		// Replace caller's table entry with the subtable we found
		// or created.
		//
		lua_insert( L, tbl_i );	   // Moves [-1] into the place of ['tbl_i']
		lua_remove( L, tbl_i+1 );  // Remove the shifted old table	
		}
	STACK_END(0)	  // Gives runtime error if stack out of balance
}
#endif  // GLUA_STATIC

void _glua_totable( lua_State *L )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L( _glua_totable, L );
#else

    STACK_CHECK(L)
    {
    // [-3] is the table to add to.
    // [-2] is the key name (string or integer)
    // [-1] is the value to add (may be string, numeric, whatever...)
    //
    ASSUME_L( lua_istable( L, -3 ) );     // table to add to

	// Check if we have a "subkey1.subkey2.key3" dot-notation for
	// subtable access (that needs to be handled separately).
	//
	// Note: 'lua_isstring()' can NOT be used for the type checking
	//       since it reports numbers (which are always convertible 
	//		 into strings) as strings.
	//
    if ( (lua_type(L,-2)!=LUA_TSTRING) || !strchr(lua_tostring(L,-2),'.') )
        {
		// Just the simple case: implicitly uses [-1] and [-2] and
		// removes them from the stack.
		//
	    lua_rawset( L, -3 );
        }
	else
		{
		//-----
		// Dot notation.
		//
		// - Make a safety copy of [-3] (the main table) as [-4].
		// - Recursively loop, keeping [-3] as the subtable and [-2] as the
		//	 remainder of the key path, until no dots exist in it.
		// - Finally, set the value & remove [-3] subtable entry.
		//
		// Note: Any only-numeric parts of the key string must be handled as
		//		 integers, not as strings.
		//
		lua_pushvalue( L, -3 );		// changes others temporarily to [-2]..[-4]
		lua_insert( L, -4 );		// now orig table is copied into [-4]

		do {
			const char* key;
			const char* dot;

			key= lua_tostring(L,-2);
			dot= strchr(key,'.');

			if (!dot) break;	// wow, we got there...

			// Go one level down (modifies -3 to be the subtable)
			//
			Loc_TableDive( L, -3, key, dot-key );

			// Modify key name to be one level deeper.
			//
			lua_remove( L, -2 );	// old key string away
			lua_pushstring( L, dot+1 );
			lua_insert( L, -2 );	// new one in place
			}
		while(1);

		Loc_ConvToNumber(L,-2);  // make numerical string actually a number

		lua_rawset( L, -3 );	// set at subtable, eats away [-1] and [-2]

		lua_remove( L,-1 );		// remove the subtable item we used
								// (leaves original main table at [-1])
		}
	}
	STACK_END(-2)	// should have consumed [-1] and [-2]
#endif
}

int _glua_gettop( lua_State *L )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L( _glua_gettop, L );
#else
	return lua_gettop(L);   // Gets the current stack size
	                        // (= absolute index of [-1] top-of-stack entry)
#endif
}

void _glua_settop( lua_State *L, int n )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _glua_settop, L, n );
#else
	lua_settop(L,n);    
#endif
}

void _glua_setglobal( lua_State *L, const char* s )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _glua_setglobal, L, s );
#else
	lua_setglobal(L,s);     // Pops [-1] and stores it to variable named 's'.
#endif
}

//---
// Checks that there is space for 'extra' more items on the stack.
//
bool_int _glua_checkstack( lua_State* L, int extra )
{
#ifdef GLUA_DYNAMIC
	//
    return HOST_CALL_L1( _glua_checkstack, L, extra );
    //
#elif (defined LUA_V40)
	//
	return (lua_stackspace(L) >= extra);	  
	//
#else	// 5.0 static
	//
	return lua_checkstack( L, extra );
	//
#endif
}

//---
// Get something from Lua registry table:
//		[-1]= key
// Pushes registry value associated with key (if any) to the stack
//
void _glua_getreg( lua_State* L )
{
#ifdef GLUA_DYNAMIC
	//
    HOST_CALL_L( _glua_getreg, L );
    //
#else
	{
	STACK_CHECK(L)
		{
		#ifdef LUA_V40
			{
			// Lua 4.0 doesn't have the 'LUA_REGISTRYINDEX' pseudo-index.

			lua_getregistry(L);	  // pushes ref to registry table to [-1]
			lua_insert(L,-2);     // move it to [-2]

			lua_rawget(L,-2);	  // eats key, pushes value

			lua_remove(L,-2);	  // remove the registry ref
			}
		#else	// Lua 5.0 static
			{
			lua_rawget( L, LUA_REGISTRYINDEX );	  // eats key, pushes value
			}
		#endif
		}
	STACK_END(0)
	}
#endif
}

//---
// Store something in Lua registry table:
//	    [-2]= key
//		[-1]= value
//
void _glua_setreg( lua_State* L )
{
#ifdef GLUA_DYNAMIC
	//
    HOST_CALL_L( _glua_setreg, L );
    //
#else
	{
	STACK_CHECK(L)
		{
		#ifdef LUA_V40
			{
			// Lua 4.0 doesn't have the 'LUA_REGISTRYINDEX' pseudo-index.

			lua_getregistry(L);	  // pushes ref to registry table to [-1]
			lua_insert(L,-3);     // move it to [-3]

			lua_rawset(L,-3);	  // eats key + value

			lua_remove(L,-1);	  // remove the registry ref
			}
		#else	// Lua 5.0 static
			{
			lua_rawset( L, LUA_REGISTRYINDEX );	  // eats key + value
			}
		#endif
		}
	STACK_END(-2)
	}
#endif
}

static void initCriticalSections()
{
    static bool_int beenhere= FALSE;
    if (!beenhere)
        {
#ifdef _TLS_UNINITIALIZED   // Win32 MSC (not WinCE)
		if (tls_L == _TLS_UNINITIALIZED) tls_L= TlsAlloc();
		if (tls_T == _TLS_UNINITIALIZED) tls_T= TlsAlloc();
#endif
        // Probably initializing these twice wouldn't hurt, but why take
        // risks..
      #ifndef LUA_V40
        CRITICAL_SECTION_INIT( &_glua_newtag_cs );
        #ifndef ENUM_PATCH
          Loc50_InitCs();    //CRITICAL_SECTION_INIT( &_glua_modenum_cs );
        #endif
      #endif
        CRITICAL_SECTION_INIT( &_glua_regtag_cs );
        CRITICAL_SECTION_INIT( &_glua_queue_add_cs );
        beenhere= TRUE;
        }
}



//---
tag_int _glua_newtag( lua_State* L )
{
#ifdef GLUA_DYNAMIC
	//
    return HOST_CALL_L( _glua_newtag, L );
	//
#elif (defined LUA_V40)
    {
	// In Lua 4.0, the engine itself provides unique tag numbers:
	//
	int tag= lua_newtag(L);

	ASSUME_L( tag != 0 );		// We expect zero never to be a valid tag.

	// All tags should have 'GcHub' as their garbage collector.
	//
	STACK_CHECK(L)
		{
		lua_pushcfunction( L, fn_GcHub );
		lua_settagmethod( L, tag, "gc" );  // pops new method, pushes old

		//lua_remove( L, -1 );
		}
	STACK_END(0)

	return (tag_int)tag;
	}
#else	// 5.0 static
	{
	// In Lua 5.0 there are no more 'tags', so we emulate some.
	//
	static int next_tag= 1;	// 0 = not a valid tag
	int ret;
	
	initCriticalSections();
	CRITICAL_SECTION_START(&_glua_newtag_cs)
        {
        if (next_tag==1)	// first time call
            {
            // Make sure the '0' metatable contains 'GcHub' (but nothing more).
            // This is used for tags that don't have a dedicated methods table.
            //
            lua_pushcfunction( L, fn_GcHub );
            Loc50_SetTagMethod( L, 0 /*tag*/, "gc", FALSE /*don't push old*/ );
            }
    
        ret= next_tag++;
        }
    CRITICAL_SECTION_END

	return (tag_int)ret;
	}
#endif
}

//---
void _glua_insert( lua_State *L, int argn )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _glua_insert, L, argn );
#else
	lua_insert(L,argn);
#endif
}

//---
void _glua_pushcclosure( lua_State *L, lua_CFunction fn, uint n )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L2( _glua_pushcclosure, L, fn, n );
#else
	lua_pushcclosure(L,fn,n);
#endif
}

//---
// Skip these functions unless compiling a host (static linkage) or
// GLUAPORT is explicitly #defined for the application project.
//
#if ((defined GLUA_STATIC) || (defined GLUAPORT))
//
// This is used by non-gluax code so it returns 'int', not 'tag_int':
//
int _gluaport_tagofuserdata( lua_State* L, int argn )
{
tag_int tag;

    return _glua_touserdata(L,argn,&tag) ? (int)tag : 0;
}
//
//---
int _gluaport_strlen( lua_State *L, int argn )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L1( _gluaport_strlen, L, argn );
#else
	return lua_strlen(L,argn);
#endif
}
//
//---
void _gluaport_rawset( lua_State *L, int argn )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _gluaport_rawset, L, argn );
#else
	lua_rawset(L,argn);
#endif
}
//
//---
void _gluaport_rawseti( lua_State *L, int argn, int n )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L2( _gluaport_rawseti, L, argn, n );
#else
	lua_rawseti(L,argn,n);
#endif
}
//
//---
void _gluaport_call( lua_State *L, int nargs, int nresults )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L2( _gluaport_call, L, nargs, nresults );
#else
	lua_call(L,nargs,nresults);
#endif
}
//
//---
void _gluaport_settable( lua_State *L, int argn )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _gluaport_settable, L, argn );
#else
	lua_settable(L,argn);
#endif
}
//
//---
void _gluaport_settag( lua_State *L, int argn, int tag )
{
#ifdef GLUA_DYNAMIC
    //
    HOST_CALL_L2( _gluaport_settag, L, argn, tag );
    //
#elif (defined LUA_V40)
    {
	lua_settag(L,tag);
	}
#else   // 5.0 static
    {
    Loc50_SetTag( L, argn, (tag_int)tag );
    }
#endif
}
//
//---
// Note: We need a different 'settagmethod' for gluaport support.
//       The Lua native behaviour is to pop new function, push the old one.
//
void _gluaport_settagmethod( lua_State *L, int tag, const char* event )
{
#ifdef GLUA_DYNAMIC
    //
    HOST_CALL_L2( _gluaport_settagmethod, L, tag, event );
    //
#else
	event= EVENT_NAMECHANGE( L, event, NULL );

    STACK_CHECK(L)
        {
        ASSUME_L( lua_isfunction(L,-1) || lua_isnil(L,-1) );

        #ifdef LUA_V40
        	Loc40_SetTagMethod(L,tag,event);
        #else   // 5.0 static
	        Loc50_SetTagMethod( L, (tag_int)tag, event, TRUE /*yes, push old*/ );
        #endif
        }
    STACK_END(0)    // popped one, pushed one
#endif
}
//
//---
int _gluaport_getstack( lua_State *L, int level, lua_Debug* ar )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L2( _gluaport_getstack, L, level, ar );
#elif defined(LUA_V40)
    return 0;   // NOT AVAILABLE ON 4.0!
#else   // 5.x
    return lua_getstack( L, level, ar );
#endif
}
//
//---
int _gluaport_getinfo( lua_State *L, const char* what, lua_Debug* ar )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L2( _gluaport_getinfo, L, what, ar );
#elif defined(LUA_V40)
    return 0;   // NOT AVAILABLE ON 4.0!
#else   // 5.x
    return lua_getinfo( L, what, ar );
#endif
}
//
//---
int _gluaport_type( lua_State *L, int argn )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L1( _gluaport_type, L, argn );
#elif defined(LUA_V40)
    return -1;   // LUA 4.0 WOULD HAVE WRONG VALUES!
#else   // 5.x
    return lua_type( L, argn );
#endif
}
//
//---
int _gluaport_pcall( lua_State *L, int nargs, int nres, int errfunc )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L3( _gluaport_pcall, L, nargs, nres, errfunc );
#elif defined(LUA_V40)
    (void)errfunc;
    return lua_call( L, nargs, nres );   // Lua 4.0 has no 'pcall()'
#else   // 5.x
    return lua_pcall( L, nargs, nres, errfunc );
#endif
}
//
//---
int _gluaport_load( lua_State *L, lua_Chunkreader reader, void *dt, const char *name )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L3( _gluaport_load, L, reader, dt, name );
#elif defined(LUA_V40)
    _glua_error( L, "'load()' not available on Lua 4.0!" );
#else   // 5.x
    return lua_load( L, reader, dt, name );
#endif
}
//
//---
const char* _gluaport_pushvfstring( lua_State *L, const char* fmt, va_list argp )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L2( _gluaport_pushvfstring, L, fmt, argp );
#elif defined(LUA_V40)
    {   // Try to emulate (note: 5.0 engine has no buffer overflow concern!)
    char buf[999];
    sprintfv( buf, fmt, argp );
    return lua_pushstring( L, buf );
    }
#else   // 5.x
    return lua_pushvfstring( L, fmt, argp );
#endif
}
//
//---
void _gluaport_rawget( lua_State *L, int idx )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _gluaport_rawget, L, idx );
#else
    lua_rawget( L, idx );
#endif
}
//
//---
void _gluaport_rawgeti( lua_State *L, int idx, int n )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L2( _gluaport_rawgeti, L, idx, n );
#else
    lua_rawgeti( L, idx, n );
#endif
}
//
//---
int _gluaport_setmetatable( lua_State *L, int idx )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L1( _gluaport_setmetatable, L, idx );
#else
    return lua_setmetatable( L, idx );
#endif
}
//
//---
int _gluaport_getmetatable( lua_State *L, int idx )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L1( _gluaport_getmetatable, L, idx );
#else
    return lua_getmetatable( L, idx );
#endif
}
//
//---
void _gluaport_replace( lua_State *L, int idx )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _gluaport_replace, L, idx );
#else
    lua_replace( L, idx );
#endif
}
#endif  // GLUAPORT
//
//---( end of GLUAPORT section )---


/***
lua_CFunction _gluaport_gettagmethod( lua_State* L, 
									  int tag, const char* event )
{
#ifdef GLUA_DYNAMIC
	//
    return HOST_CALL_L2( _gluaport_gettagmethod, L, tag, event );
	//
#elif (defined LUA_V40)
    {
	lua_CFunction func;

	event= EVENT_NAMECHANGE( event );

	// Lua 4.0 still has tag methods:
	//
    STACK_CHECK(L)
        {
		lua_gettagmethod( L, tag, event );	// pushes 'nil' or func
		
		func= lua_tocfunction( L, -1 );

		lua_pop( L, -1 );	// remove pushed entry 
		}
    STACK_END(0)

	return func;
	}
#else	// 5.0 static
	{
	event= EVENT_NAMECHANGE( event );

    return Loc50_GetTagMethod( L, tag, event );
	}
#endif
}
***/

//---
void _glua_settagmethod( lua_State* L, tag_int tag, const char* event,
								       lua_CFunction func )
{
#ifdef GLUA_DYNAMIC
	//
    HOST_CALL_L3( _glua_settagmethod, L, tag, event, func );
	//
#else  // GLUA_STATIC
    //
    uint8 opt;
	event= EVENT_NAMECHANGE( L, event, &opt );

    STACK_CHECK(L)
        {
        //AK(13-Dec-04): Added the tag value as an 'upvalue' to methods
        //               (useful for enum methods to get their actual userdata id)
        //
        //lua_pushnumber( L, tag );
        
      #ifdef LUA_V40
        lua_pushcclosure( L, func, 0 );
        (void)opt;  // not needed
      #else
        int upvalues= 0;

        //AK(19-Dec-04): Also push the metatable reference directly (this will speed up
        //               creation of new userdata of the same type, within the handler)
        //
        if (opt==1)   //AK(26-Dec-04): Only push for methods which might need it (optimization)
            {
            bool8 create= TRUE;

            if (Loc50_PushMetatableRef( L, tag, &create ))   // yes, create if not there
                upvalues++;
            ASSUME_L( upvalues==1 );
            }

        lua_pushcclosure( L, func, upvalues );
      #endif
                
      #ifdef LUA_V40
        {
    	// Lua 4.0 still has tag methods:
	    //
	    Loc40_SetTagMethod( L, (int)tag, event );
        }
      #else	// 5.0 static
        {
        if (opt == 2)   // custom "gc": use 'GcHub' with an upvalue :)
            {
            // [-1]: custom gc function (we pushed it there)
            //
	        lua_pushcclosure( L, fn_GcHub, 1 );
            Loc50_SetTagMethod( L, tag, "gc", FALSE );
            }
        else    // other than "gc"
            {
            if (Loc50_SetTagMethod( L, tag, event, FALSE /*don't push old func*/ ))
                {
                // First tag method: make sure also 'GcHub' is used as the garbage collector

    	        lua_pushcfunction( L, fn_GcHub );  // no upvalue
                Loc50_SetTagMethod( L, tag, "gc", FALSE );
                }
            }
        }
      #endif  // Lua 5
        }
    STACK_END(0)
#endif
}


//-----
void _glua_gettable( lua_State *L, int argn )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _glua_gettable, L, argn );
#else
    // 'argn': table to get from:
    //
    ASSUME_L( lua_istable( L, argn ) );     // table to get from

    // stack '-1' is the key name (string or numeric index):
    //
    ASSUME_L( lua_isstring(L,-1) || lua_isnumber(L,-1) );

    lua_gettable( L, argn );    // pushes tbl entry or 'nil'
#endif
}


bool_int _glua_next( lua_State *L, int argn )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L1( _glua_next, L, argn );
#else
    ASSUME_L( lua_istable( L, argn ) );     // table to get from

	// Key name, index or 'nil' (for first round):
	//
    ASSUME_L( lua_isstring(L,-1) || lua_isnumber(L,-1) || lua_isnil(L,-1) );

    // 'lua_next()' pops a key from stack and pushes the key,value pair
    // following it in the table. key='nil' starts the iteration.
    //    
    return lua_next( L, argn );   // pops one, pushes two
    //
    // FALSE: end of iteration (nothing pushed to stack)
    // TRUE:  valid entry (stack[-2]: next key, stack[-1]: next value)
#endif
}

void _glua_remove( lua_State *L, int argn )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _glua_remove, L, argn );
#else
    lua_remove( L, argn );      // removes '[argn]' from stack (& shifts others)
#endif
}

void _glua_pushvalue( lua_State *L, int argn )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _glua_pushvalue, L, argn );
#else
    lua_pushvalue( L, argn );   // pushes a copy of '[argn]' to stack
#endif
}

const char* _glua_tolstring_safe( lua_State* L, int argn, uint* /* size_t* */ len_ref )
{
const char* ptr;

	// To bypass the int->str conversion side-effect, we make a local
	// copy of the stack entry, get a pointer to it, and remove it.
	// Even though the item is removed from stack, our pointer remains
	// valid until Lua does Garbage Collection (which is only after
	// GLUA_FUNC_END so this is safe :).

	_glua_pushvalue( L, argn );		// Makes [-1] a local copy
		{
		ptr= _glua_tolstring_raw(L,-1,len_ref);	  // may change the local copy
		}
	_glua_remove( L, -1 );			// Removes the copy from stack

	return ptr;		// Should remain valid until GLUA_FUNC_END.
}


//---
// 'glua_exec()' is used by multithreading gluax extensions, creating 
// a Lua engine, running certain commands on it and cleaning away the 
// engine automatically. 
//
// Calling this function may take considerable time and is intended
// to be called from new threads running parallel with the main thread.
//
// Note: This function is somewhat experimental, and may be replaced by
//       a 5.0 -only function that is able to duplicate an existing Lua stack.
//
int gluahost_open (lua_State* lua);
int gluahost_close (lua_State* lua);

#if (!defined(GLUA_DYNAMIC)) && defined(LUA51_WORK1)    // Lua 5.1w1: no 'lua_dostring()'
  //
  static int lua_dostring( lua_State *L, const char *str )    // code based on 5.1w0 'lauxlib'
    {
    int st;
    
    STACK_CHECK(L)
        {    
        st= luaL_loadbuffer( L, str, strlen(str), NULL /*name*/ );
        if (st == 0)  // parse OK?
            st= lua_pcall( L, 0, LUA_MULTRET, 0 );

        if (st != 0)    // error at TOS
            {
            fprintf( stderr, "%s\n", lua_tostring(L, -1) );
            lua_pop(L, 1);  // remove error message
            }
        }
    STACK_END(0)

    return st;
    }
#endif

  /*
int _glua_exec( const char* cmd_str, int stack_size )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_2( _glua_exec, cmd_str, stack_size );
#else
	{
	lua_State* L2;
	int ret;

	#ifdef LUA_V40
		{
		L2= lua_open( stack_size );    // Creates completely new Lua instance
		}
	#else
		{
		IGNORE_PARAM( stack_size );
		L2= lua_open( );	// (no param in Lua 5.0)
		}
	#endif

	if (!L2)
		return -999;

	// Initialize static libraries for the new thread:
	// (pretty helpless without them)
	//
	#ifndef LUA51_WORK1  // < Lua 5.1w1
	  lua_baselibopen(L2);   // btw, 'table' is missing here..
	  lua_iolibopen(L2);
	  lua_strlibopen(L2);
	  lua_mathlibopen(L2);
	  lua_dblibopen(L2);
	#else
	  luaopen_stdlibs(L2);   // does it all..
	#endif

	// Gluahost() needs to be available also for the child threads, otherwise
	// they wouldn't be able to use any extension modules.
	//
	gluahost_open(L2);
		{
		ret= lua_dostring( L2, cmd_str );
		}
	gluahost_close(L2);

    lua_close( L2 );

    return ret;
	}
#endif
}
*/

int _glua_getn( lua_State* L, int index )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L1( _glua_getn, L, index );
#else
	{
	#ifdef LUA_V40
	    return lua_getn( L, index );    // 4.0
	#else
	    return luaL_getn( L, index );    // 5.0 final
	#endif
	}
#endif
}


//---
void _glua_concat( lua_State* L, int items )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L1( _glua_concat, L, items );
#else
    lua_concat( L, items );
#endif
}


//---
void* _glua_attach( lua_State* L, const void* obj_ptr, void* new_val )
{
void* old_val;

	STACK_CHECK(L) 
	{
	// Store the object key as string (could also be stored as plain userdata)
	//
    Loc_PushMerged_ptr( L, "glua_attach_", obj_ptr );

	// Get the current value (if any)
	//
	_glua_getreg( L );

	old_val= _glua_touserdata(L,-1,NULL);	// maybe NULL

	_glua_remove( L, -1 );

	// Set new value:
	//
	if ( new_val != (void*)(-1) )	// -1 = don't change
		{
        Loc_PushMerged_ptr( L, "glua_attach_", obj_ptr );

		_glua_pushuserdata( L, 0, new_val, 0, NULL );
		//
		// Stack now has key at [-2], value at [-1].

		_glua_setreg( L );	// eats up key&value
		}
	}
	STACK_END(0)

	return old_val;
}


//---
void _glua_call( lua_State* L, int argn, int retn )
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L2( _glua_call, L, argn, retn );
#else
    // Note: We can use 'lua_pcall()' (Lua 5.0) if error handling is required.
    //
    lua_call( L, argn, retn );
#endif
}


//---
// Pushes a table reference on the Lua stack (if needed, creating the table)
// and returns the absolute stack index to it.
//
int _glua_create_tbl( lua_State* L, const char* table_name, int parent_idx )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L2( _glua_create_tbl, L, table_name, parent_idx );
#else
    //
    const char* ptr= table_name;
    const char* dot;

    if (!table_name) return 0;  // global namespace

    STACK_CHECK(L)
        {
        if (parent_idx > 0)
            lua_pushvalue( L, parent_idx );
        else    // place to global table
            {
            #ifdef LUA_V40
                lua_getglobals( L );  // pushes ref to global table
            #else   // 5.0 & later
                lua_pushvalue( L, LUA_GLOBALSINDEX );
            #endif
            }

        // Each loop goes one stage further, replacing the parent
        // table ref with the child's.
        //
        while( ptr )
            {
            dot= strchr( ptr, '.' );    // end of current stage

            if (!dot)
                {
                lua_pushstring( L, ptr ); // last stage
                ptr= NULL;
                }
            else
                {
                lua_pushlstring( L, ptr, dot-ptr );
                ptr= dot+1;
                }

            lua_pushvalue( L, -1 );   // 2nd ref to stage name

            // [-1]: stage name
            // [-2]: 2nd ref to stage name
            // [-3]: ref to parent

            lua_gettable( L, -3 );    // pops key, adds value

            if (lua_istable( L, -1 ))
                {
                // Subtable existed, proceed to next stage
                //
                lua_remove( L, -2 );  // stage name
                lua_remove( L, -2 );  // parent table
                }
            else
                {
                // Create a new table & tie it.
                //
                lua_remove(L,-1);     // 'nil' away

                lua_newtable(L);
                //
                // [-1]: empty table
                // [-2]: stage name
                // [-3]: ref to parent

                lua_pushvalue( L, -1 );   // 2nd ref to new table
                lua_insert( L, -4 );

                lua_settable( L, -3 );        
                lua_remove( L, -1 );      // parent away
                }

            ASSUME_L( lua_istable(L,-1) );  // new stage
            }
        }
    STACK_END(+1)   // should have pushed one

    // Note: We should really place the index in the Lua registry or somewhere,
    //       instead of pushing it on the stack.
    //
    return lua_gettop(L);   // top of stack

#endif  // GLUA_STATIC
}


//---
#ifdef GLUA_STATIC
static
tag_int _glua_regtag2b( lua_State *L, const char *tagname, const char *prefix )
{
int tag= 0;

    ASSUME_L( tagname );

	initCriticalSections();
    CRITICAL_SECTION_START(&_glua_regtag_cs) {

    // AK(3-Jul-03): Check if tagname already exists and reuse its tag value.
    //               This allows sharing tag types between modules.
    //
    STACK_CHECK(L)
        {
        Loc_PushMerged_str( L, prefix, tagname );  // key
        _glua_getreg( L );    // eats key, pushes 'nil' or tag value (!=0)

        tag= (int)_glua_tonumber_raw( L, -1 );    // 0 if 'nil'
        _glua_remove( L, -1 );
        }
    STACK_END(0)

    if (!tag)   // Unique tagname (create new)
        {
        tag= (int)_glua_newtag( L );  // ask Lua for a new tag number

        STACK_CHECK(L)
            {
            // write key-tagname into Lua registry 
	        //
            Loc_PushMerged_int( L, prefix, tag );	    // key
    		_glua_pushstring( L, tagname );			// value
            _glua_setreg( L );	// eats key & value

            // Place also tagname->tag mapping (for duplicate checks):
	        //
            Loc_PushMerged_str( L, prefix, tagname );
    		_glua_pushinteger( L, tag );
            _glua_setreg( L );    // eats key & value
            }
        STACK_END(0)   // Should be balanced
        }
    } CRITICAL_SECTION_END

    return (tag_int)tag;
}
#endif  // GLUA_STATIC


#if (defined GLUA_STATIC) && (!defined ENUM_PATCH)
  /*--- Enumeration methods -----------------------------------------*/

  // Enumeration userdata is given 'stock' tagmethods by default:

  //---
  // ud_enum= concat( [ud_enum a], [ud_enum b] )
  //
  GLUA_FUNC( _concat )
  {
  enum e_Glua_Type type1,type2;
  tag_int tag;
  enum_int a=0;
  enum_int b=0;
  enum e_Glua_Type mask= GLUA_TYPE_USERDATA | GLUA_TYPE_NIL;

    // Allow 'nil' for concatenation as an 'uninitialized' (0) bitfield.
    //
    type1= glua_type(1);
    type2= glua_type(2);
    
    if ( (!(type1 & mask)) || (!(type2 & mask)) )
        glua_errorN( "Concat: type mismatch! (%s %s)", glua_typename(1), glua_typename(2) );

    if (type1 == GLUA_TYPE_NIL)
        b= _glua_toenum(_lua_,2,&tag);
    else
    if (type2 == GLUA_TYPE_NIL)
        a= _glua_toenum(_lua_,1,&tag);
    else
        {   // normal: both 'a' and 'b' are userdata
        tag_int tag2;
        a= _glua_toenum(_lua_,1,&tag);
        b= _glua_toenum(_lua_,2,&tag2);

        if (tag != tag2)
            glua_error( "Concat: enum mismatch!" );    // could name the tag types?
        }

    glua_pushEnum_mt( a|b, tag );
  }
  GLUA_END

  //---
  // bool= call( ud_enum a, ["test",] ud_enum b, ... )
  // ud_enum= call( ud_enum a, "or"|"and"|"xor", ud_enum b, ... )
  // ud_enum= call( ud_enum a, "xor", [ud_enum b, ...] )
  // ud_enum= call( ud_enum a, "not" )    (same as 'xor' without arguments)
  // ud_enum= call( ud_enum a, "<<"|">>" [,int=1] )
  // num= call( ud_enum a, "number" )
  // str= call( ud_enum a, "string" [,base_uint=10 [,width_uint]] )
  //
  // Multitude of binary operations can be implemented via the 'call' metamethod.
  //
  GLUA_FUNC( _call )
  {
  tag_int tag1;
  const char* op= NULL;
  char op_c= '\0';
  uint n=1;
  uint argn= glua_argn();

    // All operators have different first characters, this speeds us up.
    
    enum_int a= _glua_toenum(_lua_,1,&tag1);

    if (glua_isString(2))
        {
        op= glua_getString(2);
        op_c= tolower(*op);
        n++;
        }

    if ((!op) || (strcmp(op,"test")==0))    // the only returning 'bool'
        {
        enum_int mask=0;
        while( ++n <= argn )
            mask |= glua_getEnum(n,tag1);    // must have same tags
        glua_pushBoolean( a&mask );
        
        break;  // done (goes to 'GLUA_END')
        }

    if (strcmp(op,"number")==0)
        {
        glua_pushInteger(a);
        break;  // done (goes to 'GLUA_END')
        }
  /**
    if (op_c=='v')  // 'value'  (could also be: 'int' ?)
        {
        // Note: Over 24-bit values won't fit if host has 'float' numbertype
        //       (gluax will cause an error).
        //
        glua_pushInteger(a);
        break;  // done (goes to 'GLUA_END')
        }
  **/
    if (op_c=='s')  // 'string'
        {
        uint base= glua_getUnsigned_def(3,10);
        uint width= glua_getUnsigned(4);
        char buf[32+1];   // long enough
        
        switch( base )
            {
            case 2:   // no 'sprintf()' for binary
                {
                char* ptr= buf;
                if (!width) width=32;
                
                while( width-- )
                    *ptr++= (a & (1<<width)) ? '1':'0';
                *ptr= '\0';
                }
                break;
    
            case 10:
                sprintf( buf, "%d", a );    // width ignored
                break;
    
            case 16:
                sprintf( buf, "%0*x", a, width ? width:8 );
                break;
            
            default:
                glua_errorN( "Bad base: %d", base );
            }
        
        glua_pushString(buf);
        break;  // done (goes to 'GLUA_END')
        }
        
    switch( op_c )  // these all return 'enum'
        {
        case 'o':   // or
            while( ++n <= argn )
                a |= glua_getEnum(n,tag1);
            break;

        case 'a':   // and
            while( ++n <= argn )
                a &= glua_getEnum(n,tag1);
            break;

        case 'x':   // xor
            if (argn==2)
                a ^= 0xffffffffU;
            else
                while( ++n <= argn )
                    a ^= glua_getEnum(n,tag1);
            break;

        case 'n':   // not/neg
            a ^= 0xffffffffU;   // negate every bit (same as "xor" without params)
            break;
            
        case '<':   // << (left shift)
            ASSUME( argn<=3 );
            a <<= glua_getInteger_def(3,1);
            break;
            
        case '>':   // >> (right shift)
            // Note: We're _not_ using arithmetic shift here, although the values
            //       are otherwise regarded as signed. Bit31 always becomes zero.
            //       
            ASSUME( argn<=3 );
            a= ((uint)a) >> glua_getInteger_def(3,1);
            break;
            
        default:
            glua_errorN( "Unknown operator: '%s'", op );
        }

    glua_pushEnum_mt( a, tag1 );
  }
  GLUA_END

  //---
  // str= tostring( ud_enum a )
  //
  GLUA_FUNC( _tostring )
  {
  enum_int a= _glua_toenum(_lua_,1,NULL /*&tag1*/);
  char buf[32+1];   // long enough
        
    sprintf( buf, "%d", a );    // full width
    glua_pushString(buf);
  }
  GLUA_END

  //---
  // int= index( ud_enum a, ud_enum key )
  // int= index( ud_enum a, uint bit )    <-- experimental!
  // int= index( ud_enum a, "value" )
  //
  // Index notation can be used for testing against a certain flag bit or a masked value
  // within the enum. I.e. "var[HAS_FULLSCREEN]"
  //
  GLUA_FUNC( _index )
  {
  tag_int tag1;
  enum_int a= _glua_toenum(_lua_,1,&tag1);
  //
  uint mask=0;  // must be unsigned, so shift down places 0's at MSB.
  int ret;

    switch( glua_type(2) )
        {
        case GLUA_TYPE_USERDATA:
            mask= (uint)glua_getEnum(2,tag1);     // bitfield mask (often, just a single bit)
            break;
            
        case GLUA_TYPE_INT:     // mask with a bit location (0..31):  'var[20]'
            mask= 1<< glua_getUnsigned(2);
            break;
        
        case GLUA_TYPE_STRING:
            if (strcmp(glua_getString(2),"value")==0)   // enum.value
                {
                glua_pushInteger(a);
                break;
                }
            // pass-through..
        default:
            glua_errorN( "Bad enum index: type '%s'", glua_typename(2) );
        }

    if (mask!=0)
        {
        while( (mask&0x01) == 0 )   // shift both mask & value down to 0..N
            {
            mask >>= 1;
            a >>= 1;
            }
        ret= a & mask;

        glua_pushInteger( a & mask );
        }
  }
  GLUA_END

  //---
  // void= ud_newindex( ud_enum a, ud_enum/uint key, ud_enum/uint/bool value )
  //
  // Setting bits by the table notation?
  //
  //      myflags[FULLSCREEN]= true
  //      myflags[BITFIELD]= 10
  //      myflags[BITFIELD]= PRESET_VALUE
  //      myflags[20]= 1
  //
  // Note: This is the only method actually _changing_ a userdata 'in location'
  //       after it's creation. Is that a problem?
  //
  GLUA_FUNC( _newindex )
  {
  tag_int tag1;
  uint mask=0;
  enum_int val=0;

    _glua_toenum(_lua_,1,&tag1);    // just get the tag

    switch( glua_type(2) )  // key
        {
        case GLUA_TYPE_USERDATA:
            mask= (uint)glua_getEnum(2,tag1);     // bitfield mask (often, just a single bit)
            break;
            
        case GLUA_TYPE_INT:     // mask with a bit location (0..31):  'var[20]=...'
            mask= 1<< glua_getUnsigned(2);
            break;

        default:
            glua_errorN( "Bad enum index: type '%s'", glua_typename(2) );
        }

    switch( glua_type(3) )  // value
        {
        case GLUA_TYPE_USERDATA:
            val= glua_getEnum(3,tag1);     // must be same enum 
            break;
            
        case GLUA_TYPE_INT:     // integer values can be used (none > 24 bits)
            val= glua_getUnsigned(3);
            break;

        case GLUA_TYPE_NIL:         // same as 'false' (clears all bits)
        case GLUA_TYPE_BOOLEAN:     // true=1, false=0
            //
            // 'true' sets all the bits of a bitfield (if mask is wider than 1)
            //
            val= glua_getBoolean(3) ? 0xffffffffU : 0;
            break;

        default:
            glua_errorN( "Bad value type: %s", glua_typename(3) );
        }

    if (val!=0)   // 'val' needs to be shifted to right position
        {
        uint n= mask;
        while( (n&0x01) == 0 )
            {
            n >>= 1;
            val <<= 1;
            }
        }
   
    _glua_modenum(_lua_,1,val,mask);   // modify the existing value
  }
  GLUA_END

  //---
  // bool= eq( ud_enum a, ud_enum b )
  //
  GLUA_FUNC( _eq )
  {
  tag_int tag1,tag2;

    enum_int a= _glua_toenum(_lua_,1,&tag1);
    enum_int b= _glua_toenum(_lua_,2,&tag2);
    
    if (tag1 != tag2)
        glua_error( "EQ: Enum type mismatch!" );
    
    glua_pushBoolean( a == b );
  }
  GLUA_END

  //---
  // bool= lt( ud_enum a, ud_enum b )
  //
  GLUA_FUNC( _lt )
  {
  tag_int tag1,tag2;

    enum_int a= _glua_toenum(_lua_,1,&tag1);
    enum_int b= _glua_toenum(_lua_,2,&tag2);
    
    if (tag1 != tag2)
        glua_error( "LT: Enum type mismatch!" );
    
    glua_pushBoolean( a < b );
  }
  GLUA_END
#endif  // !ENUM_PATCH


tag_int _glua_regtag2( lua_State *L, const char *tagname, bool_int isenum )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L2( _glua_regtag2, L, tagname, isenum );
#else
    tag_int tag;
    
  #ifdef ENUM_PATCH
    tag= isenum ? (tag_int)lua_newenumtag( L, tagname )
                : _glua_regtag2b( L, tagname, TAG_PREFIX );
  #else
    tag= _glua_regtag2b( L, tagname, (isenum) ? ENUM_PREFIX:TAG_PREFIX );

    if (isenum)
        {
        // Add 'stock' tagmethods to enums (may be overridden by the modules):
        //
        _glua_settagmethod( L, tag, "concat", fn__concat );   // bor
        _glua_settagmethod( L, tag, "call", fn__call );       // btest, <<, >>, ..
        _glua_settagmethod( L, tag, "eq", fn__eq );           // ==
        _glua_settagmethod( L, tag, "lt", fn__lt );           // <
        _glua_settagmethod( L, tag, "index", fn__index );     // .key
        _glua_settagmethod( L, tag, "newindex", fn__newindex );  // .key=
        _glua_settagmethod( L, tag, "tostring", fn__tostring );
        }
  #endif

    ASSUME_L( tag != 0 );
    return tag;
#endif
}

#ifdef GLUA10_COMPATIBLE
  int _glua_regtag( lua_State *lua, const char *tagname )
    {
    return (int)_glua_regtag2( lua, tagname, 0 /*userdata*/ );
    }
#endif

#ifdef GLUA_STATIC
static
const char *Loc_tagname_try( lua_State *L, tag_int tag, const char *prefix )
{
const char *ret;

    STACK_CHECK(L)
        {
        Loc_PushMerged_int( L, prefix, (int)tag );
        _glua_getreg(L);
        ret= _glua_tostring_raw(L,-1);
        _glua_remove(L,-1);
        }
    STACK_END(0)

    return ret;
}
#endif  // GLUA_STATIC

const char *_glua_tagname( lua_State *L, tag_int tag )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L1( _glua_tagname, L, tag );
#else
    const char *ret;

  #ifdef ENUM_PATCH
    if ( (int)tag < 0 )  // enum name known by Lua core
        return lua_enumtagname( L, (lua_EnumTag)tag );
  #endif

    ret= Loc_tagname_try( L, tag, TAG_PREFIX );
  #ifndef ENUM_PATCH
    if (!ret)
        ret= Loc_tagname_try( L, tag, ENUM_PREFIX );
  #endif
    return ret;
#endif
}


/*--- Reference mapping ------------------------------------------*/
//
// Added 2-Sep-03/AK.
//
// Purpose is to allow Lua tables and functions (which can only be accessed as 
// stack references) to be tied with C-level userdata pointers. Needed for
// Lua level event handlers.

//---
void _glua_setudref( lua_State* L, void* ptr, int n )
{
	STACK_CHECK(L) 
    	{
        n= STACK_ABS(L,n);

        Loc_PushMerged_ptr( L, "glua_udref_", ptr );

        if (n!=0) _glua_pushvalue( L, n );  // ref to object table
        else      _glua_pushnil(L);         // clear the reference
        //
	    // Stack now has key at [-2], value at [-1].

	    _glua_setreg( L );	// eats up key&value
	    }
	STACK_END(0)
}

//--
// If 'member' is given, both 'obj.<member>' and the object itself (in this 
// order) will be pushed on the stack.
//
int // pushed (0/1/2)
_glua_getudref( lua_State* L, void* ptr, const char* member )
{
int pushed=0;

	STACK_CHECK(L) 
    	{
        Loc_PushMerged_ptr( L, "glua_udref_", ptr );

	    _glua_getreg( L );	// eats key, pushes value (may be 'nil')

        if (_glua_type(L,-1) <= GLUA_TYPE_NIL)
            _glua_settop( L, -2 );  // pop 1
        else
            {
            if (!member)
                pushed= 1;  // only object pushed
            else
                {
                _glua_pushstring(L,member);
                //
                // [-2]: object table
                // [-1]: key name

                _glua_gettable( L, -2 );   // pops key, pushes value (or nil)

                if (_glua_type(L,-1) <= GLUA_TYPE_NIL)
                    _glua_settop( L, -3 );  // pop 2
                else
                    {
                    _glua_insert( L, -2 );  // swap [-1] and [-2]
                    pushed= 2;              // (function first, obj is its param)
                    }
                }
            }
	    }
	STACK_END( pushed )

    return pushed;
}


/*--- IntArray support ------------------------------------------*/

// Read-only integer array userdata, for places where lots of information
// is passed to the Lua side i.e. in callbacks (and making a proper Lua
// table would be unnecessary, or too costly).

static tag_int tag_intarray;

// Win32/MSC uses '_int8', avoid it.
//
typedef signed char _int8_;
typedef unsigned char _uint8_;
typedef signed short _int16_;
typedef unsigned short _uint16_;
typedef signed long _int32_;
typedef unsigned long _uint32_;

// Note: a structure defined this way (with no malloc'ed members) will 
//       not require a custom garbage collector (Lua internal will do fine).
//
struct s_IntArray
{
    uint items;     // number of items (1..top for Lua, 0..top-1 for C)
    _int8_ style;   // GLUA_INTARRAY_... (1=uint8, -1=int8, 2=uint16, -2=int16)
    
    union {
        _uint8_ u8[0];    // start of actual data (tail buffer)
        _int8_  s8[0];
        _uint16_ u16[0];
        _int16_  s16[0];
        _uint32_ u32[0];
        _int32_  s32[0];
        } data;
};

void _glua_pushintarray( lua_State *L, void *p, uint items, int style )
{
    uint bytes= style<0 ? -style:style;
    size_t len= items * bytes;

    struct s_IntArray *s= (struct s_IntArray*)
        _glua_pushuserdata_raw( L, tag_intarray, NULL, sizeof(struct s_IntArray)+len, 0 );
    ASSUME_L(s);

    ASSUME_L(p);
    ASSUME_L( (style>=-4) && (style<=4) );
    
  #if 0     // DEBUGGING!!!
    fprintf( stderr, "%p %d %d\n", p, items, style );
    fprintf( stderr, "%d\n", (int)sizeof(struct s_IntArray) );
    fprintf( stderr, "%d\n", _glua_gettop(L) );
    //memcpy( s->data.u8, p, len );
    s->items= 0;
    s->style= style;
  #else
    memcpy( s->data.u8, p, len );
    s->items= items;
    s->style= style;
  #endif
}

//---
// [int]= index( ud, int )
//
GLUA_FUNC( intarray_index )
{
struct s_IntArray *s= (struct s_IntArray*)glua_getUserdata(1,tag_intarray);
int n= glua_getInteger(2);

    ASSUME(s);
    
    if ((n<1) || ((uint)n > s->items))
        break;  // return 'nil' (out of range)

    switch( s->style )
        {
        case 1:     // GLUA_INTARRAY_U8
            glua_pushUnsigned( s->data.u8[n-1] );
            break;
        case -1:    // GLUA_INTARRAY_S8
            glua_pushInteger( s->data.s8[n-1] );
            break;
        case 2:     // GLUA_INTARRAY_U16
            glua_pushUnsigned( s->data.u16[n-1] );
            break;
        case -2:    // GLUA_INTARRAY_S16
            glua_pushInteger( s->data.s16[n-1] );
            break;
        case 4:     // GLUA_INTARRAY_U32
            glua_pushUnsigned( s->data.u32[n-1] );
            break;
        case -4:    // GLUA_INTARRAY_S32
            glua_pushInteger( s->data.s32[n-1] );
            break;
        default:
            ASSUME(FALSE);  // shouldn't happen
            break;
        }
}
GLUA_END


/*--- Dynamic linkage support ------------------------------------------*/
//
// Added 14-Jul-03/AK.
// Revised 20-Jan-05/AK: using Lua 5.1w4 loadlib.c directly, now

#ifdef GLUA_STATIC
  // loadlib.c
  LUA_API void ll_unloadlib( void* lib );
  LUA_API void* ll_load( lua_State* L, const char* path );
  LUA_API lua_CFunction ll_sym( lua_State* L, void* lib, const char* sym );
#endif

// These functions allow GluaX modules to link to DLLs at runtime. This is
// required e.g. when lib files don't exist (GPIB on gcc) or for DynaLoad.
//
// On error (NULL return) the functions push an error message to Lua stack.
//
//void _ll_unloadlib( void *lib )
//{
//#ifdef GLUA_DYNAMIC
//    HOST_CALL_1( _ll_unloadlib, lib );
//#else
//    if (lib) ll_unloadlib( lib );
//#endif
//}
//
//void* _ll_load( lua_State* L, const char* path )
//{
//#ifdef GLUA_DYNAMIC
//    return HOST_CALL_L1( _ll_load, L, path );
//#else
//    return ll_load( L, path );
//#endif
//}

// This returns a 'lua_CFunction' but of course it's just a pointer,
// the function can have whatever prototype.
//
//lua_CFunction _ll_sym( lua_State* L, void* lib, const char* sym )
//{
//#ifdef GLUA_DYNAMIC
//    return HOST_CALL_L2( _ll_sym, L, lib, sym );
//#else
//    if (!lib) return NULL;
//    return ll_sym( L, lib, sym );
//#endif
//}


/*--- Asynchronous queues ----------------------------------------*/

// Note: Opaqueness of data structures gives us total freedom to
//       change their interior fields at will, without affecting
//       module ABI in any way.

#ifdef GLUA_STATIC
  // --- QueueItem ---
  //
  struct s_QueueItem
    {
    struct s_QueueItem *next;
    uint style;
    size_t bytes;
    _uint8_ data[0];     // tail buffer
    };
    
  static 
  struct s_QueueItem *Loc_QueueItemNew( void *data, size_t bytes, uint style )
    {
        struct s_QueueItem *p= malloc( sizeof(*p) + bytes );
        assert(p);

        if (data)
            {
            // Win32/PocketPC half word swap fixing (SDL_Mixer bug, really?)
          #if (defined PLATFORM_WIN32) || (defined PLATFORM_WINCE)
            if (style & GLUA_QUEUE_ARRAY_MESSEDUP)
                {
                _uint16_* to= (_uint16_*)p->data;
                _uint16_* from= data;
                uint n;
                
                for( n=0; n<bytes/2; n+=4 )
                    {
                    // data-data-0000-0000  -->  data-0000-data-0000
                    to[n]= from[n];
                    to[n+1]= from[n+2];
                    to[n+2]= from[n+1];
                    to[n+3]= from[n+3];
                    }
                }
            else
          #endif
                memcpy( p->data, data, bytes );   // normal, 1:1 copy
            }

        p->bytes= bytes;
        p->style= style & (0xff|GLUA_QUEUE_ARRAY_SIGNED);   // remove MSB/LSB info
        p->next= NULL;
        return p;
    }

  static 
  void Loc_QueueItemFree( struct s_QueueItem* item )
    {
        if (item) free(item);   // that's all!
    }

  static 
  void Loc_QueueItemCallback( lua_State* L, void *key, void *data, size_t bytes, uint style )
    {
      int _signed= (style & GLUA_QUEUE_ARRAY_SIGNED);

      int pushed= _glua_getudref( L, key, NULL );
      ASSUME_L( pushed==1 );
      
      ASSUME_L( _glua_type(L,-1)==GLUA_TYPE_FUNCTION );

      if (bytes && data) switch( style & 0xff )
        {
        case 8:
            if (bytes==1)
                _glua_pushinteger( L, _signed ? *((_int8_*)data) : *((_uint8_*)data) );
            else
                _glua_pushintarray( L, data, bytes /*items*/, _signed ? -1:+1 );
            break;
        case 16:
            if (bytes==2)
                _glua_pushinteger( L, _signed ? *((_int16_*)data) : *((_uint16_*)data) );
            else
                _glua_pushintarray( L, data, bytes/2 /*items*/, _signed ? -2:+2 );
            break;
        case 32:
            if (bytes==4)
                {
                if (_signed) _glua_pushinteger( L, *((_int32_*)data) );
                else         _glua_pushunsigned( L, *((_uint32_*)data) );
                }
            else
                _glua_pushintarray( L, data, bytes/4 /*items*/, _signed ? -4:+4 );
            break;
        default:
            ASSUME_L( FALSE );
        }

      lua_call( L, bytes ? 1:0, 0 );      // void= lua_func( [int]/[array_ud] )
    }

  // --- Glua_Queue ---
  //
  struct s_Glua_Queue
    {
    struct s_QueueItem* items;   // one item per callback
    enum e_Glua_QueuePolicy policy;
    //
    struct s_Glua_Queue *next;      // linked list started at 'first_queue'
    };
    
  static struct s_Glua_Queue* first_queue= NULL;  // list of all opened queues

  static
  struct s_Glua_Queue* Loc_QueueOpen( enum e_Glua_QueuePolicy policy )
    {
        struct s_Glua_Queue* s= 
            (struct s_Glua_Queue*) malloc( sizeof(struct s_Glua_Queue) );
        assert(s);
        s->items= NULL;
        s->policy= policy;
        s->next= NULL;

        { struct s_Glua_Queue** next_ref= &first_queue;
          while( *next_ref )
            next_ref= &(*next_ref)->next;
            
        // 'next_ref' points to last (empty) slot in the list
        *next_ref= s; }

        return s;
    }

  static
  void Loc_QueueClose( struct s_Glua_Queue* q )
    {
        // Don't do anything. 
    }
#endif

//---
// Initialize an asynchronous queue, attaching a Lua closure to it.
//
// Note: All 'first_queue' list handling happens in the main thread,
//       so it needs no protection.
//
void _glua_queue_attach( lua_State *L, 
                         struct s_Glua_Queue** ref,
                         int argn,        // Lua closure
                         uint policy )    // e_Glua_QueuePolicy
{
#ifdef GLUA_DYNAMIC
    HOST_CALL_L3( _glua_queue_attach, L, ref, argn, policy );
#else
    ASSUME_L(ref);
    
//fprintf( stderr, "queue_attach(ref=%p argn=%d)\n", ref, argn );
    if (*ref)
        Loc_QueueClose(*ref);

    if (argn==0)
        {
        // Detaching: remove the old UD -> closure bind:
        if (*ref) _glua_setudref( L, (void*)*ref, 0 );
        }
    else
        {
        ASSUME_L( _glua_type(L,argn) == GLUA_TYPE_FUNCTION );

        *ref= Loc_QueueOpen(policy);
        ASSUME_L(*ref);

        // Use queue pointer as a key to callback closure
        _glua_setudref( L, (void*)(*ref), argn );
        }
#endif
}

//---
// Add a callback to the asynchronous queue.
//
// Queue policy (see 'glua_queue_open()' defines, what to do if there
// already is an entry in the queue.
//
// Returns: TRUE if the items was added, and no other items was skipped
//          FALSE if either this or earlier item was lost
//
bool_int _glua_queue_add( struct s_Glua_Queue* queue, 
                          void* p, 
                          uint /*size_t*/ bytes, 
                          uint style )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_4( _glua_queue_add, queue, p, bytes, style );
#else
    bool_int skip_this= FALSE;
    bool_int skip_last= FALSE;

    // The 'queue_add()' calls come from other than the Lua main thread.
    // There's no guarantee they couldn't overlap each other, so we need 
    // protection.
    //
	CRITICAL_SECTION_START(&_glua_queue_add_cs)
      {
      struct s_QueueItem** item_ref= &queue->items;

  #if 0     // DEBUG!!!
    _uint8_* p8= (_uint8_*)p;
    _uint16_* p16= (_uint16_*)p;
    _uint32_* p32= (_uint32_*)p;
    int i;

    for( i=0; i<bytes; i+=4 )
        {
        fprintf( stderr, "%02x-%02x-%02x-%02x %04x-%04x %08x\n",
                         (int)p8[0], (int)p8[1], (int)p8[2], (int)p8[3],
                         (int)p16[0], (int)p16[1], 
                         (int)p32[0] );
        p8+=4; p16+=2; p32++;
        }
  #endif

      switch( queue->policy )
        {
        case GLUA_QUEUE_KEEP_ALL:
            while (*item_ref)
                item_ref= &(*item_ref)->next;  // find tail
            break;
        case GLUA_QUEUE_KEEP_FIRST:
            if (*item_ref) 
                skip_this= TRUE;    // skip this data
            break;
        case GLUA_QUEUE_KEEP_LAST:
            if (*item_ref)
                skip_last= TRUE;
            break;
        default:
            assert(FALSE);  // shouldn't happen
        }

      if (skip_last)
        { 
        assert( (*item_ref)->next == NULL );    // with this policy, we cannot have two items
        Loc_QueueItemFree(*item_ref);
        //*item_ref= NULL; 
        }

      if (!skip_this)
        *item_ref= Loc_QueueItemNew( p, bytes, style );
      }
    CRITICAL_SECTION_END

    return (skip_last || skip_this) ? FALSE : TRUE;
#endif
}

//---
// Run one callback (and remove it) from the queue.
//
// If the caller wants to clear all (or a certain number) of callbacks,
// it can loop the call until 'FALSE' is returned (or limit met).
//
// Returns: TRUE if a callback was made (and one item removed from queue)
//          FALSE if the queue was empty (end of looping)
//
bool_int _glua_queue_run( lua_State *L, struct s_Glua_Queue* queue )
{
#ifdef GLUA_DYNAMIC
    return HOST_CALL_L1( _glua_queue_run, L, queue );
#else
    struct s_QueueItem* item;

    if (!first_queue) return FALSE;     // no queues attached

    // Note: This is not very fair to the queues, emptying one completely
    //       before moving to the next one. Can be changed, if causes problems.
    //
    if (!queue)     // run from any queue
        {
        queue= first_queue;
        ASSUME_L(queue);

        while( (!queue->items) && queue->next )
            queue= queue->next;
        }

    item= queue->items;
    if (!item) return FALSE;     // no pending callback

    // Detach the item from the queue
    //
    queue->items= item->next;

//fprintf( stderr, "queue callback: %p %d %d\n", item->data, (int)item->bytes, (int)item->style );
    Loc_QueueItemCallback( L, (void*)queue /*key*/, item->data, item->bytes, item->style );
    Loc_QueueItemFree( item );
        
    return TRUE;
#endif
}


/*--- Garbage collection support ------------------------------------------*/

//---
// Garbage Collection hub, called by Lua engine.
//
#ifdef GLUA_STATIC
//
int fn_GcHub( lua_State* L )   // void = GcHub( ud )
{
void* obj= _glua_getUserdata(L,1,0 /*any tag*/,NULL);
void* v_ptr;
lua_CFunction custom;

    ASSUME_L(obj);    // hmm.. no idea calling 'gc' for a NULL object

	STACK_CHECK(L) 
	{
    // Before we start, if there's a custom cleanup, perform it
    // (may free up resources etc. that we wouldn't know of).
    //
    custom= lua_tocfunction( L, lua_upvalueindex(1) );

    if (custom)
        {
        int pushed= custom(L);
        ASSUME_L(pushed==0);  // "gc" shouldn't push stuff..
        }

	// Find associated garbage-collection func via Lua registry:
	//
    // Note: Normally we cannot use direct 'lua_...()' functions within
    //       'GLUA_FUNC()' but here we're always linking statically, so 
    //       that is okay.
    //
	lua_pushvalue(L,-1);	// temp copy of userdata
	_glua_getreg( L );		// eats up temp copy, pushes value or 'nil'

	v_ptr= lua_touserdata( L, -1 );
	lua_remove(L,-1);

	if (v_ptr)
		{
		// Call application's GC func:
		//
        // Object item is still at top-of-stack (-1) as it was when this
        // function was called.
        //
    	(* ((lua_CFunction)v_ptr) )( L );

	    // Remove the registry entry that's no longer needed:
		//
		lua_pushvalue(L,-1);	// temp copy of userdata (key)
		lua_pushnil(L);			// (value)
		_glua_setreg(L);		// (pops key & value)
		}
	}
	STACK_END(0)	// stack should be untouched
	
	return 0;  // nothing to push (void)
}
//
#endif	// GLUA_STATIC


//---
// Simple garbage collection for memory blocks allocated by us:
//
#if defined(GLUA_STATIC) && defined(LUA_V40)
  //
  static int fn_GcFree( lua_State* L )   // void = GcFree( ud )
    {
    void* p= glua_getUserdata(1,0);
    lua_CFunction custom;

        ASSUME_L(p);

        lua_pushupvalue( L, 1 );    // our piggyback (if any)
        custom= lua_tocfunction(L,-1);
        lua_remove(L,1);

        if (custom)
            {
            int pushed= custom(L);  // custom 'gc'
            ASSUME_L( pushed==0 );
            }
        
	    free(p);

        return 0;   // nothing to push
    }
#endif


/*--- Error handling ---------------------------------------------*/

//-----
const char* _glua_typename( enum e_Glua_Type type )
{
    switch( type )
        {
        case GLUA_TYPE_NIL:         return "nil";
        case GLUA_TYPE_BOOLEAN:     return "boolean";
        case GLUA_TYPE_NUMBER:      return "number";
        case GLUA_TYPE_INT:         return "integer";
        case GLUA_TYPE_STRING:      return "string";
        case GLUA_TYPE_TABLE:       return "table";
        case GLUA_TYPE_FUNCTION:    return "function";
        case GLUA_TYPE_USERDATA:    return "userdata";  // requested ud
        case GLUA_TYPE_USERDATA_UNK:  return "userdata_unk";  // other ud
        case GLUA_TYPE_ENUM:        return "enum";
        default:                    return "???";
        }
}

//-----
// Note: 'argn_'==0 for table reads (-1 index)
//
static int _glua_badparam( lua_State *L, int argn_, char which, tag_int opt_tag )
{
char errbuf[999];   // protected (see below)
const char* msg;
const char* type;
const char* tagname= NULL;
const char* type_str;
const char* val_str;
enum e_Glua_Type typen;
//
int argn= argn_ ? argn_ : _glua_gettop(L);

    ASSUME_L( argn>0 );   // absolute index
        
    typen= _glua_type2( L, argn, opt_tag );
    type_str= _glua_typename( typen );
    
    val_str= _glua_tostring_safe( L, argn );
    if (!val_str) val_str="";

	// clean interface stack to prevent worst-case overflow
	// Note: 'argn' cannot be used for accessing stack after this!
	//
	_glua_settop( L, 0 );
  
	if ((which=='U') || (which=='E'))	// userdata/enum
		{
    	// read tagname from Lua registry
		//
		tagname= _glua_tagname( L, opt_tag );
		if (!tagname)
			tagname= "???";
		}

    msg= ( typen & (GLUA_TYPE_BOOLEAN | GLUA_TYPE_STRING) ) ? "arg #%d (%s '%s') not a valid " :
         ( typen & (GLUA_TYPE_NUMBER | GLUA_TYPE_INT ) ) ? "arg #%d (%s %s) not a valid " :
         "arg #%d (%s%s) not a valid ";   // default
        
	type= NULL;

	switch (which)
		{
		case 'N':	type= "number";		break;
		case 'I':	type= "integer";	break;
		case 'B':	type= "boolean";	break;
		case 'S':	type= "string";		break;
		case 'U':	type= "userdata: "; break;
		case 'E':	type= "enum: "; break;
		case 'T':   type= "table";		break;

		case '\0':	// none
			msg = "too many params (arg #%d: %s '%s')";
			break;

		default:
			msg = "problem with arg #%d (%s '%s')";
			break;
		}

    ASSUME_L( sizeof(errbuf) > strlen(msg) /*+ strlen(func_name)*/ + 10 /*argn_*/
                             + strlen(type_str) + strlen(val_str) );
    
	sprintf(errbuf, msg, argn_, type_str, val_str );
	
    if (type)    strcat_safe( errbuf, type, sizeof(errbuf) );
	if (tagname) strcat_safe( errbuf, tagname, sizeof(errbuf) );
     
	_glua_error( L, errbuf );    // never returns

	return 0;  // dummy
}


/*--- Table handling ---------------------------------------------*/

//-----
// Given a table object in the stack ('argn') and a key string with optional
// subkeys (s.a. "obj.color.red"), returns the entry beneath that reference.
//
// Params: If 'index' is given (nonzero), it is taken to be a numeric first-
//         level name (same as providing "1.more.keys" in the string parameter
//         (but this should be more efficient).
//
static bool_int // TRUE:an entry was pushed to stack
                // FALSE: entry already there
                //
Loc_TableEntry( lua_State* L, int argn, 
                const int* index1,	  // NULL = not used
                const int* index2,
                const char* key_with_subkeys )  // e.g. "obj.color.red"
{                     
char key_buf[99];   // key without subkeys
const char* key= NULL;
const char* dot= NULL;
uint len;
int index_store;
bool_int pushed;

    ASSUME_L( argn>=0 );   // no relative indices, please
    
    if (argn==0)
        {
        _glua_pushnil(L);   // no table, so no contents either.
        return TRUE;
        }
        
	if (!index1)	// skip if first level numeric
		{
		if (key_with_subkeys == NULL)	// Iterating through main table?
			{
			return FALSE;	// Value already at 'stack[-1]' (no remove required)
			}

		// Key given as a parameter - may contain 'subtable diving':
		//
		dot= strchr( key_with_subkeys, '.' );    // NULL if no subkeys
        
		if (!dot)
			key= key_with_subkeys;
		else
			{
			len= (dot - key_with_subkeys);  // 1st level key length

			if (len >= sizeof(key_buf))   // exceeding buffer limit?!
				{
				_glua_pushnil(L);
				return TRUE;
				}
        
            // only copy until the dot (exclusive):
            //
			key= strcpy_safe( key_buf, key_with_subkeys, len+1 );
			}
		//
		// 'key' now has the name of this level into the stack

		// If 'key' is totally numeric, use integer index instead of string
		// ( In Lua, 'tbl[1]' and 'tbl['1']' are different! )
		//
		if (Loc_IsNumeric( key ))
			{
			index_store= atoi(key);
			index1= &index_store;    // was NULL
			}
		}
    
    STACK_CHECK(L)
    {
    if (index1) _glua_pushnumber( L, (glua_num_t)(*index1) );
	else        _glua_pushstring( L, key );

    _glua_gettable( L, argn );  // pops key, pushes entry back (or 'nil')

	if (index2 || dot)	  // Diving deeper...
        {
		// Subtable reference should now be at [-1]
		//
		if (_glua_type(L,-1) == GLUA_TYPE_TABLE)
			{
			int subtbl= _glua_gettop(L);
			pushed= Loc_TableEntry( L, subtbl,
             					    index2,	 // new index1
							        NULL,     // index2
			                        index2 ? key_with_subkeys : dot+1 );
            ASSUME_L(pushed);
			_glua_remove( L, subtbl );	// Subtable entry no longer needed
			}
        else    // bad key or no subtable (return 'nil')
			{
			_glua_remove( L,-1 );  // remove the bad thing
			_glua_pushnil(L);
			}
        }    
    }
    STACK_END(+1)     // Should have pushed one (and only one) onto the stack

	return TRUE;	// Value pushed to [-1] - needs to be removed.
}    

 
//---                       
bool_int _glua_gettablekey( lua_State* L,
                            uint argn,  // 0 = no table
                            const char** key_ref, 
                            enum e_Glua_Type* type_ref )
{
bool_int found;

    if (!argn) return FALSE;    // 'nil' param - see as an empty table
    
    ASSUME_L( key_ref );	 // Must have it! (module coding error if not)

	if( !*key_ref )    // First round, no previous key on stack
	   {
		_glua_pushnil(L);
        }
    else
        {
		// stack[-1]= last value (may be removed)
        // stack[-2]= last key (it was left in the stack by us)

		_glua_remove( L, -1 );	// last key becomes [-1]
        }

	STACK_CHECK(L)
	{
	found= _glua_next( L, argn );
	//
	if (!found)
		{
		// stack[-1] popped by the Lua engine
		}
	else
	    {
	    // stack[-1]= next value
	    // stack[-2]= next key
	    
        if (type_ref)
			*type_ref= (enum e_Glua_Type)_glua_type(L,-1);

		// We cannot do a regular 'tostring()' on [-2], since that would
		// (as a side-effect) change an integer index into string, which
		// would make future 'next()' call fail! So, we'll make a local
		// copy and get the conversion via it.
		//
		*key_ref= _glua_tostring_safe( L, -2 );

		// Leave the stack value on the stack. 'getTable_...()' functions
		// with 'NULL' key name will be able to fetch it directly.
		//
        // Also key value is left into the stack at [-2].
        }
	}
	STACK_END( found ? +1 : -1 )

	return found;   // 0 = no more entries
}                                    

//---
static void Loc_PushMerged_ijk( lua_State* L, int i, int j, const char* key )
{
uint n=1;

    ASSUME_L( i!=0 );    // "i.j", "i.k" or "i.j.k"

    STACK_CHECK(L)
        {
        _glua_pushinteger( L, i );

        if (j)
            { 
            _glua_pushliteral( L, "." );
            _glua_pushinteger( L, j );
            n += 2;
            }

        if (key)
            {
            _glua_pushliteral( L, "." );
            _glua_pushstring( L, key );
            n += 2;
            }

        _glua_concat( L, n );   // Leaves one string on the stack
        }
    STACK_END(+1)
}

//---
// [-2]= table to attach to
// [-1]= value (int/string/userdata/table/whatever..)
//
void _glua_tblkey_i( lua_State* L, int i )
{
    ASSUME_L( _glua_type(L,-2) == GLUA_TYPE_TABLE );

    STACK_CHECK(L)
        {
        _glua_pushinteger(L,i);
        _glua_insert( L, -2 );  // swap [-1] and [-2]
        _glua_totable( L );
        }
    STACK_END(-1)  // should have popped the value off stack
    //
    // [-1]= table ref
}

void _glua_tblkey_k( lua_State* L, const char* key )
{
    ASSUME_L( _glua_type(L,-2) == GLUA_TYPE_TABLE );

    STACK_CHECK(L)
        {
        _glua_pushstring(L,key);
        _glua_insert( L, -2 );  // swap [-1] and [-2]
        _glua_totable( L );
        }
    STACK_END(-1)  // should have popped the value off stack
    //
    // [-1]= table ref
}

void _glua_tblkey_ijk( lua_State* L, int i, int j, const char* key )
{
    ASSUME_L( _glua_type(L,-2) == GLUA_TYPE_TABLE );

    STACK_CHECK(L)
        {
        Loc_PushMerged_ijk( L, i, j, key );    // pushes one

        _glua_insert( L, -2 );  // swap [-1] and [-2]
        _glua_totable( L );
        }
    STACK_END(-1)  // should have popped the value off stack
    //
    // [-1]= table ref
}

//---
// Reading table contents is a common runtime thing, so we've splitted it into
// several specialized functions for speed optimization (& code clarity).
//

// String key:
//
// Note: 'key' may be NULL for member browsing (item already pushed on stack)
//       or ".subtable.path" (starting with a dot) for going into subtables of
//       such an item.
//
void _glua_usekey_k( lua_State* L, int tbl_argn, const char* key )
{
#ifndef _HAS_VA_ARGS
    ASSUME_L( tbl_argn==0 );
    tbl_argn= _getT();
#endif
    
    ASSUME_L( (tbl_argn==0) || (_glua_type(L,tbl_argn) == GLUA_TYPE_TABLE) );

    STACK_CHECK(L)
        {
        if ( key == NULL )  // Item already at top-of-stack (doing item browsing)
            {
            // just make a copy so that the original remains safe.
            //
            _glua_pushvalue( L, -1 );   // Done!
            }
        else
            {
            if ( key[0] == '.' )    // Subtable of the top-of-stack item.
                {
                // Use the 'current item' as basis, not the whole big table.
                //
                tbl_argn= _glua_gettop(L);  // abs. index of [-1]

                ASSUME_L( _glua_type(L,tbl_argn) == GLUA_TYPE_TABLE );

                key++;  // bypass the dot
                }

            // Now push an entry of the table onto the stack.
            //
            Loc_TableEntry( L, tbl_argn, NULL,NULL, key );  // pushes one
            }
        }
    STACK_END(+1)
    //
    // [-1]= table value  (to be consumed by '_glua_getXxx(0)')
}

// 1-D Arrays ('tbl[n]'):
//
void _glua_usekey_i( lua_State* L, int tbl_argn, int i )
{
#ifndef _HAS_VA_ARGS
    ASSUME_L( tbl_argn==0 );
    tbl_argn= _getT();
#endif

    ASSUME_L( (tbl_argn==0) || (_glua_type(L,tbl_argn) == GLUA_TYPE_TABLE) );

    STACK_CHECK(L)
        {
        Loc_TableEntry( L, tbl_argn, &i,NULL, NULL );  // pushes one
        }
    STACK_END(+1)
}

// 2-D Arrays ('tbl[n][m]'):
//
void _glua_usekey_ij( lua_State* L, int tbl_argn, int i, int j )
{
#ifndef _HAS_VA_ARGS
    ASSUME_L( tbl_argn==0 );
    tbl_argn= _getT();
#endif

    ASSUME_L( (tbl_argn==0) || (_glua_type(L,tbl_argn) == GLUA_TYPE_TABLE) );

    STACK_CHECK(L)
        {
        Loc_TableEntry( L, tbl_argn, &i,&j, NULL );  // pushes one
        }
    STACK_END(+1)
}

// Other cases (need merging of keys):
//
void _glua_usekey_ijk( lua_State* L, int tbl_argn, int i, int j, const char* key )
{
    ASSUME_L(key);    // otherwise, use '_glua_usekey_ij()'
    
#ifndef _HAS_VA_ARGS
    ASSUME_L( tbl_argn==0 );
    tbl_argn= _getT();
#endif

    ASSUME_L( (tbl_argn==0) || (_glua_type(L,tbl_argn) == GLUA_TYPE_TABLE) );

    STACK_CHECK(L)
        {
        Loc_TableEntry( L, tbl_argn, &i,j?(&j):NULL, key );  // always pushes one
        }
    STACK_END(+1)
    //
    // [-1]= table value  (to be consumed by '_glua_getXxx(0)')
}

//---
glua_num_t _glua_getNumber(lua_State* L, int argn_, glua_num_t def)
{
int argn= argn_ ? argn_ : -1;
glua_num_t ret;
enum e_Glua_Type type= _glua_type(L,argn);

    if (type <= GLUA_TYPE_NIL)
        return def;

    if (!(type & (GLUA_TYPE_NUMBER | GLUA_TYPE_INT)))
        _glua_badparam(L, argn_, 'N',0 );

    ret= _glua_tonumber_raw(L, argn);

    if (!argn_) _glua_settop(L,-2);   // pop away
    return ret;
}
   
long _glua_getInteger(lua_State* L, int argn_, long def)
{
int argn= argn_ ? argn_ : -1;
long ret;
enum e_Glua_Type type= _glua_type(L,argn);

    if (type <= GLUA_TYPE_NIL)
        return def;

    if (!(type & (GLUA_TYPE_INT)))
        _glua_badparam(L, argn_, 'I',0 );

    //ret= (long)_glua_tonumber_raw(L, argn);
    ret= _glua_tointeger_raw(L,argn);   // has mantissa check (if 'USE_FLOAT')

    if (!argn_) _glua_settop(L,-2);   // pop
    return ret;
}

//-----
// Note: It is important to have a separate 'getUnsigned()' function for
//       big numbers. Otherwise i.e. Gcc implicit double->int conversion
//       converts too big positive (that would still fit into uint) into
//       0x80000000. To read in the whole 0..2^32-1 range, we need this.
//
ulong _glua_getUnsigned(lua_State* L, int argn_, ulong def)
{
int argn= argn_ ? argn_ : -1;
ulong ret;
enum e_Glua_Type type= _glua_type(L,argn);

    if (type <= GLUA_TYPE_NIL)
        return def;

    if (!(type & GLUA_TYPE_INT))
        _glua_badparam(L, argn_, 'I',0 );

    ret= _glua_tounsigned_raw(L, argn);   // traps overflows if host has 'float'

    if (!argn_) _glua_settop(L,-2);   // pop
    return ret;
}

const char* _glua_getLString(lua_State* L, int argn_, size_t* len_ref, const char* def )
{
int argn= argn_ ? argn_ : -1;
const char* ret;
enum e_Glua_Type type= _glua_type(L,argn);
uint len_uint;

    if (type <= GLUA_TYPE_NIL)
        {
        //AK(24-Aug-04): Added 'len_ref' update also for default string.
        //
        if (len_ref) *len_ref= def ? strlen(def) : 0;
        return def;
        }

    if (!(type & (GLUA_TYPE_STRING | GLUA_TYPE_NUMBER | GLUA_TYPE_INT)))
        _glua_badparam(L, argn_, 'S',0 );

    // size_t/uint adaption is needed since we don't want to use 'size_t' in the
    // function table (it's width may perhaps be compiler specific?).
    //
    ret= _glua_tolstring_raw(L, argn, &len_uint);
    if (len_ref) *len_ref= (size_t)len_uint;
    
    if (!argn_) _glua_settop(L,-2); // pop
    return ret;
}

// TCHAR version:  
//
// On ANSI systems, this is just the same as normal '_glua_getLString' (above).
// On UNICODE systems (s.a. WinCE), this automatically converts to Unicode.
//
// Note: The returned 'len_ref' is always the number of TCHAR's (not bytes).
//
#ifdef UNICODE
  const WCHAR* _glua_getWLString( lua_State* L, int argn, size_t* len_ref, const char* def )
  {
  size_t len;
  const char* cstr= _glua_getLString( L, argn, &len, def );
  WCHAR* wstr= NULL;

    if (cstr)
        {
        // Allocate longer string (will be autoreleased once back in Lua)
        //
        STACK_CHECK(L)
            {
            wstr= (WCHAR*)_glua_pushuserdata_raw( L, 0 /*no tag*/, NULL, (len+1)*sizeof(WCHAR), 0 );
            _glua_settop( L, -2 );  // pop the reference (but pointer remains)
            }
        STACK_END(0)
    
        ASSUME_L(wstr);
        wsprintf( wstr, L"%hs", cstr );     // that's a handy function!
        }

    if (len_ref) *len_ref= len;

    return wstr;    // will remain available while in C
  }
#endif

void* _glua_getUserdata(lua_State* L, int argn_, tag_int tag, void* def )
{
int argn= argn_ ? argn_ : -1;
void* ret;
enum e_Glua_Type type= _glua_type2(L,argn,tag);

    if (type <= GLUA_TYPE_NIL)
        return def;

    if (type != GLUA_TYPE_USERDATA)
        _glua_badparam(L, argn_, 'U',tag );

    ret= _glua_touserdata(L, argn, NULL);

    if (!argn_) _glua_settop(L,-2); // pop
    return ret;
}

enum_int _glua_getEnum(lua_State* L, int argn_, tag_int tag, enum_int def )
{
int argn= argn_ ? argn_ : -1;
enum e_Glua_Type type= _glua_type(L,argn);

    if (type <= GLUA_TYPE_NIL)
        return def;

    // Be prepared for both 'ENUM_PATCH'ed and non-patched core
    // (patched gives 'GLUA_TYPE_ENUM', non-patched 'GLUA_TYPE_USERDATA')
    //
    if ((type == GLUA_TYPE_ENUM) || (type == GLUA_TYPE_USERDATA))
        {
        tag_int tag2;
        enum_int ret= _glua_toenum(L, argn, &tag2);
        
        if (tag2 == tag)    // same enum class?
            {
            if (!argn_) _glua_settop(L,-2); // pop
            return ret;
            }
        }

    _glua_badparam(L, argn_, 'E',tag );
    return 0;   // never
}

bool_int _glua_getBoolean(lua_State* L, int argn_, bool_int def )
{
int argn= argn_ ? argn_ : -1;
bool_int ret;

    if (_glua_gettop(L) < argn)
        return def;     //_glua_badparam(L, argn_, 'B',0 );

    ret= _glua_toboolean(L, argn);

    if (!argn_) _glua_settop(L,-2);   // pop away
    return ret;
}

int /*tbl_argn*/ _glua_getTable(lua_State* L, int argn)
{
enum e_Glua_Type type;

    ASSUME_L( argn != 0 );    // use dot notation to access subtables

    type= (enum e_Glua_Type)_glua_type(L,argn);

    return (type==GLUA_TYPE_TABLE) ? argn :
           (type==GLUA_TYPE_NIL) ? 0 /*empty*/ :
           (int)_glua_badparam(L, argn, 'T',0 );  // (never returns)
}

/*
void _glua_getDone(lua_State* L, int argn)
{
    if (_glua_gettop(L) >= argn)
        _glua_badparam(L, argn, 0,0 ); \
}
*/


/*--- Initialisation ---------------------------------------------*/

//---
// New registration function (>=2.07), using table index instead of 
// string table name.
//
// Params:  'tbl_index' > 0: absolute index of a namespace table (any name)
//                      == 0: global namespace (no table)
//
// Returns: 'true' to reset enum tag (type was other than 'const').
//
static bool_int _Loc_PushItem( lua_State* L, int (*func)(lua_State *),
                               const char* str, double num_d, tag_int etag )
{
    if (func)     	 // glua_func
        {
        _glua_pushcclosure( L, func, 0 );
        }
    else if (str)    // glua_str, glua_info, glua_wrap, ..
        {
        if (num_d==0.0) _glua_pushstring( L, str );
        else            _glua_pushlstring( L, str, (uint)num_d );
        }
    else     // glua_const
        {
        if (etag==0) _glua_pushnumber( L, (glua_num_t)num_d );    // regular constant
        else         _glua_pushenum( L, etag, (int)num_d, 0 /*mode*/ );
        
        #if 1
        if (etag)
        	{
        	enum e_Glua_Type t= _glua_type( L, -1 );
        	ASSUME_L( t == GLUA_TYPE_USERDATA );
        	}
        #endif
        
        return FALSE;   // do NOT reset the enum tag!
        }
        
    return TRUE;    // reset enum tag
}
#define LOC_PUSH_ITEM( lua, item, etag_ref ) \
    { _ASSUME_X(lua,etag_ref); \
      if (_Loc_PushItem( lua, (item).func, (item).str, (item).num_d, *(etag_ref) ) ) \
         *(etag_ref)= 0; }

//--
void _glua_register2( lua_State *L,
                     struct s_GluaItem list[], 
                     uint list_items,
                     int tbl_index )    // namespace (0 = global)
{
uint n;
bool_int pop_tbl_index= FALSE;
tag_int last_etag= 0;    // if nonzero, 'glua_const()' does userdata, not numeric

    if (!list_items) return;

    ASSUME_L( L );
    ASSUME_L( tbl_index >= 0 );   // Must be an absolute index (or 0)

    // Initialize private tag types
    {
    static bool_int virgin= TRUE;

    ASSUME_L( (sizeof(_int8_)==1) && (sizeof(_uint8_)==1) );
    ASSUME_L( (sizeof(_int16_)==2) && (sizeof(_uint16_)==2) );
    ASSUME_L( (sizeof(_int32_)==4) && (sizeof(_uint32_)==4) );

    if (virgin)
        {
        virgin= FALSE;
        STACK_CHECK(L)
          {
            lua_State *_lua_= L;  // graceful to GLUA_DECL_END
            GLUA_DECL( 0 )
              {
              glua_tag( "_intarray", &tag_intarray ),
              glua_tagmethod( tag_intarray, "index", intarray_index ),
              }
            GLUA_DECL_END   // recurses back to us (but now 'virgin'==FALSE)
          }
        STACK_END(0)
        }
    }
    
    STACK_CHECK(L)
    {
    // Make a second reference to the table (at top of stack)
    // just in case the index we got wouldn't be topmost.
    //
    if (tbl_index > 0)
        {
        _glua_pushvalue( L, tbl_index );

        ASSUME_L( _glua_type(L,-1) == GLUA_TYPE_TABLE );
        }

    for( n=0; n<list_items; n++ )
        {
		if (list[n].tag_ref)    // userdata tag, tag method or enum
			{
            if (list[n].name)  // Tag creation
                {
                int mode= (int)(list[n].num_d);     // 0= normal tag, 1=enum
                tag_int tag;

                tag= (tag_int)_glua_regtag2( L, list[n].name, (mode==1) );
                ASSUME_L(tag!=0);

				//fprintf( stderr, "registering: %s tag=%d.\n", list[n].name, (int)tag );

                if (mode==1)
                    last_etag= tag;   // remember current enum (for 'glua_const')

                // Check that the tag is unique; do allow a value to pre-exist if it is 
                // the _same_ one that we got (this dual definition happens if two wrappers
                // use the same C module. It does not hurt. :)
                //
                if (*list[n].tag_ref)   // already a definition?
                    {
                    if (*list[n].tag_ref != tag)    // must be unintentional?
                        _glua_errorN( L, "Tag listed twice: %s", list[n].name );
                    }
                else
                    *(list[n].tag_ref)= tag;
                }
            else    // tag methods (note: these don't clear 'last_etag')
                {
                tag_int tag= *(list[n].tag_ref);
                const char* event= list[n].str;
                
                if (tag==0)
                    _glua_errorN( L, "GLUA_DECL error: "
                                     "Uninitialized tag for '%s'!", event );

                if (event[0]=='_')
                    _glua_errorN( L, "GLUA_DECL error: "
                                     "Don't use underscores in method names: '%s'", event );
                                       
                _glua_settagmethod( L, tag, event, list[n].func );   // give upvalues
                }
			}
		else	// list[n].tag_ref==NULL
		if (list[n].name)
			{	// string, function, or number

			//fprintf( stderr, "%s: %s\n", last_etag ? "enum":"const", list[n].name );

			if (tbl_index==0)
				{
				// No namespace: just push the func / constant & set as global.
				//
 				LOC_PUSH_ITEM( L, list[n], &last_etag );
       			_glua_setglobal( L, list[n].name );
				}
			else
				{
				// Using namespace: push key & value onto the stack, then merge
				// with the table.
				//
   				_glua_pushstring( L, list[n].name );      // key
                LOC_PUSH_ITEM( L, list[n], &last_etag );   // value
          		_glua_totable( L );   // merges with table (pops key & val)
				}
            }
        } // for
        
    if (tbl_index > 0)  // Still need to remove the 2nd ref we pushed.
        _glua_settop( L, -2 );

    if (pop_tbl_index)
        _glua_remove( L, tbl_index );
    }
    STACK_END(0)
}

//-----
// This func is called by Gluahost at module initialization (see 'GluaModule()'
// macro in 'glua_x.h' for details). We check that the host is good enough for us.
//
int // <0 = GLUA_VERSION_ERR_..., 0=static link, >0 = host version + feature bits
	_glua_linktohost( lua_State* L, const void* v_funcs, uint funcs_size )
{
const struct s_GluaFuncs* funcs= (const struct s_GluaFuncs*)v_funcs;

int my_ver= GLUA_VERSION
#ifdef USE_FLOAT
          | GLUA_VERSION_BIT_FLOAT
#endif
#ifdef INT32_PATCH
          | GLUA_VERSION_BIT_INT32
#endif
#ifdef ENUM_PATCH
          | GLUA_VERSION_BIT_ENUM
#endif
    ;

#ifdef _TLS_UNINITIALIZED   // Win32 MSC (not WinCE)
    //
	if (tls_L == _TLS_UNINITIALIZED) tls_L= TlsAlloc();
	if (tls_T == _TLS_UNINITIALIZED) tls_T= TlsAlloc();
#endif

#ifdef GLUA_STATIC
    {
		initCriticalSections();
    ASSUME_L( funcs == NULL );
    return my_ver;	// >0
    }
#else   // Dynamic linkage    
    {
	int host_ver;

    ASSUME_L( funcs );
    _lua_gate_= funcs;
    
    // Check data type sizes used in compilation of this module (must be done
    // before passing 'funcs' to the host!)
    //
    // Note: If this happens, data alignment for the compilation of the module
    //       is probably wrong (should be 4 bytes or less).
    //
    if (!GLUAITEM_SIZECHECK())
        return GLUA_VERSION_ERR_ALIGN;

    // Check Gluahost version (may need host-side bug fixes etc.):
    //
    host_ver= _glua_ver( my_ver );   // We tell our version
                                     // ...and get host version back
    if (host_ver<0)
        return host_ver;    // immediate error (GLUA_VERSION_ERR_..)

    if ( (host_ver & GLUA_VERSION_YYMMDD_MASK) < HOST_VERSION_REQ )
        return GLUA_VERSION_ERR_OLDHOST;  // Too old host for us!
    
    // Final check - make sure all function entries we need are non-NULL.
    {
    const void** ptr_tbl= (const void**)funcs;
    uint i;
    
    for( i=0; i< (sizeof(struct s_GluaFuncs) / sizeof(void*)); i++ )
        {
        if (ptr_tbl[i] == NULL)
            return GLUA_VERSION_ERR_NOFUNC;  // Some entry is NULL! (bug in the host)
        }
    }
    
    // Good for us, but allow for module specific extra checks (i.e. refusing
    // to work with 'float' based host).
    //
	return host_ver;
	}
#endif    
}


int gluaObjFuncDispatch(lua_State *_lua_, tag_int tag, GluaObjFuncEntry *entries, int count, int bSetTable)
{
	void *userData     = glua_getUserdata(1, tag);
	const char *member = glua_getString(2);

	if(userData && member)
	{
		int i;
		for(i=0; i<count; i++)
		{
			if(!strcmp(member, entries[i].name))
			{
				switch(entries[i].type)
				{
				case GLUAOBJFUNCENTRYTYPE_FUNCTION:
					{
						lua_pushcclosure(_lua_, entries[i].funcPtr, 0);
						return 1;
					}
				case GLUAOBJFUNCENTRYTYPE_VARIABLE:
					{
						if(bSetTable)
							return entries[i].setFuncPtr(_lua_, userData, member);
						else
							return entries[i].getFuncPtr(_lua_, userData, member);
					}
				}
			}
		}
	}

	// errorf ?
	_glua_pushnil(_lua_);
	return 1;
}

GLUA_VAR_FUNC(GLUA_VAR_READONLY)
{
	glua_pushNil();
}
GLUA_END

GLUA_VAR_FUNC(GLUA_VAR_WRITEONLY)
{
	glua_pushNil();
}
GLUA_END
