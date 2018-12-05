#include "config.h"

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <grp.h>

#include <syslog.h>

#include <polkit/polkit.h>

#include "util.h"

static gchar *
get_cmdline_of_pid (GPid pid)
{
        gchar *ret;
        g_autofree gchar *filename = NULL;
        g_autofree gchar *contents = NULL;
        gsize contents_len;
        g_autoptr(GError) error = NULL;
        guint n;

        filename = g_strdup_printf ("/proc/%d/cmdline", (int) pid);

        if (!g_file_get_contents (filename,
                                  &contents,
                                  &contents_len,
                                  &error)) {
                g_warning ("Error opening `%s': %s",
                           filename,
                           error->message);
                return NULL;
        }
        for (n = 0; n < contents_len - 1; n++) {
                if (contents[n] == '\0')
                        contents[n] = ' ';
        }

        ret = g_strdup (contents);
        g_strstrip (ret);
        return ret;
}

static gboolean
get_caller_pid (GDBusMethodInvocation *context,
                GPid                  *pid)
{
        g_autoptr(GVariant) reply = NULL;
        g_autoptr(GError) error = NULL;
        guint32 pid_as_int;

        reply = g_dbus_connection_call_sync (g_dbus_method_invocation_get_connection (context),
                                             "org.group.admin.DBus",
                                             "/org/group/admin/DBus",
                                             "org.group.admin.DBus",
                                             "GetConnectionUnixProcessID",
                                             g_variant_new ("(s)",
                                                            g_dbus_method_invocation_get_sender (context)),
                                             G_VARIANT_TYPE ("(u)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL,
                                             &error);

        if (reply == NULL) {
                g_warning ("Could not talk to message bus to find uid of sender %s: %s",
                           g_dbus_method_invocation_get_sender (context),
                           error->message);
                return FALSE;
        }

        g_variant_get (reply, "(u)", &pid_as_int);
        *pid = pid_as_int;

        return TRUE;
}

void
sys_log (GDBusMethodInvocation *context,
         const gchar           *format,
                                ...)
{
        va_list args;
        g_autofree gchar *msg = NULL;

        va_start (args, format);
        msg = g_strdup_vprintf (format, args);
        va_end (args);

        if (context) {
                PolkitSubject *subject;
                g_autofree gchar *cmdline = NULL;
                g_autofree gchar *id = NULL;
                GPid pid = 0;
                gint uid = -1;
                g_autofree gchar *tmp = NULL;

                subject = polkit_system_bus_name_new (g_dbus_method_invocation_get_sender (context));
                id = polkit_subject_to_string (subject);

                if (get_caller_pid (context, &pid)) {
                        cmdline = get_cmdline_of_pid (pid);
                } else {
                        pid = 0;
                        cmdline = NULL;
                }

                if (cmdline != NULL) {
                        if (get_caller_uid (context, &uid)) {
                                tmp = g_strdup_printf ("request by %s [%s pid:%d uid:%d]: %s", id, cmdline, (int) pid, uid, msg);
                        } else {
                                tmp = g_strdup_printf ("request by %s [%s pid:%d]: %s", id, cmdline, (int) pid, msg);
                        }
                } else {
                        if (get_caller_uid (context, &uid) && pid != 0) {
                                tmp = g_strdup_printf ("request by %s [pid:%d uid:%d]: %s", id, (int) pid, uid, msg);
                        } else if (pid != 0) {
                                tmp = g_strdup_printf ("request by %s [pid:%d]: %s", id, (int) pid, msg);
                        } else {
                                tmp = g_strdup_printf ("request by %s: %s", id, msg);
                        }
                }

                g_free (msg);
                msg = g_steal_pointer (&tmp);

                g_object_unref (subject);
        }

        syslog (LOG_NOTICE, "%s", msg);
}

static void
get_caller_loginuid (GDBusMethodInvocation *context, gchar *loginuid, gint size)
{
        GPid pid;
        gint uid;
        g_autofree gchar *path = NULL;
        g_autofree gchar *buf = NULL;

        if (!get_caller_uid (context, &uid)) {
                uid = getuid ();
        }

        if (get_caller_pid (context, &pid)) {
                path = g_strdup_printf ("/proc/%d/loginuid", (int) pid);
        } else {
                path = NULL;
        }

        if (path != NULL && g_file_get_contents (path, &buf, NULL, NULL)) {
                strncpy (loginuid, buf, size);
        }
        else {
                g_snprintf (loginuid, size, "%d", uid);
        }
}

static gboolean
compat_check_exit_status (int      estatus,
                          GError **error)
{
#if GLIB_CHECK_VERSION(2, 33, 12)
        return g_spawn_check_exit_status (estatus, error);
#else
        if (!WIFEXITED (estatus)) {
                g_set_error (error,
                             G_SPAWN_ERROR,
                             G_SPAWN_ERROR_FAILED,
                             "Exited abnormally");
                return FALSE;
        }
        if (WEXITSTATUS (estatus) != 0) {
                g_set_error (error,
                             G_SPAWN_ERROR,
                             G_SPAWN_ERROR_FAILED,
                             "Exited with code %d",
                             WEXITSTATUS(estatus));
                return FALSE;
        }
        return TRUE;
#endif
}

static void
setup_loginuid (gpointer data)
{
     /*   const char *id = data;
        int fd;

        fd = open ("/proc/self/loginuid", O_WRONLY);
        write (fd, id, strlen (id));
        close (fd);
        */
}

gboolean
spawn_with_login_uid (GDBusMethodInvocation  *context,
                      const gchar            *argv[],
                      GError                **error)
{
        gboolean ret = FALSE;
        gchar loginuid[20];
        gint status;


        
       // get_caller_loginuid (context, loginuid, G_N_ELEMENTS (loginuid));

        if (!g_spawn_sync (NULL, (gchar**)argv, NULL, 0, setup_loginuid, loginuid, NULL, NULL, &status, error))
                goto out;
        if (!compat_check_exit_status (status, error))
                goto out;

               
        ret = TRUE;
 out:
        return ret;
}


gboolean
get_caller_uid (GDBusMethodInvocation *context,
                gint                  *uid)
{
        g_autoptr(GVariant) reply = NULL;
        g_autoptr(GError) error = NULL;

        reply = g_dbus_connection_call_sync (g_dbus_method_invocation_get_connection (context),
                                             "org.group.admin.DBus",
                                             "/org/group/admin/DBus",
                                             "org.group.admin.DBus",
                                             "GetConnectionUnixUser",
                                             g_variant_new ("(s)",
                                                            g_dbus_method_invocation_get_sender (context)),
                                             G_VARIANT_TYPE ("(u)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL,
                                             &error);

        if (reply == NULL) {
                g_warning ("Could not talk to message bus to find uid of sender %s: %s",
                           g_dbus_method_invocation_get_sender (context),
                           error->message);
                return FALSE;
        }

        g_variant_get (reply, "(u)", uid);

        return TRUE;
}
