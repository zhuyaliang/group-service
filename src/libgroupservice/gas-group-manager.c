#include "config.h"
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
#include "gas-group-private.h"
#include "group-generated.h"
#include "group-list-generated.h"

#define GROUPADMIN_NAME      "org.group.admin"
#define GROUPADMIN_PATH      "/org/group/admin"
#define GROUPADMIN_INTERFACE "org.group.admin"

typedef enum 
{
    GAS_GROUP_MANAGER_GET_GROUP_STATE_UNFETCHED = 0,
    GAS_GROUP_MANAGER_GET_GROUP_STATE_WAIT_FOR_LOADED,
    GAS_GROUP_MANAGER_GET_GROUP_STATE_ASK_GROUP_SERVICE,
    GAS_GROUP_MANAGER_GET_GROUP_STATE_FETCHED
} GasGroupManagerGetGroupState;

typedef enum 
{
    GAS_GROUP_MANAGER_FETCH_GROUP_FROM_GROUPNAME_REQUEST,
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
    GHashTable            *normal_groups_by_name;
    GHashTable            *groups_by_object_path;
    GDBusConnection       *connection;
    UserGroupAdmin        *group_admin_proxy;

    GSList                *new_groups;
    GSList                *new_groups_inhibiting_load;
    GSList                *fetch_group_requests;

    GSList                *exclude_groupnames;
    GSList                *include_groupnames;

    gboolean               is_loaded;
	gboolean               list_cached_groups_done;
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

static void     gas_group_manager_finalize   (GObject             *object);
static void fetch_group_incrementally (GasGroupManagerFetchGroupRequest *request);
static gboolean ensure_group_admin_proxy     (GasGroupManager *manager);
static void     load_groups                  (GasGroupManager *manager);
static void     load_group                  (GasGroupManager  *manager,
                                             const char       *groupname);

static void     set_is_loaded (GasGroupManager *manager, gboolean is_loaded);

static void     on_new_group_loaded (GasGroup        *group,
                                     GParamSpec     *pspec,
                                     GasGroupManager *manager);
static void     give_up (GasGroupManager                 *manager,
                         GasGroupManagerFetchGroupRequest *request);

static void     update_group                    (GasGroupManager *manager,
                                                 GasGroup        *group);
static gpointer group_manager_object = NULL;

G_DEFINE_TYPE_WITH_PRIVATE (GasGroupManager, gas_group_manager, G_TYPE_OBJECT)

static void g_free_list_data(gpointer data,gpointer userdata)
{
	g_free(data);
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
        g_debug ("GasGroupManager: sending group-changed signal for %s",
                 DescribeGroup (group));

        g_signal_emit (manager, signals[GROUP_CHANGED], 0, group);

        g_debug ("GasGroupManager: sent group-changed signal for %s",
                 DescribeGroup (group));

        update_group (manager,group);
    }
}

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

    g_object_ref (group);
    g_signal_handlers_disconnect_by_func (group, on_group_changed, manager);

    if (gas_group_get_object_path (group) != NULL) 
    {
        g_hash_table_remove (priv->groups_by_object_path, gas_group_get_object_path (group));
    }
    if (gas_group_get_group_name (group) != NULL) 
    {
        g_hash_table_remove (priv->normal_groups_by_name, gas_group_get_group_name (group));
    }

    if (priv->is_loaded) 
    {
        g_debug ("GasGroupManager: loaded, so emitting group-removed signal");
        g_signal_emit (manager, signals[GROUP_REMOVED], 0, group);
    } 
    else 
    {
        g_debug ("GasGroupManager: not yet loaded, so not emitting group-removed signal");
    }

    g_object_unref (group);
}

static void update_group (GasGroupManager *manager,GasGroup *group)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    
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

    return group;
}

static void on_new_group_loaded (GasGroup *group,
                                 GParamSpec     *pspec,
                                 GasGroupManager *manager)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    const char *name;

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
        g_debug ("GasGroupManager: excluding group '%s'", name);
        g_object_unref (group);
        goto out;
    }

    add_group (manager, group);
    g_object_unref (group);

