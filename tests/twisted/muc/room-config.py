# vim: fileencoding=utf-8 :
"""
Test the RoomConfig interface on MUC channels.
"""

import dbus
from twisted.words.xish import xpath

from gabbletest import (
    exec_test, make_result_iq, acknowledge_iq, make_muc_presence,
    request_muc_handle, sync_stream)
from servicetest import (
    call_async, wrap_channel, EventPattern, assertEquals, assertSameSets,
    assertContains,
)

import constants as cs
import ns

ROOM_NAME = "A place to bury strangers"
ROOM_DESCRIPTION = "I hate noise-rock."

def add_field(elem, type, var, value):
    field = elem.addElement('field')

    if type is not None:
        field['type'] = type

    field['var'] = var
    value = field.addElement('value', content=value)

def handle_muc_owner_get_iq(stream, stanza):
    iq = make_result_iq(stream, stanza)
    query = iq.firstChildElement()
    x = query.addElement(('jabber:x:data', 'x'))
    x['type'] = 'form'
    add_field(x, 'text', 'password', '')
    add_field(x, 'boolean', 'password_protected', '0')

    # We have to include this field here to convince Gabble that the
    # description can be modified by owners. As far as wjt can determine, this
    # is a question of uneven server support: see 6f20080.
    add_field(x, 'text-single', 'muc#roomconfig_roomdesc', ROOM_DESCRIPTION)

    # add a multi values setting
    field = x.addElement('field')
    field['type'] = 'list-multi'
    field['var'] = 'muc#roomconfig_presencebroadcast'
    for v in ['moderator', 'participant', 'visitor']:
        field.addElement('value', content=v)
        field.addElement('option', content=v)

    stream.send(iq)

def handle_disco_info_iq(stream, stanza):
    iq = make_result_iq(stream, stanza)

    query = iq.firstChildElement()

    # Title
    identity = query.addElement('identity')
    identity['category'] = 'conference'
    identity['type'] = 'text'
    identity['name'] = ROOM_NAME

    for var in [ns.MUC,
                'muc_anonymous', # Anonymous
                'muc_open', # ¬InviteOnly
                # Limit lives in the data form
                'muc_moderated', # Moderated
                # Title is above
                # Description is below
                'muc_temporary', # ¬Persistent
                'muc_hidden', # Private
                'muc_unsecure', # ¬PasswordProtected
                # Password is in the owner form.
               ]:
        f = query.addElement('feature')
        f['var'] = var

    # Description
    x = query.addElement((ns.X_DATA, 'x'))
    x['type'] = 'result'
    add_field(x, 'hidden', 'FORM_TYPE', ns.MUC_ROOMINFO)
    add_field(x, None, 'muc#roominfo_description', ROOM_DESCRIPTION)

    stream.send(iq)

