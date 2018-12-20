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
typedef enum 
{
    ERROR_FAILED,
    ERROR_GROUP_EXISTS,
    ERROR_GROUP_DOES_NOT_EXIST,
    ERROR_PERMISSION_DENIED,
    ERROR_NOT_SUPPORTED,
    NUM_ERRORS
} Error;
#define ERROR error_quark ()
GQuark error_quark (void);

GType   manage_get_type              (void) G_GNUC_CONST;
Manage *manage_new                   (void);
typedef void (*AuthorizedCallback)   (Manage                *,
                                      Group                 *,
                                      GDBusMethodInvocation *,
                                      gpointer              );

void    DbusPrintf (GDBusMethodInvocation *Invocation,
                    gint                   ErrorCode,
                    const gchar           *format,
                 ...);
int     RegisterGroupManage (Manage *manage);
void    ManageLoadGroup(Manage *manage);
void    LocalCheckAuthorization(Manage                *manage,
                                Group                 *group,
                                const gchar           *ActionFile,
                                gboolean               AllowInteraction,
                                AuthorizedCallback     Authorized_cb,
                                GDBusMethodInvocation *Invocation,
                                gpointer               Authorized_cb_data,
                                GDestroyNotify         DestroyNotify);
#endif
