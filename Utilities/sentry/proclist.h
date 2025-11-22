#ifndef _PROCLIST_H
#define _PROCLIST_H

#include "stdtypes.h"
#include "net/net.h"

#define NUM_TICKS 60

#define MAX_TITLE 1000
#define MAX_EXENAME 100
#define MAX_EXEPATH 1000
typedef struct
{
	U32		process_id;
	U32		time_tables[NUM_TICKS];
	U8		tag;
	U32		count;
	S64		mem_used_phys;
	S64		mem_used_virt;
	char	exename[MAX_EXENAME];
	char    exepath[MAX_EXEPATH];
	char	title[MAX_TITLE];
} ProcessInfo;

typedef struct
{
	ProcessInfo	**processes;
	ProcessInfo	total;
	U32			timestamp_tables[NUM_TICKS]; // Array of times (in ms) when sample occured
	U32			total_offset; // The number of accumulated msecs from processees who have exited since our sampling began
} ProcessList;

extern ProcessList process_list;

void procSendTrackedInfo(Packet *pak);
void procGetList(void);
void procSendList(Packet *pak);
void procKillByName(char *process_name);


#endif
