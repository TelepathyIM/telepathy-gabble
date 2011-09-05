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
from mucutil import join_muc

import constants as cs
import ns

ROOM_NAME = "A place to bury strangers"
ROOM_DESCRIPTION = "I hate noise-rock."

def get_default_form():
    return { 'password': [''],
             'password_protected': ['0'],
             'muc#roomconfig_persistentroom': ['0'],
             # We have to include this field here to convince Gabble that the
             # description can be modified by owners. As far as wjt can
             # determine, this is a question of uneven server support: see
             # 6f20080.
             'muc#roomconfig_roomdesc': [ROOM_DESCRIPTION],
             # Gabble doesn't understand this field; we include it to verify
             # that Gabble can correctly echo multi-value fields.
             'muc#roomconfig_presencebroadcast':
                ['moderator', 'participant', 'visitor'],
           }

def parse_form(stanza):
    fields = xpath.queryForNodes('/iq/query/x/field', stanza)
    form = {}
    for field in fields:
        values = xpath.queryForNodes('/field/value', field)
        form[field['var']] = [str(v) for v in values]
    return form

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

    for var, values in get_default_form().iteritems():
        if len(values) > 1:
            field = x.addElement('field')
            field['type'] = 'list-multi'
            field['var'] = var
            for v in values:
                field.addElement('value', content=v)
                field.addElement('option', content=v)
        elif values[0] == '0' or values[0] == '1':
            add_field(x, 'boolean', var, values[0])
        else:
            add_field(x, 'text', var, values[0])

    stream.send(iq)

def handle_muc_owner_set_iq(stream, stanza, fields):
    form = parse_form(stanza)
    # Check that Gabble echoed back the fields it didn't understand (or want to
    # change) with their previous values.
    expected_form = get_default_form()
    expected_form.update(fields)
    assertEquals(expected_form, form)
    acknowledge_iq(stream, stanza)

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

def test_some_stuff(q, bus, conn, stream):
    _, text_chan, _, _, disco_iq, owner_iq, _ = join_muc(q, bus, conn, stream,
        'chat@conf.localhost', role='moderator', affiliation='owner',
        also_capture=[
            EventPattern('stream-iq', to='chat@conf.localhost', iq_type='get',
                query_ns=ns.DISCO_INFO),
            EventPattern('stream-iq', to='chat@conf.localhost', iq_type='get',
                query_ns=ns.MUC_OWNER),
            # We discovered that we're an owner. Emitting a signal seems
            # acceptable, although technically this happens before the channel
            # request finishes so the channel could just as well not be on the bus.
            EventPattern('dbus-signal', signal='PropertiesChanged',
                args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                      {'CanUpdateConfiguration': True},
                      []
                     ]),
        ])

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
    handle_muc_owner_set_iq(stream, event.stanza,
        {'password': ['foo'],
         'password_protected': ['1'],
        })

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

def test_role_changes(q, bus, conn, stream):
    # The test user joins a room. Bob is an owner (and moderator); the test
    # user starts out with no affiliation and the rôle of participant.
    MUC = 'aoeu@snth'
    _, chan, _, immutable_props, disco = join_muc(q, bus, conn, stream,
        MUC, role='participant',
        also_capture=[
            EventPattern('stream-iq', to=MUC, iq_type='get',
                query_ns=ns.DISCO_INFO),
        ])
    assertContains(cs.CHANNEL_IFACE_ROOM_CONFIG, immutable_props[cs.INTERFACES])

    handle_disco_info_iq(stream, disco.stanza)
    q.expect('dbus-signal', signal='PropertiesChanged',
        args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
              {'ConfigurationRetrieved': True},
              []
             ])

    # If we try to change the configuration, Gabble should say no: it knows
    # we're not allowed to do that.
    call_async(q, chan.RoomConfig1, 'UpdateConfiguration', {})
    q.expect('dbus-error', name=cs.PERMISSION_DENIED)

    config = chan.Properties.GetAll(cs.CHANNEL_IFACE_ROOM_CONFIG)
    assert not config['CanUpdateConfiguration'], config

    # If we acquire affiliation='owner', this should be signalled as our
    # becoming able to modify the channel configuration.
    stream.send(make_muc_presence('owner', 'moderator', MUC, 'test'))
    q.expect('dbus-signal', signal='PropertiesChanged',
        args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
              {'CanUpdateConfiguration': True},
              []
             ])

    # Due to silliness, Gabble has to grab the owner configuration form to see
    # whether it's possible to change the room description.
    owner_iq = q.expect('stream-iq', to=MUC, iq_type='get', query_ns=ns.MUC_OWNER)
    handle_muc_owner_get_iq(stream, owner_iq.stanza)

    # Bob's ownership rights being taken away should have no effect.
    stream.send(make_muc_presence('none', 'participant', MUC, 'bob'))

    # So now we're an owner, and CanUpdateConfiguration is True, we should be
    # able to change some configuration.
    props = dbus.Dictionary(
        { 'Persistent': True,
        }, signature='sv')
    call_async(q, chan.RoomConfig1, 'UpdateConfiguration', props)

    owner_iq = q.expect('stream-iq', to=MUC, iq_type='get', query_ns=ns.MUC_OWNER)
    handle_muc_owner_get_iq(stream, owner_iq.stanza)

    event = q.expect('stream-iq', to=MUC, iq_type='set', query_ns=ns.MUC_OWNER)
    handle_muc_owner_set_iq(stream, event.stanza,
        {'muc#roomconfig_persistentroom': ['1']})

    q.expect_many(
        EventPattern('dbus-return', method='UpdateConfiguration'),
        EventPattern('dbus-signal', signal='PropertiesChanged',
            args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
                  {'Persistent': True},
                  []
                 ]))

    # If we lose our affiliation, that should be signalled too.
    stream.send(make_muc_presence('none', 'participant', MUC, 'test'))
    q.expect('dbus-signal', signal='PropertiesChanged',
        args=[cs.CHANNEL_IFACE_ROOM_CONFIG,
              {'CanUpdateConfiguration': False},
              []
             ])

    # Gabble should once again reject attempts to change the configuration
    call_async(q, chan.RoomConfig1, 'UpdateConfiguration', {})
    q.expect('dbus-error', name=cs.PERMISSION_DENIED)

def test_broken_server(q, bus, conn, stream):
    MUC = 'bro@ken'
    _, chan, _ , _ = join_muc(q, bus, conn, stream, MUC, affiliation='owner')
    owner_iq = q.expect('stream-iq', to=MUC, iq_type='get', query_ns=ns.MUC_OWNER)
    handle_muc_owner_get_iq(stream, owner_iq.stanza)

    call_async(q, chan.RoomConfig1, 'UpdateConfiguration', {'Private': False})
    e = q.expect('stream-iq', to=MUC, iq_type='get', query_ns=ns.MUC_OWNER)
    handle_muc_owner_get_iq(stream, e.stanza)

    # The server doesn't actually have a form field for configuring whether the
    # room is private or not.
    q.expect('dbus-error', method='UpdateConfiguration', name=cs.SERVICE_CONFUSED)

def test(q, bus, conn, stream):
    test_some_stuff(q, bus, conn, stream)
    test_role_changes(q, bus, conn, stream)
    test_broken_server(q, bus, conn, stream)

if __name__ == '__main__':
    exec_test(test)
