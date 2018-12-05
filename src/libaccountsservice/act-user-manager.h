/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifndef __ACT_USER_MANAGER_H__
#define __ACT_USER_MANAGER_H__

#include <glib-object.h>
#include <gio/gio.h>

#include "act-user.h"

G_BEGIN_DECLS

#define ACT_TYPE_USER_MANAGER         (act_user_manager_get_type ())
#define ACT_USER_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), ACT_TYPE_USER_MANAGER, ActUserManager))
#define ACT_USER_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), ACT_TYPE_USER_MANAGER, ActUserManagerClass))
#define ACT_IS_USER_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), ACT_TYPE_USER_MANAGER))
#define ACT_IS_USER_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), ACT_TYPE_USER_MANAGER))
#define ACT_USER_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), ACT_TYPE_USER_MANAGER, ActUserManagerClass))

typedef struct _ActUserManager ActUserManager;
typedef struct _ActUserManagerClass ActUserManagerClass;

struct _ActUserManager
{
        GObject  parent;

        /*< private >*/
        gpointer deprecated;
};

struct _ActUserManagerClass
{
        GObjectClass   parent_class;

        void          (* user_added)                (ActUserManager *user_manager,
                                                     ActUser        *user);
        void          (* user_removed)              (ActUserManager *user_manager,
                                                     ActUser        *user);
        void          (* user_is_logged_in_changed) (ActUserManager *user_manager,
                                                     ActUser        *user);
        void          (* user_changed)              (ActUserManager *user_manager,
                                                     ActUser        *user);
};

typedef enum ActUserManagerError
{
        ACT_USER_MANAGER_ERROR_FAILED,
        ACT_USER_MANAGER_ERROR_USER_EXISTS,
        ACT_USER_MANAGER_ERROR_USER_DOES_NOT_EXIST,
        ACT_USER_MANAGER_ERROR_PERMISSION_DENIED,
        ACT_USER_MANAGER_ERROR_NOT_SUPPORTED
} ActUserManagerError;

#define ACT_USER_MANAGER_ERROR act_user_manager_error_quark ()

GQuark              act_user_manager_error_quark           (void);
GType               act_user_manager_get_type              (void);

ActUserManager *    act_user_manager_get_default           (void);

gboolean            act_user_manager_no_service            (ActUserManager *manager);
GSList *            act_user_manager_list_users            (ActUserManager *manager);
ActUser *           act_user_manager_get_user              (ActUserManager *manager,
                                                            const char     *username);
ActUser *           act_user_manager_get_user_by_id        (ActUserManager *manager,
                                                            uid_t           id);

gboolean            act_user_manager_activate_user_session (ActUserManager *manager,
                                                            ActUser        *user);

gboolean            act_user_manager_can_switch            (ActUserManager *manager);

gboolean            act_user_manager_goto_login_session    (ActUserManager *manager);

ActUser *           act_user_manager_create_user           (ActUserManager     *manager,
                                                            const char         *username,
                                                            const char         *fullname,
                                                            ActUserAccountType  accounttype,
                                                            GError             **error);
void                act_user_manager_create_user_async     (ActUserManager     *manager,
                                                            const gchar        *username,
                                                            const gchar        *fullname,
                                                            ActUserAccountType  accounttype,
                                                            GCancellable       *cancellable,
                                                            GAsyncReadyCallback callback,
                                                            gpointer            user_data);
ActUser *           act_user_manager_create_user_finish    (ActUserManager     *manager,
                                                            GAsyncResult       *result,
                                                            GError            **error);

ActUser *           act_user_manager_cache_user            (ActUserManager     *manager,
                                                            const char         *username,
                                                            GError            **error);
void                act_user_manager_cache_user_async      (ActUserManager     *manager,
                                                            const gchar        *username,
                                                            GCancellable       *cancellable,
                                                            GAsyncReadyCallback callback,
                                                            gpointer            user_data);
ActUser *           act_user_manager_cache_user_finish     (ActUserManager     *manager,
                                                            GAsyncResult       *result,
                                                            GError            **error);

gboolean            act_user_manager_uncache_user          (ActUserManager     *manager,
                                                            const char         *username,
                                                            GError            **error);
void                act_user_manager_uncache_user_async    (ActUserManager     *manager,
                                                            const gchar        *username,
                                                            GCancellable       *cancellable,
                                                            GAsyncReadyCallback callback,
                                                            gpointer            user_data);
gboolean            act_user_manager_uncache_user_finish   (ActUserManager     *manager,
                                                            GAsyncResult       *result,
                                                            GError            **error);

gboolean            act_user_manager_delete_user           (ActUserManager     *manager,
                                                            ActUser            *user,
                                                            gboolean            remove_files,
                                                            GError             **error);
void                act_user_manager_delete_user_async     (ActUserManager     *manager,
                                                            ActUser            *user,
                                                            gboolean            remove_files,
                                                            GCancellable       *cancellable,
                                                            GAsyncReadyCallback callback,
                                                            gpointer            user_data);
gboolean            act_user_manager_delete_user_finish    (ActUserManager     *manager,
                                                            GAsyncResult       *result,
                                                            GError            **error);

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ActUserManager, g_object_unref)
#endif

G_END_DECLS

#endif /* __ACT_USER_MANAGER_H__ */
