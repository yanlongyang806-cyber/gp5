#pragma once

// ONE TIME FIXUP - Key-value conversion
// Each of these structs specifies a pair of keys
// For each account, any balance in one key is moved into the other
AUTO_STRUCT;
typedef struct KeyValueConversion
{
	char *pOldKey; // Key from which to convert
	char *pNewKey; // Key into which to convert
	char *pAccountList; // If specified, apply key change only to accounts in the text file at the specified location on disk
} KeyValueConversion;

// Stores config settings for general Account Server features that you don't feel deserve their own config file
AUTO_STRUCT;
typedef struct AccountServerConfig
{
	// django WebSrv address; Also used for AccountProxy.c:HandleRequestWebSrvGameEvent
	char *pWebSrvAddress;
	int iWebSrvPort;
	// Drupal site URL used for sending One-Time Code emails, only used if WebSrv is not set up
	char *pOneTimeCodeEmailURL;

	U32 uSavedClientMachinesMax; AST(DEFAULT(10)) // Number of saved CrypticClient machine IDs each user can have,
	U32 uSavedBrowserMachinesMax; AST(DEFAULT(10)) // Number of saved WebBrowser machine IDs each user can have, 

	U32 uOneTimeCodeDuration; AST(DEFAULT(7200)) // Number of seconds a One-Time Code is valid for after it was generated, default = 2 hours
	U32 uOneTimeCodeAttempts; AST(DEFAULT(3)) // Number of attempts per one-time code for a machine ID
	U32 uSaveNextMachineDuration; AST(DEFAULT(5)) // Number of days the "Save Next Machine" is valid for
	U32 uOneTimeCodeEmailDelay; AST(DEFAULT(MINUTES(10))) // Duplicate e-mails will not be sent faster than this

	U32 uMachineLockGracePeriod; // Number of days a newly created account has to log in for the first Cryptic Client and Web Browser login to auto-save the machine ID
	STRING_EARRAY eaMachineLockDomainWhitelist; // Accounts with e-mails in these domains won't have AG enabled

	STRING_EARRAY eaCurrencyKeys; AST(ESTRING) // List of key-values that are currencies, for transaction logging

	EARRAY_OF(KeyValueConversion) eaKeyValueConversions; AST(NAME(KeyValueConversion)) // ONE TIME FIXUP - List of key conversions for Zen conversion

	bool bVerifyEmployeeStatus; AST(DEFAULT(1)) // Employee status will restrict permission for those products requiring employee status
	STRING_EARRAY eaEmployeeEmailDomains; // Employee status is checked against this list. The domain strings shall exclude "@"
	char *pEmployeeStatusChangeAlertMailto; AST(ESTRING) // Comma separated email addresses to receive employee status change alerts
	char *pEmployeeStatusChangeAlertServer; AST(ESTRING) // Email server to talk to. Null to default to SMTP_DEFAULT_SERVER
	int iEmployeeStatusChangeAlertTimeout; AST(DEFAULT(60)) // Timeout before issuing an mail server not responsive alert

	U32 uSecondsXperfDumpLongFrame; AST(DEFAULT(5)) // If a frame is longer than this number of seconds, dump xperf
	U32 uSecondsXperfDumpReset; AST(DEFAULT(60)) // Won't dump again for this number of seconds after a xperf dump
	U32 uXperfDumpInitFrames; AST(DEFAULT(10000)) // Skip this many initial frames before measuring frame duration. Intended for the server start up

} AccountServerConfig;

SA_RET_NN_VALID AccountServerConfig *GetAccountServerConfig(void);
void LoadAccountServerConfig(void);

// Accessors
CONST_STRING_EARRAY GetCurrencyKeys(void);
CONST_EARRAY_OF(KeyValueConversion) GetKeyValueConversions(void);