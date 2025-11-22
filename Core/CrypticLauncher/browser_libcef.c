#include "file.h"
#include "Message.h"
#include "cpu_count.h"
#include "qsortG.h"
#include "winutil.h"
#include <tg_os.h>
#include <tg_windowing.h>

// CrypticLauncher
#include "browser_libcef.h"
#include "UI.h"
#include "LauncherMain.h"
#include "LauncherLocale.h"
#include "resource_CrypticLauncher.h"

// UtilitiesLib
#include "MemTrack.h"
#include "sysutil.h"
#include "file.h"
#include "fileutil.h"
#include "utils.h"
#include "error.h"
#include "EArray.h"
#include "SimpleWindowManager.h"
#include "SuperAssert.h"

#include <d3d9.h>
#include "UTF8.h"

// so that cef_v8context_t->exit() will work
#ifdef exit
#undef exit
#endif

static bool sbLibCEFInited = false;

#if ENABLE_LIBCEF

// IMPORTANT - Memory Management
//
// We must manage our reference counting of libCEF objects in the manner specified by this doc:
//
// https://code.google.com/p/chromiumembedded/wiki/UsingTheCAPI
//
// Using the debug build of libcef.dll will tell you on shutdown if there are leaked references.
//
// Key points summary
//
// 1) Any cef struct passed to one of our callbacks must be released if we are not planning to hold
//    onto it after the callback completes. The exception is the self pointer.
// 2) Any cef struct returned from a cef function call must be released if we are not planning to hold
//    onto it after we are done processing with it in the stack frame.
// 3) Any cef struct pointer that is an out parameter from a cef function call must be released if we
//    are not planning to hold onto it after we are done processing with it in the stack frame.
// 4) Any of our own structs that sub-struct a cef struct should have thread-safe reference counting
//    implemented. This is apparently not a strict requirement of libcef, but it helps ensure we do
//    not leak our own structs.
//
// WebKit Memory Leaks
//
// When running with the debug build, you may see output on shutdown that looks like this:
//
//    LEAK: 992 WebCoreNode
//    LEAK: 38 CachedResource
//
// Do not fret. Chromium and V8 intentionally leak objects on exit. This is considered a WontFix by the
// Chromium Embedded team: https://code.google.com/p/chromiumembedded/issues/detail?id=15

#pragma warning(disable:4310)
#include "include/cryptic.h"
#include "include/capi/cef_app_capi.h"
#include "include/capi/cef_url_capi.h"
#include "include/capi/cef_browser_capi.h"
#include "include/capi/cef_response_capi.h"
#include "include/capi/cef_stream_capi.h"
#include "include/capi/cef_client_capi.h"
#include "include/capi/cef_frame_capi.h"
#include "include/capi/cef_nplugin_capi.h"
#include "include/capi/cef_life_span_handler_capi.h"
#include "include/capi/cef_render_handler_capi.h"
#include "include/capi/cef_load_handler_capi.h"
#include "include/capi/cef_request_handler_capi.h"
#include "include/capi/cef_request_capi.h"
#include "include/capi/cef_base_capi.h"
#include "include/capi/cef_resource_bundle_handler_capi.h"
#include "include/capi/cef_proxy_handler_capi.h"
#include "include/capi/cef_web_urlrequest_capi.h"
#include "include/capi/cef_content_filter_capi.h"
#include "include/capi/cef_v8_capi.h"
#include "include/capi/cef_menu_handler_capi.h"

// libcef team seems to have missed these macros
#if defined(CEF_STRING_TYPE_UTF16)
#define cef_string_cmp cef_string_utf16_cmp
#elif defined(CEF_STRING_TYPE_UTF8)
#define cef_string_cmp cef_string_utf8_cmp
#elif defined(CEF_STRING_TYPE_WIDE)
#define cef_string_cmp cef_string_wide_cmp
#endif

// CefURLPartsTraits is not wrapped by C libcef API so we must destruct cef_urlparts_t ourselves.
void cef_urlparts_clear(cef_urlparts_t *urlparts)
{
	cef_string_clear(&urlparts->spec);
	cef_string_clear(&urlparts->scheme);
	cef_string_clear(&urlparts->username);
	cef_string_clear(&urlparts->password);
	cef_string_clear(&urlparts->host);
	cef_string_clear(&urlparts->port);
	cef_string_clear(&urlparts->path);
	cef_string_clear(&urlparts->query);
}

// IMPORTANT
//
// libcef.dll is and should be marked as DELAYLOAD in CrypticLauncher linker options, but this gives us the symbols (avoids GetProcAddress calls for them)
#pragma comment(lib, "../../3rdparty/cef/lib/libcef.lib")

#define CRYPTIC_CEF_OVERRIDE_VERSION 5

static void *cef_locale_pak;  // Locale pak data
static size_t cef_locale_pak_length;
static void *cef_devtools_resources_pak;  // devtools resources pak data
static size_t cef_devtools_resources_pak_length;

typedef struct CEFClient CEFClient;

typedef struct CEFRender 
{
	cef_render_handler_t _base;

	CEFClient *client;
} CEFRender;

typedef struct CEFLifespan
{
	cef_life_span_handler_t _base;

	CEFClient *client;
} CEFLifespan;

typedef struct CEFLoad
{
	cef_load_handler_t _base;

	CEFClient *client;
} CEFLoad;

typedef struct CEFRequest
{
	cef_request_handler_t _base;

	CEFClient *client;
} CEFRequest;

typedef struct CEFMenu
{
	cef_menu_handler_t _base;

	CEFClient *client;
} CEFMenu;

typedef struct CEFURLRequestClient
{
	cef_web_urlrequest_client_t _base;

	CEFClient *client;
} CEFURLRequestClient;

typedef struct CEFV8Handler
{
	cef_v8handler_t _base;

	OnChangeCallbackFunc onChangeCallbackFunc;
	void *userOnChangeData;

	int ref_count;
} CEFV8Handler;

typedef struct CEFClient
{
	cef_client_t _base;

	CEFRender _render;
	CEFLifespan _lifespan;
	CEFLoad _load;
	CEFRequest _request;
	CEFMenu _menu;

	cef_browser_t *browser;

	HWND hwnd;

	int browserWidth;
	int browserHeight;

	HDC hMemoryDC;
	HBITMAP hBitmap;
	void *pPixels;

	char *main_url;
	char *main_url_path;

	cef_rect_t popup_rect;

	HDC hPopupMemoryDC;
	HBITMAP hPopupBitmap;
	void *pPopupPixels;

	int ref_count;
} CEFClient;

static CEFClient *s_client = NULL;

// DirectX globals.
static LPDIRECT3DDEVICE9 s_d3dDevice = NULL;
static LPDIRECT3D9 s_d3d9 = NULL;
static IDirect3DTexture9 *s_d3dScreenCopyTexture = NULL;
typedef struct D3DVertex {
	FLOAT x, y, z, rhw;
	DWORD color;
	FLOAT u, v;
} D3DVertex;

static bool getD3DBackBufferSize(unsigned int *width, unsigned int *height) {

	if(s_d3dDevice) {

		IDirect3DSurface9 *backBuffer = NULL;
		D3DSURFACE_DESC desc = {0};

		IDirect3DDevice9_GetBackBuffer(
			s_d3dDevice, 0, 0,
			D3DBACKBUFFER_TYPE_MONO,
			&backBuffer);

		if(!backBuffer)
			return false;

		IDirect3DSurface9_GetDesc(
			backBuffer, &desc);

		IDirect3DSurface9_Release(backBuffer);

		if(desc.Width && desc.Height) {
			*width = desc.Width;
			*height = desc.Height;
		} else {
			return false;
		}

		return true;
	}

	return false;
}




cef_render_handler_t* CEF_CALLBACK cefGetRenderHandler(cef_client_t *client)
{
	CEFClient *c = (CEFClient *)client;
	return &c->_render._base;
}

static void cefPixelBlt(void *dstBuf, int dstStride, const void *srcBuf, int srcStride, const cef_rect_t *dstRect)
{
	int x, y;
	unsigned long *dst = (unsigned long *)dstBuf;
	unsigned long *src = (unsigned long *)srcBuf;
	int startX = dstRect->x;
	int startY = dstRect->y;
	// endX/Y are non-inclusive
	int endX = dstRect->x + dstRect->width;
	int endY = dstRect->y + dstRect->height;

	for (y = startY; y < endY; y++)
	{
		int dstIndex = y * dstStride;
		int srcIndex = y * srcStride;
		for (x = startX; x < endX; x++)
		{
			// TODO: figure out if we can make the sprite system handle opaque renders,
			// and convert this entire for (x...) loop into a memcpy.
			U32 src_col = src[srcIndex + x];
			U8 src_r = src_col & 0xFF;
			U8 src_g = (src_col >> 8) & 0xFF;
			U8 src_b = (src_col >> 16) & 0xFF;
			F32 a = ((src_col >> 24) & 0xFF) / 255.0f;

			U32 dst_col = dst[dstIndex + x];
			U8 dst_r = dst_col & 0xFF;
			U8 dst_g = (dst_col >> 8) & 0xFF;
			U8 dst_b = (dst_col >> 16) & 0xFF;

			U8 r = a * (src_r - dst_r) + dst_r;
			U8 g = a * (src_g - dst_g) + dst_g;
			U8 b = a * (src_b - dst_b) + dst_b;

			dst[dstIndex + x] = 0xFF000000 | (b << 16) | (g << 8) | r;
		}
	}
}

