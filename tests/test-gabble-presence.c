
#include <string.h>

#include <glib.h>
#include <presence.h>

int main (int argc, char **argv)
{
  const gchar *resource;
  gchar *dump;
  GabblePresence *presence;

  g_type_init ();

  presence = gabble_presence_new ();
  g_assert (GABBLE_PRESENCE_OFFLINE == presence->status);
  g_assert (NULL == presence->status_message);

  /* offline presence from unknown resource: no change */
  g_assert (FALSE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_OFFLINE, NULL, 0));
  /* available presence from unknown resource: change */
  g_assert (TRUE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, NULL, 0));

  /* accumulated presence has changed; status message unchanged */
  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (NULL == presence->status_message);

  /* available presence again; no change */
  g_assert (FALSE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, NULL, 0));
  /* available presence again, but with status message: change */
  g_assert (TRUE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, "status message", 0));

  /* accumulated presence unchanged; status message changed */
  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (0 == strcmp ("status message", presence->status_message));

  /* same presence again; no change */
  g_assert (FALSE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, "status message", 0));

  /* presence from different resource, but equal present-ness; unchanged */
  g_assert (FALSE == gabble_presence_update (presence, "bar",
    GABBLE_PRESENCE_AVAILABLE, "dingoes", 0));

  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (0 == strcmp ("status message", presence->status_message));

  /* presence with higher priority; presence and message changed */
  g_assert (TRUE == gabble_presence_update (presence, "bar",
    GABBLE_PRESENCE_AVAILABLE, "dingoes", 1));

  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (0 == strcmp ("dingoes", presence->status_message));

  /* presence from first resource with greated present-ness: change */
  g_assert (TRUE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_CHAT, "status message", 0));

  g_assert (GABBLE_PRESENCE_CHAT == presence->status);
  g_assert (0 == strcmp ("status message", presence->status_message));

  /* no resource has the Google voice cap */
  resource = gabble_presence_pick_resource_by_caps (presence,
    PRESENCE_CAP_GOOGLE_VOICE);
  g_assert (NULL == resource);

  /* give voice cap to second resource, but make priority negative */
  g_assert (FALSE == gabble_presence_update (presence, "bar",
    GABBLE_PRESENCE_AVAILABLE, "dingoes", -1));
  gabble_presence_set_capabilities (presence, "bar",
      PRESENCE_CAP_GOOGLE_VOICE, 0);

  dump = gabble_presence_dump (presence);
  g_assert (0 == strcmp (dump,
    "nickname: (null)\n"
    "accumulated status: 6\n"
    "accumulated status msg: status message\n"
    "accumulated capabilities: 2\n"
    "kept while unavailable: 0\n"
    "resources:\n"
    "  foo\n"
    "    capabilities: 0\n"
    "    status: 6\n"
    "    status msg: status message\n"
    "    priority: 0\n"
    "  bar\n"
    "    capabilities: 2\n"
    "    status: 5\n"
    "    status msg: dingoes\n"
    "    priority: -1\n"));
  g_free (dump);

  /* no resource with non-negative priority has the Google voice cap */
  resource = gabble_presence_pick_resource_by_caps (presence,
    PRESENCE_CAP_GOOGLE_VOICE);
  g_assert (NULL == resource);

  /* give voice cap to first resource */
  gabble_presence_set_capabilities (presence, "foo",
      PRESENCE_CAP_GOOGLE_VOICE, 0);

  /* resource has voice cap */
  resource = gabble_presence_pick_resource_by_caps (presence,
    PRESENCE_CAP_GOOGLE_VOICE);
  g_assert (0 == strcmp ("foo", resource));

  /* presence turns up from null resource; it trumps other presence regardless
   * of whether status is more present or not */
  g_assert (TRUE == gabble_presence_update (presence, NULL,
    GABBLE_PRESENCE_OFFLINE, "gone", 0));
  g_assert (GABBLE_PRESENCE_OFFLINE == presence->status);
  g_assert (0 == strcmp ("gone", presence->status_message));

  /* caps are gone too */
  resource = gabble_presence_pick_resource_by_caps (presence,
    PRESENCE_CAP_GOOGLE_VOICE);
  g_assert (NULL == resource);

  dump = gabble_presence_dump (presence);
  g_assert (0 == strcmp (dump,
    "nickname: (null)\n"
    "accumulated status: 0\n"
    "accumulated status msg: gone\n"
    "accumulated capabilities: 2\n"
    "kept while unavailable: 0\n"
    "resources:\n"
    "  (none)\n"));
  g_free (dump);


  return 0;
}

