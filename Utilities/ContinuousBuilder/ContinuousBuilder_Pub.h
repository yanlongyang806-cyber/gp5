#pragma once

AUTO_ENUM;
typedef enum enumCBResult 
{
	CBRESULT_NONE, //no last result, this is the first time
	CBRESULT_SUCCEEDED,
	CBRESULT_FAILED,
	CBRESULT_SUCCEEDED_W_ERRS,

	//the script aborted with neither success nor failure (ie, it detected there had been
	//no changes)
	CBRESULT_ABORTED,

	//used by CBMonitor... one build was running, then suddenly another build was running, we don't know why
	CBRESULT_UNKNOWN,
} enumCBResult;