#include "earray.h"
#include "file.h"
#include "hoglib.h"
#include "NameValuePair.h"
#include "patchdb.h"
#include "patchdb_h_ast.h"
#include "patchjournal.h"
#include "patchjournal_c_ast.h"
#include "patchhal.h"
#include "StringUtil.h"
#include "textparser.h"
#include "timing.h"

#include "patchserver.h" // Hack to get access to g_patchserver_config
#include "patchcommonutils.h"

AUTO_ENUM;
typedef enum
{
	JOURNAL_CHECKIN = 1,
	JOURNAL_NAMEVIEW,
	JOURNAL_CHECKOUT,
	JOURNAL_UNCHECKOUT,
	JOURNAL_PRUNE,
	JOURNAL_EXPIRES,
	JOURNAL_VIEWED,
	JOURNAL_VIEWEDEXTERNAL,
	JOURNAL_VIEWDIRTY,
	JOURNAL_VIEWCLEAN,
	JOURNAL_FILEEXPIRES,
} JournalType;

AUTO_STRUCT;
typedef struct JournalOrder
{
	JournalType type;
} JournalOrder;

AUTO_STRUCT;
typedef struct JournalFile
{
	char * filename;
	U32 checksum;
	U32 size;
	U32 modified;
	U32 header_size;
	U32 header_checksum;
	bool deleted;
	U32 expires;
} JournalFile;

AUTO_STRUCT;
typedef struct JournalCheckin
{
	char * author;
	char * comment;
	char * sandbox;
	int branch;
	U32 time;
	int incr_from;
	JournalFile ** files;
} JournalCheckin;

AUTO_STRUCT;
typedef struct JournalCheckout
{
	char **filenames;
	char * author;
	U32 time;
	char * sandbox;
	int branch;
} JournalCheckout;

AUTO_STRUCT;
typedef struct JournalRemoveCheckout
{
	char **filenames;
	int branch;
	char *sandbox;
} JournalRemoveCheckout;

AUTO_STRUCT;
typedef struct JournalNameView
{
	char * name;
	int branch;
	char * sandbox;
	int rev;
	char* comment;
	U32 expires;
} JournalNameView;

AUTO_STRUCT;
typedef struct JournalPrune
{
	char *filename;
	int version;
	int revision;
} JournalPrune;

AUTO_STRUCT;
typedef struct JournalExpires
{
	char *name;
	U32 expires;
} JournalExpires;

AUTO_STRUCT;
typedef struct JournalFileExpires
{
	char *name;
	U32 rev;
	U32 expires;
} JournalFileExpires;

AUTO_STRUCT;
typedef struct JournalViewed
{
	char *view_name;
} JournalViewed;

AUTO_STRUCT;
typedef struct JournalStartingState
{
	U32 currentRevision;
} JournalStartingState;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct PatchJournal
{
	JournalStartingState** startingStates;
	JournalOrder ** order;
	JournalCheckin ** checkins;
	JournalCheckout ** checkouts;
	JournalRemoveCheckout **uncheckouts;
	JournalNameView ** nameviews;
	JournalPrune **prunes;
	JournalExpires **expires;
	JournalFileExpires **fileexpires;
	JournalViewed **viewed;
} PatchJournal;

PatchJournal* journalCreate(U32 applyToRevision)
{
	PatchJournal* j = StructCreate(parse_PatchJournal);
	JournalStartingState* s = StructAlloc(parse_JournalStartingState);
	
	s->currentRevision = applyToRevision;
	
	eaPush(&j->startingStates, s);
	
	return j;
}

JournalCheckin* journalAddCheckin(PatchJournal *journal, const char *author, const char *sandbox, int branch, U32 time, int incr_from, const char *comment)
{
	JournalCheckin * checkin = StructAlloc(parse_JournalCheckin);
	JournalOrder * order = StructAlloc(parse_JournalOrder);

	order->type = JOURNAL_CHECKIN;
	eaPush(&journal->order, order);

	checkin->author = StructAllocString(author);
	checkin->branch = branch;
	checkin->comment = StructAllocString(comment);
	checkin->sandbox = StructAllocString(sandbox);
	checkin->time = time;
	checkin->incr_from = incr_from;
	eaPush(&journal->checkins, checkin);

	return checkin;
}

