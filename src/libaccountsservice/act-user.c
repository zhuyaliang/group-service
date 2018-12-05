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

#include <config.h>

#include <float.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <crypt.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "act-user-private.h"
#include "accounts-user-generated.h"

/**
 * SECTION:act-user
 * @title: ActUser
 * @short_description: information about a user account
 *
 * An ActUser object represents a user account on the system.
 */

/**
 * ActUser:
 *
 * Represents a user account on the system.
 */

/**
 * ActUserAccountType:
 * @ACT_USER_ACCOUNT_TYPE_STANDARD: Normal non-administrative user
 * @ACT_USER_ACCOUNT_TYPE_ADMINISTRATOR: Administrative user
 *
 * Type of user account
 */

/**
 * ActUserPasswordMode:
 * @ACT_USER_PASSWORD_MODE_REGULAR: Password set normally
 * @ACT_USER_PASSWORD_MODE_SET_AT_LOGIN: Password will be chosen at next login
 * @ACT_USER_PASSWORD_MODE_NONE: No password set
 *
 * Mode for setting the user's password.
 */

#define ACT_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), ACT_TYPE_USER, ActUserClass))
#define ACT_IS_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), ACT_TYPE_USER))
#define ACT_USER_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS ((object), ACT_TYPE_USER, ActUserClass))

#define ACCOUNTS_NAME           "org.freedesktop.Accounts"
#define ACCOUNTS_USER_INTERFACE "org.freedesktop.Accounts.User"

enum {
        PROP_0,
        PROP_UID,
        PROP_USER_NAME,
        PROP_REAL_NAME,
        PROP_ACCOUNT_TYPE,
        PROP_PASSWORD_MODE,
        PROP_PASSWORD_HINT,
        PROP_HOME_DIR,
        PROP_SHELL,
        PROP_EMAIL,
        PROP_LOCATION,
        PROP_LOCKED,
        PROP_AUTOMATIC_LOGIN,
        PROP_SYSTEM_ACCOUNT,
        PROP_NONEXISTENT,
        PROP_LOCAL_ACCOUNT,
        PROP_LOGIN_FREQUENCY,
        PROP_LOGIN_TIME,
        PROP_LOGIN_HISTORY,
        PROP_ICON_FILE,
        PROP_LANGUAGE,
        PROP_X_SESSION,
        PROP_IS_LOADED
};

enum {
        CHANGED,
        SESSIONS_CHANGED,
        LAST_SIGNAL
};

struct _ActUser {
        GObject         parent;

        GDBusConnection *connection;
        AccountsUser    *accounts_proxy;

        GList          *our_sessions;
        GList          *other_sessions;

        guint           is_loaded : 1;
        guint           nonexistent : 1;
};

struct _ActUserClass
{
        GObjectClass parent_class;
};

static void act_user_finalize     (GObject      *object);

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (ActUser, act_user, G_TYPE_OBJECT)

static int
session_compare (const char *a,
                 const char *b)
{
        if (a == NULL) {
                return 1;
        } else if (b == NULL) {
                return -1;
        }

        return strcmp (a, b);
}

void
_act_user_add_session (ActUser    *user,
                       const char *ssid,
                       gboolean    is_ours)
{
        GList *li;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (ssid != NULL);

        li = g_list_find_custom (user->our_sessions, ssid, (GCompareFunc)session_compare);
        if (li == NULL)
                li = g_list_find_custom (user->other_sessions, ssid, (GCompareFunc)session_compare);

        if (li == NULL) {
                g_debug ("ActUser: adding session %s", ssid);
                if (is_ours)
                        user->our_sessions = g_list_prepend (user->our_sessions, g_strdup (ssid));
                else
                        user->other_sessions = g_list_prepend (user->other_sessions, g_strdup (ssid));
                g_signal_emit (user, signals[SESSIONS_CHANGED], 0);
        } else {
                g_debug ("ActUser: session already present: %s", ssid);
        }
}

void
_act_user_remove_session (ActUser    *user,
                          const char *ssid)
{
        GList *li, **headp;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (ssid != NULL);

        headp = &(user->our_sessions);
        li = g_list_find_custom (user->our_sessions, ssid, (GCompareFunc)session_compare);
        if (li == NULL) {
                headp = &(user->other_sessions);
                li = g_list_find_custom (user->other_sessions, ssid, (GCompareFunc)session_compare);
        }

        if (li != NULL) {
                g_debug ("ActUser: removing session %s", ssid);
                g_free (li->data);
                *headp = g_list_delete_link (*headp, li);
                g_signal_emit (user, signals[SESSIONS_CHANGED], 0);
        } else {
                g_debug ("ActUser: session not found: %s", ssid);
        }
}

/**
 * act_user_get_num_sessions:
 * @user: a user
 *
 * Get the number of sessions for a user that are graphical and on the
 * same seat as the session of the calling process.
 *
 * Returns: the number of sessions
 */
guint
act_user_get_num_sessions (ActUser    *user)
{
        return g_list_length (user->our_sessions);
}

/**
 * act_user_get_num_sessions_anywhere:
 * @user: a user
 *
 * Get the number of sessions for a user on any seat of any type.
 * See also act_user_get_num_sessions().
 *
 * (Currently, this function is only implemented for systemd-logind.
 * For ConsoleKit, it is equivalent to act_user_get_num_sessions.)
 *
 * Returns: the number of sessions
 */
