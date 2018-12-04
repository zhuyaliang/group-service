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

gboolean       gas_group_user_is_group             (GasGroup   *Group,
                                                    const char *user);

char const **  gas_group_get_group_users           (GasGroup   *Group);

gint           gas_group_collate                   (GasGroup   *Group1,
                                                    GasGroup   *Group2);
gboolean       gas_group_is_loaded                   (GasGroup   *group);

void           gas_group_set_group_name            (GasGroup   *group,
                                                    const char *name);
void           gas_group_remove_user_group         (GasGroup   *group,
                                                    const char *user);
void           gas_group_add_user_group            (GasGroup   *group,
                                                   const char  *user);

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GasGroup, g_object_unref)
#endif

G_END_DECLS

#endif 
