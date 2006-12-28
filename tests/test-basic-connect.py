#!/usr/bin/python
from dbustesting import *

class BasicConnectTest(ClientTestCase):

	def start_manager(self, nick):
		self[nick] = ("org.freedesktop.Telepathy.ConnectionManager.gabble","/org/freedesktop/Telepathy/ConnectionManager/gabble","org.freedesktop.Telepathy.ConnectionManager")

	def connect(self, nick, proto, params):
		# Connect to the manager
		conn, obj = self[nick]["RequestConnection"].call(proto, params)
		# register connection as self.nick_conn
		self[nick+"_conn"] = (conn, obj, "org.freedesktop.Telepathy.Connection")
		self[nick+"_conn"]["Connect"].call(timeout=30000)
		
		# Retrieve connection status, and wait until connected
		self[nick+"_conn"]["StatusChanged"].listen()
		status = self[nick+"_conn"]["GetStatus"].call()
		while status != 0:
			if status == 2:
				raise Exception("Could not connect")
			status, reason = self[nick+"_conn"]["StatusChanged"].wait()
		
		return conn
		
	def run(self):
		# Test Setup
		self.start_manager("client1")
		self.start_manager("client2")
		
		# Connect the two accounts
		conn1 = self.connect("client1", "jabber",
			{"account": "telepathytest1@jabber.org", "password": "telepathy"})
		conn2 = self.connect("client2", "jabber",
			{ "account": "telepathytest2@jabber.org","password": "telepathy"})
		
		# Start listening to NewChannel signal emitted when client2 starts to talk to client1
		self["client1_conn"]["NewChannel"].listen()
		
		# Create a text channel
		self["client2_chan"] = (
			# The connection service
			conn2,
			# A new text channel to the client 1
			self["client2_conn"]["RequestChannel"].call(
				"org.freedesktop.Telepathy.Channel.Type.Text", 1,
				self["client2_conn"]["RequestHandles"].call (1, ["telepathytest1@jabber.org"])[0], False), 
			# The interfaces for this text channel
			{"text": "org.freedesktop.Telepathy.Channel.Type.Text", "chan": "org.freedesktop.Telepathy.Channel"})
		
		# Send a test message to client 1 from client 2
		self["client2_chan"]["text"]["Send"].call (0, "Test")
		
		# We got a newchannel from client 1
		obj, channel_type, handle_type, handle, suppress_handler = self["client1_conn"]["NewChannel"].wait()
		
		assert channel_type == "org.freedesktop.Telepathy.Channel.Type.Text"
		assert handle_type == 1
		assert suppress_handler == False
		
		# Create the channel structure for client 1
		self["client1_chan"] = (
			conn1,
			obj, 
			{"text": "org.freedesktop.Telepathy.Channel.Type.Text", "chan": "org.freedesktop.Telepathy.Channel"})
		
		# Retreive pending messages
		messages = self["client1_chan"]["text"]["ListPendingMessages"].call(True)
		
		id, stamp, from_handle, msg_type, flags, string = messages[0]
		assert len(messages) == 1
		assert from_handle == handle
		assert msg_type == 0
		assert flags == 0
		assert string == "Test"
		
		# Close/Disconnect stuff
		self["client2_chan"]["chan"]["Close"].call()
		self["client1_chan"]["chan"]["Close"].call()
		self["client2_conn"]["Disconnect"].call()
		self["client1_conn"]["Disconnect"].call()


run(BasicConnectTest)
