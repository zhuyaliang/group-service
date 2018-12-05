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
static void AcquiredCallback (GDBusConnection *Connection,
                              const gchar *name,
                              gpointer UserData)
{
	Manage *manage;

	manage = manage_new();
	if(RegisterGroupManage (manage) < 0)
	{
		printf("error !!!\r\n");;
	}
}

static void NameLostCallback (GDBusConnection *connection,
                              const gchar *name,
                              gpointer user_data)
{
    printf("Lost Lost !!!!\r\n");
    g_main_loop_quit (loop);
}

int main (int argc, char *argv[])
{    
    guint OwnID;

//    bindtextdomain (PACKAGE, LOCALEDIR);   
    bind_textdomain_codeset (PACKAGE, "UTF-8");
    setlocale (LC_ALL, "");
    //textdomain (PACKAGE);
#if !GLIB_CHECK_VERSION (2, 35, 3)
    g_type_init ();
#endif
 
    OwnID = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                            NAME_TO_CLAIM,
                            G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT,
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