void journalAddFile(JournalCheckin *checkin, const char *filename, U32 checksum, U32 size, U32 modified,
					U32 header_size, U32 header_checksum, bool deleted, U32 expires)
{
	JournalFile * file = StructAlloc(parse_JournalFile);

	file->checksum = checksum;
	file->deleted = deleted;
	file->filename = StructAllocString(filename);
	file->modified = modified;
	file->size = size;
	file->header_checksum = header_checksum;
	file->header_size = header_size;
	file->expires = expires;

	eaPush(&checkin->files, file);
}

JournalCheckout* journalAddCheckout(PatchJournal *journal, const char *author, const char *sandbox, int branch, U32 time)
{
	JournalCheckout * checkout = StructAlloc(parse_JournalCheckout);
	JournalOrder * order = StructAlloc(parse_JournalOrder);

	order->type = JOURNAL_CHECKOUT;
	eaPush(&journal->order, order);

	checkout->author = StructAllocString(author);
	checkout->branch = branch;
	checkout->sandbox = StructAllocString(sandbox);
	checkout->time = time;
	eaPush(&journal->checkouts, checkout);

	return checkout;
}

void journalAddCheckoutFile(JournalCheckout *checkout, const char *filename)
{
	eaPush(&checkout->filenames, StructAllocString(filename));
}

JournalRemoveCheckout* journalRemoveCheckout(PatchJournal *journal, int branch, const char *sandbox)
{
	JournalRemoveCheckout *uncheckout = StructAlloc(parse_JournalRemoveCheckout);
	JournalOrder *order = StructAlloc(parse_JournalOrder);

	order->type = JOURNAL_UNCHECKOUT;
	eaPush(&journal->order, order);

	uncheckout->branch = branch;
	uncheckout->sandbox = StructAllocString(sandbox);
	eaPush(&journal->uncheckouts, uncheckout);

	return uncheckout;
}

void journalRemoveCheckoutFile(JournalRemoveCheckout *uncheckout, const char *filename)
{
	eaPush(&uncheckout->filenames, StructAllocString(filename));
}

void journalAddName(	PatchJournal *journal,
						 const char *name,
						 const char *sandbox,
						 int branch,
						 int rev,
						 const char* comment,
						 U32 expires)
{
	JournalNameView * nameview = StructAlloc(parse_JournalNameView);
	JournalOrder * order = StructAlloc(parse_JournalOrder);

	order->type = JOURNAL_NAMEVIEW;
	eaPush(&journal->order, order);

	nameview->branch = branch;
	nameview->name = StructAllocString(name);
	nameview->sandbox = StructAllocString(sandbox);
	nameview->rev = rev;
	nameview->comment = StructAllocString(comment);
	nameview->expires = expires;
	eaPush(&journal->nameviews, nameview);
}

void journalAddNameFlush(	U32 applyToRevision,
							const char *dbname,
							const char *name,
							const char *sandbox,
							int branch,
							int rev,
							const char* comment,
							U32 expires)
{
	PatchJournal *journal = journalCreate(applyToRevision);
	JournalNameView * nameview = StructAlloc(parse_JournalNameView);
	JournalOrder * order = StructAlloc(parse_JournalOrder);

	order->type = JOURNAL_NAMEVIEW;
	eaPush(&journal->order, order);

	nameview->branch = branch;
	nameview->name = StructAllocString(name);
	nameview->sandbox = StructAllocString(sandbox);
	nameview->rev = rev;
	nameview->comment = StructAllocString(comment);
	nameview->expires = expires;
	eaPush(&journal->nameviews, nameview);

	journalFlushAndDestroy(&journal, dbname);
}

void journalAddPrune(PatchJournal *journal, const char *filename, int revision)
{
	JournalPrune *prune = StructAlloc(parse_JournalPrune);
	JournalOrder *order = StructAlloc(parse_JournalOrder);

	order->type = JOURNAL_PRUNE;
	eaPush(&journal->order, order);

	prune->filename = StructAllocString(filename);
	prune->revision = revision;
	eaPush(&journal->prunes, prune);
}

