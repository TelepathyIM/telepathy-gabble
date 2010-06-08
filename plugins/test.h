#include <glib-object.h>

#include <gio/gio.h>
#include <wocky/wocky-session.h>
#include <gabble/gabble.h>

/* Plugin */
typedef struct _TestPluginClass TestPluginClass;
typedef struct _TestPlugin TestPlugin;

struct _TestPluginClass {
    GObjectClass parent;
};

struct _TestPlugin {
    GObject parent;
};

GType test_plugin_get_type (void);

#define TEST_TYPE_PLUGIN \
  (test_plugin_get_type ())
#define TEST_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TEST_TYPE_PLUGIN, TestPlugin))
#define TEST_PLUGIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TEST_TYPE_PLUGIN, \
                           TestPluginClass))
#define TEST_IS_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TEST_TYPE_PLUGIN))
#define TEST_IS_PLUGIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TEST_TYPE_PLUGIN))
#define TEST_PLUGIN_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TEST_TYPE_PLUGIN, \
                              TestPluginClass))

/* Sidecar */
typedef struct _TestSidecarClass TestSidecarClass;
typedef struct _TestSidecar TestSidecar;

struct _TestSidecarClass {
    GObjectClass parent;
};

struct _TestSidecar {
    GObject parent;
};

GType test_sidecar_get_type (void);

#define TEST_TYPE_SIDECAR \
  (test_sidecar_get_type ())
#define TEST_SIDECAR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TEST_TYPE_SIDECAR, TestSidecar))
#define TEST_SIDECAR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TEST_TYPE_SIDECAR, \
                           TestSidecarClass))
#define TEST_IS_SIDECAR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TEST_TYPE_SIDECAR))
#define TEST_IS_SIDECAR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TEST_TYPE_SIDECAR))
#define TEST_SIDECAR_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TEST_TYPE_SIDECAR, \
                              TestSidecarClass))

/* Sidecar with properties */
typedef struct _TestSidecarPropsClass TestSidecarPropsClass;
typedef struct _TestSidecarProps TestSidecarProps;

struct _TestSidecarPropsClass {
    GObjectClass parent;
};

struct _TestSidecarProps {
    GObject parent;
    GHashTable *props;
};

GType test_sidecar_props_get_type (void);

#define TEST_TYPE_SIDECAR_PROPS \
  (test_sidecar_props_get_type ())
#define TEST_SIDECAR_PROPS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TEST_TYPE_SIDECAR_PROPS, TestSidecarProps))
#define TEST_SIDECAR_PROPS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TEST_TYPE_SIDECAR_PROPS, \
                           TestSidecarPropsClass))
#define TEST_IS_SIDECAR_PROPS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TEST_TYPE_SIDECAR_PROPS))
#define TEST_IS_SIDECAR_PROPS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TEST_TYPE_SIDECAR_PROPS))
#define TEST_SIDECAR_PROPS_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TEST_TYPE_SIDECAR_PROPS, \
                              TestSidecarPropsClass))

/* Sidecar which sends an IQ and waits for a reply before being ready. */
typedef struct _TestSidecarIQClass TestSidecarIQClass;
typedef struct _TestSidecarIQ TestSidecarIQ;

struct _TestSidecarIQClass {
    GObjectClass parent;
};

struct _TestSidecarIQ {
    GObject parent;
    GSimpleAsyncResult *result;
    WockySession *session;
    GabbleConnection *connection;
};

GType test_sidecar_iq_get_type (void);

#define TEST_TYPE_SIDECAR_IQ \
  (test_sidecar_iq_get_type ())
#define TEST_SIDECAR_IQ(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TEST_TYPE_SIDECAR_IQ, TestSidecarIQ))
#define TEST_SIDECAR_IQ_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TEST_TYPE_SIDECAR_IQ, \
                           TestSidecarIQClass))
#define TEST_IS_SIDECAR_IQ(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TEST_TYPE_SIDECAR_IQ))
#define TEST_IS_SIDECAR_IQ_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TEST_TYPE_SIDECAR_IQ))
#define TEST_SIDECAR_IQ_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TEST_TYPE_SIDECAR_IQ, \
                              TestSidecarIQClass))
