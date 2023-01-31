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
#ifndef __GAS_GROUP_H__
#define __GAS_GROUP_H__

#include <sys/types.h>
#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GAS_TYPE_GROUP       (gas_group_get_type ())
#define GAS_GROUP(object)    (G_TYPE_CHECK_INSTANCE_CAST ((object), GAS_TYPE_GROUP, GasGroup))
#define GAS_IS_GROUP(object) (G_TYPE_CHECK_INSTANCE_TYPE ((object), GAS_TYPE_GROUP))

typedef struct _GasGroup      GasGroup;
typedef struct _GasGroupClass GasGroupClass;

GType          gas_group_get_type                  (void) G_GNUC_CONST;

const char    *gas_group_get_object_path           (GasGroup   *Group);

gid_t          gas_group_get_gid                   (GasGroup   *Group);

const char    *gas_group_get_group_name            (GasGroup   *Group);

gboolean       gas_group_is_local_group            (GasGroup   *Group);

gboolean       gas_group_is_primary_group          (GasGroup   *Group);

gboolean       gas_group_user_is_group             (GasGroup   *Group,
                                                    const char *user);

char const **  gas_group_get_group_users           (GasGroup   *Group);

gint           gas_group_collate                   (GasGroup   *Group1,
                                                    GasGroup   *Group2);

gboolean       gas_group_is_loaded                 (GasGroup   *group);

void           gas_group_set_group_name            (GasGroup   *group,
                                                    const char *name);

void           gas_group_set_group_id              (GasGroup   *group,
                                                    uint        gid);

void           gas_group_remove_user_group         (GasGroup   *group,
                                                    const char *user);

void           gas_group_add_user_group            (GasGroup   *group,
                                                    const char *user);

void          _gas_group_load_from_group           (GasGroup   *group,
                                                    GasGroup   *group_to_copy);
#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GasGroup, g_object_unref)
#endif

G_END_DECLS

#endif
