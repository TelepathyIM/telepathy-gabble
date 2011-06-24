
#include "config.h"

#include <string.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <glib.h>

#include "src/debug.h"
#include "src/presence.h"
#include "src/namespaces.h"

static void
big_test_of_doom (void)
{
  const gchar *resource;
  GabblePresence *presence;
  GabbleCapabilitySet *cap_set;
  time_t now = time (NULL);

  /* When we create a new presence, we know nothing about the contact in
   * question's presence.
   */
  presence = gabble_presence_new ();
  g_assert (GABBLE_PRESENCE_UNKNOWN == presence->status);
  g_assert (NULL == presence->status_message);

  /* offline presence from no resource: we now know something about this
   * contact's presence.
   */
  g_assert (TRUE == gabble_presence_update (presence, NULL,
    GABBLE_PRESENCE_OFFLINE, NULL, 0, NULL, now));
  g_assert (GABBLE_PRESENCE_OFFLINE == presence->status);
  g_assert (NULL == presence->status_message);

  /* offline presence from unknown resource: no change */
  g_assert (FALSE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_OFFLINE, NULL, 0, NULL, now));
  /* available presence from unknown resource: change */
  g_assert (TRUE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, NULL, 0, NULL, now));

  /* accumulated presence has changed; status message unchanged */
  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (NULL == presence->status_message);

  /* available presence again; no change */
  g_assert (FALSE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, NULL, 0, NULL, now));
  /* available presence again, but with status message: change */
  g_assert (TRUE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, "status message", 0, NULL, now));

  /* accumulated presence unchanged; status message changed */
  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (0 == strcmp ("status message", presence->status_message));

  /* same presence again; no change */
  g_assert (FALSE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, "status message", 0, NULL, now));

  /* time passes */
  now++;

  /* presence from different resource, but equal present-ness and equal
   * status message; unchanged */
  g_assert (FALSE == gabble_presence_update (presence, "bar",
    GABBLE_PRESENCE_AVAILABLE, "status message", 0, NULL, now));

  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (0 == strcmp ("status message", presence->status_message));

  /* but if we were to make a voip call, we would prefer the newer one */
  g_assert (0 == strcmp ("bar",
        gabble_presence_pick_resource_by_caps (presence, 0, NULL, NULL)));

  /* mountain ranges form */
  now++;

  /* presence from different resource, but equal present-ness and different
   * status message; changed */
  g_assert (TRUE == gabble_presence_update (presence, "baz",
    GABBLE_PRESENCE_AVAILABLE, "dingbats", 0, NULL, now));

  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (0 == strcmp ("dingbats", presence->status_message));

  /* babies are born */
  now++;

  /* presence with higher priority; presence and message changed */
  g_assert (TRUE == gabble_presence_update (presence, "bar",
    GABBLE_PRESENCE_AVAILABLE, "dingoes", 1, NULL, now));

  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (0 == strcmp ("dingoes", presence->status_message));

  /* third-world dictators are deposed */
  now++;

  /* now foo is newer, so the next voip call would prefer that */
  g_assert (FALSE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, "status message", 0, NULL, now));
  g_assert (0 == strcmp ("foo",
        gabble_presence_pick_resource_by_caps (presence, 0, NULL, NULL)));

  /* portal 2 is released */
  now++;

  /* presence from first resource with greated present-ness: change */
  g_assert (TRUE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_CHAT, "status message", 0, NULL, now));

  /* seasons change */
  now++;

  /* make bar be the latest presence: no change, since foo is more present */
  g_assert (FALSE == gabble_presence_update (presence, "bar",
    GABBLE_PRESENCE_AVAILABLE, "dingoes", 1, NULL, now));

  /* we still prefer foo for the voip calls, because it's more present */
  g_assert (0 == strcmp ("foo",
        gabble_presence_pick_resource_by_caps (presence, 0, NULL, NULL)));

  g_assert (GABBLE_PRESENCE_CHAT == presence->status);
  g_assert (0 == strcmp ("status message", presence->status_message));

  /* no resource has the Google voice cap */
  resource = gabble_presence_pick_resource_by_caps (presence, 0,
      gabble_capability_set_predicate_has, NS_GOOGLE_FEAT_VOICE);
  g_assert (NULL == resource);

  /* give voice cap to second resource, but make priority negative */
  g_assert (FALSE == gabble_presence_update (presence, "bar",
    GABBLE_PRESENCE_AVAILABLE, "dingoes", -1, NULL, now));
  cap_set = gabble_capability_set_new ();
  gabble_capability_set_add (cap_set, NS_GOOGLE_FEAT_VOICE);
  gabble_presence_set_capabilities (presence, "bar", cap_set, 0);
  gabble_capability_set_free (cap_set);

  /* no resource with non-negative priority has the Google voice cap */
  resource = gabble_presence_pick_resource_by_caps (presence, 0,
      gabble_capability_set_predicate_has, NS_GOOGLE_FEAT_VOICE);
  g_assert (NULL == resource);

  /* give voice cap to first resource */
  cap_set = gabble_capability_set_new ();
  gabble_capability_set_add (cap_set, NS_GOOGLE_FEAT_VOICE);
  gabble_presence_set_capabilities (presence, "foo", cap_set, 0);
  gabble_capability_set_free (cap_set);

  /* resource has voice cap */
  resource = gabble_presence_pick_resource_by_caps (presence, 0,
      gabble_capability_set_predicate_has, NS_GOOGLE_FEAT_VOICE);
  g_assert (0 == strcmp ("foo", resource));

  /* presence turns up from null resource; it trumps other presence regardless
   * of whether status is more present or not */
  g_assert (TRUE == gabble_presence_update (presence, NULL,
    GABBLE_PRESENCE_OFFLINE, "gone", 0, NULL, now));
  g_assert (GABBLE_PRESENCE_OFFLINE == presence->status);
  g_assert (0 == strcmp ("gone", presence->status_message));

  /* caps are gone too */
  resource = gabble_presence_pick_resource_by_caps (presence, 0,
      gabble_capability_set_predicate_has, NS_GOOGLE_FEAT_VOICE);
  g_assert (NULL == resource);

  g_object_unref (presence);
}

