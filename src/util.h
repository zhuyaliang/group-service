#ifndef __UTIL_H__
#define __UTIL_H__

#include <glib.h>

G_BEGIN_DECLS

void sys_log (GDBusMethodInvocation *context,
              const gchar           *format,
                                     ...);

gboolean get_caller_uid (GDBusMethodInvocation *context, gint *uid);

gboolean spawn_with_login_uid (GDBusMethodInvocation  *context,
                               const gchar            *argv[],
                               GError                **error);

G_END_DECLS

#endif /* __UTIL_H__ */