// starting code for this was found in ClientOSRenderer::OnPaint() here:
// https://code.google.com/p/chromiumembedded/source/browse/trunk/cef1/tests/cefclient/osrenderer.cpp?r=745
void CEF_CALLBACK cefOnPaint(cef_render_handler_t* self, cef_browser_t* browser, enum cef_paint_element_type_t type, size_t dirtyRectsCount, cef_rect_t const* dirtyRects, const void* buffer)
{
	CEFRender *r = (CEFRender*)self;
	CEFClient *c = r->client;

	if(type == PET_VIEW)
	{
		HDC hDC;
		size_t i;

		browser->get_size(browser, type, &c->browserWidth, &c->browserHeight);

		if(!s_d3dDevice) {

			// Windows GDI mode.

			hDC = GetDC(c->hwnd);

			if(!c->hMemoryDC)
				c->hMemoryDC = CreateCompatibleDC(hDC);
			if(!c->hBitmap)
			{
				BITMAPINFO bmi;
				ZeroMemory(&bmi, sizeof(BITMAPINFO));
				bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bmi.bmiHeader.biWidth = c->browserWidth;
				bmi.bmiHeader.biHeight = -c->browserHeight;
				bmi.bmiHeader.biPlanes = 1;
				bmi.bmiHeader.biBitCount = 32;
				bmi.bmiHeader.biCompression = BI_RGB;
				bmi.bmiHeader.biSizeImage = c->browserWidth * c->browserHeight * 4;

				c->hBitmap = CreateDIBSection(c->hMemoryDC, &bmi, 0, &c->pPixels, NULL, 0);

				SelectObject(c->hMemoryDC, c->hBitmap);
			}

			for(i = 0; i < dirtyRectsCount; i++)
				cefPixelBlt(c->pPixels, c->browserWidth, buffer, c->browserWidth, &dirtyRects[i]);

			BitBlt(hDC, 0, 0, c->browserWidth, c->browserHeight, c->hMemoryDC, 0, 0, SRCCOPY);
			if(c->popup_rect.width > 0 && c->popup_rect.height > 0)
				BitBlt(hDC, c->popup_rect.x, c->popup_rect.y, c->popup_rect.width, c->popup_rect.height, c->hPopupMemoryDC, 0, 0, SRCCOPY);

			ReleaseDC(c->hwnd, hDC);

		} else {

			// DirectX mode.

			D3DLOCKED_RECT lockedRect = {0};
			unsigned int width = 0;
			unsigned int height = 0;
			unsigned int y;

			getD3DBackBufferSize(&width, &height);

			assert(s_d3dScreenCopyTexture);

			assert(
				IDirect3DTexture9_LockRect(
					s_d3dScreenCopyTexture, 0,
					&lockedRect, NULL,
					D3DLOCK_DISCARD) == D3D_OK);

			// FIXME: Using cefPixelBlt on Cider to just copy the dirty rectangles results
			//   in only half the image (diagonally) being represented. This still happens
			//   without the D3DLOCK_DISCARD flag.

			// for(i = 0; i < dirtyRectsCount; i++)
			// 	cefPixelBlt(lockedRect.pBits, lockedRect.Pitch/4, buffer, c->browserWidth, &dirtyRects[i]);

			// I'm just going to copy the entire image for now. -Cliff

			for(y = 0; y < height; y++) {
				U32 *dstRowPtr = (U32*)(((U8*)lockedRect.pBits) + y * lockedRect.Pitch);
				U32 *srcRowPtr = (U32*)(((U8*)buffer) + y * c->browserWidth * 4);
				memcpy(dstRowPtr, srcRowPtr, MIN(lockedRect.Pitch, c->browserWidth * 4));
			}

			assert(IDirect3DTexture9_UnlockRect(s_d3dScreenCopyTexture, 0) == D3D_OK);

			IDirect3DDevice9_Clear(s_d3dDevice, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0,0,255), 1.0f, 0);

			assert(IDirect3DDevice9_BeginScene(s_d3dDevice) == D3D_OK);

			assert(IDirect3DDevice9_SetFVF(s_d3dDevice, D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1) == D3D_OK);
			assert(IDirect3DDevice9_SetTexture(s_d3dDevice, 0, (IDirect3DBaseTexture9*)s_d3dScreenCopyTexture) == D3D_OK);
			assert(IDirect3DDevice9_SetTextureStageState(s_d3dDevice, 0, D3DTSS_COLOROP, D3DTOP_MODULATE) == D3D_OK);
			assert(IDirect3DDevice9_SetTextureStageState(s_d3dDevice, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE) == D3D_OK);
			assert(IDirect3DDevice9_SetTextureStageState(s_d3dDevice, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE) == D3D_OK);
			assert(IDirect3DDevice9_SetTextureStageState(s_d3dDevice, 0, D3DTSS_ALPHAOP, D3DTOP_DISABLE) == D3D_OK);

			{
				D3DVertex vertices[] = {
					{ 0,     0,      0.5, 1, 0xffffffff, 0, 0 },
					{ width, 0,      0.5, 1, 0xffffffff, 1, 0 },
					{ width, height, 0.5, 1, 0xffffffff, 1, 1 },
					{ 0,     height, 0.5, 1, 0xffffffff, 0, 1 },
				};

				assert(
					IDirect3DDevice9_DrawPrimitiveUP(
						s_d3dDevice, D3DPT_TRIANGLEFAN,
						4, vertices, sizeof(D3DVertex)) == D3D_OK);;
			}

			assert(IDirect3DDevice9_EndScene(s_d3dDevice) == D3D_OK);

			IDirect3DDevice9_Present(s_d3dDevice, NULL, NULL, NULL, NULL);

		}




	}
	else if(type == PET_POPUP && c->popup_rect.width > 0 && c->popup_rect.height > 0)
	{
		// we ignore the dirtyRects passed in, and instead use the popup_rect set in 
		// cefOnPopupShow() and cefOnPopupSize()

		HDC hDC;
		cef_rect_t dstRect = c->popup_rect;
		dstRect.x = dstRect.y = 0;

		hDC = GetDC(c->hwnd);

		cefPixelBlt(c->pPopupPixels, c->popup_rect.width, buffer, c->popup_rect.width, &dstRect);

		BitBlt(hDC, c->popup_rect.x, c->popup_rect.y, c->popup_rect.width, c->popup_rect.height, c->hPopupMemoryDC, 0, 0, SRCCOPY);
		BitBlt(c->hMemoryDC, c->popup_rect.x, c->popup_rect.y, c->popup_rect.width, c->popup_rect.height, c->hPopupMemoryDC, 0, 0, SRCCOPY);

		ReleaseDC(c->hwnd, hDC);
	}

	browser->base.release(&browser->base);
}

void CEF_CALLBACK cefOnCursorChange(cef_render_handler_t* self, cef_browser_t* browser, cef_cursor_handle_t cursor)
{
	SetCursor(cursor);
	SetClassLong(s_client->hwnd, GCL_HCURSOR, (DWORD)cursor);

	browser->base.release(&browser->base);
}

// Called to retrieve the view rectangle which is relative to screen
// coordinates. Return true (1) if the rectangle was provided.
int CEF_CALLBACK cefGetViewRect(cef_render_handler_t* self, cef_browser_t* browser, cef_rect_t* rect)
{
	CEFRender *r = (CEFRender*)self;
	CEFClient *c = r->client;

	rect->x = 0;
	rect->y = 0;

	if(!getD3DBackBufferSize(&rect->width, &rect->height)) {
		RECT clientRect;
		GetClientRect(c->hwnd, &clientRect);
		rect->width = clientRect.right - clientRect.left;
		rect->height = clientRect.bottom - clientRect.top;
	}

	browser->base.release(&browser->base);

	return true;
}

// Called to retrieve the simulated screen rectangle. Return true (1) if the
// rectangle was provided.
int CEF_CALLBACK cefGetScreenRect(cef_render_handler_t* self, cef_browser_t* browser, cef_rect_t* rect)
{
	CEFRender *r = (CEFRender*)self;
	CEFClient *c = r->client;

	rect->x = 0;
	rect->y = 0;

	if(!getD3DBackBufferSize(&rect->width, &rect->height)) {
		RECT clientRect;
		GetClientRect(c->hwnd, &clientRect);
		rect->width = clientRect.right - clientRect.left;
		rect->height = clientRect.bottom - clientRect.top;
	}

	browser->base.release(&browser->base);

	return true;
}

int CEF_CALLBACK cefGetScreenPoint(cef_render_handler_t* self, cef_browser_t* browser, int viewX, int viewY, int* screenX, int* screenY)
{
	CEFRender *r = (CEFRender*)self;
	CEFClient *c = r->client;

	POINT screen_pt = { viewX, viewY };

	MapWindowPoints(c->hwnd, HWND_DESKTOP, &screen_pt, 1);

	*screenX = screen_pt.x;
	*screenY = screen_pt.y;

	browser->base.release(&browser->base);

	return 1;
}

// Called when the browser wants to show or hide the popup widget. The popup
// should be shown if |show| is true (1) and hidden if |show| is false (0).
void CEF_CALLBACK cefOnPopupShow(cef_render_handler_t* self, cef_browser_t* browser, int show)
{
	if(!show)
	{
		CEFRender *r = (CEFRender*)self;
		CEFClient *c = r->client;

		browser->invalidate(browser, &(c->popup_rect));

		c->popup_rect.x =
		c->popup_rect.y =
		c->popup_rect.width =
		c->popup_rect.height = 0;

		if(s_client->hPopupBitmap)
		{
			DeleteObject(s_client->hPopupBitmap);
			s_client->hPopupBitmap = NULL;
		}
		if(s_client->hPopupMemoryDC)
		{
			DeleteDC(s_client->hPopupMemoryDC);
			s_client->hPopupMemoryDC = NULL;
		}
	}

	browser->base.release(&browser->base);
}

// Called when the browser wants to move or resize the popup widget. |rect|
// contains the new location and size.
void CEF_CALLBACK cefOnPopupSize(cef_render_handler_t* self, cef_browser_t* browser, const cef_rect_t* rect)
{
	if(rect->width > 0 && rect->height > 0)
	{
		CEFRender *r = (CEFRender*)self;
		CEFClient *c = r->client;
		c->popup_rect.x = rect->x;
		c->popup_rect.y = rect->y;
		c->popup_rect.width = rect->width;
		c->popup_rect.height = rect->height;

		{
			BITMAPINFO bmi;
			HDC hDC = GetDC(c->hwnd);

			c->hPopupMemoryDC = CreateCompatibleDC(hDC);

			ZeroMemory(&bmi, sizeof(BITMAPINFO));
			bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bmi.bmiHeader.biWidth = c->popup_rect.width;
			bmi.bmiHeader.biHeight = -c->popup_rect.height;
			bmi.bmiHeader.biPlanes = 1;
			bmi.bmiHeader.biBitCount = 32;
			bmi.bmiHeader.biCompression = BI_RGB;
			bmi.bmiHeader.biSizeImage = c->popup_rect.width * c->popup_rect.height * 4;

			c->hPopupBitmap = CreateDIBSection(c->hPopupMemoryDC, &bmi, 0, &c->pPopupPixels, NULL, 0);

			SelectObject(c->hPopupMemoryDC, c->hPopupBitmap);
		}
	}

	browser->base.release(&browser->base);
}

