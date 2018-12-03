#ifndef __GROUP_SERVER__
#define __GROUP_SERVER__

#include "group.h"
G_BEGIN_DECLS

#define TYPE_MANAGE         (manage_get_type ())
#define MANAGE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TYPE_MANAGE, Manage))
#define MANAGE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), TYPE_MANAGE, ManageClass))
#define IS_MANAGE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TYPE_MANAGE))
#define IS_MANAGE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TYPE_MANAGE))
#define MANAGE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TYPE_MANAGE, ManageClass))
#define MANAGE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TYPE_MANAGE, ManagePrivate))
typedef struct ManageClass   ManageClass;
typedef struct Manage        Manage;
typedef struct ManagePrivate ManagePrivate;
struct Manage {
        groupAdminSkeleton parent;
		ManagePrivate *priv;
};

struct ManageClass {
        groupAdminSkeletonClass parent_class;
};

typedef enum {
        ERROR_FAILED,
        ERROR_USER_EXISTS,
        ERROR_USER_DOES_NOT_EXIST,
        ERROR_PERMISSION_DENIED,
        ERROR_NOT_SUPPORTED,
        NUM_ERRORS
} Error;

#define ERROR error_quark ()

GType error_get_type (void);
#define TYPE_ERROR (error_get_type ())
GQuark error_quark (void);

GType   manage_get_type              (void) G_GNUC_CONST;
Manage *manage_new                   (void);
int RegisterGroupManage (Manage *manage);


typedef struct GroupManage
{
    GDBusConnection *BusConnection;
    GHashTable      *GroupsHashTable;
    GFileMonitor    *PasswdMonitor;
    GFileMonitor    *ShadowMonitor;
    GFileMonitor    *GroupMonitor;
    guint            ReloadId;
}GroupManage;

void StartLoadGroup(GroupManage *GM);

#endif
