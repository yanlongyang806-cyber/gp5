#ifndef _PATCHJOURNAL_H
#define _PATCHJOURNAL_H

typedef struct PatchDB PatchDB;
typedef struct PatchJournal PatchJournal;
typedef struct JournalCheckin JournalCheckin;
typedef struct JournalCheckout JournalCheckout;
typedef struct JournalRemoveCheckout JournalRemoveCheckout;
typedef struct JournalViewedExternal JournalViewedExternal;

// these flush immediately
void journalAddNameFlush(	U32 applyToRevision,
							SA_PARAM_NN_STR const char *dbname,
							SA_PARAM_NN_STR const char *name,
							const char *sandbox,
							int branch,
							int rev,
							const char* comment,
							U32 expires);

void journalAddExpiresFlush(U32 applyToRevision,
							SA_PARAM_NN_STR const char *dbname,
							SA_PARAM_NN_STR const char *view_name,
							U32 expires);
void journalAddViewedExternalFlush(U32 applyToRevision, const char *dbname, const char *view_name);
void journalAddViewDirtyFlush(U32 applyToRevision, const char *dbname, const char *view_name);
void journalAddViewCleanFlush(U32 applyToRevision, const char *dbname, const char *view_name);

// for those that don't flush immediately
SA_RET_NN_VALID PatchJournal* journalCreate(U32 applyToRevision);
void journalFlushAndDestroy(SA_PRE_NN_VALID SA_POST_FREE PatchJournal **journal, SA_PARAM_NN_STR const char *dbname);
void journalDestroy(SA_PRE_NN_VALID SA_POST_FREE PatchJournal **journal);

// these don't flush immediately
SA_RET_NN_VALID JournalCheckin* journalAddCheckin(SA_PARAM_NN_VALID PatchJournal *journal, SA_PARAM_NN_STR const char *author,
											  SA_PARAM_NN_STR const char *sandbox, int branch, U32 time, int incr_from,
											  SA_PARAM_NN_STR const char *comment);
void journalAddFile(SA_PARAM_NN_VALID JournalCheckin * checkin, SA_PARAM_NN_STR const char * filename, U32 checksum,
					U32 size, U32 modified, U32 header_size, U32 header_checksum, bool deleted, U32 expires);
void journalAddPrune(SA_PARAM_NN_VALID PatchJournal *journal, SA_PARAM_NN_STR const char *filename, int version);
SA_RET_NN_VALID JournalCheckout* journalAddCheckout(SA_PARAM_NN_VALID PatchJournal *journal, SA_PARAM_NN_STR const char *author,
												SA_PARAM_NN_STR const char *sandbox, int branch, U32 time);
void journalAddCheckoutFile(SA_PARAM_NN_VALID JournalCheckout *checkout, SA_PARAM_NN_STR const char *filename);
SA_RET_NN_VALID JournalRemoveCheckout* journalRemoveCheckout(SA_PARAM_NN_VALID PatchJournal *journal, int branch,
														 SA_PARAM_NN_STR const char *sandbox);
void journalRemoveCheckoutFile(SA_PARAM_NN_VALID JournalRemoveCheckout *uncheckout, SA_PARAM_NN_STR const char *filename);
void journalAddFileExpires(SA_PARAM_NN_VALID PatchJournal *journal, SA_PARAM_NN_STR const char *filename, U32 rev, U32 expires);
void journalAddName(SA_PARAM_NN_VALID PatchJournal *journal,
						 SA_PARAM_NN_STR const char *name,
						 const char *sandbox,
						 int branch,
						 int rev,
						 const char* comment,
						 U32 expires);


// playback
bool journalRename(SA_PARAM_NN_STR const char *dbname);
bool journalMerge(SA_PARAM_NN_VALID PatchDB *db, SA_PARAM_NN_STR const char *dbname);
void journalBackup(SA_PARAM_NN_STR const char *dbname);

#endif