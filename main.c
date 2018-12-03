#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib-unix.h>

#include "group-server.h"

#define NAME_TO_CLAIM    "org.group.admin"
#define PACKAGE          "group-service"   
#define LOCALEDIR        "/usr/share/locale/" 

static GMainLoop *loop = NULL;
static gboolean SignalQuit (gpointer data)
{
    g_main_loop_quit (data);
    return FALSE;
}

static void SignalInit (userGroup *skeleton)
{
    g_signal_connect(skeleton,
                     "handle-set-group-name",
                     G_CALLBACK(SetGroupName),
                     NULL);
    g_signal_connect(skeleton,
                    "handle-add-user",
                     G_CALLBACK(ChangeGroup),
                     NULL);
    g_signal_connect(skeleton,
                    "handle-remove-user",
                     G_CALLBACK(RemoveGroup),
                     NULL);

}    
    
static void AcquiredCallback (GDBusConnection *Connection,
                              const gchar *name,
                              gpointer UserData)
{
    GError *error = NULL;

    GroupManage GM;
    userGroup *skeleton=NULL;
	GM.BusConnection = Connection;
    skeleton = user_group_skeleton_new();
    
    SignalInit(skeleton);
    g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(skeleton),
                                     Connection,
                                     "/org/group/admin",
                                     &error);
    if(error != NULL){
        g_print("Error: Failed to export object. Reason: %s.\n", error->message);
        g_error_free(error);
    }
    StartLoadGroup(&GM);
}

static void NameLostCallback (GDBusConnection *connection,
                              const gchar *name,
                              gpointer user_data)
{
    printf("end end !!!!\r\n");
    g_main_loop_quit (loop);
}

int main (int argc, char *argv[])
{    
    guint OwnID;

    bindtextdomain (PACKAGE, LOCALEDIR);   
    textdomain (PACKAGE);

#if !GLIB_CHECK_VERSION (2, 35, 3)
    g_type_init ();
#endif
 
    OwnID = g_bus_own_name (G_BUS_TYPE_SESSION,
                            NAME_TO_CLAIM,
                            G_BUS_NAME_OWNER_FLAGS_NONE,
                            AcquiredCallback,
                            NULL,
                            NameLostCallback,
                            NULL,
                            NULL);
    
    loop = g_main_loop_new (NULL, FALSE);

    g_unix_signal_add (SIGINT,  SignalQuit, loop);
    g_unix_signal_add (SIGTERM, SignalQuit, loop);

    g_main_loop_run (loop);
    g_bus_unown_name(OwnID);
    g_main_loop_unref (loop);

    return 0;
}

