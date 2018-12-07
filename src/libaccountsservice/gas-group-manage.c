#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>

#include "gas-group-manager.h"
#include "act-user-private.h"
#include "group-generated.h"
#include "group-list-generated.h"

#define GROUPADMIN_NAME      "org.user.admin"
#define GROUPADMIN_PATH      "/org/user/admin"
#define GROUPADMIN_INTERFACE "org.user.admin"

typedef enum 
{
    GAS_GROUP_MANAGER_GET_GROUP_STATE_UNFETCHED = 0,
    GAS_GROUP_MANAGER_GET_GROUP_STATE_WAIT_FOR_LOADED,
    GAS_GROUP_MANAGER_GET_GROUP_STATE_ASK_GROUP_SERVICE,
    GAS_GROUP_MANAGER_GET_GROUP_STATE_FETCHED
} GasGroupManagerGetGroupState;

typedef enum 
{
    GAS_GROUP_MANAGER_FETCH_GROUP_FROM_USERNAME_REQUEST,
    GAS_GROUP_MANAGER_FETCH_GROUP_FROM_ID_REQUEST,
} GasGroupManagerFetchGroupRequestType;

typedef struct
{
    GasGroupManager               *manager;
    GasGroupManagerGetGroupState   state;
    GasGroup                      *group;
    GasGroupManagerFetchGroupRequestType type;
    union 
    {
        char    *name;
        gid_t   gid;
    };
    char                          *object_path;
    char                          *description;
} GasGroupManagerFetchGroupRequest;

typedef struct
{
    GHashTable            *normal_group_by_name;
    GHashTable            *groups_by_object_path;
    GDBusConnection       *connection;
    UserGroupAdmin        *group_admin_proxy;

    GSList                *new_groups;
    GSList                *new_groups_inhibiting_load;
    GSList                *fetch_group_requests;

    GSList                *exclude_groupnames;
    GSList                *include_groupnames;

    guint                  load_id;

    gboolean               is_loaded;
} GasGroupManagerPrivate;

enum 
{
    PROP_0,
    PROP_INCLUDE_GROUPNAMES_LIST,
    PROP_EXCLUDE_GROUPNAMES_LIST,
    PROP_IS_LOADED,
};

enum 
{
    GROUP_ADDED,
    GROUP_REMOVED,
    GROUP_IS_LOGGED_IN_CHANGED,
    GROUP_CHANGED,
    LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gas_group_manager_class_init (GasGroupManagerClass *klass);
static void     gsa_group_manager_init       (GasGroupManager      *GroupManager);
static void     gas_group_manager_finalize   (GObject             *object);

static gboolean ensure_group_admin_proxy     (GasGroupManager *manager);
static gboolean load_seat_incrementally     (ActUserManager *manager);
static void     unload_seat                 (ActUserManager *manager);
static void     load_users                  (ActUserManager *manager);
static void     load_user                   (ActUserManager *manager,
                                             const char     *username);
static void     act_user_manager_queue_load (ActUserManager *manager);
static void     queue_load_seat             (ActUserManager *manager);

static void     set_is_loaded (ActUserManager *manager, gboolean is_loaded);

static void     on_new_group_loaded (ActUser        *user,
                                    GParamSpec     *pspec,
                                    ActUserManager *manager);
static void     give_up (ActUserManager                 *manager,
                         ActUserManagerFetchUserRequest *request);
static void     fetch_user_incrementally       (ActUserManagerFetchUserRequest *request);

static void     maybe_set_is_loaded            (ActUserManager *manager);
static void     update_user                    (ActUserManager *manager,
                                                ActUser        *user);
static gpointer group_manager_object = NULL;

G_DEFINE_TYPE_WITH_PRIVATE (GasGroupManager, gas_group_manager, G_TYPE_OBJECT)

gboolean
act_user_manager_can_switch (ActUserManager *manager)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);

        if (!priv->is_loaded) {
                g_debug ("ActUserManager: Unable to switch sessions until fully loaded");
                return FALSE;
        }

        if (priv->seat.id == NULL || priv->seat.id[0] == '\0') {
                g_debug ("ActUserManager: display seat ID is not set; can't switch sessions");
                return FALSE;
        }

        g_debug ("ActUserManager: checking if seat can activate sessions");


#ifdef WITH_SYSTEMD
        if (LOGIND_RUNNING()) {
                return _can_activate_systemd_sessions (manager);
        }
#endif

        return _can_activate_console_kit_sessions (manager);
}


static const char * DescribeGroup (GasGroup *group)
{
    GasGroupManagerFetchGroupRequest *request;

    if (gas_group_is_loaded (group)) 
    {
        static char *description = NULL;
        g_clear_pointer (&description, (GDestroyNotify) g_free);

        description = g_strdup_printf ("group %s", gas_group_get_group_name (group));
        return description;
    }

    request = g_object_get_data (G_OBJECT (group), "fetch-group-request");
    if (request != NULL) 
    {
                return request->description;
    }

    return "group";
}

static void on_group_changed (GasGroup *group,GasGroupManager *manager)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);

    if (priv->is_loaded) 
    {
        g_print ("GasGroupManager: sending group-changed signal for %s",
                 DescribeGroup (group));

        g_signal_emit (manager, signals[GROUP_CHANGED], 0, group);

        g_print ("GasGroupManager: sent group-changed signal for %s",
                 DescribeGroup (group));

        update_group (manager,group);
    }
}

static void
queue_load_seat_incrementally (ActUserManager *manager)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);

        if (priv->seat.load_idle_id == 0) {
            priv->seat.load_idle_id = g_idle_add ((GSourceFunc) load_seat_incrementally, manager);
        }
}

