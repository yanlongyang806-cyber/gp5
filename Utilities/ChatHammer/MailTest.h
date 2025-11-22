#ifndef MAILTEST_H
#define MAILTEST_H

bool SendBasicMails(NetLink *link, const int count);
bool SendEmptyMails(NetLink *link, const int count);
bool SendBasicMailsToInvalidPlayers(NetLink *link, const int count);
bool SendItemMails(NetLink *link, const int count);
bool SendItemMailsToInvalidPlayers(NetLink *link, const int count);
bool GetMails(NetLink *link, const int count);
bool SetMailsRead(NetLink *link, const int count);
bool DeleteMails(NetLink *link, const int count);
void InitStaticMailUser(NetLink *link);
bool SendDeleteBasicMails(NetLink *link, const int count);
bool SendDeleteItemMails(NetLink *link, const int count);

#endif // MAILTEST_H