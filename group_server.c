#include<stdio.h>
#include<gio/gio.h>
#include<glib.h>
#include<assert.h>
#include"user-group-generated.h"

static userGroup *skeleton=NULL;


static gboolean SetGroupName( userGroup *object,
                              GDBusMethodInvocation *invocation,
                              const gchar *arg_user)
{

}
static gboolean ChangeGroup ( userGroup *object,
                              GDBusMethodInvocation *invocation,
                              const gchar *arg_user)
{

}    

static gboolean RemoveGroup (userGroup *object,
                             GDBusMethodInvocation *invocation,
                             const gchar *arg_user)
{

}    
void AcquiredCallback (GDBusConnection *Connection,
                           const gchar *name,
                           gpointer UserData)
{
    GError *error = NULL;

    skeleton =  user_group_skeleton_new();

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

    g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(skeleton), 
                                     Connection, 
                                     "/org/isoft/UG", 
                                     &error);
    if(error != NULL){                                                           
        g_print("Error: Failed to export object. Reason: %s.\n", error->message);
        g_error_free(error);                                                     
    }                                                                             
}

void NameAcquiredCallback (GDBusConnection *connection,
        const gchar *name,
        gpointer user_data)
{
}

void NameLostCallback (GDBusConnection *connection,
        const gchar *name,
        gpointer user_data)
{
    printf("GBusNaddllllllllllllllllllllldmeLost_Callback has been invoked\n");
}

int main(int argc,char* argv[])
{
    guint OwnID;
    GMainLoop* loop = NULL;

    OwnID = g_bus_own_name (G_BUS_TYPE_SESSION,
                           "org.isoft.UG",
                            G_BUS_NAME_OWNER_FLAGS_NONE,
                            AcquiredCallback,
                            NameAcquiredCallback,
                            NameLostCallback,
                            NULL,
                            NULL);
    
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    g_bus_unown_name(OwnID);
    return 0;
}