guint
act_user_get_num_sessions_anywhere (ActUser    *user)
{
        return (g_list_length (user->our_sessions)
                + g_list_length (user->other_sessions));
}

static void
act_user_get_property (GObject    *object,
                       guint       param_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
        ActUser *user;

        user = ACT_USER (object);

        switch (param_id) {
        case PROP_NONEXISTENT:
                g_value_set_boolean (value, user->nonexistent);
                break;
        case PROP_IS_LOADED:
                g_value_set_boolean (value, user->is_loaded);
                break;
        default:
                if (user->accounts_proxy != NULL) {
                        const char *property_name;

                        property_name = g_param_spec_get_name (pspec);

                        g_object_get_property (G_OBJECT (user->accounts_proxy), property_name, value);

                }
                break;
        }
}


static void
act_user_class_init (ActUserClass *class)
{
        GObjectClass *gobject_class;

        gobject_class = G_OBJECT_CLASS (class);

        gobject_class->finalize = act_user_finalize;
        gobject_class->get_property = act_user_get_property;

        g_object_class_install_property (gobject_class,
                                         PROP_REAL_NAME,
                                         g_param_spec_string ("real-name",
                                                              "Real Name",
                                                              "The real name to display for this user.",
                                                              NULL,
                                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (gobject_class,
                                         PROP_ACCOUNT_TYPE,
                                         g_param_spec_int ("account-type",
                                                           "Account Type",
                                                           "The account type for this user.",
                                                           ACT_USER_ACCOUNT_TYPE_STANDARD,
                                                           ACT_USER_ACCOUNT_TYPE_ADMINISTRATOR,
                                                           ACT_USER_ACCOUNT_TYPE_STANDARD,
                                                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (gobject_class,
                                         PROP_PASSWORD_MODE,
                                         g_param_spec_int ("password-mode",
                                                           "Password Mode",
                                                           "The password mode for this user.",
                                                           ACT_USER_PASSWORD_MODE_REGULAR,
                                                           ACT_USER_PASSWORD_MODE_NONE,
                                                           ACT_USER_PASSWORD_MODE_REGULAR,
                                                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (gobject_class,
                                         PROP_PASSWORD_HINT,
                                         g_param_spec_string ("password-hint",
                                                              "Password Hint",
                                                              "Hint to help this user remember his password",
                                                              NULL,
                                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (gobject_class,
                                         PROP_UID,
                                         g_param_spec_int ("uid",
                                                           "User ID",
                                                           "The UID for this user.",
                                                           0, G_MAXINT, 0,
                                                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (gobject_class,
                                         PROP_USER_NAME,
                                         g_param_spec_string ("user-name",
                                                              "User Name",
                                                              "The login name for this user.",
                                                              NULL,
                                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (gobject_class,
                                         PROP_HOME_DIR,
                                         g_param_spec_string ("home-directory",
                                                              "Home Directory",
                                                              "The home directory for this user.",
                                                              NULL,
                                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (gobject_class,
                                         PROP_SHELL,
                                         g_param_spec_string ("shell",
                                                              "Shell",
                                                              "The shell for this user.",
                                                              NULL,
                                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (gobject_class,
                                         PROP_EMAIL,
                                         g_param_spec_string ("email",
                                                              "Email",
                                                              "The email address for this user.",
                                                              NULL,
                                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (gobject_class,
                                         PROP_LOCATION,
                                         g_param_spec_string ("location",
                                                              "Location",
                                                              "The location of this user.",
                                                              NULL,
                                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (gobject_class,
                                         PROP_LOGIN_FREQUENCY,
                                         g_param_spec_int ("login-frequency",
                                                           "login frequency",
                                                           "login frequency",
                                                           0,
                                                           G_MAXINT,
                                                           0,
                                                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (gobject_class,
                                         PROP_LOGIN_TIME,
                                         g_param_spec_int64 ("login-time",
                                                             "Login time",
                                                             "The last login time for this user.",
                                                             0,
                                                             G_MAXINT64,
                                                             0,
                                                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (gobject_class,
                                         PROP_LOGIN_HISTORY,
                                         g_param_spec_variant ("login-history",
                                                               "Login history",
                                                               "The login history for this user.",
                                                               G_VARIANT_TYPE ("a(xxa{sv})"),
                                                               NULL,
                                                               G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (gobject_class,
                                         PROP_ICON_FILE,
                                         g_param_spec_string ("icon-file",
                                                              "Icon File",
                                                              "The path to an icon for this user.",
                                                              NULL,
                                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (gobject_class,
                                         PROP_LANGUAGE,
                                         g_param_spec_string ("language",
                                                              "Language",
                                                              "User's locale.",
                                                              NULL,
                                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (gobject_class,
                                         PROP_X_SESSION,
                                         g_param_spec_string ("x-session",
                                                              "X session",
                                                              "User's X session.",
                                                              NULL,
                                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (gobject_class,
                                         PROP_IS_LOADED,
                                         g_param_spec_boolean ("is-loaded",
                                                               "Is loaded",
                                                               "Determines whether or not the user object is loaded and ready to read from.",
                                                               FALSE,
                                                               G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (gobject_class,
                                         PROP_NONEXISTENT,
                                         g_param_spec_boolean ("nonexistent",
                                                               "Doesn't exist",
                                                               "Determines whether or not the user object represents a valid user account.",
                                                               FALSE,
                                                               G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
        g_object_class_install_property (gobject_class,
                                         PROP_LOCKED,
                                         g_param_spec_boolean ("locked",
                                                               "Locked",
                                                               "Locked",
                                                               FALSE,
                                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (gobject_class,
                                         PROP_AUTOMATIC_LOGIN,
                                         g_param_spec_boolean ("automatic-login",
                                                               "Automatic Login",
                                                               "Automatic Login",
                                                               FALSE,
                                                               G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (gobject_class,
                                         PROP_LOCAL_ACCOUNT,
                                         g_param_spec_boolean ("local-account",
                                                               "Local Account",
                                                               "Local Account",
                                                               FALSE,
                                                               G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

        g_object_class_install_property (gobject_class,
                                         PROP_SYSTEM_ACCOUNT,
                                         g_param_spec_boolean ("system-account",
                                                               "System Account",
                                                               "System Account",
                                                               FALSE,
                                                               G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));


        /**
         * ActUser::changed:
         *
         * Emitted when the user accounts changes in some way.
         */
        signals [CHANGED] =
                g_signal_new ("changed",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
        /**
         * ActUser::sessions-changed:
         *
         * Emitted when the list of sessions for this user changes.
         */
        signals [SESSIONS_CHANGED] =
                g_signal_new ("sessions-changed",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
}

static void
act_user_init (ActUser *user)
{
        g_autoptr(GError) error = NULL;

        user->our_sessions = NULL;
        user->other_sessions = NULL;

        user->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (user->connection == NULL) {
                g_warning ("Couldn't connect to system bus: %s", error->message);
        }
}

static void
act_user_finalize (GObject *object)
{
        ActUser *user;

        user = ACT_USER (object);

        if (user->accounts_proxy != NULL) {
                g_object_unref (user->accounts_proxy);
        }

        if (user->connection != NULL) {
                g_object_unref (user->connection);
        }

        if (G_OBJECT_CLASS (act_user_parent_class)->finalize)
                (*G_OBJECT_CLASS (act_user_parent_class)->finalize) (object);
}

static void
set_is_loaded (ActUser  *user,
               gboolean  is_loaded)
{
        if (user->is_loaded != is_loaded) {
                user->is_loaded = is_loaded;
                g_object_notify (G_OBJECT (user), "is-loaded");
        }
}

/**
 * act_user_get_uid:
 * @user: the user object to examine.
 *
 * Retrieves the ID of @user.
 *
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 **/

uid_t
act_user_get_uid (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), -1);

        if (user->accounts_proxy == NULL)
                return -1;

        return accounts_user_get_uid (user->accounts_proxy);
}

/**
 * act_user_get_real_name:
 * @user: the user object to examine.
 *
 * Retrieves the display name of @user.
 *
 * Returns: (transfer none): a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 **/
const char *
act_user_get_real_name (ActUser *user)
{
        const char *real_name = NULL;

        g_return_val_if_fail (ACT_IS_USER (user), NULL);

        if (user->accounts_proxy == NULL)
                return NULL;

        real_name = accounts_user_get_real_name (user->accounts_proxy);

        if (real_name == NULL || real_name[0] == '\0') {
                real_name = accounts_user_get_user_name (user->accounts_proxy);
        }

        return real_name;
}

/**
 * act_user_get_account_type:
 * @user: the user object to examine.
 *
 * Retrieves the account type of @user.
 *
 * Returns: a #ActUserAccountType
 **/
ActUserAccountType
act_user_get_account_type (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), ACT_USER_ACCOUNT_TYPE_STANDARD);

        if (user->accounts_proxy == NULL)
                return ACT_USER_ACCOUNT_TYPE_STANDARD;

        return accounts_user_get_account_type (user->accounts_proxy);
}

/**
 * act_user_get_password_mode:
 * @user: the user object to examine.
 *
 * Retrieves the password mode of @user.
 *
 * Returns: a #ActUserPasswordMode
 **/
ActUserPasswordMode
act_user_get_password_mode (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), ACT_USER_PASSWORD_MODE_REGULAR);

        if (user->accounts_proxy == NULL)
                return ACT_USER_PASSWORD_MODE_REGULAR;

        return accounts_user_get_password_mode (user->accounts_proxy);
}

/**
 * act_user_get_password_hint:
 * @user: the user object to examine.
 *
 * Retrieves the password hint set by @user.
 *
 * Returns: (transfer none): a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 **/
const char *
act_user_get_password_hint (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), NULL);

        if (user->accounts_proxy == NULL)
                return NULL;

        return accounts_user_get_password_hint (user->accounts_proxy);
}

/**
 * act_user_get_home_dir:
 * @user: the user object to examine.
 *
 * Retrieves the home directory for @user.
 *
 * Returns: (transfer none): a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 **/
const char *
act_user_get_home_dir (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), NULL);

        if (user->accounts_proxy == NULL)
                return NULL;

        return accounts_user_get_home_directory (user->accounts_proxy);
}

/**
 * act_user_get_shell:
 * @user: the user object to examine.
 *
 * Retrieves the shell assigned to @user.
 *
 * Returns: (transfer none): a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 **/
const char *
act_user_get_shell (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), NULL);

        if (user->accounts_proxy == NULL)
                return NULL;

        return accounts_user_get_shell (user->accounts_proxy);
}

/**
 * act_user_get_email:
 * @user: the user object to examine.
 *
 * Retrieves the email address set by @user.
 *
 * Returns: (transfer none): a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 **/
const char *
act_user_get_email (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), NULL);

        if (user->accounts_proxy == NULL)
                return NULL;

        return accounts_user_get_email (user->accounts_proxy);
}

/**
 * act_user_get_location:
 * @user: the user object to examine.
 *
 * Retrieves the location set by @user.
 *
 * Returns: (transfer none): a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 **/
const char *
act_user_get_location (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), NULL);

        if (user->accounts_proxy == NULL)
                return NULL;

        return accounts_user_get_location (user->accounts_proxy);
}

/**
 * act_user_get_user_name:
 * @user: the user object to examine.
 *
 * Retrieves the login name of @user.
 *
 * Returns: (transfer none): a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 **/

const char *
act_user_get_user_name (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), NULL);

        if (user->accounts_proxy == NULL)
                return NULL;

        return accounts_user_get_user_name (user->accounts_proxy);
}

/**
 * act_user_get_login_frequency:
 * @user: a #ActUser
 *
 * Returns the number of times @user has logged in.
 *
 * Returns: the login frequency
 */
int
act_user_get_login_frequency (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), 0);

        if (user->accounts_proxy == NULL)
                return 1;

        return accounts_user_get_login_frequency (user->accounts_proxy);
}

/**
 * act_user_get_login_time:
 * @user: a #ActUser
 *
 * Returns the last login time for @user.
 *
 * Returns: the login time
 */
gint64
act_user_get_login_time (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), 0);

        if (user->accounts_proxy == NULL)
                return 0;

        return accounts_user_get_login_time (user->accounts_proxy);
}

/**
 * act_user_get_login_history:
 * @user: a #ActUser
 *
 * Returns the login history for @user.
 *
 * Returns: (transfer none): a pointer to GVariant of type "a(xxa{sv})"
 * which must not be modified or freed, or %NULL.
 */
const GVariant *
act_user_get_login_history (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), NULL);

        if (user->accounts_proxy == NULL)
                return NULL;

        return accounts_user_get_login_history (user->accounts_proxy);
}

/**
 * act_user_collate:
 * @user1: a user
 * @user2: a user
 *
 * Organize the user by login frequency and names.
 *
 * Returns: negative if @user1 is before @user2, zero if equal
 *    or positive if @user1 is after @user2
 */
int
act_user_collate (ActUser *user1,
                  ActUser *user2)
{
        const char *str1;
        const char *str2;
        int         num1;
        int         num2;
        guint       len1;
        guint       len2;

        g_return_val_if_fail (ACT_IS_USER (user1), 0);
        g_return_val_if_fail (ACT_IS_USER (user2), 0);

        num1 = act_user_get_login_frequency (user1);
        num2 = act_user_get_login_frequency (user2);

        if (num1 > num2) {
                return -1;
        }

        if (num1 < num2) {
                return 1;
        }


        len1 = g_list_length (user1->our_sessions);
        len2 = g_list_length (user2->our_sessions);

        if (len1 > len2) {
                return -1;
        }

        if (len1 < len2) {
                return 1;
        }

        /* if login frequency is equal try names */
        str1 = act_user_get_real_name (user1);
        str2 = act_user_get_real_name (user2);

        if (str1 == NULL && str2 != NULL) {
                return -1;
        }

        if (str1 != NULL && str2 == NULL) {
                return 1;
        }

        if (str1 == NULL && str2 == NULL) {
                return 0;
        }

        return g_utf8_collate (str1, str2);
}

/**
 * act_user_is_logged_in:
 * @user: a #ActUser
 *
 * Returns whether or not #ActUser is currently graphically logged in
 * on the same seat as the seat of the session of the calling process.
 *
 * Returns: %TRUE or %FALSE
 */
gboolean
act_user_is_logged_in (ActUser *user)
{
        return user->our_sessions != NULL;
}

/**
 * act_user_is_logged_in_anywhere:
 * @user: a #ActUser
 *
 * Returns whether or not #ActUser is currently logged in in any way
 * whatsoever.  See also act_user_is_logged_in().
 *
 * (Currently, this function is only implemented for systemd-logind.
 * For ConsoleKit, it is equivalent to act_user_is_logged_in.)
 *
 * Returns: %TRUE or %FALSE
 */
gboolean
act_user_is_logged_in_anywhere (ActUser *user)
{
        return user->our_sessions != NULL || user->other_sessions != NULL;
}

/**
 * act_user_get_saved:
 * @user: a #ActUser
 *
 * Returns whether or not the #ActUser account has retained state in accountsservice.
 *
 * Returns: %TRUE or %FALSE
 */
gboolean
act_user_get_saved (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), TRUE);

        if (user->accounts_proxy == NULL)
                return FALSE;

        return accounts_user_get_saved (user->accounts_proxy);
}

/**
 * act_user_get_locked:
 * @user: a #ActUser
 *
 * Returns whether or not the #ActUser account is locked.
 *
 * Returns: %TRUE or %FALSE
 */
gboolean
act_user_get_locked (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), TRUE);

        if (user->accounts_proxy == NULL)
                return TRUE;

        return accounts_user_get_locked (user->accounts_proxy);
}

/**
 * act_user_get_automatic_login:
 * @user: a #ActUser
 *
 * Returns whether or not #ActUser is automatically logged in at boot time.
 *
 * Returns: %TRUE or %FALSE
 */
gboolean
act_user_get_automatic_login (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), FALSE);

        if (user->accounts_proxy == NULL)
                return FALSE;

       return accounts_user_get_automatic_login (user->accounts_proxy);
}

/**
 * act_user_is_system_account:
 * @user: a #ActUser
 *
 * Returns whether or not #ActUser represents a 'system account' like
 * 'root' or 'nobody'.
 *
 * Returns: %TRUE or %FALSE
 */
gboolean
act_user_is_system_account (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), TRUE);

        if (user->accounts_proxy == NULL)
                return TRUE;

        return accounts_user_get_system_account (user->accounts_proxy);
}

/**
 * act_user_is_local_account:
 * @user: the user object to examine.
 *
 * Retrieves whether the user is a local account or not.
 *
 * Returns: %TRUE if the user is local
 **/
gboolean
act_user_is_local_account (ActUser   *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), FALSE);

        if (user->accounts_proxy == NULL)
                return FALSE;

        return accounts_user_get_local_account (user->accounts_proxy);
}

/**
 * act_user_is_nonexistent:
 * @user: the user object to examine.
 *
 * Retrieves whether the user is nonexistent or not.
 *
 * Returns: %TRUE if the user is nonexistent
 **/
gboolean
act_user_is_nonexistent (ActUser   *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), FALSE);

        return user->nonexistent;
}

/**
 * act_user_get_icon_file:
 * @user: a #ActUser
 *
 * Returns the path to the account icon belonging to @user.
 *
 * Returns: (transfer none): a path to an icon
 */
const char *
act_user_get_icon_file (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), NULL);

        if (user->accounts_proxy == NULL)
                return NULL;

        return accounts_user_get_icon_file (user->accounts_proxy);
}

/**
 * act_user_get_language:
 * @user: a #ActUser
 *
 * Returns the path to the configured locale of @user.
 *
 * Returns: (transfer none): a path to an icon
 */
const char *
act_user_get_language (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), NULL);

        if (user->accounts_proxy == NULL)
                return NULL;

        return accounts_user_get_language (user->accounts_proxy);
}

/**
 * act_user_get_x_session:
 * @user: a #ActUser
 *
 * Returns the path to the configured X session for @user.
 *
 * Returns: (transfer none): a path to an icon
 */
const char *
act_user_get_x_session (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), NULL);

        if (user->accounts_proxy == NULL)
                return NULL;

        return accounts_user_get_xsession (user->accounts_proxy);
}

/**
 * act_user_get_session:
 * @user: a #ActUser
 *
 * Returns the path to the configured session for @user.
 *
 * Returns: (transfer none): a path to an icon
 */
const char *
act_user_get_session (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), NULL);

        if (user->accounts_proxy == NULL)
                return NULL;

        return accounts_user_get_session (user->accounts_proxy);
}

