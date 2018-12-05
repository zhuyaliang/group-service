#include "config.h"
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
#include <stdio.h>
#include <gio/gio.h>
#include <glib.h>
#include <polkit/polkit.h>
#include "group-server.h"

#define PATH_PASSWD "/etc/passwd"
#define PATH_SHADOW "/etc/shadow"
#define PATH_GROUP  "/etc/group"

enum 
{
	PROP_0,
    PROP_MANAGE_VERSION
};

struct ManagePrivate
{
	GDBusConnection *BusConnection;
	GHashTable   *GroupsHashTable;
    GFileMonitor *PasswdMonitor;
    GFileMonitor *ShadowMonitor;
    GFileMonitor *GroupMonitor;
    guint         ReloadId;
    PolkitAuthority *Authority;

};

typedef struct group * (* GroupEntryGeneratorFunc) (FILE *);
typedef void  ( FileChangeCallback )(GFileMonitor *,
                                     GFile        *,
                                     GFile        *,
                                     GFileMonitorEvent,
									 Manage       *);
static void manage_user_group_admin_iface_init (UserGroupAdminIface *iface);
static Group *ManageLocalFindGroupByname (Manage *manage,
                                   const gchar *name);

G_DEFINE_TYPE_WITH_CODE (Manage,manage, USER_GROUP_TYPE_ADMIN_SKELETON, 
                         G_ADD_PRIVATE (Manage) G_IMPLEMENT_INTERFACE (
                         USER_GROUP_TYPE_ADMIN, manage_user_group_admin_iface_init));

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Manage, g_object_unref)
/*
static void DbusPrintf (GDBusMethodInvocation *Invocation,
                        gint                   ErrorCode,
                        const gchar           *format,
                        ...)
{
    va_list args;
    g_autofree gchar *Message = NULL;

    va_start (args, format);
    Message = g_strdup_vprintf (format, args);
    va_end (args);
   // g_dbus_method_invocation_return_error (Invocation, ERROR, ErrorCode, "%s", Message);
}
*/
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
/*        struct passwd *pwent = getpwnam (grent->gr_name);
        
		if (pwent && pwent->pw_gid == grent->gr_gid)
                return TRUE;
*/
        return FALSE;
}
static void LoadGroupEntries (GHashTable *groups,
				              GroupEntryGeneratorFunc EntryGenerator,
							  Manage *manage)
{
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
    GHashTableIter iter;
    gpointer name,value;
	
	manage->priv = MANAGE_GET_PRIVATE (manage);
    GroupsHashTable = CreateGroupsHashTable ();
	LoadGroupEntries(GroupsHashTable, entry_generator_fgetgrent,manage);
    manage->priv->GroupsHashTable = GroupsHashTable;
	g_hash_table_iter_init (&iter, GroupsHashTable);
	
    while (g_hash_table_iter_next (&iter, &name,&value)) 
	{
    	RegisterGroup (manage,value);
  	}


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
    manage->priv->GroupsHashTable = CreateGroupsHashTable();
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
static void manage_finalize (GObject *object)
{
        ManagePrivate *priv;
        Manage *manage;

        manage = MANAGE (object);
        priv = MANAGE_GET_PRIVATE (manage);;

        if (priv->BusConnection != NULL)
                g_object_unref (priv->BusConnection);

}
static void get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    switch (prop_id) 
    {
        case PROP_MANAGE_VERSION:
            g_value_set_string (value, VERSION);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    switch (prop_id) 
    {
        case PROP_MANAGE_VERSION:
            g_assert_not_reached ();
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void manage_class_init (ManageClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = manage_finalize;
    object_class->get_property = get_property;
    object_class->set_property = set_property;

    g_object_class_override_property (object_class,
                                      PROP_MANAGE_VERSION,
									  "daemon-version");
										  
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
    manage->priv->Authority = polkit_authority_get_sync (NULL, &error);
    if (manage->priv->Authority == NULL) 
    {
        if (error != NULL)
        {    
            g_print ("error getting polkit authority: %s", error->message);
        	g_error_free(error);
        }    
        return -1;
    }

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
typedef struct 
{
    Manage *manage;
    Group  *group;
    AuthorizedCallback Authorized_cb;
    GDBusMethodInvocation *Invocation;
    gpointer data;
    GDestroyNotify DestroyNotify;
} CheckAuthData;

static void CheckAuthDataFree (CheckAuthData *data)
{
    g_object_unref (data->manage);
    if (data->group)
        g_object_unref (data->group);

    if (data->DestroyNotify)
        (*data->DestroyNotify) (data->data);

    g_free (data);
}
static void CheckAuth_cb (PolkitAuthority *Authority,
                          GAsyncResult    *res,
                          gpointer         data)
{
    CheckAuthData *cad = data;
    PolkitAuthorizationResult *result;
    GError *error = NULL;
    gboolean is_authorized = FALSE;

    result = polkit_authority_check_authorization_finish (Authority, res, &error);
    if (error) 
    {
        //DbusPrintf (cad->Invocation, ERROR_PERMISSION_DENIED, "Not authorized: %s", error->message);
        g_error_free(error);
    }
    else 
    {
        if (polkit_authorization_result_get_is_authorized (result)) 
        {
            is_authorized = TRUE;
        }
        else if (polkit_authorization_result_get_is_challenge (result)) 
        {
            //throw_error (cad->context, ERROR_PERMISSION_DENIED, "Authentication is required");
        }
        else 
        {
            //throw_error (cad->context, ERROR_PERMISSION_DENIED, "Not authorized");
        }

        g_object_unref (result);
    }

    if (is_authorized) 
    {
        (* cad->Authorized_cb) (cad->manage,
                                    cad->group,
                                    cad->Invocation,
                                    cad->data);
    }

    CheckAuthDataFree (data);
}
void LocalCheckAuthorization(Manage                *manage,
                             Group                 *group,
                             const gchar           *ActionFile,
                             gboolean               AllowInteraction,
                             AuthorizedCallback     Authorized_cb,
                             GDBusMethodInvocation *Invocation,
                             gpointer               Authorized_cb_data,
                             GDestroyNotify         DestroyNotify)
{
    ManagePrivate *priv = MANAGE_GET_PRIVATE (manage);
    CheckAuthData *data;
    PolkitSubject *subject;
    PolkitCheckAuthorizationFlags flags;

    data = g_new0 (CheckAuthData, 1);
    data->manage = g_object_ref (manage);
    if (group)
    {    
        data->group = g_object_ref (group);
    }    
    data->Invocation = Invocation;
    data->Authorized_cb = Authorized_cb;
    data->data = Authorized_cb_data;
    data->DestroyNotify = DestroyNotify;

    subject = polkit_system_bus_name_new (g_dbus_method_invocation_get_sender (Invocation));

    flags = POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE;
    if (AllowInteraction)
    {    
        flags |= POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION;
    }
    polkit_authority_check_authorization (priv->Authority,
                                          subject,
                                          ActionFile,
                                          NULL,
                                          flags,
                                          NULL,
                                          (GAsyncReadyCallback) CheckAuth_cb,
                                          data);

    g_object_unref (subject);
}
typedef struct 
{
    gchar *NewGroupName;
} CreateGroupData;

static void CreateGroupDataFree (gpointer data)
{
    CreateGroupData *cd = data;
    g_free (cd->NewGroupName);
    g_free (cd);
}
static void CreateNewGroup_cb (Manage                *manage,
                               Group                 *g,
                               GDBusMethodInvocation *Invocation,
                               gpointer               data)

{
    CreateGroupData *cd = data;
    GError *error = NULL;
    Group *group;
    const gchar *argv[4];

    if (getgrnam (cd->NewGroupName) != NULL) 
    {
        g_print("A group with name '%s' already exists", cd->NewGroupName);
       // throw_error (context, ERROR_GROUP_EXISTS, "A group with name '%s' already exists", cd->group_name);
        return;
    }
    sys_log (Invocation, "create group '%s'", cd->NewGroupName);

    argv[0] = "/usr/sbin/groupadd";
    argv[1] = "--";
    argv[2] = cd->NewGroupName;
    argv[3] = NULL;

    if (!spawn_with_login_uid (Invocation, argv, &error)) 
    {
        printf("running '%s' failed: %s", argv[0], error->message);
        //throw_error (context, ERROR_FAILED, "running '%s' failed: %s", argv[0], error->message);
        g_error_free (error);
        return;
    }

    group = ManageLocalFindGroupByname (manage, cd->NewGroupName);
    user_group_admin_complete_create_group (NULL, Invocation, group_get_object_path (group));
}
static gboolean ManageCreateGroup (UserGroupAdmin *object,
                                   GDBusMethodInvocation *Invocation,
                                   const gchar *name)
{
    Manage *manage = (Manage *)object;
    CreateGroupData *data;
    
    data = g_new0 (CreateGroupData, 1);
    data->NewGroupName = g_strdup (name);
    LocalCheckAuthorization(manage,
                            NULL,
                           "org.group.admin.group-administration",
                            TRUE,
                            CreateNewGroup_cb,
                            Invocation,
                            data,
                            (GDestroyNotify)CreateGroupDataFree);

    return TRUE;
}    
static gboolean ManageDeleteGroup (UserGroupAdmin *object,
                                   GDBusMethodInvocation *Invocation,
                                   gint64 arg_id)
{
    return TRUE;

}    
 
static Group *ManageLocalFindGroupByid(Manage *manage,
                                       gid_t gid)
{
    ManagePrivate *priv = MANAGE_GET_PRIVATE (manage);
    Group *group;
    struct group *grent;

    grent = getgrgid(gid);
    if (grent == NULL) 
    {
        g_print ("unable to lookup gid %d",(int)gid);
        return NULL;
    }
    group = g_hash_table_lookup (priv->GroupsHashTable, grent->gr_name);
    if(group == NULL)
    {
        printf("Not Local Group\r\n");
    } 
   
    return group;
}
static gboolean ManageFindGRoupByid (UserGroupAdmin *object,
                                     GDBusMethodInvocation *invocation,
                                     gint64 gid)
{    
    Manage *manage = (Manage *)object;
    Group  *group;

    group = ManageLocalFindGroupByid (manage, gid);
    if (group) 
    {
        user_group_admin_complete_find_group_by_id(NULL,invocation,group_get_object_path (group));
    }
    else 
    {
        //DbusPrintf (invocation, ERROR_FAILED, "Failed to look up user with name %d.", gid);
    }

    return TRUE;
}    

static Group *ManageLocalFindGroupByname (Manage *manage,
                                   const gchar *name)
{
    ManagePrivate *priv = MANAGE_GET_PRIVATE (manage);
    Group *group;
    struct group *grent;

    grent = getgrnam (name);
    if (grent == NULL) 
    {
        g_print ("unable to lookup name %s: %s", name, g_strerror (errno));
        return NULL;
    }

    group = g_hash_table_lookup (priv->GroupsHashTable, grent->gr_name);
    if(group_get_local_group(group) == FALSE)
    {
        g_print("Not local group !!!"); 
    }    
    return group;
}

static gboolean ManageFindGroupByname(UserGroupAdmin *object,
                                      GDBusMethodInvocation *invocation,
                                      const gchar *name)
{
    Manage *manage = (Manage *)object;
    Group  *group;

    group = ManageLocalFindGroupByname (manage, name);
    if (group) 
    {
        user_group_admin_complete_find_group_by_name(NULL,invocation,group_get_object_path (group));
    }
    else 
    {
        //DbusPrintf (invocation, ERROR_FAILED, "Failed to look up user with name %s.", name);
    }

    return TRUE;
}    
 
static const gchar * ManageGetDammonVersion (UserGroupAdmin *object)
{
    return VERSION;
}    

static void manage_user_group_admin_iface_init (UserGroupAdminIface *iface)
{
    iface->handle_create_group =       ManageCreateGroup;
    iface->handle_delete_group =       ManageDeleteGroup;
    iface->handle_find_group_by_id =   ManageFindGRoupByid;
    iface->handle_find_group_by_name = ManageFindGroupByname;
    iface->get_daemon_version =        ManageGetDammonVersion;
}