static void
on_get_seat_id_finished (GObject        *object,
                         GAsyncResult   *result,
                         gpointer        data)
{
        ConsoleKitSession *proxy = CONSOLE_KIT_SESSION (object);
        g_autoptr(ActUserManager) manager = data;
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        g_autoptr(GError)  error = NULL;
        char              *seat_id;

        if (!console_kit_session_call_get_seat_id_finish (proxy, &seat_id, result, &error)) {
                if (error != NULL) {
                        g_debug ("Failed to identify the seat of the "
                                 "current session: %s",
                                 error->message);
                } else {
                        g_debug ("Failed to identify the seat of the "
                                 "current session");
                }

                g_debug ("ActUserManager: GetSeatId call failed, so unloading seat");
                unload_seat (manager);

                return;
        }

        g_debug ("ActUserManager: Found current seat: %s", seat_id);

        priv->seat.id = seat_id;
        priv->seat.state++;
}

#ifdef WITH_SYSTEMD
static void
_get_systemd_seat_id (ActUserManager *manager)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        int   res;
        g_autofree gchar *seat_id = NULL;

        res = sd_session_get_seat (NULL, &seat_id);
        if (res == -ENOENT) {
                seat_id = NULL;
        } else if (res < 0) {
                g_warning ("Could not get current seat: %s",
                           strerror (-res));
                unload_seat (manager);
                return;
        }

        priv->seat.id = g_strdup (seat_id);
        priv->seat.state++;
}
#endif


static gint match_name_cmpfunc (gconstpointer a,
                               gconstpointer b)
{
    return g_strcmp0 ((char *) a,(char *) b);
}

static gboolean groupname_in_exclude_list (GasGroupManager *manager,
                                           const char      *name)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GSList   *found;
    gboolean  ret = FALSE;

    if (priv->exclude_groupnames != NULL) 
    {
        found = g_slist_find_custom (priv->exclude_groupnames,name,match_name_cmpfunc);
        if (found != NULL) 
        {
            ret = TRUE;
        }
    }

    return ret;
}

static GasGroup *create_new_group (GasGroupManager *manager)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GasGroup *group;

    group = g_object_new (GAS_TYPE_GROUP, NULL);
    priv->new_groups = g_slist_prepend (priv->new_groups, g_object_ref (group));

    g_signal_connect_object (group, "notify::is-loaded", G_CALLBACK (on_new_group_loaded), manager, 0);

    return group;
}

static void add_group (GasGroupManager *manager,
                       GasGroup        *group)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    const char *object_path;

    g_print ("GasGroupManager: tracking group '%s'", gas_group_get_group_name (group));
    g_hash_table_insert (priv->normal_groups_by_name,
                         g_strdup (gas_group_get_group_name (group)),
                                   g_object_ref (group));

    object_path = gas_group_get_object_path (group);
    if (object_path != NULL) 
    {
        g_hash_table_replace (priv->groups_by_object_path,
                             (gpointer) object_path,
                              g_object_ref (group));
    }

    g_signal_connect_object (group,
                            "changed",
                             G_CALLBACK (on_group_changed),
                             manager, 0);

    g_signal_emit (manager, signals[GROUP_ADDED], 0, group);
}

static void remove_group (GasGroupManager *manager,GasGroup *group)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);

    g_print ("GasGroupManager: no longer tracking group '%s' (with object path %s)",
              gas_group_get_group_name (group),
              gas_group_get_object_path (group));

    g_object_ref (group);
    g_signal_handlers_disconnect_by_func (group, on_group_changed, manager);

    if (gas_group_get_object_path (group) != NULL) 
    {
        g_hash_table_remove (priv->groups_by_object_path, act_group_get_object_path (group));
    }
    if (gas_group_get_group_name (group) != NULL) 
    {
        g_hash_table_remove (priv->normal_groups_by_name, gas_group_get_group_name (group));
    }

    if (priv->is_loaded) 
    {
        g_print ("GasGroupManager: loaded, so emitting group-removed signal");
        g_signal_emit (manager, signals[GROUP_REMOVED], 0, group);
    } 
    else 
    {
        g_print ("GasGroupManager: not yet loaded, so not emitting group-removed signal");
    }

    g_print ("GasGroupManager: group '%s' (with object path %s) now removed",
             gas_group_get_group_name (group),
             gas_group_get_object_path (group));
    g_object_unref (group);
}

static void update_group (GasGroupManager *manager,GasGroup *group)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    const char *name;
    
    g_print ("GasGroupManager: updating %s", DescribeGroup (group));
    name = gas_group_get_group_name (group);
    g_hash_table_insert (priv->normal_groups_by_name,
                         g_strdup (gas_group_get_group_name (group)),
                         g_object_ref (group));
    g_signal_emit (manager, signals[GROUP_ADDED], 0, group);
}

static GasGroup *lookup_group_by_name (GasGroupManager *manager,
                                       const char      *name)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GasGroup *group;

    group = g_hash_table_lookup (priv->normal_groups_by_name, name);

    if (user == NULL) 
    {
        group = g_hash_table_lookup (priv->system_groups_by_name, name);
    }

    return group;
}

