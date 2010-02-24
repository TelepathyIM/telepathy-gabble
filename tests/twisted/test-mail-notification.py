"""
Test Connection.Interface.MailNotification
"""

from twisted.words.xish import domish
from gabbletest import exec_test, make_result_iq, GoogleXmlStream
from servicetest import EventPattern

import constants as cs
import ns
import dbus

def check_properties_empty(conn, capabilities=0):
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

    unread_mails = conn.Get(
            cs.CONN_IFACE_MAIL_NOTIFICATION, 'UnreadMails',
            dbus_interface=cs.PROPERTIES_IFACE)
    assert len(unread_mails) == 0


def test_google_featured(q, bus, conn, stream):
    """Test functionnality when google mail notification is supported"""

    inbox_url = 'http://mail.google.com/mail'

    # E-mail thread 1 data
    thread1_id = "1"
    # Dates are 32bit unsigned integers, let's use the biggest possible value
    thread1_date = (pow(2,32) - 1) * 1000L
    thread1_url = 'http://mail.google.com/mail/#inbox/%x' % long(thread1_id)
    thread1_senders = [('John Smith', 'john@smith.com'),
                       ('Denis Tremblay', 'denis@tremblay.qc.ca')]
    thread1_subject = "subject1"
    thread1_snippet = "body1"

    # Email thread 2 data
    thread2_id = "2"
    thread2_date = 1234L
    thread2_url = 'http://mail.google.com/mail/#inbox/%x' % long(thread2_id)
    thread2_senders = [('Sam Gratte', 'sam@gratte.edu'),]
    thread2_subject = "subject2"
    thread2_snippet = "body2"

    # Email thread 3 data
    thread3_id = "3"
    thread3_date = 1235L
    thread3_url = 'http://mail.google.com/mail/#inbox/%x' % long(thread3_id)
    thread3_senders = [('Le Chat', 'le@chat.fr'),]
    thread3_subject = "subject3"
    thread3_snippet = "body3"

    # Supported mail capability flags
    Supports_Unread_Mail_Count = 1
    Supports_Unread_Mails = 2
    Supports_Request_Inbox_URL = 8
    Supports_Request_Mail_URL = 16
    expected_caps = Supports_Unread_Mail_Count\
                    | Supports_Unread_Mails\
                    | Supports_Request_Inbox_URL\
                    | Supports_Request_Mail_URL

    # Nobody is subscribed yet, attributes should all be empty, and
    # capabilities are set properly.
    check_properties_empty(conn, expected_caps)

    # Check that Gabble queries mail data on initial call to Subscribe().
    conn.MailNotification.Subscribe()
    event = q.expect('stream-iq', query_ns=ns.GOOGLE_MAIL_NOTIFY)

    result = make_result_iq(stream, event.stanza, False)
    mailbox = result.addElement('mailbox')
    mailbox['xmlns'] = ns.GOOGLE_MAIL_NOTIFY
    mailbox['url'] = inbox_url

    # Set e-mail thread 1
    mail = mailbox.addElement('mail-thread-info')
    mail['tid'] = thread1_id
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
    mail['tid'] = thread2_id
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

    # Then we expect UnreadMailsChanged with all the mail information.
    event = q.expect('dbus-signal', signal="UnreadMailsChanged")

    # Check that inbox URL is correct
    stored_url = conn.MailNotification.RequestInboxURL()
    assert stored_url[0] == inbox_url
    assert stored_url[1] == 0 # HTTP GET
    assert len(stored_url[2]) == 0

    # UnreadMailsChanged(u: count, aa{sv}: mails_added, ax: mails_removed)
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

    # Extract mails from signal, order is unknown
    mail1 = None
    mail2 = None
    for mail in mails_added:
        if mail['id'] == thread1_id:
            mail1 = mail
        elif mail['id'] == thread2_id:
            mail2 = mail
        else:
            assert False, "Gabble sent an unknown mail id=" + str(mail['id'])

    # Validate added e-mails with original data.
    assert mail1 != None
    # While date is in millisecond, the received timestamp is in seconds thus
    # we need to divided by 1000
    assert mail1['received-timestamp'] == thread1_date / 1000
    assert mail1['subject'] == thread1_subject
    assert mail1['truncated'] == True
    assert mail1['content'] == thread1_snippet
    assert mail1['senders'] == thread1_senders

    assert mail2 != None
    assert mail2['received-timestamp'] == thread2_date / 1000
    assert mail2['subject'] == thread2_subject
    assert mail2['truncated'] == True
    assert mail2['content'] == thread2_snippet
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
    assert stored_mail1['received-timestamp'] == thread1_date / 1000
    assert stored_mail1['subject'] == thread1_subject
    assert stored_mail1['truncated'] == True
    assert stored_mail1['content'] == thread1_snippet
    assert stored_mail1['senders'] == thread1_senders

    assert stored_mail2 != None
    assert stored_mail2['received-timestamp'] == thread2_date / 1000
    assert stored_mail2['subject'] == thread2_subject
    assert stored_mail2['truncated'] == True
    assert stored_mail2['content'] == thread2_snippet
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

    result = make_result_iq(stream, event.stanza, False)
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

    # Check the we can get an URL for a specific mail
    mail_url = conn.MailNotification.RequestMailURL(thread1_id,
            mails_added[0]['url-data']);

    # Unsubscribe and check that all data has been dropped
    conn.MailNotification.Unsubscribe()
    check_properties_empty(conn, expected_caps)


def test_no_google_featured(q, bus, conn, stream):
    """Check that Gabble reacts correctly when called on MailNotification
    while the feature is not supported."""

    # Google mail notification is not supported, gabble should not emit any
    # signals.
    forbidden = [EventPattern('dbus-signal', signal='MailsReceived'),
                 EventPattern('dbus-signal', signal='UnreadMailsChanged'),
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

    # Make sure method returns not implemented exception
    try:
        conn.MailNotification.Subscribe()
    except dbus.DBusException, e:
        assert e.get_dbus_name() == cs.NOT_IMPLEMENTED

    try:
        conn.MailNotification.Unsubscribe()
    except dbus.DBusException, e:
        assert e.get_dbus_name() == cs.NOT_IMPLEMENTED

    try:
        conn.MailNotification.RequestInboxURL()
    except dbus.DBusException, e:
        assert e.get_dbus_name() == cs.NOT_IMPLEMENTED

    try:
        conn.MailNotification.RequestMailURL("1", "http://test.com/mail")
    except dbus.DBusException, e:
        assert e.get_dbus_name() == cs.NOT_IMPLEMENTED

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
