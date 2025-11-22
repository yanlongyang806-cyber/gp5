//---
// GLUAX_50.C		                Copyright (c) 2002-05, Asko Kauppi
//
// Code for STATICALLY compiling GluaX using the Lua 5.0 engine (or newer).
// You do not need this code for DYNAMICALLY compiled modules.
//
// License:
//    Zlib/Lua4 (see license.txt)
//
// Notes:
//      ...
//---

#define LUA_CORE        // (enable that section)
#include "luaconf.h"    // lua_str2number()  -- must be before 'gluax.h'

#ifndef lua_str2number
  #error "Failed to read in 'luaconf.h'"
#endif

#include "gluax.h"

#ifdef GLUA_DYNAMIC
  #error "You do not need this source for dynamic GluaX compilations."
#endif

#include <stdio.h>  // fprintf()

typedef unsigned short uint16;

//---
// The magic word is only used for making sure we don't step on any non-gluax
// userdata's toes.. There shouldn't even be such in the system, though.
//
#define UD_MAGIC_WORD  0xa0a0

struct s_Prelude   // shared by both Userdata & Enums (just after Lua-internal fields)
{
  #ifdef UD_MAGIC_WORD
    uint16 magic;
  #endif
    uint16 tag16;	// saves space (we shouldn't need more than 65000 types?)

	void* ptr;	    // application-provided pointer (if any) or 'enum_int' (casted)
};

#ifdef UD_MAGIC_WORD
  #define APPROVE_UD(ud)  ASSUME_L( (ud)->magic == UD_MAGIC_WORD )
#else
  #define APPROVE_UD(ud)  /*no check*/
#endif

//---
// Return pointer to the application's real userdata block.
//
static void* Loc_AppUserdata( struct s_Prelude* ud )
{
	return (ud->ptr) ? ud->ptr
	                 : ((uint8*)ud) + sizeof(*ud);
}

//---
// Pushes a reference to the tag type's 'metatable', creating
// such if not already there.
//
bool8   // pushed (TRUE/FALSE)
Loc50_PushMetatableRef( lua_State* L, tag_int tag, bool8* create_if_none )
{
bool8 pushed= TRUE;
bool8 created= FALSE;

	STACK_CHECK(L);
		{
		// Get 'registry["glua_meta_xx"]':
		//
		if (tag==0)   // use a common meta table
            {
            _glua_pushliteral( L, "glua_meta_def" );    // faster
            }
        else
            {
            _glua_pushliteral( L, "glua_meta_" );
            _glua_pushinteger( L, (int)tag );
            _glua_concat( L, 2 );
            }

		lua_pushvalue( L, -1 );	 // copy of the key (eaten by 'gettable')

		lua_gettable( L, LUA_REGISTRYINDEX );
        //
        // [-2]= "glua_meta_<tag>" string
        // [-1]= registry entry (or nil)

		if (lua_istable( L, -1 ))
			lua_remove( L, -2 );	// don't need 2nd key
		else
		if (create_if_none && *create_if_none)
			{
			// Was not yet there, create an empty table.
            //
            created= TRUE;

			lua_remove( L, -1 );	// remove the 'nil'

			// [-1]= key name to the registry 

			lua_newtable( L );	// value

			// make copy of value (2nd ref) to [-3]:
			//
			lua_pushvalue( L, -1 );
			lua_insert( L, -3 );

            // [-3]= 2nd ref to [-1] table
            // [-2]= key name
            // [-1]= empty table

			lua_settable( L, LUA_REGISTRYINDEX );	// eats key + value

			// [-1]= 2nd ref to the created empty table
			}
		else	// don't force create
			{
			lua_pop( L, 2 );    // eat 'nil' & copy of the key
                                // (pushes none to the caller)
			pushed= FALSE;
			}
		}
	STACK_END( pushed ? +1:0 )

    if (create_if_none)    // tell caller if we created or not
        *create_if_none= created;

	return pushed;
}