/**
 * act_user_get_session_type:
 * @user: a #ActUser
 *
 * Returns the type of the configured session for @user.
 *
 * Returns: (transfer none): a path to an icon
 */
const char *
act_user_get_session_type (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), NULL);

        if (user->accounts_proxy == NULL)
                return NULL;

        return accounts_user_get_session_type (user->accounts_proxy);
}

/**
 * act_user_get_object_path:
 * @user: a #ActUser
 *
 * Returns the user accounts service object path of @user,
 * or %NULL if @user doesn't have an object path associated
 * with it.
 *
 * Returns: (transfer none): the object path of the user
 */
const char *
act_user_get_object_path (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), NULL);

        if (user->accounts_proxy == NULL)
                return NULL;

        return g_dbus_proxy_get_object_path (G_DBUS_PROXY (user->accounts_proxy));
}

/**
 * act_user_get_primary_session_id:
 * @user: a #ActUser
 *
 * Returns the id of the primary session of @user, or %NULL if @user
 * has no primary session.  The primary session will always be
 * graphical and will be chosen from the sessions on the same seat as
 * the seat of the session of the calling process.
 *
 * Returns: (transfer none): the id of the primary session of the user
 */
const char *
act_user_get_primary_session_id (ActUser *user)
{
        if (user->our_sessions == NULL) {
                g_debug ("User %s is not logged in here, so has no primary session",
                         act_user_get_user_name (user));
                return NULL;
        }

        /* FIXME: better way to choose? */
        return user->our_sessions->data;
}

