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
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <grp.h>
#ifdef HAVE_SHADOW_H
#include <shadow.h>
#endif
#include <pwd.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include "group.h"
#include "group-server.h"

static void user_group_list_iface_init (UserGroupListIface *iface);

G_DEFINE_TYPE_WITH_CODE (Group, group, USER_GROUP_TYPE_LIST_SKELETON,
                         G_IMPLEMENT_INTERFACE (USER_GROUP_TYPE_LIST, user_group_list_iface_init));

const gchar *group_get_group_name (Group *group)
{
    return user_group_list_get_group_name(USER_GROUP_LIST(group));
}

const gchar *group_get_object_path (Group *group)
{
    return group->object_path;
}

gid_t group_get_gid (Group *group)
{
    return user_group_list_get_gid(USER_GROUP_LIST(group));
}

gboolean group_get_local_group(Group *group)
{
    return user_group_list_get_local_group(USER_GROUP_LIST(group));
}

gboolean is_user_in_group(Group *group,const char *user)
{
    char const **users;
    int i = 0;
    struct group * grent;

    users = user_group_list_get_users(USER_GROUP_LIST(group));
    while(users[i] != NULL)
    {
        if(g_strcmp0(users[i],user) == 0)
            return TRUE;
        i++;
    }
    i = 0;
    grent = getgrnam(group_get_group_name(group));
    while (grent->gr_mem[i] != NULL)
    {
        if(g_strcmp0(grent->gr_mem[i],user) == 0)
            return TRUE;
        i++;
    }
    return FALSE;
}

void RegisterGroup (Manage *manage,Group *group)
{
    GError *error = NULL;
    GDBusConnection *Connection = NULL;
    g_autofree gchar *object_path = NULL;

    Connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (Connection == NULL)
    {
        if (error != NULL)
        {
            g_error ("error getting system bus: %s", error->message);
            g_error_free(error);
        }
        return;
    }

    object_path = compute_object_path (group);
    if(g_strcmp0(object_path,group_get_object_path(group)) != 0)
    {
        g_error("object_path = %s Non-existent",object_path);
        return;
    }
    group->system_bus_connection = Connection;
    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (group),
                                           Connection,
                                           object_path,
                                           &error))
    {
        if (error != NULL)
        {
            g_error ("error exporting group object: %s", error->message);
            g_error_free (error);
        }
        return;
    }
}

void UnRegisterGroup (Manage *manage,Group *group)
{
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (group));
}

void group_update_from_grent (Group        *group,
                              struct group *grent)
{
    g_object_freeze_notify (G_OBJECT (group));
    if (grent->gr_gid != group->gid)
    {
        group->gid = grent->gr_gid;
        g_object_notify (G_OBJECT (group), "gid");
    }

    if (g_strcmp0 (group->group_name, grent->gr_name) != 0)
    {
        g_free (group->group_name);
        group->group_name = g_strdup (grent->gr_name);
        g_object_notify (G_OBJECT (group), "group-name");
    }

    user_group_list_set_local_group(USER_GROUP_LIST(group),TRUE);
    user_group_list_set_gid(USER_GROUP_LIST(group),group->gid);
    user_group_list_set_group_name(USER_GROUP_LIST(group),group->group_name);
    user_group_list_set_users(USER_GROUP_LIST(group),
                             (const gchar *const *)grent->gr_mem);
    g_object_thaw_notify (G_OBJECT (group));
}

gchar * compute_object_path (Group *group)
{
    gchar *object_path;
    group->gid = (gulong) user_group_list_get_gid (USER_GROUP_LIST(group));
    object_path = g_strdup_printf ("/org/group/admin/Group%ld",
                                    (long) group->gid);
    return object_path;
}

static void group_finalize (GObject *object)
{
    Group *group;

    group = GROUP (object);

    g_free (group->object_path);
    g_free (group->group_name);
}

static void group_class_init (GroupClass *class)
{
    GObjectClass *gobject_class;

    gobject_class = G_OBJECT_CLASS (class);
    gobject_class->finalize = group_finalize;
}

