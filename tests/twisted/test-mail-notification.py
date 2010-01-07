"""
Test Connection.Interface.MailNotification
"""

from twisted.words.xish import domish
from gabbletest import exec_test, make_result_iq, GoogleXmlStream
from servicetest import EventPattern

import constants as cs
import ns
import dbus

def check_properties_empty (conn, capabilities = 0):
    """Check that all mail notification properties are empty and that
       capabilities match the provided bit flags"""

    caps = conn.Get(
            cs.CONN_IFACE_MAIL_NOTIFICATION, 'Capabilities', 
            dbus_interface=cs.PROPERTIES_IFACE)
    assert caps == capabilities

    mail_count = conn.Get(
            cs.CONN_IFACE_MAIL_NOTIFICATION, 'UnreadMailCount', 
            dbus_interface=cs.PROPERTIES_IFACE)
    assert mail_count == 0

    inbox_url = conn.Get(
            cs.CONN_IFACE_MAIL_NOTIFICATION, 'InboxURL', 
            dbus_interface=cs.PROPERTIES_IFACE)
    assert inbox_url == ''

    http_method = conn.Get(
            cs.CONN_IFACE_MAIL_NOTIFICATION, 'Method', 
            dbus_interface=cs.PROPERTIES_IFACE)
    assert http_method == 0

    post_data = conn.Get(
            cs.CONN_IFACE_MAIL_NOTIFICATION, 'PostData', 
            dbus_interface=cs.PROPERTIES_IFACE)
    assert len(post_data) == 0

    unread_mails = conn.Get(
            cs.CONN_IFACE_MAIL_NOTIFICATION, 'UnreadMails',
            dbus_interface=cs.PROPERTIES_IFACE)
    assert len(unread_mails) == 0


