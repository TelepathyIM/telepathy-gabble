#include "config.h"

#include <gabble/gabble.h>

static void
test_remap (GQuark domain,
    gint code,
    const gchar *message,
    gint exp_code,
    const gchar *exp_message)
{
  GError *error = NULL;
  GError wocky_error = { domain, code, (gchar *) message };

  gabble_set_tp_error_from_wocky (&wocky_error, &error);
  g_assert (error != NULL);
  g_assert_cmpstr (g_quark_to_string (error->domain), ==,
        g_quark_to_string (TP_ERRORS));
  g_assert_cmpint (error->code, ==, exp_code);

  if (exp_message != NULL)
    g_assert_cmpstr (error->message, ==, exp_message);

  g_error_free (error);
}

#define TEST_XMPP(nick, n, message, exp_code) \
  test_remap (WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_ ## nick, message, \
      exp_code, \
      "WOCKY_XMPP_ERROR_" #nick " (#" #n "): " \
      message)

int
main (void)
{
  g_type_init ();

  test_remap (WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_FORBIDDEN, "computer says no",
      TP_ERROR_PERMISSION_DENIED,
      "WOCKY_XMPP_ERROR_FORBIDDEN (#8): computer says no");
  /* shorthand version of the above */
  TEST_XMPP (FORBIDDEN, 8, "computer says no", TP_ERROR_PERMISSION_DENIED);

  /* more mappings */
  TEST_XMPP (RESOURCE_CONSTRAINT, 19, "shut up!", TP_ERROR_SERVICE_BUSY);
  TEST_XMPP (FEATURE_NOT_IMPLEMENTED, 20, "what?", TP_ERROR_NOT_AVAILABLE);

  /* out-of-range is handled gracefully */
  test_remap (WOCKY_XMPP_ERROR, 12345678, "lalala I am broken",
      TP_ERROR_NOT_AVAILABLE,
      "unknown WockyXmppError code (#12345678): lalala I am broken");

  /* GIOError is NetworkError, for now */
  test_remap (G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "network fail",
      TP_ERROR_NETWORK_ERROR,
      "G_IO_ERROR_TIMED_OUT (#24): network fail");

  /* other domains do something basically sane (the message will be something
   * vaguely helpful) */
  test_remap (G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE,
      "what's this doing here?",
      TP_ERROR_NOT_AVAILABLE, NULL);

  return 0;
}

