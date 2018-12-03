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
#include "group-generated.h"
#include "group.h"
#include "group-server.h"

#define PATH_PASSWD "/etc/passwd"
#define PATH_SHADOW "/etc/shadow"
#define PATH_GROUP  "/etc/group"

enum 
{
	PROP_0,
    PROP_DAEMON_VERSION
};

struct ManagePrivate
{
	GDBusConnection *BusConnection;
	GHashTable   *GroupsHashTable;
    GFileMonitor *PasswdMonitor;
    GFileMonitor *ShadowMonitor;
    GFileMonitor *GroupMonitor;
    guint         ReloadId;

};

typedef struct group * (* GroupEntryGeneratorFunc) (FILE *);
typedef void  ( FileChangeCallback )(GFileMonitor *,
                                     GFile        *,
                                     GFile        *,
                                     GFileMonitorEvent,
									 Manage       *);
static void manage_class_init (ManageClass *klass);
static void manage_init (Manage *manage);
GType manage_get_type(void)
{
    static GType manage_type = 0;
    if(!manage_type)
    {
        static const GTypeInfo manage_info = {
            sizeof(ManageClass),
            NULL,NULL,
            (GClassInitFunc)manage_class_init,
            NULL,NULL,
            sizeof(Manage),
            0,
            (GInstanceInitFunc)manage_init
        };
        manage_type = g_type_register_static(GROUP_TYPE_ADMIN_SKELETON,
                                           "Group",&manage_info,0);
    }
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
/*        
		if (pwent && pwent->pw_gid == grent->gr_gid)
                return TRUE;
*/
        return FALSE;
}
static void LoadGroupEntries (GHashTable *groups,
				              GroupEntryGeneratorFunc EntryGenerator,
							  Manage *manage)
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
	
    while(1) 
	{
    	grent = EntryGenerator (fd);
        if (grent == NULL)
        	break;

        if (LocalGroupIsExcluded (grent)) 
		{
            continue;
        }

       	if (g_hash_table_lookup (groups, grent->gr_name)) 
		{
            continue;
        }
        
		group = group_new (grent->gr_gid);
        //g_object_freeze_notify (G_OBJECT (group));
        //group_update_from_grent (group, grent);

        g_hash_table_insert (groups, g_strdup (group_get_group_name (group)), group);
    }

}
static void RegisterGroup (Manage *manage,Group *group)
{
	GError *error = NULL;
	GDBusConnection *Connection = NULL;
	Connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	
	if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (group),
                                           Connection,
                                           group_get_object_path (group),
                                           &error)) 
	{
    	if (error != NULL) 
		{
        	g_print ("error exporting group object: %s", error->message);
            g_error_free (error);
        }
        return;
    }
}

static void ReloadGroups (Manage *manage)
{
	GHashTable *GroupsHashTable;
    GHashTable *local;
    GHashTableIter iter;
    gpointer name,value;
    Group *group;
	
	group = group_new (100);
    GroupsHashTable = CreateGroupsHashTable ();
	LoadGroupEntries(GroupsHashTable, entry_generator_fgetgrent,manage);
/*
	g_hash_table_iter_init (&iter, GroupsHashTable);
	
    while (g_hash_table_iter_next (&iter, &name,&value)) 
	{
    	RegisterGroup (manage,value);
  	}
*/

}
static gboolean ReloadGroupsTimeout (Manage *manage)
{
        ReloadGroups (manage);
        manage->priv->ReloadId = 0;

        return FALSE;
}

static void QueueReloadGroups (Manage *manage)
{
	if (manage->priv->ReloadId > 0) 
	{
    	return;
    }
    manage->priv->ReloadId = g_timeout_add (500, 
					              (GSourceFunc)ReloadGroupsTimeout,
								  manage);
}
static void QueueLoadGroups (Manage *manage)
{
	if (manage->priv->ReloadId > 0) 
	{
    	return;
    }

   	manage->priv->ReloadId = g_idle_add ((GSourceFunc)ReloadGroupsTimeout,manage);
}

static void GroupsMonitorChanged (GFileMonitor      *monitor,
                                  GFile             *file,
                                  GFile             *other_file,
                                  GFileMonitorEvent  event_type,
								  Manage            *manage)
{
        if (event_type != G_FILE_MONITOR_EVENT_CHANGED &&
            event_type != G_FILE_MONITOR_EVENT_CREATED) {
                return;
        }

        QueueReloadGroups(manage);
}


static GFileMonitor *SetupMonitor (const gchar *FileName,
                                   FileChangeCallback *Callback,
								   Manage *manage)
{
    GError *error = NULL;
    GFile *file;
    GFileMonitor *Monitor;

    file    = g_file_new_for_path (FileName);
    Monitor = g_file_monitor_file (file,
                                   G_FILE_MONITOR_NONE,
                                   NULL,
                                   &error);
    if (Monitor != NULL) 
    {
        g_signal_connect (Monitor,
                         "changed",
                          G_CALLBACK (Callback),
                          manage);
    } 
    else 
    {
        g_warning ("Unable to monitor %s: %s", FileName, error->message);
        g_error_free (error);
    }
    g_object_unref (file);

    return Monitor;
}

static void manage_init (Manage *manage)
{
	manage->priv = MANAGE_GET_PRIVATE (manage);
	manage->priv->ReloadId = 0;

    manage->priv->PasswdMonitor = SetupMonitor (PATH_PASSWD,
					                            GroupsMonitorChanged,
											    manage);
    manage->priv->ShadowMonitor = SetupMonitor (PATH_SHADOW,
					                            GroupsMonitorChanged,
												manage);
    manage->priv->GroupMonitor =  SetupMonitor (PATH_GROUP ,
					                            GroupsMonitorChanged,
												manage);

	QueueLoadGroups (manage);
}
static void manage_class_init (ManageClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
/*
        object_class->finalize = daemon_finalize;
        object_class->get_property = get_property;
        object_class->set_property = set_property;

        g_object_class_override_property (object_class,
                                          PROP_DAEMON_VERSION,
										  "daemon-version");
*/										  
}
Manage *manage_new(void)
{
	Manage *manage = NULL;

    manage = MANAGE(g_object_new (TYPE_MANAGE, NULL));

    return manage;
}
int	RegisterGroupManage (Manage *manage)
{
    GError *error = NULL;
	manage->priv = MANAGE_GET_PRIVATE (manage);

	manage->priv->BusConnection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (manage->priv->BusConnection == NULL)
   	{
    	if (error != NULL)
		{			
        	g_print ("error getting system bus: %s\r\n", error->message);
        	g_error_free(error);
		}
        printf ("error getting system bus\r\n");
        return -1;
    }

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (manage),
                                           manage->priv->BusConnection,
                                          "/org/group/admin",
                                           &error)) 
	{
    	if (error != NULL)
		{			
        	g_print ("error export system bus: %s\r\n", error->message);
        	g_print ("error exporting interface: %s\r\n", error->message);
        	g_error_free(error);
		}
        printf ("error exporting interface: \r\n");
        return -1;
	}
	
	return 0;
}	
