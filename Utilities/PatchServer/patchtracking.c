#include "net.h"
#include "logging.h"
#include "patchproject.h"
#include "patchserver.h"
#include "patchserver_h_ast.h"
#include "patchserverdb.h"
#include "patchtracking.h"
#include "patchtracking_c_ast.h"
#include "ResourceInfo.h"
#include "StringUtil.h"
#include "textparser.h"
#include "timing.h"

// Exponential weighting weight
#define EXP_WEIGHT 0.4

// Maximum number of update times to allow.
#define MAX_UPDATE_NUMBER 25

// Kill switch for patch tracking.
static bool s_disable_patch_tracking = false;
AUTO_CMD_INT(s_disable_patch_tracking, disable_patch_tracking);

// Information about how long it took to update a checkin.
AUTO_STRUCT;
typedef struct PatchServerUpdateTime
{
	int rev;											// (master/xfer) Revision number
	float duration;					AST(DEFAULT(-1))	// (master/xfer) If synced, how long it took to update this checkin
	time_t eta;						AST(INT)			// (master) If not synced, when we think it will be synced
} PatchServerUpdateTime;

// The state of a child's updating, for a particular database.
AUTO_STRUCT;
typedef struct PatchServerUpdateInformationDatabase
{
	STRING_POOLED database;			AST(POOL_STRING)	// (master/xfer) Name of the database
	float database_overhead;		AST(DEFAULT(-1))	// (master/xfer) Overhead for this database
	int latest_rev;					AST(DEFAULT(-1))	// (master/xfer) Latest checkin to be completely updated
	char *latest_view_name;								// (master/xfer) Latest view to be completely updated
	int latest_view_index;			AST(DEFAULT(-1))	// (master) Latest view to be completely updated, index
	EARRAY_OF(PatchServerUpdateTime) updates;			// (master/xfer) Checkins just updated, if any
} PatchServerUpdateInformationDatabase;

// The overall state of a child's updating.
typedef struct PatchServerUpdateInformation PatchServerUpdateInformation;
AUTO_STRUCT;
typedef struct PatchServerUpdateInformation
{
	char *server;										AST(KEY)			// (master) Server name
	char *category;															// (master) Server category
	PatchServerUpdateInformation *parent;				AST(UNOWNED)		// (master) Server's parent server, if any.
	float overall_overhead;								AST(DEFAULT(-1))	// (master/xfer) Overall overhead
	MirrorConfig** mirrorConfig;											// (master/xfer) Mirror configuration
	EARRAY_OF(PatchServerUpdateInformationDatabase) databases;				// (master/xfer) databases
	EARRAY_OF(PatchServerUpdateInformation) children;	AST(UNOWNED)		// (master) children, determined by parent
} PatchServerUpdateInformation;

// Estimated update information for a database.
AUTO_STRUCT;
typedef struct PatchServerUpdateEstimationDatabase
{
	STRING_POOLED database;			AST(POOL_STRING)	// (master/xfer) Name of the database
	int latest_rev;					AST(DEFAULT(-1))	// (master/xfer) Latest checkin to be completely updated
	EARRAY_OF(PatchServerUpdateTime) updates;			// (master/xfer) Checkins just updated, if any
} PatchServerUpdateEstimationDatabase;

// Status of all attached servers.
static EARRAY_OF(PatchServerUpdateInformation) servers;

// Estimations about completion of revisions.
static EARRAY_OF(PatchServerUpdateEstimationDatabase) estimated_databases;