static void on_new_group_loaded (GasGroup *group,
                                 GParamSpec     *pspec,
                                 GasGroupManager *manager)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    const char *name;
    GasGroup *old_group;

    if (!gas_group_is_loaded (group)) 
    {
        g_debug ("GasGroupManager: %s loaded function called when not loaded",
                DescribeGroup (group));
        return;
    }
    g_signal_handlers_disconnect_by_func (group, on_new_group_loaded, manager);

    priv->new_groups = g_slist_remove (priv->new_groups,group);
    priv->new_groups_inhibiting_load = g_slist_remove (priv->new_groups_inhibiting_load,
                                                       group);

    name = gas_group_get_group_name (group);

    if (name == NULL) 
    {
        const char *object_path;
        object_path = gas_group_get_object_path (group);

        if (object_path != NULL) 
        {
            g_warning ("GasGroupManager: %s has no groupname ""(object path: %s, gid: %d)",
                        DescribeGroup (group),object_path, (int) gas_group_get_gid (group));
        } 
        else 
        {
            g_warning ("GasGroupManager: %s has no groupname (gid: %d)",
                        DescribeGroup (group),(int) gas_group_get_gid (group));
        }
        g_object_unref (group);
        goto out;
    }

    if (groupname_in_exclude_list (manager, name)) 
    {
        g_print ("GasGroupManager: excluding group '%s'", name);
        g_object_unref (group);
        goto out;
    }

    old_group = lookup_group_by_name (manager,name);

    add_group (manager, group);
    g_object_unref (group);

out:
    if (priv->new_groups_inhibiting_load == NULL) 
    {
        g_print ("GasGroupManager: no pending groups, trying to set loaded property");
        set_is_loaded (manager, TRUE);
    } 
    else 
    {
        g_print ("GasGroupManager: not all groups loaded yet");
    }
}

static GasGroup *find_new_group_with_object_path (GasGroupManager *manager,
                                                  const char *object_path)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GSList *node;

    for (node = priv->new_groups; node != NULL; node = node->next) 
    {
        GasGroup *group = GAS_GROUP (node->data);
        const char *group_object_path = gas_group_get_object_path (group);
        if (g_strcmp0 (group_object_path, object_path) == 0) 
        {
            return group;
        }
    }
    return NULL;
}

static GasGroup *add_new_group_for_object_path (const char *object_path,
                                                GasGroupManager *manager)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GasGroup *group;

    group = g_hash_table_lookup (priv->groups_by_object_path, object_path);
    if (group != NULL) 
    {
        g_print ("GasGroupManager: tracking existing %s with object path %s",
                  DescribeGroup (group), object_path);
        return group;
    }

    group = find_new_group_with_object_path (manager, object_path);
    if (group != NULL) 
    {
        g_print ("GasGroupManager: tracking existing (but very recently added) %s with object path %s",
                  DescribeGroup (group), object_path);
        return group;
    }

    g_print ("GasGroupManager: tracking new group with object path %s", object_path);

    group = create_new_group (manager);
    _gas_group_update_from_object_path (group, object_path);

    return group;
}

static void new_group_add_in_group_admin_service (GDBusProxy *proxy,
                                              const char *object_path,
                                              gpointer    data)
{
    GasGroupManager *manager = GAS_GROUP_MANAGER (data);
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GasGroup *group = NULL;

    if (!priv->is_loaded) 
    {
        g_print ("GasGroupManager: ignoring new group in group_admin service with object path %s since not loaded yet",
                 object_path);
        return;
    }

    group = add_new_group_for_object_path (object_path, manager);
}

static void old_group_removed_in_group_admin_service (GDBusProxy *proxy,
                                                      const char *object_path,
                                                      gpointer    data)
{
    GasGroupManager *manager = GAS_GROUP_MANAGER (data);
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GasGroup *group ;
    GSList   *node;

    group = g_hash_table_lookup (priv->groups_by_object_path, object_path);
    if (user == NULL) 
    {
        g_print ("GasGroupManager: ignoring untracked group %s", object_path);
        return;
    } 
    else 
    {
        g_print ("GasGroupManager: tracked group %s removed from group-admin",object_path);
    }

    node = g_slist_find (priv->new_groups, group);
    if (node != NULL) 
    {
        g_signal_handlers_disconnect_by_func (group, on_new_group_loaded, manager);
        g_object_unref (group);
        priv->new_groups = g_slist_delete_link (priv->new_groups, node);
    }

    remove_group (manager,group);
}


static void
on_find_user_by_name_finished (GObject       *object,
                               GAsyncResult  *result,
                               gpointer       data)
{
        AccountsAccounts *proxy = ACCOUNTS_ACCOUNTS (object);
        ActUserManagerFetchUserRequest *request = data;
        g_autoptr(GError) error = NULL;
        char            *user;

        if (!accounts_accounts_call_find_user_by_name_finish (proxy, &user, result, &error)) {
                if (error != NULL) {
                        g_debug ("ActUserManager: Failed to find %s: %s",
                                 request->description, error->message);
                } else {
                        g_debug ("ActUserManager: Failed to find %s",
                                 request->description);
                }
                give_up (request->manager, request);
                return;
        }

        g_debug ("ActUserManager: Found object path of %s: %s",
                 request->description, user);
        request->object_path = user;
        request->state++;

        fetch_user_incrementally (request);
}

static void
on_find_user_by_id_finished (GObject       *object,
                             GAsyncResult  *result,
                             gpointer       data)
{
        AccountsAccounts *proxy = ACCOUNTS_ACCOUNTS (object);
        ActUserManagerFetchUserRequest *request = data;
        g_autoptr(GError) error = NULL;
        char            *user;

        if (!accounts_accounts_call_find_user_by_id_finish (proxy, &user, result, &error)) {
                if (error != NULL) {
                        g_debug ("ActUserManager: Failed to find user %lu: %s",
                                 (gulong) request->uid, error->message);
                } else {
                        g_debug ("ActUserManager: Failed to find user with id %lu",
                                 (gulong) request->uid);
                }
                give_up (request->manager, request);
                return;
        }

        g_debug ("ActUserManager: Found object path of %s: %s",
                 request->description, user);
        request->object_path = user;
        request->state++;

        fetch_user_incrementally (request);
}

