// CrypticLauncher
#include "browser_ie.h"
#include "UI.h"
#include "LauncherMain.h"

// UtilitiesLib
#include "wininclude.h"
#include "EString.h"
#include "earray.h"
#include "trivia.h"
#include "error.h"
#include "utils.h"

#include "SimpleWindowManager.h"
#include "url.h"
#include "StringUtil.h"

// Windows / Microsoft
#include <mshtml.h>		/* Defines of stuff like IHTMLDocument2. This is an include file with Visual C 6 and above */
#include <ExDisp.h>
#include <OAIdl.h>
#include <MsHtmHst.h>
#include <ExDispid.h>
#include <Ole2.h>

#define DISPATCH_CALLBACK_SELECT_ELEMENT_ON_CHANGE	1

//typedef struct
//{
//	NMHDR			nmhdr;
//	IHTMLEventObj *	htmlEvent;
//	LPCTSTR			eventStr;
//}
//WEBPARAMS; 

// Our _IDispatchEx struct. This is just an IDispatch with some
// extra fields appended to it for our use in storing extra
// info we need for the purpose of reacting to events that happen
// to some element on a web page.
typedef struct {
	IDispatch		dispatchObj;	// The mandatory IDispatch.
	DWORD			refCount;		// Our reference count.
	IHTMLWindow2   *htmlWindow2;	// Where we store the IHTMLWindow2 so that our IDispatch's Invoke() can get it.
	HWND			hwnd;			// The window hosting the browser page. Our IDispatch's Invoke() sends messages when an event of interest occurs.
	short			id;				// Any numeric value of your choosing that you wish to associate with this IDispatch.
	void		   *callback;
	void		   *userdata;		// An extra pointer.
} _IDispatchEx;

typedef enum WaitOnReadyStateType
{
	WORS_SUCCESS = 0,
	WORS_TIMEOUT = -1,
	WORS_DESTROYED = -2,
	WORS_ERROR = -3,
}
WaitOnReadyStateType;

static IWebBrowser2 *sWebBrowser = NULL;
static HWND sWindowHandle = NULL;

static BSTR bstrFromNullTermString(const char *string); // must SysFreeString() the returned BSTR
static char *nullTermStringFromBstr(BSTR bstr); // must GlobalFree() the returned char*
static IHTMLElement *getElement(const char *elementName);
static void OutputDetailedElementError(IHTMLElement *elem, const char *type);
static IHTMLInputElement *getInputElement(const char *inputElementName);
static IHTMLSelectElement *getSelectElement(const char *selectElementName);
static void extractTriviaData(IOmNavigator *navigator, HRESULT (_stdcall *getter) (IOmNavigator*, BSTR*), char *key);
static WaitOnReadyStateType WaitOnReadyState(READYSTATE rs, DWORD timeout);

// Our IOleInPlaceFrame functions that the browser may call
static HRESULT STDMETHODCALLTYPE Frame_QueryInterface(IOleInPlaceFrame *, REFIID, LPVOID *);
static HRESULT STDMETHODCALLTYPE Frame_AddRef(IOleInPlaceFrame *);
static HRESULT STDMETHODCALLTYPE Frame_Release(IOleInPlaceFrame *);
static HRESULT STDMETHODCALLTYPE Frame_GetWindow(IOleInPlaceFrame *, HWND *);
static HRESULT STDMETHODCALLTYPE Frame_ContextSensitiveHelp(IOleInPlaceFrame *, BOOL);
static HRESULT STDMETHODCALLTYPE Frame_GetBorder(IOleInPlaceFrame *, LPRECT);
static HRESULT STDMETHODCALLTYPE Frame_RequestBorderSpace(IOleInPlaceFrame *, LPCBORDERWIDTHS);
static HRESULT STDMETHODCALLTYPE Frame_SetBorderSpace(IOleInPlaceFrame *, LPCBORDERWIDTHS);
static HRESULT STDMETHODCALLTYPE Frame_SetActiveObject(IOleInPlaceFrame *, IOleInPlaceActiveObject *, LPCOLESTR);
static HRESULT STDMETHODCALLTYPE Frame_InsertMenus(IOleInPlaceFrame *, HMENU, LPOLEMENUGROUPWIDTHS);
static HRESULT STDMETHODCALLTYPE Frame_SetMenu(IOleInPlaceFrame *, HMENU, HOLEMENU, HWND);
static HRESULT STDMETHODCALLTYPE Frame_RemoveMenus(IOleInPlaceFrame *, HMENU);
static HRESULT STDMETHODCALLTYPE Frame_SetStatusText(IOleInPlaceFrame *, LPCOLESTR);
static HRESULT STDMETHODCALLTYPE Frame_EnableModeless(IOleInPlaceFrame *, BOOL);
static HRESULT STDMETHODCALLTYPE Frame_TranslateAccelerator(IOleInPlaceFrame *, LPMSG, WORD);

// Our IOleInPlaceFrame VTable. This is the array of pointers to the above functions in our C
// program that the browser may call in order to interact with our frame window that contains
// the browser object. We must define a particular set of functions that comprise the
// IOleInPlaceFrame set of functions (see above), and then stuff pointers to those functions
// in their respective 'slots' in this table. We want the browser to use this VTable with our
// IOleInPlaceFrame structure.
static IOleInPlaceFrameVtbl MyIOleInPlaceFrameTable =
{
	Frame_QueryInterface,
	Frame_AddRef,
	Frame_Release,
	Frame_GetWindow,
	Frame_ContextSensitiveHelp,
	Frame_GetBorder,
	Frame_RequestBorderSpace,
	Frame_SetBorderSpace,
	Frame_SetActiveObject,
	Frame_InsertMenus,
	Frame_SetMenu,
	Frame_RemoveMenus,
	Frame_SetStatusText,
	Frame_EnableModeless,
	Frame_TranslateAccelerator
};

// We need to return an IOleInPlaceFrame struct to the browser object. And one of our IOleInPlaceFrame
// functions (Frame_GetWindow) is going to need to access our window handle. So let's create our own
// struct that starts off with an IOleInPlaceFrame struct (and that's important -- the IOleInPlaceFrame
// struct *must* be first), and then has an extra data member where we can store our own window's HWND.
//
// And because we may want to create multiple windows, each hosting its own browser object (to
// display its own web page), then we need to create a IOleInPlaceFrame struct for each window. So,
// we're not going to declare our IOleInPlaceFrame struct globally. We'll allocate it later using
// GlobalAlloc, and then stuff the appropriate HWND in it then, and also stuff a pointer to
// MyIOleInPlaceFrameTable in it. But let's just define it here.
typedef struct
{
	IOleInPlaceFrame	frame;		// The IOleInPlaceFrame must be first!

	///////////////////////////////////////////////////
	// Here you add any extra variables that you need
	// to access in your IOleInPlaceFrame functions.
	// You don't want those functions to access global
	// variables, because then you couldn't use more
	// than one browser object. (ie, You couldn't have
	// multiple windows, each with its own embedded
	// browser object to display a different web page).
	//
	// So here is where I added my extra HWND that my
	// IOleInPlaceFrame function Frame_GetWindow() needs
	// to access.
	///////////////////////////////////////////////////
	HWND				window;
}
_IOleInPlaceFrameEx;

// Our IOleClientSite functions that the browser may call
static HRESULT STDMETHODCALLTYPE Site_QueryInterface(IOleClientSite *, REFIID, void **);
static HRESULT STDMETHODCALLTYPE Site_AddRef(IOleClientSite *);
static HRESULT STDMETHODCALLTYPE Site_Release(IOleClientSite *);
static HRESULT STDMETHODCALLTYPE Site_SaveObject(IOleClientSite *);
static HRESULT STDMETHODCALLTYPE Site_GetMoniker(IOleClientSite *, DWORD, DWORD, IMoniker **);
static HRESULT STDMETHODCALLTYPE Site_GetContainer(IOleClientSite *, LPOLECONTAINER *);
static HRESULT STDMETHODCALLTYPE Site_ShowObject(IOleClientSite *);
static HRESULT STDMETHODCALLTYPE Site_OnShowWindow(IOleClientSite *, BOOL);
static HRESULT STDMETHODCALLTYPE Site_RequestNewObjectLayout(IOleClientSite *);

// Our IOleClientSite VTable. This is the array of pointers to the above functions in our C
// program that the browser may call in order to interact with our frame window that contains
// the browser object. We must define a particular set of functions that comprise the
// IOleClientSite set of functions (see above), and then stuff pointers to those functions
// in their respective 'slots' in this table. We want the browser to use this VTable with our
// IOleClientSite structure.
static IOleClientSiteVtbl MyIOleClientSiteTable =
{
	Site_QueryInterface,
	Site_AddRef,
	Site_Release,
	Site_SaveObject,
	Site_GetMoniker,
	Site_GetContainer,
	Site_ShowObject,
	Site_OnShowWindow,
	Site_RequestNewObjectLayout
};

// Our IDocHostUIHandler functions that the browser may call
static HRESULT STDMETHODCALLTYPE UI_QueryInterface(IDocHostUIHandler *, REFIID, void **);
static HRESULT STDMETHODCALLTYPE UI_AddRef(IDocHostUIHandler *);
static HRESULT STDMETHODCALLTYPE UI_Release(IDocHostUIHandler *);
static HRESULT STDMETHODCALLTYPE UI_ShowContextMenu(IDocHostUIHandler *, DWORD, POINT *, IUnknown *, IDispatch *);
static HRESULT STDMETHODCALLTYPE UI_GetHostInfo(IDocHostUIHandler *, DOCHOSTUIINFO *);
static HRESULT STDMETHODCALLTYPE UI_ShowUI(IDocHostUIHandler *, DWORD, IOleInPlaceActiveObject *, IOleCommandTarget *, IOleInPlaceFrame *, IOleInPlaceUIWindow *);
static HRESULT STDMETHODCALLTYPE UI_HideUI(IDocHostUIHandler *);
static HRESULT STDMETHODCALLTYPE UI_UpdateUI(IDocHostUIHandler *);
static HRESULT STDMETHODCALLTYPE UI_EnableModeless(IDocHostUIHandler *, BOOL);
static HRESULT STDMETHODCALLTYPE UI_OnDocWindowActivate(IDocHostUIHandler *, BOOL);
static HRESULT STDMETHODCALLTYPE UI_OnFrameWindowActivate(IDocHostUIHandler *, BOOL);
static HRESULT STDMETHODCALLTYPE UI_ResizeBorder(IDocHostUIHandler *, LPCRECT, IOleInPlaceUIWindow  *, BOOL);
static HRESULT STDMETHODCALLTYPE UI_TranslateAccelerator(IDocHostUIHandler *, LPMSG, const GUID *, DWORD);
static HRESULT STDMETHODCALLTYPE UI_GetOptionKeyPath(IDocHostUIHandler *, LPOLESTR *, DWORD);
static HRESULT STDMETHODCALLTYPE UI_GetDropTarget(IDocHostUIHandler *, IDropTarget *, IDropTarget **);
static HRESULT STDMETHODCALLTYPE UI_GetExternal(IDocHostUIHandler *, IDispatch **);
static HRESULT STDMETHODCALLTYPE UI_TranslateUrl(IDocHostUIHandler *, DWORD, SA_PARAM_NN_VALID OLECHAR *, OLECHAR  **);
static HRESULT STDMETHODCALLTYPE UI_FilterDataObject(IDocHostUIHandler *, IDataObject *, IDataObject **);

// Our IDocHostUIHandler VTable. This is the array of pointers to the above functions in our C
// program that the browser may call in order to replace/set certain user interface considerations
// (such as whether to display a pop-up context menu when the user right-clicks on the embedded
// browser object). We must define a particular set of functions that comprise the
// IDocHostUIHandler set of functions (see above), and then stuff pointers to those functions
// in their respective 'slots' in this table. We want the browser to use this VTable with our
// IDocHostUIHandler structure.
static IDocHostUIHandlerVtbl MyIDocHostUIHandlerTable = 
{
	UI_QueryInterface,
	UI_AddRef,
	UI_Release,
	UI_ShowContextMenu,
	UI_GetHostInfo,
	UI_ShowUI,
	UI_HideUI,
	UI_UpdateUI,
	UI_EnableModeless,
	UI_OnDocWindowActivate,
	UI_OnFrameWindowActivate,
	UI_ResizeBorder,
	UI_TranslateAccelerator,
	UI_GetOptionKeyPath,
	UI_GetDropTarget,
	UI_GetExternal,
	UI_TranslateUrl,
	UI_FilterDataObject
};

// We'll allocate our IDocHostUIHandler object dynamically with GlobalAlloc() for reasons outlined later.

// Our IOleInPlaceSite functions that the browser may call
static HRESULT STDMETHODCALLTYPE InPlace_QueryInterface(IOleInPlaceSite *, REFIID, void **);
static HRESULT STDMETHODCALLTYPE InPlace_AddRef(IOleInPlaceSite *);
static HRESULT STDMETHODCALLTYPE InPlace_Release(IOleInPlaceSite *);
static HRESULT STDMETHODCALLTYPE InPlace_GetWindow(IOleInPlaceSite *, HWND *);
static HRESULT STDMETHODCALLTYPE InPlace_ContextSensitiveHelp(IOleInPlaceSite *, BOOL);
static HRESULT STDMETHODCALLTYPE InPlace_CanInPlaceActivate(IOleInPlaceSite *);
static HRESULT STDMETHODCALLTYPE InPlace_OnInPlaceActivate(IOleInPlaceSite *);
static HRESULT STDMETHODCALLTYPE InPlace_OnUIActivate(IOleInPlaceSite *);
static HRESULT STDMETHODCALLTYPE InPlace_GetWindowContext(IOleInPlaceSite *, LPOLEINPLACEFRAME *, LPOLEINPLACEUIWINDOW *, LPRECT, LPRECT, LPOLEINPLACEFRAMEINFO);
static HRESULT STDMETHODCALLTYPE InPlace_Scroll(IOleInPlaceSite *, SIZE);
static HRESULT STDMETHODCALLTYPE InPlace_OnUIDeactivate(IOleInPlaceSite *, BOOL);
static HRESULT STDMETHODCALLTYPE InPlace_OnInPlaceDeactivate(IOleInPlaceSite *);
static HRESULT STDMETHODCALLTYPE InPlace_DiscardUndoState(IOleInPlaceSite *);
static HRESULT STDMETHODCALLTYPE InPlace_DeactivateAndUndo(IOleInPlaceSite *);
static HRESULT STDMETHODCALLTYPE InPlace_OnPosRectChange(IOleInPlaceSite *, LPCRECT);

// Our IOleInPlaceSite VTable. This is the array of pointers to the above functions in our C
// program that the browser may call in order to interact with our frame window that contains
// the browser object. We must define a particular set of functions that comprise the
// IOleInPlaceSite set of functions (see above), and then stuff pointers to those functions
// in their respective 'slots' in this table. We want the browser to use this VTable with our
// IOleInPlaceSite structure.
static IOleInPlaceSiteVtbl MyIOleInPlaceSiteTable =
{
	InPlace_QueryInterface,
	InPlace_AddRef,
	InPlace_Release,
	InPlace_GetWindow,
	InPlace_ContextSensitiveHelp,
	InPlace_CanInPlaceActivate,
	InPlace_OnInPlaceActivate,
	InPlace_OnUIActivate,
	InPlace_GetWindowContext,
	InPlace_Scroll,
	InPlace_OnUIDeactivate,
	InPlace_OnInPlaceDeactivate,
	InPlace_DiscardUndoState,
	InPlace_DeactivateAndUndo,
	InPlace_OnPosRectChange
};

