// Now with a bunch of changes by Jimb Esser to handle DX9/DX11 interop

/*

This is RAD's high level API for using 3D hardware to do color conversion.
It is supported on PS3, Xbox, Xbox 360, Wii, Windows and GameCube.

It's a simple API, so you should see your platform's example code
to see all the nitty-gritty details.


There are three main cross platform functions:

  Create_Bink_textures:  This function takes a BINKTEXTURESET structure and
    creates the texture resources to render it quickly.
  
  Free_Bink_textures:  Frees the resources allocated in Create_Bink_textures.

  Draw_Bink_textures:  Renders the textures onto the screen.


There are also a few platform specific functions:

  Wait_for_Bink_textures:  On Wii, Xbox, Xbox 360 and PS3, this function 
    waits for the GPU to finish using the given texture set. Call
    before BinkOpen.


  Create_Bink_shaders:  On PS3, Xbox 360 and Windows, this function creates
    the pixel shaders we use to do the color conversion. Call this function
    before the first call to Create_Bink_textures.

  Free_Bink_shaders:  Frees the pixel shaders created in Create_Bink_shaders.


  Lock_Bink_textures:  On Windows, locks the textures so that BinkDoFrame can
    decompress into them.

  Unlock_Bink_textures:  On Windows, unlocks the textures after BinkDoFrame.


So, basically, playback works like this:

  1) Create the pixel shaders on the platforms that need it (PS3, Xbox 360, Win).
  
  2) Open the Bink file with the BINKNOFRAMEBUFFERS flag.  We will use our API
     to create the frame buffers that Bink will use.

  3) Call BinkGetFrameBuffersInfo to get the details on the frame buffers 
     that we need.

  4) Call Create_Bink_textures to create the textures.

  5) Call BinkRegisterFrameBuffers to register these new textures with Bink.

  6) Call Wait_for_Bink_textures before BinkDoFrame (or Lock_Bink_textures 
     on Windows).

  7) Call BinkDoFrame to decompress a video frame.

  8) Draw the frame using Draw_Bink_textures.


And that's it! (Skipping over a few details - see the examples for all 
the details...)

Should drop in really quickly and it hides a ton of platform specific ugliness!

*/

#ifndef BINKTEXTURESH
#define BINKTEXTURESH


#include "bink.h"

typedef struct BINKFRAMETEXTURES9
{
  U32 Ysize;
  U32 cRsize;
  U32 cBsize;
  U32 Asize;

    // xenon and windows use dx9
    LPDIRECT3DTEXTURE9 Ytexture;
    LPDIRECT3DTEXTURE9 cRtexture;
    LPDIRECT3DTEXTURE9 cBtexture;
    LPDIRECT3DTEXTURE9 Atexture;
} BINKFRAMETEXTURES9;


typedef struct BINKFRAMETEXTURES11
{
	U32 Ysize;
	U32 cRsize;
	U32 cBsize;
	U32 Asize;

	ID3D11Texture2D * Ytexture;
	ID3D11Texture2D * cRtexture;
	ID3D11Texture2D * cBtexture;
	ID3D11Texture2D * Atexture;

} BINKFRAMETEXTURES11;




typedef struct BINKTEXTURESET
{
	// this is the Bink info on the textures
	BINKFRAMEBUFFERS bink_buffers;

	// this is the GPU info for the textures
	BINKFRAMETEXTURES9 textures9[ BINKMAXFRAMEBUFFERS ];
	BINKFRAMETEXTURES9 tex_draw9;

	// this is the GPU info for the textures
	BINKFRAMETEXTURES11 textures11[ BINKMAXFRAMEBUFFERS ];
	BINKFRAMETEXTURES11 tex_draw11;

	ID3D11DeviceContext *d3d_context;

	ID3D11Buffer * vert_buf;
	ID3D11Buffer * const_buf;

	ID3D11ShaderResourceView * Yview;
	ID3D11ShaderResourceView * cBview;
	ID3D11ShaderResourceView * cRview;
	ID3D11ShaderResourceView * Aview;
} BINKTEXTURESET;

//=============================================================================

//
// allocate the textures that we'll need
//
RADDEFFUNC S32 Create_Bink_textures9( LPDIRECT3DDEVICE9 d3d_device,
                                     BINKTEXTURESET * set_textures );
RADDEFFUNC S32 Create_Bink_textures11( ID3D11Device *d3d_device, ID3D11DeviceContext *d3d_context,
									BINKTEXTURESET * set_textures );

// frees the textures
RADDEFFUNC void Free_Bink_textures9(LPDIRECT3DDEVICE9 d3d_device,
                                    BINKTEXTURESET * set_textures );
RADDEFFUNC void Free_Bink_textures11(BINKTEXTURESET * set_textures );

// draws the textures with D3D
RADDEFFUNC void Draw_Bink_textures9( LPDIRECT3DDEVICE9 d3d_device,
                                    BINKTEXTURESET * set_textures,
                                    U32 width,
                                    U32 height,
                                    F32 x_offset,
                                    F32 y_offset,
                                    F32 x_scale,
                                    F32 y_scale,
                                    F32 alpha_level,
                                    S32 is_premultiplied_alpha );

RADDEFFUNC void Draw_Bink_textures11( BINKTEXTURESET * set_textures,
                                    U32 width,
                                    U32 height,
                                    F32 x_offset,
                                    F32 y_offset,
                                    F32 x_scale,
                                    F32 y_scale,
                                    F32 alpha_level,
                                    S32 is_premultiplied_alpha );

//=============================================================================



  // On Windows, we need to use lock and unlock semantics for best performance

  // Lock the textures for use by D3D - use lock_both_for_writing when using BinkGoto
RADDEFFUNC void Lock_Bink_textures9( BINKTEXTURESET * set_textures, S32 lock_both_for_writing );
RADDEFFUNC void Lock_Bink_textures11( BINKTEXTURESET * set_textures, S32 lock_both_for_writing );

  // Unlock the textures after rendering
  RADDEFFUNC void Unlock_Bink_textures9(LPDIRECT3DDEVICE9 d3d_device, BINKTEXTURESET * set_textures, HBINK Bink );
  RADDEFFUNC void Unlock_Bink_textures11( BINKTEXTURESET * set_textures, HBINK Bink );

//=============================================================================

  // creates the couple of shaders that we use

  RADDEFFUNC S32 Create_Bink_shaders9(LPDIRECT3DDEVICE9 d3d_device);
  RADDEFFUNC S32 Create_Bink_shaders11(ID3D11Device *d3d_device);

  // free our shaders
  RADDEFFUNC void Free_Bink_shaders9( void );
  RADDEFFUNC void Free_Bink_shaders11( void );

//=============================================================================

#endif
