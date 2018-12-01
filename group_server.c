#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <pwd.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <grp.h>
#ifdef HAVE_UTMPX_H
#include <utmpx.h>
#endif
#include <stdio.h>
#include <gio/gio.h>
#include <glib.h>
#include <assert.h>
#include "user-group-generated.h"
#include "group.h"

#define PATH_PASSWD "/etc/passwd"
#define PATH_SHADOW "/etc/shadow"
#define PATH_GROUP  "/etc/group"



struct User 
{
        gchar *object_path;

        GKeyFile     *keyfile;
        gid_t         gid;
        gint64        expiration_time;
        gint64        last_change_time;
        gint64        min_days_between_changes;
        gint64        max_days_between_changes;
        gint64        days_to_warn;
        gint64        days_after_expiration_until_lock;
        GVariant     *login_history;
        gchar        *icon_file;
        gchar        *default_icon_file;
        gboolean      account_expiration_policy_known;
        gboolean      cached;
};
typedef struct group * (* GroupEntryGeneratorFunc) (FILE *);
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
static GHashTable * CreateGroupsHashTable (void)
{
        return g_hash_table_new_full (g_str_hash,
                                      g_str_equal,
                                      g_free,
                                      g_object_unref);
}
static struct group *
entry_generator_fgetgrent (FILE *fd)
{
	struct group *grent;
	
    grent = fgetgrent (fd);
    if (grent != NULL) 
	{
      	return grent;
    }
    fclose (fd);
	return NULL;
}
static gboolean LocalGroupIsExcluded (struct group *grent)
{
        struct passwd *pwent = getpwnam (grent->gr_name);
        
		if (pwent && pwent->pw_gid == grent->gr_gid)
                return TRUE;

        return FALSE;
}
static void LoadGroupEntries (GHashTable *groups,
				              GroupEntryGeneratorFunc EntryGenerator)
{
	gpointer generator_state = NULL;
    struct group *grent;
    Group *group = NULL;
	FILE *fd;

	fd = fopen (PATH_GROUP, "r");
   	if(fd == NULL) 
    {
    	return;
	}
	int i = 2;
    while(i--) 
	{
    	grent = EntryGenerator (fd);
        if (grent == NULL)
        	break;

        if (LocalGroupIsExcluded (grent)) 
		{
         	printf ("skipping group: %s", grent->gr_name);
            continue;
        }

       	if (g_hash_table_lookup (groups, grent->gr_name)) 
		{
            continue;
        }

        group = group_new (grent->gr_gid);

    //    g_object_freeze_notify (G_OBJECT (group));
        //group_update_from_grent (group, grent, users);

    //    g_hash_table_insert (groups, g_strdup (group_get_group_name (group)), group);
      //  printf ("loaded group: %s", group_get_group_name (group));
    }

}
void LoadGroup(void)
{
 	GHashTable *groups;
	Group *group;

    groups = CreateGroupsHashTable ();
	LoadGroupEntries(groups, entry_generator_fgetgrent);
	
}		
void AcquiredCallback (GDBusConnection *Connection,
                           const gchar *name,
                           gpointer UserData)
{
    GError *error = NULL;

    //skeleton =  user_group_skeleton_new();
    Group *group;
	/*
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
	user_group_set_group_name(skeleton,"test");
	*/
    group = group_new (100);
    //g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(skeleton), 
    g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(group), 
                                     Connection, 
                                     "/org/isoft/UG", 
                                     &error);
    if(error != NULL){                                                           
        g_print("Error: Failed to export object. Reason: %s.\n", error->message);
        g_error_free(error);                                                     
    }
//	LoadGroup();	
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
