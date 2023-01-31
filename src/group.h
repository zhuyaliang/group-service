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
#ifndef __GROUP__
#define __GROUP__

#include <sys/types.h>
#include <grp.h>
#include <glib.h>
#include <gio/gio.h>
#include "group-generated.h"
#include "group-list-generated.h"
#include "util.h"
#include "types.h"
G_BEGIN_DECLS

#define TYPE_GROUP       (group_get_type ())
#define GROUP(object)    (G_TYPE_CHECK_INSTANCE_CAST ((object), TYPE_GROUP, Group))
#define IS_GROUP(object) (G_TYPE_CHECK_INSTANCE_TYPE ((object), TYPE_GROUP))

typedef struct Group
{
    UserGroupListSkeleton parent;

    Manage       *manage;
    gchar        *object_path;
    gid_t         gid;
    GDBusConnection *system_bus_connection;
    gchar        *group_name;
    gboolean      local_group;
    GStrv         users;
    guint         changed_timeout_id;
} Group;

typedef struct GroupClass
{
    UserGroupListSkeletonClass  parent_class;
} GroupClass;

GType          group_get_type                (void) G_GNUC_CONST;
Group *        group_new                     (Manage         *manage,
                                              gid_t           gid);
void           group_update_from_grent       (Group          *group,
                                              struct group   *grent);

void           RegisterGroup                 (Manage         *manage,
                                              Group          *group);

void           UnRegisterGroup               (Manage         *manage,
                                              Group          *group);

void           group_changed                 (Group          *group);

const gchar *  group_get_object_path         (Group          *group);
gid_t          group_get_gid                 (Group          *group);
const gchar *  group_get_group_name          (Group          *group);
gboolean       group_get_local_group         (Group          *group);
GStrv          group_get_users               (Group          *group);
gboolean       is_user_in_group              (Group          *group,
                                              const char      *user);
gchar       *  compute_object_path           (Group          *group);
G_END_DECLS

#endif