void journalAddFileExpires(PatchJournal *journal, const char *filename, U32 rev, U32 expires)
{
	JournalFileExpires *fileexpires = StructAlloc(parse_JournalFileExpires);
	JournalOrder *order = StructAlloc(parse_JournalOrder);

	order->type = JOURNAL_FILEEXPIRES;
	eaPush(&journal->order, order);

	fileexpires->name = StructAllocString(filename);
	fileexpires->rev = rev;
	fileexpires->expires = expires;
	eaPush(&journal->fileexpires, fileexpires);
}

void journalAddExpiresFlush(U32 applyToRevision,
							const char *dbname,
							const char *view_name,
							U32 expires)
{
	PatchJournal *journal = journalCreate(applyToRevision);
	JournalExpires *jexpires = StructAlloc(parse_JournalExpires);
	JournalOrder *order = StructAlloc(parse_JournalOrder);

	order->type = JOURNAL_EXPIRES;
	eaPush(&journal->order, order);

	jexpires->name = StructAllocString(view_name);
	jexpires->expires = expires;
	eaPush(&journal->expires, jexpires);

	journalFlushAndDestroy(&journal, dbname);
}

void journalAddViewedExternalFlush(U32 applyToRevision, const char *dbname, const char *view_name)
{
	PatchJournal *journal = journalCreate(applyToRevision);
	JournalNameView * viewed = StructAlloc(parse_JournalNameView);
	JournalOrder * order = StructAlloc(parse_JournalOrder);

	order->type = JOURNAL_VIEWEDEXTERNAL;
	eaPush(&journal->order, order);

	viewed->name = StructAllocString(view_name);
	eaPush(&journal->nameviews, viewed);

	journalFlushAndDestroy(&journal, dbname);
}

void journalAddViewDirtyFlush(U32 applyToRevision, const char *dbname, const char *view_name)
{
	PatchJournal *journal = journalCreate(applyToRevision);
	JournalNameView * viewed = StructAlloc(parse_JournalNameView);
	JournalOrder * order = StructAlloc(parse_JournalOrder);

	order->type = JOURNAL_VIEWDIRTY;
	eaPush(&journal->order, order);

	viewed->name = StructAllocString(view_name);
	eaPush(&journal->nameviews, viewed);

	journalFlushAndDestroy(&journal, dbname);
}

void journalAddViewCleanFlush(U32 applyToRevision, const char *dbname, const char *view_name)
{
	PatchJournal *journal = journalCreate(applyToRevision);
	JournalNameView * viewed = StructAlloc(parse_JournalNameView);
	JournalOrder * order = StructAlloc(parse_JournalOrder);

	order->type = JOURNAL_VIEWCLEAN;
	eaPush(&journal->order, order);

	viewed->name = StructAllocString(view_name);
	eaPush(&journal->nameviews, viewed);

	journalFlushAndDestroy(&journal, dbname);
}

void journalFlushAndDestroy(PatchJournal **journal, const char *dbname)
{
	PERFINFO_AUTO_START_FUNC();
	if(journal)
	{
		if(*journal && eaSize(&(*journal)->order))
		{
			char journalfile[MAX_PATH];
			int success;
			sprintf(journalfile, "./%s.journal", dbname);
			success = ParserWriteTextFileAppend(journalfile, parse_PatchJournal, *journal, 0, 0);
			if (!success)
				FatalErrorf("Journal write %s failed", journalfile);
		}
		journalDestroy(journal);
	}
	PERFINFO_AUTO_STOP_FUNC();
}

void journalDestroy(PatchJournal **journal)
{
	StructDestroySafe(parse_PatchJournal, journal);
}

