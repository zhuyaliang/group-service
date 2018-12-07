#ifndef __GAS_GROUP_PRIVATE_H_
#define __GAS_GROUP_PRIVATE_H_

#include <pwd.h>

#include "gas-group.h"

G_BEGIN_DECLS

void           _gas_group_update_from_object_path   (GasGroup  *group,
                                                    const char *object_path);
G_END_DECLS

#endif