// We need to pass our IOleClientSite structure to the browser object's SetClientSite().
// But the browser is also going to ask our IOleClientSite's QueryInterface() to return a pointer to
// our IOleInPlaceSite and/or IDocHostUIHandler structs. So we'll need to have those pointers handy.
// Plus, some of our IOleClientSite and IOleInPlaceSite functions will need to have the HWND to our
// window, and also a pointer to our IOleInPlaceFrame struct. So let's create a single struct that
// has the IOleClientSite, IOleInPlaceSite, IDocHostUIHandler, and IOleInPlaceFrame structs all inside
// it (so we can easily get a pointer to any one from any of those structs' functions). As long as the
// IOleClientSite struct is the very first thing in this custom struct, it's all ok. We can still pass
// it to the browser's SetClientSite() and pretend that it's an ordinary IOleClientSite. We'll call
// this new struct a _IOleClientSiteEx.
//
// And because we may want to create multiple windows, each hosting its own browser object (to
// display its own web page), then we need to create a unique _IOleClientSiteEx struct for
// each window. So, we're not going to declare this struct globally. We'll allocate it later
// using GlobalAlloc, and then initialize the structs within it.

typedef struct
{
	IOleInPlaceSite			inplace;	// My IOleInPlaceSite object. Must be first with in _IOleInPlaceSiteEx.

	///////////////////////////////////////////////////
	// Here you add any extra variables that you need
	// to access in your IOleInPlaceSite functions.
	//
	// So here is where I added my IOleInPlaceFrame
	// struct. If you need extra variables, add them
	// at the end.
	///////////////////////////////////////////////////
	_IOleInPlaceFrameEx		frame;		// My IOleInPlaceFrame object. Must be first within my _IOleInPlaceFrameEx
}
_IOleInPlaceSiteEx;

typedef struct
{
	IDocHostUIHandler		ui;			// My IDocHostUIHandler object. Must be first.

	///////////////////////////////////////////////////
	// Here you add any extra variables that you need
	// to access in your IDocHostUIHandler functions.
	///////////////////////////////////////////////////
}
_IDocHostUIHandlerEx;

typedef struct _DWebBrowserEvents2Ex
{
	DWebBrowserEvents2 events;
	U32 refCount;
//	SimpleWindow *window;
}
_DWebBrowserEvents2Ex;

typedef struct
{
	IOleClientSite			client;		// My IOleClientSite object. Must be first.
	_IOleInPlaceSiteEx		inplace;	// My IOleInPlaceSite object. A convenient place to put it.
	_IDocHostUIHandlerEx	ui;			// My IDocHostUIHandler object. Must be first within my _IDocHostUIHandlerEx.
	_DWebBrowserEvents2Ex	events;     // My DWebBrowserEvents2 object.

	///////////////////////////////////////////////////
	// Here you add any extra variables that you need
	// to access in your IOleClientSite functions.
	///////////////////////////////////////////////////
}
_IOleClientSiteEx;

// We need an IDispatch function in order to receive some "feedback"
// from IE's browser engine as to particular actions (events) that happen.
// For example, we can request that IE inform us when the mouse pointer
// moves over some element on the web page, such as text marked with
// a particular FONT tag. Or we can request that IE inform us when the
// user clicks on the button that submits a FORM's information. Or, we
// can request that we be informed when the user double-clicks anywhere on
// the page (which isn't part of some tag). There are many elements (ie,
// tags) on the typical web page, and each type of element typically
// has many kinds of actions it can report. We can request to be informed
// of only specific actions, with specific elements. But we need an IDispatch
// for that. IE calls our IDispatch's Invoke() function for each action we've
// requested to be informed of.

// Our IDispatch functions that the browser may call
static HRESULT STDMETHODCALLTYPE Dispatch_QueryInterface(IDispatch *, REFIID riid, void **);
static HRESULT STDMETHODCALLTYPE Dispatch_AddRef(IDispatch *);
static HRESULT STDMETHODCALLTYPE Dispatch_Release(IDispatch *);
static HRESULT STDMETHODCALLTYPE Dispatch_GetTypeInfoCount(IDispatch *, unsigned int *);
static HRESULT STDMETHODCALLTYPE Dispatch_GetTypeInfo(IDispatch *, unsigned int, LCID, ITypeInfo **);
static HRESULT STDMETHODCALLTYPE Dispatch_GetIDsOfNames(IDispatch *, REFIID, OLECHAR **, unsigned int, LCID, DISPID *);
static HRESULT STDMETHODCALLTYPE Dispatch_Invoke(IDispatch *, DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, unsigned int *);

// The VTable for our _IDispatchEx object.
IDispatchVtbl MyIDispatchVtbl =
{
	Dispatch_QueryInterface,
	Dispatch_AddRef,
	Dispatch_Release,
	Dispatch_GetTypeInfoCount,
	Dispatch_GetTypeInfo,
	Dispatch_GetIDsOfNames,
	Dispatch_Invoke
};

// Custom IDispatch implementation to see DWebBrowserEvents2
static HRESULT STDMETHODCALLTYPE Events_QueryInterface(DWebBrowserEvents2 *, REFIID, void **);
static HRESULT STDMETHODCALLTYPE Events_AddRef(DWebBrowserEvents2 *);
static HRESULT STDMETHODCALLTYPE Events_Release(DWebBrowserEvents2 *);
static HRESULT STDMETHODCALLTYPE Events_GetTypeInfoCount(DWebBrowserEvents2 *, UINT *);
static HRESULT STDMETHODCALLTYPE Events_GetTypeInfo(DWebBrowserEvents2 *, UINT, LCID, ITypeInfo **);
static HRESULT STDMETHODCALLTYPE Events_GetIDsOfNames(DWebBrowserEvents2 *, REFIID, LPOLESTR *, UINT, LCID, DISPID *);
static HRESULT STDMETHODCALLTYPE Events_Invoke (DWebBrowserEvents2 *, DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *);

// The VTable for our _IDispatchEx object for events.
DWebBrowserEvents2Vtbl MyIDispatchVtblEvents =
{
	Events_QueryInterface,
	Events_AddRef,
	Events_Release,
	Events_GetTypeInfoCount,
	Events_GetTypeInfo,
	Events_GetIDsOfNames,
	Events_Invoke
};

// Some misc stuff used by our IDispatch
static const BSTR	OnBeforeUnload = L"onbeforeunload";
static const WCHAR	BeforeUnload[] = L"beforeunload";

// ============================================================================ PUBLIC FUNCTIONS ===================================================================================

bool BrowserIEInit(HWND windowHandle)
{
	bool retVal = false;

	if (!sWindowHandle)
	{
		// Initialize the embedded IE window
		static HRESULT com_initialization_status;
		com_initialization_status = OleInitialize(NULL);
		if (com_initialization_status == S_OK)
		{
			/***************************** EmbedBrowserObject() **************************
			* Puts the browser object inside our host window, and save a pointer to this
			* window's browser object in the window's GWL_USERDATA member.
			*
			* NOTE: No HTML page will be displayed here. We can do that with a subsequent
			* call to either DisplayHTMLPage() or DisplayHTMLStr(). This is merely once-only
			* initialization for using the browser object. In a nutshell, what we do
			* here is get a pointer to the browser object in our window's GWL_USERDATA
			* so we can access that object's functions whenever we want, and we also pass
			* the browser a pointer to our IOleClientSite struct so that the browser can
			* call our functions in our struct's VTable.
			*/

			// Get a pointer to the browser object and lock it down (so it doesn't "disappear" while we're using
			// it in this program). We do this by calling the OS function CoCreateInstance().
			//
			// NOTE: We need this pointer to interact with and control the browser. With normal WIN32 controls such as a
			// Static, Edit, Combobox, etc, you obtain an HWND and send messages to it with SendMessage(). Not so with
			// the browser object. You need to get a pointer to it. This object contains an array of pointers to functions 
			// you can call within the browser object. Actually, it contains a 'lpVtbl' member that is a pointer to that
			// array. We call the array a 'VTable'.
			//
			// For example, the browser object happens to have a SetClientSite() function we want to call. So, after we
			// retrieve the pointer to the browser object (in a local we'll name 'browserObject'), then we can call that
			// function, and pass it args, as so:
			//
			// browserObject->lpVtbl->SetClientSite(browserObject, SomeIOleClientObject);
			//
			// There's our pointer to the browser object in 'browserObject'. And there's the pointer to the browser object's
			// VTable in 'browserObject->lpVtbl'. And the pointer to the SetClientSite function happens to be stored in a
			// member named 'SetClientSite' within the VTable. So we are actually indirectly calling SetClientSite by using
			// a pointer to it. That's how you use a VTable.

			// Get Internet Explorer's IWebBrowser2 object (ie, IE's main object)
			if (SUCCEEDED(CoCreateInstance(&CLSID_WebBrowser, 0, CLSCTX_INPROC, &IID_IWebBrowser2, (void **)&sWebBrowser)) && sWebBrowser)
			{
				// We need to get a pointer to IWebBrowser2's IOleObject child object
				IOleObject* browserObject = NULL;
				if (SUCCEEDED(sWebBrowser->lpVtbl->QueryInterface(sWebBrowser, &IID_IOleObject, (void**)&browserObject)) && browserObject)
				{
					char *ptr;

					// Our IOleClientSite, IOleInPlaceSite, and IOleInPlaceFrame functions need to get our window handle. We
					// could store that in some global. But then, that would mean that our functions would work with only that
					// one window. If we want to create multiple windows, each hosting its own browser object (to display its
					// own web page), then we need to create unique IOleClientSite, IOleInPlaceSite, and IOleInPlaceFrame
					// structs for each window. And we'll put an extra member at the end of those structs to store our extra
					// data such as a window handle. So, our functions won't have to touch global data, and can therefore be
					// re-entrant and work with multiple objects/windows.
					//
					// Remember that a pointer to our IOleClientSite we create here will be passed as the first arg to every
					// one of our IOleClientSite functions. Ditto with the IOleInPlaceFrame object we create here, and the
					// IOleInPlaceFrame functions. So, our functions are able to retrieve the window handle we'll store here,
					// and then, they'll work with all such windows containing a browser control.
					//
					// Furthermore, since the browser will be calling our IOleClientSite's QueryInterface to get a pointer to
					// our IOleInPlaceSite and IDocHostUIHandler objects, that means that our IOleClientSite QueryInterface
					// must have an easy way to grab those pointers. Probably the easiest thing to do is just embed our
					// IOleInPlaceSite and IDocHostUIHandler objects inside of an extended IOleClientSite which we'll call
					// a _IOleClientSiteEx. As long as they come after the pointer to the IOleClientSite VTable, then we're
					// ok.
					//
					// Of course, we need to GlobalAlloc the above structs now. We'll just get all 3 with a single call to
					// GlobalAlloc, especially since they're are contained inside of our _IOleClientSiteEx anyway.
					//
					// So, we're not actually allocating separate IOleClientSite, IOleInPlaceSite, IOleInPlaceFrame and
					// IDocHostUIHandler structs.
					//
					// One final thing. We're going to allocate extra room to store the pointer to the browser object.
					if (ptr = (char *)GlobalAlloc(GMEM_FIXED, sizeof(_IOleClientSiteEx) + sizeof(IOleObject *)))
					{
						_IOleClientSiteEx *_iOleClientSiteEx;

						// Initialize our IOleClientSite object with a pointer to our IOleClientSite VTable.
						_iOleClientSiteEx = (_IOleClientSiteEx *)(ptr + sizeof(IOleObject *));
						_iOleClientSiteEx->client.lpVtbl = &MyIOleClientSiteTable;

						// Initialize our IOleInPlaceSite object with a pointer to our IOleInPlaceSite VTable.
						_iOleClientSiteEx->inplace.inplace.lpVtbl = &MyIOleInPlaceSiteTable;

						// Initialize our IOleInPlaceFrame object with a pointer to our IOleInPlaceFrame VTable.
						_iOleClientSiteEx->inplace.frame.frame.lpVtbl = &MyIOleInPlaceFrameTable;

						// Save our HWND (in the IOleInPlaceFrame object) so our IOleInPlaceFrame functions can retrieve it.
						_iOleClientSiteEx->inplace.frame.window = windowHandle;

						// Initialize our IDocHostUIHandler object with a pointer to our IDocHostUIHandler VTable.
						_iOleClientSiteEx->ui.ui.lpVtbl = &MyIDocHostUIHandlerTable;

						// Initialize out IDispatch object.
						_iOleClientSiteEx->events.events.lpVtbl = &MyIDispatchVtblEvents;
						_iOleClientSiteEx->events.refCount = 0;
						//	_iOleClientSiteEx->events.window = window;


						// Ok, we now have the pointer to the IOleObject child object in 'browserObject'. Let's save this in the
						// memory block we allocated above, and then save the pointer to that whole thing in our window's
						// USERDATA member. That way, if we need multiple windows each hosting its own browser object, we can
						// call EmbedBrowserObject() for each one, and easily associate the appropriate browser object with
						// its matching window and its own objects containing per-window data.
						//	*((IOleObject **)ptr) = browserObject;
						//	((CrypticLauncherWindow *)window->pUserData)->browserPtr = (IOleObject **)ptr;
						// Don't use SetWindowLong, as the SimpleWindowManager system uses it.
						//SetWindowLong(hwnd, GWL_USERDATA, (LONG)ptr);

						// Give the browser a pointer to my IOleClientSite object.
						//
						// NOTE: We pass our _IOleClientSiteEx struct and lie -- saying that it's a IOleClientSite. It's ok. A
						// _IOleClientSiteEx struct starts with an embedded IOleClientSite. So the browser won't care, and we want
						// this extended struct passed to our IOleClientSite functions.
						if (SUCCEEDED(browserObject->lpVtbl->SetClientSite(browserObject, (IOleClientSite *)_iOleClientSiteEx)))
						{
							RECT rect;

							// Set the display area of our browser control the same as our window's size
							if (GetClientRect(windowHandle, &rect))
							{
								// and actually put the browser object into our window.
								if (SUCCEEDED(browserObject->lpVtbl->DoVerb(browserObject, OLEIVERB_INPLACEACTIVATE, 0, (IOleClientSite *)_iOleClientSiteEx, 0, windowHandle, &rect)))
								{
									IConnectionPointContainer *pConnectionPointContainer = NULL;

									// Let's call several functions in the IWebBrowser2 object to position the browser display area
									// in our window. The functions we call are put_Left(), put_Top(), put_Width(), and put_Height().
									// Note that we reference the IWebBrowser2 object's VTable to get pointers to those functions. And
									// also note that the first arg we pass to each is the pointer to the IWebBrowser2 object.
									sWebBrowser->lpVtbl->put_Left(sWebBrowser, 0);
									sWebBrowser->lpVtbl->put_Top(sWebBrowser, 0);
									sWebBrowser->lpVtbl->put_Width(sWebBrowser, rect.right);
									sWebBrowser->lpVtbl->put_Height(sWebBrowser, rect.bottom);

									// Get the connection point container
									if (SUCCEEDED(browserObject->lpVtbl->QueryInterface(browserObject, &IID_IConnectionPointContainer, (void**)(&pConnectionPointContainer))) && pConnectionPointContainer)
									{
										IConnectionPoint *pConnectionPoint = NULL;

										// Get the appropriate connection point
										if (SUCCEEDED(pConnectionPointContainer->lpVtbl->FindConnectionPoint(pConnectionPointContainer,&DIID_DWebBrowserEvents2, &pConnectionPoint)) && pConnectionPoint)
										{
											DWORD dwAdviseCookie = 0;
											// Advise the connection point of our event sink
											assert(SUCCEEDED(pConnectionPoint->lpVtbl->Advise(pConnectionPoint, (IUnknown *)&_iOleClientSiteEx->events, &dwAdviseCookie)));
											assert(dwAdviseCookie);
											sWindowHandle = windowHandle;
											BrowserSetLanguageCode(BL_ENGLISH);
											pConnectionPoint->lpVtbl->Release(pConnectionPoint);
											pConnectionPoint = NULL;
											retVal = true;
										}

										pConnectionPointContainer->lpVtbl->Release(pConnectionPointContainer);
										pConnectionPointContainer = NULL;
									}
								}
							}
						}

						if (!retVal)
						{
							GlobalFree(ptr);
							ptr = NULL;
						}
					}

					browserObject->lpVtbl->Release(browserObject);
					browserObject = NULL;
				}

				if (!retVal)
				{
					sWebBrowser->lpVtbl->Release(sWebBrowser);
					sWebBrowser = NULL;
				}
				else
				{
					// sWebBrowser is held with a reference here
				}
			}
		}
	}

	// Success
	return retVal;
}

