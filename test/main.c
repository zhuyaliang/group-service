#include <stdio.h>
#include <gtk/gtk.h>
#include <group-service-1.0/gas/gas-group.h>
#include <group-service-1.0/gas/gas-group-manager.h>
 static void GroupTest (GasGroup *group,GasGroupManager *GroupManager)
{
    const char *name = NULL;
    gid_t gid;
    GError *error = NULL;
     name = gas_group_get_group_name(group);
    gid = gas_group_get_gid(group);
 	if(name == NULL)
	{
		printf("Failed to get group name !!!\r\n");
		return;
	}
	printf("group name %s gid %d include %d user \r\n",
					   name,
					          gid,
										 g_strv_length(gas_group_get_group_users(group)));	
	
}
 int main(void)
{
	GasGroupManager *GroupManager;
    GasGroup *group;
	GasGroup  *new_group;
	GError *error = NULL;
	GSList *list, *l;
	int i = 0;
	int count = 0;
 	GroupManager = gas_group_manager_get_default ();
	if(GroupManager == NULL)
	{
		printf("Failed initialization group !!!\r\n");
		return FALSE;
	}
	
	if( gas_group_manager_no_service(GroupManager) == TRUE)
	{
		printf("Query Service Failure Service !!!\r\n");
		return FALSE;
	}	
	
    list = gas_group_manager_list_groups (GroupManager);
	count = g_slist_length(list);
	if(count <= 0 )
	{
		printf("No group found !!!\r\n");
		return FALSE;
	}			
    printf("There are %d group\r\n",count);
 	new_group = gas_group_manager_create_group(GroupManager,
					                          "test-group-gas-21",
											   &error);      
	if(new_group == NULL)
	{
		if(error != NULL)
		{
			printf("Failed to create new group %s !!!\r\n",error->message);
			g_error_free (error);
		}	
		else
		{
			printf("Failed to create new group !!!\r\n");
		}			
		return FALSE;
	}
 	printf("Cretae new group %s success\r\n",gas_group_get_group_name(new_group));
	gas_group_add_user_group(new_group,"mouse");
	gas_group_remove_user_group(new_group,"mouse");         
	gas_group_set_group_name(new_group,"test-group-gas-22");
	printf("Change the group name to %s\r\n",gas_group_get_group_name(new_group));
    for(l = list; l ; l = l->next,i++)
    {
        group = l->data;
        GroupTest(group,GroupManager);
    }
   
    if(gas_group_manager_delete_group(GroupManager,new_group,&error) == FALSE)
	{
		if(error != NULL)
		{
			printf("Failed to delete old group %s !!!\r\n",error->message);
			g_error_free (error);
		}	
		else
		{
			printf("Failed to delete old group !!!\r\n");
		}			
		return FALSE;
	}
 
 	return TRUE;
}		
