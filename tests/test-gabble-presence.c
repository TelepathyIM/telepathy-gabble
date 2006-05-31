
#include <string.h>

#include <glib.h>
#include <gabble-presence.h>

int main(int argc, char **argv)
{
  const gchar *resource;
  GabblePresence *presence;

  g_type_init();

  presence = gabble_presence_new();
  g_assert (GABBLE_PRESENCE_OFFLINE == presence->status);
  g_assert (NULL == presence->status_message);

  g_assert (FALSE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_OFFLINE, NULL, 0));
  g_assert (TRUE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, NULL, 0));

  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (NULL == presence->status_message);

  g_assert (FALSE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, NULL, 0));
  g_assert (TRUE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, "status message", 0));

  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (0 == strcmp ("status message", presence->status_message));

  g_assert (FALSE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_AVAILABLE, "status message", 0));

  g_assert (FALSE == gabble_presence_update (presence, "bar",
    GABBLE_PRESENCE_AVAILABLE, "dingoes", 0));

  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (0 == strcmp ("status message", presence->status_message));

  g_assert (TRUE == gabble_presence_update (presence, "bar",
    GABBLE_PRESENCE_AVAILABLE, "dingoes", 1));

  g_assert (GABBLE_PRESENCE_AVAILABLE == presence->status);
  g_assert (0 == strcmp ("dingoes", presence->status_message));

  g_assert (TRUE == gabble_presence_update (presence, "foo",
    GABBLE_PRESENCE_CHAT, "status message", 0));

  g_assert (GABBLE_PRESENCE_CHAT == presence->status);
  g_assert (0 == strcmp ("status message", presence->status_message));

  resource = gabble_presence_pick_resource_by_caps (presence,
    PRESENCE_CAP_GOOGLE_VOICE);
  g_assert (NULL == resource);

  return 0;
}