//---
// Events:	"add"
//			"sub"
//			"mul"
//			"div"
//			"pow"
//			"unm"
//          "eq"
//			"lt"
//			"le"		(not in lua 4.0!)
//			"concat"
//			"index"		("gettable" in lua 4.0)
//			"newindex"	("settable" in lua 4.0)
//			"call"		("function" in lua 4.0)
//			"gc"
//
// Note: To help 'gluaport', we get the function from Lua stack (-1)
//		 and (optionally) push the old value back to the stack.
//
// Note2: Do NOT convert stack entries to C function pointers! The 
//       stack entry may be a 'closure' (function + upvalue).
//
// Returns: TRUE if a new metatable was initiated for this method.
//          FALSE if a metatable already existed.
//
bool8 Loc50_SetTagMethod( lua_State* L, 
                         tag_int tag,
                         const char* event,
						 bool8 push_old_func )
{
bool8 create= TRUE;

	ASSUME_L( lua_type(L,-1) == LUA_TFUNCTION );

	STACK_CHECK(L)
		{
		// Creates a new metatable if not already there:
		//
		Loc50_PushMetatableRef( L, tag, &create );  // sets/resets 'create'

		// push the event prefixed with "__":
		lua_pushliteral( L, "__" );
		lua_pushstring( L, event );		// key
		lua_concat( L, 2 );

		// [-3]= new function
		// [-2]= table ref
		// [-1]= key name

		if (push_old_func)
			{
			lua_pushvalue( L, -1 );	 // copy of key name
			lua_gettable( L, -3 );	 // pops key, pushes old func

			// [-4]= new function
			// [-3]= table ref
			// [-2]= key
			// [-1]= old func

			// Now move the old func below other stuff:
			lua_insert( L, -4 );

			// [-4]= old function
			// [-3]= new function
			// [-2]= table ref
			// [-1]= key name 
			}

		// Copy func to top of stack:
		lua_pushvalue( L, -3 );
		lua_remove( L, -4 );        
	
		// ([-4]= old function)
		// [-3]= table ref
		// [-2]= key name 
		// [-1]= new function

		lua_settable( L, -3 );	// (eats key + value)

		lua_remove( L, -1 );	// remove the table reference

		// ([-1]= old function)   (if 'push_old_func'==TRUE)
		}
	STACK_END( push_old_func ? 0:-1 )

    return create;  // TRUE/FALSE
}


//---
static uint8* Loc_CacheLookup( uint tag )
{
#define CACHE_VALUES 300    // This is no limit, also > values will work (unoptimized)
static uint8 cache_table[CACHE_VALUES]= {0};

    if (tag < CACHE_VALUES)
        return &cache_table[tag];   // optimization slot
    else
        return NULL;    // we can't help, do unoptimized
}

//---
// Attach a metatable to a given userdata or table.
//
static void Loc_SetMetaTable( lua_State* L, int rel_index /*ud|tbl*/, tag_int tag )
{
int abs_index= STACK_ABS( L, rel_index );
int tmp;

	STACK_CHECK(L)
		{
		// Optimization: If it is already known that a tag type would use
		//               the default table, skip looking for others.
		//
		// Multithreading: There is no critical section here, really. If two
		//            threads were to come in with the same tag, they could
		//            (at the worst case) do the 'unknown state' preparation
		//            twice, but they'd end up writing the same value to the
		//            table. So, rest in peace.. :)
		//
        //#define CACHE_STATE_UNKNOWN 0
        #define CACHE_STATE_COMMON  1
        #define CACHE_STATE_CUSTOM  2

        uint8* cache_entry= Loc_CacheLookup((uint)tag);
        uint8 state= cache_entry ? *cache_entry : 0;
        bool8 ok;
        
// BUG: Linux (Ubuntu) had problems with the caching, happens when pressing
//      'ESC' in the SDL demo.  disabled for now...  TBD!!!
//
        //if (state==0)
        if (TRUE)
            {
            // 1st: Try to find metatable specifically for this tag type.
            // 2nd: Use the common ('tag 0') metatable.
            //
            state= CACHE_STATE_CUSTOM;   // found there?
            
            if (!Loc50_PushMetatableRef( L, tag, NULL /*don't create*/ ))
                {
                bool8 create= TRUE;
                ok= Loc50_PushMetatableRef( L, 0, &create );  // create if none yet
                ASSUME_L(ok);
                state= CACHE_STATE_COMMON;
                }
                
            if (cache_entry)
                *cache_entry= state;    // now we know :)
            }
        else    // state is known
            {
            ok= Loc50_PushMetatableRef( L, state==CACHE_STATE_CUSTOM ? tag:0, NULL );
            ASSUME_L(ok);   // should exist
            }                

		tmp= lua_setmetatable( L, abs_index );
        ASSUME_L( tmp!=0 );
		}
	STACK_END(0)
}