static bool BrowserIEOnPageLoaded(void)
{
	bool retVal = false;

	if (sWebBrowser)
	{
		IDispatch *dispatch = NULL;

		// First, we need the IDispatch object
		if (SUCCEEDED(sWebBrowser->lpVtbl->get_Document(sWebBrowser, &dispatch)) && dispatch)
		{
			IHTMLDocument2* htmlDoc = NULL;

			// Get the IHTMLDocument2 object embedded within the IDispatch object
			if (SUCCEEDED(dispatch->lpVtbl->QueryInterface(dispatch, &IID_IHTMLDocument2, (void **)&htmlDoc)) && htmlDoc)
			{
				IHTMLWindow2 *htmlWindow2;

				if (SUCCEEDED(htmlDoc->lpVtbl->get_parentWindow(htmlDoc, &htmlWindow2)) && htmlWindow2)
				{
					IOmNavigator *navigator;

					if (SUCCEEDED(htmlWindow2->lpVtbl->get_navigator(htmlWindow2, &navigator)) && navigator)
					{
						VARIANT_BOOL b;

						// Get information about the web browser and put it in trivia.
						extractTriviaData(navigator, navigator->lpVtbl->get_appVersion, "WebBrowser:IEVersion");
						extractTriviaData(navigator, navigator->lpVtbl->get_browserLanguage, "WebBrowser:BrowserLanguage");
						extractTriviaData(navigator, navigator->lpVtbl->get_systemLanguage, "WebBrowser:SystemLanguage");
						extractTriviaData(navigator, navigator->lpVtbl->get_userLanguage, "WebBrowser:UserLanguage");

						navigator->lpVtbl->get_cookieEnabled(navigator, &b);
						triviaPrintf("WebBrowser:CookiesEnabled", "%d", (b == VARIANT_FALSE) ? 0 : 1);

						navigator->lpVtbl->javaEnabled(navigator, &b);
						triviaPrintf("WebBrowser:JavaEnabled", "%d", (b == VARIANT_FALSE) ? 0 : 1);

						navigator->lpVtbl->Release(navigator);
						navigator = NULL;

						retVal = true;
					}

					htmlWindow2->lpVtbl->Release(htmlWindow2);
					htmlWindow2 = NULL;
				}

				htmlDoc->lpVtbl->Release(htmlDoc);
				htmlDoc = NULL;
			}

			// Release the IDispatch object now that we have the IHTMLDocument2
			dispatch->lpVtbl->Release(dispatch);
			dispatch = NULL;
		}
	}

	return retVal;
}

bool BrowserIEShutdown(void)
{
	bool retVal = false;

	if (sWebBrowser)
	{
		IOleObject *browserObject = NULL;

		if (SUCCEEDED(sWebBrowser->lpVtbl->QueryInterface(sWebBrowser, &IID_IOleObject, (void**)&browserObject)) && browserObject)
		{
			browserObject->lpVtbl->Close(browserObject, OLECLOSE_NOSAVE);
			browserObject->lpVtbl->Release(browserObject);
			browserObject = NULL;
			retVal = true;
		}

		sWebBrowser->lpVtbl->Release(sWebBrowser);
		sWebBrowser = NULL;
	}

	if (sWindowHandle)
	{
		sWindowHandle = NULL;
	}

	return retVal;
}

HWND BrowserIEGetHtmlDocHWND(void)
{
	HWND retVal = NULL;

	if (sWebBrowser)
	{
		IDispatch *dispatch = NULL;

		// First, we need the IDispatch object
		if (SUCCEEDED(sWebBrowser->lpVtbl->get_Document(sWebBrowser, &dispatch)) && dispatch)
		{
			IHTMLDocument2* htmlDoc = NULL;

			// Get the IHTMLDocument2 object embedded within the IDispatch object
			if (SUCCEEDED(dispatch->lpVtbl->QueryInterface(dispatch, &IID_IHTMLDocument2, (void **)&htmlDoc)) && htmlDoc)
			{
				IOleInPlaceObject* pInPlace = NULL;

				if (SUCCEEDED(htmlDoc->lpVtbl->QueryInterface(htmlDoc, &IID_IOleInPlaceObject, (void**)&pInPlace)) && pInPlace)
				{
					pInPlace->lpVtbl->GetWindow(pInPlace, &retVal); // this sets retVal
					pInPlace->lpVtbl->Release(pInPlace);
					pInPlace = NULL;
				}

				htmlDoc->lpVtbl->Release(htmlDoc);
				htmlDoc = NULL;
			}

			dispatch->lpVtbl->Release(dispatch);
			dispatch = NULL;
		}
	}

	return retVal;
}


bool BrowserIEGetCurrentURL(char **currentURLOut)
{
	bool retVal = false;

	if (currentURLOut && sWebBrowser)
	{
		IDispatch *dispatch = NULL;

		// First, we need the IDispatch object
		if (SUCCEEDED(sWebBrowser->lpVtbl->get_Document(sWebBrowser, &dispatch)) && dispatch)
		{
			IHTMLDocument2* htmlDoc = NULL;

			// Get the IHTMLDocument2 object embedded within the IDispatch object
			if (SUCCEEDED(dispatch->lpVtbl->QueryInterface(dispatch, &IID_IHTMLDocument2, (void **)&htmlDoc)) && htmlDoc)
			{
				IHTMLLocation *loc = NULL;

				if (SUCCEEDED(htmlDoc->lpVtbl->get_location(htmlDoc, &loc)) && loc)
				{
					BSTR bstr = NULL;

					if (SUCCEEDED(loc->lpVtbl->get_pathname(loc, &bstr))) // this will sometimes succeed but return a NULL bstr - code below handles this
					{
						*currentURLOut = nullTermStringFromBstr(bstr);
						SysFreeString(bstr);
						retVal = true;
					}

					loc->lpVtbl->Release(loc);
					loc = NULL;
				}

				htmlDoc->lpVtbl->Release(htmlDoc);
				htmlDoc = NULL;
			}

			dispatch->lpVtbl->Release(dispatch);
			dispatch = NULL;
		}
	}

	return retVal;
}

bool BrowserIEGetElementInnerHTML(const char *elementName, char **msgOut)
{
	bool retVal = false;

	if (msgOut)
	{
		IHTMLElement *elem = getElement(elementName);

		if (elem)
		{
			BSTR bstr = NULL;

			if (SUCCEEDED(elem->lpVtbl->get_innerHTML(elem, &bstr))) // this will sometimes succeed but return a NULL bstr - code below handles this
			{
				(*msgOut) = nullTermStringFromBstr(bstr);
				SysFreeString(bstr);
				retVal = true;
			}

			elem->lpVtbl->Release(elem);
			elem = NULL;
		}
	}

	return retVal;
}

bool BrowserIESetElementInnerHTML(const char *elementName, const char *msgIn)
{
	bool retVal = false;

	if (msgIn)
	{
		IHTMLElement *elem = getElement(elementName);

		if (elem)
		{
			BSTR bstr = bstrFromNullTermString(msgIn);

			retVal = SUCCEEDED(elem->lpVtbl->put_innerHTML(elem, bstr));

			SysFreeString(bstr);
			elem->lpVtbl->Release(elem);
			elem = NULL;
		}
	}

	return retVal;
}

bool BrowserIESetElementClassName(const char *elementName, const char *classNameIn)
{
	bool retVal = false;

	if (classNameIn)
	{
		IHTMLElement *elem = getElement(elementName);

		if (elem)
		{
			BSTR bstr = bstrFromNullTermString(classNameIn);

			retVal = SUCCEEDED(elem->lpVtbl->put_className(elem, bstr)) ? true : false;

			SysFreeString(bstr);
			elem->lpVtbl->Release(elem);
			elem = NULL;
		}
	}

	return retVal;
}

bool BrowserIESetElementCSSText(const char *elementName, const char *cssText)
{
	bool retVal = false;

	if (cssText)
	{
		IHTMLElement *elem = getElement(elementName);

		if (elem)
		{
			IHTMLStyle *style;

			if (SUCCEEDED(elem->lpVtbl->get_style(elem, &style)) && style)
			{
				BSTR style_text = bstrFromNullTermString(cssText);

				retVal = SUCCEEDED(style->lpVtbl->put_cssText(style, style_text)) ? true : false;

				SysFreeString(style_text);
				style->lpVtbl->Release(style);
				style = NULL;
			}
			elem->lpVtbl->Release(elem);
			elem = NULL;
		}
	}

	return retVal;
}

bool BrowserIEGetInputElementValue(const char *inputElementName, const char **valueTextOut)
{
	bool retVal = false;

	if (valueTextOut)
	{
		IHTMLInputElement *inputElem = getInputElement(inputElementName);

		if (inputElem)
		{
			BSTR value_bstr = NULL;

			if (SUCCEEDED(inputElem->lpVtbl->get_value(inputElem, &value_bstr))) // this will sometimes succeed but return a NULL bstr - code below handles this
			{
				*valueTextOut = nullTermStringFromBstr(value_bstr);
				SysFreeString(value_bstr);
				retVal = true;
			}

			inputElem->lpVtbl->Release(inputElem);
			inputElem = NULL;
		}
	}

	return retVal;
}

bool BrowserIESetInputElementValue(const char *inputElementName, const char *valueTextIn)
{
	bool retVal = false;

	if (valueTextIn)
	{
		IHTMLInputElement *inputElem = getInputElement(inputElementName);

		if (inputElem)
		{
			BSTR value_bstr = bstrFromNullTermString(valueTextIn);

			retVal = SUCCEEDED(inputElem->lpVtbl->put_value(inputElem, value_bstr)) ? true : false;

			SysFreeString(value_bstr);
			inputElem->lpVtbl->Release(inputElem);
			inputElem = NULL;
		}
	}

	return retVal;
}

bool BrowserIESetSelectElementValue(const char *selectElementName, const char *valueTextIn)
{
	bool retVal = false;

	if (valueTextIn)
	{
		IHTMLSelectElement *selectElem = getSelectElement(selectElementName);

		if (selectElem)
		{
			BSTR value_bstr = bstrFromNullTermString(valueTextIn);

			retVal = SUCCEEDED(selectElem->lpVtbl->put_value(selectElem, value_bstr)) ? true : false;

			SysFreeString(value_bstr);
			selectElem->lpVtbl->Release(selectElem);
			selectElem = NULL;
		}
	}

	return retVal;
}

// returns number of options added, or -1 on failure
int BrowserIESetSelectElementOptions(const char *selectElementName, OptionCallbackFunc optionCallbackFunc, void *userOptionData)
{
	bool retVal = false;
	int optionsAdded = 0;

	if (sWebBrowser)
	{
		IDispatch *dispatch = NULL;

		// First, we need the IDispatch object
		if (SUCCEEDED(sWebBrowser->lpVtbl->get_Document(sWebBrowser, &dispatch)) && dispatch)
		{
			IHTMLDocument2* htmlDoc = NULL;

			// Get the IHTMLDocument2 object embedded within the IDispatch object
			if (SUCCEEDED(dispatch->lpVtbl->QueryInterface(dispatch, &IID_IHTMLDocument2, (void **)&htmlDoc)) && htmlDoc)
			{
				IHTMLSelectElement *selectElem = getSelectElement(selectElementName);

				if (selectElem)
				{
					VARIANT var;
					char optionValue[MAX_PATH];
					char optionText[MAX_PATH];
					long userOptionIndex;
					bool anyFailure = false;
					bool bShouldInsert = false;

					retVal = true; // we call it successful if we found the select element.

					if (SUCCEEDED(selectElem->lpVtbl->get_length(selectElem, &userOptionIndex)))
					{
						// wipe existing options from the list
						for (; userOptionIndex>=1; userOptionIndex--)
						{
							selectElem->lpVtbl->remove(selectElem, userOptionIndex);
						}
					}

					// userOptionIndex should be 0 now
					assert(!userOptionIndex);
					VariantInit(&var);
					while (optionCallbackFunc(userOptionData, userOptionIndex, &bShouldInsert, optionValue, sizeof(optionValue), optionText, sizeof(optionText)))
					{
						if (bShouldInsert)
						{
							BSTR bstr = bstrFromNullTermString("option");

							IHTMLElement *newElem = NULL;
							if (SUCCEEDED(htmlDoc->lpVtbl->createElement(htmlDoc, bstr, &newElem)) && newElem)
							{
								IHTMLOptionElement *optionElem = NULL;
								if (SUCCEEDED(newElem->lpVtbl->QueryInterface(newElem, &IID_IHTMLOptionElement, (void **)&optionElem)) && optionElem)
								{
									BSTR optionBstr = bstrFromNullTermString(optionText);

									if (SUCCEEDED(optionElem->lpVtbl->put_text(optionElem, optionBstr)))
									{
										SysFreeString(optionBstr);

										optionBstr = bstrFromNullTermString(optionValue);
										if (SUCCEEDED(optionElem->lpVtbl->put_value(optionElem, optionBstr)))
										{
											bool addSucceeded;
											SysFreeString(optionBstr);

											addSucceeded = SUCCEEDED(selectElem->lpVtbl->add(selectElem, newElem, var)) ? true : false; // this adds the option element
											if (addSucceeded)
											{
												optionsAdded++;
											}

											if (!anyFailure && !addSucceeded)
											{
												anyFailure = true;
											}
										}
									}

									optionElem->lpVtbl->Release(optionElem);
									optionElem = NULL;
								}

								newElem->lpVtbl->Release(newElem);
								newElem = NULL;
							}

							SysFreeString(bstr);
						}

						userOptionIndex++;
					}

					if (!anyFailure)
					{
						retVal = true;
					}

					selectElem->lpVtbl->Release(selectElem);
					selectElem = NULL;
				}

				htmlDoc->lpVtbl->Release(htmlDoc);
				htmlDoc = NULL;
			}

			dispatch->lpVtbl->Release(dispatch);
			dispatch = NULL;
		}
	}

	return retVal ? optionsAdded : -1;
}

bool BrowserIEGetSelectElementValue(const char *selectElementName, const char **valueTextOut)
{
	bool retVal = false;

	if (valueTextOut)
	{
		IHTMLSelectElement *selectElem = getSelectElement(selectElementName);

		if (selectElem)
		{
			BSTR value_bstr = NULL;

			if (SUCCEEDED(selectElem->lpVtbl->get_value(selectElem, &value_bstr))) // this will sometimes succeed but return a NULL bstr - code below handles this
			{
				*valueTextOut = nullTermStringFromBstr(value_bstr);
				SysFreeString(value_bstr);
				retVal = true;
			}

			selectElem->lpVtbl->Release(selectElem);
			selectElem = NULL;
		}
	}

	return retVal;
}

