"""
Helper functions for writing tubes tests
"""

from servicetest import unwrap

def check_tube_in_tubes(tube, tubes):
    """
    Check that 'tube' is in 'tubes', which should be the return value of
    ListTubes()
    """

    utube = unwrap(tube)
    for t in tubes:
        if tube[0] != t[0]:
            continue

        pair = "\n    %s,\n    %s" % (utube, unwrap(t))

        assert tube[1] == t[1], "self handles don't match: %s" % pair
        assert tube[2] == t[2], "tube types don't match: %s" % pair
        assert tube[3] == t[3], "services don't match: %s " % pair
        assert tube[4] == t[4], "parameters don't match: %s" % pair
        assert tube[5] == t[5], "states don't match: %s" % pair

        return

    assert False, "tube %s not in %s" % (unwrap (tube), unwrap (tubes))