// Merge update data in, for a particular database.
static void mergeUpdateDatabase(const char *server_name, PatchServerDb *local, PatchServerUpdateInformationDatabase *database,
								PatchServerUpdateInformationDatabase *update)
{
	// Merge per-database overall overhead.
	if (update->database_overhead >= 0)
	{
		if (database->database_overhead >= 0)
			database->database_overhead = (1-EXP_WEIGHT)*database->database_overhead + EXP_WEIGHT*update->database_overhead;
		else
			database->database_overhead = update->database_overhead;
	}

	// Set latest revision.
	database->latest_rev = MAX(database->latest_rev, update->latest_rev);

	// Set latest view.
	if (update->latest_view_name && *update->latest_view_name
		&& stricmp_safe(database->latest_view_name, update->latest_view_name))
	{
		int index = -1;
		EARRAY_FOREACH_REVERSE_BEGIN(local->db->namedviews, i);
		{
			if (!stricmp_safe(local->db->namedviews[i]->name, update->latest_view_name))
			{
				index = i;
				break;
			}
		}
		EARRAY_FOREACH_END;
		if (index == -1)
			log_printf(LOG_PATCHSERVER_INFO, "Server \"%s\" updated unknown view \"%s\"", server_name, update->latest_view_name);
		else
		{
			database->latest_view_name = update->latest_view_name;
			update->latest_view_name = NULL;
			database->latest_view_index = index;
		}
	}

	// Merge revision information.
	EARRAY_FOREACH_BEGIN(update->updates, i);
	{
		int j;
		if (update->updates[i]->duration < 0)
			continue;
		for (j = 0; j != eaSize(&database->updates); ++j)
		{
			if (database->updates[j]->rev == update->updates[i]->rev)
				break;
		}
		if (j == eaSize(&database->updates))
		{
			eaPush(&database->updates, update->updates[i]);
			eaRemove(&update->updates, i);
			--i;
		}
		else
			database->updates[j]->duration = update->updates[i]->duration;
	}
	EARRAY_FOREACH_END;
}

// Merge update data in.
static void mergeUpdate(PatchServerUpdateInformation *server, PatchServerUpdateInformation *update)
{
	int i;

	// Merge overall overhead.
	if (update->overall_overhead >= 0)
	{
		if (server->overall_overhead >= 0)
			server->overall_overhead = (1-EXP_WEIGHT)*server->overall_overhead + EXP_WEIGHT*update->overall_overhead;
		else
			server->overall_overhead = update->overall_overhead;
	}

	// Copy mirror configuration.
	if (eaSize(&update->mirrorConfig))
	{
		eaDestroyStruct(&server->mirrorConfig, parse_MirrorConfig);
		eaCopyStructs(&update->mirrorConfig, &server->mirrorConfig, parse_MirrorConfig);
	}

	// Merge per-database information.
	while (eaSize(&update->databases))
	{
		PatchServerDb *database;

		// Find this database locally.
		for (i = 0; i != eaSize(&g_patchserver_config.serverdbs); ++i)
		{
			if (!stricmp(g_patchserver_config.serverdbs[i]->name, update->databases[0]->database))
				break;
		}
		if (i == eaSize(&g_patchserver_config.serverdbs))
		{
			log_printf(LOG_PATCHSERVER_INFO, "Server \"%s\" updated unknown project \"%s\"",
				server->server, update->databases[0]->database);
			return;
		}

		// Find this database in our tracking information, and merge it.
		database = g_patchserver_config.serverdbs[i];
		for (i = 0; i != eaSize(&server->databases); ++i)
		{
			if (!stricmp(server->databases[i]->database, update->databases[0]->database))
				break;
		}
		if (i == eaSize(&server->databases))
			eaPush(&server->databases, update->databases[0]);
		else
			mergeUpdateDatabase(server->server, database, server->databases[i], update->databases[0]);
		eaRemove(&update->databases, 0);
	}
}