out:
    if (priv->new_groups_inhibiting_load == NULL) 
    {
        g_debug ("GasGroupManager: no pending groups, trying to set loaded property");
        set_is_loaded (manager, TRUE);
    } 
    else 
    {
        g_debug ("GasGroupManager: not all groups loaded yet");
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
        g_debug ("GasGroupManager: tracking existing %s with object path %s",
                  DescribeGroup (group), object_path);
        return group;
    }

    group = find_new_group_with_object_path (manager, object_path);
    if (group != NULL) 
    {
        g_debug ("GasGroupManager: tracking existing (but very recently added) %s with object path %s",
                  DescribeGroup (group), object_path);
        return group;
    }

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

    if (!priv->is_loaded) 
    {
        g_debug ("GasGroupManager: ignoring new group in group_admin service with object path %s since not loaded yet",
                 object_path);
        return;
    }

    add_new_group_for_object_path (object_path, manager);
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
    if (group == NULL) 
    {
        g_debug ("GasGroupManager: ignoring untracked group %s", object_path);
        return;
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
on_find_group_by_name_finished (GObject       *object,
                               GAsyncResult  *result,
                               gpointer       data)
{
    UserGroupAdmin *proxy = USER_GROUP_ADMIN (object);
    GasGroupManagerFetchGroupRequest *request = data;
    GError *error = NULL;
    char *group;

    if (!user_group_admin_call_find_group_by_name_finish(proxy, &group, result, &error)) 
    {
        give_up (request->manager, request);
        return;
    }
    request->object_path = group;
    request->state++;

    fetch_group_incrementally (request);
}
static void
on_find_group_by_id_finished (GObject       *object,
                              GAsyncResult  *result,
                              gpointer       data)
{
    UserGroupAdmin *proxy = USER_GROUP_ADMIN (object);
    GasGroupManagerFetchGroupRequest *request = data;
    GError *error = NULL;
    char *group;

    if (!user_group_admin_call_find_group_by_id_finish(proxy, &group, result, &error)) 
    {
        give_up (request->manager, request);
        return;
    }
    request->object_path = group;
    request->state++;

    fetch_group_incrementally (request);
}
static void find_group_in_group_admin_service (GasGroupManager *manager,
                               GasGroupManagerFetchGroupRequest *request)
{	
	GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);

    switch (request->type) 
	{
    	case GAS_GROUP_MANAGER_FETCH_GROUP_FROM_GROUPNAME_REQUEST:
        	user_group_admin_call_find_group_by_name (priv->group_admin_proxy,
                                                      request->name,
                                                      NULL,
                                                      on_find_group_by_name_finished,
                                                      request);
        	break;
     	case GAS_GROUP_MANAGER_FETCH_GROUP_FROM_ID_REQUEST:
        	user_group_admin_call_find_group_by_id (priv->group_admin_proxy,
                                                    request->gid,
                                                    NULL,
                                                    on_find_group_by_id_finished,
                                                    request);
        	break;
		default:
			break;

    }
}

static void set_is_loaded (GasGroupManager *manager,
                           gboolean        is_loaded)
{
	GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);

    if (priv->is_loaded != is_loaded) 
	{
    	priv->is_loaded = is_loaded;
        g_object_notify (G_OBJECT (manager), "is-loaded");
    }
}

static void load_groups_paths (GasGroupManager       *manager,
                               const char * const * group_paths)
{
    int i;
    GasGroup *group;
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    
	if (g_strv_length ((char **) group_paths) > 0) 
	{
    	for (i = 0; group_paths[i] != NULL; i++) 
		{
			group = add_new_group_for_object_path (group_paths[i], manager);
            if (!priv->is_loaded) 
			{
            	priv->new_groups_inhibiting_load = g_slist_prepend (priv->new_groups_inhibiting_load, group);
            }
        }
    } 
}

static void load_included_groupnames (GasGroupManager *manager)
{
	GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GSList *l;

    for (l = priv->include_groupnames; l != NULL; l = l->next) 
	{	
		g_debug ("GasGroupManager: Adding included group %s", (char *)l->data);
		load_group (manager, l->data);
    }
}

static void give_up (GasGroupManager                 *manager,
         GasGroupManagerFetchGroupRequest *request)
{
	request->type = GAS_GROUP_MANAGER_FETCH_GROUP_FROM_GROUPNAME_REQUEST;
	request->state = GAS_GROUP_MANAGER_GET_GROUP_STATE_UNFETCHED;
}

