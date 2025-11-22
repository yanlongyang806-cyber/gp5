#pragma once

int autopatchOk(NetLink *link);
void autopatchHandleMessage(Packet *pak,int cmd,NetLink* link,void *user_data);
void autopatchHandleMessage64(Packet *pak,int cmd,NetLink* link,void *user_data);
void autopatchReset();

