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
#include "group-server.h"

#define PATH_PASSWD "/etc/passwd"
#define PATH_SHADOW "/etc/shadow"
#define PATH_GROUP  "/etc/group"

typedef struct group * (* GroupEntryGeneratorFunc) (FILE *);
typedef void  ( FileChangeCallback )(GFileMonitor *,
                                     GFile        *,
                                     GFile        *,
                                     GFileMonitorEvent);

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
        printf ("loaded group: =%s\r\n", group_get_group_name (group));
    }

}
static void GroupsMonitorChanged (GFileMonitor      *monitor,
                                  GFile             *file,
                                  GFile             *other_file,
                                  GFileMonitorEvent  event_type)
{
        if (event_type != G_FILE_MONITOR_EVENT_CHANGED &&
            event_type != G_FILE_MONITOR_EVENT_CREATED) {
                return;
        }

        //QueueReloadGroups();
}


static GFileMonitor *SetupMonitor (const gchar *FileName,
                                   FileChangeCallback *Callback)
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
                          NULL);
    } 
    else 
    {
        g_warning ("Unable to monitor %s: %s", FileName, error->message);
        g_error_free (error);
    }
    g_object_unref (file);

    return Monitor;
}

static void MonitorFileChange(void)
{
    GFileMonitor *GroupMonitor;
    GFileMonitor *PasswdMonitor;
    GFileMonitor *ShadowMonitor;

    PasswdMonitor = SetupMonitor (PATH_PASSWD,GroupsMonitorChanged);
    ShadowMonitor = SetupMonitor (PATH_SHADOW,GroupsMonitorChanged);
    GroupMonitor  = SetupMonitor (PATH_GROUP ,GroupsMonitorChanged);

}    
void StartLoadGroup(void)
{
 	GHashTable *groups;
	Group *group;

    MonitorFileChange();
    groups = CreateGroupsHashTable ();
	LoadGroupEntries(groups, entry_generator_fgetgrent);
/*
    g_hash_table_iter_init (&iter, groups);
    
    while (g_hash_table_iter_next (&iter, &name, (gpointer *)&group)) 
	{
    	if (!g_hash_table_lookup (old_groups, name)) 
		{
        	register_group (daemon, group);
            accounts_accounts_emit_group_added (ACCOUNTS_ACCOUNTS (daemon),
            group_get_object_path (group));
        }
        g_object_thaw_notify (G_OBJECT (group));
    }	
    */
}		