/**
 * _act_user_update_as_nonexistent:
 * @user: the user object to update.
 *
 * Set's the 'non-existent' property of @user to #TRUE
 * Can only be called before the user is loaded.
 **/
void
_act_user_update_as_nonexistent (ActUser *user)
{
        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (!act_user_is_loaded (user));
        g_return_if_fail (act_user_get_object_path (user) == NULL);

        user->nonexistent = TRUE;
        g_object_notify (G_OBJECT (user), "nonexistent");

        set_is_loaded (user, TRUE);
}

static void
on_accounts_proxy_changed (ActUser *user)
{
        g_signal_emit (user, signals[CHANGED], 0);
}

/**
 * _act_user_update_from_object_path:
 * @user: the user object to update.
 * @object_path: the object path of the user to use.
 *
 * Updates the properties of @user from the accounts service via
 * the object path in @object_path.
 **/
void
_act_user_update_from_object_path (ActUser    *user,
                                   const char *object_path)
{
        AccountsUser    *accounts_proxy;
        GError          *error = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (object_path != NULL);
        g_return_if_fail (act_user_get_object_path (user) == NULL);

        accounts_proxy = accounts_user_proxy_new_sync (user->connection,
                                                       G_DBUS_PROXY_FLAGS_NONE,
                                                       ACCOUNTS_NAME,
                                                       object_path,
                                                       NULL,
                                                       &error);
        if (!accounts_proxy) {
                g_warning ("Couldn't create accounts proxy: %s", error->message);
                return;
        }

        user->accounts_proxy = accounts_proxy;

        g_signal_connect_object (user->accounts_proxy,
                                 "changed",
                                 G_CALLBACK (on_accounts_proxy_changed),
                                 user,
                                 G_CONNECT_SWAPPED);

        g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (user->accounts_proxy), INT_MAX);

        set_is_loaded (user, TRUE);
}