static void group_init (Group *group)
{
    group->object_path = NULL;
    group->group_name = NULL;
    group->gid = -1;
}

Group * group_new (Manage *manage,gid_t gid)
{
    Group *group;

    group = g_object_new (TYPE_GROUP, NULL);
    group->manage = manage;
    user_group_list_set_gid(USER_GROUP_LIST(group),gid);
    group->object_path = compute_object_path (group);

    return group;
}

static void AddUserAuthorized_cb (Manage                *manage,
                                  Group                 *g,
                                  GDBusMethodInvocation *Invocation,
                                  gpointer               udata)
{
    gchar *name = udata;
    GError *error = NULL;
    const gchar *argv[6];
    struct group * grent;

    if(getpwnam (name) == NULL)
    {
        DbusPrintf(Invocation,ERROR_GROUP_DOES_NOT_EXIST,
                  "%s user does not exist",name);
        return;
    }
    if(!is_user_in_group(g,name))
    {
        sys_log (Invocation, "%s user '%s' %s group '%s'","add",
                name,"to",group_get_group_name (g));

        argv[0] = "/usr/sbin/groupmems";
        argv[1] = "-g";
        argv[2] = group_get_group_name (g);
        argv[3] = "-a";
        argv[4] = name;
        argv[5] = NULL;

        if (!spawn_with_login_uid (Invocation, argv, &error))
        {
            DbusPrintf(Invocation,ERROR_FAILED,
                       "running '%s' failed: %s", argv[0], error->message);
            g_error_free (error);
            return;
        }

        grent = getgrnam(group_get_group_name(g));
        user_group_list_set_users(USER_GROUP_LIST(g),
                                 (const gchar *const *)grent->gr_mem);
        user_group_list_emit_changed (USER_GROUP_LIST(g));
    }
    user_group_list_complete_add_user_to_group(USER_GROUP_LIST(g),Invocation);
}

static gboolean AddUserToGroup (UserGroupList *object,
                                GDBusMethodInvocation *Invocation,
                                const gchar *name)
{
    Group *group = (Group*) object;

    LocalCheckAuthorization (group->manage,
                             group,
                             "org.group.admin.group-administration",
                             TRUE,
                             AddUserAuthorized_cb,
                             Invocation,
                             g_strdup (name),
                             (GDestroyNotify)g_free);

    return TRUE;
}

static void ChangeNameAuthorized_cb (Manage                *manage,
                                     Group                 *g,
                                     GDBusMethodInvocation *Invocation,
                                     gpointer               udata)
{
    gchar *name = udata;
    GError *error = NULL;
    const gchar *argv[6];

    if (g_strcmp0 (group_get_group_name (g), name) != 0)
    {
        sys_log (Invocation, "changing name of group '%s' to '%s'",
                 group_get_group_name (g),name);

        argv[0] = "/usr/sbin/groupmod";
        argv[1] = "-n";
        argv[2] = name;
        argv[3] = "--";
        argv[4] = group_get_group_name (g);
        argv[5] = NULL;

        if (!spawn_with_login_uid (Invocation, argv, &error))
        {
            DbusPrintf(Invocation,ERROR_FAILED,
                       "running '%s' failed: %s", argv[0], error->message);
            g_error_free (error);
            return;
        }
        user_group_list_set_group_name(USER_GROUP_LIST(g),name);
        user_group_list_emit_changed (USER_GROUP_LIST(g));
    }
    user_group_list_complete_change_group_name(USER_GROUP_LIST(g),Invocation);
}

static gboolean ChangeGroupName (UserGroupList *object,
                                 GDBusMethodInvocation *Invocation,
                                 const gchar *name)
{
    Group *group = (Group*) object;

    LocalCheckAuthorization (group->manage,
                             group,
                             "org.group.admin.group-administration",
                             TRUE,
                             ChangeNameAuthorized_cb,
                             Invocation,
                             g_strdup (name),
                             (GDestroyNotify)g_free);

    return TRUE;
}

