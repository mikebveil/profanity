/*
 * blocking.c
 *
 * Copyright (C) 2012 - 2016 James Booth <boothj5@gmail.com>
 *
 * This file is part of Profanity.
 *
 * Profanity is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Profanity is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Profanity.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link the code of portions of this program with the OpenSSL library under
 * certain conditions as described in each individual source file, and
 * distribute linked combinations including the two.
 *
 * You must obey the GNU General Public License in all respects for all of the
 * code used other than OpenSSL. If you modify file(s) with this exception, you
 * may extend this exception to your version of the file(s), but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version. If you delete this exception statement from all
 * source files in the program, then also delete it here.
 *
 */

#include <stdlib.h>

#ifdef HAVE_LIBMESODE
#include <mesode.h>
#endif
#ifdef HAVE_LIBSTROPHE
#include <strophe.h>
#endif

#include <glib.h>

#include "log.h"
#include "common.h"
#include "ui/ui.h"
#include "xmpp/session.h"
#include "xmpp/stanza.h"
#include "xmpp/iq.h"

static int _blocklist_result_handler(xmpp_stanza_t *const stanza, void *const userdata);
static int _block_add_result_handler(xmpp_stanza_t *const stanza, void *const userdata);
static int _block_remove_result_handler(xmpp_stanza_t *const stanza, void *const userdata);

static GList *blocked;
static Autocomplete blocked_ac;

void
blocking_request(void)
{
    char *id = create_unique_id("blocked_list_request");
    xmpp_ctx_t *ctx = connection_get_ctx();
    xmpp_stanza_t *iq;

    if (blocked) {
        g_list_free_full(blocked, free);
        blocked = NULL;
    }

    if (blocked_ac) {
        autocomplete_free(blocked_ac);
    }
    blocked_ac = autocomplete_new();

    iq_id_handler_add(id, _blocklist_result_handler, NULL);

    iq = stanza_create_blocked_list_request(ctx);
    xmpp_stanza_set_id(iq, id);
    iq_send_stanza(iq);
    xmpp_stanza_release(iq);
    free(id);
}

GList*
blocked_list(void)
{
    return blocked;
}

char*
blocked_ac_find(const char *const search_str)
{
    return autocomplete_complete(blocked_ac, search_str, TRUE);
}

void
blocked_ac_reset(void)
{
    if (blocked_ac) {
        autocomplete_reset(blocked_ac);
    }
}

gboolean
blocked_add(char *jid)
{
    GList *found = g_list_find_custom(blocked, jid, (GCompareFunc)g_strcmp0);
    if (found) {
        return FALSE;
    }

    xmpp_ctx_t *ctx = connection_get_ctx();

    xmpp_stanza_t *iq = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(iq, STANZA_NAME_IQ);
    xmpp_stanza_set_type(iq, STANZA_TYPE_SET);
    char *id = create_unique_id("block");
    xmpp_stanza_set_id(iq, id);

    xmpp_stanza_t *block = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(block, STANZA_NAME_BLOCK);
    xmpp_stanza_set_ns(block, STANZA_NS_BLOCKING);

    xmpp_stanza_t *item = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(item, STANZA_NAME_ITEM);
    xmpp_stanza_set_attribute(item, STANZA_ATTR_JID, jid);

    xmpp_stanza_add_child(block, item);
    xmpp_stanza_release(item);

    xmpp_stanza_add_child(iq, block);
    xmpp_stanza_release(block);

    iq_id_handler_add(id, _block_add_result_handler, strdup(jid));

    iq_send_stanza(iq);
    xmpp_stanza_release(iq);
    free(id);

    return TRUE;
}

gboolean
blocked_remove(char *jid)
{
    GList *found = g_list_find_custom(blocked, jid, (GCompareFunc)g_strcmp0);
    if (!found) {
        return FALSE;
    }

    xmpp_ctx_t *ctx = connection_get_ctx();

    xmpp_stanza_t *iq = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(iq, STANZA_NAME_IQ);
    xmpp_stanza_set_type(iq, STANZA_TYPE_SET);
    char *id = create_unique_id("unblock");
    xmpp_stanza_set_id(iq, id);

    xmpp_stanza_t *block = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(block, STANZA_NAME_UNBLOCK);
    xmpp_stanza_set_ns(block, STANZA_NS_BLOCKING);

    xmpp_stanza_t *item = xmpp_stanza_new(ctx);
    xmpp_stanza_set_name(item, STANZA_NAME_ITEM);
    xmpp_stanza_set_attribute(item, STANZA_ATTR_JID, jid);

    xmpp_stanza_add_child(block, item);
    xmpp_stanza_release(item);

    xmpp_stanza_add_child(iq, block);
    xmpp_stanza_release(block);

    iq_id_handler_add(id, _block_remove_result_handler, strdup(jid));

    iq_send_stanza(iq);
    xmpp_stanza_release(iq);
    free(id);

    return TRUE;
}