//---
// Optimized version for enumeration constants (very simple userdata).
//
// Note: This is being used a _lot_ (for intermediate enums in bitwise ops etc.)
//       so performance really counts!
//
// 'mode': 0= works always (but slow), fetches metatable from registry lookup.
//         1= metatable at first upvalue
//        (2= metatable at top of stack (eat it)  NOT USED!!)
//
// Enums have 'size==-1', which enables some optimizations.
//
void*
Loc50_PushUserdataExt( lua_State* L, const void* ptr, int size, tag_int tag, uint8 mode )
{
struct s_Prelude* ud;
int tmp;
		
    STACK_CHECK(L)
        {
        // Let Lua allocate its data tables for the userdata and also request 
        // space for our 'prelude' and (optional) application block.
        //
        ud= (struct s_Prelude*)lua_newuserdata( L, sizeof(*ud) + size );
    	ASSUME_L( ud );

        #ifdef UD_MAGIC_WORD
          ud->magic= UD_MAGIC_WORD;    // maybe unnecessary?
        #endif

        ASSUME_L( (uint)tag <= 0xffff );  // fits in 16 bits

        ud->tag16= (uint16)(uint)tag;
    	ud->ptr= (size>0) ? NULL : (void*)ptr;

        // Associate metatable with the userdata
        //
        switch( mode )
            {
            case _PUSHUD_MODE_REG:   // 0: metatable unknown, get it from registry
                {
                if (size<0)  // enum
                    {
                    bool8 ok= Loc50_PushMetatableRef( L, tag, NULL );   // enums must have a metatable
                    ASSUME_L(ok);
            
                    ASSUME_L( lua_type(L,-2) == LUA_TUSERDATA );
                    ASSUME_L( lua_type(L,-1) == LUA_TTABLE );
        
                    tmp= lua_setmetatable( L, -2 );   // pops metatable from stack
                    ASSUME_L( tmp != 0 );   // 0 means setting failed!
                    }
                else    // 'real' userdata
                    {
	                Loc_SetMetaTable( L, -1, tag );
                    }
                }
                break;

            case _PUSHUD_MODE_UPV:    // 1: metatable as upvalue
                {
	            lua_pushvalue( L, lua_upvalueindex(1) );   // upvalue #1 (not that we had any others.. :)

                ASSUME_L( _glua_type(L,-1) == GLUA_TYPE_TABLE );

                tmp= lua_setmetatable( L, -2 );     // eats the metatable
                ASSUME_L(tmp!=0);
                }
                break;

/** not used?
            case _PUSHUD_MODE_TOS:    // 2: metatable at top-of-stack (now [-2])
                {
                lua_insert(L,-2);   // [-2]= userdata
                                    // [-1]= metatable
    
                ASSUME_L( _glua_type(L,-1) == GLUA_TYPE_TABLE );
                
                tmp= lua_setmetatable( L, -2 );     // eats the metatable
                ASSUME_L(tmp!=0);
                }
                break;
**/
        
            default:
                ASSUME_L(FALSE);      // bad mode
                
            } // switch(mode)
        }
    //STACK_END( (mode==_PUSHENUM_MODE_TOS) ? 0:+1 )
    STACK_END( +1 )

    return (size<0) ? NULL : Loc_AppUserdata( ud );
}

