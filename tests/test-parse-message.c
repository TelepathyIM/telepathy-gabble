
#include "config.h"

#include <string.h>

#include <glib-object.h>

#include "src/util.h"
#include "src/message-util.h"

/* Test the most basic <message> possible. */
static gboolean
test1 (void)
{
  LmMessage *msg;
  gboolean ret;
  const gchar *from;
  time_t stamp;
  TpChannelTextMessageType type;
  TpChannelTextSendError send_error;
  TpDeliveryStatus delivery_status;
  const gchar *id;
  const gchar *body;
  gint state;

  msg = lm_message_build (NULL, LM_MESSAGE_TYPE_MESSAGE,
        '@', "id", "a867c060-bd3f-4ecc-a38f-3e306af48e4c",
        '@', "from", "foo@bar.com",
        NULL);
  ret = gabble_message_util_parse_incoming_message (
      msg, &from, &stamp, &type, &id, &body, &state, &send_error,
      &delivery_status);
  g_assert (ret == TRUE);
  g_assert (0 == strcmp (id, "a867c060-bd3f-4ecc-a38f-3e306af48e4c"));
  g_assert (0 == strcmp (from, "foo@bar.com"));
  g_assert (stamp == 0);
  g_assert (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE);
  g_assert (body == NULL);
  g_assert (state == -1);
  g_assert (send_error == GABBLE_TEXT_CHANNEL_SEND_NO_ERROR);
  lm_message_unref (msg);
  return TRUE;
}

/* A <message> with a simple body. Parsed as a NOTICE because it doesn't have
 * a 'type' attribute.
 */
static gboolean
test2 (void)
{
  LmMessage *msg;
  gboolean ret;
  const gchar *from;
  time_t stamp;
  TpChannelTextMessageType type;
  TpChannelTextSendError send_error;
  TpDeliveryStatus delivery_status;
  const gchar *id;
  const gchar *body;
  gint state;

  msg = lm_message_build (NULL, LM_MESSAGE_TYPE_MESSAGE,
        '@', "from", "foo@bar.com",
        '@', "id", "a867c060-bd3f-4ecc-a38f-3e306af48e4c",
        '(', "body", "hello", ')',
        NULL);
  ret = gabble_message_util_parse_incoming_message (
      msg, &from, &stamp, &type, &id, &body, &state, &send_error,
      &delivery_status);
  g_assert (ret == TRUE);
  g_assert (0 == strcmp (id, "a867c060-bd3f-4ecc-a38f-3e306af48e4c"));
  g_assert (0 == strcmp (from, "foo@bar.com"));
  g_assert (stamp == 0);
  g_assert (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE);
  g_assert (0 == strcmp (body, "hello"));
  g_assert (state == -1);
  g_assert (send_error == GABBLE_TEXT_CHANNEL_SEND_NO_ERROR);
  lm_message_unref (msg);
  return TRUE;
}

/* Simple type="chat" message. */
static gboolean
test3 (void)
{
  LmMessage *msg;
  gboolean ret;
  const gchar *from;
  time_t stamp;
  TpChannelTextMessageType type;
  TpChannelTextSendError send_error;
  TpDeliveryStatus delivery_status;
  const gchar *id;
  const gchar *body;
  gint state;

  msg = lm_message_build (NULL, LM_MESSAGE_TYPE_MESSAGE,
        '@', "from", "foo@bar.com",
        '@', "id", "a867c060-bd3f-4ecc-a38f-3e306af48e4c",
        '@', "type", "chat",
        '(', "body", "hello", ')',
        NULL);
  ret = gabble_message_util_parse_incoming_message (
      msg, &from, &stamp, &type, &id, &body, &state, &send_error,
      &delivery_status);
  g_assert (ret == TRUE);
  g_assert (0 == strcmp (id, "a867c060-bd3f-4ecc-a38f-3e306af48e4c"));
  g_assert (0 == strcmp (from, "foo@bar.com"));
  g_assert (stamp == 0);
  g_assert (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL);
  g_assert (0 == strcmp (body, "hello"));
  g_assert (state == -1);
  g_assert (send_error == GABBLE_TEXT_CHANNEL_SEND_NO_ERROR);
  lm_message_unref (msg);
  return TRUE;
}

/* A simple error. */
static gboolean
test_error (void)
{
  LmMessage *msg;
  gboolean ret;
  const gchar *from;
  time_t stamp;
  TpChannelTextMessageType type;
  TpChannelTextSendError send_error;
  TpDeliveryStatus delivery_status;
  const gchar *id;
  const gchar *body;
  gint state;

  msg = lm_message_build_with_sub_type (NULL, LM_MESSAGE_TYPE_MESSAGE,
      LM_MESSAGE_SUB_TYPE_ERROR,
      '@', "from", "foo@bar.com",
      '@', "id", "a867c060-bd3f-4ecc-a38f-3e306af48e4c",
      '@', "type", "error",
      '(', "error", "oops", ')',
      NULL);
  ret = gabble_message_util_parse_incoming_message (
      msg, &from, &stamp, &type, &id, &body, &state, &send_error,
      &delivery_status);
  g_assert (ret == TRUE);
  g_assert (0 == strcmp (id, "a867c060-bd3f-4ecc-a38f-3e306af48e4c"));
  g_assert (0 == strcmp (from, "foo@bar.com"));
  g_assert (stamp == 0);
  g_assert (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE);
  g_assert (body == NULL);
  g_assert (state == -1);
  g_assert (send_error == TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN);
  g_assert (delivery_status == TP_DELIVERY_STATUS_PERMANENTLY_FAILED);
  lm_message_unref (msg);
  return TRUE;
}

/* A more complicated error, described in XEP-0086 as a "simple error response".
 */