static void
on_group_manager_maybe_ready_for_request (GasGroupManager                 *manager,
                                         GParamSpec                     *pspec,
                                         GasGroupManagerFetchGroupRequest *request)
{
	GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);

    if (!priv->is_loaded) 
	{
    	return;
    }
    g_signal_handlers_disconnect_by_func (manager, on_group_manager_maybe_ready_for_request, request);

    request->state++;
    fetch_group_incrementally (request);
}
static void
free_fetch_group_request (GasGroupManagerFetchGroupRequest *request,gpointer userdata)
{
    GasGroupManager *manager = request->manager;
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);

    g_object_set_data (G_OBJECT (request->group), "fetch-user-request", NULL);

    priv->fetch_group_requests = g_slist_remove (priv->fetch_group_requests, request);
    if (request->type == GAS_GROUP_MANAGER_FETCH_GROUP_FROM_GROUPNAME_REQUEST) {
            g_free (request->name);
    }

    g_free (request->object_path);
    g_free (request->description);
    g_object_unref (manager);

    g_slice_free (GasGroupManagerFetchGroupRequest, request);
}

static void fetch_group_incrementally (GasGroupManagerFetchGroupRequest *request)
{
	GasGroupManager *manager = request->manager;
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);

    switch (request->state) 
	{
    	case GAS_GROUP_MANAGER_GET_GROUP_STATE_WAIT_FOR_LOADED:
    		if (priv->is_loaded) 
			{
            	request->state++;
                fetch_group_incrementally (request);
            } 
			else 
			{
            	g_signal_connect (manager, "notify::is-loaded",
                                 G_CALLBACK (on_group_manager_maybe_ready_for_request), request);

            }
            break;

    	case GAS_GROUP_MANAGER_GET_GROUP_STATE_ASK_GROUP_SERVICE:
            if (priv->group_admin_proxy == NULL) 
			{
             	give_up (manager, request);
            } 
			else 
			{
                find_group_in_group_admin_service (manager, request);
            }
            break;
    	case GAS_GROUP_MANAGER_GET_GROUP_STATE_FETCHED:
            _gas_group_update_from_object_path (request->group, request->object_path);
            break;
    	case GAS_GROUP_MANAGER_GET_GROUP_STATE_UNFETCHED:
            break;
    	default:
            g_assert_not_reached ();
    }

    if (request->state == GAS_GROUP_MANAGER_GET_GROUP_STATE_FETCHED  ||
        request->state == GAS_GROUP_MANAGER_GET_GROUP_STATE_UNFETCHED) {
            free_fetch_group_request (request,NULL);
    }
}

static void
fetch_group_with_groupname_from_group_admin_service (GasGroupManager *manager,
                                                     GasGroup        *group,
                                                     const char      *name)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GasGroupManagerFetchGroupRequest *request;

    request = g_slice_new0 (GasGroupManagerFetchGroupRequest);

    request->manager = g_object_ref (manager);
    request->type = GAS_GROUP_MANAGER_FETCH_GROUP_FROM_GROUPNAME_REQUEST;
    request->name = g_strdup (name);
    request->group = group;
    request->state = GAS_GROUP_MANAGER_GET_GROUP_STATE_UNFETCHED + 1;
    request->description = g_strdup_printf ("group '%s'", request->name);

    priv->fetch_group_requests = g_slist_prepend (priv->fetch_group_requests,
                                                 request);
    g_object_set_data (G_OBJECT (group), "fetch-user-request", request);
    fetch_group_incrementally (request);
}

static void fetch_group_with_id_from_group_admin_service (GasGroupManager *manager,
                                          GasGroup        *group,
                                          gid_t           id)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GasGroupManagerFetchGroupRequest *request;

    request = g_slice_new0 (GasGroupManagerFetchGroupRequest);

    request->manager = g_object_ref (manager);
    request->type = GAS_GROUP_MANAGER_FETCH_GROUP_FROM_ID_REQUEST;
    request->gid = id;
    request->group = group;
    request->state = GAS_GROUP_MANAGER_GET_GROUP_STATE_UNFETCHED + 1;
    request->description = g_strdup_printf ("group with id %lu", (gulong) request->gid);

    priv->fetch_group_requests = g_slist_prepend (priv->fetch_group_requests,
                                                 request);
    g_object_set_data (G_OBJECT (group), "fetch-group-request", request);
    fetch_group_incrementally (request);
}

GasGroup *gas_group_manager_get_group (GasGroupManager *manager,
                           const char     *name)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GasGroup *group;

    g_return_val_if_fail (GAS_IS_GROUP_MANAGER (manager), NULL);
    g_return_val_if_fail (name != NULL && name[0] != '\0', NULL);

    group = lookup_group_by_name (manager,name);

    if (group == NULL) 
	{
    	group = create_new_group (manager);
		if (priv->group_admin_proxy != NULL) 
		{
        	fetch_group_with_groupname_from_group_admin_service (manager,group,name);
        }
    }

    return group;
}

