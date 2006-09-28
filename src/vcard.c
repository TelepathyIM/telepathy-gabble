
#include "gabble-connection.h"
#include "namespaces.h"

#include "vcard.h"

static VCard *
vcard_from_node (LmMessageNode *node)
{
  VCard *vcard;
  LmMessageNode *vcard_node;

  vcard_node = lm_message_node_get_child (node, "vCard");

  if (NULL == vcard_node)
    return NULL;

  vcard = g_new0 (VCard, 1);

  return vcard;
}

typedef struct _VCardRequest VCardRequest;

struct _VCardRequest
{
  const gchar *jid;
  GabbleVCardCallback cb;
  gpointer user_data;
};

LmHandlerResult
_vcard_cb (
  LmMessageHandler *handler,
  LmConnection *conn,
  LmMessage *message,
  gpointer user_data)
{
  VCard *card;
  VCardRequest *req = (VCardRequest *) user_data;

  card = vcard_from_node (message->node);

  if (NULL != card)
    {
      req->cb (req->jid, card, req->user_data);
      g_free (card);
    }

  g_free (req);
  lm_message_handler_unref (handler);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

void
request_vcard (GabbleConnection *conn, const gchar *jid, GabbleVCardCallback cb, gpointer user_data)
{
  LmMessage *message;
  LmMessageNode *vcard;
  LmMessageHandler *handler;
  VCardRequest *req;

  req = g_new0 (VCardRequest, 1);
  req->jid = jid;
  req->cb = cb;
  req->user_data = user_data;

  handler = lm_message_handler_new (_vcard_cb, req, NULL);

  message = lm_message_new_with_sub_type (jid, LM_MESSAGE_TYPE_IQ,
    LM_MESSAGE_SUB_TYPE_GET);

  vcard = lm_message_node_add_child (message->node, "vCard", "");
  lm_message_node_set_attribute (vcard, "xmlns", NS_VCARD_TEMP);

  g_assert (
    lm_connection_send_with_reply (conn->lmconn, message, handler, NULL));
}

