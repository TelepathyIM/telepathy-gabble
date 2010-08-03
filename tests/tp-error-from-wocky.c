#include "config.h"

#include <gabble/gabble.h>

static void
test_remap (GQuark domain,
    gint code,
    const gchar *message,
    TpConnectionStatus prev_status,
    gint exp_code,
    const gchar *exp_message,
    TpConnectionStatusReason exp_reason)
{
  GError *error = NULL;
  GError wocky_error = { domain, code, (gchar *) message };
  TpConnectionStatusReason conn_reason;

  gabble_set_tp_conn_error_from_wocky (&wocky_error, prev_status,
      &conn_reason, &error);
  g_assert (error != NULL);
  g_assert_cmpstr (g_quark_to_string (error->domain), ==,
        g_quark_to_string (TP_ERRORS));
  g_assert_cmpint (error->code, ==, exp_code);
  g_assert_cmpint (conn_reason, ==, exp_reason);

  if (exp_message != NULL)
    g_assert_cmpstr (error->message, ==, exp_message);

  g_error_free (error);
}

#define TEST_XMPP(nick, n, message, exp_code, exp_reason) \
  test_remap (WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_ ## nick, message, \
      TP_CONNECTION_STATUS_CONNECTED, \
      exp_code, \
      "WOCKY_XMPP_ERROR_" #nick " (#" #n "): " \
      message, \
      exp_reason)

int
main (void)
{
  g_type_init ();

  test_remap (WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_FORBIDDEN, "computer says no",
      TP_CONNECTION_STATUS_CONNECTED,
      TP_ERROR_PERMISSION_DENIED,
      "WOCKY_XMPP_ERROR_FORBIDDEN (#8): computer says no",
      TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED);
  /* shorthand version of the above */
  TEST_XMPP (FORBIDDEN, 8, "computer says no", TP_ERROR_PERMISSION_DENIED,
      TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED);

  /* more mappings */
  TEST_XMPP (RESOURCE_CONSTRAINT, 19, "shut up!", TP_ERROR_SERVICE_BUSY,
      TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
  TEST_XMPP (FEATURE_NOT_IMPLEMENTED, 20, "what?", TP_ERROR_NOT_AVAILABLE,
      TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);

  /* ConnectionReplaced, AlreadyConnected are mapped depending on the
   * connection status */
  test_remap (WOCKY_XMPP_STREAM_ERROR, WOCKY_XMPP_STREAM_ERROR_CONFLICT,
      "go away",
      TP_CONNECTION_STATUS_CONNECTED,
      TP_ERROR_CONNECTION_REPLACED,
      "WOCKY_XMPP_STREAM_ERROR_CONFLICT (#2): go away",
      TP_CONNECTION_STATUS_REASON_NAME_IN_USE);
  test_remap (WOCKY_XMPP_STREAM_ERROR, WOCKY_XMPP_STREAM_ERROR_CONFLICT,
      "no you don't",
      TP_CONNECTION_STATUS_CONNECTING,
      TP_ERROR_ALREADY_CONNECTED,
      "WOCKY_XMPP_STREAM_ERROR_CONFLICT (#2): no you don't",
      TP_CONNECTION_STATUS_REASON_NAME_IN_USE);

  /* out-of-range is handled gracefully */
  test_remap (WOCKY_XMPP_ERROR, 12345678, "lalala I am broken",
      TP_CONNECTION_STATUS_CONNECTED,
      TP_ERROR_NOT_AVAILABLE,
      "unknown WockyXmppError code (#12345678): lalala I am broken",
      TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);

  /* GIOError is NetworkError, for now */
  test_remap (G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "network fail",
      TP_CONNECTION_STATUS_CONNECTED,
      TP_ERROR_NETWORK_ERROR,
      "G_IO_ERROR_TIMED_OUT (#24): network fail",
      TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);

  /* other domains do something basically sane (the message will be something
   * vaguely helpful) */
  test_remap (G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE,
      "what's this doing here?",
      TP_CONNECTION_STATUS_CONNECTED,
      TP_ERROR_NOT_AVAILABLE, NULL,
      TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);

  return 0;
}