void journalMergeCheckin(PatchDB * db, JournalCheckin * journalCheckin)
{
	Checkin * checkin;
	int i;

	checkin = patchAddCheckin(db, journalCheckin->branch, journalCheckin->sandbox, journalCheckin->author, journalCheckin->comment, journalCheckin->time, journalCheckin->incr_from);
	
	for(i = 0; i < eaSize(&journalCheckin->files); i++)
	{
		JournalFile * jFile = journalCheckin->files[i];
		FileVersion *new_ver;
		new_ver = patchAddVersion(db, checkin, jFile->checksum, jFile->deleted, jFile->filename, jFile->modified, jFile->size,
			jFile->header_size, jFile->header_checksum, NULL, jFile->expires);
		// See updateAddVersion() for an explanation of the following.
		if (jFile->deleted)
			new_ver->modified = jFile->modified;
	}
}

bool journalRename(const char *dbname)
{
	char journalSrc[MAX_PATH], journalDst[MAX_PATH];
	sprintf(journalSrc, "./%s.journal", dbname);
	sprintf(journalDst, "./%s.journal.merge", dbname);
	if(fileExists(journalSrc))
	{
		assert(patchRenameWithAlert(journalSrc, journalDst) == 0);
		return true;
	}
	else if(fileExists(journalDst))
	{
		// this is left over from the initial load
		// FIXME: there's a race condition here. if something gets journaled before the first merge process runs.
		return true;
	}
	return false;
}

