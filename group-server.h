#ifndef __GROUP_SERVER__
#define __GROUP_SERVER__

#include "user-group-generated.h"
#include "group.h"

typedef struct GroupManage
{
    GDBusConnection *BusConnection;
    userGroup       *Skeleton;
    GHashTable      *groups;
    GFileMonitor    *PasswdMonitor;
    GFileMonitor    *ShadowMonitor;
    GFileMonitor    *GroupMonitor;
    guint            ReloadId;
}GroupManage;

void StartLoadGroup(void);

#endif