bool BrowserIESetSelectElementOnChangeCallback(const char *selectElementName, OnChangeCallbackFunc onChangeCallbackFunc, void *userOnChangeData)
{
	bool retVal = false;

	if (sWebBrowser)
	{
		IDispatch *dispatch = NULL;

		// First, we need the IDispatch object
		if (SUCCEEDED(sWebBrowser->lpVtbl->get_Document(sWebBrowser, &dispatch)) && dispatch)
		{
			IHTMLDocument2* htmlDoc = NULL;

			// Get the IHTMLDocument2 object embedded within the IDispatch object
			if (SUCCEEDED(dispatch->lpVtbl->QueryInterface(dispatch, &IID_IHTMLDocument2, (void **)&htmlDoc)) && htmlDoc)
			{
				IHTMLSelectElement *selectElem = getSelectElement(selectElementName);

				if (selectElem)
				{
					_IDispatchEx *dispatchEx;
					IHTMLWindow2 *htmlWindow2;
					VARIANT disp;
					VariantInit(&disp);
					disp.vt = VT_DISPATCH;

					/************************ CreateWebEvtHandler() *********************
					* Creates an event handler, to be used to "attach to" some events that
					* happen with an element on the web page.
					*
					* hwnd - The window where messages will be sent when the event happens.
					*
					* htmlDoc2 - Pointer to an IHTMLDocument2 object. Objects that use
					*			  the resulting event handler must be associated with this
					*			  (ie. either its parent window, itself or a child element).
					*
					* extraData -	sizeof() any application defined struct you wish
					*				prepended to the returned IDispatch. You can use
					*				GetWebExtraData() to fetch a pointer to this struct.
					*				0 if no extra data needed.
					*
					* id -		Any numeric value of your choosing to be stored in the
					*			_IDispatchEx's "id" member. If a negative value, then the
					*			WEBPARAMS->eventStr will be set to the passed USERDATA
					*			instead of an event string.
					*
					* obj -	A pointer to any other object to be stored in the _IDispatchEx's
					*			"object" member. 0 if none.
					*
					* userdata -	If not zero, then this will be stored in the _IDispatchEx's
					*				"userdata" member.
					*
					* attachObj -	If not zero, then "userdata" is considered to be a BSTR of
					*				some event type to attach to, and "attachObj" is the
					*				
					*
					* RETURNS: Pointer to the IDispatch if success, or 0 if an error.
					*
					* NOTE: "elem" will automatically be Release()'ed by this DLL when the
					* _IDispatchEx is destroyed. It is also Release()'ed if this call fails.
					*/

					// Get the IHTMLWindow2. Our IDispatch's Invoke() will need it to cleanup.
					if (SUCCEEDED(htmlDoc->lpVtbl->get_parentWindow(htmlDoc, &htmlWindow2)) && htmlWindow2)
					{
						// Create an IDispatch object (actually we create one of our own _IDispatchEx objects)
						// which we'll use to monitor "events" that occur to an element on a web page.
						// IE's engine will call our IDispatch's Invoke() function when it wants to inform
						// us that a specific event has occurred.
						if ((dispatchEx = (_IDispatchEx *)GlobalAlloc(GMEM_FIXED, sizeof(_IDispatchEx))))
						{
							IHTMLWindow3 *htmlWindow3 = NULL;

							ZeroMemory(dispatchEx, sizeof(_IDispatchEx));

							// Fill in our _IDispatchEx with its VTable, and the args passed to us by the caller
							dispatchEx->dispatchObj.lpVtbl = &MyIDispatchVtbl;
							dispatchEx->hwnd = sWindowHandle;
							dispatchEx->htmlWindow2 = htmlWindow2;
							dispatchEx->id = DISPATCH_CALLBACK_SELECT_ELEMENT_ON_CHANGE;
							dispatchEx->callback = onChangeCallbackFunc;
							dispatchEx->userdata = userOnChangeData;

							// No one has yet called its Dispatch_AddRef(). That won't happen until we
							// attach some event to it, such as below.
							dispatchEx->refCount = 0;

							// Now we attach our IDispatch to the "beforeunload" event so that our IDispatch's
							// Invoke() is called when the browser fires off this event.

							// We need to get the IHTMLWindow3 object (so we can call its attachEvent() and pass it
							// our IDispatch wrapped in a VARIANT).
							if (SUCCEEDED(htmlWindow2->lpVtbl->QueryInterface(htmlWindow2, &IID_IHTMLWindow3, (void **)&htmlWindow3)) && htmlWindow3)
							{
								VARIANT	varDisp;
								VariantInit(&varDisp);
								varDisp.vt = VT_DISPATCH;
								varDisp.pdispVal = (IDispatch *)dispatchEx;

								if (SUCCEEDED(htmlWindow2->lpVtbl->put_onbeforeunload(htmlWindow2, varDisp)))
								{
									// Return the IDispatch, so the app can use it to attach some other events to the
									// same element on the web page.
									//
									// NOTE: We don't Release() the IHTMLWindow2 object. We stored that pointer
									// in our _IDispatchEx and its Invoke() needs it. webDetach() will
									// Release() it.
									disp.pdispVal = (IDispatch*)dispatchEx;

									// Install the change callback
									if (SUCCEEDED(selectElem->lpVtbl->put_onchange(selectElem, disp)))
									{
										retVal = true;
									}
								}

								htmlWindow3->lpVtbl->Release(htmlWindow3);
								htmlWindow3 = NULL;
							}

							// An error. Free all stuff above.
							if (!retVal)
							{
								GlobalFree((char *)dispatchEx);
							}
						}

						// An error. Free all stuff above.
						if (!retVal)
						{
							htmlWindow2->lpVtbl->Release(htmlWindow2);
							htmlWindow2 = NULL;
						}
					}

					selectElem->lpVtbl->Release(selectElem);
					selectElem = NULL;
				}

				htmlDoc->lpVtbl->Release(htmlDoc);
				htmlDoc = NULL;
			}

			dispatch->lpVtbl->Release(dispatch);
			dispatch = NULL;
		}
	}

	return retVal;
}

bool BrowserIEFocusElement(const char *elementName)
{
	bool retVal = false;

	IHTMLElement *elem = getElement(elementName);

	if (elem)
	{
		IHTMLElement2 *elem2 = NULL;
		if (SUCCEEDED(elem->lpVtbl->QueryInterface(elem, &IID_IHTMLElement2, (void **)&elem2)) && elem2)
		{
			if (SUCCEEDED(elem2->lpVtbl->focus(elem2)))
			{
				retVal = true;
			}

			elem2->lpVtbl->Release(elem2);
			elem2 = NULL;
		}

		elem->lpVtbl->Release(elem);
		elem = NULL;
	}

	return retVal;
}

bool BrowserIEExistsElement(const char *elementName)
{
	bool retVal = false;

	IHTMLElement *elem = getElement(elementName);

	if (elem)
	{
		retVal = true;
		elem->lpVtbl->Release(elem);
		elem = NULL;
	}

	return retVal;
}

BOOL BrowserIEMessageCallback(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	if(pWindow->eWindowType == CL_WINDOW_MAIN && iMsg >= WM_KEYDOWN && iMsg <= WM_KEYLAST && hDlg == BrowserGetHtmlDocHWND())
	{
		bool bInLoggedInState = LauncherIsInLoggedInState();
		bool bControlKeyDepressed = GetKeyState(VK_CONTROL) & 0x8000 ? true : false;

		if(wParam == VK_TAB || wParam == VK_RETURN || (bControlKeyDepressed && (wParam == 'V')))
		{
			pWindow->pDialogCB(hDlg, iMsg, wParam, lParam, pWindow);
			return TRUE;
		}
	}
	return FALSE;
}

bool BrowserIEProcessKeystrokes(MSG msg, SimpleWindow *pWindow)
{
	if (sWebBrowser)
	{
		switch(msg.message)
		{
			case WM_KEYDOWN:
			case WM_CHAR:
			case WM_DEADCHAR:
			case WM_SYSKEYDOWN:
			case WM_SYSCHAR:
			case WM_SYSDEADCHAR:
			case WM_KEYLAST:
			{
				// Do not process repeated keys
				// From http://msdn.microsoft.com/en-us/library/ms646280(VS.85).aspx
				// 30 - Specifies the previous key state. The value is 1 if the key is down before the message is sent, or it is zero if the key is up.
				if(msg.lParam & 0x40000000)
					break;

				if (LauncherGetState() == CL_STATE_LOGINPAGELOADED)
				{
					IOleInPlaceActiveObject* pIOIPAO = NULL;

					if (SUCCEEDED(sWebBrowser->lpVtbl->QueryInterface(sWebBrowser, &IID_IOleInPlaceActiveObject, (LPVOID *)&pIOIPAO)) && pIOIPAO)
					{
						bool retVal = SUCCEEDED(pIOIPAO->lpVtbl->TranslateAccelerator(pIOIPAO, &msg)) ? true : false;

						pIOIPAO->lpVtbl->Release(pIOIPAO);
						pIOIPAO = NULL;

						return retVal;
					}
				}
			}
			break;
		}
	}

	return false;
}

// !!!: THIS DOESN'T ACTUALLY WORK WITH MORE THAN ONE ARGUMENT. <NPK 2008-06-09>
bool BrowserIEInvokeScript(const char *scriptName, InvokeScriptArgType firstType, va_list args)
{
	bool retVal = false;

	if (sWebBrowser)
	{
		IDispatch *dispatch = NULL;

		// First, we need the IDispatch object
		if (SUCCEEDED(sWebBrowser->lpVtbl->get_Document(sWebBrowser, &dispatch)) && dispatch)
		{
			IHTMLDocument2* htmlDoc = NULL;

			// Get the IHTMLDocument2 object embedded within the IDispatch object
			if (SUCCEEDED(dispatch->lpVtbl->QueryInterface(dispatch, &IID_IHTMLDocument2, (void **)&htmlDoc)) && htmlDoc)
			{
				IDispatch *pScriptDisp = NULL;
				VARIANT **vArgs = NULL;
				InvokeScriptArgType currentType = firstType;
				BSTR bstrScriptName = bstrFromNullTermString(scriptName); // Convert name to BSTR

				// Convert args to VARIANT
				eaCreate(&vArgs);
				while(currentType != INVOKE_SCRIPT_ARG_NULL)
				{
					VARIANT *vArg = GlobalAlloc(GMEM_FIXED, sizeof(VARIANT));
					assert(vArg);
		#pragma warning(suppress:6386) // Buffer overrun: accessing 'argument 1', the writable size is '1*0' bytes, but '16' bytes might be written
					VariantInit(vArg);

					switch (currentType)
					{
						case INVOKE_SCRIPT_ARG_STRING:
						case INVOKE_SCRIPT_ARG_STRING_OBJ:
						{
							V_VT(vArg) = VT_BSTR;
							V_BSTR(vArg) = bstrFromNullTermString(va_arg(args, char*));
							break;
						}
						case INVOKE_SCRIPT_ARG_INT:
						{
							V_VT(vArg) = VT_I4;
							V_I4(vArg) = va_arg(args, S32);
							break;
						}
					}

					eaPush(&vArgs, vArg);
					currentType = va_arg(args, InvokeScriptArgType);
				}

				// Get the script and execute it
				if (SUCCEEDED(htmlDoc->lpVtbl->get_Script(htmlDoc, &pScriptDisp)) && pScriptDisp)
				{
					DISPID IdDisp = 0;

					if (SUCCEEDED(pScriptDisp->lpVtbl->GetIDsOfNames(pScriptDisp,
						&IID_NULL,
						&bstrScriptName,
						1,
						LOCALE_SYSTEM_DEFAULT,
						&IdDisp)))
					{
						DISPPARAMS dpArgs = {NULL, NULL, 0, 0};

						// Initialize the DISPPARAMS structure
						dpArgs.cArgs = eaSize(&vArgs);
						if (dpArgs.cArgs > 0)
						{
							dpArgs.rgvarg = *vArgs;
						}

						retVal = SUCCEEDED(pScriptDisp->lpVtbl->Invoke(pScriptDisp,
							IdDisp,
							&IID_NULL,
							LOCALE_SYSTEM_DEFAULT,
							DISPATCH_METHOD,
							&dpArgs,
							NULL, // pVarResult
							NULL, // pExcepInfo
							NULL)) ? true : false; // puArgErr
					}
					else if (gDebugMode)
					{
						printf("ERROR: JS function '%s' not found!\n", scriptName);
					}

					pScriptDisp->lpVtbl->Release(pScriptDisp);
					pScriptDisp = NULL;
				}

				// Clean up

				SysFreeString(bstrScriptName);
				FOR_EACH_IN_EARRAY(vArgs, VARIANT, var)
					//switch (V_VT(var))
					//{
					//case VT_BSTR:
					//	SysFreeString(V_BSTR(var));
					//}
					VariantClear(var);
					GlobalFree(var);
				FOR_EACH_END
				eaDestroy(&vArgs);

				htmlDoc->lpVtbl->Release(htmlDoc);
				htmlDoc = NULL;
			}

			dispatch->lpVtbl->Release(dispatch);
			dispatch = NULL;
		}
	}

	return retVal;
}

bool BrowserIEDisplayHTMLFromURL(const char *webPageURL, const char **eaEstrKeyValuePostData)
{
	bool retVal = false;

	if (sWebBrowser)
	{
		VARIANT	myURL, varFlags, varPostData, varHeaders;
		SAFEARRAY *saPostData;
		SAFEARRAYBOUND sabPostData[1];

		// Our URL (ie, web address, such as "http://www.microsoft.com" or an HTM filename on disk
		// such as "c:\myfile.htm") must be passed to the IWebBrowser2's Navigate2() function as a BSTR.
		// A BSTR is like a pascal version of a double-byte character string. In other words, the
		// first unsigned short is a count of how many characters are in the string, and then this
		// is followed by those characters, each expressed as an unsigned short (rather than a
		// char). The string is not nul-terminated. The OS function SysAllocString can allocate and
		// copy a UNICODE C string to a BSTR. Of course, we'll need to free that BSTR after we're done
		// with it. If we're not using UNICODE, we first have to convert to a UNICODE string.
		//
		// What's more, our BSTR needs to be stuffed into a VARIENT struct, and that VARIENT struct is
		// then passed to Navigate2(). Why? The VARIENT struct makes it possible to define generic
		// 'datatypes' that can be used with all languages. Not all languages support things like
		// nul-terminated C strings. So, by using a VARIENT, whose first member tells what sort of
		// data (ie, string, float, etc) is in the VARIENT, COM interfaces can be used by just about
		// any language.
		VariantInit(&varFlags);
		varFlags.vt = VT_I4;
		varFlags.lVal = navNoHistory | navNoReadFromCache | navNoWriteToCache;

		VariantInit(&myURL);
		myURL.vt = VT_BSTR;

		if (myURL.bstrVal = bstrFromNullTermString(webPageURL))
		{
			char **eaEstrHeaderKeyValues = NULL;
			char *estrHeaders = NULL;
			int i;

			if(eaEstrKeyValuePostData)
			{
				char *estrPostData = NULL;
				char *arrPostData;

				for(i = 1; i < eaSize(&eaEstrKeyValuePostData); i += 2)
				{
					const char *estrKey = eaEstrKeyValuePostData[i - 1];
					const char *estrValue = eaEstrKeyValuePostData[i];
					if(i > 1)
						estrAppend2(&estrPostData, "&");
					estrConcatf(&estrPostData, "%s=%s", estrKey, estrValue);
				}

				sabPostData[0].lLbound = 0;
				sabPostData[0].cElements = (ULONG)estrLength(&estrPostData);
				saPostData = SafeArrayCreate(VT_UI1, 1, sabPostData);
				assert(saPostData);
				assert(SUCCEEDED(SafeArrayAccessData(saPostData, (void**)&arrPostData)));
				memcpy(arrPostData, estrPostData, estrLength(&estrPostData));
				SafeArrayUnaccessData(saPostData);

				VariantInit(&varPostData);
				varPostData.vt = VT_ARRAY|VT_UI1;
				varPostData.parray = saPostData;

				estrConcatf(&estrHeaders, "Content-Type: application/x-www-form-urlencoded\r\n");
			}

			BrowserAppendStandardDataToHeader(&eaEstrHeaderKeyValues);
			for(i = 1; i < eaSize(&eaEstrHeaderKeyValues); i += 2)
			{
				char *estrKey = eaEstrHeaderKeyValues[i - 1];
				char *estrValue = eaEstrHeaderKeyValues[i];

				estrConcatf(&estrHeaders, "%s: %s\r\n", estrKey, estrValue);
			}
			eaDestroyEString(&eaEstrHeaderKeyValues);

			if (estrHeaders)
			{
				VariantInit(&varHeaders);
				varHeaders.vt = VT_BSTR;

				if (varHeaders.bstrVal = bstrFromNullTermString(estrHeaders))
				{
					// Call the Navigate2() function to actually display the page.
					if (SUCCEEDED(sWebBrowser->lpVtbl->Navigate2(sWebBrowser, &myURL, &varFlags, NULL, eaEstrKeyValuePostData ? &varPostData : NULL, &varHeaders)))
					{
						// Success
						BrowserPageLoading();
						retVal = true;
					}

					VariantClear(&varHeaders);
				}

				estrDestroy(&estrHeaders);
			}

			if (eaEstrKeyValuePostData)
			{
				VariantClear(&varPostData);
			}

			VariantClear(&myURL);
		}

		VariantClear(&varFlags);
	}

	return retVal;
}