bool journalMerge(PatchDB *db, const char *dbname)
{
	char journalFile[MAX_PATH], manifestFile[MAX_PATH];
	PatchJournal journal = {0};
	int i, currentCheckin = 0, currentCheckout = 0, currentUncheckout = 0, currentNameView = 0, currentPrune = 0,
		currentExpires = 0, currentFileExpires = 0, currentViewed = 0, currentViewedExternal = 0;

	sprintf(journalFile, "./%s.journal.merge", dbname);
	if(!fileExists(journalFile))
		return false;

	loadstart_printf("Merging journal...");

	// Check the manifest is newer, if so bail out
	// XXX: This seems to not work well on master servers, should be fixed to be more robust <NPK 2009-12-04>
	sprintf(manifestFile, "./%s.manifest", dbname);
	if(g_patchserver_config.parent.server && fileNewer(journalFile, manifestFile))
	{
		journalBackup(dbname);
		loadend_printf("manifest newer, skipping");
		return false;
	}

	ParserReadTextFile(journalFile, parse_PatchJournal, &journal, 0);
	
	assert(eaSize(&journal.startingStates));
	assertmsgf(	eaSize(&db->checkins) - 1 == journal.startingStates[0]->currentRevision,
				"Journal to merge starts from rev %d, but latest rev in manifest is %d",
				journal.startingStates[0]->currentRevision,
				eaSize(&db->checkins) - 1);

	for(i = 0; i < eaSize(&journal.order); i++)
	{
		switch(journal.order[i]->type)
		{
			xcase JOURNAL_CHECKIN:
			{
				if(verify(currentCheckin < eaSize(&journal.checkins)))
				{
					JournalCheckin *checkin = journal.checkins[currentCheckin++];
					journalMergeCheckin(db, checkin);
				}
			}
			xcase JOURNAL_CHECKOUT:
			{
				if(verify(currentCheckout < eaSize(&journal.checkouts)))
				{
					int j;
					JournalCheckout *checkout = journal.checkouts[currentCheckout++];
					for(j = 0; j < eaSize(&checkout->filenames); j++)
					{
						DirEntry *dir = patchFindPath(db, checkout->filenames[j], 0);
						patchAddCheckout(db, dir, checkout->author, checkout->branch, checkout->sandbox, checkout->time);
					}
				}
			}
			xcase JOURNAL_UNCHECKOUT:
			{
				if(verify(currentUncheckout < eaSize(&journal.uncheckouts)))
				{
					int j;
					JournalRemoveCheckout *uncheckout = journal.uncheckouts[currentUncheckout++];
					for(j = 0; j < eaSize(&uncheckout->filenames); j++)
					{
						DirEntry *dir = patchFindPath(db, uncheckout->filenames[j], 0);
						patchRemoveCheckout(db, dir, uncheckout->branch, uncheckout->sandbox);
					}
				}
			}
			xcase JOURNAL_NAMEVIEW:
			{
				if(verify(currentNameView < eaSize(&journal.nameviews)))
				{
					JournalNameView *nameview = journal.nameviews[currentNameView++];
					NamedView *view = patchAddNamedView(db, nameview->name, nameview->branch, nameview->sandbox, nameview->rev, nameview->comment, nameview->expires, NULL, 0);
				}
			}
			xcase JOURNAL_PRUNE:
			{
				if(verify(currentPrune < eaSize(&journal.prunes)))
				{
					JournalPrune *prune = journal.prunes[currentPrune++];
					if(prune->version)
					{
						fileVersionRemoveById(db, prune->filename, prune->version);
					}
					else
					{
						fileVersionRemoveByRevision(db, prune->filename, prune->revision);
					}
				}
			}
			xcase JOURNAL_EXPIRES:
			{
				if(verify(currentExpires < eaSize(&journal.expires)))
				{
					JournalExpires *expires = journal.expires[currentExpires++];
					NamedView *view = patchFindNamedView(db, expires->name);
					assertmsgf(view, "Named view %s not found", expires->name);
					view->expires = expires->expires;
					view->dirty = true;
				}
			}
			xcase JOURNAL_VIEWED:
			{
				if(verify(currentViewed < eaSize(&journal.viewed)))
				{
					JournalViewed *viewed = journal.viewed[currentViewed++];
					NamedView *view = patchFindNamedView(db, viewed->view_name);
					assertmsgf(view, "Named view %s not found", viewed->view_name);
					view->viewed++;
				}
			}
			xcase JOURNAL_VIEWEDEXTERNAL:
			{
				if(verify(currentNameView < eaSize(&journal.nameviews)))
				{
					JournalNameView *viewed = journal.nameviews[currentNameView++];
					NamedView *view = patchFindNamedView(db, viewed->name);
					assertmsgf(view, "Named view %s not found", viewed->name);
					view->viewed_external = 1;
				}
			}
			xcase JOURNAL_VIEWDIRTY:
			{
				if(verify(currentNameView < eaSize(&journal.nameviews)))
				{
					JournalNameView *viewed = journal.nameviews[currentNameView++];
					NamedView *view = patchFindNamedView(db, viewed->name);
					assertmsgf(view, "Named view %s not found", viewed->name);
					view->dirty = true;
				}
			}
			xcase JOURNAL_VIEWCLEAN:
			{
				if(verify(currentNameView < eaSize(&journal.nameviews)))
				{
					JournalNameView *viewed = journal.nameviews[currentNameView++];
					NamedView *view = patchFindNamedView(db, viewed->name);
					assertmsgf(view, "Named view %s not found", viewed->name);
					view->dirty = false;
				}
			}
			xcase JOURNAL_FILEEXPIRES:
			{
				if(verify(currentFileExpires < eaSize(&journal.fileexpires)))
				{
					JournalFileExpires *expires = journal.fileexpires[currentFileExpires++];
					DirEntry *dir = patchFindPath(db, expires->name, false);
					FOR_EACH_IN_EARRAY(dir->versions, FileVersion, ver)
						if(ver->rev == expires->rev)
						{
							ver->expires = expires->expires;
							break;
						}
					FOR_EACH_END
				}
			}
			xdefault:
				assert(0);
		};
	}

	StructDeInit(parse_PatchJournal, &journal);

	loadend_printf("");
	return true;
}

void journalBackup(const char *dbname)
{
	char journalSrc[MAX_PATH], journalDst[MAX_PATH];
	sprintf(journalSrc, "./%s.journal.merge", dbname);
	sprintf(journalDst, "./history/%s.%i.journal", dbname, getCurrentFileTime());
	makeDirectoriesForFile(journalDst);
	assert(patchRenameWithAlert(journalSrc, journalDst) == 0);
	assert(fileGzip(journalDst));
}

// Find a Checkin in the list.
static Checkin *rebuildJournals_FindRev(Checkin **rebuild_checkins, int rev)
{
	int base = rebuild_checkins[0]->rev;
	int offset;
	if (rev < base)
		return NULL;
	offset = rev - base;
	if (offset >= eaSize(&rebuild_checkins))
		return NULL;
	return rebuild_checkins[offset];
}