void
_act_user_update_login_frequency (ActUser    *user,
                                  int         login_frequency)
{
        if (act_user_get_login_frequency (user) == login_frequency) {
                return;
        }

        accounts_user_set_login_frequency (user->accounts_proxy,
                                           login_frequency);
}

static void
copy_sessions_lists (ActUser *user,
                     ActUser *user_to_copy)
{
        GList *node;

        for (node = g_list_last (user_to_copy->our_sessions);
             node != NULL;
             node = node->prev) {
                user->our_sessions = g_list_prepend (user->our_sessions, g_strdup (node->data));
        }

        for (node = g_list_last (user_to_copy->other_sessions);
             node != NULL;
             node = node->prev) {
                user->other_sessions = g_list_prepend (user->other_sessions, g_strdup (node->data));
        }
}

void
_act_user_load_from_user (ActUser    *user,
                          ActUser    *user_to_copy)
{
        if (!user_to_copy->is_loaded) {
                return;
        }

        user->accounts_proxy = g_object_ref (user_to_copy->accounts_proxy);

        g_signal_connect_object (user->accounts_proxy,
                                 "changed",
                                 G_CALLBACK (on_accounts_proxy_changed),
                                 user,
                                 G_CONNECT_SWAPPED);

        if (user->our_sessions == NULL && user->other_sessions == NULL) {
                copy_sessions_lists (user, user_to_copy);
                g_signal_emit (user, signals[SESSIONS_CHANGED], 0);
        }

        set_is_loaded (user, TRUE);
}