static void
find_user_in_accounts_service (ActUserManager                 *manager,
                               ActUserManagerFetchUserRequest *request)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);

        g_assert (priv->accounts_proxy != NULL);

        g_debug ("ActUserManager: Looking for %s in accounts service",
                 request->description);

        switch (request->type) {
                case ACT_USER_MANAGER_FETCH_USER_FROM_USERNAME_REQUEST:
                    accounts_accounts_call_find_user_by_name (priv->accounts_proxy,
                                                              request->username,
                                                              NULL,
                                                              on_find_user_by_name_finished,
                                                              request);
                    break;
                case ACT_USER_MANAGER_FETCH_USER_FROM_ID_REQUEST:
                    accounts_accounts_call_find_user_by_id (priv->accounts_proxy,
                                                            request->uid,
                                                            NULL,
                                                            on_find_user_by_id_finished,
                                                            request);
                    break;

        }
}

static void
set_is_loaded (ActUserManager *manager,
               gboolean        is_loaded)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);

        if (priv->is_loaded != is_loaded) {
                priv->is_loaded = is_loaded;
                g_object_notify (G_OBJECT (manager), "is-loaded");
        }
}

static void
load_user_paths (ActUserManager       *manager,
                 const char * const * user_paths)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);

        /* We now have a batch of unloaded users that we know about. Once that initial
         * batch is loaded up, we can mark the manager as loaded.
         *
         * (see on_new_user_loaded)
         */
        if (g_strv_length ((char **) user_paths) > 0) {
                int i;

                g_debug ("ActUserManager: ListCachedUsers finished, will set loaded property after list is fully loaded");
                for (i = 0; user_paths[i] != NULL; i++) {
                        ActUser *user;

                        user = add_new_user_for_object_path (user_paths[i], manager);
                        if (!priv->is_loaded) {
                                priv->new_users_inhibiting_load = g_slist_prepend (priv->new_users_inhibiting_load, user);
                        }
                }
        } else {
                g_debug ("ActUserManager: ListCachedUsers finished with empty list, maybe setting loaded property now");
                maybe_set_is_loaded (manager);
        }
}

static void
load_included_usernames (ActUserManager *manager)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        GSList *l;

        /* Add users who are specifically included */
        for (l = priv->include_usernames; l != NULL; l = l->next) {
                g_debug ("ActUserManager: Adding included user %s", (char *)l->data);

                load_user (manager, l->data);
        }
}

static void
load_new_session_incrementally (ActUserManagerNewSession *new_session)
{
        switch (new_session->state) {
        case ACT_USER_MANAGER_NEW_SESSION_STATE_GET_PROXY:
                get_proxy_for_new_session (new_session);
                break;
        case ACT_USER_MANAGER_NEW_SESSION_STATE_GET_UID:
                get_uid_for_new_session (new_session);
                break;
        case ACT_USER_MANAGER_NEW_SESSION_STATE_GET_X11_DISPLAY:
                get_x11_display_for_new_session (new_session);
                break;
        case ACT_USER_MANAGER_NEW_SESSION_STATE_MAYBE_ADD:
                maybe_add_new_session (new_session);
                break;
        case ACT_USER_MANAGER_NEW_SESSION_STATE_LOADED:
                break;
        default:
                g_assert_not_reached ();
        }
}

static void free_fetch_group_request (GasGroupManagerFetchGroupRequest *request)
{
    GasGroupManager *manager = request->manager;
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);

    g_object_set_data (G_OBJECT (request->group), "fetch-user-request", NULL);

    priv->fetch_group_requests = g_slist_remove (priv->fetch_group_requests, request);
    if (request->type == GAS_GROUP_MANAGER_FETCH_GROUP_FROM_GROUPNAME_REQUEST) 
    {
        g_free (request->name);
    }

    g_free (request->object_path);
    g_free (request->description);
    g_object_unref (manager);
    g_slice_free (GasGRoupManagerFetchGroupRequest, request);
}

static void
give_up (ActUserManager                 *manager,
         ActUserManagerFetchUserRequest *request)
{
        if (request->type == ACT_USER_MANAGER_FETCH_USER_FROM_USERNAME_REQUEST)
                g_debug ("ActUserManager: failed to load user %s", request->username);
        else
                g_debug ("ActUserManager: failed to load user %lu", (gulong) request->uid);

        request->state = ACT_USER_MANAGER_GET_USER_STATE_UNFETCHED;

        if (request->user)
                _act_user_update_as_nonexistent (request->user);
}

static void
on_user_manager_maybe_ready_for_request (ActUserManager                 *manager,
                                         GParamSpec                     *pspec,
                                         ActUserManagerFetchUserRequest *request)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);

        if (!priv->is_loaded) {
                return;
        }

        g_debug ("ActUserManager: user manager now loaded, proceeding with fetch user request for %s",
                 request->description);

        g_signal_handlers_disconnect_by_func (manager, on_user_manager_maybe_ready_for_request, request);

        request->state++;
        fetch_user_incrementally (request);
}

