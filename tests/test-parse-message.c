
#include <string.h>

#include "text-mixin.h"

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
  const gchar *body;
  gint state;

  msg = lm_message_build (NULL, LM_MESSAGE_TYPE_MESSAGE,
        '@', "from", "foo@bar.com",
        NULL);
  ret = gabble_text_mixin_parse_incoming_message (
      msg, &from, &stamp, &type, &body, &state, &send_error);
  g_assert (ret == TRUE);
  g_assert (0 == strcmp (from, "foo@bar.com"));
  g_assert (stamp == 0);
  g_assert (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE);
  g_assert (body == NULL);
  g_assert (state == -1);
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
  const gchar *body;
  gint state;

  msg = lm_message_build (NULL, LM_MESSAGE_TYPE_MESSAGE,
        '@', "from", "foo@bar.com",
        '(', "body", "hello", ')',
        NULL);
  ret = gabble_text_mixin_parse_incoming_message (
      msg, &from, &stamp, &type, &body, &state, &send_error);
  g_assert (ret == TRUE);
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
  const gchar *body;
  gint state;

  msg = lm_message_build (NULL, LM_MESSAGE_TYPE_MESSAGE,
        '@', "from", "foo@bar.com",
        '@', "type", "chat",
        '(', "body", "hello", ')',
        NULL);
  ret = gabble_text_mixin_parse_incoming_message (
      msg, &from, &stamp, &type, &body, &state, &send_error);
  g_assert (ret == TRUE);
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
  const gchar *body;
  gint state;

  msg = lm_message_build_with_sub_type (NULL, LM_MESSAGE_TYPE_MESSAGE,
      LM_MESSAGE_SUB_TYPE_ERROR,
      '@', "from", "foo@bar.com",
      '@', "type", "error",
      '(', "error", "oops", ')',
      NULL);
  ret = gabble_text_mixin_parse_incoming_message (
      msg, &from, &stamp, &type, &body, &state, &send_error);
  g_assert (ret == TRUE);
  g_assert (0 == strcmp (from, "foo@bar.com"));
  g_assert (stamp == 0);
  g_assert (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE);
  g_assert (body == NULL);
  g_assert (state == -1);
  g_assert (send_error == TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN);
  lm_message_unref (msg);
  return TRUE;
}

int
main (void)
{
  g_assert (test1 ());
  g_assert (test2 ());
  g_assert (test3 ());
  g_assert (test_error ());

  return 0;
}