static gboolean
test_another_error (void)
{
  LmMessage *msg;
  gboolean ret;
  const gchar *from;
  time_t stamp;
  TpChannelTextMessageType type;
  TpChannelTextSendError send_error;
  TpDeliveryStatus delivery_status;
  const gchar *id;
  const gchar *body;
  gint state;
  const gchar *message = "Wherefore art thou, Romeo?";

  msg = lm_message_build_with_sub_type (NULL, LM_MESSAGE_TYPE_MESSAGE,
      LM_MESSAGE_SUB_TYPE_ERROR,
      '@', "to", "juliet@capulet.com/balcony",
      '@', "id", "a867c060-bd3f-4ecc-a38f-3e306af48e4c",
      '@', "from", "romeo@montague.net/garden",
      '@', "type", "error",
      '(', "body", message, ')',
      '(', "error", "",
        '@', "code", "404",
        '@', "type", "cancel",
        '(', "item-not-found", "",
          '@', "xmlns", "urn:ietf:params:xml:ns:xmpp-stanzas",
        ')',
      ')',
      NULL);
  ret = gabble_message_util_parse_incoming_message (
      msg, &from, &stamp, &type, &id, &body, &state, &send_error,
      &delivery_status);
  g_assert (ret == TRUE);
  g_assert (0 == strcmp (id, "a867c060-bd3f-4ecc-a38f-3e306af48e4c"));
  g_assert (0 == strcmp (from, "romeo@montague.net/garden"));
  g_assert (stamp == 0);
  g_assert (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE);
  g_assert (!tp_strdiff (body, message));
  g_assert (state == -1);
  g_assert (send_error == TP_CHANNEL_TEXT_SEND_ERROR_INVALID_CONTACT);
  g_assert (delivery_status == TP_DELIVERY_STATUS_PERMANENTLY_FAILED);
  lm_message_unref (msg);
  return TRUE;
}

/* One million, seven hundred seventy-one thousand, five hundred sixty-one
 * errors.
 */
static gboolean
test_yet_another_error (void)
{
  LmMessage *msg;
  gboolean ret;
  const gchar *from;
  time_t stamp;
  TpChannelTextMessageType type;
  TpChannelTextSendError send_error;
  TpDeliveryStatus delivery_status;
  const gchar *id;
  const gchar *body;
  gint state;
  const gchar *message = "Its trilling seems to have a tranquilizing effect on "
                         "the human nervous system.";

  msg = lm_message_build_with_sub_type (NULL, LM_MESSAGE_TYPE_MESSAGE,
      LM_MESSAGE_SUB_TYPE_ERROR,
      '@', "to", "spock@starfleet.us/Enterprise",
      '@', "id", "a867c060-bd3f-4ecc-a38f-3e306af48e4c",
      '@', "from", "other@starfleet.us/Enterprise",
      '@', "type", "error",
      '(', "body", message, ')',
      '(', "error", "",
        '@', "code", "404",
        '@', "type", "wait",
        '(', "recipient-unavailable", "",
          '@', "xmlns", "urn:ietf:params:xml:ns:xmpp-stanzas",
        ')',
      ')',
      NULL);
  ret = gabble_message_util_parse_incoming_message (
      msg, &from, &stamp, &type, &id, &body, &state, &send_error,
      &delivery_status);
  g_assert (ret == TRUE);
  g_assert (0 == strcmp (id, "a867c060-bd3f-4ecc-a38f-3e306af48e4c"));
  g_assert (0 == strcmp (from, "other@starfleet.us/Enterprise"));
  g_assert (stamp == 0);
  g_assert (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE);
  g_assert (!tp_strdiff (body, message));
  g_assert (state == -1);
  g_assert (send_error == TP_CHANNEL_TEXT_SEND_ERROR_OFFLINE);
  g_assert (delivery_status == TP_DELIVERY_STATUS_TEMPORARILY_FAILED);
  lm_message_unref (msg);
  return TRUE;
}

static gboolean
test_google_offline (void)
{
  LmMessage *msg;
  gboolean ret;
  const gchar *from;
  time_t stamp;
  TpChannelTextMessageType type;
  TpChannelTextSendError send_error;
  TpDeliveryStatus delivery_status;
  const gchar *id;
  const gchar *body;
  gint state;

  msg = lm_message_build (NULL, LM_MESSAGE_TYPE_MESSAGE,
      '@', "id", "a867c060-bd3f-4ecc-a38f-3e306af48e4c",
      '@', "from", "foo@bar.com",
      '(', "body", "hello", ')',
      '(', "x", "",
         '@', "xmlns", "jabber:x:delay",
         '@', "stamp", "20070927T13:24:14",
      ')',
      '(', "time", "",
         '@', "xmlns", "google:timestamp",
         '@', "ms", "1190899454656",
      ')',
      NULL);
  ret = gabble_message_util_parse_incoming_message (
      msg, &from, &stamp, &type, &id, &body, &state, &send_error,
      &delivery_status);
  g_assert (ret == TRUE);
  g_assert (0 == strcmp (id, "a867c060-bd3f-4ecc-a38f-3e306af48e4c"));
  g_assert (0 == strcmp (from, "foo@bar.com"));
  g_assert (stamp == 1190899454);
  g_assert (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL);
  g_assert (0 == strcmp (body, "hello"));
  g_assert (state == -1);
  g_assert (send_error == GABBLE_TEXT_CHANNEL_SEND_NO_ERROR);
  lm_message_unref (msg);
  return TRUE;
}

int
main (void)
{
  g_type_init ();

  g_assert (test1 ());
  g_assert (test2 ());
  g_assert (test3 ());
  g_assert (test_error ());
  g_assert (test_another_error ());
  g_assert (test_yet_another_error ());
  g_assert (test_google_offline ());

  return 0;
}

