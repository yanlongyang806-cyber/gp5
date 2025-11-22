#ifndef __SIMPLYGONSDKLOADER_H__
#define __SIMPLYGONSDKLOADER_H__

#include "SimplygonSDKCrypticNW.h"

namespace SimplygonSDK
	{
	/// Adds a location to look for the DLL in. This method must be called before calling Initialize()
	void AddSearchPath( const char *search_path );

	/// Loads and initializes the SDK
	int Initialize( ISimplygonSDK **pInterfacePtr , const char *SDKPath = NULL , char *LicenseData = NULL);

	/// Deinitializes the SDK, releases the DLL and all allocated memory
	void Deinitialize();

	/// Retrieves the error string of the error code.
	const char *GetError( int error_code );
	};
	
#endif //__SIMPLYGONSDKLOADER_H__
