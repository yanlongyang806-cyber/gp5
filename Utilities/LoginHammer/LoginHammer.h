#ifndef LOGINHAMMER_H
#define LOGINHAMMER_H

typedef struct NetLink NetLink;
typedef U32 ContainerID;

typedef enum LoginAttemptState
{
	LH_INITIAL,
	LH_ACCOUNT_INITIAL,
	LH_ACCOUNT_CONNECT,
	LH_ACCOUNT_TICKET,
	LH_LOGIN_INITIAL,
	LH_LOGIN_CONNECT,
	LH_LOGIN_CHARACTERS,
	LH_LOGIN_CREATIONDATA,
	LH_LOGIN_CREATE,
	LH_LOGIN_MAPS,
	LH_LOGIN_TRANSFER,
	LH_SERVER_INITIAL,
	LH_SERVER_CONNECT,
	LH_SERVER_CONNECTED,
	LH_DONE
} LoginAttemptState;

typedef struct LoginAttempt
{
	int id;
	int timer;
	LoginAttemptState state;
	char *account;
	char *error;

	// AccountServer ticket request support
	NetLink *accountLink;
	bool bPass;
	U32 uAccountID;
	U32 uTicketID;

	// AccountServer results
	bool bAccountConnectFailure;
	bool bAccountConnectTimeout;
	bool bAccountTicketFailure;
	bool bAccountTicketTimeout;
	bool bAccountAuthed;

	// AccountServer timing
	float fAccountConnectWait;
	float fAccountTicketWait;
	float fAccountWait;

	// LoginServer interaction
	NetLink *loginLink;
	bool bCreate;
	char *character;

	// LoginServer results
	bool bLoginConnectFailure;
	bool bLoginConnectTimeout;
	bool bLoginCharactersFailure;
	bool bLoginCharactersTimeout;
	bool bLoginCreationDataFailure;
	bool bLoginCreationDataTimeout;
	bool bLoginMapsFailure;
	bool bLoginMapsTimeout;
	bool bLoginTransferFailure;
	bool bLoginTransferTimeout;
	bool bLoginAuthed;

	// LoginServer timing
	float fLoginConnectWait;
	float fLoginCharactersWait;
	float fLoginCreationDataWait;
	float fLoginCreationWait;
	float fLoginMapsWait;
	float fLoginTransferWait;
	float fLoginWait;

	// GameServer interaction
	NetLink *serverLink;
	char server[32];
	int iServerPort;
	int iServerID;
	int iServerCookie;
	ContainerID iServerContainerID;
	
	// GameServer results
	bool bServerConnectFailure;
	bool bServerConnectTimeout;

	// GameServer timing
	float fServerConnectWait;

	// GameAccountData retrieval timing
	float fGADGetWait;
} LoginAttempt;

typedef struct ChatAuthData ChatAuthData;

typedef enum ChatAttemptState
{
	CHAT_ATTEMPT_CONNECT = 0,
	CHAT_ATTEMPT_AUTH,
	CHAT_ATTEMPT_DONE,
} ChatAttemptState;

typedef struct ChatAttempt
{
	ChatAuthData *pData;
	ChatAttemptState eState;
	int timer;

	float fChatRelayConnectWait;
	float fChatRelayAuthWait;
	bool bFailed;
	char *pLastCommand;
} ChatAttempt;

#endif