#define __GAS_GROUP_MANAGER_H__

#include <glib-object.h>
#include <gio/gio.h>

#include "gas-group.h"

G_BEGIN_DECLS

#define GAS_TYPE_GROUP_MANAGER         (gas_group_manager_get_type ())
#define GAS_GROUP_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GAS_TYPE_USER_MANAGER, GasGroupManager))
#define GAS_GROUP_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GAS_TYPE_GROUP_MANAGER, GasGroupManagerClass))
#define GAS_IS_GROUP_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GAS_TYPE_GROUP_MANAGER))
#define GAS_IS_GROUP_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GAS_TYPE_GROUP_MANAGER))
#define GAS_GROUP_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GAS_TYPE_GROUP_MANAGER, GasGroupManagerClass))

typedef struct _GasGroupManager GasGroupManager;
typedef struct _GasGroupManagerClass GasGroupManagerClass;

struct _GasGroupManager
{
        GObject  parent;

        /*< private >*/
        gpointer deprecated;
};

struct _GasGroupManagerClass
{
        GObjectClass   parent_class;

        void          (* group_added)                (GasGroupManager *GroupManager,
                                                      GasGroup        *group);

        void          (* group_removed)              (GasGroupManager *GroupManager,
                                                      GasGroup        *group);
        void          (* group_is_logged_in_changed) (GasGroupManager *GroupManager,
                                                      GasGroup        *group);
        void          (* group_changed)              (GasUserManager  *GropuManager,
                                                      GasGroup        *group);
};

GType                gas_group_manager_get_type              (void);

GasGroupManager *    gas_group_manager_get_default           (void);

gboolean             gas_group_manager_no_service            (GasGroupManager *manager);

GSList *             gas_group_manager_list_users            (GasGroupManager *manager);

GasGroup *           gas_group_manager_get_group             (GasGroupManager *manager,
                                                              const char      *name);
GasGroup *           gas_group_manager_get_group_by_id       (GasGroupManager *manager,
                                                              uid_t           id);

GasGroup *           gas_group_manager_create_group          (GasGroupManager *manager,
                                                              const char         *name,
                                                              GError             **error);

void                 gas_group_manager_create_group_async     (GasGroupManager *manager,
                                                              const gchar        *name
                                                              GCancellable       *cancellable,
                                                              GAsyncReadyCallback callback,
                                                              gpointer            data);

GasGroup *           gas_group_manager_create_group_finish    (GasGroupManager *manager,
                                                               GAsyncResult    *result,
                                                               GError          **error);

gboolean             gas_group_manager_delete_group           (GasGroupManager *manager,
                                                               GasGRoup        *group,
                                                               GError          **error);

void                 gas_group_manager_delete_group_async     (GasGroupManager *manager,
                                                               GasGroup        *group,
                                                               GCancellable       *cancellable,
                                                               GAsyncReadyCallback callback,
                                                               gpointer            data);

gboolean             gas_group_manager_delete_group_finish    (GasGroupManager *manager,
                                                               GAsyncResult    *result,
                                                               GError          **error);

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GasGroupManager, g_object_unref)
#endif

G_END_DECLS

#endif
