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

typedef struct group * (* GroupEntryGeneratorFunc) (FILE *);
typedef void  ( FileChangeCallback )(GFileMonitor *,
                                     GFile        *,
                                     GFile        *,
                                     GFileMonitorEvent,
									 GroupManage  *);

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
				              GroupEntryGeneratorFunc EntryGenerator,
							  GroupManage *GM)
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
        g_object_freeze_notify (G_OBJECT (group));
        group_update_from_grent (group, grent);

        g_hash_table_insert (groups, g_strdup (group_get_group_name (group)), group);
    }

}
static void RegisterGroup (GroupManage *GM,Group *group)
{
	GError *error = NULL;
	GDBusConnection *Connection = NULL;
	Connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	
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

static void ReloadGroups (GroupManage *GM)
{
	GHashTable *GroupsHashTable;
    GHashTable *local;
    GHashTableIter iter;
    gpointer name,value;
    Group *group;
	
    GroupsHashTable = CreateGroupsHashTable ();
	LoadGroupEntries(GroupsHashTable, entry_generator_fgetgrent,GM);

    GM->GroupsHashTable = GroupsHashTable;
	g_hash_table_iter_init (&iter, GroupsHashTable);
	
    while (g_hash_table_iter_next (&iter, &name,&value)) 
	{
    	RegisterGroup (GM,value);
		printf("ssssss\r\n");
        user_group_emit_group_added(USER_GROUP(value),
								    group_get_object_path (value)); 
  	}


}
static gboolean ReloadGroupsTimeout (GroupManage *GM)
{
        ReloadGroups (GM);
        GM->ReloadId = 0;

        return FALSE;
}

static void QueueReloadGroups (GroupManage *GM)
{
	if (GM->ReloadId > 0) 
	{
    	return;
    }
    GM->ReloadId = g_timeout_add (500, 
					              (GSourceFunc)ReloadGroupsTimeout,
								  GM);
}
static void QueueLoadGroups (GroupManage *GM)
{
	if (GM->ReloadId > 0) 
	{
    	return;
    }

   	GM->ReloadId = g_idle_add ((GSourceFunc)ReloadGroupsTimeout,GM);
}

static void GroupsMonitorChanged (GFileMonitor      *monitor,
                                  GFile             *file,
                                  GFile             *other_file,
                                  GFileMonitorEvent  event_type,
								  GroupManage *GM)
{
        if (event_type != G_FILE_MONITOR_EVENT_CHANGED &&
            event_type != G_FILE_MONITOR_EVENT_CREATED) {
                return;
        }

        QueueReloadGroups(GM);
}


static GFileMonitor *SetupMonitor (const gchar *FileName,
                                   FileChangeCallback *Callback,
								   GroupManage *GM)
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
                          GM);
    } 
    else 
    {
        g_warning ("Unable to monitor %s: %s", FileName, error->message);
        g_error_free (error);
    }
    g_object_unref (file);

    return Monitor;
}

static void MonitorFileChange(GroupManage *GM)
{
    GFileMonitor *GroupMonitor;
    GFileMonitor *PasswdMonitor;
    GFileMonitor *ShadowMonitor;
	
	GM->ReloadId = 0;
    PasswdMonitor = SetupMonitor (PATH_PASSWD,GroupsMonitorChanged,GM);
	GM->PasswdMonitor = PasswdMonitor;
    ShadowMonitor = SetupMonitor (PATH_SHADOW,GroupsMonitorChanged,GM);
	GM->ShadowMonitor = ShadowMonitor;
    GroupMonitor  = SetupMonitor (PATH_GROUP ,GroupsMonitorChanged,GM);
	GM->GroupMonitor  = GroupMonitor;
}    


void StartLoadGroup( GroupManage *GM)
{
    MonitorFileChange(GM);
	GM->GroupsHashTable = CreateGroupsHashTable ();
	QueueLoadGroups (GM);

}		