/**
 * act_user_is_loaded:
 * @user: a #ActUser
 *
 * Determines whether or not the user object is loaded and ready to read from.
 * #ActUserManager:is-loaded property must be %TRUE before calling
 * act_user_manager_list_users()
 *
 * Returns: %TRUE or %FALSE
 */
gboolean
act_user_is_loaded (ActUser *user)
{
        return user->is_loaded;
}

/**
 * act_user_get_password_expiration_policy:
 * @user: the user object to query.
 * @expiration_time: location to write time users password expires
 * @last_change_time: location to write time users password was last changed.
 * @min_days_between_changes: location to write minimum number of days needed between password changes.
 * @max_days_between_changes: location to write maximum number of days password can stay unchanged.
 * @days_to_warn: location to write number of days to warn user password is about to expire.
 * @days_after_expiration_until_lock: location to write number of days account will be locked after password expires.
 *
 * Get the password expiration policy for a user.
 *
 * Note this function is synchronous and ignores errors.
 **/
void
act_user_get_password_expiration_policy (ActUser *user,
                                         gint64  *expiration_time,
                                         gint64  *last_change_time,
                                         gint64  *min_days_between_changes,
                                         gint64  *max_days_between_changes,
                                         gint64  *days_to_warn,
                                         gint64  *days_after_expiration_until_lock)
{
        g_autoptr(GError) error = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (ACCOUNTS_IS_USER (user->accounts_proxy));

        if (!accounts_user_call_get_password_expiration_policy_sync (user->accounts_proxy,
                                                                     expiration_time,
                                                                     last_change_time,
                                                                     min_days_between_changes,
                                                                     max_days_between_changes,
                                                                     days_to_warn,
                                                                     days_after_expiration_until_lock,
                                                                     NULL,
                                                                     &error)) {
                g_warning ("GetPasswordExpirationPolicy call failed: %s", error->message);
                return;
        }
}

/**
 * act_user_set_email:
 * @user: the user object to alter.
 * @email: an email address
 *
 * Assigns a new email to @user.
 *
 * Note this function is synchronous and ignores errors.
 **/
void
act_user_set_email (ActUser    *user,
                    const char *email)
{
        g_autoptr(GError) error = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (email != NULL);
        g_return_if_fail (ACCOUNTS_IS_USER (user->accounts_proxy));

        if (!accounts_user_call_set_email_sync (user->accounts_proxy,
                                                email,
                                                NULL,
                                                &error)) {
                g_warning ("SetEmail call failed: %s", error->message);
                return;
        }
}

/**
 * act_user_set_language:
 * @user: the user object to alter.
 * @language: a locale (e.g. en_US.utf8)
 *
 * Assigns a new locale for @user.
 *
 * Note this function is synchronous and ignores errors.
 **/