def test(q, bus, conn, stream):
    muc_handle = request_muc_handle(q, conn, stream, 'chat@conf.localhost')

    call_async(q, conn, 'RequestChannel', cs.CHANNEL_TYPE_TEXT, cs.HT_ROOM,
        muc_handle, True)

    q.expect('stream-presence', to='chat@conf.localhost/test')

    # Send presence for own membership of room.
    stream.send(
        make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'test'))

    disco_iq, owner_iq, ret, _ = q.expect_many(
        EventPattern('stream-iq', to='chat@conf.localhost', iq_type='get',
            query_ns=ns.DISCO_INFO),
        EventPattern('stream-iq', to='chat@conf.localhost', iq_type='get',
            query_ns=ns.MUC_OWNER),
        EventPattern('dbus-return', method='RequestChannel'),
        # We discovered that we're an owner. Emitting a signal seems
        # acceptable, although technically this happens before the channel
        # request finishes so the channel could just as well not be on the bus.
        EventPattern('dbus-signal', signal='PropertiesChanged',
            args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                  {'CanUpdateConfiguration': True},
                  []
                 ]),
        )

    # This tells Gabble that the MUC is well-behaved and lets owners modify the
    # room description. Technically we could also pull the description out of
    # here, but as an implementation detail we only read configuration out of
    # the disco reply.
    handle_muc_owner_get_iq(stream, owner_iq.stanza)
    pc = q.expect('dbus-signal', signal='PropertiesChanged',
        predicate=lambda e: e.args[0] == cs.CHANNEL_IFACE_ROOM_CONFIG)
    _, changed, invalidated = pc.args
    assertEquals(['MutableProperties'], changed.keys())
    assertContains('Description', changed['MutableProperties'])

    handle_disco_info_iq(stream, disco_iq.stanza)
    pc = q.expect('dbus-signal', signal='PropertiesChanged',
        predicate=lambda e: e.args[0] == cs.CHANNEL_IFACE_ROOM_CONFIG)
    q.expect('dbus-signal', signal='PropertiesChanged',
        args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
              {'ConfigurationRetrieved': True},
              []
             ])
    _, changed, invalidated = pc.args
    assertEquals(
        { 'Anonymous': True,
          'Moderated': True,
          'Title': ROOM_NAME,
          'Description': ROOM_DESCRIPTION,
          'Private': True,
        }, changed)

    assertEquals([], invalidated)

    text_chan = wrap_channel(
        bus.get_object(conn.bus_name, ret.value[0]), 'Text', ['RoomConfig1'])
    config = text_chan.Properties.GetAll(cs.CHANNEL_IFACE_ROOM_CONFIG)

    # Verify that all of the config properties (besides the password ones)
    # correspond to the flags set in handle_disco_info_iq().
    assertEquals(True, config['Anonymous'])
    assertEquals(False, config['InviteOnly'])
    assertEquals(0, config['Limit'])
    assertEquals(True, config['Moderated'])
    assertEquals(ROOM_NAME, config['Title'])
    assertEquals(ROOM_DESCRIPTION, config['Description'])
    assertEquals(False, config['Persistent'])
    assertEquals(True, config['Private'])
    # This is affirmed to be false both by the disco reply and by the muc#owner
    # reply.
    assertEquals(False, config['PasswordProtected'])
    # This comes from the muc#owner reply.
    assertEquals('', config['Password'])

    # We're a room owner, so we should be able to modify the room configuration
    assertEquals(True, config['CanUpdateConfiguration'])
    assertSameSets(
        ['Anonymous',
         'InviteOnly',
        # TODO: when we understand member limit fields, add Limit
         'Moderated',
         'Title',
         'Description',
         'Persistent',
         'Private',
         'PasswordProtected',
         'Password',
        ],
        config['MutableProperties'])

    props = dbus.Dictionary(
        { 'Password': 'foo',
          'PasswordProtected': True,
        }, signature='sv')
    call_async(q, text_chan.RoomConfig1, 'UpdateConfiguration', props)

    event = q.expect('stream-iq', to='chat@conf.localhost', iq_type='get',
        query_ns=ns.MUC_OWNER)
    handle_muc_owner_get_iq(stream, event.stanza)

    event = q.expect('stream-iq', to='chat@conf.localhost', iq_type='set',
        query_ns=ns.MUC_OWNER)
    fields = xpath.queryForNodes('/iq/query/x/field', event.stanza)
    form = {}
    for field in fields:
        values = xpath.queryForNodes('/field/value', field)
        form[field['var']] = [str(v) for v in values]
    # Check that Gabble echoed back the fields it didn't understand (or want to
    # change) with their previous values.
    assertEquals(
        {'password': ['foo'],
         'password_protected': ['1'],
         'muc#roomconfig_presencebroadcast':
            ['moderator', 'participant', 'visitor'],
         'muc#roomconfig_roomdesc': [ROOM_DESCRIPTION],
        }, form)
    acknowledge_iq(stream, event.stanza)

    pc, _ = q.expect_many(
        EventPattern('dbus-signal', signal='PropertiesChanged',
            predicate=lambda e: e.args[0] == cs.CHANNEL_IFACE_ROOM_CONFIG),
        EventPattern('dbus-return', method='UpdateConfiguration'),
        )

    _, changed, invalidated = pc.args

    assertEquals(props, changed)
    assertEquals([], invalidated)

    config = text_chan.Properties.GetAll(cs.CHANNEL_IFACE_ROOM_CONFIG)
    assertEquals(True, config['PasswordProtected'])
    assertEquals('foo', config['Password'])

    # Check unknown fields are rejected.
    props = dbus.Dictionary(
        { 'PasswordProtected': True,
          'Riding on a donkey': True,
        }, signature='sv')
    call_async(q, text_chan.RoomConfig1, 'UpdateConfiguration', props)
    q.expect('dbus-error', name=cs.INVALID_ARGUMENT)

    # Check that mis-typed fields are rejected.
    props = dbus.Dictionary(
        { 'PasswordProtected': 'foo',
          'Password': True,
        }, signature='sv')
    call_async(q, text_chan.RoomConfig1, 'UpdateConfiguration', props)
    q.expect('dbus-error', name=cs.INVALID_ARGUMENT)

    # Updating no fields should be a no-op, and not wait on any network
    # traffic.
    text_chan.RoomConfig1.UpdateConfiguration({})

if __name__ == '__main__':
    exec_test(test)
