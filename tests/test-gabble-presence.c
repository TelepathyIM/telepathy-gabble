
#include <string.h>

#include <glib.h>
#include <gabble-presence.h>

int main(int argc, char **argv)
{
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

  return 0;
}