static void
fetch_user_incrementally (ActUserManagerFetchUserRequest *request)
{
        ActUserManager *manager = request->manager;
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);

        g_debug ("ActUserManager: finding %s state %d",
                 request->description, request->state);
        switch (request->state) {
        case ACT_USER_MANAGER_GET_USER_STATE_WAIT_FOR_LOADED:
                if (priv->is_loaded) {
                        request->state++;
                        fetch_user_incrementally (request);
                } else {
                        g_debug ("ActUserManager: waiting for user manager to load before finding %s",
                                 request->description);
                        g_signal_connect (manager, "notify::is-loaded",
                                          G_CALLBACK (on_user_manager_maybe_ready_for_request), request);

                }
                break;

        case ACT_USER_MANAGER_GET_USER_STATE_ASK_ACCOUNTS_SERVICE:
                if (priv->accounts_proxy == NULL) {
                        give_up (manager, request);
                } else {
                        find_user_in_accounts_service (manager, request);
                }
                break;
        case ACT_USER_MANAGER_GET_USER_STATE_FETCHED:
                g_debug ("ActUserManager: %s fetched", request->description);
                _act_user_update_from_object_path (request->user, request->object_path);
                break;
        case ACT_USER_MANAGER_GET_USER_STATE_UNFETCHED:
                g_debug ("ActUserManager: %s was not fetched", request->description);
                break;
        default:
                g_assert_not_reached ();
        }

        if (request->state == ACT_USER_MANAGER_GET_USER_STATE_FETCHED  ||
            request->state == ACT_USER_MANAGER_GET_USER_STATE_UNFETCHED) {
                g_debug ("ActUserManager: finished handling request for %s",
                         request->description);
                free_fetch_user_request (request);
        }
}

static void
fetch_user_with_username_from_accounts_service (ActUserManager *manager,
                                                ActUser        *user,
                                                const char     *username)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        ActUserManagerFetchUserRequest *request;

        request = g_slice_new0 (ActUserManagerFetchUserRequest);

        request->manager = g_object_ref (manager);
        request->type = ACT_USER_MANAGER_FETCH_USER_FROM_USERNAME_REQUEST;
        request->username = g_strdup (username);
        request->user = user;
        request->state = ACT_USER_MANAGER_GET_USER_STATE_UNFETCHED + 1;
        request->description = g_strdup_printf ("user '%s'", request->username);

        priv->fetch_user_requests = g_slist_prepend (priv->fetch_user_requests,
                                                     request);
        g_object_set_data (G_OBJECT (user), "fetch-user-request", request);
        fetch_user_incrementally (request);
}

static void
fetch_user_with_id_from_accounts_service (ActUserManager *manager,
                                          ActUser        *user,
                                          uid_t           id)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        ActUserManagerFetchUserRequest *request;

        request = g_slice_new0 (ActUserManagerFetchUserRequest);

        request->manager = g_object_ref (manager);
        request->type = ACT_USER_MANAGER_FETCH_USER_FROM_ID_REQUEST;
        request->uid = id;
        request->user = user;
        request->state = ACT_USER_MANAGER_GET_USER_STATE_UNFETCHED + 1;
        request->description = g_strdup_printf ("user with id %lu", (gulong) request->uid);

        priv->fetch_user_requests = g_slist_prepend (priv->fetch_user_requests,
                                                     request);
        g_object_set_data (G_OBJECT (user), "fetch-user-request", request);
        fetch_user_incrementally (request);
}

ActUser *
act_user_manager_get_user (ActUserManager *manager,
                           const char     *username)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        ActUser *user;

        g_return_val_if_fail (ACT_IS_USER_MANAGER (manager), NULL);
        g_return_val_if_fail (username != NULL && username[0] != '\0', NULL);

        user = lookup_user_by_name (manager, username);

        /* if we don't have it loaded try to load it now */
        if (user == NULL) {
                g_debug ("ActUserManager: trying to track new user with username %s", username);
                user = create_new_user (manager);

                if (priv->accounts_proxy != NULL) {
                        fetch_user_with_username_from_accounts_service (manager, user, username);
                }
        }

        return user;
}

static void
load_user (ActUserManager *manager,
           const char     *username)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        ActUser *user;
        g_autoptr(GError) error = NULL;
        char *object_path = NULL;
        gboolean user_found;

        g_return_if_fail (ACT_IS_USER_MANAGER (manager));
        g_return_if_fail (username != NULL && username[0] != '\0');

        user = lookup_user_by_name (manager, username);

        if (user == NULL) {
                g_debug ("ActUserManager: trying to track new user with username %s", username);
                user = create_new_user (manager);
        }

        user_found = accounts_accounts_call_find_user_by_name_sync (priv->accounts_proxy,
                                                                    username,
                                                                    &object_path,
                                                                    NULL,
                                                                    &error);

        if (!user_found) {
                if (error != NULL) {
                        g_debug ("ActUserManager: Failed to find user '%s': %s",
                                 username, error->message);
                } else {
                        g_debug ("ActUserManager: Failed to find user '%s'",
                                 username);
                }
        }

        _act_user_update_from_object_path (user, object_path);
}

ActUser *
act_user_manager_get_user_by_id (ActUserManager *manager,
                                 uid_t           id)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        ActUser *user;
        g_autofree gchar *object_path = NULL;

        g_return_val_if_fail (ACT_IS_USER_MANAGER (manager), NULL);

        object_path = g_strdup_printf ("/org/freedesktop/Accounts/User%lu", (gulong) id);
        user = g_hash_table_lookup (priv->users_by_object_path, object_path);

        if (user != NULL) {
                return g_object_ref (user);
        } else {
                g_debug ("ActUserManager: trying to track new user with uid %lu", (gulong) id);
                user = create_new_user (manager);

                if (priv->accounts_proxy != NULL) {
                        fetch_user_with_id_from_accounts_service (manager, user, id);
                }
        }

        return user;
}