/*
 * prefer_higher_priority_resources:
 *
 * This is a regression test for a bug which didn't actually happen, but would
 * have happened (and would not have been caught by the other tests in this
 * file) had a series of apparently-tautological if(){}s been turned into
 * if(){}else if(){} as suggested in a review comment
 * <https://bugs.freedesktop.org/show_bug.cgi?id=32139#c3>.
 */
static void
prefer_higher_priority_resources (void)
{
  GabblePresence *presence = gabble_presence_new ();
  time_t now = time (NULL);

  /* 'foo' and 'bar' are equally available, at the same time, but bar has a
   * lower priority.
   */
  gabble_presence_update (presence, "foo", GABBLE_PRESENCE_AVAILABLE, "foo", 10,
      NULL, now);
  gabble_presence_update (presence, "bar", GABBLE_PRESENCE_AVAILABLE, "bar", 5,
      NULL, now);

  /* We should be sure to prefer "foo"'s status message to "bar"'s.
   */
  g_assert_cmpstr (presence->status_message, ==, "foo");

  g_object_unref (presence);
}

int main (int argc, char **argv)
{
  int ret;

  g_type_init ();
  gabble_capabilities_init (NULL);
  gabble_debug_set_flags_from_env ();

  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/presence/big-test-of-doom", big_test_of_doom);
  g_test_add_func ("/presence/prefer-higher-priority-resources",
      prefer_higher_priority_resources);

  ret = g_test_run ();

  gabble_capabilities_finalize (NULL);
  gabble_debug_free ();

  return ret;
}