static void ChangeIdAuthorized_cb   (Manage                *manage,
                                     Group                 *g,
                                     GDBusMethodInvocation *Invocation,
                                     gpointer               udata)
{
    uint id = GPOINTER_TO_UINT (udata);
    GError *error = NULL;
    const gchar *Strid = g_strdup_printf("%u",id);
    const gchar *argv[6];

    if (group_get_gid (g) != id)
    {
        sys_log (Invocation, "changing id of group '%u' to '%u'",
                 group_get_gid (g),id);

        argv[0] = "/usr/sbin/groupmod";
        argv[1] = "-g";
        argv[2] = Strid;
        argv[3] = "--";
        argv[4] = group_get_group_name (g);
        argv[5] = NULL;
        if (!spawn_with_login_uid (Invocation, argv, &error))
        {
            DbusPrintf(Invocation,ERROR_FAILED,
                       "running '%s' failed: %s", argv[0], error->message);
            g_error_free (error);
            g_free((gpointer)Strid);
            return;
        }
        user_group_list_set_gid(USER_GROUP_LIST(g),id);
        user_group_list_emit_changed (USER_GROUP_LIST(g));
    }
    user_group_list_complete_change_group_id(USER_GROUP_LIST(g),Invocation);
}

static gboolean ChangeGroupId (UserGroupList *object,
                               GDBusMethodInvocation *Invocation,
                               guint64 id)
{
    Group *group = (Group*) object;

    LocalCheckAuthorization (group->manage,
                             group,
                             "org.group.admin.group-administration",
                             TRUE,
                             ChangeIdAuthorized_cb,
                             Invocation,
                             GUINT_TO_POINTER(id),
                             NULL);

    return TRUE;
}

static void RemoveUserAuthorized_cb (Manage                *manage,
                                     Group                 *g,
                                     GDBusMethodInvocation *Invocation,
                                     gpointer               udata)
{
    gchar *name = udata;
    GError *error = NULL;
    const gchar *argv[6];
    struct group * grent;

    if(getpwnam (name) == NULL)
    {
        DbusPrintf(Invocation,ERROR_GROUP_DOES_NOT_EXIST,
                   "%s user does not exist",name);
        return;
    }
    if(is_user_in_group(g,name))
    {
        sys_log (Invocation, "%s user '%s' %s group '%s'","remove",
                name,"from",group_get_group_name (g));

        argv[0] = "/usr/sbin/groupmems";
        argv[1] = "-g";
        argv[2] = group_get_group_name (g);
        argv[3] = "-d";
        argv[4] = name;
        argv[5] = NULL;

        if (!spawn_with_login_uid (Invocation, argv, &error))
        {
            DbusPrintf(Invocation,ERROR_FAILED,
                       "running '%s' failed: %s", argv[0], error->message);
            g_error_free (error);
            return;
        }

        grent = getgrnam(group_get_group_name(g));
        user_group_list_set_users(USER_GROUP_LIST(g),
                                 (const gchar *const *)grent->gr_mem);
        user_group_list_emit_changed (USER_GROUP_LIST(g));

    }
    user_group_list_complete_remove_user_from_group (USER_GROUP_LIST(g),Invocation);
}

static gboolean RemoveUserFromGroup (UserGroupList *object,
                                     GDBusMethodInvocation *Invocation,
                                     const gchar *name)
{

    Group *group = (Group*) object;

    LocalCheckAuthorization (group->manage,
                             group,
                             "org.group.admin.group-administration",
                             TRUE,
                             RemoveUserAuthorized_cb,
                             Invocation,
                             g_strdup (name),
                             (GDestroyNotify)g_free);

    return TRUE;
}

static void user_group_list_iface_init (UserGroupListIface *iface)
{
    iface->handle_add_user_to_group =      AddUserToGroup;
    iface->handle_change_group_name =      ChangeGroupName;
    iface->handle_change_group_id =        ChangeGroupId;
    iface->handle_remove_user_from_group = RemoveUserFromGroup;
}