static void
listify_hash_values_hfunc (gpointer key,
                           gpointer value,
                           gpointer user_data)
{
        GSList **list = user_data;

        *list = g_slist_prepend (*list, value);
}

GSList *
act_user_manager_list_users (ActUserManager *manager)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        GSList *retval;

        g_return_val_if_fail (ACT_IS_USER_MANAGER (manager), NULL);

        if (!priv->list_cached_users_done) {
                load_users (manager);

                if (priv->seat.state == ACT_USER_MANAGER_SEAT_STATE_GET_SEAT_PROXY)
                        queue_load_seat_incrementally (manager);
        }

        retval = NULL;
        g_hash_table_foreach (priv->normal_users_by_name, listify_hash_values_hfunc, &retval);

        return g_slist_sort (retval, (GCompareFunc) act_user_collate);
}

static void maybe_set_is_loaded (GasGroupManager *manager)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);

}


static GSList *
slist_deep_copy (const GSList *list)
{
        GSList *retval;
        GSList *l;

        if (list == NULL)
                return NULL;

        retval = g_slist_copy ((GSList *) list);
        for (l = retval; l != NULL; l = l->next) {
                l->data = g_strdup (l->data);
        }

        return retval;
}

static void
load_users (ActUserManager *manager)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        g_autoptr(GError) error = NULL;
        g_auto(GStrv) user_paths = NULL;
        gboolean could_list = FALSE;

        if (!ensure_accounts_proxy (manager)) {
                return;
        }

        g_debug ("ActUserManager: calling 'ListCachedUsers'");

        could_list = accounts_accounts_call_list_cached_users_sync (priv->accounts_proxy,
                                                                    &user_paths,
                                                                    NULL, &error);

        if (!could_list) {
                g_debug ("ActUserManager: ListCachedUsers failed: %s", error->message);
                return;
        }

        load_user_paths (manager, (const char * const *) user_paths);

        load_included_usernames (manager);

        priv->list_cached_users_done = TRUE;
}

static gboolean
load_seat_incrementally (ActUserManager *manager)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);

        priv->seat.load_idle_id = 0;

        switch (priv->seat.state) {
        case ACT_USER_MANAGER_SEAT_STATE_GET_SESSION_ID:
                get_current_session_id (manager);
                break;
        case ACT_USER_MANAGER_SEAT_STATE_GET_SESSION_PROXY:
                get_session_proxy (manager);
                break;
        case ACT_USER_MANAGER_SEAT_STATE_GET_ID:
                get_seat_id_for_current_session (manager);
                break;
        case ACT_USER_MANAGER_SEAT_STATE_GET_SEAT_PROXY:
                get_seat_proxy (manager);
                break;
        case ACT_USER_MANAGER_SEAT_STATE_LOADED:
                g_debug ("ActUserManager: Seat loading sequence complete");
                break;
        default:
                g_assert_not_reached ();
        }

        if (priv->seat.state == ACT_USER_MANAGER_SEAT_STATE_LOADED) {
                load_sessions (manager);
        }

        maybe_set_is_loaded (manager);

        return FALSE;
}

static gboolean
load_idle (ActUserManager *manager)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);

        priv->seat.state = ACT_USER_MANAGER_SEAT_STATE_UNLOADED + 1;
        load_seat_incrementally (manager);
        priv->load_id = 0;

        return FALSE;
}

static void
queue_load_seat (ActUserManager *manager)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);

        if (priv->load_id > 0) {
                return;
        }

        priv->load_id = g_idle_add ((GSourceFunc)load_idle, manager);
}

static void gas_group_manager_get_property (GObject        *object,
                                            guint           prop_id,
                                            GValue         *value,
                                            GParamSpec     *pspec)
{
    GasGroupManager *manager = GAS_GROUP_MANAGER (object);
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);

    switch (prop_id) 
    {
        case PROP_IS_LOADED:
            g_value_set_boolean (value, priv->is_loaded);
            break;
        case PROP_INCLUDE_GROUPNAMES_LIST:
            g_value_set_pointer (value, priv->include_groupnames);
            break;
        case PROP_EXCLUDE_USERNAMES_LIST:
            g_value_set_pointer (value, priv->exclude_groupnames);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
             break;
    }
}

static void set_include_groupnames (GasGroupManager *manager,
                                    GSList          *list)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);

    if (priv->include_groupnames != NULL) 
    {
        g_slist_foreach (priv->include_groupnames, (GFunc) g_free, NULL);
        g_slist_free (priv->include_groupnames);
    }
    priv->include_groupnames = slist_deep_copy (list);
}

static void set_exclude_groupnames (GasGroupManager *manager,
                                    GSList          *list)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);

    if (priv->exclude_groupnames != NULL) 
    {
        g_slist_foreach (priv->exclude_groupnames, (GFunc) g_free, NULL);
        g_slist_free (priv->exclude_groupnames);
    }
    priv->exclude_groupnames = slist_deep_copy (list);
}