bool BrowserIEDisplayHTMLStr(const char *htmlString)
{
	bool retVal = false;

	if (sWebBrowser)
	{
		static const wchar_t Blank[] = {L"about:blank"};
		static const SAFEARRAYBOUND ArrayBound = {1, 0};
		SAFEARRAY		*sfArray;
		VARIANT			myURL;
		VARIANT			*pVar;

		VariantInit(&myURL);
		myURL.vt = VT_BSTR;

		// Ok, now the pointer to our IWebBrowser2 object is in 'webBrowser2', and so its VTable is
		// webBrowser2->lpVtbl.

		// Before we can get the IHTMLDocument2, we actually need to have some HTML page loaded in
		// the browser. So, let's load an empty HTML page. Then, once we have that empty page, we
		// can get that IHTMLDocument2 and call its write() to stuff our HTML string into it.

		// Call the IWebBrowser2 object's get_Document so we can get its DISPATCH object. I don't know why you
		// don't get the DISPATCH object via the browser object's QueryInterface(), but you don't.

		// Give a URL that causes the browser to display an empty page.
		myURL.bstrVal = SysAllocString(&Blank[0]);

		// Call the Navigate2() function to actually display the page.
		sWebBrowser->lpVtbl->Navigate2(sWebBrowser, &myURL, 0, 0, 0, 0);
		BrowserPageLoading();

		// Wait for blank page to finish loading
		if (WaitOnReadyState(READYSTATE_COMPLETE, 1000) > WORS_DESTROYED)
		{
			IDispatch *dispatch = NULL;

			// First, we need the IDispatch object
			if (SUCCEEDED(sWebBrowser->lpVtbl->get_Document(sWebBrowser, &dispatch)) && dispatch)
			{
				IHTMLDocument2 *htmlDoc = NULL;

				// Get the IHTMLDocument2 object embedded within the IDispatch object
				if (SUCCEEDED(dispatch->lpVtbl->QueryInterface(dispatch, &IID_IHTMLDocument2, (void **)&htmlDoc)) && htmlDoc)
				{
					// Our HTML must be in the form of a BSTR. And it must be passed to write() in an
					// array of "VARIANT" structs. So let's create all that.
					if ((sfArray = SafeArrayCreate(VT_VARIANT, 1, (SAFEARRAYBOUND *)&ArrayBound)))
					{
						if (SUCCEEDED(SafeArrayAccessData(sfArray, (void **)&pVar)) && pVar)
						{
							pVar->vt = VT_BSTR;

							// Store our BSTR pointer in the VARIENT.
							if ((pVar->bstrVal = bstrFromNullTermString(htmlString)))
							{
								// Pass the VARIENT with its BSTR to write() in order to shove our desired HTML string
								// into the body of that empty page we created above.
								retVal = SUCCEEDED(htmlDoc->lpVtbl->write(htmlDoc, sfArray));

								// Close the document. If we don't do this, subsequent calls to DisplayHTMLStr
								// would append to the current contents of the page
								htmlDoc->lpVtbl->close(htmlDoc);

								// Normally, we'd need to free our BSTR, but SafeArrayDestroy() does it for us
								//						SysFreeString(pVar->bstrVal);
							}
						}

						// Free the array. This also frees the VARIENT that SafeArrayAccessData created for us,
						// and even frees the BSTR we allocated with SysAllocString
						SafeArrayDestroy(sfArray);
					}

					htmlDoc->lpVtbl->Release(htmlDoc);
					htmlDoc = NULL;
				}

				dispatch->lpVtbl->Release(dispatch);
				dispatch = NULL;
			}
		}

		SysFreeString(myURL.bstrVal);
	}

	// An error
	return retVal;
}

// ============================================================================ PRIVATE FUNCTIONS ===================================================================================

// Creates/Allocates a BSTR from the passed null-terminated string (ANSI or UNICODE).
// NOTE: The caller must SysFreeString() the returned BSTR when done with it.
// RETURNS: Pointer to the BSTR, or 0 if failure.
static BSTR bstrFromNullTermString(const char *string)
{
	BSTR bstr;

	if (!IsWindowUnicode(sWindowHandle))
	{
		WCHAR *buffer;
		DWORD size;

		size = MultiByteToWideChar(CP_UTF8, 0, (char *)string, -1, 0, 0);
		if (!(buffer = (WCHAR *)GlobalAlloc(GMEM_FIXED, sizeof(WCHAR) * size))) return(0);
		MultiByteToWideChar(CP_UTF8, 0, (char *)string, -1, buffer, size);
		bstr = SysAllocString(buffer);
		GlobalFree(buffer);
	}
	else
	{
		bstr = SysAllocString((WCHAR *)string);
	}

	return bstr;
}

// Creates/Allocates a BSTR from the passed null-terminated string (ANSI or UNICODE).
// NOTE: The caller must GlobalFree() the returned char* when done with it.
// INPUT: bstr, which CAN be NULL.
// RETURNS: Pointer to the char*, or NULL if failure.  If bstr was null, will return empty string.
static char *nullTermStringFromBstr(BSTR bstr)
{
	DWORD size;
	char *strOut;

	if (bstr)
	{
		if (!IsWindowUnicode(sWindowHandle))
		{
			size = WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)((char *)bstr), -1, 0, 0, 0, 0);
			if (strOut = GlobalAlloc(GMEM_FIXED, size))
			{
				WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)((char *)bstr), -1, (char *)strOut, size, 0, 0);
			}
		}
		else
		{
			size = (*((short *)bstr) + 1) * sizeof(wchar_t);
			if (strOut = GlobalAlloc(GMEM_FIXED, size))
			{
				CopyMemory(strOut, (char *)bstr + 2, size);
			}
		}
	}
	else
	{
		if (strOut = GlobalAlloc(GMEM_FIXED, 1))
		{
			*strOut = 0;
		}
	}

	return strOut;
}

static IHTMLElement *getElement(const char *elementName)
{
	char *err = NULL;
	IHTMLElement *elem = NULL;
	assert(elementName);
	if (elementName && sWebBrowser)
	{
		IDispatch *dispatch = NULL;

		// First, we need the IDispatch object
		if (SUCCEEDED(sWebBrowser->lpVtbl->get_Document(sWebBrowser, &dispatch)) && dispatch)
		{
			IHTMLDocument2* htmlDoc = NULL;

			// Get the IHTMLDocument2 object embedded within the IDispatch object
			if (SUCCEEDED(dispatch->lpVtbl->QueryInterface(dispatch, &IID_IHTMLDocument2, (void **)&htmlDoc)) && htmlDoc)
			{
				IHTMLElementCollection *htmlCollection = NULL;

				// Get the IHTMLElementCollection object. We need this to get the IDispatch
				// object for the element the caller wants on the web page. And from that
				// IDispatch, we get the IHTMLElement object. Really roundabout, ain't it?
				// That's COM
				if (SUCCEEDED(htmlDoc->lpVtbl->get_all(htmlDoc, &htmlCollection)) && htmlCollection)
				{
					IDispatch *collectionDispatch = NULL;
					VARIANT	varName;
					VARIANT	varIndex;

					// Get the IDispatch for that element. We need to call the IHTMLElementCollection
					// object's item() function, passing it the name of the element. Note that we
					// have to pass the element name as a BSTR stuffed into a VARIENT struct. And
					// we also need to stuff the index into a VARIANT struct too.
					VariantInit(&varName);
					varName.vt = VT_BSTR;
					if ((varName.bstrVal = bstrFromNullTermString(elementName)))
					{
						VariantInit(&varIndex);
						varIndex.vt = VT_I4;
						varIndex.lVal = 0; // retrieve the first instance we find of this element

						if (SUCCEEDED(htmlCollection->lpVtbl->item(htmlCollection, varName, varIndex, &collectionDispatch)) && collectionDispatch)
						{
							// We can finally get the IHTMLElement object for the desired object.
							if (SUCCEEDED(collectionDispatch->lpVtbl->QueryInterface(collectionDispatch, &IID_IHTMLElement, (void **)&elem)) && elem)
							{
								// IE seems to hand back the last element on the page if none match the requested ID. Check we got what we asked for.
								BSTR bstrid = NULL;
								char *id_tstr = NULL;

								if (SUCCEEDED(elem->lpVtbl->get_id(elem, &bstrid))) // this will sometimes succeed but return a NULL bstr - code below handles this
								{
									id_tstr = nullTermStringFromBstr(bstrid);
									SysFreeString(bstrid);
								}

								if (!id_tstr || stricmp(id_tstr, elementName) != 0)
								{
									// Mismatch, I hate IE for thinking it should do this.
									elem->lpVtbl->Release(elem);
									elem = NULL;
								}
								// else elem reference is retained here

								if (id_tstr)
								{
									GlobalFree(id_tstr);
								}
							}

							// Release the IDispatch now that we got the IHTMLElement.
							collectionDispatch->lpVtbl->Release(collectionDispatch);
							collectionDispatch = NULL;
						}
						else if (gDebugMode)
						{
							// this is purely for general debugging - if we cannot find the html element that we're searching for,
							// we output some context of the html collection for inspection
							long i, items;
							estrPrintf(&err, "Cannot find element %s in collection:\n", elementName);
							htmlCollection->lpVtbl->get_length(htmlCollection, &items);
							for (i=0; i<items; ++i)
							{
								varIndex.lVal = i;
								if (SUCCEEDED(htmlCollection->lpVtbl->item(htmlCollection, varIndex, varIndex, &collectionDispatch)) && collectionDispatch)
								{
									if (SUCCEEDED(collectionDispatch->lpVtbl->QueryInterface(collectionDispatch, &IID_IHTMLElement, (void **)&elem)) && elem)
									{
										BSTR bstr = NULL;
										if (SUCCEEDED(elem->lpVtbl->get_outerHTML(elem, &bstr))) // this will sometimes succeed but return a NULL bstr - code below handles this
										{
											char *cstr = nullTermStringFromBstr(bstr);
											SysFreeString(bstr);
											estrConcatf(&err, "%s,\n", cstr);
											GlobalFree(cstr);
										}
										else
										{
											estrConcatf(&err, "err no outerhtml,\n");
										}

										elem->lpVtbl->Release(elem);
										elem = NULL;
									}

									collectionDispatch->lpVtbl->Release(collectionDispatch);
									collectionDispatch = NULL;
								}
							}
						}

						// We don't need the VARIANTs anymore. This frees the string that SysAllocString() gave us
						VariantClear(&varIndex);
					}

					VariantClear(&varName);

					// Release the IHTMLElementCollection now that we're done with it.
					htmlCollection->lpVtbl->Release(htmlCollection);
					htmlCollection = NULL;
				}

				htmlDoc->lpVtbl->Release(htmlDoc);
				htmlDoc = NULL;
			}

			dispatch->lpVtbl->Release(dispatch);
			dispatch = NULL;
		}
	}

	if (!elem && gDebugMode)
	{
		printf("ERROR: %s\n", err);
	}

	estrDestroy(&err);
	return elem;
}

static void OutputDetailedElementError(IHTMLElement *elem, const char *type)
{
	// output more information on error to help debug html issues
	BSTR tag_value = NULL;

	if (SUCCEEDED(elem->lpVtbl->get_tagName(elem, &tag_value))) // this will sometimes succeed but return a NULL bstr - code below handles this
	{
		BSTR as_value = NULL;

		char *tagName = nullTermStringFromBstr(tag_value);
		if (SUCCEEDED(elem->lpVtbl->get_outerHTML(elem, &as_value))) // this will sometimes succeed but return a NULL bstr - code below handles this
		{
			char *asString = nullTermStringFromBstr(as_value);
			if (gDebugMode)
			{
				printf("ERROR: Unable to cast <%s> to %s: %s\n", tagName, type, asString);
			}
			GlobalFree(asString);
			SysFreeString(as_value);
		}

		GlobalFree(tagName);
		SysFreeString(tag_value);
	}
}

static IHTMLInputElement *getInputElement(const char *inputElementName)
{
	IHTMLInputElement *inputElem = NULL;
	IHTMLElement *elem = getElement(inputElementName);

	if (elem)
	{
		if (FAILED(elem->lpVtbl->QueryInterface(elem, &IID_IHTMLInputElement, (void **)&inputElem)) || !inputElem)
		{
			OutputDetailedElementError(elem, "IHTMLInputElement");
		}

		elem->lpVtbl->Release(elem);
		elem = NULL;
	}

	return inputElem;
}

static IHTMLSelectElement *getSelectElement(const char *selectElementName)
{
	IHTMLSelectElement *selectElem = NULL;
	IHTMLElement *elem = getElement(selectElementName);

	if (elem)
	{
		if (FAILED(elem->lpVtbl->QueryInterface(elem, &IID_IHTMLSelectElement, (void **)&selectElem)) || !selectElem)
		{
			OutputDetailedElementError(elem, "IHTMLSelectElement");
		}

		elem->lpVtbl->Release(elem);
		elem = NULL;
	}

	return selectElem;
}

static void extractTriviaData(IOmNavigator *navigator, HRESULT (_stdcall *getter) (IOmNavigator*, BSTR*), char *key)
{
	BSTR result = NULL;

	if (SUCCEEDED(getter(navigator, &result)) && result)
	{
		int cstr_size = WideCharToMultiByte(CP_ACP, 0, (WCHAR *)result, -1, 0, 0, 0, 0);
		char *cstr = (char*)malloc(sizeof(char) * cstr_size);
		WideCharToMultiByte(CP_ACP, 0, (WCHAR *)result, -1, cstr, cstr_size, 0, 0);
		triviaPrintf(key, "%s", cstr);
		free(cstr);
		SysFreeString(result);
	}
}

/************************** WaitOnReadyState() **********************
* Waits for a page (that is currently loading into the browser (for
* the specified window) to be in specified state.
*
* hwnd =	Handle to the window hosting the browser object.
*
* rs =		The desired readystate to wait for. A list of ready states can
*			be found at:
* http://msdn.microsoft.com/library/default.asp?url=/workshop/browser/webbrowser/reference/enums/readystate_enum.asp
*
* timeout = How long to wait for the state to match 'rs'. 0 for no wait.
*
* webBrowser2 =	A pointer to the IWebBrowser2 for the browser control,
*					or null if not supplied.
*
* RETURNS: WORS_SUCCESS (0) if the loading page has achieved the specified
* state by the time WaitOnReadyState returns, WORS_TIMEOUT (-1) for timeout
* and WORS_DESTROYED (-2) if the window was destroyed or a webBrowser
* object could not be obtained from the window.
*/