void
act_user_set_language (ActUser    *user,
                       const char *language)
{
        g_autoptr(GError) error = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (language != NULL);
        g_return_if_fail (ACCOUNTS_IS_USER (user->accounts_proxy));

        if (!accounts_user_call_set_language_sync (user->accounts_proxy,
                                                   language,
                                                   NULL,
                                                   &error)) {
                g_warning ("SetLanguage for language %s failed: %s", language, error->message);
                return;
        }
}

/**
 * act_user_set_x_session:
 * @user: the user object to alter.
 * @x_session: an x session (e.g. gnome)
 *
 * Assigns a new x session for @user.
 *
 * Note this function is synchronous and ignores errors.
 **/
void
act_user_set_x_session (ActUser    *user,
                        const char *x_session)
{
        g_autoptr(GError) error = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (x_session != NULL);
        g_return_if_fail (ACCOUNTS_IS_USER (user->accounts_proxy));

        if (!accounts_user_call_set_xsession_sync (user->accounts_proxy,
                                                   x_session,
                                                   NULL,
                                                   &error)) {
                g_warning ("SetXSession call failed: %s", error->message);
                return;
        }
}

/**
 * act_user_set_session:
 * @user: the user object to alter.
 * @session: a session (e.g. gnome)
 *
 * Assigns a new session for @user.
 *
 * Note this function is synchronous and ignores errors.
 **/
void
act_user_set_session (ActUser    *user,
                      const char *session)
{
        g_autoptr(GError) error = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (session != NULL);
        g_return_if_fail (ACCOUNTS_IS_USER (user->accounts_proxy));

        if (!accounts_user_call_set_session_sync (user->accounts_proxy,
                                                  session,
                                                  NULL,
                                                  &error)) {
                g_warning ("SetSession call failed: %s", error->message);
                return;
        }
}

/**
 * act_user_set_session_type:
 * @user: the user object to alter.
 * @session_type: a type of session (e.g. "wayland" or "x11")
 *
 * Assigns a type to the session for @user.
 *
 * Note this function is synchronous and ignores errors.
 **/
void
act_user_set_session_type (ActUser    *user,
                           const char *session_type)
{
        g_autoptr(GError) error = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (session_type != NULL);
        g_return_if_fail (ACCOUNTS_IS_USER (user->accounts_proxy));

        if (!accounts_user_call_set_session_type_sync (user->accounts_proxy,
                                                       session_type,
                                                       NULL,
                                                       &error)) {
                g_warning ("SetSessionType call failed: %s", error->message);
                return;
        }
}

/**
 * act_user_set_location:
 * @user: the user object to alter.
 * @location: a location
 *
 * Assigns a new location for @user.
 *
 * Note this function is synchronous and ignores errors.
 **/
void
act_user_set_location (ActUser    *user,
                       const char *location)
{
        g_autoptr(GError) error = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (location != NULL);
        g_return_if_fail (ACCOUNTS_IS_USER (user->accounts_proxy));

        if (!accounts_user_call_set_location_sync (user->accounts_proxy,
                                                   location,
                                                   NULL,
                                                   &error)) {
                g_warning ("SetLocation call failed: %s", error->message);
                return;
        }
}

/**
 * act_user_set_user_name:
 * @user: the user object to alter.
 * @user_name: a new user name
 *
 * Assigns a new username for @user.
 *
 * Note this function is synchronous and ignores errors.
 **/
void
act_user_set_user_name (ActUser    *user,
                        const char *user_name)
{
        g_autoptr(GError) error = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (user_name != NULL);
        g_return_if_fail (ACCOUNTS_IS_USER (user->accounts_proxy));

        if (!accounts_user_call_set_user_name_sync (user->accounts_proxy,
                                                    user_name,
                                                    NULL,
                                                    &error)) {
                g_warning ("SetUserName call failed: %s", error->message);
                return;
        }
}

/**
 * act_user_set_real_name:
 * @user: the user object to alter.
 * @real_name: a new name
 *
 * Assigns a new name for @user.
 *
 * Note this function is synchronous and ignores errors.
 **/
void
act_user_set_real_name (ActUser    *user,
                        const char *real_name)
{
        g_autoptr(GError) error = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (real_name != NULL);
        g_return_if_fail (ACCOUNTS_IS_USER (user->accounts_proxy));

        if (!accounts_user_call_set_real_name_sync (user->accounts_proxy,
                                                    real_name,
                                                    NULL,
                                                    &error)) {
                g_warning ("SetRealName call failed: %s", error->message);
                return;
        }
}

/**
 * act_user_set_icon_file:
 * @user: the user object to alter.
 * @icon_file: path to an icon
 *
 * Assigns a new icon for @user.
 *
 * Note this function is synchronous and ignores errors.
 **/
void
act_user_set_icon_file (ActUser    *user,
                        const char *icon_file)
{
        g_autoptr(GError) error = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (icon_file != NULL);
        g_return_if_fail (ACCOUNTS_IS_USER (user->accounts_proxy));

        if (!accounts_user_call_set_icon_file_sync (user->accounts_proxy,
                                                    icon_file,
                                                    NULL,
                                                    &error)) {
                g_warning ("SetIconFile call failed: %s", error->message);
                return;
        }
}

/**
 * act_user_set_account_type:
 * @user: the user object to alter.
 * @account_type: a #ActUserAccountType
 *
 * Changes the account type of @user.
 *
 * Note this function is synchronous and ignores errors.
 **/
