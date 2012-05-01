/* 
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2005-2012, Anthony Minessale II <anthm@freeswitch.org>
 *
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 *
 * The Initial Developer of the Original Code is
 * Anthony Minessale II <anthm@freeswitch.org>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * 
 * Joao Mesquita <jmesquita@gmail.com>
 *
 *
 * mod_pickupgroup.c -- Pickup Groups
 *
 */
#include <switch.h>

/* Prototypes */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_pickupgroup_shutdown);
SWITCH_MODULE_RUNTIME_FUNCTION(mod_pickupgroup_runtime);
SWITCH_MODULE_LOAD_FUNCTION(mod_pickupgroup_load);

/* SWITCH_MODULE_DEFINITION(name, load, shutdown, runtime) 
 * Defines a switch_loadable_module_function_table_t and a static const char[] modname
 */
SWITCH_MODULE_DEFINITION(mod_pickupgroup, mod_pickupgroup_load, mod_pickupgroup_shutdown, NULL);

#define MAX_PICKUP 3000
struct e_data {
	char *uuid_list[MAX_PICKUP];
	int total;
};

static int e_callback(void *pArg, int argc, char **argv, char **columnNames)
{
	char *uuid = argv[0];
	struct e_data *e_data = (struct e_data *) pArg;

	if (uuid && e_data) {
		e_data->uuid_list[e_data->total] = strdup(uuid);
		return 0;
	}

	return 1;
}

// static switch_status_t ingroup(char* mygroups, char* changroups)
// {
//     char *mygroups_argv[] = { 0 };
//     int mygroups_count = switch_split(mygroups, ',', mygroups_argv);
//     int x, y = 0;
//     
//     switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "The data is: (%s) and its count is %d\n", mygroups, mygroups_count);
//     
//     // Check to see if the channel is within some of the uuids.
//     for (x = 0; x < mygroups_count; x)
//     {
//         char *changroups_argv[] = { 0 };
//         int changroups_count = switch_split(changroups, ',', changroups_argv);
//         
//         for (y = 0; y < changroups_count; y)
//         {
//             switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Comparing: %s = %s\n", changroups_argv[y], mygroups_argv[x]);
//             if (!strcasecmp(changroups_argv[y], mygroups_argv[x])) {
//                 return SWITCH_STATUS_SUCCESS;
//             }
//         }
//     }
//     return SWITCH_STATUS_FALSE;
// }

SWITCH_STANDARD_APP(pickup_function)
{
    switch_cache_db_handle_t *db = NULL;
	char *errmsg = NULL;
	struct e_data e_data = { {0} };
	char *sql = switch_mprintf("select uuid from channels where uuid != '%q'", switch_core_session_get_uuid(session));
	const char *file = NULL;
	int x = 0;
	switch_status_t status;
	switch_channel_t *channel = switch_core_session_get_channel(session);
    char * mydata;
	
	mydata = switch_core_session_strdup(session, data);
		
    for (x = 0; x < MAX_PICKUP; x) {
        switch_safe_free(e_data.uuid_list[x]);
    }
    e_data.total = 0;
    if (switch_core_db_handle(&db) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Database Error!\n");
        goto done;
    }
    switch_cache_db_execute_sql_callback(db, sql, e_callback, &e_data, &errmsg);
    switch_cache_db_release_db_handle(&db);
    if (errmsg) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "Error: %s\n", errmsg);
        free(errmsg);
        if ((file = switch_channel_get_variable(channel, "pickupgroup_indicate_failed"))) {
            switch_ivr_play_file(session, NULL, file, NULL);
        }
            // Leave the loop since we have a DB error.
        goto done;
    }
    if (e_data.total) {
        for (x = 0; x < e_data.total && switch_channel_ready(channel); x) {
            switch_channel_t *other_channel;
            const char *pickupgroup = NULL;
            char *pickupgroup_dup;
            switch_core_session_t *other_session = NULL;;

            if ((other_session = switch_core_session_locate(e_data.uuid_list[x]))) {
                other_channel = switch_core_session_get_channel(other_session);
                pickupgroup = switch_channel_get_variable(other_channel, "pickupgroups");
                pickupgroup_dup = switch_core_session_strdup(session, pickupgroup);
                switch_core_session_rwunlock(other_session);
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "Pickup: %s Trying: %s\n", e_data.uuid_list[x], pickupgroup_dup);

                if (!strcasecmp(mydata, pickupgroup_dup)) {
                // if (ingroup(mydata, pickupgroup_dup) == SWITCH_STATUS_SUCCESS) {
                    if ((status = switch_ivr_intercept_session(session, e_data.uuid_list[x], SWITCH_TRUE)) != SWITCH_STATUS_SUCCESS) {
                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "Pickup: %s Failed, moving to next pickup.\n", e_data.uuid_list[x]);
                        continue;
                    }
                }
                goto done;
            } else {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING, "Pickup: %s Failed, moving to next pickup. (No session)\n", e_data.uuid_list[x]);
            }
        }
    }

 done:
    for (x = 0; x < MAX_PICKUP; x) {
		switch_safe_free(e_data.uuid_list[x]);
	}

	free(sql);
}

/* Macro expands to: switch_status_t mod_pickupgroup_load(switch_loadable_module_interface_t **module_interface, switch_memory_pool_t *pool) */
SWITCH_MODULE_LOAD_FUNCTION(mod_pickupgroup_load)
{
	switch_application_interface_t *app_interface;

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);
	
    SWITCH_ADD_APP(app_interface, "pickupgroup", "pickupgroup", "pickupgroup", pickup_function, "pickupgroup", SAF_NONE);
	return SWITCH_STATUS_SUCCESS;
}

/*
   Called when the system shuts down
   Macro expands to: switch_status_t mod_pickupgroup_shutdown() */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_pickupgroup_shutdown)
{
	/* Cleanup dynamically allocated config settings */
	return SWITCH_STATUS_SUCCESS;
}

/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4
 */
 