cef_life_span_handler_t* CEF_CALLBACK cefGetLifespanHandler(cef_client_t *client)
{
	CEFClient *c = (CEFClient *)client;
	return &c->_lifespan._base;
}

// IMPORTANT
//
// cefHandleURL is where I am routing all URL requests. This can occur in 2 ways:
//
// 1) Interacting with a link with target='_blank'. This causes chromium to popup browser window by default. cefOnBeforePopup calls handleUrl to intercept that case.
// 2) Posting a form submit action. This is done on the existing browser window. cefRequestOnBeforeBrowse calls handleUrl to intercept that case.
//
static int cefHandleURL(const cef_string_t *url)
{
	int result = 0;
	cef_urlparts_t urlparts = {0};

	cef_string_t about_scheme = {0};
	cef_string_t app_scheme = {0};
	cef_string_from_utf8("about", strlen("about"), &about_scheme);
	cef_string_from_utf8("app", strlen("app"), &app_scheme);

	if(cef_parse_url(url, &urlparts))
	{
		if(0 == cef_string_cmp(&urlparts.scheme, &app_scheme)) // using app:
		{
			int num;
			cef_string_utf8_t path_utf8 = {0};
			cef_string_to_utf8(urlparts.path.str, urlparts.path.length, &path_utf8);

			num = atoi(path_utf8.str);
			switch(num)
			{
				case CLMSG_PAGE_LOADED: // Page load XHR
					break;
				case CLMSG_ACTION_BUTTON_CLICKED: // Action button link
					break;
				case CLMSG_OPTIONS_CLICKED: // Options link
					break;
				case CLMSG_LOGIN_SUBMIT: // Login form
					break;
				case CLMSG_OPTIONS_SAVED: // options saved
					break;
				default:
					FatalErrorf("Unknown application URL %u", num);
			}

			PostMessage(s_client->hwnd, WM_APP, num, 0);

			cef_string_utf8_clear(&path_utf8);

			result = 1;
		}
		else if(0 != cef_string_cmp(&urlparts.scheme, &about_scheme)) // not using about:
		{
			char *estrLoginOrPrepatchURL = NULL;
			cef_string_t loginOrPrepatchURL_str = {0};
			cef_urlparts_t loginOrPrepatchURL_parts = {0};

			LauncherGetLoginOrPrepatchURL(&estrLoginOrPrepatchURL);
			cef_string_from_utf8(estrLoginOrPrepatchURL, estrLength(&estrLoginOrPrepatchURL), &loginOrPrepatchURL_str);

			if(cef_parse_url(&loginOrPrepatchURL_str, &loginOrPrepatchURL_parts))
			{
				if(0 != cef_string_cmp(&urlparts.host, &loginOrPrepatchURL_parts.host)) // not using launcher host
				{
					cef_string_utf16_t url_utf16 = {0};
					cef_string_to_utf16(url->str, url->length, &url_utf16);

					// For any non launcher URL, send it to the default browser.
					ShellExecuteW(NULL, L"open", url_utf16.str, NULL, NULL, SW_SHOWNORMAL);
					result = 1;

					cef_string_utf16_clear(&url_utf16);
				}
			}

			cef_urlparts_clear(&loginOrPrepatchURL_parts);
			cef_string_clear(&loginOrPrepatchURL_str);
			estrDestroy(&estrLoginOrPrepatchURL);
		}
	}

	cef_string_clear(&about_scheme);
	cef_string_clear(&app_scheme);

	cef_urlparts_clear(&urlparts);

	return result;
}

int CEF_CALLBACK cefOnBeforePopup(cef_life_span_handler_t* self, cef_browser_t* parentBrowser, const cef_popup_features_t* popupFeatures, cef_window_info_t* windowInfo, const cef_string_t* url,
	cef_client_t** client, cef_browser_settings_t* settings)
{
	int result = cefHandleURL(url);

	if(0 == result) // this indicates that it is a CrypitLauncher URL, but we do not want them to popup. We want them to load in frame.
	{
		cef_string_utf8_t url_utf8 = {0};

		cef_string_to_utf8(url->str, url->length, &url_utf8);

		BrowserLibCEFDisplayHTMLFromURL(url_utf8.str, NULL);

		cef_string_utf8_clear(&url_utf8);
	}

	parentBrowser->base.release(&parentBrowser->base);

	return 1; // We have handled the URL
}

