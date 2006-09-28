
#ifndef __VCARD_H__
#define __VCARD_H__

typedef struct _VCard VCard;

struct _VCard
{
  gchar *avatar_mime_type;
  GString *avatar;
};

typedef void (*GabbleVCardCallback) (const gchar *jid, VCard *card, gpointer user_data);

void request_vcard (GabbleConnection *conn, const gchar *jid, GabbleVCardCallback cb, gpointer user_data);

#endif /* __VCARD_H__ */

