def get_contacts_presences_sync(conn, contacts):
    return conn.SimplePresence.GetPresences(contacts)