// Initialize estimations.
static void updateEstimationsInit()
{
	// Clear old data.
	eaClearStruct(&estimated_databases, parse_PatchServerUpdateEstimationDatabase);

	// Set up latest_rev for each database.
	EARRAY_CONST_FOREACH_BEGIN(servers, i, n);
	{
		// Check the maximum revision on this server.
		EARRAY_CONST_FOREACH_BEGIN(servers[i]->databases, j, m);
		{
			PatchServerUpdateInformationDatabase *database = servers[i]->databases[j];
			PatchServerUpdateEstimationDatabase *estimated = NULL;
			EARRAY_CONST_FOREACH_BEGIN(estimated_databases, k, o);
			{
				if (!stricmp_safe(estimated_databases[k]->database, database->database))
				{
					estimated = estimated_databases[k];
					break;
				}
			}
			EARRAY_FOREACH_END;
			if (!estimated)
			{
				estimated = StructCreate(parse_PatchServerUpdateEstimationDatabase);
				estimated->database = database->database;
				estimated->latest_rev = INT_MAX;
				eaPush(&estimated_databases, estimated);
			}
			estimated->latest_rev = MIN(estimated->latest_rev, database->latest_rev);
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;
}

// Trim old data.
static void trimEstimations()
{
	// Set up latest_rev for each database.
	EARRAY_CONST_FOREACH_BEGIN(servers, i, n);
	{
		// Check the maximum revision on this server.
		EARRAY_CONST_FOREACH_BEGIN(servers[i]->databases, j, m);
		{
			PatchServerUpdateInformationDatabase *database = servers[i]->databases[j];
			PatchServerUpdateEstimationDatabase *estimated = NULL;
			int latest_rev;

			// Get latest_rev.
			EARRAY_CONST_FOREACH_BEGIN(estimated_databases, k, o);
			{
				if (!stricmp_safe(estimated_databases[k]->database, database->database))
				{
					estimated = estimated_databases[k];
					break;
				}
			}
			EARRAY_FOREACH_END;
			latest_rev = estimated ? estimated->latest_rev : 0;

			// Remove old updates.
			while (estimated && eaSize(&database->updates) && database->updates[0]->rev <= latest_rev)
				eaRemove(&database->updates, 0);
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;
}

// Update estimations.
static void updateEstimations()
{
	int global_latest_rev = INT_MAX;

	PERFINFO_AUTO_START_FUNC();

	// Set up the estimations.
	updateEstimationsInit();

	// TODO alaframboise: Simulate sync to update estimations.

	// Trim old data.
	trimEstimations();

	PERFINFO_AUTO_STOP_FUNC();
}

// Initialize patch tracking.
void patchTrackingInit()
{
	// If disabled, do nothing.
	if (s_disable_patch_tracking || g_patchserver_config.parent.server)
		return;

	resRegisterDictionaryForEArray("Tracked Servers", RESCATEGORY_OTHER, 0, &servers, parse_PatchServerUpdateInformation);
}

// Add a server to be tracked.
void patchTrackingAdd(const char *name, const char *category, const char *parent)
{
	PatchServerUpdateInformation *server;

	PERFINFO_AUTO_START_FUNC();

	// If disabled, do nothing.
	if (s_disable_patch_tracking || g_patchserver_config.parent.server)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Find the parent, and add this new server to its child list.
	server = StructCreate(parse_PatchServerUpdateInformation);
	if (parent && *parent && stricmp(parent, g_patchserver_config.displayName))
	{
		EARRAY_CONST_FOREACH_BEGIN(servers, i, n);
		{
			if (!stricmp_safe(servers[i]->server, parent))
			{
				server->parent = servers[i];
				break;
			}
		}
		EARRAY_FOREACH_END;
		if (!server->parent)
		{
			AssertOrAlert("TRACKING_UNKNOWN_PARENT", "Server \"%s\" claims to have parent \"%s\" that doesn't seem to be connected.",
				name, parent);
			StructDestroy(parse_PatchServerUpdateInformation, server);
			return;
		}
		eaPush(&server->parent->children, server);
	}

	// Save information about this server.
	server->server = strdup(name);
	server->category = strdup(category);
	eaPush(&servers, server);

	PERFINFO_AUTO_STOP_FUNC();
}

// Remove a server from tracking.
void patchTrackingRemove(const char *name)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	// If disabled, do nothing.
	if (s_disable_patch_tracking || g_patchserver_config.parent.server)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	for (i = 0; i != eaSize(&servers); ++i)
	{
		if (!stricmp(servers[i]->server, name))
		{
			PatchServerUpdateInformation *server = servers[i];
			int index_on_parent = -1;
			devassertmsgf(eaSize(&server->children) == 0, "server %s category %s parent %s", server->server, server->category, server->parent ? server->parent->server : "");
			eaDestroy(&server->children);
			if (server->parent)
			{
				EARRAY_CONST_FOREACH_BEGIN(server->parent->children, j, n);
				{
					if (server->parent->children[j] == server)
					{
						index_on_parent = j;
						break;
					}
				}
				EARRAY_FOREACH_END;
				devassertmsgf(index_on_parent != -1, "server %s category %s parent \"%s\" index_on_parent %d", server->server, server->category,
					server->parent ? server->parent->server : "", index_on_parent);
				eaRemove(&server->parent->children, index_on_parent);
			}
			StructDestroy(parse_PatchServerUpdateInformation, servers[i]);
			eaRemove(&servers, i);
			return;
		}
	}

	// This should never happen.
	devassertmsgf(0, "patchtracking removed unknown server %s", name);

	PERFINFO_AUTO_STOP_FUNC();
}

// Handle status updates from children.
void patchTrackingUpdate(PatchClientLink *client, const char *name, const char *status)
{
	PatchServerUpdateInformation update = {0};
	bool success;
	int i;

	PERFINFO_AUTO_START_FUNC();

	// If disabled, do nothing.
	if (s_disable_patch_tracking || g_patchserver_config.parent.server)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Parse update information.
	success = ParserReadText(status, parse_PatchServerUpdateInformation, &update, PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE);
	if (!success)
	{
		log_printf(LOG_PATCHSERVER_GENERAL, "Could not parse PatchServerUpdateInformation: name='%s', status='%s'", name, status);
		AssertOrAlert("BAD_TRACKING_UPDATE", "Invalid tracking update data was received from %s", name);
		return;
	}

	// Merge in updated data.
	success = false;
	for (i = 0; i != eaSize(&servers); ++i)
	{
		if (!stricmp(servers[i]->server, name))
		{
			mergeUpdate(servers[i], &update);
			success = true;
			break;
		}
	}
	if (!success)
	{
		AssertOrAlert("UPDATE_UNKNOWN", "Received an update from unknown server \"%s\"", name);
		return;
	}

	// Update estimations.
	updateEstimations();

	// Clean up.
	StructDeInit(parse_PatchServerUpdateInformation, &update);
	PERFINFO_AUTO_STOP_FUNC();
}

// Scan our databases for any updates to be sent to the master.
void patchTrackingScanForUpdates(bool do_scan, unsigned duration)
{
	PatchServerUpdateInformation update = {0};
	char *status = NULL;
	unsigned total_duration = 0;

	PERFINFO_AUTO_START_FUNC();

	// If disabled, do nothing.
	if (s_disable_patch_tracking || !g_patchserver_config.serverCategory)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Copy mirror configuration.
	eaCopyStructs(&g_patchserver_config.mirrorConfig, &update.mirrorConfig, parse_MirrorConfig);

	if (do_scan)
	{
		// Scan for updates on each project.
		EARRAY_CONST_FOREACH_BEGIN(g_patchserver_config.serverdbs, i, n);
		{
			PatchServerDb *serverdb = g_patchserver_config.serverdbs[i];
			PatchServerUpdateInformationDatabase *database = NULL;

			// Check for updates in this database.
			if (serverdb->db && serverdb->updated)
			{
				unsigned db_duration = 0;
				total_duration += serverdb->update_duration; 
				EARRAY_CONST_FOREACH_BEGIN(serverdb->db->checkins, j, m);
				{
					Checkin *checkin = serverdb->db->checkins[j];
					if (checkin->updated)
					{
						PatchServerUpdateTime *checkin_update;
						if (!database)
						{
							NamedView *view;
							database = StructCreate(parse_PatchServerUpdateInformationDatabase);
							database->database = serverdb->name;
							database->latest_rev = serverdb->latest_rev;
							view = eaSize(&serverdb->db->namedviews) ? eaTail(&serverdb->db->namedviews) : NULL;
							if (view)
								database->latest_view_name = strdup(view->name);
						}
						checkin_update = StructCreate(parse_PatchServerUpdateTime);
						devassertmsgf(checkin->update_duration + db_duration <= serverdb->update_duration,
							"serverdb %s rev %d checkin_update_duration %u db_duration %u serverdb_update_duration %u",
							serverdb->name, checkin->rev, checkin->update_duration, db_duration, serverdb->update_duration);
						checkin_update->rev = checkin->rev;
						checkin_update->duration = checkin->update_duration;
						eaPush(&database->updates, checkin_update);
						db_duration += checkin->update_duration;
						checkin->updated = false;
					}
				}
				EARRAY_FOREACH_END;

				// Add serverdb overhead.
				if (database)
				{
					devassertmsgf(serverdb->update_duration >= db_duration, "serverdb %s update_duration %u db_duration %u",
						serverdb->name, serverdb->update_duration, db_duration);
					if (serverdb->update_duration >= db_duration)
						database->database_overhead = serverdb->update_duration - db_duration;
					serverdb->updated = false;
				}

				// Trim update times list to MAX_UPDATE_NUMBER.
				if (database && eaSize(&database->updates) > MAX_UPDATE_NUMBER)
				{
					int count = eaSize(&database->updates) - MAX_UPDATE_NUMBER;
					int k;
					for (k = 0; k < count; ++k)
						StructDestroy(parse_PatchServerUpdateTime, database->updates[k]);
					eaRemoveRange(&database->updates, 0, count);
				}
			}

			// Add updates to struct, if any.
			if (database)
				eaPush(&update.databases, database);
		}
		EARRAY_FOREACH_END;

		// Add overall overhead.
		if (duration)
		{
			devassert(duration >= total_duration);
			update.overall_overhead = duration - total_duration;
		}
	}

	// Send general information about each database.
	EARRAY_CONST_FOREACH_BEGIN(g_patchserver_config.serverdbs, i, n);
	{
		PatchServerDb *serverdb = g_patchserver_config.serverdbs[i];
		bool already_in_update = false;
		PatchServerUpdateInformationDatabase *database;
		NamedView *view = NULL;

		// Check if there is already information about this database.
		EARRAY_CONST_FOREACH_BEGIN(update.databases, j, m);
		{
			if (!stricmp_safe(update.databases[j]->database, serverdb->name))
			{
				already_in_update = true;
				break;
			}
		}
		EARRAY_FOREACH_END;

		// If there is no information about this database in the update, add it.
		if (!already_in_update)
		{
			database = StructCreate(parse_PatchServerUpdateInformationDatabase);
			database->database = serverdb->name;
			database->latest_rev = serverdb->latest_rev;
			if (serverdb->db)
				view = eaSize(&serverdb->db->namedviews) ? eaTail(&serverdb->db->namedviews) : NULL;
			if (view)
				database->latest_view_name = strdup(view->name);
			eaPush(&update.databases, database);
		}
	}
	EARRAY_FOREACH_END;

	// Send update.
	estrStackCreate(&status);
	ParserWriteText(&status, parse_PatchServerUpdateInformation, &update, 0, 0, 0);
	patchupdateSendUpdateStatus(status);
	estrDestroy(&status);

	// Clean up.
	StructDeInit(parse_PatchServerUpdateInformation, &update);
	PERFINFO_AUTO_STOP_FUNC();
}

// Get the maximum required revision.
static void UpdatedCallback(DirEntry *dir, void *data)
{
	int *maximum_revision = data;
	FileVersion *version;

	if (eaSize(&dir->versions))
	{
		version = eaTail(&dir->versions);
		devassert(version);
		*maximum_revision = MAX(*maximum_revision, version->rev);
	}
}

// Return true if a path is completely updated by all child servers.
bool patchTrackingIsCompletelyUpdatedPath(PatchProject *project, const char *path,
										  const char **include_categories, const char **exclude_categories)
{
	int maximum_revision = -1;

	PERFINFO_AUTO_START_FUNC();

	// If disabled, do nothing.
	if (s_disable_patch_tracking || g_patchserver_config.parent.server)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	// Check project.
	if (!project || !project->serverdb || !project->serverdb->db)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	// Get maximum required revision in this path.
	patchForEachDirEntryPrefix(project->serverdb->db, path, UpdatedCallback, &maximum_revision);

	// If path doesn't seem to exist at all, return false.
	if (maximum_revision == -1)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	// Check if any child server does not have this revision.
	EARRAY_CONST_FOREACH_BEGIN(servers, i, n);
	{
		// Skip this server if it is not included or excluded.
		if (eaSize(&include_categories) && eaFindString(&include_categories, servers[i]->category) == -1)
			continue;
		if (eaSize(&exclude_categories) && eaFindString(&exclude_categories, servers[i]->category) != -1)
			continue;

		// Check the maximum revision on this server.
		EARRAY_CONST_FOREACH_BEGIN(servers[i]->databases, j, m);
		{
			PatchServerUpdateInformationDatabase *database = servers[i]->databases[j];
			if (!stricmp_safe(database->database, project->serverdb->name))
			{
				if (database->latest_rev < maximum_revision)
					return false;
			}
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	// Everything is OK, return true.
	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

#include "patchtracking_c_ast.c"
