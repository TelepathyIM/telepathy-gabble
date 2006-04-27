
#ifndef __GABBLE_PRESENCE_H__
#define __GABBLE_PRESENCE_H__

#include <glib-object.h>

#include "gabble-connection.h"

G_BEGIN_DECLS

#define GABBLE_TYPE_PRESENCE gabble_presence_get_type()

#define GABBLE_PRESENCE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                                   GABBLE_TYPE_PRESENCE, GabblePresence))

#define GABBLE_PRESENCE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), \
                                GABBLE_TYPE_PRESENCE, GabblePresenceClass))

#define GABBLE_IS_PRESENCE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                                   GABBLE_TYPE_PRESENCE))

#define GABBLE_IS_PRESENCE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), \
                                GABBLE_TYPE_PRESENCE))

#define GABBLE_PRESENCE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), \
                                  GABBLE_TYPE_PRESENCE, GabblePresenceClass))

typedef enum {
    CAP_NONE = 0,
    CAP_VOICE = 1 << 0,
    CAP_VIDEO = 1 << 1,
} GabblePresenceCapability;

typedef struct _GabblePresence GabblePresence;

struct _GabblePresence {
    GObject parent;
    GabblePresenceId status;
    gchar *status_name;
    gchar *status_message;
    gpointer priv;
};

typedef struct _GabblePresenceClass GabblePresenceClass;

struct _GabblePresenceClass {
    GObjectClass parent_class;
};

GType gabble_presence_get_type (void);

GabblePresence* gabble_presence_new (void);

/*
GabblePresenceId gabble_presence_get_status(GabblePresence *presence, GabbleHandle handle);
const gchar *gabble_presence_get_status_message(GabblePresence *presence, GabbleHandle handle);
const gchar *gabble_presence_get_status_name(GabblePresence *presence, GabbleHandle handle);
*/

gboolean gabble_presence_get_supports_voice (GabblePresence *presence);

void gabble_presence_update (GabblePresence *presence, const gchar *resource, GabblePresenceId status, const gchar *status_message);

G_END_DECLS

#endif /* __GABBLE_PRESENCE_H__ */

