/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2005 James M. Cape <jcape@ignore-your.tv>.
 * Copyright (C) 2007-2008 William Jon McCann <mccann@jhu.edu>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Facade object for user data, owned by ActUserManager
 */

#ifndef __ACT_USER_H__
#define __ACT_USER_H__

#include <sys/types.h>
#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define ACT_TYPE_USER (act_user_get_type ())
#define ACT_USER(object) (G_TYPE_CHECK_INSTANCE_CAST ((object), ACT_TYPE_USER, ActUser))
#define ACT_IS_USER(object) (G_TYPE_CHECK_INSTANCE_TYPE ((object), ACT_TYPE_USER))

typedef enum {
        ACT_USER_ACCOUNT_TYPE_STANDARD,
        ACT_USER_ACCOUNT_TYPE_ADMINISTRATOR,
} ActUserAccountType;

typedef enum {
        ACT_USER_PASSWORD_MODE_REGULAR,
        ACT_USER_PASSWORD_MODE_SET_AT_LOGIN,
        ACT_USER_PASSWORD_MODE_NONE,
} ActUserPasswordMode;

typedef struct _ActUser ActUser;
typedef struct _ActUserClass ActUserClass;

GType          act_user_get_type                  (void) G_GNUC_CONST;

const char    *act_user_get_object_path           (ActUser *user);

uid_t          act_user_get_uid                   (ActUser   *user);
const char    *act_user_get_user_name             (ActUser   *user);
const char    *act_user_get_real_name             (ActUser   *user);
ActUserAccountType act_user_get_account_type      (ActUser   *user);
ActUserPasswordMode act_user_get_password_mode    (ActUser   *user);
const char    *act_user_get_password_hint         (ActUser   *user);
const char    *act_user_get_home_dir              (ActUser   *user);
const char    *act_user_get_shell                 (ActUser   *user);
const char    *act_user_get_email                 (ActUser   *user);
const char    *act_user_get_location              (ActUser   *user);
guint          act_user_get_num_sessions          (ActUser   *user);
guint          act_user_get_num_sessions_anywhere (ActUser   *user);
gboolean       act_user_is_logged_in              (ActUser   *user);
gboolean       act_user_is_logged_in_anywhere     (ActUser   *user);
int            act_user_get_login_frequency       (ActUser   *user);
gint64         act_user_get_login_time            (ActUser   *user);
const GVariant*act_user_get_login_history         (ActUser   *user);
gboolean       act_user_get_saved                 (ActUser   *user);
gboolean       act_user_get_locked                (ActUser   *user);
gboolean       act_user_get_automatic_login       (ActUser   *user);
gboolean       act_user_is_system_account         (ActUser   *user);
gboolean       act_user_is_local_account          (ActUser   *user);
gboolean       act_user_is_nonexistent            (ActUser   *user);
const char    *act_user_get_icon_file             (ActUser   *user);
const char    *act_user_get_language              (ActUser   *user);
const char    *act_user_get_x_session             (ActUser   *user);
const char    *act_user_get_session               (ActUser   *user);
const char    *act_user_get_session_type          (ActUser   *user);
const char    *act_user_get_primary_session_id    (ActUser   *user);

gint           act_user_collate                   (ActUser   *user1,
                                                   ActUser   *user2);
gboolean       act_user_is_loaded                 (ActUser   *user);

void           act_user_get_password_expiration_policy (ActUser   *user,
                                                        gint64    *expiration_time,
                                                        gint64    *last_change_time,
                                                        gint64    *min_days_between_changes,
                                                        gint64    *max_days_between_changes,
                                                        gint64    *days_to_warn,
                                                        gint64    *days_after_expiration_until_lock);

void           act_user_set_email                 (ActUser    *user,
                                                   const char *email);
void           act_user_set_language              (ActUser    *user,
                                                   const char *language);
void           act_user_set_x_session             (ActUser    *user,
                                                   const char *x_session);
void           act_user_set_session               (ActUser    *user,
                                                   const char *session);
void           act_user_set_session_type          (ActUser    *user,
                                                   const char *session_type);
void           act_user_set_location              (ActUser    *user,
                                                   const char *location);
void           act_user_set_user_name             (ActUser    *user,
                                                   const char  *user_name);
void           act_user_set_real_name             (ActUser    *user,
                                                   const char *real_name);
void           act_user_set_icon_file             (ActUser    *user,
                                                   const char *icon_file);
void           act_user_set_account_type          (ActUser    *user,
                                                   ActUserAccountType account_type);
void           act_user_set_password              (ActUser     *user,
                                                   const gchar *password,
                                                   const gchar *hint);
void           act_user_set_password_hint         (ActUser             *user,
                                                   const gchar *hint);
void           act_user_set_password_mode         (ActUser             *user,
                                                   ActUserPasswordMode  password_mode);
void           act_user_set_locked                (ActUser    *user,
                                                   gboolean    locked);
void           act_user_set_automatic_login       (ActUser   *user,
                                                   gboolean  enabled);

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ActUser, g_object_unref)
#endif

G_END_DECLS

#endif /* __ACT_USER_H__ */
