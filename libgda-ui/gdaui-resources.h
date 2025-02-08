#ifndef __RESOURCE__gdaui_H__
#define __RESOURCE__gdaui_H__

#include <gio/gio.h>

extern GResource *_gdaui_get_resource (void);

extern void _gdaui_register_resource (void);
extern void _gdaui_unregister_resource (void);

#endif