void
act_user_set_account_type (ActUser            *user,
                           ActUserAccountType  account_type)
{
        g_autoptr(GError) error = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (ACCOUNTS_IS_USER (user->accounts_proxy));

        if (!accounts_user_call_set_account_type_sync (user->accounts_proxy,
                                                    account_type,
                                                    NULL,
                                                    &error)) {
                g_warning ("SetAccountType call failed: %s", error->message);
                return;
        }
}

static gchar
salt_char (GRand *rand)
{
        gchar salt[] = "ABCDEFGHIJKLMNOPQRSTUVXYZ"
                       "abcdefghijklmnopqrstuvxyz"
                       "./0123456789";

        return salt[g_rand_int_range (rand, 0, G_N_ELEMENTS (salt))];
}

static gchar *
make_crypted (const gchar *plain)
{
        g_autoptr(GString) salt = NULL;
        g_autoptr(GRand) rand = NULL;
        gint i;

        rand = g_rand_new ();
        salt = g_string_sized_new (21);

        /* SHA 256 */
        g_string_append (salt, "$6$");
        for (i = 0; i < 16; i++) {
                g_string_append_c (salt, salt_char (rand));
        }
        g_string_append_c (salt, '$');

        return g_strdup (crypt (plain, salt->str));
}

/**
 * act_user_set_password:
 * @user: the user object to alter.
 * @password: a password
 * @hint: a hint to help user recall password
 *
 * Changes the password of @user to @password.
 * @hint is displayed to the user if they forget the password.
 *
 * Note this function is synchronous and ignores errors.
 **/
void
act_user_set_password (ActUser             *user,
                       const gchar         *password,
                       const gchar         *hint)
{
        g_autoptr(GError) error = NULL;
        g_autofree gchar *crypted = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (password != NULL);
        g_return_if_fail (ACCOUNTS_IS_USER (user->accounts_proxy));

        crypted = make_crypted (password);
        if (!accounts_user_call_set_password_sync (user->accounts_proxy,
                                                   crypted,
                                                   hint,
                                                   NULL,
                                                   &error)) {
                g_warning ("SetPassword call failed: %s", error->message);
        }
        memset (crypted, 0, strlen (crypted));
}

/**
 * act_uset_set_password_hint:
 * @user: the user object to alter.
 * @hint: a hint to help user recall password
 *
 * Sets the password hint of @user.
 * @hint is displayed to the user if they forget the password.
 *
 * Note this function is synchronous and ignores errors.
 **/
void
act_user_set_password_hint (ActUser     *user,
                            const gchar *hint)
{
        g_autoptr(GError) error = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (ACCOUNTS_IS_USER (user->accounts_proxy));

        if (!accounts_user_call_set_password_hint_sync (user->accounts_proxy,
                                                        hint,
                                                        NULL,
                                                        &error)) {
                g_warning ("SetPasswordHint call failed: %s", error->message);
        }
}

/**
 * act_user_set_password_mode:
 * @user: the user object to alter.
 * @password_mode: a #ActUserPasswordMode
 *
 * Changes the password of @user.  If @password_mode is
 * ACT_USER_PASSWORD_MODE_SET_AT_LOGIN then the user will
 * be asked for a new password at the next login.  If @password_mode
 * is ACT_USER_PASSWORD_MODE_NONE then the user will not require
 * a password to log in.
 *
 * Note this function is synchronous and ignores errors.
 **/
void
act_user_set_password_mode (ActUser             *user,
                            ActUserPasswordMode  password_mode)
{
        g_autoptr(GError) error = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (ACCOUNTS_IS_USER (user->accounts_proxy));

        if (!accounts_user_call_set_password_mode_sync (user->accounts_proxy,
                                                        (gint) password_mode,
                                                        NULL,
                                                        &error)) {
                g_warning ("SetPasswordMode call failed: %s", error->message);
        }
}

/**
 * act_user_set_locked:
 * @user: the user object to alter.
 * @locked: whether or not the account is locked
 *
 * Note this function is synchronous and ignores errors.
 **/
void
act_user_set_locked (ActUser  *user,
                     gboolean  locked)
{
        g_autoptr(GError) error = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (ACCOUNTS_IS_USER (user->accounts_proxy));

        if (!accounts_user_call_set_locked_sync (user->accounts_proxy,
                                                 locked,
                                                 NULL,
                                                 &error)) {
                g_warning ("SetLocked call failed: %s", error->message);
        }
}

/**
 * act_user_set_automatic_login:
 * @user: the user object to alter
 * @enabled: whether or not to autologin for user.
 *
 * If enabled is set to %TRUE then this user will automatically be logged in
 * at boot up time.  Only one user can be configured to auto login at any given
 * time, so subsequent calls to act_user_set_automatic_login() override previous
 * calls.
 *
 * Note this function is synchronous and ignores errors.
 **/
void
act_user_set_automatic_login (ActUser   *user,
                              gboolean  enabled)
{
        g_autoptr(GError) error = NULL;

        g_return_if_fail (ACT_IS_USER (user));
        g_return_if_fail (ACCOUNTS_IS_USER (user->accounts_proxy));

        if (!accounts_user_call_set_automatic_login_sync (user->accounts_proxy,
                                                          enabled,
                                                          NULL,
                                                          &error)) {
                g_warning ("SetAutomaticLogin call failed: %s", error->message);
        }
}
