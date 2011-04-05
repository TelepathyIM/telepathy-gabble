
#include "config.h"

#include <string.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <glib.h>

#include "src/debug.h"
#include "src/presence.h"
#include "src/namespaces.h"

static gboolean
predicate_true (const GabbleCapabilitySet *set,
    gconstpointer unused G_GNUC_UNUSED)
{
  return TRUE;
}

static void
big_test_of_doom (void)
{
  const gchar *resource;
  GabblePresence *presence;
  GabbleCapabilitySet *cap_set;

  presence = gabble_presence_new ();
  g_assert (GABBLE_PRESENCE_OFFLINE == presence->status);
  g_assert (NULL == presence->status_message);

  /* offline presence from unknown resource: no change */
  g_assert (FALSE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_OFFLINE, NULL, 0, NULL));
  /* available presence from unknown resource: change */
  g_assert (TRUE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, NULL, 0, NULL));

  /* accumulated presence has changed; status message unchanged */
  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (NULL == presence->status_message);

  /* available presence again; no change */
  g_assert (FALSE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, NULL, 0, NULL));
  /* available presence again, but with status message: change */
  g_assert (TRUE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, "status message", 0, NULL));

  /* accumulated presence unchanged; status message changed */
  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (0 == strcmp ("status message", presence->status_message));

  /* same presence again; no change */
  g_assert (FALSE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, "status message", 0, NULL));

  /* sleep a while so the next resource will have different timestamp */
  sleep (1);

  /* presence from different resource, but equal present-ness and equal
   * status message; unchanged */
  g_assert (FALSE == gabble_presence_update (presence, "bar",
    GABBLE_PRESENCE_AVAILABLE, "status message", 0, NULL));

  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (0 == strcmp ("status message", presence->status_message));

  /* but if we were to make a voip call, we would prefer the newer one */
  g_assert (0 == strcmp ("bar",
        gabble_presence_pick_resource_by_caps (presence,
            DEVICE_AGNOSTIC,
            predicate_true, NULL)));

  /* sleep a while so the next resource will have different timestamp */
  sleep (1);

  /* presence from different resource, but equal present-ness and different
   * status message; changed */
  g_assert (TRUE == gabble_presence_update (presence, "baz",
    GABBLE_PRESENCE_AVAILABLE, "dingbats", 0, NULL));

  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (0 == strcmp ("dingbats", presence->status_message));

  /* sleep a while so the next resource will have different timestamp */
  sleep (1);

  /* presence with higher priority; presence and message changed */
  g_assert (TRUE == gabble_presence_update (presence, "bar",
    GABBLE_PRESENCE_AVAILABLE, "dingoes", 1, NULL));

  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (0 == strcmp ("dingoes", presence->status_message));

  /* sleep a while so the next resource will have different timestamp */
  sleep (1);

  /* now foo is newer, so the next voip call would prefer that */
  g_assert (FALSE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, "status message", 0, NULL));
  g_assert (0 == strcmp ("foo",
        gabble_presence_pick_resource_by_caps (presence,
            DEVICE_AGNOSTIC,
            predicate_true, NULL)));

  /* sleep a while so the next resource will have different timestamp */
  sleep (1);

  /* presence from first resource with greated present-ness: change */
  g_assert (TRUE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_CHAT, "status message", 0, NULL));

  /* sleep a while so the next resource will have different timestamp */
  sleep (1);

  /* make bar be the latest presence: no change, since foo is more present */
  g_assert (FALSE == gabble_presence_update (presence, "bar",
    GABBLE_PRESENCE_AVAILABLE, "dingoes", 1, NULL));

  /* we still prefer foo for the voip calls, because it's more present */
  g_assert (0 == strcmp ("foo",
        gabble_presence_pick_resource_by_caps (presence,
            DEVICE_AGNOSTIC,
            predicate_true, NULL)));

  g_assert (GABBLE_PRESENCE_CHAT == presence->status);
  g_assert (0 == strcmp ("status message", presence->status_message));

  /* no resource has the Google voice cap */
  resource = gabble_presence_pick_resource_by_caps (presence,
      DEVICE_AGNOSTIC,
      gabble_capability_set_predicate_has, NS_GOOGLE_FEAT_VOICE);
  g_assert (NULL == resource);

  /* give voice cap to second resource, but make priority negative */
  g_assert (FALSE == gabble_presence_update (presence, "bar",
    GABBLE_PRESENCE_AVAILABLE, "dingoes", -1, NULL));
  cap_set = gabble_capability_set_new ();
  gabble_capability_set_add (cap_set, NS_GOOGLE_FEAT_VOICE);
  gabble_presence_set_capabilities (presence, "bar", cap_set, 0);
  gabble_capability_set_free (cap_set);

  /* no resource with non-negative priority has the Google voice cap */
  resource = gabble_presence_pick_resource_by_caps (presence,
      DEVICE_AGNOSTIC,
      gabble_capability_set_predicate_has, NS_GOOGLE_FEAT_VOICE);
  g_assert (NULL == resource);

  /* give voice cap to first resource */
  cap_set = gabble_capability_set_new ();
  gabble_capability_set_add (cap_set, NS_GOOGLE_FEAT_VOICE);
  gabble_presence_set_capabilities (presence, "foo", cap_set, 0);
  gabble_capability_set_free (cap_set);

  /* resource has voice cap */
  resource = gabble_presence_pick_resource_by_caps (presence,
      DEVICE_AGNOSTIC,
      gabble_capability_set_predicate_has, NS_GOOGLE_FEAT_VOICE);
  g_assert (0 == strcmp ("foo", resource));

  /* presence turns up from null resource; it trumps other presence regardless
   * of whether status is more present or not */
  g_assert (TRUE == gabble_presence_update (presence, NULL,
    GABBLE_PRESENCE_OFFLINE, "gone", 0, NULL));
  g_assert (GABBLE_PRESENCE_OFFLINE == presence->status);
  g_assert (0 == strcmp ("gone", presence->status_message));

  /* caps are gone too */
  resource = gabble_presence_pick_resource_by_caps (presence,
      DEVICE_AGNOSTIC,
      gabble_capability_set_predicate_has, NS_GOOGLE_FEAT_VOICE);
  g_assert (NULL == resource);

  g_object_unref (presence);
}

int main (int argc, char **argv)
{
  int ret;

  g_type_init ();
  gabble_capabilities_init (NULL);

  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/presence/big-test-of-doom", big_test_of_doom);

  ret = g_test_run ();

  gabble_capabilities_finalize (NULL);
  /* The capabilities code will have initialized the debugging infrastructure
   */
  gabble_debug_free ();

  return ret;
}
