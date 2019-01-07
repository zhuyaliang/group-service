/*  group-service 
*   Copyright (C) 2018  zhuyaliang https://github.com/zhuyaliang/
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <config.h>

#include <float.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "gas-group-private.h"
#include "group-list-generated.h"

#define GAS_GROUP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GAS_TYPE_GROUP, GasGroupClass))
#define GAS_IS_GROUP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GAS_TYPE_GROUP))
#define GAS_GROUP_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS ((object), GAS_TYPE_GROUP, GasGRoupClass))

#define GROUP_NAME           "org.group.admin"
#define GROUP_LSIT_INTERFACE "org.group.admin.list"

enum 
{
    PROP_0,
    PROP_GID,
    PROP_GROUP_NAME,
    PROP_LOCAL_GROUP,
    PROP_IS_LOADED
};

enum 
{
    CHANGED,
    LAST_SIGNAL
};

struct _GasGroup 
{
    GObject         parent;
    GDBusConnection *connection;
    UserGroupList   *group_proxy;
    guint           is_loaded : 1;
};

struct _GasGroupClass
{
    GObjectClass parent_class;
};

static void gas_group_finalize     (GObject      *object);

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GasGroup, gas_group, G_TYPE_OBJECT)

static void gas_group_get_property (GObject    *object,
                                    guint       param_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    GasGroup *group;
    const char *property_name;
    group = GAS_GROUP (object);

    switch (param_id) 
    {
        case PROP_IS_LOADED:
            g_value_set_boolean (value, group->is_loaded);
            break;
        default:
            if (group->group_proxy != NULL) 
            {
                property_name = g_param_spec_get_name (pspec);
                g_object_get_property (G_OBJECT (group->group_proxy), property_name, value);
            }
            break;
    }
}

static void gas_group_class_init (GasGroupClass *class)
{
    GObjectClass *gobject_class;
    gobject_class = G_OBJECT_CLASS (class);

    gobject_class->finalize = gas_group_finalize;
    gobject_class->get_property = gas_group_get_property;

    g_object_class_install_property (gobject_class,
                                     PROP_GROUP_NAME,
                                     g_param_spec_string ("group-name",
                                                          "Group Name",
                                                          "The group name ",
                                                          NULL,
                                                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class,
                                     PROP_GID,
                                     g_param_spec_int ("gid",
                                                       "Group ID",
                                                       "The GID for this group.",
                                                       0, G_MAXINT, 0,
                                                       G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (gobject_class,
                                     PROP_IS_LOADED,
                                     g_param_spec_boolean ("is-loaded",
                                                           "Is loaded",
                                                           "Determines whether or not the group object is loaded and ready to read from.",
                                                           FALSE,
                                                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (gobject_class,
                                     PROP_LOCAL_GROUP,
                                     g_param_spec_boolean ("local-group",
                                                           "Local Group",
                                                           "Local Group",
                                                           FALSE,
                                                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

    signals [CHANGED] =
            g_signal_new ("changed",
                          G_TYPE_FROM_CLASS (class),
                          G_SIGNAL_RUN_LAST,
                          0,
                          NULL, NULL,
                          g_cclosure_marshal_VOID__VOID,
						  G_TYPE_NONE, 0);
}

static void gas_group_init (GasGroup *group)
{
    GError *error = NULL;

    group->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (group->connection == NULL) 
    {
    	g_warning ("Couldn't connect to system bus: %s", error->message);
    }
}

static void gas_group_finalize (GObject *object)
{
    GasGroup *group;
    group = GAS_GROUP (object);

    if (group->group_proxy != NULL) 
    {
    	g_object_unref (group->group_proxy);
    }

    if (group->connection != NULL) 
    {
        g_object_unref (group->connection);
    }

}

static void set_is_loaded (GasGroup *group,gboolean is_loaded)
{
    if (group->is_loaded != is_loaded) 
    {
        group->is_loaded = is_loaded;
        g_object_notify (G_OBJECT (group), "is-loaded");
    }
}

int gas_group_collate (GasGroup *group1,GasGroup *group2)
{
    const char *str1;
    const char *str2;

    g_return_val_if_fail (GAS_IS_GROUP (group1), 0);
    g_return_val_if_fail (GAS_IS_GROUP (group2), 0);

    str1 = gas_group_get_group_name (group1);
    str2 = gas_group_get_group_name (group2);

    if (str1 == NULL && str2 != NULL) 
    {
        return -1;
    }

    if (str1 != NULL && str2 == NULL) 
    {
        return 1;
    }

    if (str1 == NULL && str2 == NULL) 
    {
        return 0;
    }

    return g_utf8_collate (str1, str2);
}

char const **gas_group_get_group_users (GasGroup *group)
{
    g_return_val_if_fail (GAS_IS_GROUP (group), NULL);

    if (group->group_proxy == NULL)
        return NULL;
    return user_group_list_get_users(group->group_proxy);
}    
gboolean gas_group_is_local_group(GasGroup *group)
{
    g_return_val_if_fail (GAS_IS_GROUP (group), FALSE);

    if (group->group_proxy == NULL)
    {			
        return FALSE;
    }
    return user_group_list_get_local_group(group->group_proxy);

}		
const char * gas_group_get_group_name (GasGroup *group)
{
    g_return_val_if_fail (GAS_IS_GROUP (group), NULL);

    if (group->group_proxy == NULL)
        return NULL;

    return user_group_list_get_group_name(group->group_proxy);
}
gboolean gas_group_user_is_group (GasGroup *group,const char *user)
{
    char const **users;
    int i = 0;
    g_return_val_if_fail (GAS_IS_GROUP (group), FALSE);
    g_return_val_if_fail (user != NULL,FALSE);
    g_return_val_if_fail (getpwnam(user) != NULL,FALSE);
    g_return_val_if_fail (USER_GROUP_IS_LIST (group->group_proxy),FALSE);
	users = gas_group_get_group_users(group);
    while(users[i] != NULL)
    {
        if(g_strcmp0(users[i],user) == 0)
            return TRUE;
        i++;
    }    
    return FALSE;
}    

const char * gas_group_get_object_path (GasGroup *group)
{
    g_return_val_if_fail (GAS_IS_GROUP (group), NULL);

    if (group->group_proxy == NULL)
        return NULL;

    return g_dbus_proxy_get_object_path (G_DBUS_PROXY (group->group_proxy));
}

gid_t gas_group_get_gid (GasGroup *group)
{
    g_return_val_if_fail (GAS_IS_GROUP (group), -1);
    
    if (group->group_proxy == NULL)
        return -1;

    return user_group_list_get_gid(group->group_proxy);
}

static void on_group_proxy_changed (GasGroup *group)
{
    g_signal_emit (group, signals[CHANGED], 0);
}

void _gas_group_update_from_object_path (GasGroup *group,
                                         const char *object_path)
{
    UserGroupList *group_proxy;
    GError *error = NULL;

    g_return_if_fail (GAS_IS_GROUP (group));
    g_return_if_fail (object_path != NULL);
    g_return_if_fail (gas_group_get_object_path (group) == NULL);

    group_proxy = user_group_list_proxy_new_sync (group->connection,
                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                  GROUP_NAME,
                                                  object_path,
                                                  NULL,
                                                  &error);
    if (!group_proxy) 
    {
        g_print ("Couldn't create group-admin proxy: %s", error->message);
        return;
    }

    group->group_proxy = group_proxy;
    g_signal_connect_object (group->group_proxy,
                            "changed",
                             G_CALLBACK (on_group_proxy_changed),
                             group,
                             G_CONNECT_SWAPPED);

    g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (group->group_proxy), INT_MAX);
    set_is_loaded (group, TRUE);
}

gboolean gas_group_is_loaded (GasGroup *group)
{
    return group->is_loaded;
}

void gas_group_set_group_name (GasGroup *group,const char *name)
{
    GError *error = NULL;

    g_return_if_fail (GAS_IS_GROUP (group));
    g_return_if_fail (name != NULL);
    g_return_if_fail (USER_GROUP_IS_LIST (group->group_proxy));

    if (!user_group_list_call_change_group_name_sync (group->group_proxy,
                                                      name,
                                                      NULL,
                                                      &error)) 
    {
        g_warning ("set_group_name call failed: %s", error->message);
        return;
    }
}
void gas_group_set_group_id (GasGroup *group,uint gid)
{
    GError *error = NULL;

    g_return_if_fail (GAS_IS_GROUP (group));
    g_return_if_fail (USER_GROUP_IS_LIST (group->group_proxy));

    if (!user_group_list_call_change_group_id_sync (group->group_proxy,
                                                      gid,
                                                      NULL,
                                                      &error)) 
    {
        g_warning ("set_group_id call failed: %s", error->message);
        return;
    }
}


void gas_group_add_user_group (GasGroup *group,const char *name)
{
    GError  *error = NULL;
    g_return_if_fail (GAS_IS_GROUP (group));
    g_return_if_fail (name != NULL);
    g_return_if_fail (USER_GROUP_IS_LIST (group->group_proxy));
    g_return_if_fail (getpwnam (name) != NULL);

    if (!user_group_list_call_add_user_to_group_sync (group->group_proxy,
                                                      name,
                                                      NULL,
                                                      &error)) 
    {
        g_warning ("add user to group call failed: %s", error->message);
        return;
    }
}
void gas_group_remove_user_group (GasGroup *group,const char *name)
{
    GError  *error = NULL;
    g_return_if_fail (GAS_IS_GROUP (group));
    g_return_if_fail (name != NULL);
    g_return_if_fail (USER_GROUP_IS_LIST (group->group_proxy));
    g_return_if_fail (getpwnam (name) != NULL);

    if (!user_group_list_call_remove_user_from_group_sync(group->group_proxy,
                                                          name,
                                                          NULL,
                                                          &error)) 
    {
        g_print ("remove user from group call failed: %s", error->message);
        return;
    }
}