void CEF_CALLBACK cefOnAfterCreated(cef_life_span_handler_t* self, cef_browser_t* browser)
{
	CEFLifespan *l = (CEFLifespan*)self;
	CEFClient *c = l->client;

	RECT clientRect;
	GetClientRect(c->hwnd, &clientRect);

	browser->set_size(browser, PET_VIEW, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
	c->browser = browser;
}

int CEF_CALLBACK cefLifespanDoClose(cef_life_span_handler_t* self, cef_browser_t* browser)
{
	browser->base.release(&browser->base);

	return false;
}

void CEF_CALLBACK cefLifespanOnBeforeClose(cef_life_span_handler_t* self, cef_browser_t* browser)
{
	CEFLifespan *l = (CEFLifespan *)self;
	CEFClient *c = l->client;

	// IMPORTANT
	//
	// From https://code.google.com/p/chromiumembedded/wiki/UsingTheCAPI
	//
	// "Reverse any additional references that your code adds to a struct (for instance, if you keep a reference to
	// the cef_browser_t pointer in your handler implementation). The last opportunity to release references is in
	// cef_handler_t::handle_before_window_closed()."
	c->browser->base.release(&c->browser->base);

	browser->base.release(&browser->base);
}

cef_load_handler_t* CEF_CALLBACK cefGetLoadHandler(cef_client_t* client)
{
	CEFClient *c = (CEFClient *)client;
	return &c->_load._base;
}

cef_request_handler_t* CEF_CALLBACK cefGetRequestHandler(cef_client_t* client)
{
	CEFClient *c = (CEFClient *)client;
	return &c->_request._base;
}

cef_menu_handler_t* CEF_CALLBACK cefGetMenuHandler(cef_client_t* client)
{
	CEFClient *c = (CEFClient *)client;
	return &c->_menu._base;
}

void CEF_CALLBACK cefLoadOnLoadStart(cef_load_handler_t* self, cef_browser_t* browser, cef_frame_t *frame)
{
	CEFLoad *l = (CEFLoad*)self;
	CEFClient *c = l->client;

	if(frame->is_main(frame))
	{
		cef_string_utf8_t url_utf8 = {0};
		cef_string_userfree_t url_str = frame->get_url(frame);
		cef_urlparts_t urlparts = {0};

		BrowserPageLoading();

		cef_string_to_utf8(url_str->str, url_str->length, &url_utf8);

		SAFE_FREE(c->main_url);
		c->main_url = malloc(url_utf8.length+1);
		strcpy_s(c->main_url, url_utf8.length+1, url_utf8.str);

		SAFE_FREE(c->main_url_path);
		if(cef_parse_url(url_str, &urlparts))
		{
			if(urlparts.path.length > 0)
			{
				cef_string_utf8_clear(&url_utf8);
				cef_string_to_utf8(urlparts.path.str, urlparts.path.length, &url_utf8);

				c->main_url_path = malloc(url_utf8.length+1);
				strcpy_s(c->main_url_path, url_utf8.length+1, url_utf8.str);
			}
		}

		cef_urlparts_clear(&urlparts);
		cef_string_userfree_free(url_str);
		cef_string_utf8_clear(&url_utf8);
	}

	frame->base.release(&frame->base);
	browser->base.release(&browser->base);
}

#define IS_LAUNCHER_URL(url, prefix, suffix) strStartsWith(url, prefix) && (p = strchr(url+strlen(prefix), '/')) && strstri(p+1, suffix)

void CEF_CALLBACK cefLoadOnLoadEnd(cef_load_handler_t* self, cef_browser_t* browser, cef_frame_t *frame, int httpStatusCode)
{
	CEFLoad *l = (CEFLoad*)self;
	CEFClient *c = l->client;

	if(frame->is_main(frame))
	{
		char *p = NULL;

		BrowserPageComplete(s_client->main_url);
		if(!strcmp(s_client->main_url, "about:blank") || IS_LAUNCHER_URL(s_client->main_url, URL_PREFIX_HTTP, URL_SUFFIX_LAUNCHER) ||
			IS_LAUNCHER_URL(s_client->main_url, URL_PREFIX_HTTPS, URL_SUFFIX_LAUNCHER))
		{
			PostMessage(s_client->hwnd, WM_APP, CLMSG_PAGE_LOADED, 0);
		}
	}

	frame->base.release(&frame->base);
	browser->base.release(&browser->base);
}

// This function can be used to replace load error messages (default is "error -###.  blah blah")
int CEF_CALLBACK cefLoadOnLoadError(cef_load_handler_t* self, cef_browser_t* browser, cef_frame_t *frame, enum cef_handler_errorcode_t errorCode, const cef_string_t* failedUrl, cef_string_t* errorText)
{
	// Check enumeration cef_handler_errorcode_t in cef_types.h for list of supported error codes
	char url_error_string[1024];
	char error_display[1024];
	cef_string_utf8_t error_url_utf8 = {0};

	cef_string_to_utf8(failedUrl->str, failedUrl->length, &error_url_utf8);
	Errorf("Error %d when loading url %s", errorCode, error_url_utf8.str);
	sprintf(url_error_string, FORMAT_OK(_("Error %d when loading url %s")), errorCode, error_url_utf8.str);
	sprintf(error_display, "%s<br><br>%s", BROWSER_MAIN_PAGE_ERROR_MSG_PREFIX, url_error_string);
	if( frame->is_main(frame) )
	{
		browser->stop_load(browser);
		BrowserDisplayHTMLStr(error_display);
	}
	else
	{
		//suppress the default error message so it doesn't draw over the page
		cef_string_from_utf8(" ", strlen(" "), errorText);
	}
	cef_string_utf8_clear(&error_url_utf8);

	frame->base.release(&frame->base);
	browser->base.release(&browser->base);

	return errorText->length > 0;
}

// Called on the UI thread before browser navigation. Return true (1) to
// cancel the navigation or false (0) to allow the navigation to proceed.
int CEF_CALLBACK cefRequestOnBeforeBrowse(cef_request_handler_t* self, cef_browser_t* browser, cef_frame_t *frame, cef_request_t *request, enum cef_handler_navtype_t navType, int isRedirect)
{
	cef_string_userfree_t url = request->get_url(request);
	int result;

	// Disable forward/back navigation
	if (navType == NAVTYPE_BACKFORWARD)
		result = 1;
	else
		result = cefHandleURL(url);

	cef_string_userfree_free(url);

	request->base.release(&request->base);
	frame->base.release(&frame->base);
	browser->base.release(&browser->base);

	return result;
}

// Called on the UI thread after a response to the resource request is
// received. Set |filter| if response content needs to be monitored and/or
// modified as it arrives.
void CEF_CALLBACK cefRequestOnResourceResponse(cef_request_handler_t *self, cef_browser_t *browser, const cef_string_t *url, cef_response_t *response, cef_content_filter_t** filter)
{
	int responseCode = response->get_status(response);

	if (responseCode >= 400)
	{
		cef_string_utf8_t response_url = {0};
		char* main_url = 0;

		Errorf("Received response error code %d from url %s", responseCode, response_url.str);

		cef_string_to_utf8(url->str, url->length, &response_url);
		LauncherGetLoginOrPrepatchURL(&main_url);

		if (0 == strcmp(response_url.str, main_url))
		{
			char url_error_string[1024];
			char error_display[1024];
			sprintf(url_error_string, FORMAT_OK(_("Received response error code %d from url %s")), responseCode, response_url.str);
			sprintf(error_display, "%s<br><br>%s", BROWSER_MAIN_PAGE_ERROR_MSG_PREFIX, url_error_string);
			BrowserDisplayHTMLStr(error_display);
		}

		cef_string_utf8_clear(&response_url);
		estrDestroy(&main_url);
	}

	response->base.release(&response->base);
	browser->base.release(&browser->base);
}

// Called before a context menu is displayed. Return false (0) to display the
// default context menu or true (1) to cancel the display.
int CEF_CALLBACK cefMenuOnBeforeMenu(cef_menu_handler_t *self, cef_browser_t *browser, const cef_menu_info_t *menuInfo)
{
	int retVal = !(menuInfo->typeFlags & MENUTYPE_EDITABLE || menuInfo->typeFlags & MENUTYPE_SELECTION);

	browser->base.release(&browser->base);

	return retVal;
}

// Called to optionally override the default text for a context menu item.
// |label| contains the default text and may be modified to substitute
// alternate text.
void CEF_CALLBACK cefMenuGetMenuLabel(cef_menu_handler_t* self, cef_browser_t* browser, enum cef_menu_id_t menuId, cef_string_t* label)
{
	char localizedText[256];
	bool updateLabel = true;

	switch (menuId)
	{
	// All menu options in a menu path allowed by the cefMenuOnBeforeMenu override above in the CEF 
	// file \3rdparty\cef\chromium\src\cef\libcef\browser_webview_delegate_win.cc should be included 
	// here so they can be localized.
	case MENU_ID_UNDO:			sprintf(localizedText, FORMAT_OK(_("Undo")));			break;
	case MENU_ID_REDO:			sprintf(localizedText, FORMAT_OK(_("Redo")));			break;
	case MENU_ID_CUT:			sprintf(localizedText, FORMAT_OK(_("Cut")));			break;
	case MENU_ID_COPY:			sprintf(localizedText, FORMAT_OK(_("Copy")));			break;
	case MENU_ID_PASTE:			sprintf(localizedText, FORMAT_OK(_("Paste")));			break;
	case MENU_ID_DELETE:		sprintf(localizedText, FORMAT_OK(_("Delete")));			break;
	case MENU_ID_SELECTALL:		sprintf(localizedText, FORMAT_OK(_("Select All")));		break;

	// These shouldn't be able to happen, the menu should have been canceled in cefMenuOnBeforeMenu.
	case MENU_ID_NAV_BACK:
	case MENU_ID_NAV_FORWARD:
	case MENU_ID_NAV_STOP:
	case MENU_ID_NAV_RELOAD:
	case MENU_ID_NAV_RELOAD_NOCACHE:
	case MENU_ID_PRINT:
	case MENU_ID_VIEWSOURCE:
		Errorf("Getting known bad menu button type %d with label %s", menuId, label->str);
		updateLabel = false;
		break;

	default:
		Errorf("Getting unexpected menu button type %d with label %s", menuId, label->str);
		updateLabel = false;
		break;
	}

	if (updateLabel)
	{
		cef_string_clear(label);
		cef_string_from_utf8(localizedText, strlen(localizedText), label);
	}

	browser->base.release(&browser->base);
}

// Called when an option is selected from the default context menu. Return
// false (0) to execute the default action or true (1) to cancel the action.
int CEF_CALLBACK cefMenuOnMenuAction(cef_menu_handler_t* self, cef_browser_t* browser, enum cef_menu_id_t menuId)
{
	bool disableAction = true;

	switch (menuId)
	{
	// Only allow the known okay options
	case MENU_ID_UNDO:		
	case MENU_ID_REDO:		
	case MENU_ID_CUT:		
	case MENU_ID_COPY:		
	case MENU_ID_PASTE:		
	case MENU_ID_DELETE:	
	case MENU_ID_SELECTALL:	
		disableAction = false;
		break;
	default:
		Errorf("Disabling unexpected menu action type %d", menuId);
		break;
	}

	browser->base.release(&browser->base);

	return disableAction;
}

int CEF_CALLBACK cefClientAddRef(cef_base_t* self)
{
	CEFClient *c = (CEFClient *)self;

	InterlockedIncrement(&c->ref_count);

	return c->ref_count;
}

int CEF_CALLBACK cefClientGetRefct(cef_base_t* self)
{
	CEFClient *c = (CEFClient *)self;

	return c->ref_count;
}

int CEF_CALLBACK cefClientRelease(cef_base_t* self)
{
	CEFClient *c = (CEFClient *)self;

	InterlockedDecrement(&c->ref_count);

	if(0 == c->ref_count)
	{
		if(s_client->hBitmap)
		{
			DeleteObject(s_client->hBitmap);
			s_client->hBitmap = NULL;
		}
		if(s_client->hMemoryDC)
		{
			DeleteDC(s_client->hMemoryDC);
			s_client->hMemoryDC = NULL;
		}
		if(s_client->hPopupBitmap)
		{
			DeleteObject(s_client->hPopupBitmap);
			s_client->hPopupBitmap = NULL;
		}
		if(s_client->hPopupMemoryDC)
		{
			DeleteDC(s_client->hPopupMemoryDC);
			s_client->hPopupMemoryDC = NULL;
		}

		free(c);

		return 0;
	}

	return c->ref_count;
}

CEFClient *cefCreateClient(HWND hwnd)
{
	CEFClient *client = calloc(1, sizeof(CEFClient));

	client->hwnd = hwnd;
	client->hMemoryDC = NULL;
	client->hBitmap = NULL;
	client->pPixels = NULL;
	client->hPopupMemoryDC = NULL;
	client->hPopupBitmap = NULL;
	client->pPopupPixels = NULL;
	client->ref_count = 1;

	// cef_base_t init
	client->_base.base.size = sizeof(CEFClient);
	client->_base.base.add_ref = cefClientAddRef;
	client->_base.base.get_refct = cefClientGetRefct;
	client->_base.base.release = cefClientRelease;

	// cef_client_t init
	client->_base.get_render_handler = cefGetRenderHandler;
	client->_base.get_life_span_handler = cefGetLifespanHandler;
	client->_base.get_load_handler = cefGetLoadHandler;
	client->_base.get_request_handler = cefGetRequestHandler;
	client->_base.get_menu_handler = cefGetMenuHandler;

	// cef_lifespan_handler_t init
	client->_lifespan._base.base.size = sizeof(CEFLifespan);
	client->_lifespan._base.on_before_popup = cefOnBeforePopup;
	client->_lifespan._base.on_after_created = cefOnAfterCreated;
	client->_lifespan._base.do_close = cefLifespanDoClose;
	client->_lifespan._base.on_before_close = cefLifespanOnBeforeClose;
	client->_lifespan.client = client;

	// cef_render_handler_t init
	client->_render._base.base.size = sizeof(CEFRender);
	client->_render._base.on_paint = cefOnPaint;
	client->_render._base.on_cursor_change = cefOnCursorChange;
	client->_render._base.get_screen_rect = cefGetScreenRect;
	client->_render._base.get_view_rect = cefGetViewRect;
	client->_render._base.get_screen_point = cefGetScreenPoint;
	client->_render._base.on_popup_show = cefOnPopupShow;
	client->_render._base.on_popup_size = cefOnPopupSize;
	client->_render.client = client;

	// cef_load_handler_t init
	client->_load._base.base.size = sizeof(CEFLoad);
	client->_load._base.on_load_start = cefLoadOnLoadStart;
	client->_load._base.on_load_end = cefLoadOnLoadEnd;
	client->_load._base.on_load_error = cefLoadOnLoadError;
	client->_load.client = client;

	// cef_request_handler_t init
	client->_request._base.base.size = sizeof(CEFRequest);
	client->_request._base.on_before_browse = cefRequestOnBeforeBrowse;
	client->_request._base.on_resource_response = cefRequestOnResourceResponse;
	client->_request.client = client;

	client->_menu._base.base.size = sizeof(CEFMenu);
	client->_menu._base.on_before_menu = cefMenuOnBeforeMenu;
	client->_menu._base.get_menu_label = cefMenuGetMenuLabel;
	client->_menu._base.on_menu_action = cefMenuOnMenuAction;
	client->_menu.client = client;

	return client;
}

int CEF_CALLBACK cefV8HandlerAddRef(cef_base_t* self)
{
	CEFV8Handler *v = (CEFV8Handler *)self;

	InterlockedIncrement(&v->ref_count);

	return v->ref_count;
}

int CEF_CALLBACK cefV8HandlerGetRefct(cef_base_t* self)
{
	CEFV8Handler *v = (CEFV8Handler *)self;

	return v->ref_count;
}

int CEF_CALLBACK cefV8HandlerRelease(cef_base_t* self)
{
	CEFV8Handler *v = (CEFV8Handler *)self;

	InterlockedDecrement(&v->ref_count);

	if(0 == v->ref_count)
	{
		free(v);

		return 0;
	}

	return v->ref_count;
}

int CEF_CALLBACK cefV8Handler(cef_v8handler_t* self, const cef_string_t* name, cef_v8value_t *object, size_t argumentsCount, cef_v8value_t *const *arguments,
	cef_v8value_t **retval, cef_string_t* exception)
{
	CEFV8Handler *_handler = (CEFV8Handler *)self;
	size_t i;

	_handler->onChangeCallbackFunc(_handler->userOnChangeData);

	for(i = 0; i < argumentsCount; i++)
		arguments[i]->base.release(&arguments[i]->base);
	object->base.release(&object->base);

	return 1;
}

CEFV8Handler *cefCreateV8Handler(OnChangeCallbackFunc onChangeCallbackFunc, void *userOnChangeData)
{
	CEFV8Handler *v8handler = calloc(1, sizeof(CEFV8Handler));

	v8handler->ref_count = 1;
	v8handler->onChangeCallbackFunc = onChangeCallbackFunc;
	v8handler->userOnChangeData = userOnChangeData;

	// cef_base_t init
	v8handler->_base.base.size = sizeof(CEFV8Handler);
	v8handler->_base.base.add_ref = cefV8HandlerAddRef;
	v8handler->_base.base.get_refct = cefV8HandlerGetRefct;
	v8handler->_base.base.release = cefV8HandlerRelease;

	// cef_v8handler_t init
	v8handler->_base.execute = cefV8Handler;

	return v8handler;
}

static unsigned char *cef_file_mapper(const wchar_t *filename, size_t *length)
{
	if (wcsstr(filename, L"\\locales\\en-US.pak"))
	{
		if (length)
			*length = cef_locale_pak_length;
		return cef_locale_pak;
	}
	else if (wcsstr(filename, L"\\devtools_resources.pak"))
	{
		if (length)
			*length = cef_devtools_resources_pak_length;
		return cef_devtools_resources_pak;
	}
	return 0;
}

static void cef_file_release(void* data)
{
	// This will be one of the files passed in the function above.  Because those files were 
	// allocated through LoadResource, they do not need to be freed.
	if (data != cef_locale_pak && data != cef_devtools_resources_pak)
	{
		Errorf("Unknown file being released from cef");
	}
}

static int cef_crash_report(unsigned long exc_code, struct _EXCEPTION_POINTERS *exc_info)
{
	int exc_expression = EXCEPTION_HANDLER_END_EXPR(exc_code, exc_info);

	if(exc_expression == EXCEPTION_EXECUTE_HANDLER)
	{
		EXCEPTION_HANDLER_END_RESPONSE
	}

	return exc_expression;
}

static void* cef_malloc(size_t size)
{
	return malloc(size);
}

static void* cef_realloc(void* ptr, size_t size)
{
	return realloc(ptr, size);
}

static void cef_free(void* ptr)
{
	free(ptr);
}

static size_t cef_msize(void* ptr)
{
	return GetAllocSize(ptr);
}

__declspec(dllexport) void cef_cryptic_override(cef_cryptic_override_t *override)
{
	override->cef_cryptic_version = CRYPTIC_CEF_OVERRIDE_VERSION;
	override->cef_cryptic_file_mapper = cef_file_mapper;
	override->cef_cryptic_malloc_override = cef_malloc;
	override->cef_cryptic_realloc_override = cef_realloc;
	override->cef_cryptic_free_override = cef_free;
	override->cef_cryptic_msize_override = cef_msize;
	override->cef_cryptic_file_release = cef_file_release;
	override->cef_cryptic_report_crash = cef_crash_report;
}

static void cefEscapeJavascript(char **estrEscape)
{
	estrReplaceOccurrences(estrEscape, "\\", "\\\\");
	estrReplaceOccurrences(estrEscape, "'", "\\'");
	estrReplaceOccurrences(estrEscape, "\n", "\\n");
	estrReplaceOccurrences(estrEscape, "\r", "\\r");
	estrReplaceOccurrences(estrEscape, "\t", "\\t");
}

static void cefPrintWithIndent(const char *printString, int indent)
{
	int i;
	for (i = 0; i < indent; i++)
	{
		printf(" ");
	}
	printf("%s", printString);
}

static void cefDumpValue(cef_v8value_t *val, int indent)
{
	if (val->is_array(val))
	{
		int i, numElements;
		numElements = val->get_array_length(val);

		cefPrintWithIndent("array\n", indent);
		for (i = 0; i < numElements; i++)
		{
			cef_v8value_t *array_val = val->get_value_byindex(val, i);
			cefPrintWithIndent(STACK_SPRINTF("index %d\n", i), indent+2);
			cefDumpValue(array_val, indent+4);
			array_val->base.release(&array_val->base);
		}
	}
	else if (val->is_bool(val))
	{
		cefPrintWithIndent(STACK_SPRINTF("bool of value %s\n", val->get_bool_value(val) ? "true" : "false"), indent);
	}
	else if (val->is_date(val))
	{
		cefPrintWithIndent(STACK_SPRINTF("date value\n"), indent);
	}
	else if (val->is_double(val))
	{
		cefPrintWithIndent(STACK_SPRINTF("double of value %f\n", val->get_double_value(val)), indent);
	}
	else if (val->is_function(val))
	{
		cefPrintWithIndent(STACK_SPRINTF("function value\n"), indent);
	}
	else if (val->is_int(val))
	{
		cefPrintWithIndent(STACK_SPRINTF("int of value %d\n", val->get_int_value(val)), indent);
	}
	else if (val->is_null(val))
	{
		cefPrintWithIndent(STACK_SPRINTF("null value\n"), indent);
	}
	else if (val->is_object(val))
	{
		int i, numKeys;
		cef_string_list_t keys = cef_string_list_alloc();
		cefPrintWithIndent("object\n", indent);
		val->get_keys(val, keys);
		numKeys = cef_string_list_size(keys);
		for (i = 0; i < numKeys; i++)
		{
			cef_string_t key = {0};

			if (cef_string_list_value(keys, i, &key))
			{
				cef_v8value_t *obj_val = NULL;
				cef_string_utf8_t str_utf8 = {0};
				cef_string_to_utf8(key.str, key.length, &str_utf8);
				cefPrintWithIndent(STACK_SPRINTF("key '%s'\n", str_utf8.str), indent+2);
				cef_string_utf8_clear(&str_utf8);

				obj_val = val->get_value_bykey(val, &key);
				cefDumpValue(obj_val, indent+4);
				obj_val->base.release(&obj_val->base);
			}
		}
		cef_string_list_free(keys);
	}
	else if (val->is_string(val))
	{
		cef_string_userfree_t str = val->get_string_value(val);
		if (str)
		{
			cef_string_utf8_t str_utf8 = {0};
			cef_string_to_utf8(str->str, str->length, &str_utf8);
			cefPrintWithIndent(STACK_SPRINTF("string of value '%s'\n", str_utf8.str), indent);
			cef_string_utf8_clear(&str_utf8);
			cef_string_userfree_free(str);
		}
		else
		{
			cefPrintWithIndent("string of value null\n", indent);
		}
	}
	else if (val->is_uint(val))
	{
		cefPrintWithIndent(STACK_SPRINTF("uint of value %u\n", val->get_uint_value(val)), indent);
	}
	else if (val->is_undefined(val))
	{
		cefPrintWithIndent(STACK_SPRINTF("undefined value\n"), indent);
	}
	else if (val->is_user_created(val))
	{
		cefPrintWithIndent(STACK_SPRINTF("user created value\n"), indent);
	}
	else
	{
		cefPrintWithIndent(STACK_SPRINTF("UNKNOWN value type!\n"), indent);
	}
}

static void cefFlagUnexpectedReturnType(cef_v8value_t *ret_val, const char *expected_type, const char *javascript_str, bool shouldAssert)
{
	if (shouldAssert)
	{
		cefDumpValue(ret_val, 0);
		assertmsgf(false, "libcef error: expected %s result: %s", expected_type, javascript_str);
	}
	else if(gDebugMode)
	{
		cefDumpValue(ret_val, 0);
		printf("libcef error: expected %s result: %s\n", expected_type, javascript_str);
	}
}

static void cefFlagJavaScriptException(cef_v8exception_t *ret_exc, const char *javascript_str, bool shouldAssert)
{
	cef_string_userfree_t message = ret_exc->get_message(ret_exc);
	cef_string_utf8_t message_utf8 = {0};

	cef_string_to_utf8(message->str, message->length, &message_utf8);

	if (shouldAssert)
	{
		assertmsgf(false, "JavaScript exception: %s: %s", message_utf8.str, javascript_str);
	}
	else if(gDebugMode)
	{
		printf("JavaScript exception: %s: %s\n", message_utf8.str, javascript_str);
	}

	cef_string_utf8_clear(&message_utf8);
	cef_string_userfree_free(message);
}

static char *cefEvalJavaScriptString(bool assertOnFail, const char *javascript_format, ...)
{
	char *result = NULL;
	char *estrJavaScript = NULL;
	cef_string_t cef_javascript_str = {0};
	cef_frame_t *frame = s_client->browser->get_main_frame(s_client->browser);
	cef_v8context_t *context = frame->get_v8context(frame);

	VA_START(va, javascript_format);
	estrConcatfv(&estrJavaScript, javascript_format, va);
	VA_END();
	cef_string_from_utf8(estrJavaScript, estrLength(&estrJavaScript), &cef_javascript_str);

	if (context->enter(context))
	{
		cef_v8exception_t *ret_exc = NULL;
		cef_v8value_t *ret_val = NULL;

		if (context->eval(context, &cef_javascript_str, &ret_val, &ret_exc))
		{
			if (ret_val->is_string(ret_val))
			{
				cef_string_userfree_t text = ret_val->get_string_value(ret_val);
				if (text)
				{
					cef_string_utf8_t text_utf8 = {0};

					cef_string_to_utf8(text->str, text->length, &text_utf8);
					cef_string_userfree_free(text);

					result = (char *)GlobalAlloc(GMEM_FIXED, text_utf8.length + 1);
					strcpy_s(result, text_utf8.length + 1, text_utf8.str);

					cef_string_utf8_clear(&text_utf8);
				}
				else
				{
					result = (char *)GlobalAlloc(GMEM_FIXED, 1);
					strcpy_s(result, 1, "");
				}
			}
			else if (!ret_val->is_null(ret_val))
			{
				cefFlagUnexpectedReturnType(ret_val, "string or null", estrJavaScript, assertOnFail);
			}

			ret_val->base.release(&ret_val->base);
		}
		else
		{
			cefFlagJavaScriptException(ret_exc, estrJavaScript, assertOnFail);
			ret_exc->base.release(&ret_exc->base);
		}

		context->exit(context);
	}

	cef_string_clear(&cef_javascript_str);
	estrDestroy(&estrJavaScript);

	context->base.release(&context->base);
	frame->base.release(&frame->base);

	return result;
}

static bool cefEvalJavaScriptBool(bool assertOnFail, const char *javascript_format, ...)
{
	bool result = false;
	char *estrJavaScript = NULL;
	cef_string_t cef_javascript_str = {0};
	cef_frame_t *frame = s_client->browser->get_main_frame(s_client->browser);
	cef_v8context_t *context = frame->get_v8context(frame);

	VA_START(va, javascript_format);
	estrConcatfv(&estrJavaScript, javascript_format, va);
	VA_END();
	cef_string_from_utf8(estrJavaScript, estrLength(&estrJavaScript), &cef_javascript_str);

	if (context->enter(context))
	{
		cef_v8exception_t *ret_exc = NULL;
		cef_v8value_t *ret_val = NULL;

		if (context->eval(context, &cef_javascript_str, &ret_val, &ret_exc))
		{
			if (ret_val->is_bool(ret_val))
			{
				result = !!ret_val->get_bool_value(ret_val);
			}
			else
			{
				cefFlagUnexpectedReturnType(ret_val, "bool", estrJavaScript, assertOnFail);
			}

			ret_val->base.release(&ret_val->base);
		}
		else
		{
			cefFlagJavaScriptException(ret_exc, estrJavaScript, assertOnFail);
			ret_exc->base.release(&ret_exc->base);
		}

		context->exit(context);
	}

	cef_string_clear(&cef_javascript_str);
	estrDestroy(&estrJavaScript);

	context->base.release(&context->base);
	frame->base.release(&frame->base);

	return result;
}

static void cefEvalJavaScript(bool assertOnFail, const char *javascript_format, ...)
{
	char *estrJavaScript = NULL;
	cef_string_t cef_javascript_str = {0};
	cef_frame_t *frame = s_client->browser->get_main_frame(s_client->browser);
	cef_v8context_t *context = frame->get_v8context(frame);

	VA_START(va, javascript_format);
	estrConcatfv(&estrJavaScript, javascript_format, va);
	VA_END();
	cef_string_from_utf8(estrJavaScript, estrLength(&estrJavaScript), &cef_javascript_str);

	if (context->enter(context))
	{
		cef_v8exception_t *ret_exc = NULL;
		cef_v8value_t *ret_val = NULL;

		if (context->eval(context, &cef_javascript_str, &ret_val, &ret_exc))
		{
			ret_val->base.release(&ret_val->base);
		}
		else
		{
			cefFlagJavaScriptException(ret_exc, estrJavaScript, assertOnFail);
			ret_exc->base.release(&ret_exc->base);
		}

		context->exit(context);
	}

	cef_string_clear(&cef_javascript_str);
	estrDestroy(&estrJavaScript);

	context->base.release(&context->base);
	frame->base.release(&frame->base);
}

static bool loadResource(int source_resource_id, void **loaded_data, size_t *loaded_data_size)
{
	bool retVal = false;

	HRSRC rsrc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(source_resource_id), L"FILE");
	if (rsrc)
	{
		HGLOBAL gptr = LoadResource(GetModuleHandle(NULL), rsrc);
		if (gptr)
		{
			*loaded_data = LockResource(gptr);
			*loaded_data_size = SizeofResource(GetModuleHandle(NULL), rsrc);
			retVal = true;
		}
		else
		{
			Errorf("Cannot load resource ID %d!", source_resource_id);
		}
	}
	else
	{
		Errorf("Cannot find resource ID %d!", source_resource_id);
	}

	return retVal;
}

