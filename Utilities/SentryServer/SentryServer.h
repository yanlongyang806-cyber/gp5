#pragma once

bool clientIsServerSideReadOnly(const char *pClientName);
bool clientIsAlertOnDisconnect(const char *pClientName);
void checkIfClientRemainsDisconnected(const char *pClientName);