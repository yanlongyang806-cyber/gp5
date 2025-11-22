// Now with a bunch of changes by Jimb Esser to handle DX9/DX11 interop

#include "RdrFMV.h"
#if ENABLE_BINK
#include <d3d11.h>
#include <d3d9.h>
#include <D3Dcompiler.h>
#include "binktextures.h"


//
// define what type of sampling to use - usually use bilinear, but 
//   if you are using a software renderer, point sampling is faster.
//   

#define SAMPLING_TYPE D3D11_FILTER_MIN_MAG_MIP_LINEAR 
//#define SAMPLING_TYPE D3D11_FILTER_MIN_MAG_MIP_POINT 


//
// pointers to our local vertex and pixel shader
//

static ID3D11PixelShader  * YCrCbToRGBNoPixelAlpha = 0;
static ID3D11PixelShader  * YCrCbAToRGBA = 0;
static ID3D11InputLayout  * vertex_decl = 0;
static ID3D11VertexShader * PositionAndTexCoordPassThrough = 0;
static ID3D11BlendState * alpha_pm_state = 0;
static ID3D11BlendState * alpha_state = 0;
static ID3D11SamplerState * sampler_state = 0;
static ID3D11RasterizerState * rasterizer_state = 0;
static ID3D11DepthStencilState * depth_stencil_state = 0;


typedef struct POS_TC_VERTEX
{
  F32 sx, sy, sz, rhw;  // Screen coordinates
  F32 tu, tv;           // Texture coordinates
} POS_TC_VERTEX;


//
// simple pass through vertex shader
//

static CONST CHAR StrPositionAndTexCoordPassThrough[] = 
"\
struct VS_IN_DATA\
{\
  float4 Pos : POSITION;\
  float2 T0 : TEXCOORD0;\
};\
\
struct VS_OUT_DATA\
{\
  float4 Pos : SV_Position;\
  float2 T0: TEXCOORD0;\
};\
\
VS_OUT_DATA main( VS_IN_DATA In )\
{\
  VS_OUT_DATA Out; \
  Out.Pos = In.Pos; \
  Out.T0 = In.T0; \
  return Out;\
}\
";

//
// simple pixel shader to apply the yuvtorgb matrix
//

static const char StrYCrCbToRGBNoPixelAlpha[] =
"\
Texture2D tex0 : register( t0 );\
Texture2D tex1 : register( t1 );\
Texture2D tex2 : register( t2 );\
sampler samp0  : register( s0 );\
float4  consta : register( c0 );\
\
\
struct VS_OUT\
{\
  float4 Pos : SV_Position;\
  float2 T0: TEXCOORD0;\
};\
\
\
float4 main( VS_OUT In ) : SV_Target\
{\
  const float4 crc = { 1.595794678f, -0.813476563f, 0, 0.0 };\
  const float4 crb = { 0, -0.391448975f, 2.017822266f, 0.0 };\
  const float4 adj = { -0.87065506f, 0.529705048f, -1.081668854f, 0 };\
  float4 p;\
\
  float y = tex0.Sample( samp0, In.T0 ).a;\
  float cr = tex1.Sample( samp0, In.T0 ).a;\
  float cb = tex2.Sample( samp0, In.T0 ).a;\
\
  p = y * 1.164123535f;\
\
  p += (crc * cr) + (crb * cb) + adj;\
\
  p.w = 1.0;\
  p *= consta;\
  return p;\
}\
";


//
// simple pixel shader to apply the yuvtorgb matrix with alpha
//

static const char StrYCrCbAToRGBA[] =
"\
Texture2D tex0 : register( t0 );\
Texture2D tex1 : register( t1 );\
Texture2D tex2 : register( t2 );\
Texture2D tex3 : register( t3 );\
sampler samp0  : register( s0 );\
float4  consta : register( c0 );\
\
\
struct VS_OUT\
{\
  float4 Pos : SV_Position;\
  float2 T0: TEXCOORD0;\
};\
\
\
float4 main( VS_OUT In ) : SV_Target\
{\
  const float4 crc = { 1.595794678f, -0.813476563f, 0, 0.0 };\
  const float4 crb = { 0, -0.391448975f, 2.017822266f, 0.0 };\
  const float4 adj = { -0.87065506f, 0.529705048f, -1.081668854f, 0 };\
  float4 p;\
\
  float y = tex0.Sample( samp0, In.T0 ).a;\
  float cr = tex1.Sample( samp0, In.T0 ).a;\
  float cb = tex2.Sample( samp0, In.T0 ).a;\
  float a = tex3.Sample( samp0, In.T0 ).a;\
\
  p = y * 1.164123535f;\
\
  p += (crc * cr) + (crb * cb) + adj;\
\
  p.w = a;\
  p *= consta;\
  return p;\
}\
";




