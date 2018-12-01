#ifndef __GROUP__
#define __GROUP__

#include <sys/types.h>
#include <grp.h>

#include <glib.h>
#include <gio/gio.h>

#include "user-group-generated.h"

G_BEGIN_DECLS

#define TYPE_GROUP (group_get_type ())
#define GROUP(object) (G_TYPE_CHECK_INSTANCE_CAST ((object), TYPE_GROUP, Group))
#define IS_GROUP(object) (G_TYPE_CHECK_INSTANCE_TYPE ((object), TYPE_GROUP))

typedef struct Group 
{
	userGroupSkeleton parent;

    gchar *object_path;
    gid_t         gid;
    gchar        *group_name;
    gboolean      local_group;
	GStrv         users;
}Group;
typedef struct GroupClass
{
        userGroupSkeletonClass  parent_class;
} GroupClass;

static userGroup *skeleton=NULL;
GType          group_get_type                (void) G_GNUC_CONST;
Group *        group_new                     (gid_t           gid);
void           group_update_from_grent       (Group          *group,
                                              struct group   *grent,
                                              GHashTable     *users);

void           group_changed                 (Group          *group);

const gchar *  group_get_object_path         (Group          *group);
gid_t          group_get_gid                 (Group          *group);
const gchar *  group_get_group_name          (Group          *group);
gboolean       group_get_local_group         (Group          *group);
GStrv          group_get_users               (Group          *group);

G_END_DECLS

#endif