static void load_group (GasGroupManager *manager,const char *name)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GasGroup *group;
    GError *error = NULL;
    char *object_path = NULL;

    g_return_if_fail (GAS_IS_GROUP_MANAGER (manager));
    g_return_if_fail (name != NULL && name[0] != '\0');

    group = lookup_group_by_name (manager,name);

    if (group == NULL) 
	{
    	g_debug ("GasGroupManager: trying to track new gropu with name %s",name);
        group = create_new_group (manager);
    }

    user_group_admin_call_find_group_by_name_sync (priv->group_admin_proxy,
                                                   name,
                                                   &object_path,
                                                   NULL,
                                                   &error);

    _gas_group_update_from_object_path (group, object_path);
}

GasGroup * gas_group_manager_get_group_by_id (GasGroupManager *manager,
                                 			  gid_t           id)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GasGroup *group;
    gchar *object_path = NULL;

    g_return_val_if_fail (GAS_IS_GROUP_MANAGER (manager), NULL);

    object_path = g_strdup_printf ("/org/group/admin/Group%lu", (gulong) id);
    group = g_hash_table_lookup (priv->groups_by_object_path, object_path);

    if (group != NULL) 
	{
    	return g_object_ref (group);
    } 
	else 
	{
        group = create_new_group (manager);

        if (priv->group_admin_proxy != NULL)
	   	{
         	fetch_group_with_id_from_group_admin_service (manager,group, id);
        }
    }

    return group;
}

static void listify_hash_values_hfunc (gpointer key,
                                       gpointer value,
                                       gpointer user_data)
{
	GSList **list = user_data;
    *list = g_slist_prepend (*list, value);
}

GSList * gas_group_manager_list_groups (GasGroupManager *manager)
{
	GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GSList *retval = NULL;

    g_return_val_if_fail (GAS_IS_GROUP_MANAGER (manager), NULL);
    load_groups (manager);
	g_hash_table_foreach (priv->normal_groups_by_name,listify_hash_values_hfunc,&retval);

    return g_slist_sort (retval, (GCompareFunc) gas_group_collate);
}

static GSList * slist_deep_copy (const GSList *list)
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

