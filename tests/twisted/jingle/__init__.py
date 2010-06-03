# Without this, caps/jingle-caps.py can't include jingle.jingletest2.
#
# I guess the rationale for requiring an __init__.py at
# <http://docs.python.org/tutorial/modules.html#packages> makes sense, but it
# was still kind of a pain.

__all__ = ['jingletest2']