// Find a FileVersion in a Checkin.
static FileVersion *rebuildJournals_FindVer(Checkin *checkin, char *name)
{
	EARRAY_CONST_FOREACH_BEGIN(checkin->versions, i, n);
	{
		FileVersion *ver = checkin->versions[i];
		if (!stricmp(ver->parent->path, name))
			return ver;
	}
	EARRAY_FOREACH_END;
	return NULL;
}

// Process a hog entry.
static bool rebuildJournals_FromHogEntry(HogFile *handle, HogFileIndex index, const char* filename, void * userData)
{
	char buffer[MAX_PATH];
	Checkin **rebuild_checkins = userData;
	char *slash;
	char *dir_name;
	int count;
	int rev;
	int time;
	char hogfilename[MAX_PATH];
	int branch;
	char sandbox[MAX_PATH];
	Checkin *checkin;
	FileVersion *ver;

	// Split out dir name.
	strcpy(buffer, filename);
	slash = strrchr(buffer, '/');
	assert(slash);
	*slash = 0;
	dir_name = buffer;

	// Parse metadata.
	sandbox[0] = 0;
	count = sscanf_s(slash + 1, "r%u_t%u_b%u%s", &rev, &time, &branch, SAFESTR(sandbox));
	assert(count == 3 || count == 4);

	// TODO: Support deletes.

	// Check time.
	assert(time);
	patchHALGetHogFileNameFromTimeStamp(SAFESTR(hogfilename), NULL, time);
	assert(strstri(hogFileGetArchiveFileName(handle), hogfilename + 2));

	// Find checkin.
	checkin = rebuildJournals_FindRev(rebuild_checkins, rev);
	if (!checkin)
		return true;

	// Copy checkin information.
	if (checkin->branch == -1)
	{
		Checkin *previous = rebuildJournals_FindRev(rebuild_checkins, rev - 1);
		Checkin *next = rebuildJournals_FindRev(rebuild_checkins, rev + 1);
		checkin->branch = branch;
		checkin->time = time;
		if (*sandbox)
			checkin->sandbox = strdup(sandbox);
		checkin->incr_from = PATCHREVISION_NONE;

		if (previous && previous->branch != -1)
			assert(previous->time <= checkin->time);
		if (next && next->branch != -1)
			assert(next->time >= checkin->time);
	}
	else
	{
		assert(checkin->branch == branch);
		assert(checkin->time == time);
		assert(!stricmp_safe(checkin->sandbox, sandbox));
	}

	// Create a FileVersion.
	ver = rebuildJournals_FindVer(checkin, dir_name);
	assert(!ver);
	ver = StructCreate(parse_FileVersion);
	ver->parent = StructCreate(parse_DirEntry);
	ver->parent->path = strdup(dir_name);
	eaPush(&checkin->versions, ver);

	// Set the FileVersion metadata.
	ver->checksum = hogFileGetFileChecksum(handle, index);
	ver->size = hogFileGetFileSize(handle, index);
	ver->modified = hogFileGetFileTimestamp(handle, index);

	// TODO: Support headers.

	return true;
}