static void load_groups (GasGroupManager *manager)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
   	GError *error = NULL;
    g_auto(GStrv) group_paths = NULL;
    gboolean could_list = FALSE;

    if (!ensure_group_admin_proxy (manager)) 
	{
		g_print("check group_admin_proxy fail !!!\r\n");
    	return;
    }
    could_list = user_group_admin_call_list_cached_groups_sync (priv->group_admin_proxy,
                                                                &group_paths,
                                                                NULL, &error);
    if (!could_list) 
	{
    	g_print ("GasGroupManager: ListCachedGroups failed: %s", error->message);
        return;
    }

    load_groups_paths (manager, (const char * const *) group_paths);
    load_included_groupnames (manager);
    priv->list_cached_groups_done = TRUE;
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
        case PROP_EXCLUDE_GROUPNAMES_LIST:
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
        g_slist_foreach (priv->include_groupnames, (GFunc) g_free_list_data, NULL);
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
        g_slist_foreach (priv->exclude_groupnames, (GFunc) g_free_list_data, NULL);
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
        case PROP_EXCLUDE_GROUPNAMES_LIST:
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
                                     g_param_spec_pointer ("include-groupnames-list",
                                                           "Include groupnames list",
                                                           "Groupnames who are specifically included",
                                                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (object_class,
                                     PROP_EXCLUDE_GROUPNAMES_LIST,
                                     g_param_spec_pointer ("exclude-groupames-list",
                                                           "Exclude groupnames list",
                                                           "Groupnames who are specifically excluded",
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
                              G_TYPE_NONE, 1, GAS_TYPE_GROUP);

    signals [GROUP_CHANGED] = g_signal_new ("group-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GasGroupManagerClass,group_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1, GAS_TYPE_GROUP);
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
        g_print("GasGroupManager: getting account proxy failed: %s", error->message);
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

    priv->groups_by_object_path = g_hash_table_new_full (g_str_hash,
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
        g_slist_foreach (priv->exclude_groupnames, (GFunc) g_free_list_data, NULL);
        g_slist_free (priv->exclude_groupnames);
    }

    if (priv->include_groupnames != NULL) 
    {
        g_slist_foreach (priv->include_groupnames, (GFunc) g_free_list_data, NULL);
        g_slist_free (priv->include_groupnames);
    }

    if (priv->group_admin_proxy != NULL) 
    {
        g_object_unref (priv->group_admin_proxy);
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
    }

    return GAS_GROUP_MANAGER (group_manager_object);
}

gboolean gas_group_manager_no_service (GasGroupManager *manager)
{
 	GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    return priv->group_admin_proxy == NULL;
}

GasGroup *gas_group_manager_create_group (GasGroupManager *manager,
                                          const char      *name,
                                          GError          **error)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GError *local_error = NULL;
    gboolean res;
    gchar *path = NULL;
    GasGroup *group;

    res = user_group_admin_call_create_group_sync (priv->group_admin_proxy,
                                                   name,
                                                   &path,
                                                   NULL,
                                                   &local_error);
    if (!res) {
            return NULL;
    }

    group = add_new_group_for_object_path (path, manager);

    return group;
}

static void gas_group_manager_async_complete_handler (GObject      *source,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
        GTask *task = user_data;

        g_task_return_pointer (task, g_object_ref (result), g_object_unref);
        g_object_unref (task);
}

void gas_group_manager_create_group_async (GasGroupManager     *manager,
                                           const char          *name,
                                           GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
	GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GTask *task;

    g_return_if_fail (GAS_IS_GROUP_MANAGER (manager));
    g_return_if_fail (priv->group_admin_proxy != NULL);


    task = g_task_new (G_OBJECT (manager),
                       cancellable,
                       callback, user_data);

    user_group_admin_call_create_group (priv->group_admin_proxy,
                                        name,
                                        cancellable,
                                        gas_group_manager_async_complete_handler, task);
}

GasGroup *gas_group_manager_create_group_finish (GasGroupManager  *manager,
                                     GAsyncResult    *result,
                                     GError         **error)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GAsyncResult *inner_result;
    GasGroup *group = NULL;
    gchar *path = NULL;
    GError *remote_error = NULL;

    inner_result = g_task_propagate_pointer (G_TASK (result), error);
    if (inner_result == NULL) 
	{
    	return FALSE;
    }

    if (user_group_admin_call_create_group_finish (priv->group_admin_proxy,
                                                   &path, inner_result, &remote_error)) 
	{
    	group = add_new_group_for_object_path (path, manager);
    }

    if (remote_error) 
	{
    	g_dbus_error_strip_remote_error (remote_error);
        g_propagate_error (error, remote_error);
    }

    return group;
}

gboolean gas_group_manager_delete_group (GasGroupManager  *manager,
                                         GasGroup         *group,
                                         GError         **error)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GError *local_error = NULL;
    
	g_return_val_if_fail (GAS_IS_GROUP_MANAGER (manager), FALSE);
    g_return_val_if_fail (GAS_IS_GROUP (group), FALSE);
    g_return_val_if_fail (priv->group_admin_proxy != NULL, FALSE);

    if (!user_group_admin_call_delete_group_sync (priv->group_admin_proxy,
                                                  gas_group_get_gid (group),
                                                  NULL,
                                                  &local_error)) 
	{
    	return FALSE;
    }

    return TRUE;
}
void gas_group_manager_delete_group_async (GasGroupManager     *manager,
                                    	   GasGroup            *group,
                                           GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GTask *task;

    g_return_if_fail (GAS_IS_GROUP_MANAGER (manager));
    g_return_if_fail (GAS_IS_GROUP (group));
    g_return_if_fail (priv->group_admin_proxy != NULL);

    task = g_task_new (G_OBJECT (manager),
                       cancellable,
                       callback, user_data);

    user_group_admin_call_delete_group (priv->group_admin_proxy,
                                        gas_group_get_gid (group),
                                        cancellable,
                                        gas_group_manager_async_complete_handler, task);
}

gboolean gas_group_manager_delete_group_finish (GasGroupManager  *manager,
                                                GAsyncResult    *result,
                                                GError         **error)
{
    GasGroupManagerPrivate *priv = gas_group_manager_get_instance_private (manager);
    GAsyncResult *inner_result;
    gboolean success;
    GError *remote_error = NULL;

    inner_result = g_task_propagate_pointer (G_TASK (result), error);
    if (inner_result == NULL) 
	{
    	return FALSE;
    }

    success = user_group_admin_call_delete_group_finish (priv->group_admin_proxy,
                                                         inner_result, &remote_error);
    if (remote_error) 
	{
    	g_dbus_error_strip_remote_error (remote_error);
        g_propagate_error (error, remote_error);
    }

    return success;
}
