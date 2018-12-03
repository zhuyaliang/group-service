#ifndef __GROUP_SERVER__
#define __GROUP_SERVER__

#include "group-generated.h"
#include "group.h"

typedef struct GroupManage
{
    GDBusConnection *BusConnection;
    userGroup       *Skeleton;
    GHashTable      *GroupsHashTable;
    GFileMonitor    *PasswdMonitor;
    GFileMonitor    *ShadowMonitor;
    GFileMonitor    *GroupMonitor;
    guint            ReloadId;
}GroupManage;

void StartLoadGroup(GroupManage *GM);

#endif
