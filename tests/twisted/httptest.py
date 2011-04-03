
from twisted.web import http
from twisted.internet import reactor

from servicetest import Event

class Request(http.Request):
    def process(self):
        self.queue.append(Event('http-request',
            method=self.method, path=self.path, request=self))

class HTTPChannel(http.HTTPChannel):
    def requestFactory(self, *misc):
        request = Request(*misc)
        request.queue = self.queue
        return request

class HTTPFactory(http.HTTPFactory):
    protocol = HTTPChannel

    def __init__(self, queue):
        http.HTTPFactory.__init__(self)
        self.queue = queue

    def buildProtocol(self, addr):
        protocol = http.HTTPFactory.buildProtocol(self, addr)
        protocol.queue = self.queue
        return protocol

def listen_http(q, port=0):
    return reactor.listenTCP(port, HTTPFactory(q), interface='localhost')

