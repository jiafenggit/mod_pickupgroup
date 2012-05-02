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


static switch_status_t intercept_session(switch_core_session_t *session, const char *uuid, switch_bool_t bleg)
{
        switch_core_session_t *rsession, *bsession = NULL;
        switch_channel_t *channel, *rchannel, *bchannel = NULL;
        const char *buuid, *var;
        char brto[SWITCH_UUID_FORMATTED_LENGTH + 1] = "";

        if (bleg) {
                if (switch_ivr_find_bridged_uuid(uuid, brto, sizeof(brto)) == SWITCH_STATUS_SUCCESS) {
                        uuid = switch_core_session_strdup(session, brto);
                } else {
                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "no uuid bridged to %s\n", uuid);
                        return SWITCH_STATUS_FALSE;
                }
        }

        if (zstr(uuid) || !(rsession = switch_core_session_locate(uuid))) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "no uuid %s\n", uuid);
                return SWITCH_STATUS_FALSE;
        }

        channel = switch_core_session_get_channel(session);
        rchannel = switch_core_session_get_channel(rsession);
        buuid = switch_channel_get_variable(rchannel, SWITCH_SIGNAL_BOND_VARIABLE);

        if ((var = switch_channel_get_variable(channel, "intercept_unbridged_only")) && switch_true(var)) {
                if ((switch_channel_test_flag(rchannel, CF_BRIDGED))) {
                        switch_core_session_rwunlock(rsession);
                        return SWITCH_STATUS_FALSE;
                }
        }

        if ((var = switch_channel_get_variable(channel, "intercept_unanswered_only")) && switch_true(var)) {
                if ((switch_channel_test_flag(rchannel, CF_ANSWERED))) {
                        switch_core_session_rwunlock(rsession);
                        return SWITCH_STATUS_FALSE;
                }
        }

        switch_channel_answer(channel);

        if (!zstr(buuid)) {
                if ((bsession = switch_core_session_locate(buuid))) {
                        bchannel = switch_core_session_get_channel(bsession);
                        switch_channel_set_flag(bchannel, CF_INTERCEPT);
                }
        }

        if (!switch_channel_test_flag(rchannel, CF_ANSWERED)) {
                switch_channel_answer(rchannel);
        }

        switch_channel_mark_hold(rchannel, SWITCH_FALSE);

        switch_channel_set_state_flag(rchannel, CF_TRANSFER);
        switch_channel_set_state(rchannel, CS_PARK);

        if (bchannel) {
                switch_channel_set_state_flag(bchannel, CF_TRANSFER);
                switch_channel_set_state(bchannel, CS_PARK);
        }

        switch_channel_set_flag(rchannel, CF_INTERCEPTED);
        switch_ivr_uuid_bridge(switch_core_session_get_uuid(session), uuid);
        switch_core_session_rwunlock(rsession);

        if (bsession) {
                switch_channel_hangup(bchannel, SWITCH_CAUSE_PICKED_OFF);
                switch_core_session_rwunlock(bsession);
        }
        return SWITCH_STATUS_SUCCESS;
}

static switch_status_t ingroup(char* mygroups, char* changroups)
{
    char *mygroups_argv[50] = { 0 }, *changroups_argv[50] = { 0 };
    int mygroups_count = 1, changroups_count = 1;
    int x, y = 0;

    if (strchr(mygroups, ','))  {
        mygroups_count = switch_split(mygroups, ',', mygroups_argv);
    } else {
        mygroups_argv[0] = mygroups;
    }

    if (strchr(changroups, ','))  {
        changroups_count = switch_split(changroups, ',', changroups_argv);
    } else {
        changroups_argv[0] = changroups;
    }

    //Check to see if the channel is within some of the uuids.
    for (x = 0; x < mygroups_count; x++)
    {
        for (y = 0; y < changroups_count; y++)
        {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Comparing: %s = %s\n", mygroups_argv[x], changroups_argv[y]);
            if (!strcasecmp(changroups_argv[y], mygroups_argv[x])) {
                return SWITCH_STATUS_SUCCESS;
            }
        }
    }
    return SWITCH_STATUS_FALSE;
}

/* SQL callback function. */
static int e_callback(void *pArg, int argc, char **argv, char **columnNames)
{
	char *uuid = argv[0];
	struct e_data *e_data = (struct e_data *) pArg;

	if (uuid && e_data) {
		e_data->uuid_list[e_data->total++] = strdup(uuid);
		return 0;
	}

	return 1;
}

SWITCH_STANDARD_APP(pickup_function)
{
    switch_cache_db_handle_t *db = NULL;
    char *errmsg = NULL;
    struct e_data e_data = { {0} };
    char *sql = switch_mprintf("select uuid from channels where uuid != '%q'", switch_core_session_get_uuid(session));
    const char *file = NULL;
    int x, argc = 0;
    char *argv[2] = { 0 };
    char * mydata = NULL;
    char *groups = NULL;
    switch_status_t status;
    switch_bool_t bleg = SWITCH_FALSE;
    switch_channel_t *channel = switch_core_session_get_channel(session);

    //Check to see if we are not getting garbage on input and dup input
    if (!zstr(data) && (mydata = switch_core_session_strdup(session, data))) {
        if ((argc = switch_separate_string(mydata, ' ', argv, (sizeof(argv) / sizeof(argv[0])))) >= 1) {
            if (!strcasecmp(argv[0], "-bleg")) {
                if (argv[1]) {
                    groups = argv[1];
                    bleg = SWITCH_TRUE;
                } else {
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "You are not using it right.\n");
                    return;
                }
            } else {
                groups = argv[0];
            }
        } else {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "You are not using it right.\n");
            return;
        }
    } else {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "You are not using it right.\n");
        return;
    }


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
        for (x = 0; x < e_data.total && switch_channel_ready(channel); x++) {
            switch_channel_t *other_channel;
            const char *pickupgroup = NULL;
            char *pickupgroup_dup;
            switch_core_session_t *other_session = NULL;;

            if ((other_session = switch_core_session_locate(e_data.uuid_list[x]))) {
                other_channel = switch_core_session_get_channel(other_session);
                pickupgroup = switch_channel_get_variable(other_channel, "pickupgroups");

                //Before doing anything, check if channel has pickugroups set.
                if(zstr(pickupgroup)) {
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "Pickup: %s has no group set.\n", e_data.uuid_list[x]);
                    continue;
                }

                pickupgroup_dup = switch_core_session_strdup(session, pickupgroup);
                switch_core_session_rwunlock(other_session);
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "Pickup: %s Trying: %s\n", e_data.uuid_list[x], pickupgroup_dup);

                if (ingroup(groups, pickupgroup_dup) == SWITCH_STATUS_SUCCESS) {
                    if ((status = intercept_session(session, e_data.uuid_list[x], bleg)) != SWITCH_STATUS_SUCCESS) {
                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "Pickup: %s Failed, moving to next pickup.\n", e_data.uuid_list[x]);
                        continue;
                    }
                }
                goto done;
            } else {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING, "Pickup: %s Failed, moving to next pickup. (No session)\n", e_data.uuid_list[x]);
            }
        }
    } else {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING, "Pickup failed because resultset it %d.\n", e_data.total);
    }

 done:
    for (x = 0; x < MAX_PICKUP; x++) {
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