static D3D11_INPUT_ELEMENT_DESC vertex_def[] =
{
  { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
  { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};


//############################################################################
//##                                                                        ##
//## Free the shaders that we use.                                          ##
//##                                                                        ##
//############################################################################

void Free_Bink_shaders11( void )
{
  if ( vertex_decl )
  {
    vertex_decl->Release();
    vertex_decl = 0;
  }

  if ( PositionAndTexCoordPassThrough )
  {
    PositionAndTexCoordPassThrough->Release();
    PositionAndTexCoordPassThrough = 0;
  }

  if ( YCrCbToRGBNoPixelAlpha )
  {
    YCrCbToRGBNoPixelAlpha->Release();
    YCrCbToRGBNoPixelAlpha = 0;
  }

  if ( YCrCbAToRGBA )
  {
    YCrCbAToRGBA->Release();
    YCrCbAToRGBA = 0;
  }

  if ( alpha_pm_state )
  {
    alpha_pm_state->Release();
    alpha_pm_state = 0;
  }
      
  if ( alpha_state )
  {
    alpha_state->Release();
    alpha_state = 0;
  }
      
  if ( sampler_state )
  {
    sampler_state->Release();
    sampler_state = 0;
  }
      
  if ( rasterizer_state )
  {
    rasterizer_state->Release();
    rasterizer_state = 0;
  }
      
  if ( depth_stencil_state )
  {
    depth_stencil_state->Release();
    depth_stencil_state = 0;
  }
}


//############################################################################
//##                                                                        ##
//## Create the three shaders that we use.                                  ##
//##                                                                        ##
//############################################################################

S32 Create_Bink_shaders11( ID3D11Device * d3d_device )
{
  HRESULT hr;
  ID3D10Blob * buffer = 0;

  //
  // create a vertex shader that just passes the vertices straight through
  //
  
  if ( PositionAndTexCoordPassThrough == 0 )
  {
    hr = D3DCompile( StrPositionAndTexCoordPassThrough, sizeof( StrPositionAndTexCoordPassThrough ),
                            0, 0, 0, "main", "vs_4_0", D3D10_SHADER_ENABLE_STRICTNESS, 0, &buffer, 0 );
    if ( FAILED( hr ) )
      goto fail;
      
    if ( FAILED( d3d_device->CreateVertexShader( (DWORD*) buffer->GetBufferPointer(), buffer->GetBufferSize(), NULL, &PositionAndTexCoordPassThrough ) ) )
    {
      buffer->Release();
      goto fail;
    }

    //
    // Define the vertex buffer layout
    //

    if ( vertex_decl == 0 )
    {
      hr = d3d_device->CreateInputLayout( vertex_def, 2, buffer->GetBufferPointer(), buffer->GetBufferSize(), &vertex_decl );
      buffer->Release();

      if ( FAILED( hr ) )
        goto fail;
    }
  }
  
  //
  // create a pixel shader that goes from YcRcB to RGB (without alpha)
  //

  if ( YCrCbToRGBNoPixelAlpha == 0 )
  {
    hr = D3DCompile( StrYCrCbToRGBNoPixelAlpha, sizeof( StrYCrCbToRGBNoPixelAlpha ),
                            0, 0, 0, "main", "ps_4_0", D3D10_SHADER_ENABLE_STRICTNESS, 0, &buffer, 0 );
    if ( FAILED( hr ) )
      goto fail;

    hr = d3d_device->CreatePixelShader( (DWORD*) buffer->GetBufferPointer(), buffer->GetBufferSize(), NULL, &YCrCbToRGBNoPixelAlpha );
    buffer->Release();

    if ( FAILED( hr ) )
      goto fail;
  }

  //
  // create a pixel shader that goes from YcRcB to RGB with an alpha plane
  //

  if ( YCrCbAToRGBA == 0 )
  {
    hr = D3DCompile( StrYCrCbAToRGBA, sizeof( StrYCrCbAToRGBA ),
                            0, 0, 0, "main", "ps_4_0", D3D10_SHADER_ENABLE_STRICTNESS, 0, &buffer, 0 );
    if ( FAILED( hr ) )
      goto fail;

    hr = d3d_device->CreatePixelShader( (DWORD*) buffer->GetBufferPointer(), buffer->GetBufferSize(), NULL, &YCrCbAToRGBA );
    buffer->Release();

    if ( FAILED( hr ) )
      goto fail;
  }

  // create our blend state - gad, this is crazy
  if ( alpha_pm_state == 0 )
  {
	D3D11_BLEND_DESC bd = {0};
    
    bd.AlphaToCoverageEnable = 0;
	bd.RenderTarget[0].BlendEnable = 1;
	bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    
    if ( FAILED( d3d_device->CreateBlendState( &bd, &alpha_pm_state ) ) )
      goto fail;
  
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
      
    if ( FAILED( d3d_device->CreateBlendState( &bd, &alpha_state ) ) )
      goto fail;
  }


  // create the sampler state
  if ( sampler_state == 0 )
  {
	D3D11_SAMPLER_DESC sd = {(D3D11_FILTER)0};
    
    sd.Filter = SAMPLING_TYPE;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MinLOD = 0.0f;
    sd.MaxLOD = 3.4e+38f;
    sd.MipLODBias = 0.0f;
    sd.MaxAnisotropy = 16;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.BorderColor[ 0 ] = 0.0f; 
    sd.BorderColor[ 1 ] = 0.0f; 
    sd.BorderColor[ 2 ] = 0.0f; 
    sd.BorderColor[ 3 ] = 0.0f; 
    
    if ( FAILED( d3d_device->CreateSamplerState( &sd, &sampler_state ) ) )
      goto fail;
  }
  
  
  // create the rasterizer state - it never ends!
  if ( rasterizer_state == 0 )
  {  
	D3D11_RASTERIZER_DESC rd = {(D3D11_FILL_MODE)0};
    
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.FrontCounterClockwise = 0;
    rd.DepthBias = 0;
    rd.DepthBiasClamp = 0.0f;
    rd.SlopeScaledDepthBias = 0.0f;
    rd.DepthClipEnable = 0;
    rd.ScissorEnable = 0;
    rd.MultisampleEnable = 0;
    rd.AntialiasedLineEnable = 0;
    
    if ( FAILED( d3d_device->CreateRasterizerState( &rd, &rasterizer_state ) ) )
      goto fail;
  }
  
  
  // create depth/stencil state - omg!
  if ( depth_stencil_state == 0 )
  {  
	D3D11_DEPTH_STENCIL_DESC dsd = {0};
    
    dsd.DepthEnable = 0;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    dsd.StencilEnable = 0;
    dsd.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
    dsd.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
    dsd.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    dsd.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    dsd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    dsd.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    dsd.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    dsd.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    dsd.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    dsd.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    
    if ( FAILED( d3d_device->CreateDepthStencilState( &dsd, &depth_stencil_state ) ) )
      goto fail;
  }

  return( 1 );
  
 fail:
  Free_Bink_shaders11( );
  return( 0 );
}


//############################################################################
//##                                                                        ##
//## Free the textures that we allocated.                                   ##
//##                                                                        ##
//############################################################################

void Free_Bink_textures11( BINKTEXTURESET * set_textures )
{
  BINKFRAMETEXTURES11 * abt[] = { &set_textures->textures11[ 0 ], &set_textures->textures11[ 1 ], &set_textures->tex_draw11 };
  BINKFRAMETEXTURES11 * bt;
  int i;

  // free the vertex buffer
  if ( set_textures->vert_buf )
  {
    set_textures->vert_buf->Release();
    set_textures->vert_buf = 0;
  }

  // free the constant buffer
  if ( set_textures->const_buf )
  {
    set_textures->const_buf->Release();
    set_textures->const_buf = 0;
  }

  // Free the texture memory and then the textures directly
  for ( i = 0; i < sizeof( abt )/sizeof( *abt ); ++i )
  {
    bt = abt[ i ];
    if ( bt->Ytexture )
    {
      bt->Ytexture->Release();
      bt->Ytexture = NULL;
    }
    if ( bt->cRtexture )
    {
      bt->cRtexture->Release();
      bt->cRtexture = NULL;
    }
    if ( bt->cBtexture )
    {
      bt->cBtexture->Release();
      bt->cBtexture = NULL;
    }
    if ( bt->Atexture )
    {
      bt->Atexture->Release();
      bt->Atexture = NULL;
    }
  }

  if ( set_textures->Yview )
  {
    set_textures->Yview->Release();
    set_textures->Yview = 0;
  }
  if ( set_textures->cRview )
  {
    set_textures->cRview->Release();
    set_textures->cRview = 0;
  }
  if ( set_textures->cBview )
  {
    set_textures->cBview->Release();
    set_textures->cBview = 0;
  }
  if ( set_textures->Aview )
  {
    set_textures->Aview->Release();
    set_textures->Aview = 0;
  }
}


//############################################################################
//##                                                                        ##
//## Create a texture while allocating the memory ourselves.                ##
//##                                                                        ##
//############################################################################

static S32 make_texture( ID3D11Device * d3d_device, ID3D11DeviceContext *d3d_context,
                         U32 width, U32 height, S32 system, DXGI_FORMAT format, U32 pixel_size,
                         ID3D11Texture2D ** out_texture, void ** out_ptr, U32 * out_pitch, U32 * out_size,
                         U8 fill_byte = 0 )
{
  ID3D11Texture2D * texture = NULL;

  //
  // Create a texture.
  //

  D3D11_TEXTURE2D_DESC desc;
  
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Usage = ( system ) ? D3D11_USAGE_STAGING : D3D11_USAGE_DYNAMIC;
  desc.BindFlags = ( system ) ? 0 : D3D11_BIND_SHADER_RESOURCE;
  desc.CPUAccessFlags = ( system ) ? ( D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ ) : D3D11_CPU_ACCESS_WRITE;
  desc.MiscFlags = 0;


  U8 *junkData = NULL;
  if(format == DXGI_FORMAT_A8_UNORM) {
	  junkData = new U8[width * height];
	  memset(junkData, fill_byte, width * height);
  }

  D3D11_SUBRESOURCE_DATA subResourceData = {0};
  subResourceData.pSysMem = junkData;
  subResourceData.SysMemPitch = width;
  subResourceData.SysMemSlicePitch = 0;

  if ( SUCCEEDED( d3d_device->CreateTexture2D( &desc, junkData ? &subResourceData : NULL, &texture ) ) )
  {
    *out_texture = texture;
    *out_size = width * height * pixel_size;

    if ( out_ptr && out_pitch )
    {
      D3D11_MAPPED_SUBRESOURCE m;
	  
      if ( FAILED( d3d_context->Map(texture, 0, D3D11_MAP_READ_WRITE, 0, &m) ) ) {
		  delete[] junkData;
        return( 0 );
	  }
      
	  //didn't help: memset(m.pData, 0, *out_size);
      *out_pitch = m.RowPitch;
      *out_ptr = m.pData;
	  d3d_context->Unmap(texture, 0);
    }

	delete[] junkData;
    return( 1 );
  }

  //
  // Failed
  //

  if (texture)
  {
    texture->Release();
  }

  delete[] junkData;
  return( 0 );
}


//############################################################################
//##                                                                        ##
//## Create 2 sets of textures for Bink to decompress into...               ##
//## Also does some basic sampler and render state init                     ##
//##                                                                        ##
//############################################################################

S32 Create_Bink_textures11( ID3D11Device * d3d_device, ID3D11DeviceContext *d3d_context,
                          BINKTEXTURESET * set_textures )
{
  BINKFRAMEBUFFERS * bb;
  BINKFRAMETEXTURES11 * bt;
  int i;

  set_textures->vert_buf = 0;
  set_textures->d3d_context = d3d_context;
  
  //
  // Create our system decompress textures (in system memory)
  //

  bb = &set_textures->bink_buffers;
  
  for ( i = 0; i < set_textures->bink_buffers.TotalFrames; ++i )
  {
    bt = &set_textures->textures11[ i ];
    bt->Ytexture = 0;
    bt->cBtexture = 0;
    bt->cRtexture = 0;
    bt->Atexture = 0;

    // Create Y plane
    if ( bb->Frames[ i ].YPlane.Allocate )
    {
      if ( !make_texture( d3d_device, d3d_context,
                          bb->YABufferWidth, bb->YABufferHeight,
                          1, DXGI_FORMAT_A8_UNORM, 1,
                          &bt->Ytexture,
                          &bb->Frames[ i ].YPlane.Buffer,
                          &bb->Frames[ i ].YPlane.BufferPitch,
                          &bt->Ysize, 0 ) )
        goto fail;
    }

    // Create cR plane
    if ( bb->Frames[ i ].cRPlane.Allocate )
    {
      if ( !make_texture( d3d_device, d3d_context,
                          bb->cRcBBufferWidth, bb->cRcBBufferHeight,
                          1, DXGI_FORMAT_A8_UNORM, 1,
                          &bt->cRtexture,
                          &bb->Frames[ i ].cRPlane.Buffer,
                          &bb->Frames[ i ].cRPlane.BufferPitch,
                          &bt->cRsize, 128 ) )
        goto fail;
    }

    // Create cB plane
    if ( bb->Frames[ i ].cBPlane.Allocate )
    {
      if ( !make_texture( d3d_device, d3d_context,
                          bb->cRcBBufferWidth, bb->cRcBBufferHeight,
                          1, DXGI_FORMAT_A8_UNORM, 1,
                          &bt->cBtexture,
                          &bb->Frames[ i ].cBPlane.Buffer,
                          &bb->Frames[ i ].cBPlane.BufferPitch,
                          &bt->cBsize, 128 ) )
        goto fail;
    }

    // Create alpha plane
    if ( bb->Frames[ i ].APlane.Allocate )
    {
      if ( !make_texture( d3d_device, d3d_context,
                          bb->YABufferWidth, bb->YABufferHeight,
                          1, DXGI_FORMAT_A8_UNORM, 1,
                          &bt->Atexture,
                          &bb->Frames[ i ].APlane.Buffer,
                          &bb->Frames[ i ].APlane.BufferPitch,
                          &bt->Asize ) )
        goto fail;
    }
  }

  //
  // Create our output draw texture (this should be in video card memory)
  //

  bt = &set_textures->tex_draw11;
  bt->Ytexture = 0;
  bt->cBtexture = 0;
  bt->cRtexture = 0;
  bt->Atexture = 0;

  set_textures->Yview = 0;
  set_textures->cBview = 0;
  set_textures->cRview = 0;
  set_textures->Aview = 0;

  D3D11_SHADER_RESOURCE_VIEW_DESC srvd;
  srvd.Format = DXGI_FORMAT_A8_UNORM;
  srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srvd.Texture2D.MostDetailedMip = 0;
  srvd.Texture2D.MipLevels = 1;

  // Create Y plane
  if ( bb->Frames[ 0 ].YPlane.Allocate )
  {
    if ( !make_texture( d3d_device, d3d_context,
                        bb->YABufferWidth, bb->YABufferHeight,
                        0, DXGI_FORMAT_A8_UNORM, 1,
                        &bt->Ytexture,
                        0, 0,
                        &bt->Ysize, 0 ) )
      goto fail;
    
    if ( FAILED( d3d_device->CreateShaderResourceView( bt->Ytexture, &srvd, &set_textures->Yview ) ) )
      goto fail;
  }

  // Create cR plane
  if ( bb->Frames[ 0 ].cRPlane.Allocate )
  {
    if ( !make_texture( d3d_device, d3d_context,
                        bb->cRcBBufferWidth, bb->cRcBBufferHeight,
                        0, DXGI_FORMAT_A8_UNORM, 1,
                        &bt->cRtexture,
                        0, 0,
                        &bt->cRsize, 128 ) )
      goto fail;

    if ( FAILED( d3d_device->CreateShaderResourceView( bt->cRtexture, &srvd, &set_textures->cRview ) ) )
      goto fail;
  }

  // Create cB plane
  if ( bb->Frames[ 0 ].cBPlane.Allocate )
  {
    if ( !make_texture( d3d_device, d3d_context,
                        bb->cRcBBufferWidth, bb->cRcBBufferHeight,
                        0, DXGI_FORMAT_A8_UNORM, 1,
                        &bt->cBtexture,
                        0, 0,
                        &bt->cBsize, 128 ) )
      goto fail;

    if ( FAILED( d3d_device->CreateShaderResourceView( bt->cBtexture, &srvd, &set_textures->cBview ) ) )
      goto fail;
  }

  // Create alpha plane
  if ( bb->Frames[ 0 ].APlane.Allocate )
  {
    if ( !make_texture( d3d_device, d3d_context,
                        bb->YABufferWidth, bb->YABufferHeight,
                        0, DXGI_FORMAT_A8_UNORM, 1,
                        &bt->Atexture,
                        0, 0,
                        &bt->Asize ) )
      goto fail;

    if ( FAILED( d3d_device->CreateShaderResourceView( bt->Atexture, &srvd, &set_textures->Aview ) ) )
      goto fail;
  }


  // create a vertex buffer to use
  
  D3D11_BUFFER_DESC desc;

  desc.ByteWidth = 4 * sizeof( POS_TC_VERTEX );
  desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  desc.CPUAccessFlags = 0;
  desc.MiscFlags = 0;
  desc.Usage = D3D11_USAGE_DEFAULT;
  
  if ( FAILED( d3d_device->CreateBuffer( &desc, 0, &set_textures->vert_buf ) ) )
    goto fail;
 
 
  // create a small constant buffer

  desc.ByteWidth = 4 * sizeof( F32 );
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  desc.CPUAccessFlags = 0;
  desc.MiscFlags = 0;

  if ( FAILED( d3d_device->CreateBuffer( &desc, 0, &set_textures->const_buf ) ) )
    goto fail;
 
  return( 1 );

fail:

  Free_Bink_textures11( set_textures );
  return( 0 );
}


//############################################################################
//##                                                                        ##
//## Lock Bink textures for use by D3D.                                     ##
//##                                                                        ##
//############################################################################

void Lock_Bink_textures11( BINKTEXTURESET * set_textures, S32 lock_both_for_writing )
{
  BINKFRAMETEXTURES11 * bt;
  BINKFRAMEPLANESET * bp;
  ID3D11DeviceContext *d3d_context = set_textures->d3d_context;
  D3D11_MAPPED_SUBRESOURCE m;
  int frame_cur;
  int i;

  //
  // Lock the frame textures
  //

  bt = set_textures->textures11;
  bp = set_textures->bink_buffers.Frames;

  frame_cur = set_textures->bink_buffers.FrameNum;

  for ( i = 0; i < set_textures->bink_buffers.TotalFrames; ++i, ++bt, ++bp )
  {
    D3D11_MAP lock_flags;
    
    if ( lock_both_for_writing )
      lock_flags = D3D11_MAP_READ_WRITE;
    else
      lock_flags = ( i == frame_cur ) ? D3D11_MAP_WRITE : D3D11_MAP_READ;

    if ( SUCCEEDED( d3d_context->Map(bt->Ytexture, 0, lock_flags, 0, &m ) ) )
    {
      bp->YPlane.Buffer = m.pData;
      bp->YPlane.BufferPitch = m.RowPitch;
    }

    if ( SUCCEEDED( d3d_context->Map(bt->cRtexture, 0, lock_flags, 0, &m ) ) )
    {
      bp->cRPlane.Buffer = m.pData;
      bp->cRPlane.BufferPitch = m.RowPitch;
    }

    if ( SUCCEEDED( d3d_context->Map(bt->cBtexture, 0, lock_flags, 0, &m ) ) )
    {
      bp->cBPlane.Buffer = m.pData;
      bp->cBPlane.BufferPitch = m.RowPitch;
    }

    if ( bt->Atexture )
    {
      //
      // Lock the alpha texture
      //

      if ( SUCCEEDED( d3d_context->Map(bt->Atexture, 0, lock_flags, 0, &m ) ) )
      {
        bp->APlane.Buffer = m.pData;
        bp->APlane.BufferPitch = m.RowPitch;
      }
    }
  }
}


//############################################################################
//##                                                                        ##
//## Unlock Bink textures for use by D3D.                                   ##
//##                                                                        ##
//############################################################################

void Unlock_Bink_textures11( BINKTEXTURESET * set_textures, HBINK Bink )
{
  BINKFRAMETEXTURES11 * bt;
  BINKFRAMEPLANESET * bp;
  ID3D11DeviceContext *d3d_context = set_textures->d3d_context;
  int i;

  //
  // Unlock the frame textures
  //

  bt = set_textures->textures11;
  bp = set_textures->bink_buffers.Frames;

  for ( i = 0; i < set_textures->bink_buffers.TotalFrames; ++i, ++bt, ++bp )
  {
    d3d_context->Unmap(bt->Ytexture, 0);
    bp->YPlane.Buffer = NULL;

    d3d_context->Unmap(bt->cRtexture, 0 );
    bp->cRPlane.Buffer = NULL;

    d3d_context->Unmap(bt->cBtexture, 0 );
    bp->cBPlane.Buffer = NULL;

    if ( bt->Atexture )
    {
      //
      // Unlock the alpha texture
      //

      d3d_context->Unmap(bt->Atexture, 0 );
      bp->APlane.Buffer = NULL;
    }
  }

  //
  // Now we have to update the pixels on the video card texture
  //

  S32 num_rects;
  BINKFRAMETEXTURES11 * bt_dst;
  BINKFRAMETEXTURES11 * bt_src;

  bt_src = &set_textures->textures11[ set_textures->bink_buffers.FrameNum ];
  bt_dst = &set_textures->tex_draw11;

  num_rects = BinkGetRects( Bink, BINKSURFACEFAST );
  if ( num_rects > 0 )
  {
    BINKRECT * brc;
    D3D11_BOX rc;
	BINKFRAMEBUFFERS set = {0};

    rc.front = 0;
    rc.back = 1;

	BinkGetFrameBuffersInfo(Bink, &set);

    for ( i = 0; i < num_rects; ++i )
    {
      brc = &Bink->FrameRects[ i ];

      rc.left = brc->Left;
      rc.top = brc->Top;
      rc.right = rc.left + brc->Width;
      rc.bottom = rc.top + brc->Height;

      d3d_context->CopySubresourceRegion( bt_dst->Ytexture, 0, rc.left, rc.top, 0, bt_src->Ytexture, 0, &rc );

      if ( bt_src->Atexture )
      {
        d3d_context->CopySubresourceRegion( bt_dst->Atexture, 0, rc.left, rc.top, 0, bt_src->Atexture, 0, &rc );
      }

      rc.left >>= 1;
      rc.top >>= 1;
      rc.right >>= 1;
      rc.bottom >>= 1;

	  // Attempt to expand the region we're about to copy to entire 8x8 blocks.
	  if(rc.bottom % 8) rc.bottom = ((rc.bottom / 8) + 1) * 8;
	  if(rc.right  % 8) rc.right  = ((rc.right  / 8) + 1) * 8;
	  if(rc.top    % 8) rc.top    = (rc.bottom / 8) * 8;
	  if(rc.left   % 8) rc.left   = (rc.bottom / 8) * 8;

	  // And just for safety, make sure that's really in the image's area.
	  if(rc.left < 0) rc.left = 0;
	  if(rc.top  < 0) rc.top  = 0;
	  if(rc.right  > set.cRcBBufferWidth)  rc.right  = set.cRcBBufferWidth;
	  if(rc.bottom > set.cRcBBufferHeight) rc.bottom = set.cRcBBufferHeight;

      d3d_context->CopySubresourceRegion( bt_dst->cRtexture, 0, rc.left, rc.top, 0, bt_src->cRtexture, 0, &rc );
      d3d_context->CopySubresourceRegion( bt_dst->cBtexture, 0, rc.left, rc.top, 0, bt_src->cBtexture, 0, &rc );
    }
  }
}


//############################################################################
//##                                                                        ##
//## Draw our textures onto the screen with our vertex and pixel shaders.   ##
//##                                                                        ##
//############################################################################

void Draw_Bink_textures11( BINKTEXTURESET * set_textures,
                         U32 width,
                         U32 height,
                         F32 x_offset,
                         F32 y_offset,
                         F32 x_scale,
                         F32 y_scale,
                         F32 alpha_level,
                         S32 is_premultiplied_alpha )
{
  POS_TC_VERTEX vertices[ 4 ];
  BINKFRAMEPLANESET * bp;
  BINKFRAMETEXTURES11 * bt_draw;
  F32 ac[ 4 ];
  ID3D11DeviceContext * d3d_device = set_textures->d3d_context;

  ac[ 0 ] = ( is_premultiplied_alpha ) ? alpha_level : 1.0f;
  ac[ 1 ] = ac [ 0 ];
  ac[ 2 ] = ac [ 0 ];
  ac[ 3 ] = alpha_level;

  bp = &set_textures->bink_buffers.Frames[ set_textures->bink_buffers.FrameNum ];

  bt_draw = &set_textures->tex_draw11;

  //
  // Turn on texture filtering and texture clamping
  //

  d3d_device->PSSetSamplers( 0, 1, &sampler_state );

  //
  // turn off Z buffering, culling, and projection (since we are drawing orthographically)
  //

  d3d_device->RSSetState( rasterizer_state );
  d3d_device->OMSetDepthStencilState( depth_stencil_state, 0 );

  //
  // Set the textures.
  //
  
  d3d_device->PSSetShaderResources( 0, 1, &set_textures->Yview );
  d3d_device->PSSetShaderResources( 1, 1, &set_textures->cRview );
  d3d_device->PSSetShaderResources( 2, 1, &set_textures->cBview );

  //
  // upload the fixed alpha amount
  //

  d3d_device->UpdateSubresource( set_textures->const_buf, 0, 0, ac, sizeof( ac ), 1 );
  d3d_device->PSSetConstantBuffers( 0, 1, &set_textures->const_buf );

  //
  // Setup up the vertices.
  //

  d3d_device->IASetInputLayout( vertex_decl );

  //
  // set vertex shader
  //
  
  d3d_device->VSSetShader( PositionAndTexCoordPassThrough, NULL, 0 );

  //
  // are we using an alpha plane? if so, turn on the 4th texture and set our pixel shader
  //

  if ( bt_draw->Atexture )
  {
    //
    // Update and set the alpha texture
    //

    d3d_device->PSSetShaderResources( 3, 1, &set_textures->Aview );

    //
    // turn on our pixel shader
    //

    d3d_device->PSSetShader( YCrCbAToRGBA, NULL, 0 );

    goto do_alpha;
  }
  else
  {
    //
    // turn on our pixel shader
    //

    d3d_device->PSSetShader( YCrCbToRGBNoPixelAlpha, NULL, 0 );
  }

  //
  // are we completely opaque or somewhat transparent?
  //

  if ( alpha_level >= 0.999f )
    d3d_device->OMSetBlendState( 0, 0, 0xffffffff );
  else
  {
  do_alpha:
    d3d_device->OMSetBlendState( is_premultiplied_alpha ? alpha_pm_state : alpha_state, 0, 0xffffffff );
  }
  
  F32 iw = 2.0f / (F32) (S32) width;
  F32 ih = 2.0f / (F32) (S32) height;

  vertices[ 0 ].sx = ( x_offset * iw ) - 1.0f;
  vertices[ 0 ].sy = 1.0f - ( y_offset * ih );
  vertices[ 0 ].sz = 0.0f;
  vertices[ 0 ].rhw = 1.0f;
  vertices[ 0 ].tu = 0.0f;
  vertices[ 0 ].tv = 0.0f;

  vertices[ 1 ] = vertices[ 0 ];
  vertices[ 1 ].sx = ( ( x_offset * iw ) + x_scale + x_scale ) - 1.0f;
  vertices[ 1 ].tu = 1.0f;

  vertices[ 2 ] = vertices[ 0 ];
  vertices[ 2 ].sy = 1.0f - ( ( y_offset * ih ) + y_scale + y_scale );
  vertices[ 2 ].tv = 1.0f;

  vertices[ 3 ] = vertices[ 1 ];
  vertices[ 3 ].sy = vertices[ 2 ].sy;
  vertices[ 3 ].tv = 1.0f;

  d3d_device->UpdateSubresource( set_textures->vert_buf, 0, 0, vertices, sizeof( vertices ), sizeof( vertices ) );


  UINT stride = sizeof( POS_TC_VERTEX );
  UINT offset = 0;
  d3d_device->IASetVertexBuffers( 0, 1, &set_textures->vert_buf, &stride, &offset );
  d3d_device->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );

  //
  // Draw the vertices.
  //

  d3d_device->Draw( 4, 0 );

  //
  // unselect everything
  //
  
  void * zero = 0;
  
  d3d_device->PSSetSamplers( 0, 1, (ID3D11SamplerState**)&zero );
  d3d_device->RSSetState( 0 );
  d3d_device->OMSetDepthStencilState( 0, 0 );
  d3d_device->PSSetConstantBuffers( 0, 1, (ID3D11Buffer**)&zero );
  d3d_device->PSSetShaderResources( 0, 1, (ID3D11ShaderResourceView**)&zero );
  d3d_device->PSSetShaderResources( 1, 1, (ID3D11ShaderResourceView**)&zero );
  d3d_device->PSSetShaderResources( 2, 1, (ID3D11ShaderResourceView**)&zero );
  d3d_device->PSSetShaderResources( 3, 1, (ID3D11ShaderResourceView**)&zero );
  d3d_device->PSSetShader( NULL, NULL, 0 );
  d3d_device->OMSetBlendState( 0, 0, 0xffffffff );
  d3d_device->IASetInputLayout( 0 );
  d3d_device->IASetVertexBuffers( 0, 1, (ID3D11Buffer**)&zero, (UINT*) &zero, (UINT*) &zero );
}

#endif