int
blocked_set_handler(xmpp_stanza_t *stanza)
{
    xmpp_stanza_t *block = xmpp_stanza_get_child_by_name(stanza, STANZA_NAME_BLOCK);
    if (block) {
        xmpp_stanza_t *child = xmpp_stanza_get_children(block);
        while (child) {
            if (g_strcmp0(xmpp_stanza_get_name(child), STANZA_NAME_ITEM) == 0) {
                const char *jid = xmpp_stanza_get_attribute(child, STANZA_ATTR_JID);
                if (jid) {
                    blocked = g_list_append(blocked, strdup(jid));
                    autocomplete_add(blocked_ac, jid);
                }

            }

            child = xmpp_stanza_get_next(child);
        }
    }

    xmpp_stanza_t *unblock = xmpp_stanza_get_child_by_name(stanza, STANZA_NAME_UNBLOCK);
    if (unblock) {
        xmpp_stanza_t *child = xmpp_stanza_get_children(unblock);
        if (!child) {
            g_list_free_full(blocked, free);
            blocked = NULL;
            autocomplete_clear(blocked_ac);
        } else {
            while (child) {
                if (g_strcmp0(xmpp_stanza_get_name(child), STANZA_NAME_ITEM) == 0) {
                    const char *jid = xmpp_stanza_get_attribute(child, STANZA_ATTR_JID);
                    if (jid) {
                        GList *found = g_list_find_custom(blocked, jid, (GCompareFunc)g_strcmp0);
                        if (found) {
                            blocked = g_list_remove_link(blocked, found);
                            g_list_free_full(found, free);
                            autocomplete_remove(blocked_ac, jid);
                        }
                    }

                }

                child = xmpp_stanza_get_next(child);
            }
        }
    }

    return 1;
}

static int
_block_add_result_handler(xmpp_stanza_t *const stanza, void *const userdata)
{
    char *jid = (char*)userdata;

    const char *type = xmpp_stanza_get_attribute(stanza, STANZA_ATTR_TYPE);
    if (type == NULL) {
        log_info("Block response received for %s with no type attribute.", jid);
        free(jid);
        return 0;
    }

    if (g_strcmp0(type, "result") != 0) {
        log_info("Block response received for %s with unrecognised type attribute.", jid);
        free(jid);
        return 0;
    }

    cons_show("User %s successfully blocked.", jid);
    free(jid);

    return 0;
}

static int
_block_remove_result_handler(xmpp_stanza_t *const stanza, void *const userdata)
{
    char *jid = (char*)userdata;

    const char *type = xmpp_stanza_get_attribute(stanza, STANZA_ATTR_TYPE);
    if (type == NULL) {
        log_info("Unblock response received for %s with no type attribute.", jid);
        free(jid);
        return 0;
    }

    if (g_strcmp0(type, "result") != 0) {
        log_info("Unblock response received for %s with unrecognised type attribute.", jid);
        free(jid);
        return 0;
    }

    cons_show("User %s successfully unblocked.", jid);
    free(jid);

    return 0;
}

static int
_blocklist_result_handler(xmpp_stanza_t *const stanza, void *const userdata)
{
    log_info("Blocked list result handler fired.");

    const char *type = xmpp_stanza_get_type(stanza);
    if (g_strcmp0(type, "result") != 0) {
        log_info("Received blocklist without result type");
        return 0;
    }

    xmpp_stanza_t *blocklist = xmpp_stanza_get_child_by_name(stanza, STANZA_NAME_BLOCKLIST);
    if (!blocklist) {
        log_info("Received blocklist without blocklist element");
        return 0;
    }

    if (blocked) {
        g_list_free_full(blocked, free);
        blocked = NULL;
    }

    xmpp_stanza_t *items = xmpp_stanza_get_children(blocklist);
    if (!items) {
        log_info("No blocked users.");
        return 0;
    }

    xmpp_stanza_t *curr = items;
    while (curr) {
        const char *name = xmpp_stanza_get_name(curr);
        if (g_strcmp0(name, "item") == 0) {
            const char *jid = xmpp_stanza_get_attribute(curr, STANZA_ATTR_JID);
            if (jid) {
                blocked = g_list_append(blocked, strdup(jid));
                autocomplete_add(blocked_ac, jid);
            }
        }
        curr = xmpp_stanza_get_next(curr);
    }

    return 0;
}