// Process a log entry.
// Example log block: (Currently, these always happen together)
//   130614 13:37:51 19851567 PatchServer[164742932]: : PatchCmd(type request server UGCMaster uid 710835 ip 172.26.8.195 function handleFinishCheckin context_project Neverwinter_Nightugc context_sandbox "" context_branch 0 context_rev 2261160 context_author GameServer force 1 comment "UploadResources for ns Dungeon_ugc_300227594_d441e87c(Saving project 300227594)")
// 	 130614 13:37:51 19851568 PatchServer[164742932]: Finishing checkin (GameServer:172.26.8.195), comment: UploadResources for ns Dungeon_ugc_300227594_d441e87c(Saving project 300227594)
// 	 130614 13:37:51 19851569 PatchServer[164742932]: : WriteFile(hogfile ./Neverwinter_Nightugc/1371200000.hogg file Data/ns/Dungeon_ugc_300227594_d441e87c/autosave/autosave.ugcproject/r2261166_t1371217070_b0 size 6 pack_size 14 modified 1371217029)
// 	 130614 13:37:51 19851570 PatchServer[164742932]: : WriteFile(hogfile ./Neverwinter_Nightugc/1371200000.hogg file Data/ns/Dungeon_ugc_300227594_d441e87c/project/NW-DNZTNE2SA.gz/r2261166_t1371217070_b0 size 8110 pack_size 8127 modified 1371217029)
// 	 130614 13:37:51 19851571 PatchServer[164742932]: : PatchCmd(type response server UGCMaster uid 710835 ip 172.26.8.195 function handleFinishCheckin success 1 time 1371217070 rev 2261166)
static void rebuildJournals_FromLog(char *log, Checkin **rebuild_checkins)
{
	char *body, *body_end;
	char *body_escaped = NULL;
	NameValuePair **pairs = NULL;
	bool success;
	char *function;
	char *type_string;
	bool request;
	static char *saved_author = NULL;
	static char *saved_comment = NULL;
	static int saved_branch = 0;
	static int saved_time = 0;
	char *success_string;
	char *time_string;
	char *rev_string;
	int time, rev;
	Checkin *checkin;

	// Only look at PatchCmd logs.
	body = strchr(log, '(');
	if (!body)
		return;
	*body = 0;
	if (!strEndsWith(log, " PatchCmd"))
		return;
	++body;

	// Find the end of the body.
	body_end = strrchr(body, ')');
	if (!body_end)
		return;
	*body_end = 0;

	// Hack to deal with NameValuePairs inability to deal with parens in values.
	// See comment handling below.
	body_escaped = estrStackCreateFromStr(body);
	estrReplaceOccurrences(&body_escaped, "(", "\1");
	estrReplaceOccurrences(&body_escaped, ")", "\2");

	// Try to parse the name-value pairs.
	success = GetNameValuePairsFromString(body_escaped, &pairs, ",");
	estrDestroy(&body_escaped);
	if (!success)
		return;

	// Only look at handleFinishCheckin logs.
	function = GetValueFromNameValuePairs(&pairs, "function");
	if (stricmp_safe(function, "handleFinishCheckin"))
	{
		eaDestroyStruct(&pairs, parse_NameValuePair);
		return;
	}

	// Find out whether this is a request or a response.
	type_string = GetValueFromNameValuePairs(&pairs, "type");
	if (!stricmp_safe(type_string, "request"))
		request = true;
	else if (!stricmp_safe(type_string, "response"))
		request = false;
	else
	{
		eaDestroyStruct(&pairs, parse_NameValuePair);
		return;
	}

	// If it's the request, save some information, so we can remember it during the response.
	if (request)
	{
		char *escaped_comment = NULL;
		char *saved_branch_string;
		free(saved_author);
		free(saved_comment);
		saved_author = strdup(GetValueFromNameValuePairs(&pairs, "context_author"));
		escaped_comment = estrStackCreateFromStr(GetValueFromNameValuePairs(&pairs, "comment"));
		estrReplaceOccurrences(&escaped_comment, "\1", "(");
		estrReplaceOccurrences(&escaped_comment, "\2", ")");
		saved_comment = strdup(escaped_comment);
		estrDestroy(&escaped_comment);
		saved_branch_string = GetValueFromNameValuePairs(&pairs, "context_branch");
		saved_branch = atoi(saved_branch_string);
		eaDestroyStruct(&pairs, parse_NameValuePair);
		return;
	}

	// We're in the response: only process it if is was successful.
	success_string = GetValueFromNameValuePairs(&pairs, "success");
	if (stricmp_safe(success_string, "1"))
	{
		eaDestroyStruct(&pairs, parse_NameValuePair);
		return;
	}

	// Get time and rev.
	time_string = GetValueFromNameValuePairs(&pairs, "time");
	rev_string = GetValueFromNameValuePairs(&pairs, "rev");
	time = atoi(time_string);
	rev = atoi(rev_string);

	// Make sure this is a checkin that we're interested in.
	checkin = rebuildJournals_FindRev(rebuild_checkins, rev);
	if (!checkin)
	{
		eaDestroyStruct(&pairs, parse_NameValuePair);
		return;
	}
	
	// Copy basic checkin information, if necessary.
	// This might be filled in already from the hog.
	if (checkin->branch == -1)
	{
		Checkin *previous = rebuildJournals_FindRev(rebuild_checkins, rev - 1);
		Checkin *next = rebuildJournals_FindRev(rebuild_checkins, rev + 1);
		checkin->branch = saved_branch;
		checkin->time = time;
		checkin->incr_from = PATCHREVISION_NONE;

		if (previous && previous->branch != -1)
			assert(previous->time <= checkin->time);
		if (next && next->branch != -1)
			assert(next->time >= checkin->time);
	}
	else
	{
		assert(checkin->branch == saved_branch);
		assert(checkin->time == time);
	}

	// Copy checkin information that is only in the log file.
	checkin->author = saved_author;
	saved_author = NULL;
	checkin->comment = saved_comment;
	saved_comment = NULL;

	// Clean up.
	eaDestroyStruct(&pairs, parse_NameValuePair);
}

