#include "accountnet.h"
#include "chatCommonFake.h"
#include "cmdparse.h"
#include "GlobalTypes.h"
#include "MemoryPool.h"
#include "objPath.h"
#include "rand.h"
#include "sock.h"
#include "structNet.h"
#include "textparser.h"

#include "ChatHammer.h"
#include "MailTest.h"

#include "AutoGen/ChatHammer_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"

extern int giDelay;
extern U32 giTestLength;

extern bool gbDebugText;

extern U32 gTestStartTime;

int giBasicMails = 6;
AUTO_CMD_INT(giBasicMails, SetBasicMails); // Number of basic mails to add per tick.

int giItemMails = 6;
AUTO_CMD_INT(giItemMails, SetItemMails); // Number of basic mails to add per tick.

int giMailGets = 6;
AUTO_CMD_INT(giItemMails, SetMailGets); // Number of mail gets per tick.

int giMailReads = 6;
AUTO_CMD_INT(giMailReads, SetMailReads); // Number of mail reads per tick.

int giMailDeletes = 6;
AUTO_CMD_INT(giMailDeletes, SetMailDeletes); // Number of mail deletes per tick.

static void SendBasicMail(NetLink *link, int from_user_id, int to_user_id, const char *shard_name, const char *subject, const char *body);
static void SendItemMail(NetLink *link, int from_user_id, int to_user_id, const char *shard_name, const char *subject, const char *body, U32 item_id);
static void GetMail(NetLink *link, int user_id);
static void SetMailRead(NetLink *link, int user_id, int mail_id);
static void DeleteMail(NetLink *link, int user_id, int mail_id);

ChatTestCase MailTests[] = 
{
	{"Send Basic Mail", NULL, &SendBasicMails, &giBasicMails},
	{"Send Basic Mail to invalid players", NULL, &SendBasicMailsToInvalidPlayers, &giBasicMails},
	{"Send Empty Mail", NULL, &SendEmptyMails, &giBasicMails},
	{"Send Item Mail", NULL, &SendItemMails, &giItemMails},
	{"Send Item Mail to invalid players", NULL, &SendItemMailsToInvalidPlayers, &giItemMails},
	{"Get Mail", NULL, &GetMails, &giMailGets},
	{"Set Mail Read", NULL, &SetMailsRead, &giMailReads},
	{"Send and Delete Basic Mails", &InitStaticMailUser, &SendDeleteBasicMails, &giBasicMails},
	{"Send and Delete Item Mails", &InitStaticMailUser, &SendDeleteItemMails, &giItemMails},
};

int GetMailTestSize()
{
	return sizeof(MailTests)/sizeof(ChatTestCase);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static int gMailUser;

static void SendBasicMail(NetLink *link, int from_user_id, int to_user_id, const char *shard_name, const char *subject, const char *body)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerSendMailByID %d %d \"%s\" \"%s\" \"%s\"", from_user_id, to_user_id, shard_name, subject, body);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

bool SendBasicMails(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		SendBasicMail(link, randomIntRange(1, next_user_id - 1), randomIntRange(1, next_user_id - 1), "", "To whom it may concern.", "A short message.");
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool SendEmptyMails(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		SendBasicMail(link, randomIntRange(1, next_user_id - 1), randomIntRange(1, next_user_id - 1), "", "", "");
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool SendBasicMailsToInvalidPlayers(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		SendBasicMail(link, randomIntRange(1, next_user_id - 1), next_user_id, "", "To whom it may concern.", "A short message.");
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

static void SendItemMail(NetLink *link, int from_user_id, int to_user_id, const char *shard_name, const char *subject, const char *body, U32 item_id)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	ChatMailStruct *mail;

	mail = StructCreate(parse_ChatMailStruct);

	mail->uID = 0;
	mail->sent = timeSecondsSince2000();
	mail->fromID = from_user_id;
	estrCopy2(&mail->subject, subject);
	estrCopy2(&mail->body, body);
	mail->uLotID = item_id;

	estrCreate(&command_string);
	estrConcatf(&command_string, "ChatServerSendMailItemsByID %d %d \"%s\"", from_user_id, to_user_id, shard_name);
	estrConcatf(&command_string, " ");

	ParserWriteTextEscaped(&command_string, parse_ChatMailStruct, mail, 0, 0, 0);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

bool SendItemMails(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		SendItemMail(link, randomIntRange(1, next_user_id - 1), randomIntRange(1, next_user_id - 1), "", "To whom it may concern.", "A short message.", 42);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool SendItemMailsToInvalidPlayers(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		SendItemMail(link, randomIntRange(1, next_user_id - 1), next_user_id, "", "To whom it may concern.", "A short message.", 42);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

static void GetMail(NetLink *link, int user_id)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerGetMail %d", user_id);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

bool GetMails(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		GetMail(link, randomIntRange(1, next_user_id - 1));
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

static void SetMailRead(NetLink *link, int user_id, int mail_id)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerSetMailRead %d %d", user_id, mail_id);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

bool SetMailsRead(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		SetMailRead(link, randomIntRange(1, next_user_id - 1), randomIntRange(1, 20));
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

static void DeleteMail(NetLink *link, int user_id, int mail_id)
{
	Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
	char *command_string = NULL;
	estrCreate(&command_string);
	estrPrintf(&command_string, "ChatServerDeleteMail %d %d", user_id, mail_id);

	pktSendString(pkt, command_string);
	ChatHammer_pktSend(&pkt);	
	estrDestroy(&command_string);
}

bool DeleteMails(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		DeleteMail(link, randomIntRange(1, next_user_id - 1), randomIntRange(1, 5));
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

static int gMailId;

void InitStaticMailUser(NetLink *link)
{
	gMailUser = randomIntRange(1, next_user_id - 1);
	gMailId = 1;
}

bool SendDeleteBasicMails(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		SendBasicMail(link, randomIntRange(1, next_user_id - 1), gMailUser, "", "To whom it may concern.", "A short message.");
		DeleteMail(link, gMailUser, gMailId++);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}

bool SendDeleteItemMails(NetLink *link, const int count)
{
	int i;

	if(next_user_id <= 1)
		return true;

	for(i = 0; i < count; ++i)
	{
		SendItemMail(link, randomIntRange(1, next_user_id - 1), gMailUser, "", "To whom it may concern.", "A short message.", 42);
		DeleteMail(link, gMailUser, gMailId++);
	}
	return gTestStartTime + giTestLength <= timeSecondsSince2000();
}