def test_google_featured(q, bus, conn, stream):
    """Test functionnality when google mail notificagtion is supported"""

    inbox_url = 'http://mail.google.com/mail'

    # E-mail thread 1 data
    # tid and date are 64bit unsigned integer, lets use the biggest possible value
    thread1_id = pow(2,64) - 1L
    thread1_date = (pow(2,32) - 1) * 1000L
    thread1_url = 'http://mail.google.com/mail/#inbox/%x' % thread1_id
    thread1_senders = [('John Smith', 'john@smith.com'), 
                       ('Denis Tremblay', 'denis@trempblay.qc.ca')]
    thread1_subject = "subject1"
    thread1_snippet = "body1"

    # Email thread 2 data
    thread2_id = 2L
    thread2_date = 1234L
    thread2_url = 'http://mail.google.com/mail/#inbox/%x' % +thread2_id
    thread2_senders = [('Sam Gratte', 'sam@gratte.edu'),]
    thread2_subject = "subject2"
    thread2_snippet = "body2"

    # Email thread 3 data
    thread3_id = 3L
    thread3_date = 1235L
    thread3_url = 'http://mail.google.com/mail/#inbox/%x' % +thread2_id
    thread3_senders = [('Le Chat', 'le@chat.fr'),]
    thread3_subject = "subject2"
    thread3_snippet = "body2"

    # Supported mail capability flags
    Has_Prop_UnreadMailCount = 1
    Has_Prop_UnreadMails = 2
    
    # Nobody is subscribed yet, attributes should all be empty, and
    # capabilities are set properly.
    check_properties_empty(conn, Has_Prop_UnreadMailCount | Has_Prop_UnreadMails)

    # Check that gabble query mail data on initial subscribe.
    conn.MailNotification.Subscribe()
    event = q.expect('stream-iq', query_ns=ns.GOOGLE_MAIL_NOTIFY)

    result = make_result_iq (stream, event.stanza, False)
    mailbox = result.addElement('mailbox')
    mailbox['xmlns'] = ns.GOOGLE_MAIL_NOTIFY
    mailbox['url'] = inbox_url
    
    # Set e-mail thread 1
    mail = mailbox.addElement('mail-thread-info')
    mail['tid'] = str(thread1_id)
    mail['date'] = str(thread1_date)
    senders = mail.addElement('senders')
    for t1_sender in thread1_senders:
        sender = senders.addElement('sender')
        sender['name'] = t1_sender[0]
        sender['address'] = t1_sender[1]
        sender['unread'] = '1'
    mail.addElement('subject', content=thread1_subject)
    mail.addElement('snippet', content=thread1_snippet)


    # Set e-mail thread 2
    mail = mailbox.addElement('mail-thread-info')
    mail['tid'] = str(thread2_id)
    mail['date'] = str(thread2_date)
    senders = mail.addElement('senders')
    for t2_sender in thread2_senders:
        sender = senders.addElement('sender')
        sender['name'] = t2_sender[0]
        sender['address'] = t2_sender[1]
        sender['unread'] = '1'
    sender = senders.addElement('sender')
    sender['name'] = 'Read Sender'
    sender['address'] = 'read@sender.net'
    mail.addElement('subject', content=thread2_subject)
    mail.addElement('snippet', content=thread2_snippet)

    stream.send(result)

    # We expect the InboxURLChanged to happen before the UnreadMailsChanged
    # since we need this information. Also, Gabble should not have queried
    # this information already since we are first subscriber.
    event = q.expect('dbus-signal', signal='InboxURLChanged')
    
    stored_url = conn.Get(
            cs.CONN_IFACE_MAIL_NOTIFICATION, 'InboxURL', 
            dbus_interface=cs.PROPERTIES_IFACE)

    assert event.args[0] == inbox_url
    assert stored_url == inbox_url

    # Then we expect UnreadMailsChanged with all the mail information.
    event = q.expect('dbus-signal', signal="UnreadMailsChanged")

    # UnreadMailsChanged (u: count, aa{sv}: mails_added, ax: mails_removed)
    unread_count = event.args[0]
    mails_added = event.args[1]
    mails_removed = event.args[2]

    # Get stored data to check we have same thing
    stored_unread_count = conn.Get(
            cs.CONN_IFACE_MAIL_NOTIFICATION, 'UnreadMailCount', 
            dbus_interface=cs.PROPERTIES_IFACE)
    stored_unread_mails = conn.Get(
            cs.CONN_IFACE_MAIL_NOTIFICATION, 'UnreadMails',
            dbus_interface=cs.PROPERTIES_IFACE)

    assert unread_count == 2
    assert stored_unread_count == unread_count
    assert len(stored_unread_mails) == unread_count
    assert len(mails_added) == unread_count
    assert len(mails_removed) == 0

    # Extract mails from signal, order is unkown
    mail1 = None
    mail2 = None
    for mail in mails_added:
        if mail['id'] == thread1_id:
            mail1 = mail
        elif mail['id'] == thread2_id:
            mail2 = mail
        else:
            assert False, "Gabble sent an unkown mail id=" + str(mail['id'])

    # Validate added e-mails with original data.
    assert mail1 != None
    assert mail1['url'] == thread1_url
    # While date is in millisecond, the received timestamp is in seconds thus
    # we need to divided by 1000
    assert mail1['received-timestamp'] == thread1_date / 1000
    assert mail1['subject'] == thread1_subject
    assert mail1['snippet'] == thread1_snippet
    assert mail1['senders'] == thread1_senders

    assert mail2 != None
    assert mail2['url'] == thread2_url
    assert mail2['received-timestamp'] == thread2_date / 1000
    assert mail2['subject'] == thread2_subject
    assert mail2['snippet'] == thread2_snippet
    assert mail2['senders'] == thread2_senders

    # Extract mails from stored mails, order is unkown
    stored_mail1 = None
    stored_mail2 = None
    for mail in stored_unread_mails:
        if mail['id'] == thread1_id:
            stored_mail1 = mail
        elif mail['id'] == thread2_id:
            stored_mail2 = mail
        else:
            assert False, "Gabble stored an unkown mail id=" + str(mail['id'])
    
    # Validate stored e-mails with original data
    assert stored_mail1 != None
    assert stored_mail1['url'] == thread1_url
    assert stored_mail1['received-timestamp'] == thread1_date / 1000
    assert stored_mail1['subject'] == thread1_subject
    assert stored_mail1['snippet'] == thread1_snippet
    assert stored_mail1['senders'] == thread1_senders

    assert stored_mail2 != None
    assert stored_mail2['url'] == thread2_url
    assert stored_mail2['received-timestamp'] == thread2_date / 1000
    assert stored_mail2['subject'] == thread2_subject
    assert stored_mail2['snippet'] == thread2_snippet
    assert stored_mail2['senders'] == thread2_senders
    
    # Now we want to validate the update mechanism. Thus we wil send an
    # new-mail event, wait for gabble to query the latest mail and reply
    # a different list.
    m = domish.Element((None, 'iq'))
    m['type'] = 'set'
    m['from'] = 'alice@foo.com'
    m['id'] = '3'
    m.addElement((ns.GOOGLE_MAIL_NOTIFY, 'new-mail'))
    stream.send(m)

    # Wait for mail information request
    event = q.expect('stream-iq', query_ns=ns.GOOGLE_MAIL_NOTIFY)
    
    result = make_result_iq (stream, event.stanza, False)
    mailbox = result.addElement('mailbox')
    mailbox['xmlns'] = ns.GOOGLE_MAIL_NOTIFY
    # We alter the URL to see if it gets detected
    mailbox['url'] = inbox_url + 'diff'
    
    # Set e-mail thread 1 and change snippet to see if it's detected
    mail = mailbox.addElement('mail-thread-info')
    mail['tid'] = str(thread1_id)
    mail['date'] = str(thread1_date)
    senders = mail.addElement('senders')
    for t1_sender in thread1_senders:
        sender = senders.addElement('sender')
        sender['name'] = t1_sender[0]
        sender['address'] = t1_sender[1]
        sender['unread'] = '1'
    mail.addElement('subject', content=thread1_subject)
    mail.addElement('snippet', content=thread1_snippet + 'diff')

    # We don't set the thread 2, as if it was removed

    # Set e-mail thread 3
    mail = mailbox.addElement('mail-thread-info')
    mail['tid'] = str(thread3_id)
    mail['date'] = str(thread3_date)
    senders = mail.addElement('senders')
    for t3_sender in thread3_senders:
        sender = senders.addElement('sender')
        sender['name'] = t3_sender[0]
        sender['address'] = t3_sender[1]
        sender['unread'] = '1'
    mail.addElement('subject', content=thread3_subject)
    mail.addElement('snippet', content=thread3_snippet)

    stream.send(result)

    q.expect('dbus-signal', signal='InboxURLChanged')
    event = q.expect('dbus-signal', signal='UnreadMailsChanged')
    unread_count = event.args[0]
    mails_added = event.args[1]
    mails_removed = event.args[2]

    # Validate that changed is set for correct items
    assert unread_count == 2
    assert len(mails_added) == 2
    assert mails_added[0]['id'] in (thread1_id, thread3_id)
    assert mails_added[1]['id'] in (thread1_id, thread3_id)
    assert mails_added[0]['id'] != mails_added[1]['id']
    assert len(mails_removed) == 1
    assert mails_removed[0] == thread2_id
    
    # Unsubscribe and check that all data has been dropped
    conn.MailNotification.Unsubscribe()
    check_properties_empty(conn, Has_Prop_UnreadMailCount | Has_Prop_UnreadMails)