#endif // #if ENABLE_LIBCEF

static int forceD3DLauncher = 0;
AUTO_CMD_INT(forceD3DLauncher, forceD3DLauncher);

bool BrowserLibCEFInit(HWND windowHandle)
{
#if ENABLE_LIBCEF
	if(!sbLibCEFInited)
	{
		char libcefDLL[MAX_PATH];

		// Load CEF DLL
		fileGetcwd(SAFESTR(libcefDLL));
		strcat(libcefDLL, "/libcef.dll");
		if(LoadLibrary_UTF8(libcefDLL))
		{
			// Attempt to load required CEF data files.
			// The Cryptic absolute paths are for backward-compatibility.
			if(loadResource(IDR_LIBCEF_RES_US, &cef_locale_pak, &cef_locale_pak_length))
			{
				if(loadResource(IDR_LIBCEF_DEV_RES, &cef_devtools_resources_pak, &cef_devtools_resources_pak_length))
				{
					cef_settings_t settings = {0};
					char cache_path[CRYPTIC_MAX_PATH];

					// Set cache path local to the working directory
					fileGetcwd(SAFESTR(cache_path));
					strncat_s(SAFESTR(cache_path), SAFESTR("\\cef_cache"));
					cef_string_from_utf8(cache_path, strlen(cache_path), &settings.cache_path);

					if (cef_initialize(&settings, NULL))
					{
						cef_window_info_t window_info = {0};
						cef_browser_settings_t browser_settings = {0};

						s_client = cefCreateClient(windowHandle);

						window_info.m_hWndParent = windowHandle;
						window_info.m_bWindowRenderingDisabled = true;
						window_info.m_bTransparentPainting = true;

						cef_browser_create(&window_info, (cef_client_t *)s_client, NULL, &browser_settings);

						cef_do_message_loop_work();

						sbLibCEFInited = true;
					}
					else
					{
						assertmsg(false, "cef_initialize returned failure.");
					}
				}
				else
				{
					assertmsg(false, "Failed to load dev tools .pak file.");
				}
			}
			else
			{
				assertmsg(false, "Failed to load locale .pak file.");
			}
		}
		else
		{
			assertmsg(false, "Failed to load libcef.dll.");
		}
	}

	if(getIsTransgaming() || forceD3DLauncher) {

		D3DPRESENT_PARAMETERS presentParams = {0};
		s_d3d9 = Direct3DCreate9(D3D_SDK_VERSION);

		if(s_d3d9) {

			RECT rect = {0};
			UINT i = 0;
			UINT modeCount = 0;
			UINT desiredWidth = 0;
			UINT desiredHeight = 0;
			UINT smallestFittingWidth = 0;
			UINT smallestFittingHeight = 0;
			D3DDISPLAYMODE mode = {0};

			// Find a fullscreen mode that most closely matches whatever window size we're
			// using without going under that size.
			GetClientRect(windowHandle, &rect);
			desiredWidth = rect.right - rect.left;
			desiredHeight = rect.bottom - rect.top;
			modeCount = IDirect3D9_GetAdapterModeCount(s_d3d9, 0, D3DFMT_X8R8G8B8);

			for(i = 0; i < modeCount; i++) {

				IDirect3D9_EnumAdapterModes(
					s_d3d9, 0, D3DFMT_X8R8G8B8,
					i, &mode);

				if(mode.Height >= desiredHeight && mode.Width >= desiredWidth) {
					if((mode.Width < smallestFittingWidth || !smallestFittingWidth) &&
					   (mode.Height < smallestFittingHeight || !smallestFittingHeight)) {
						smallestFittingWidth = mode.Width;
						smallestFittingHeight = mode.Height;
					}
				}

			}

			presentParams.BackBufferWidth = smallestFittingWidth;
			presentParams.BackBufferHeight = smallestFittingHeight;
			presentParams.Windowed = !getIsTransgaming();
			presentParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
			presentParams.hDeviceWindow = windowHandle;
			presentParams.BackBufferFormat = D3DFMT_X8R8G8B8;

			TGSetSkipFullScreenStateSave();
			TGStartFullscreen(false);

			assert(
				IDirect3D9_CreateDevice(
					s_d3d9,
					D3DADAPTER_DEFAULT,
					D3DDEVTYPE_HAL,
					windowHandle,
					D3DCREATE_SOFTWARE_VERTEXPROCESSING,
					&presentParams,
					&s_d3dDevice) == D3D_OK);

			// Resize the browser to match whatever fullscreen size we're going with.
			s_client->browser->set_size(
				s_client->browser, PET_VIEW,
				presentParams.BackBufferWidth,
				presentParams.BackBufferHeight);

			// Create our one texture to copy the browser screen into.
			assert(
				IDirect3DDevice9_CreateTexture(
					s_d3dDevice,
					presentParams.BackBufferWidth,
					presentParams.BackBufferHeight,
					1,
					D3DUSAGE_DYNAMIC,
					D3DFMT_X8R8G8B8,
					D3DPOOL_DEFAULT,
					&s_d3dScreenCopyTexture, NULL) == D3D_OK);
		}
	}

#endif // #if ENABLE_LIBCEF

	return sbLibCEFInited;
}