// Generate a journal entry from a checkin.
static void rebuildJournals_AddCheckin(const char *dbname, Checkin *checkin)
{
	PatchJournal *journal;
	JournalCheckin *journalCheckin;

	// Create Checkin journal.
	journal = journalCreate(checkin->rev - 1);
	journalCheckin = journalAddCheckin(	journal,
		checkin->author,
		checkin->sandbox,
		checkin->branch,
		checkin->time,
		checkin->incr_from,
		checkin->comment);

	// Add FileVersions.
	EARRAY_CONST_FOREACH_BEGIN(checkin->versions, i, n);
	{
		FileVersion *ver = checkin->versions[i];
		journalAddFile(journalCheckin,
			ver->parent->path,
			ver->checksum,
			ver->size,
			ver->modified,
			0,  // TODO header_size
			0,  // TODO header_checksum
			0,  // TODO deleted
			0);
	}
	EARRAY_FOREACH_END;

	// Flush journal;
	journalFlushAndDestroy(&journal, dbname);
}

// Try to rebuild journals hog file entries and log entries.
// TODO: This so far only has only been used with regular UGCMaster checkins generated by Game Servers.  For anything else, missing functionality probably needs to be added and carefully tested.
// TODO: Support sandboxes and incr_from.
AUTO_COMMAND;
void rebuildJournals(char *dbname, int first_rev, int last_rev, char *hogfile, char *logfile)
{
	static Checkin **rebuild_checkins = NULL;
	int rev;
	HogFile *hog;
	FILE *log;
	char *line = NULL;

	s_disable_cor_16585 = true;

	// Validate parameters.
	if (!dbname || !*dbname)
		FatalErrorf("dbname empty");
	if (first_rev > last_rev)
		FatalErrorf("first_rev > last_rev");
	assert(!rebuild_checkins);

	// Create checkins array.
	for (rev = first_rev; rev <= last_rev; ++rev)
	{
		Checkin *c = StructCreate(parse_Checkin);
		c->rev = rev;
		c->branch = -1;
		eaPush(&rebuild_checkins, c);
	}

	// Scan hog.
	hog = hogFileRead(hogfile, NULL, PIGERR_ASSERT, NULL, HOG_NOCREATE|HOG_READONLY|HOG_NO_REPAIR);
	if (!hog)
		FatalErrorf("Unable to open hog %s", hogfile);
	hogScanAllFiles(hog, rebuildJournals_FromHogEntry, rebuild_checkins);

	// Scan log.
	log = fopen(logfile, "r");
	if (!log)
		FatalErrorf("Unable to open log %s", logfile);
	estrStackCreate(&line);
	while (fgetEString(&line, log))
		rebuildJournals_FromLog(line, rebuild_checkins);
	estrDestroy(&line);

	// TODO: Fill checkins missing from log.

	// Generate journal from checkins.
	EARRAY_CONST_FOREACH_BEGIN(rebuild_checkins, i, n);
	{
		rebuildJournals_AddCheckin(dbname, rebuild_checkins[i]);
	}
	EARRAY_FOREACH_END;

	// Done.
	exit(0);
}

#include "patchjournal_c_ast.c"
