import constants as cs

def get_contacts_presences_sync(conn, contacts):
     h2asv = conn.Contacts.GetContactAttributes(contacts, [cs.CONN_IFACE_SIMPLE_PRESENCE])
     presences = {}
     for h in contacts:
         presences[h] = h2asv[h][cs.ATTR_PRESENCE]
     return presences