static void gas_group_manager_set_property (GObject        *object,
                                            guint           prop_id,
                                            const GValue   *value,
                                            GParamSpec     *pspec)
{
    GasGroupManager *self;
    self = GAS_GROUP_MANAGER (object);

    switch (prop_id) 
    {
        case PROP_INCLUDE_GROUPNAMES_LIST:
            set_include_groupnames (self, g_value_get_pointer (value));
            break;
        case PROP_EXCLUDE_USERNAMES_LIST:
            set_exclude_groupnames (self, g_value_get_pointer (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void gas_group_manager_class_init (GasGroupManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize =     gas_group_manager_finalize;
    object_class->get_property = gas_group_manager_get_property;
    object_class->set_property = gas_group_manager_set_property;

    g_object_class_install_property (object_class,
                                     PROP_IS_LOADED,
                                     g_param_spec_boolean ("is-loaded",
                                                           "Is loaded",
                                                            "Determines whether or not the manager object is loaded and ready to read from.",
                                                            FALSE,
                                                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (object_class,
                                     PROP_INCLUDE_GROUPNAMES_LIST,
                                     g_param_spec_pointer ("include-usernames-list",
                                                           "Include usernames list",
                                                           "Usernames who are specifically included",
                                                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (object_class,
                                     PROP_EXCLUDE_GROUPNAMES_LIST,
                                     g_param_spec_pointer ("exclude-usernames-list",
                                                           "Exclude usernames list",
                                                           "Usernames who are specifically excluded",
                                                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    signals [GROUP_ADDED] = g_signal_new ("group-added",
                            G_TYPE_FROM_CLASS (klass),
                            G_SIGNAL_RUN_LAST,
                            G_STRUCT_OFFSET (GasGroupManagerClass, group_added),
                            NULL, NULL,
                            g_cclosure_marshal_VOID__OBJECT,
                            G_TYPE_NONE, 1, GAS_TYPE_GROUP);

    signals [GROUP_REMOVED] = g_signal_new ("group-removed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GasGroupManagerClass,group_removed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1, ACT_TYPE_GROUP);

    signals [GROUP_CHANGED] = g_signal_new ("group-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GasGroupManagerClass,group_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1, GAS_TYPE_GROUP);
}

static void
act_user_manager_queue_load (ActUserManager *manager)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);

        g_return_if_fail (ACT_IS_USER_MANAGER (manager));

        if (!priv->is_loaded) {
                queue_load_seat (manager);
        }
}

static gboolean ensure_group_admin_proxy (GasGroupManager *manager)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GError *error = NULL;
    
    if (priv->group_admin_proxy != NULL) 
    {
        return TRUE;
    }

    priv->group_admin_proxy = user_group_admin_proxy_new_sync (priv->connection,
                                                               G_DBUS_PROXY_FLAGS_NONE,
                                                               GROUPADMIN_NAME,
                                                               GROUPADMIN_PATH,
                                                               NULL,
                                                               &error);
    if (error != NULL) 
    {
        g_print("ActUserManager: getting account proxy failed: %s", error->message);
        g_error_free(error);
        return FALSE;
    }

    g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (priv->group_admin_proxy), G_MAXINT);


    g_signal_connect (priv->group_admin_proxy,
                     "group-added",
                      G_CALLBACK (new_group_add_in_group_admin_service),
                      manager);
    g_signal_connect (priv->group_admin_proxy,
                     "group-deleted",
                      G_CALLBACK (old_group_removed_in_group_admin_service),
                      manager);

    return TRUE;
}

static GHashTable *CreateHashNewTable(void )
{
    return g_hash_table_new_full(g_str_hash,
                                 g_str_equal,
                                 g_free,
                                 g_object_unref);
}    
static void gas_group_manager_init (GasGroupManager *manager)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GError *error = NULL;

    priv->normal_groups_by_name = CreateHashNewTable();

    priv->system_users_by_name  = CreateHashNewTable();

    priv->users_by_object_path = g_hash_table_new_full (g_str_hash,
                                                        g_str_equal,
                                                        NULL,
                                                        g_object_unref);

    priv->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (priv->connection == NULL) 
    {
        if (error != NULL) 
        {
            g_warning ("Failed to connect to the D-Bus daemon: %s", error->message);
            g_error_free (error);
        }
        else 
        {
            g_warning ("Failed to connect to the D-Bus daemon");
        }
        return;
    }
    ensure_group_admin_proxy (manager);
}

static void gas_group_manager_finalize (GObject *object)
{
    GasGroupManager *manager = GAS_GROUP_MANAGER (object);
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GSList         *node;
    GasGroup *group;
    GSList   *next_node;

    g_slist_foreach (priv->fetch_group_requests,
                    (GFunc) free_fetch_group_request, NULL);
    g_slist_free (priv->fetch_group_requests);
    g_slist_free (priv->new_groups_inhibiting_load);

    node = priv->new_groups;
    while (node != NULL) 
    {
        group = GAS_GROUP (node->data);
        next_node = node->next;
        g_signal_handlers_disconnect_by_func (group, on_new_group_loaded, manager);
        g_object_unref (group);
        priv->new_groups = g_slist_delete_link (priv->new_groups, node);
        node = next_node;
    }

    if (priv->exclude_groupnames != NULL) 
    {
        g_slist_foreach (priv->exclude_groupnames, (GFunc) g_free, NULL);
        g_slist_free (priv->exclude_groupnames);
    }

    if (priv->include_groupnames != NULL) 
    {
        g_slist_foreach (priv->include_groupnames, (GFunc) g_free, NULL);
        g_slist_free (priv->include_groupnames);
    }

    if (priv->group_admin_proxy != NULL) 
    {
        g_object_unref (priv->group_admin_proxy);
    }

    if (priv->load_id > 0) 
    {
        g_source_remove (priv->load_id);
        priv->load_id = 0;
    }


    g_hash_table_destroy (priv->normal_groups_by_name);
    g_hash_table_destroy (priv->groups_by_object_path);

}

GasGroupManager *gas_group_manager_get_default (void)
{
    if (group_manager_object == NULL) 
    {
        group_manager_object = g_object_new (GAS_TYPE_GROUP_MANAGER, NULL);
        g_object_add_weak_pointer (group_manager_object,
                                  (gpointer *) &group_manager_object);
        gas_group_manager_queue_load (group_manager_object);
    }

    return GAS_GROUP_MANAGER (group_manager_object);
}

gboolean
act_user_manager_no_service (ActUserManager *manager)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        return priv->accounts_proxy == NULL;
}

ActUser *
act_user_manager_create_user (ActUserManager      *manager,
                              const char          *username,
                              const char          *fullname,
                              ActUserAccountType   accounttype,
                              GError             **error)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        GError *local_error = NULL;
        gboolean res;
        g_autofree gchar *path = NULL;
        ActUser *user;

        g_debug ("ActUserManager: Creating user '%s', '%s', %d",
                 username, fullname, accounttype);

        g_assert (priv->accounts_proxy != NULL);

        res = accounts_accounts_call_create_user_sync (priv->accounts_proxy,
                                                       username,
                                                       fullname,
                                                       accounttype,
                                                       &path,
                                                       NULL,
                                                       &local_error);
        if (!res) {
                g_propagate_error (error, local_error);
                return NULL;
        }

        user = add_new_user_for_object_path (path, manager);

        return user;
}

static void
act_user_manager_async_complete_handler (GObject      *source,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
        GTask *task = user_data;

        g_task_return_pointer (task, g_object_ref (result), g_object_unref);
        g_object_unref (task);
}

void
act_user_manager_create_user_async (ActUserManager      *manager,
                                    const char          *username,
                                    const char          *fullname,
                                    ActUserAccountType   accounttype,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        GTask *task;

        g_return_if_fail (ACT_IS_USER_MANAGER (manager));
        g_return_if_fail (priv->accounts_proxy != NULL);

        g_debug ("ActUserManager: Creating user (async) '%s', '%s', %d",
                 username, fullname, accounttype);

        g_assert (priv->accounts_proxy != NULL);

        task = g_task_new (G_OBJECT (manager),
                           cancellable,
                           callback, user_data);

        accounts_accounts_call_create_user (priv->accounts_proxy,
                                            username,
                                            fullname,
                                            accounttype,
                                            cancellable,
                                            act_user_manager_async_complete_handler, task);
}

ActUser *
act_user_manager_create_user_finish (ActUserManager  *manager,
                                     GAsyncResult    *result,
                                     GError         **error)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        GAsyncResult *inner_result;
        ActUser *user = NULL;
        g_autofree gchar *path = NULL;
        GError *remote_error = NULL;

        inner_result = g_task_propagate_pointer (G_TASK (result), error);
        if (inner_result == NULL) {
                return FALSE;
        }

        if (accounts_accounts_call_create_user_finish (priv->accounts_proxy,
                                                       &path, inner_result, &remote_error)) {
                user = add_new_user_for_object_path (path, manager);
        }

        if (remote_error) {
                g_dbus_error_strip_remote_error (remote_error);
                g_propagate_error (error, remote_error);
        }

        return user;
}

gboolean
act_user_manager_delete_user (ActUserManager  *manager,
                              ActUser         *user,
                              gboolean         remove_files,
                              GError         **error)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        GError *local_error = NULL;

        g_debug ("ActUserManager: Deleting user '%s' (uid %ld)", act_user_get_user_name (user), (long) act_user_get_uid (user));

        g_return_val_if_fail (ACT_IS_USER_MANAGER (manager), FALSE);
        g_return_val_if_fail (ACT_IS_USER (user), FALSE);
        g_return_val_if_fail (priv->accounts_proxy != NULL, FALSE);

        if (!accounts_accounts_call_delete_user_sync (priv->accounts_proxy,
                                                      act_user_get_uid (user),
                                                      remove_files,
                                                      NULL,
                                                      &local_error)) {
                g_propagate_error (error, local_error);
                return FALSE;
        }

        return TRUE;
}
void
act_user_manager_delete_user_async (ActUserManager      *manager,
                                    ActUser             *user,
                                    gboolean             remove_files,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        GTask *task;

        g_return_if_fail (ACT_IS_USER_MANAGER (manager));
        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (priv->accounts_proxy != NULL);

        task = g_task_new (G_OBJECT (manager),
                           cancellable,
                           callback, user_data);

        g_debug ("ActUserManager: Deleting (async) user '%s' (uid %ld)", act_user_get_user_name (user), (long) act_user_get_uid (user));

        accounts_accounts_call_delete_user (priv->accounts_proxy,
                                            act_user_get_uid (user), remove_files,
                                            cancellable,
                                            act_user_manager_async_complete_handler, task);
}

gboolean
act_user_manager_delete_user_finish (ActUserManager  *manager,
                                     GAsyncResult    *result,
                                     GError         **error)
{
        ActUserManagerPrivate *priv = act_user_manager_get_instance_private (manager);
        GAsyncResult *inner_result;
        gboolean success;
        GError *remote_error = NULL;

        inner_result = g_task_propagate_pointer (G_TASK (result), error);
        if (inner_result == NULL) {
                return FALSE;
        }

        success = accounts_accounts_call_delete_user_finish (priv->accounts_proxy,
                                                             inner_result, &remote_error);
        if (remote_error) {
                g_dbus_error_strip_remote_error (remote_error);
                g_propagate_error (error, remote_error);
        }

        return success;
}
