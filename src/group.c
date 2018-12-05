#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <grp.h>
#ifdef HAVE_SHADOW_H
#include <shadow.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include "group.h"
enum {
        PROP_0,
        PROP_GID,
        PROP_GROUP_NAME,
        PROP_LOCAL_GROUP,
        PROP_USERS,
};

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
void
group_update_from_grent (Group        *group,
                         struct group *grent)
{
        g_object_freeze_notify (G_OBJECT (group));

        if (grent->gr_gid != group->gid) {
                group->gid = grent->gr_gid;
                g_object_notify (G_OBJECT (group), "gid");
        }

        if (g_strcmp0 (group->group_name, grent->gr_name) != 0) {
                g_free (group->group_name);
                group->group_name = g_strdup (grent->gr_name);
                g_object_notify (G_OBJECT (group), "group-name");
        }

        g_object_thaw_notify (G_OBJECT (group));
        user_group_list_set_local_group(USER_GROUP_LIST(group),TRUE);
   		user_group_list_set_gid(USER_GROUP_LIST(group),group->gid);
		user_group_list_set_group_name(USER_GROUP_LIST(group),group->group_name);
}
static gchar *
compute_object_path (Group *group)
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
Group * group_new (gid_t   gid)
{
    Group *group;

    group = g_object_new (TYPE_GROUP, NULL);
   	user_group_list_set_gid(USER_GROUP_LIST(group),gid);
	group->object_path = compute_object_path (group);
    return group;
}
static gboolean AddUserToGroup (UserGroupList *object,
                                GDBusMethodInvocation *invocation,
                                const gchar *arg_name)
{
    return TRUE;
}    
static gboolean ChangeGroupName (UserGroupList *object,
                                GDBusMethodInvocation *invocation,
                                const gchar *arg_name)
{
    return TRUE;
}    
static gboolean RemoveUserFromGroup (UserGroupList *object,
                                     GDBusMethodInvocation *invocation,
                                     const gchar *arg_name)
{
    return TRUE;
}    
static void user_group_list_iface_init (UserGroupListIface *iface)
{
    iface->handle_add_user_to_group =      AddUserToGroup;
    iface->handle_change_group_name =      ChangeGroupName;
    iface->handle_remove_user_from_group = RemoveUserFromGroup;
}