//----
void* Loc50_ToUserdata( lua_State* L, int argn, tag_int* tagref )
{
struct s_Prelude* ud;
	
    ud= (struct s_Prelude*)lua_touserdata( L, argn );
    if (!ud) return NULL;
    
    APPROVE_UD( ud );
    
    if (tagref) *tagref= (tag_int)(uint)(ud->tag16);

	return Loc_AppUserdata(ud);
}

#ifndef ENUM_PATCH
  //----
  enum_int Loc50_ToEnum( lua_State* L, int argn, tag_int* tagref )
  {
  struct s_Prelude* ud;
	
    ud= (struct s_Prelude*)lua_touserdata( L, argn );
    ASSUME_L(ud);
    
    APPROVE_UD(ud);
    
    if (tagref) *tagref= (tag_int)(uint)(ud->tag16);

	return (enum_int)(ud->ptr);   // may be zero
  }

  //----
  // Modify an existing enum's 'mask' bits. 
  //
  // Note: With 'mask'==0xffffffff this can be used to reset whole value.
  //
  static CRITICAL_SECTION _glua_modenum_cs;

  void Loc50_InitCs(void) { CRITICAL_SECTION_INIT(&_glua_modenum_cs); }

  void Loc50_ModEnum( lua_State* L, int argn, enum_int val, uint mask )
  {
  struct s_Prelude* ud;
	
    ud= (struct s_Prelude*)lua_touserdata( L, argn );
    ASSUME_L(ud);
    
    APPROVE_UD(ud);

    CRITICAL_SECTION_START(&_glua_modenum_cs)
        {
        enum_int a= (enum_int)(ud->ptr);

        a &= ~mask;         // clear masked bits
        a |= val & mask;    // set new ones
    
        ud->ptr= (void*)a;
        }	
	CRITICAL_SECTION_END
  }
#endif  // ENUM_PATCH


//---
void Loc50_SetTag( lua_State* L, int argn, tag_int tag )
{

    switch( lua_type(L,argn) )
        {
        case LUA_TTABLE:
            Loc_SetMetaTable( L, argn, tag );
            break;
        
        case LUA_TLIGHTUSERDATA:
        case LUA_TUSERDATA:
            {
            struct s_Prelude* ud=
                (struct s_Prelude*)lua_touserdata( L, argn );

            ASSUME_L(ud);
            APPROVE_UD(ud);

            Loc_SetMetaTable( L, argn, tag );

            ud->tag16= (uint16)(uint)tag;
            }
            break;
        
        default:
            ASSUME_L(FALSE);  // Cannot set tag for other types!
            break;
        }
}


//---
// Modify '[argn]' in location, converting numeric strings to
// actual numbers (and leaving other strings alone).
//
glua_num_t Loc50_ConvToNumber( lua_State* L, int argn )
{
int argn_abs= STACK_ABS(L,argn);
glua_num_t val= GLUA_NUM_ZERO;	// default (for non-convertible types)

	STACK_CHECK(L)
		{
		switch( lua_type(L, argn) )
			{
			case LUA_TSTRING:
                {
                char* end;

                // This is 'strtod' or 'strtof' (see 'luaconf.h')
                //
                lua_str2number( lua_tostring(L,argn), & end );

                if (*end == '\0')   // was numeric?
					{
					val= lua_tonumber(L,argn);

					lua_pushnumber( L, val );
					lua_replace(L, argn_abs );
					}
                }
				break;

			case LUA_TNUMBER:
				// No conversion necessary, already a number.
				val= lua_tonumber(L,argn);
				break;

			default:	// Non-convertible types (let them be)
				break;
			}
		}
	STACK_END(0)

	return val;
}