static WaitOnReadyStateType WaitOnReadyState(READYSTATE rs, DWORD timeout)
{
	WaitOnReadyStateType retVal = WORS_ERROR;

	if (sWebBrowser)
	{
		READYSTATE rsi;

		// Get the current ready state of the loading page
		if (SUCCEEDED(sWebBrowser->lpVtbl->get_ReadyState(sWebBrowser, &rsi)))
		{
			// Is the current ready state at least as high as the caller needs?
			if (rsi >= rs)
			{
				// Yes, it is. Tell the caller that the page is in a state where
				// he can proceed with whatever he wants to do.
				retVal = WORS_SUCCESS;
			}
			else
			{
				// Ok, the loading page is not yet in the state that the caller
				// requires. We need to timeout. We can't just Sleep() for the
				// specified amount of time. Why? Because a page will not load
				// unless we are emptying out certain messages in our thread's
				// message queue. So we need to at least call doEvents() periodically
				// while we are waiting for the ready state to be achieved.
				DWORD dwStart = GetTickCount();

				do
				{
					// Empty out messages in the message queue.
					MSG	msg;

					while (PeekMessage(&msg, sWindowHandle, 0, 0, PM_REMOVE))
					{
						TranslateMessage(&msg); 
						DispatchMessage(&msg);
					}

					// Make sure our window with the browser object wasn't closed down in while processing messages.
					if (!IsWindow(sWindowHandle))
					{
						// Oops! It was. Get out of here with WORS_DESTROYED.
						retVal = WORS_DESTROYED;
						return retVal;
					}

					// Is the current ready state at least as high as the caller needs?
					sWebBrowser->lpVtbl->get_ReadyState(sWebBrowser, &rsi);
					if (rsi >= rs)
					{
						retVal = WORS_SUCCESS;
						return retVal;
					}

					// We may need a sleep here on Win9x/Me systems to avoid a system hang.
					Sleep(10);

					// Did we timeout yet?
				}
				while (!timeout || (GetTickCount() - dwStart) <= timeout);

				// We timed out before the page achieved the desired ready state.
				retVal = WORS_TIMEOUT;
			}
		}
	}

	return retVal;
}

// -------------------------------------------------------------------------------------------
// The browser uses our IDispatch to give feedback when certain actions occur on the web page.

static HRESULT STDMETHODCALLTYPE Dispatch_QueryInterface(IDispatch * This, REFIID riid, void **ppvObject)
{
	*ppvObject = 0;

	if (!memcmp(riid, &IID_IUnknown, sizeof(GUID)) || !memcmp(riid, &IID_IDispatch, sizeof(GUID)))
	{
		*ppvObject = (void *)This;

		// Increment its usage count. The caller will be expected to call our
		// IDispatch's Release() (ie, Dispatch_Release) when it's done with
		// our IDispatch.
		Dispatch_AddRef(This);

		return(S_OK);
	}

	*ppvObject = 0;
	return(E_NOINTERFACE);
}

static HRESULT STDMETHODCALLTYPE Dispatch_AddRef(IDispatch *This)
{
	return(InterlockedIncrement(&((_IDispatchEx *)This)->refCount));
}

static HRESULT STDMETHODCALLTYPE Dispatch_Release(IDispatch *dispatch)
{
	_IDispatchEx* dispatchEx = (_IDispatchEx *)dispatch;

	if (InterlockedDecrement( &dispatchEx->refCount ) == 0)
	{
		/* If you uncomment the following line you should get one message
		* when the document unloads for each successful call to
		* CreateEventHandler. If not, check you are setting all events
		* (that you set), to null or detaching them.
		*/
		// OutputDebugString("One event handler destroyed");

		GlobalFree((char *)dispatch);
		return 0;
	}

	return dispatchEx->refCount;
}

static HRESULT STDMETHODCALLTYPE Dispatch_GetTypeInfoCount(IDispatch *This, unsigned int *pctinfo)
{
	return(E_NOTIMPL);
}

static HRESULT STDMETHODCALLTYPE Dispatch_GetTypeInfo(IDispatch *This, unsigned int iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
	return(E_NOTIMPL);
}

static HRESULT STDMETHODCALLTYPE Dispatch_GetIDsOfNames(IDispatch *This, REFIID riid, OLECHAR ** rgszNames, unsigned int cNames, LCID lcid, DISPID * rgDispId)
{
	return(S_OK);
}

static HRESULT STDMETHODCALLTYPE Dispatch_Invoke(IDispatch *dispatch, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS * pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, unsigned int *puArgErr)
{
	IHTMLEventObj *	htmlEvent;
	_IDispatchEx * dispatchEx = (_IDispatchEx *)dispatch;

	// Get the IHTMLEventObj from the associated window.
	if (SUCCEEDED(dispatchEx->htmlWindow2->lpVtbl->get_event(dispatchEx->htmlWindow2, &htmlEvent)) && htmlEvent)
	{
		BSTR strType = NULL;

		// Get the event's type (ie, a BSTR) by calling the IHTMLEventObj's get_type().
		if (SUCCEEDED(htmlEvent->lpVtbl->get_type(htmlEvent, &strType)) && strType)
		{
			switch (dispatchEx->id)
			{
				case DISPATCH_CALLBACK_SELECT_ELEMENT_ON_CHANGE:
				{
					OnChangeCallbackFunc onChangeCallbackFunc = (OnChangeCallbackFunc)dispatchEx->callback;
					(*onChangeCallbackFunc)(dispatchEx->userdata);
					break;
				}
			}

			// Free anything allocated or gotten above
			SysFreeString(strType);
		}

		// Release the IHTMLEventObj.
		htmlEvent->lpVtbl->Release(htmlEvent);
		htmlEvent = NULL;
	}

	return S_OK;
}

// This is a simple C example. There are lots more things you can control about the browser object, but
// we don't do it in this example. _Many_ of the functions we provide below for the browser to call, will
// never actually be called by the browser in our example. Why? Because we don't do certain things
// with the browser that would require it to call those functions (even though we need to provide
// at least some stub for all of the functions).
//
// So, for these "dummy functions" that we don't expect the browser to call, we'll just stick in some
// assembly code that causes a debugger breakpoint and tells the browser object that we don't support
// the functionality. That way, if you try to do more things with the browser object, and it starts
// calling these "dummy functions", you'll know which ones you should add more meaningful code to.
#ifdef NDEBUG
#define NOTIMPLEMENTED return(E_NOTIMPL)
#else
#define NOTIMPLEMENTED assert(0); return(E_NOTIMPL)
#endif




//////////////////////////////////// My IDocHostUIHandler functions  //////////////////////////////////////
// The browser object asks us for the pointer to our IDocHostUIHandler object by calling our IOleClientSite's
// QueryInterface (ie, Site_QueryInterface) and specifying a REFIID of IID_IDocHostUIHandler.
//
// NOTE: You need at least IE 4.0. Previous versions do not ask for, nor utilize, our IDocHostUIHandler functions.

static HRESULT STDMETHODCALLTYPE UI_QueryInterface(IDocHostUIHandler *This, REFIID riid, LPVOID *ppvObj)
{
	// The browser assumes that our IDocHostUIHandler object is associated with our IOleClientSite
	// object. So it is possible that the browser may call our IDocHostUIHandler's QueryInterface()
	// to ask us to return a pointer to our IOleClientSite, in the same way that the browser calls
	// our IOleClientSite's QueryInterface() to ask for a pointer to our IDocHostUIHandler.
	//
	// Rather than duplicate much of the code in IOleClientSite's QueryInterface, let's just get
	// a pointer to our _IOleClientSiteEx object, substitute it as the 'This' arg, and call our
	// our IOleClientSite's QueryInterface. Note that since our _IDocHostUIHandlerEx is embedded right
	// inside our _IOleClientSiteEx, and comes immediately after the _IOleInPlaceSiteEx, we can employ
	// the following trickery to get the pointer to our _IOleClientSiteEx.
	return(Site_QueryInterface((IOleClientSite *)((char *)This - sizeof(IOleClientSite) - sizeof(_IOleInPlaceSiteEx)), riid, ppvObj));
}

static HRESULT STDMETHODCALLTYPE UI_AddRef(IDocHostUIHandler *This)
{
	return(1);
}

static HRESULT STDMETHODCALLTYPE UI_Release(IDocHostUIHandler *This)
{
	return(1);
}

// Called when the browser object is about to display its context menu.
static HRESULT STDMETHODCALLTYPE UI_ShowContextMenu(IDocHostUIHandler *This, DWORD dwID, POINT *ppt, IUnknown *pcmdtReserved, IDispatch *pdispReserved)
{
	POINT			pt;

	GetCursorPos(&pt);

	// If desired, we can pop up our own custom context menu here. But instead, let's
	// just post a WM_CONTENTMENU message to the window hosting the web browser (stored in our
	// _IOleInPlaceFrameEx). Then, we'll tell the browser not to bring up its own context menu,
	// by returning S_OK.
	if (IsDebuggerPresent())
	{
		return(S_FALSE);
	}
	else
	{
		PostMessage(((_IOleInPlaceSiteEx *)((char *)This - sizeof(_IOleInPlaceSiteEx)))->frame.window, WM_CONTEXTMENU, (WPARAM)pt.x, pt.y);
		return(S_OK);
	}

}

// Called at initialization of the browser object UI. We can set various features of the browser object here.
static HRESULT STDMETHODCALLTYPE UI_GetHostInfo(IDocHostUIHandler *This, DOCHOSTUIINFO *pInfo)
{
	pInfo->cbSize = sizeof(DOCHOSTUIINFO);

	// Set some flags. We don't want any 3D border. You can do other things like hide
	// the scroll bar (DOCHOSTUIFLAG_SCROLL_NO), disable picture display (DOCHOSTUIFLAG_NOPICS),
	// disable any script running when the page is loaded (DOCHOSTUIFLAG_DISABLE_SCRIPT_INACTIVE),
	// open a site in a new browser window when the user clicks on some link (DOCHOSTUIFLAG_OPENNEWWIN),
	// and lots of other things. See the MSDN docs on the DOCHOSTUIINFO struct passed to us.
	pInfo->dwFlags = DOCHOSTUIFLAG_NO3DBORDER;

	// Set what happens when the user double-clicks on the object. Here we use the default.
	pInfo->dwDoubleClick = DOCHOSTUIDBLCLK_DEFAULT;

	return(S_OK);
}

// Called when the browser object shows its UI. This allows us to replace its menus and toolbars by creating our
// own and displaying them here.
static HRESULT STDMETHODCALLTYPE UI_ShowUI(IDocHostUIHandler *This, DWORD dwID, IOleInPlaceActiveObject *pActiveObject, IOleCommandTarget __RPC_FAR *pCommandTarget, IOleInPlaceFrame __RPC_FAR *pFrame, IOleInPlaceUIWindow *pDoc)
{
	// We've already got our own UI in place so just return S_OK to tell the browser
	// not to display its menus/toolbars. Otherwise we'd return S_FALSE to let it do
	// that.
	return(S_OK);
}

// Called when browser object hides its UI. This allows us to hide any menus/toolbars we created in ShowUI.
static HRESULT STDMETHODCALLTYPE UI_HideUI(IDocHostUIHandler *This)
{
	return(S_OK);
}

// Called when the browser object wants to notify us that the command state has changed. We should update any
// controls we have that are dependent upon our embedded object, such as "Back", "Forward", "Stop", or "Home"
// buttons.
static HRESULT STDMETHODCALLTYPE UI_UpdateUI(IDocHostUIHandler *This)
{
	// We update our UI in our window message loop so we don't do anything here.
	return(S_OK);
}

// Called from the browser object's IOleInPlaceActiveObject object's EnableModeless() function. Also
// called when the browser displays a modal dialog box.
static HRESULT STDMETHODCALLTYPE UI_EnableModeless(IDocHostUIHandler *This, BOOL fEnable)
{
	return(S_OK);
}

// Called from the browser object's IOleInPlaceActiveObject object's OnDocWindowActivate() function.
// This informs off of when the object is getting/losing the focus.
static HRESULT STDMETHODCALLTYPE UI_OnDocWindowActivate(IDocHostUIHandler *This, BOOL fActivate)
{
	return(S_OK);
}

// Called from the browser object's IOleInPlaceActiveObject object's OnFrameWindowActivate() function.
static HRESULT STDMETHODCALLTYPE UI_OnFrameWindowActivate(IDocHostUIHandler *This, BOOL fActivate)
{
	return(S_OK);
}

// Called from the browser object's IOleInPlaceActiveObject object's ResizeBorder() function.
static HRESULT STDMETHODCALLTYPE UI_ResizeBorder(IDocHostUIHandler *This, LPCRECT prcBorder, IOleInPlaceUIWindow *pUIWindow, BOOL fRameWindow)
{
	return(S_OK);
}

// Called from the browser object's TranslateAccelerator routines to translate key strokes to commands.
static HRESULT STDMETHODCALLTYPE UI_TranslateAccelerator(IDocHostUIHandler *This, LPMSG lpMsg, const GUID *pguidCmdGroup, DWORD nCmdID)
{
	// We don't intercept any keystrokes, so we do nothing here. But for example, if we wanted to
	// override the TAB key, perhaps do something with it ourselves, and then tell the browser
	// not to do anything with this keystroke, we'd do:
	//
	//	if (pMsg && pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_TAB)
	//	{
	//		// Here we do something as a result of a TAB key press.
	//
	//		// Tell the browser not to do anything with it.
	//		return(S_FALSE);
	//	}
	//
	//	// Otherwise, let the browser do something with this message.
	//	return(S_OK);
	
	//if (lpMsg && lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_TAB)
	//{
	//	return(S_OK);
	//}

	// For our example, we want to make sure that the user can't invoke some key to popup the context
	// menu, so we'll tell it to ignore all msgs.
	return(S_FALSE);
}

// Called by the browser object to find where the host wishes the browser to get its options in the registry.
// We can use this to prevent the browser from using its default settings in the registry, by telling it to use
// some other registry key we've setup with the options we want.
static HRESULT STDMETHODCALLTYPE UI_GetOptionKeyPath(IDocHostUIHandler * This, LPOLESTR __RPC_FAR *pchKey, DWORD dw)
{
	// Let the browser use its default registry settings.
	return(S_FALSE);
}

// Called by the browser object when it is used as a drop target. We can supply our own IDropTarget object,
// IDropTarget functions, and IDropTarget VTable if we want to determine what happens when someone drags and
// drops some object on our embedded browser object.
static HRESULT STDMETHODCALLTYPE UI_GetDropTarget(IDocHostUIHandler * This, IDropTarget __RPC_FAR *pDropTarget, IDropTarget __RPC_FAR *__RPC_FAR *ppDropTarget)
{
	// Return our IDropTarget object associated with this IDocHostUIHandler object. I don't
	// know why we don't do this via UI_QueryInterface(), but we don't.

	// NOTE: If we want/need an IDropTarget interface, then we would have had to setup our own
	// IDropTarget functions, IDropTarget VTable, and create an IDropTarget object. We'd want to put
	// a pointer to the IDropTarget object in our own custom IDocHostUIHandlerEx object (like how
	// we may add an HWND member for the use of UI_ShowContextMenu). So when we created our
	// IDocHostUIHandlerEx object, maybe we'd add a 'idrop' member to the end of it, and
	// store a pointer to our IDropTarget object there. Then we could return this pointer as so:
	//
	// *pDropTarget = ((IDocHostUIHandlerEx FAR *)This)->idrop;
	// return(S_OK);

	// But for our purposes, we don't need an IDropTarget object, so we'll tell whomever is calling
	// us that we don't have one.
	return(S_FALSE);
}

// Called by the browser when it wants a pointer to our IDispatch object. This object allows us to expose
// our own automation interface (ie, our own COM objects) to other entities that are running within the
// context of the browser so they can call our functions if they want. An example could be a javascript
// running in the URL we display could call our IDispatch functions. We'd write them so that any args passed
// to them would use the generic datatypes like a BSTR for utmost flexibility.
static HRESULT STDMETHODCALLTYPE UI_GetExternal(IDocHostUIHandler *This, IDispatch **ppDispatch)
{
	// Return our IDispatch object associated with this IDocHostUIHandler object. I don't
	// know why we don't do this via UI_QueryInterface(), but we don't.

	// NOTE: If we want/need an IDispatch interface, then we would have had to setup our own
	// IDispatch functions, IDispatch VTable, and create an IDispatch object. We'd want to put
	// a pointer to the IDispatch object in our custom _IDocHostUIHandlerEx object (like how
	// we may add an HWND member for the use of UI_ShowContextMenu). So when we defined our
	// _IDocHostUIHandlerEx object, maybe we'd add a 'idispatch' member to the end of it, and
	// store a pointer to our IDispatch object there. Then we could return this pointer as so:
	//
	// *ppDispatch = ((_IDocHostUIHandlerEx FAR *)This)->idispatch;
	// return(S_OK);

	// But for our purposes, we don't need an IDispatch object, so we'll tell whomever is calling
	// us that we don't have one. Note: We must set ppDispatch to 0 if we don't return our own
	// IDispatch object.
	*ppDispatch = 0;
	return(S_FALSE);
}