def test_no_google_featured(q, bus, conn, stream):
    """Check that Gabble react correctly when called on MailNotification
    while the feature is not support."""

    not_implemented = 'org.freedesktop.Telepathy.Error.NotImplemented'

    # Google mail notification is not support, gabble should not emit any
    # signals.
    forbidden = [EventPattern('dbus-signal', signal='MailsReceived'),
                 EventPattern('dbus-signal', signal='UnreadMailsChanged'),
                 EventPattern('dbus-signal', signal='InboxURLChanged'),
                 EventPattern('stream-iq', query_ns=ns.GOOGLE_MAIL_NOTIFY)]
    q.forbid_events(forbidden)

    # Make sure gabble does not query mail data on an unexpected new-mail
    # notification.
    m = domish.Element((None, 'iq'))
    m['type'] = 'set'
    m['from'] = 'alice@foo.com'
    m['id'] = '2'
    m.addElement((ns.GOOGLE_MAIL_NOTIFY, 'new-mail'))
    stream.send(m)

    # Make sure method return not implemented exception
    try:
        conn.MailNotification.Subscribe()
    except dbus.DBusException, e:
        assert e.get_dbus_name() == not_implemented

    try:
        conn.MailNotification.Unsubscribe()
    except dbus.DBusException, e:
        assert e.get_dbus_name() == not_implemented

    # Make sure all properties return with empty or 0 data including
    # capabilities
    check_properties_empty(conn)
    
    # Unforbids events
    q.unforbid_events(forbidden)


def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    interfaces = conn.GetInterfaces()

    if stream.__class__ is GoogleXmlStream:
        assert cs.CONN_IFACE_MAIL_NOTIFICATION in interfaces
        test_google_featured(q, bus, conn, stream)
    else:
        assert cs.CONN_IFACE_MAIL_NOTIFICATION not in interfaces
        test_no_google_featured(q, bus, conn, stream)

if __name__ == '__main__':
    exec_test(test, protocol=GoogleXmlStream)
    exec_test(test)
