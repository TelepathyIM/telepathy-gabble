"""
Tests that disco replies whose <query/> node is missing don't crash Gabble.
"""

from gabbletest import exec_test, sync_stream, make_result_iq
import caps_helper

def test(q, bus, conn, stream):
    jid = 'crashy@cra.shy/hi'
    caps = { 'node': 'oh:hi',
             'ver': "dere",
           }
    h = caps_helper.send_presence(q, conn, stream, jid, caps, initial=False)
    request = caps_helper.expect_disco(q, jid, caps['node'], caps)
    result = make_result_iq(stream, request, add_query_node=False)
    stream.send(result)
    sync_stream(q, stream)

if __name__ == '__main__':
    exec_test(test)
