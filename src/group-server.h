#ifndef __GROUP_SERVER__
#define __GROUP_SERVER__

#include "group.h"
#include "types.h"
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
    UserGroupAdminSkeleton parent;
	ManagePrivate *priv;
};

struct ManageClass {
    UserGroupAdminSkeletonClass parent_class;
};

GType   manage_get_type              (void) G_GNUC_CONST;
Manage *manage_new                   (void);
typedef void (*AuthorizedCallback)   (Manage                *,
                                      Group                 *,
                                      GDBusMethodInvocation *,
                                      gpointer              );

int RegisterGroupManage (Manage *manage);
void ManageLoadGroup(Manage *manage);
void LocalCheckAuthorization(Manage                *manage,
                             Group                 *group,
                             const gchar           *ActionFile,
                             gboolean               AllowInteraction,
                             AuthorizedCallback     Authorized_cb,
                             GDBusMethodInvocation *Invocation,
                             gpointer               Authorized_cb_data,
                             GDestroyNotify         DestroyNotify);
#endif
