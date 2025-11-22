#pragma once

// returns false to stop enumeration
typedef bool (*HistoryItemCallbackFunc)(char *historyItem, void *userData);

extern void HistoryLoadFromRegistry(void);
extern void HistoryEnumerate(HistoryItemCallbackFunc historyItemCallbackFunc, void *userData); // called from patcher
extern void HistoryAddItemAndSaveToRegistry(char *historyItem);
