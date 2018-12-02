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

static void group_class_init (GroupClass *class);
static void group_init (Group *group);

GType group_get_type(void)
{
    static GType group_type = 0;
    if(!group_type)
    {
        static const GTypeInfo group_info = {
            sizeof(GroupClass),
            NULL,NULL,
            (GClassInitFunc)group_class_init,
            NULL,NULL,
            sizeof(Group),
            0,
            (GInstanceInitFunc)group_init
        };
        group_type = g_type_register_static(USER_TYPE_GROUP_SKELETON,"Group",&group_info,0);
    }
    return group_type;
}
const gchar *group_get_group_name (Group *group)
{
	return user_group_get_group_name(USER_GROUP(group)); 
}		
void
group_update_from_grent (Group        *group,
                         struct group *grent)
{
        gboolean changed = FALSE;
        GStrv new_members;

        g_object_freeze_notify (G_OBJECT (group));

        if (grent->gr_gid != group->gid) {
                group->gid = grent->gr_gid;
                changed = TRUE;
                g_object_notify (G_OBJECT (group), "gid");
        }

        if (g_strcmp0 (group->group_name, grent->gr_name) != 0) {
                g_free (group->group_name);
                group->group_name = g_strdup (grent->gr_name);
                changed = TRUE;
                g_object_notify (G_OBJECT (group), "group-name");
        }

        g_object_thaw_notify (G_OBJECT (group));
		user_group_set_group_name(USER_GROUP(group),group->group_name);
}
static gchar *
compute_object_path (Group *group)
{
	gchar *object_path;

    object_path = g_strdup_printf ("/org/freedesktop/Accounts/Group%ld",
                                       (long) group->gid);
}
static void
group_finalize (GObject *object)
{
    Group *group;

    group = GROUP (object);

    g_free (group->object_path);
    g_free (group->group_name);
}

static void
group_class_init (GroupClass *class)
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
Group *
group_new (gid_t   gid)
{
    Group *group;

    group = g_object_new (TYPE_GROUP, NULL);
   	user_group_set_gid(USER_GROUP(group),gid); 
	group->object_path = compute_object_path (group);
    return group;
}


