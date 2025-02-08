#ifndef __RESOURCE__jdbc_H__
#define __RESOURCE__jdbc_H__

#include <gio/gio.h>

extern GResource *_jdbc_get_resource (void);

extern void _jdbc_register_resource (void);
extern void _jdbc_unregister_resource (void);

#endif