bool BrowserLibCEFShutdown(void)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(s_d3dScreenCopyTexture) {
			IDirect3DTexture9_Release(s_d3dScreenCopyTexture);
			s_d3dScreenCopyTexture = NULL;
		}

		if(s_d3dDevice) {
			IDirect3DDevice9_Release(s_d3dDevice);
			s_d3dDevice = NULL;
		}

		cef_shutdown();

		sbLibCEFInited = false;

		return true;
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

HWND BrowserLibCEFGetHtmlDocHWND(void)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		return s_client->hwnd;
#endif // #if ENABLE_LIBCEF
	}
	return NULL;
}

bool BrowserLibCEFGetCurrentURL(char **currentURLOut)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(currentURLOut)
		{
			size_t sz = strlen(s_client->main_url_path) + 1;
			(*currentURLOut) = GlobalAlloc(GMEM_FIXED, sz);
			if(*currentURLOut)
			{
				strcpy_s((*currentURLOut), sz, s_client->main_url_path);

				return true;
			}
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

bool BrowserLibCEFGetElementInnerHTML(const char *elementName, char **msgOut)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(elementName && msgOut)
		{
			(*msgOut) = cefEvalJavaScriptString(true, "var el = document.getElementById('%s'); el ? el.innerHTML : null;", elementName);
			return !!(*msgOut);
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

bool BrowserLibCEFSetElementInnerHTML(const char *elementName, const char *msgIn)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(elementName && msgIn)
		{
			bool result;

			char *estrMsgIn = estrCreateFromStr(msgIn);
			cefEscapeJavascript(&estrMsgIn);
			result = cefEvalJavaScriptBool(true, "var el = document.getElementById('%s'); if(el) el.innerHTML = '%s'; el ? true : false;", elementName, estrMsgIn);
			estrDestroy(&estrMsgIn);

			return result;
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

bool BrowserLibCEFSetElementClassName(const char *elementName, const char *classNameIn)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(elementName && classNameIn)
		{
			return cefEvalJavaScriptBool(true, "var el = document.getElementById('%s'); if(el) el.className = '%s'; el ? true : false;", elementName, classNameIn);
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

bool BrowserLibCEFSetElementCSSText(const char *elementName, const char *cssText)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(elementName && cssText)
		{
			char css[MAX_PATH];
			char *curPos = NULL;
			char *cssAttr;
			char *cssVal;

			strcpy(css, cssText);

			cssAttr = strtok_s(css, ":", &curPos);
			cssVal = strtok_s(NULL, ":", &curPos);

			return cefEvalJavaScriptBool(true, "var el = document.getElementById('%s'); if(el) el.style.%s = '%s'; el ? true : false;", elementName, cssAttr, NULL_TO_EMPTY(cssVal));
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

bool BrowserLibCEFGetInputElementValue(const char *inputElementName, const char **valueTextOut)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(inputElementName && valueTextOut)
		{
			(*valueTextOut) = cefEvalJavaScriptString(true, "var el = document.getElementById('%s'); el ? el.value : null;", inputElementName);
			return !!(*valueTextOut);
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

bool BrowserLibCEFSetInputElementValue(const char *inputElementName, const char *valueTextIn, bool assertOnFail)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(inputElementName && valueTextIn)
		{
			char *estrValueTextIn = estrCreateFromStr(valueTextIn);
			cefEscapeJavascript(&estrValueTextIn);
			return cefEvalJavaScriptBool(assertOnFail, "var el = document.getElementById('%s'); if(el) el.value = '%s'; el ? true : false;", inputElementName, estrValueTextIn);
			estrDestroy(&estrValueTextIn);
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

bool BrowserLibCEFSetSelectElementValue(const char *selectElementName, const char *valueTextIn)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(selectElementName && valueTextIn)
		{
			return cefEvalJavaScriptBool(true, "var el = document.getElementById('%s'); if(el) el.value = '%s'; el ? el.value == '%s' : false;", selectElementName, valueTextIn, valueTextIn);
		}
#endif // #if ENABLE_LIBCFE
	}

	return false;
}

// returns number of options added, or -1 on failure
int BrowserLibCEFSetSelectElementOptions(const char *selectElementName, OptionCallbackFunc optionCallbackFunc, void *userOptionData)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(selectElementName && optionCallbackFunc)
		{
			int userOptionIndex = 0;
			bool bShouldInsert = false;
			char optionValue[MAX_PATH];
			char optionText[MAX_PATH];
			int optionsAdded = 0;

			bool exists = cefEvalJavaScriptBool(true, "var el = document.getElementById('%s'); el ? true : false;", selectElementName);
			if(!exists)
				return -1;

			cefEvalJavaScript(true, "document.getElementById('%s').innerHTML = ''", selectElementName);

			while (optionCallbackFunc(userOptionData, userOptionIndex, &bShouldInsert, optionValue, sizeof(optionValue), optionText, sizeof(optionText)))
			{
				if (bShouldInsert)
				{
					cefEvalJavaScript(true, "var o = document.createElement('option'); o.value = '%s'; o.text = '%s'; document.getElementById('%s').add(o);",
						optionValue, optionText, selectElementName);
					optionsAdded++;
				}
				userOptionIndex++;
			}

			return optionsAdded;
		}
#endif // #if ENABLE_LIBCEF
	}

	return -1;
}

bool BrowserLibCEFGetSelectElementValue(const char *selectElementName, const char **valueTextOut)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(selectElementName && valueTextOut)
		{
			(*valueTextOut) = cefEvalJavaScriptString(true, "var el = document.getElementById('%s'); el ? el.value : null;", selectElementName);
			return !!(*valueTextOut);
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

bool BrowserLibCEFSetSelectElementOnChangeCallback(const char *selectElementName, OnChangeCallbackFunc onChangeCallbackFunc, void *userOnChangeData)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(selectElementName && onChangeCallbackFunc)
		{
			cef_frame_t *frame = s_client->browser->get_main_frame(s_client->browser);
			cef_v8context_t *context = frame->get_v8context(frame);
			cef_v8value_t *global = context->get_global(context);
			char *estrFnName = NULL;

			bool exists = cefEvalJavaScriptBool(true, "var el = document.getElementById('%s'); el ? true : false;", selectElementName);
			if(!exists)
				return false;

			if(context->enter(context))
			{
				cef_string_t fn_name = {0};
				cef_v8value_t *function;
				CEFV8Handler *v8handler = cefCreateV8Handler(onChangeCallbackFunc, userOnChangeData);

				estrPrintf(&estrFnName, "cryptic_launcher_%s_onchange", selectElementName);
				cef_string_from_utf8(estrFnName, estrLength(&estrFnName), &fn_name);

				function = cef_v8value_create_function(&fn_name, &v8handler->_base);

				global->set_value_bykey(global, &fn_name, function, V8_PROPERTY_ATTRIBUTE_NONE);

				context->exit(context);

				cef_string_clear(&fn_name);
			}

			cefEvalJavaScript(true, "document.getElementById('%s').onchange = function() { window.%s() };", selectElementName, estrFnName);

			estrDestroy(&estrFnName);

			global->base.release(&global->base);
			context->base.release(&context->base);
			frame->base.release(&frame->base);

			return true;
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

bool BrowserLibCEFFocusElement(const char *elementName)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(elementName)
		{
			s_client->browser->send_focus_event(s_client->browser, 1);
			return cefEvalJavaScriptBool(true, "var el = document.getElementById('%s'); if(el) el.focus(); el ? true : false", elementName);
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

bool BrowserLibCEFExistsElement(const char *elementName)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(elementName)
		{
			return cefEvalJavaScriptBool(true, "document.getElementById('%s') != null;", elementName);
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

// IMPORTANT
//
// Chromium needs all messages to be translated and dispatched and not sent to IsDialogMessage. Otherwise, messages would frequently not
// make its way to the window. IE did not need this behavior.
//
BOOL BrowserLibCEFMessageCallback(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(pWindow->eWindowType == CL_WINDOW_MAIN && hDlg == BrowserGetHtmlDocHWND())
		{
			if(iMsg >= WM_KEYDOWN && iMsg <= WM_KEYLAST)
			{
				bool bInLoggedInState = LauncherIsInLoggedInState();

				if(wParam == VK_TAB || wParam == VK_RETURN)
				{
					pWindow->pDialogCB(hDlg, iMsg, wParam, lParam, pWindow);
					return TRUE;
				}
			}

			{
				MSG msg;

				msg.hwnd = hDlg;
				msg.message = iMsg;
				msg.wParam = wParam;
				msg.lParam = lParam;
				msg.time = pWindow->iLastMessageTime;

				TranslateMessage(&msg);
				DispatchMessage(&msg);

				return TRUE;
			}
		}
#endif // #if ENABLE_LIBCEF
	}

	return FALSE;
}

bool BrowserLibCEFProcessKeystrokes(MSG msg, SimpleWindow *pWindow)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		switch(msg.message)
		{
			case WM_KEYDOWN:
			case WM_KEYUP:
			case WM_SYSKEYDOWN:
			case WM_SYSKEYUP:
			case WM_CHAR:
			case WM_SYSCHAR:
			case WM_IME_CHAR:
			{
				enum cef_key_type_t type;
				cef_key_info_t key_info = {0};
				key_info.key = msg.wParam;

				if(msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN)
					type = KT_KEYDOWN;
				else if(msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP)
					type = KT_KEYUP;
				else
					type = KT_CHAR;

				if(msg.message == WM_SYSKEYDOWN || msg.message == WM_SYSKEYUP || msg.message == WM_SYSCHAR)
					key_info.sysChar = true;

				if(msg.message == WM_IME_CHAR)
					key_info.imeChar = true;

				s_client->browser->send_key_event(s_client->browser, type, &key_info, msg.lParam);

				return true;
			}
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

bool BrowserLibCEFProcessMouse(MSG msg)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		// See https://code.google.com/p/chromiumembedded/source/browse/trunk/cef1/tests/cefclient/osrplugin.cpp
		// It is their test offscreen-rendering client.

		static bool mouseTracking = false;

		switch(msg.message)
		{
			case WM_LBUTTONDOWN:
			case WM_RBUTTONDOWN:
			case WM_MBUTTONDOWN:
				SetCapture(msg.hwnd);
				SetFocus(msg.hwnd);
				s_client->browser->send_mouse_click_event(s_client->browser, LOWORD(msg.lParam), HIWORD(msg.lParam),
					(msg.message == WM_LBUTTONDOWN ? MBT_LEFT : ((msg.message == WM_RBUTTONDOWN ? MBT_RIGHT : MBT_MIDDLE))),
					/*mouseUp=*/false, /*clickCount=*/1);
				return true;
			case WM_LBUTTONDBLCLK:
			case WM_RBUTTONDBLCLK:
			case WM_MBUTTONDBLCLK:
				SetCapture(msg.hwnd);
				SetFocus(msg.hwnd);
				s_client->browser->send_mouse_click_event(s_client->browser, LOWORD(msg.lParam), HIWORD(msg.lParam),
					(msg.message == WM_LBUTTONDBLCLK ? MBT_LEFT : ((msg.message == WM_RBUTTONDBLCLK ? MBT_RIGHT : MBT_MIDDLE))),
					/*mouseUp=*/false, /*clickCount=*/2);
				return true;
			case WM_LBUTTONUP:
			case WM_RBUTTONUP:
			case WM_MBUTTONUP:
				if(GetCapture() == msg.hwnd)
					ReleaseCapture();
				s_client->browser->send_mouse_click_event(s_client->browser, LOWORD(msg.lParam), HIWORD(msg.lParam),
					(msg.message == WM_LBUTTONUP ? MBT_LEFT : ((msg.message == WM_RBUTTONUP ? MBT_RIGHT : MBT_MIDDLE))),
					/*mouseUp=*/true, /*clickCount=*/1);
				return true;
			case WM_MOUSEMOVE:
				if(!mouseTracking)
				{
					// Start tracking mouse leave. Required for the WM_MOUSELEAVE event to
					// be generated.
					TRACKMOUSEEVENT tme;
					tme.cbSize = sizeof(TRACKMOUSEEVENT);
					tme.dwFlags = TME_LEAVE;
					tme.hwndTrack = msg.hwnd;
					TrackMouseEvent(&tme);
					mouseTracking = true;
				}
				s_client->browser->send_mouse_move_event(s_client->browser, LOWORD(msg.lParam), HIWORD(msg.lParam), /*leave=*/false);
				return true;
			case WM_MOUSELEAVE:
				if(mouseTracking)
				{
					// Stop tracking mouse leave.
					TRACKMOUSEEVENT tme;
					tme.cbSize = sizeof(TRACKMOUSEEVENT);
					tme.dwFlags = TME_LEAVE & TME_CANCEL;
					tme.hwndTrack = msg.hwnd;
					TrackMouseEvent(&tme);
					mouseTracking = false;
				}
				s_client->browser->send_mouse_move_event(s_client->browser, 0, 0, /*leave=*/true);
				return true;
			case WM_MOUSEWHEEL:
				{
					// WM_MOUSEWHEEL event passes coordinates as screen coordinates 
					// rather than client coordinates.  CEF expects client coordinates.
					POINTS screenCoord = MAKEPOINTS(msg.lParam);
					POINT clientCoord;
					POINTSTOPOINT(clientCoord, screenCoord);
					ScreenToClient(msg.hwnd, &clientCoord);
					s_client->browser->send_mouse_wheel_event(s_client->browser, clientCoord.x, clientCoord.y, /*deltaX=*/0, GET_WHEEL_DELTA_WPARAM(msg.wParam));
				}
				return true;
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

bool BrowserLibCEFProcessFocus(MSG msg)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		// See https://code.google.com/p/chromiumembedded/source/browse/trunk/cef1/tests/cefclient/osrplugin.cpp
		// It is their test offscreen-rendering client.

		switch(msg.message)
		{
			case WM_SETFOCUS:
			case WM_KILLFOCUS:
				s_client->browser->send_focus_event(s_client->browser, msg.message == WM_SETFOCUS);
				return true;
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

bool BrowserLibCEFInvokeScript(const char *scriptName, InvokeScriptArgType firstType, va_list args)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(scriptName)
		{
			char **eaEstrArgs = NULL;
			char *estrJavaScriptArgs = NULL;
			InvokeScriptArgType currentType = firstType;
			while (currentType != INVOKE_SCRIPT_ARG_NULL)
			{
				char *estrVar = NULL;
				switch (currentType)
				{
					case INVOKE_SCRIPT_ARG_STRING:
						{
							estrPrintf(&estrVar, "'%s'", va_arg(args, char *));
							break;
						}
					case INVOKE_SCRIPT_ARG_STRING_OBJ:
						{
							estrPrintf(&estrVar, "%s", va_arg(args, char *));
							break;
						}
					case INVOKE_SCRIPT_ARG_INT:
						{
							estrPrintf(&estrVar, "%d", va_arg(args, S32));
							break;
						}
				}

				eaPush(&eaEstrArgs, estrVar);
				currentType = va_arg(args, InvokeScriptArgType);
			}

			estrConcatSeparatedStringEarray(&estrJavaScriptArgs, &eaEstrArgs, ", ");

			cefEvalJavaScript(true, "%s(%s)", scriptName, estrJavaScriptArgs);

			estrDestroy(&estrJavaScriptArgs);
			eaDestroyEString(&eaEstrArgs);
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

char *BrowserLibCEFInvokeScriptString(const char *scriptName, InvokeScriptArgType firstType, va_list args)
{
	char *result = NULL;

	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(scriptName)
		{
			char **eaEstrArgs = NULL;
			char *estrJavaScriptArgs = NULL;
			InvokeScriptArgType currentType = firstType;
			while (currentType != INVOKE_SCRIPT_ARG_NULL)
			{
				char *estrVar = NULL;
				switch (currentType)
				{
				case INVOKE_SCRIPT_ARG_STRING:
					{
						estrPrintf(&estrVar, "'%s'", va_arg(args, char *));
						break;
					}
				case INVOKE_SCRIPT_ARG_STRING_OBJ:
					{
						estrPrintf(&estrVar, "%s", va_arg(args, char *));
						break;
					}
				case INVOKE_SCRIPT_ARG_INT:
					{
						estrPrintf(&estrVar, "%d", va_arg(args, S32));
						break;
					}
				}

				eaPush(&eaEstrArgs, estrVar);
				currentType = va_arg(args, InvokeScriptArgType);
			}

			estrConcatSeparatedStringEarray(&estrJavaScriptArgs, &eaEstrArgs, ", ");

			if (estrJavaScriptArgs)
			{
				result = cefEvalJavaScriptString(true, "%s('%s');", scriptName, estrJavaScriptArgs);
			}
			else
			{
				result = cefEvalJavaScriptString(true, "%s();", scriptName);
			}

			estrDestroy(&estrJavaScriptArgs);
			eaDestroyEString(&eaEstrArgs);
		}
#endif // #if ENABLE_LIBCEF
	}

	return result;
}

bool BrowserLibCEFDisplayHTMLFromURL(const char *webPageURL, const char **eaEstrKeyValuePostData)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(webPageURL)
		{
			cef_frame_t *frame = s_client->browser->get_main_frame(s_client->browser);
			cef_request_t *request = cef_request_create();
			cef_string_multimap_t headers = cef_string_multimap_alloc();
			cef_string_t url = {0};

			char **eaEstrHeaderKeyValues = NULL;
			int i;

			cef_string_from_utf8(webPageURL, strlen(webPageURL), &url);

			request->set_url(request, &url);

			if(eaEstrKeyValuePostData)
			{
				cef_post_data_t *post_data = cef_post_data_create();
				cef_post_data_element_t *post_data_element = cef_post_data_element_create();
				char *estrPostData = NULL;

				for(i = 1; i < eaSize(&eaEstrKeyValuePostData); i += 2)
				{
					const char *estrKey = eaEstrKeyValuePostData[i - 1];
					const char *estrValue = eaEstrKeyValuePostData[i];
					if(i > 1)
						estrAppend2(&estrPostData, "&");
					estrConcatf(&estrPostData, "%s=%s", estrKey, estrValue);
				}

				post_data_element->set_to_bytes(post_data_element, estrLength(&estrPostData), estrPostData);

				post_data->add_element(post_data, post_data_element);

				request->set_post_data(request, post_data);

				eaPush(&eaEstrHeaderKeyValues, estrCreateFromStr("Content-Type"));
				eaPush(&eaEstrHeaderKeyValues, estrCreateFromStr("application/x-www-form-urlencoded"));
			}

			BrowserAppendStandardDataToHeader(&eaEstrHeaderKeyValues);
			for(i = 1; i < eaSize(&eaEstrHeaderKeyValues); i += 2)
			{
				char *estrKey = eaEstrHeaderKeyValues[i - 1];
				char *estrValue = eaEstrHeaderKeyValues[i];

				cef_string_t key = {0};
				cef_string_t value = {0};

				cef_string_from_utf8(estrKey, estrLength(&estrKey), &key);
				cef_string_from_utf8(estrValue, estrLength(&estrValue), &value);

				cef_string_multimap_append(headers, &key, &value);

				cef_string_clear(&value);
				cef_string_clear(&key);
			}
			eaDestroyEString(&eaEstrHeaderKeyValues);

			request->set_header_map(request, headers);

			frame->load_request(frame, request);

			frame->base.release(&frame->base);

			cef_string_multimap_free(headers);
			cef_string_clear(&url);

			return true;
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

bool BrowserLibCEFDisplayHTMLStr(const char *htmlString)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(htmlString)
		{
			static char *url = "about:blank";

			cef_frame_t *frame = s_client->browser->get_main_frame(s_client->browser);

			char *estrHtmlDcument = NULL;
			cef_string_t url_str = {0};
			cef_string_t html_str = {0};

			estrPrintf(&estrHtmlDcument, "<html><head></head><body style='background-color:black'><p style=\"color:white\">%s</p></body></html>", htmlString);
			cef_string_from_utf8(url, strlen(url), &url_str);
			cef_string_from_utf8(estrHtmlDcument, estrLength(&estrHtmlDcument), &html_str);

			frame->load_string(frame, &html_str, &url_str);

			cef_string_clear(&html_str);
			cef_string_clear(&url_str);
			estrDestroy(&estrHtmlDcument);

			frame->base.release(&frame->base);

			return true;
		}
#endif // #if ENABLE_LIBCEF
	}

	return false;
}

void BrowserLibCEFUpdate()
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		cef_do_message_loop_work();
#endif // #if ENABLE_LIBCEF
	}
}

// IMPORTANT
//
// cefOnPaint is only invoked by chromium when the document state changes in a way to require an offscreen render.
// We store the DIB section bitmap for the window and any active popup (i.e. drop-down box) and render it to the window
// in response to WM_PAINT. The IE version did not need this behavior because it paints directly to the window and presumably
// manages WM_PAINT on its own.
//
bool BrowserLibCEFPaint(HWND hwnd)
{
	if(sbLibCEFInited)
	{
#if ENABLE_LIBCEF
		if(hwnd == s_client->hwnd && s_client->hMemoryDC)
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hwnd, &ps);

			BitBlt(hDC, 0, 0, s_client->browserWidth, s_client->browserHeight, s_client->hMemoryDC, 0, 0, SRCCOPY);
			if(s_client->popup_rect.width > 0 && s_client->popup_rect.height > 0)
				BitBlt(hDC, s_client->popup_rect.x, s_client->popup_rect.y, s_client->popup_rect.width, s_client->popup_rect.height, s_client->hPopupMemoryDC, 0, 0, SRCCOPY);

			EndPaint(hwnd, &ps);
			return true;
		}
#endif // #if ENABLE_LIBCEF
	}
	return false;
}
