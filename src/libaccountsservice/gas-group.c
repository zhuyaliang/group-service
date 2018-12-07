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

#define ACT_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), ACT_TYPE_USER, ActUserClass))
#define ACT_IS_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), ACT_TYPE_USER))
#define ACT_USER_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS ((object), ACT_TYPE_USER, ActUserClass))

#define GROUP_NAME           "org.group.admin"
#define GROUP_LSIT_INTERFACE "org.group.admin.list"

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


const GVariant *
act_user_get_login_history (ActUser *user)
{
        g_return_val_if_fail (ACT_IS_USER (user), NULL);

        if (user->accounts_proxy == NULL)
                return NULL;

        return accounts_user_get_login_history (user->accounts_proxy);
}

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

char const **gas_group_get_group_users (GasGroup *group)
{
    g_return_val_if_fail (GAS_IS_GROUP (group), NULL);

    if (group->group_proxy == NULL)
        return NULL;
    return user_group_list_get_users(group->group_proxy)
}    

const char * gas_group_get_group_name (GasGroup *group)
{
    g_return_val_if_fail (GAS_IS_GROUP (group), NULL);

    if (group->group_proxy == NULL)
        return NULL;

    return user_group_list_get_group_name(group->group_proxy)
}

const char * gas_group_get_object_path (GasGroup *group)
{
    g_return_val_if_fail (GAS_IS_GROUP (group), NULL);

    if (group->group_proxy == NULL)
        return NULL;

    return g_dbus_proxy_get_object_path (G_DBUS_PROXY (group->group_proxy));
}

gid_t gas_group_get_gid (GasGroup *group)
{
    g_return_val_if_fail (ACT_IS_USER (user), -1);
    
    if (group->group_proxy == NULL)
        return -1;

    return user_group_list_get_gid(group->group_proxy);
}

static void on_group_proxy_changed (GasGroup *group)
{
    g_signal_emit (group, signals[CHANGED], 0);
}

void _gas_group_update_from_object_path (GasGroup *group,
                                         const char *object_path)
{
    UserGroupList *group_proxy;
    GError *error = NULL;

    g_return_if_fail (GAS_IS_GROUP (group));
    g_return_if_fail (object_path != NULL);
    g_return_if_fail (gas_group_get_object_path (group) == NULL);

    group_proxy = user_group_list_proxy_new_sync (group->connection,
                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                  GROUP_NAME,
                                                  object_path,
                                                  NULL,
                                                  &error);
    if (!group_proxy) 
    {
        g_print ("Couldn't create accounts proxy: %s", error->message);
        return;
    }

    group->group_proxy = group_proxy;

    g_signal_connect_object (group->group_proxy,
                            "changed",
                             G_CALLBACK (on_group_proxy_changed),
                             group,
                             G_CONNECT_SWAPPED);

    g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (group->group_proxy), INT_MAX);
    set_is_loaded (group, TRUE);
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

gboolean gsa_group_is_loaded (GasGroup *group)
{
    return group->is_loaded;
}

void gas_group_set_group_name (GasGroup *group,const char *name)
{
    GError error = NULL;

    g_return_if_fail (GAS_IS_GROUP (group));
    g_return_if_fail (name != NULL);
    g_return_if_fail (USER_GROUP_IS_LIST (group->group_proxy));

    if (!user_group_list_call_change_group_name_sync (group->group_proxy,
                                                      name,
                                                      NULL,
                                                      &error)) 
    {
        g_warning ("set_group_name call failed: %s", error->message);
        return;
    }
}

void gas_group_add_user_group (GasGroup *group,const char *name)
{
    GError  *error = NULL;
    g_return_if_fail (GAS_IS_GROUP (group));
    g_return_if_fail (name != NULL);
    g_return_if_fail (USER_GROUP_IS_LIST (group->group_proxy));
    g_return_if_fail (getpwnam (name) == NULL);

    if (!user_group_list_call_add_user_to_group_sync (group->group_proxy,
                                                      name,
                                                      NULL,
                                                      &error)) 
    {
        g_warning ("add user to group call failed: %s", error->message);
        return;
    }
}
void gas_group_remove_user_group (GasGroup *group,const char *name)
{
    GError  *error = NULL;
    g_return_if_fail (GAS_IS_GROUP (group));
    g_return_if_fail (name != NULL);
    g_return_if_fail (USER_GROUP_IS_LIST (group->group_proxy));
    g_return_if_fail (getpwnam (name) == NULL);

    if (!user_group_list_call_remove_user_from_group_sync(user->accounts_proxy,
                                                          icon_file,
                                                          NULL,
                                                          &error)) 
    {
        g_print ("remove user from group call failed: %s", error->message);
        return;
    }
}