/* ************************* asciiToNumW() **************************
* Converts the OLECHAR string of digits (expressed in base 10) to a
* 32-bit DWORD.
*
* val =	Pointer to the nul-terminated string of digits to convert.
*
* RETURNS: The integer value as a DWORD.
*
* NOTE: Skips leading spaces before the first digit.
*/

DWORD asciiToNumW(OLECHAR *val)
{
	OLECHAR			chr;
	DWORD			len;

	// Result is initially 0
	len = 0;

	// Skip leading spaces
	while (*val == ' ' || *val == 0x09) val++;

	// Convert next digit
	while (*val)
	{
		chr = *(val)++ - '0';
		if ((DWORD)chr > 9) break;
		len += (len + (len << 3) + chr);
	}

	return(len);
}

// Called by the browser object to give us an opportunity to modify the URL to be loaded.
static HRESULT STDMETHODCALLTYPE UI_TranslateUrl(IDocHostUIHandler *This, DWORD dwTranslate, OLECHAR *pchURLIn, OLECHAR **ppchURLOut)
{
	static const wchar_t Blank[] = {L"about:blank"};
	static const wchar_t Anchor[] = {L"#"};
	static const wchar_t AppUrl[] = {L"app:"};
	unsigned short	*src;
	unsigned short	*dest;
	DWORD			len;
	HINSTANCE		ret;
	HWND	hwnd;
	OLECHAR *pchURLInAfterHTTP;

	// Get length of URL
	src = pchURLIn;
	while ((*(src)++));
	--src;
	len = src - pchURLIn; 

	// See if the URL starts with 'app:'. We will use this as a "special link" that can be
	// placed on a web page. The URL will be in the format "app:XXX" where XXX is some
	// number. For example, maybe the following will be placed on the web page:
	//
	// <A HREF="app:1">Some special link</A>
	//
	// When the user clicks on it, we will substitute a blank page, and then send the
	// application a WM_APP message with wParam as the number after the app:. The
	// application can then do anything it wants as a result of this, for example,
	// call DisplayHTMLStr to load some other string in memory, or whatever.
	if (len >= 4 && _wcsnicmp(pchURLIn, (WCHAR *)AppUrl, wcslen(AppUrl)) == 0)
	{
		UrlArgumentList *pArgList = NULL;

		// Allocate a new buffer to return an "about:blank" URL
		dest = (OLECHAR *)CoTaskMemAlloc(12<<1);
		assertmsg(dest, "Unable to allocate memory for new URL");
		*ppchURLOut = dest;

		// Convert the number after the "app:"
		len = asciiToNumW(pchURLIn + 4);

		// Return "about:blank" or "#"
		// XHRs need to become "about:blank" while normal page links should become "#"
		switch (len)
		{
		case CLMSG_PAGE_LOADED: // Page load XHR
			CopyMemory(dest, &Blank[0], 12<<1);
			break;
		case CLMSG_ACTION_BUTTON_CLICKED: // Action button link
			CopyMemory(dest, &Anchor[0], 12<<1);
			break;
		case CLMSG_OPTIONS_CLICKED: // Options link
			CopyMemory(dest, &Anchor[0], 12<<1);
			break;
		case CLMSG_LOGIN_SUBMIT: // Login form
			CopyMemory(dest, &Anchor[0], 12<<1);
			break;
		case CLMSG_OPTIONS_SAVED: // Login form
			CopyMemory(dest, &Anchor[0], 12<<1);
			break;
		default:
			FatalErrorf("Unknown application URL %u", len);
		}

		// Get our host window. That was stored in our _IOleInPlaceFrameEx
		hwnd = ((_IOleInPlaceSiteEx *)((char *)This - sizeof(_IOleInPlaceSiteEx)))->frame.window;

		{
			char url[1024];
			WideToUTF8StrConvert(pchURLIn, SAFESTR(url));
			pArgList = urlToUrlArgumentList(url);
		}

		// Post a message to this window using WM_APP, and pass the number converted above.
		// Do not SendMessage()!. Post instead, since the browser does not like us changing
		// the URL within this here callback.
		PostMessage(hwnd, WM_APP, (WPARAM)len, (LPARAM)pArgList);

		// Tell browser that we returned a URL
		return(S_OK);
	}

	// We don't need to modify the URL. Note: We need to set ppchURLOut to 0 if we don't
	// return an OLECHAR (buffer) containing a modified version of pchURLIn.
	pchURLInAfterHTTP = wcsstr(pchURLIn, L"://");
	if (pchURLInAfterHTTP)
		pchURLInAfterHTTP += 3;
	if (pchURLInAfterHTTP)
	{
		ANALYSIS_ASSUME(pchURLInAfterHTTP);
		if (wcsstr(pchURLInAfterHTTP, L"/" LURL_SUFFIX_LAUNCHER))
		{
			*ppchURLOut = 0; 
			return(S_FALSE);
		}
	}
	else if (wcsicmp(pchURLIn, Blank) == 0 || wcsstr(pchURLIn, L"res:") != NULL)
	{
		*ppchURLOut = 0; 
		return(S_FALSE);
	}

	// For any other URL, send it to the default browser.
	ret = ShellExecuteW(NULL, L"open", pchURLIn, NULL, NULL, SW_SHOWNORMAL);

	// Send back a blank anchor
	dest = (OLECHAR *)CoTaskMemAlloc(2<<1);
	assertmsg(dest, "Unable to allocate memory for new URL");
	*ppchURLOut = dest;
	CopyMemory(dest, &Anchor[0], 2<<1);
	return(S_OK);
}

// Called by the browser when it does cut/paste to the clipboard. This allows us to block certain clipboard
// formats or support additional clipboard formats.
static HRESULT STDMETHODCALLTYPE UI_FilterDataObject(IDocHostUIHandler * This, IDataObject *pDO, IDataObject **ppDORet)
{
	// Return our IDataObject object associated with this IDocHostUIHandler object. I don't
	// know why we don't do this via UI_QueryInterface(), but we don't.

	// NOTE: If we want/need an IDataObject interface, then we would have had to setup our own
	// IDataObject functions, IDataObject VTable, and create an IDataObject object. We'd want to put
	// a pointer to the IDataObject object in our custom _IDocHostUIHandlerEx object (like how
	// we may add an HWND member for the use of UI_ShowContextMenu). So when we defined our
	// _IDocHostUIHandlerEx object, maybe we'd add a 'idata' member to the end of it, and
	// store a pointer to our IDataObject object there. Then we could return this pointer as so:
	//
	// *ppDORet = ((_IDocHostUIHandlerEx FAR *)This)->idata;
	// return(S_OK);

	// But for our purposes, we don't need an IDataObject object, so we'll tell whomever is calling
	// us that we don't have one. Note: We must set ppDORet to 0 if we don't return our own
	// IDataObject object.
	*ppDORet = 0;
	return(S_FALSE);
}






////////////////////////////////////// My IOleClientSite functions  /////////////////////////////////////
// We give the browser object a pointer to our IOleClientSite object when we call OleCreate() or DoVerb().

/************************* Site_QueryInterface() *************************
* The browser object calls this when it wants a pointer to one of our
* IOleClientSite, IDocHostUIHandler, or IOleInPlaceSite structures. They
* are all accessible via the _IOleClientSiteEx struct we allocated in
* EmbedBrowserObject() and passed to DoVerb() and OleCreate().
*
* This =		A pointer to whatever _IOleClientSiteEx struct we passed to
*				OleCreate() or DoVerb().
* riid =		A GUID struct that the browser passes us to clue us as to
*				which type of struct (object) it would like a pointer
*				returned for.
* ppvObject =	Where the browser wants us to return a pointer to the
*				appropriate struct. (ie, It passes us a handle to fill in).
*
* RETURNS: S_OK if we return the struct, or E_NOINTERFACE if we don't have
* the requested struct.
*/

static HRESULT STDMETHODCALLTYPE Site_QueryInterface(IOleClientSite *This, REFIID riid, void **ppvObject)
{
	// It just so happens that the first arg passed to us is our _IOleClientSiteEx struct we allocated
	// and passed to DoVerb() and OleCreate(). Nevermind that 'This' is declared is an IOleClientSite *.
	// Remember that in EmbedBrowserObject(), we allocated our own _IOleClientSiteEx struct, and lied
	// to OleCreate() and DoVerb() -- passing our _IOleClientSiteEx struct and saying it was an
	// IOleClientSite struct. It's ok. An _IOleClientSiteEx starts with an embedded IOleClientSite, so
	// the browser didn't mind. So that's what the browser object is passing us now. The browser doesn't
	// know that it's really an _IOleClientSiteEx struct. But we do. So we can recast it and use it as
	// so here.

	// If the browser is asking us to match IID_IOleClientSite, then it wants us to return a pointer to
	// our IOleClientSite struct. Then the browser will use the VTable in that struct to call our
	// IOleClientSite functions. It will also pass this same pointer to all of our IOleClientSite
	// functions.
	//
	// Actually, we're going to lie to the browser again. We're going to return our own _IOleClientSiteEx
	// struct, and tell the browser that it's a IOleClientSite struct. It's ok. The first thing in our
	// _IOleClientSiteEx is an embedded IOleClientSite, so the browser doesn't mind. We want the browser
	// to continue passing our _IOleClientSiteEx pointer wherever it would normally pass a IOleClientSite
	// pointer.
	// 
	// The IUnknown interface uses the same VTable as the first object in our _IOleClientSiteEx
	// struct (which happens to be an IOleClientSite). So if the browser is asking us to match
	// IID_IUnknown, then we'll also return a pointer to our _IOleClientSiteEx.

	if (!memcmp(riid, &IID_IUnknown, sizeof(GUID)) || !memcmp(riid, &IID_IOleClientSite, sizeof(GUID)))
		*ppvObject = &((_IOleClientSiteEx *)This)->client;

	// If the browser is asking us to match IID_IOleInPlaceSite, then it wants us to return a pointer to
	// our IOleInPlaceSite struct. Then the browser will use the VTable in that struct to call our
	// IOleInPlaceSite functions.  It will also pass this same pointer to all of our IOleInPlaceSite
	// functions (except for Site_QueryInterface, Site_AddRef, and Site_Release. Those will always get
	// the pointer to our _IOleClientSiteEx.
	//
	// Actually, we're going to lie to the browser. We're going to return our own _IOleInPlaceSiteEx
	// struct, and tell the browser that it's a IOleInPlaceSite struct. It's ok. The first thing in
	// our _IOleInPlaceSiteEx is an embedded IOleInPlaceSite, so the browser doesn't mind. We want the
	// browser to continue passing our _IOleInPlaceSiteEx pointer wherever it would normally pass a
	// IOleInPlaceSite pointer.
	else if (!memcmp(riid, &IID_IOleInPlaceSite, sizeof(GUID)))
		*ppvObject = &((_IOleClientSiteEx *)This)->inplace;

	// If the browser is asking us to match IID_IDocHostUIHandler, then it wants us to return a pointer to
	// our IDocHostUIHandler struct. Then the browser will use the VTable in that struct to call our
	// IDocHostUIHandler functions.  It will also pass this same pointer to all of our IDocHostUIHandler
	// functions (except for Site_QueryInterface, Site_AddRef, and Site_Release. Those will always get
	// the pointer to our _IOleClientSiteEx.
	//
	// Actually, we're going to lie to the browser. We're going to return our own _IDocHostUIHandlerEx
	// struct, and tell the browser that it's a IDocHostUIHandler struct. It's ok. The first thing in
	// our _IDocHostUIHandlerEx is an embedded IDocHostUIHandler, so the browser doesn't mind. We want the
	// browser to continue passing our _IDocHostUIHandlerEx pointer wherever it would normally pass a
	// IDocHostUIHandler pointer. My, we're really playing dirty tricks on the browser here. heheh.
	else if (!memcmp(riid, &IID_IDocHostUIHandler, sizeof(GUID)))
		*ppvObject = &((_IOleClientSiteEx *)This)->ui;

	// For other types of objects the browser wants, just report that we don't have any such objects.
	// NOTE: If you want to add additional functionality to your browser hosting, you may need to
	// provide some more objects here. You'll have to investigate what the browser is asking for
	// (ie, what REFIID it is passing).
	else
	{
		*ppvObject = 0;
		return(E_NOINTERFACE);
	}

	return(S_OK);
}

static HRESULT STDMETHODCALLTYPE Site_AddRef(IOleClientSite *This)
{
	return(1);
}

static HRESULT STDMETHODCALLTYPE Site_Release(IOleClientSite *This)
{
	return(1);
}

static HRESULT STDMETHODCALLTYPE Site_SaveObject(IOleClientSite *This)
{
	NOTIMPLEMENTED;
}

static HRESULT STDMETHODCALLTYPE Site_GetMoniker(IOleClientSite *This, DWORD dwAssign, DWORD dwWhichMoniker, IMoniker **ppmk)
{
	NOTIMPLEMENTED;
}

static HRESULT STDMETHODCALLTYPE Site_GetContainer(IOleClientSite *This, LPOLECONTAINER *ppContainer)
{
	// Tell the browser that we are a simple object and don't support a container
	*ppContainer = 0;

	return(E_NOINTERFACE);
}

static HRESULT STDMETHODCALLTYPE Site_ShowObject(IOleClientSite *This)
{
	return(NOERROR);
}

static HRESULT STDMETHODCALLTYPE Site_OnShowWindow(IOleClientSite *This, BOOL fShow)
{
	NOTIMPLEMENTED;
}

static HRESULT STDMETHODCALLTYPE Site_RequestNewObjectLayout(IOleClientSite *This)
{
	NOTIMPLEMENTED;
}











////////////////////////////////////// My IOleInPlaceSite functions  /////////////////////////////////////
// The browser object asks us for the pointer to our IOleInPlaceSite object by calling our IOleClientSite's
// QueryInterface (ie, Site_QueryInterface) and specifying a REFIID of IID_IOleInPlaceSite.

static HRESULT STDMETHODCALLTYPE InPlace_QueryInterface(IOleInPlaceSite *This, REFIID riid, LPVOID * ppvObj)
{
	// The browser assumes that our IOleInPlaceSite object is associated with our IOleClientSite
	// object. So it is possible that the browser may call our IOleInPlaceSite's QueryInterface()
	// to ask us to return a pointer to our IOleClientSite, in the same way that the browser calls
	// our IOleClientSite's QueryInterface() to ask for a pointer to our IOleInPlaceSite.
	//
	// Rather than duplicate much of the code in IOleClientSite's QueryInterface, let's just get
	// a pointer to our _IOleClientSiteEx object, substitute it as the 'This' arg, and call our
	// our IOleClientSite's QueryInterface. Note that since our IOleInPlaceSite is embedded right
	// inside our _IOleClientSiteEx, and comes immediately after the IOleClientSite, we can employ
	// the following trickery to get the pointer to our _IOleClientSiteEx.
	return(Site_QueryInterface((IOleClientSite *)((char *)This - sizeof(IOleClientSite)), riid, ppvObj));
}

static HRESULT STDMETHODCALLTYPE InPlace_AddRef(IOleInPlaceSite *This)
{
	return(1);
}

static HRESULT STDMETHODCALLTYPE InPlace_Release(IOleInPlaceSite *This)
{
	return(1);
}

static HRESULT STDMETHODCALLTYPE InPlace_GetWindow(IOleInPlaceSite *This, HWND * lphwnd)
{
	// Return the HWND of the window that contains this browser object. We stored that
	// HWND in our _IOleInPlaceSiteEx struct. Nevermind that the function declaration for
	// Site_GetWindow says that 'This' is an IOleInPlaceSite *. Remember that in
	// EmbedBrowserObject(), we allocated our own _IOleInPlaceSiteEx struct which
	// contained an embedded IOleInPlaceSite struct within it. And when the browser
	// called Site_QueryInterface() to get a pointer to our IOleInPlaceSite object, we
	// returned a pointer to our _IOleClientSiteEx. The browser doesn't know this. But
	// we do. That's what we're really being passed, so we can recast it and use it as
	// so here.
	*lphwnd = ((_IOleInPlaceSiteEx *)This)->frame.window;

	return(S_OK);
}

static HRESULT STDMETHODCALLTYPE InPlace_ContextSensitiveHelp(IOleInPlaceSite *This, BOOL fEnterMode)
{
	NOTIMPLEMENTED;
}

static HRESULT STDMETHODCALLTYPE InPlace_CanInPlaceActivate(IOleInPlaceSite *This)
{
	// Tell the browser we can in place activate
	return(S_OK);
}

static HRESULT STDMETHODCALLTYPE InPlace_OnInPlaceActivate(IOleInPlaceSite *This)
{
	// Tell the browser we did it ok
	return(S_OK);
}

static HRESULT STDMETHODCALLTYPE InPlace_OnUIActivate(IOleInPlaceSite *This)
{
	return(S_OK);
}

static HRESULT STDMETHODCALLTYPE InPlace_GetWindowContext(IOleInPlaceSite *This, LPOLEINPLACEFRAME *lplpFrame, LPOLEINPLACEUIWINDOW *lplpDoc, LPRECT lprcPosRect, LPRECT lprcClipRect, LPOLEINPLACEFRAMEINFO lpFrameInfo)
{
	// Give the browser the pointer to our IOleInPlaceFrame struct. We stored that pointer
	// in our _IOleInPlaceSiteEx struct. Nevermind that the function declaration for
	// Site_GetWindowContext says that 'This' is an IOleInPlaceSite *. Remember that in
	// EmbedBrowserObject(), we allocated our own _IOleInPlaceSiteEx struct which
	// contained an embedded IOleInPlaceSite struct within it. And when the browser
	// called Site_QueryInterface() to get a pointer to our IOleInPlaceSite object, we
	// returned a pointer to our _IOleClientSiteEx. The browser doesn't know this. But
	// we do. That's what we're really being passed, so we can recast it and use it as
	// so here.
	//
	// Actually, we're giving the browser a pointer to our own _IOleInPlaceSiteEx struct,
	// but telling the browser that it's a IOleInPlaceSite struct. No problem. Our
	// _IOleInPlaceSiteEx starts with an embedded IOleInPlaceSite, so the browser is
	// cool with it. And we want the browser to pass a pointer to this _IOleInPlaceSiteEx
	// wherever it would pass a IOleInPlaceSite struct to our IOleInPlaceSite functions.
	*lplpFrame = (LPOLEINPLACEFRAME)&((_IOleInPlaceSiteEx *)This)->frame;

	// We have no OLEINPLACEUIWINDOW
	*lplpDoc = 0;

	// Fill in some other info for the browser
	lpFrameInfo->fMDIApp = FALSE;
	lpFrameInfo->hwndFrame = ((_IOleInPlaceFrameEx *)*lplpFrame)->window;
	lpFrameInfo->haccel = 0;
	lpFrameInfo->cAccelEntries = 0;

	// Give the browser the dimensions of where it can draw. We give it our entire window to fill.
	// We do this in InPlace_OnPosRectChange() which is called right when a window is first
	// created anyway, so no need to duplicate it here.
	//	GetClientRect(lpFrameInfo->hwndFrame, lprcPosRect);
	//	GetClientRect(lpFrameInfo->hwndFrame, lprcClipRect);

	return(S_OK);
}

static HRESULT STDMETHODCALLTYPE InPlace_Scroll(IOleInPlaceSite *This, SIZE scrollExtent)
{
	NOTIMPLEMENTED;
}

static HRESULT STDMETHODCALLTYPE InPlace_OnUIDeactivate(IOleInPlaceSite *This, BOOL fUndoable)
{
	return(S_OK);
}

static HRESULT STDMETHODCALLTYPE InPlace_OnInPlaceDeactivate(IOleInPlaceSite *This)
{
	return(S_OK);
}

static HRESULT STDMETHODCALLTYPE InPlace_DiscardUndoState(IOleInPlaceSite *This)
{
	NOTIMPLEMENTED;
}

static HRESULT STDMETHODCALLTYPE InPlace_DeactivateAndUndo(IOleInPlaceSite *This)
{
	NOTIMPLEMENTED;
}

// Called when the position of the browser object is changed, such as when we call the IWebBrowser2's put_Width(),
// put_Height(), put_Left(), or put_Right().
static HRESULT STDMETHODCALLTYPE InPlace_OnPosRectChange(IOleInPlaceSite *This, LPCRECT lprcPosRect)
{
	if (sWebBrowser)
	{
		// We need to get the browser's IOleInPlaceObject object so we can call its SetObjectRects
		// function.
		IOleInPlaceObject *inplace = NULL;
		if (SUCCEEDED(sWebBrowser->lpVtbl->QueryInterface(sWebBrowser, &IID_IOleInPlaceObject, (void **)&inplace)) && inplace)
		{
			// Give the browser the dimensions of where it can draw.
			inplace->lpVtbl->SetObjectRects(inplace, lprcPosRect, lprcPosRect);
			inplace->lpVtbl->Release(inplace);
			inplace = NULL;
		}
	}

	return(S_OK);
}







////////////////////////////////////// My IOleInPlaceFrame functions  /////////////////////////////////////////

static HRESULT STDMETHODCALLTYPE Frame_QueryInterface(IOleInPlaceFrame *This, REFIID riid, LPVOID *ppvObject)
{
	*ppvObject = 0;

	if (!memcmp(riid, &IID_IUnknown, sizeof(GUID)) || !memcmp(riid, &IID_IOleInPlaceFrame, sizeof(GUID)))
	{
		*ppvObject = (void *)This;

		// Increment its usage count. The caller will be expected to call our
		// IOleInPlaceFrame's Release() (ie, Frame_Release) when it's done with
		// our IOleInPlaceFrame.
		Frame_AddRef(This);

		return(S_OK);
	}

	*ppvObject = 0;
	return(E_NOINTERFACE);
}

static HRESULT STDMETHODCALLTYPE Frame_AddRef(IOleInPlaceFrame *This)
{
	return(1);
}

static HRESULT STDMETHODCALLTYPE Frame_Release(IOleInPlaceFrame *This)
{
	return(1);
}

static HRESULT STDMETHODCALLTYPE Frame_GetWindow(IOleInPlaceFrame *This, HWND *lphwnd)
{
	// Give the browser the HWND to our window that contains the browser object. We
	// stored that HWND in our IOleInPlaceFrame struct. Nevermind that the function
	// declaration for Frame_GetWindow says that 'This' is an IOleInPlaceFrame *. Remember
	// that in EmbedBrowserObject(), we allocated our own IOleInPlaceFrameEx struct which
	// contained an embedded IOleInPlaceFrame struct within it. And then we lied when
	// Site_GetWindowContext() returned that IOleInPlaceFrameEx. So that's what the
	// browser is passing us. It doesn't know that. But we do. So we can recast it and
	// use it as so here.
	*lphwnd = ((_IOleInPlaceFrameEx *)This)->window;
	return(S_OK);
}

static HRESULT STDMETHODCALLTYPE Frame_ContextSensitiveHelp(IOleInPlaceFrame *This, BOOL fEnterMode)
{
	NOTIMPLEMENTED;
}

static HRESULT STDMETHODCALLTYPE Frame_GetBorder(IOleInPlaceFrame *This, LPRECT lprectBorder)
{
	NOTIMPLEMENTED;
}

static HRESULT STDMETHODCALLTYPE Frame_RequestBorderSpace(IOleInPlaceFrame * This, LPCBORDERWIDTHS pborderwidths)
{
	NOTIMPLEMENTED;
}

static HRESULT STDMETHODCALLTYPE Frame_SetBorderSpace(IOleInPlaceFrame * This, LPCBORDERWIDTHS pborderwidths)
{
	NOTIMPLEMENTED;
}

static HRESULT STDMETHODCALLTYPE Frame_SetActiveObject(IOleInPlaceFrame * This, IOleInPlaceActiveObject *pActiveObject, LPCOLESTR pszObjName)
{
	return(S_OK);
}

static HRESULT STDMETHODCALLTYPE Frame_InsertMenus(IOleInPlaceFrame * This, HMENU hmenuShared, LPOLEMENUGROUPWIDTHS lpMenuWidths)
{
	NOTIMPLEMENTED;
}

static HRESULT STDMETHODCALLTYPE Frame_SetMenu(IOleInPlaceFrame * This, HMENU hmenuShared, HOLEMENU holemenu, HWND hwndActiveObject)
{
	return(S_OK);
}

static HRESULT STDMETHODCALLTYPE Frame_RemoveMenus(IOleInPlaceFrame * This, HMENU hmenuShared)
{
	NOTIMPLEMENTED;
}

static HRESULT STDMETHODCALLTYPE Frame_SetStatusText(IOleInPlaceFrame * This, LPCOLESTR pszStatusText)
{
	return(S_OK);
}

static HRESULT STDMETHODCALLTYPE Frame_EnableModeless(IOleInPlaceFrame * This, BOOL fEnable)
{
	return(S_OK);
}

static HRESULT STDMETHODCALLTYPE Frame_TranslateAccelerator(IOleInPlaceFrame * This, LPMSG lpmsg, WORD wID)
{
	NOTIMPLEMENTED;
}



//////////////////////////////////////////////////////////////////////////
//
/*
static void EventHandler_BeforeNavigate2(SimpleWindow *window, IDispatch *pDisp, VARIANT *url, VARIANT *Flags, VARIANT *TargetFrameName, VARIANT *PostData, VARIANT *Headers, VARIANT_BOOL *Cancel) 
{
	//CrypticLauncherWindow *launcher = (CrypticLauncherWindow*)window->pUserData;
#if 0
	if (Headers && Headers->bstrVal)
	{
		char *h = BStr2TStr(window, Headers->bstrVal), *p;
		printf("%s", h);
		p = strstri(h, "en-us");
		if (p)
		{
			p[0] = 'f';
			p[1] = 'r';
			p[3] = 'f';
			p[4] = 'r';
			SysFreeString(Headers->bstrVal);
			Headers->bstrVal = TStr2BStr(window, h);
		}
		GlobalFree(h);
	}
#endif
}
*/

#define IS_LAUNCHER_URL(url, prefix, suffix) strStartsWith(url, prefix) && (p = strchr(url+strlen(prefix), '/')) && strstri(p+1, suffix)

static void EventHandler_DocumentComplete(IDispatch *pDisp, VARIANT *URL)
{
	char *curl, *p;
	if (URL)
	{
		curl = nullTermStringFromBstr(URL->bstrVal);
	}
	else
	{
		curl = nullTermStringFromBstr(L"");
	}

	BrowserPageComplete(curl);

	// Check the URL starts with "http://" and that the path starts with "launcher"
	if (!strcmp(curl, "about:blank") || IS_LAUNCHER_URL(curl, URL_PREFIX_HTTP, URL_SUFFIX_LAUNCHER) ||
		IS_LAUNCHER_URL(curl, URL_PREFIX_HTTPS, URL_SUFFIX_LAUNCHER))
	{
		BrowserIEOnPageLoaded();
		SendMessage(sWindowHandle, WM_APP, CLMSG_PAGE_LOADED, 0);
	}

	GlobalFree(curl);
}



//////////////////////////////////////////////////////////////////////////
// Implementation of IDispatch for event handler

static HRESULT STDMETHODCALLTYPE Events_QueryInterface(DWebBrowserEvents2 * This, REFIID riid, void **ppvObject)
{
	*ppvObject = 0;

	if (!memcmp(riid, &IID_IUnknown, sizeof(GUID)) || !memcmp(riid, &IID_IDispatch, sizeof(GUID)))
	{
		*ppvObject = (void *)This;

		// Increment its usage count. The caller will be expected to call our
		// IDispatch's Release() (ie, Dispatch_Release) when it's done with
		// our IDispatch.
		Events_AddRef(This);

		return(S_OK);
	}

	*ppvObject = 0;
	return(E_NOINTERFACE);
}

static HRESULT STDMETHODCALLTYPE Events_AddRef(DWebBrowserEvents2 *This)
{
	return(InterlockedIncrement(&((_DWebBrowserEvents2Ex *)This)->refCount));
}

static HRESULT STDMETHODCALLTYPE Events_Release(DWebBrowserEvents2 *This)
{
	if (InterlockedDecrement( &((_DWebBrowserEvents2Ex *)This)->refCount ) == 0)
	{
		/* If you uncomment the following line you should get one message
		* when the document unloads for each successful call to
		* CreateEventHandler. If not, check you are setting all events
		* (that you set), to null or detaching them.
		*/
		// OutputDebugString("One event handler destroyed");

		//GlobalFree(((char *)This - ((_DWebBrowserEvents2Ex *)This)->extraSize));
		return(0);
	}

	return(((_DWebBrowserEvents2Ex *)This)->refCount);
}

static HRESULT STDMETHODCALLTYPE Events_GetTypeInfoCount(DWebBrowserEvents2 *This, unsigned int *pctinfo)
{
	return(E_NOTIMPL);
}

static HRESULT STDMETHODCALLTYPE Events_GetTypeInfo(DWebBrowserEvents2 *This, unsigned int iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
	return(E_NOTIMPL);
}

static HRESULT STDMETHODCALLTYPE Events_GetIDsOfNames(DWebBrowserEvents2 *This, REFIID riid, OLECHAR ** rgszNames, unsigned int cNames, LCID lcid, DISPID * rgDispId)
{
	return(S_OK);
}

static HRESULT STDMETHODCALLTYPE Events_Invoke(DWebBrowserEvents2 *This, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS * pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, unsigned int *puArgErr)
{
	// This function is called to receive an event. The event is identified by the
	// dispIdMember argument. It is our responsibility to retrieve the event arguments
	// from the pDispParams->rgvarg array and call the event function.
	// If we do not handle an event we can return DISP_E_MEMBERNOTFOUND.
	// The variant member that we use for each argument is determined by the argument
	// type of the event function. eg. If an event has the argument long x we would
	// use the lVal member of the VARIANT struct.

	// Here is our message map, where we map dispids to function calls.
	switch (dispIdMember) {
	case DISPID_BEFORENAVIGATE2:
		// call BeforeNavigate
		// (parameters are on stack, thus in reverse order)
//		EventHandler_BeforeNavigate2(window,
//			            pDispParams->rgvarg[6].pdispVal,    // pDisp
//			            pDispParams->rgvarg[5].pvarVal,     // url
//			            pDispParams->rgvarg[4].pvarVal,     // Flags
//			            pDispParams->rgvarg[3].pvarVal,     // TargetFrameName
//			            pDispParams->rgvarg[2].pvarVal,     // PostData
//			            pDispParams->rgvarg[1].pvarVal,     // Headers
//			            pDispParams->rgvarg[0].pboolVal);   // Cancel
		break;
	case DISPID_NAVIGATECOMPLETE2:
		{
			char *url = nullTermStringFromBstr(pDispParams->rgvarg[0].pvarVal->bstrVal);
			// DISPID_DOCUMENTCOMPLETE messages are not sent about local URLs, so use this message instead only for "about:blank" page
			// loads in order to still send a message about the loading screen completing.
			if (strcmp(url, "about:blank"))
			{
				GlobalFree(url);
				break;
			}
			GlobalFree(url);

			// Fall through if this is an "about:blank" page
		}
	case DISPID_DOCUMENTCOMPLETE:
		EventHandler_DocumentComplete(pDispParams->rgvarg[1].pdispVal, pDispParams->rgvarg[0].pvarVal);
		break;
	}

	return(S_OK);
}
