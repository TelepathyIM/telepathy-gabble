import sys
import traceback

import dbus
import dbus.glib
import dbus.service
from dbus.proxies import ProxyObject
import gobject

# The default timeout for all blocking calls
DEFAULT_TIMEOUT=10

# The session bus used for the tests
session_bus = dbus.SessionBus()

# The loop used for the tests, if needed provide another one.
LOOP = gobject.MainLoop()

class MainloopClock:
	def __init__(self, callback=None):
		self._ticks = None
		self._callback = callback
		
	def _tick(self):
		self._ticks[1] -= 1
		return True
	
	def _reset(self):
		if self._ticks != None:
			gobject.source_remove(self._ticks[0])
			self._ticks = None
			
	def sleep(self, timeout):
		"""
		Sleeps for 'timeout' seconds without blocking the Mainloop. Blocking call.
		"""
		if self._callback == None:
			print 'Sleeping for %d seconds' % (timeout)
		self._reset()
	
		self._ticks = [gobject.timeout_add(timeout*1000, self._tick), timeout]
		while self._ticks[1] > 0 and (self._callback == None or self._callback()):
			LOOP.get_context().iteration(True)

class TestCase:
	"""
	Subclass this class to create your TestCase. then pass the class to the run()
	function to execute the tests.
	"""
	def __init__(self):
		pass
			
	def Sleep(self, timeout):
		"""
		Calling this method will block for 'timeout' seconds, without hanging the process.
		"""
		MainloopClock().sleep(timeout)
	
	def run(self):
		"""
		This method is called when the test must be run. Override it to implement your
		test script. Any exception thrown in this method will be reported as a test failure.
		"""
		raise NotImplementedError()

	def __repr__(self):
		return str(self.__class__)
		
def run(cases):
	"""
	Call this method with a TestCase sub-class, or a list of TestCase sub-classes.
	Warning !
	 As the proxy objects and various signal connections aren't destroyed after a test, it is
	 not a good idea to run more than one test after the other, better launch one process per test.
	
	The test case to run will be given on the command line, if not, a help notice is printed
	"""
	if type(cases) != list:
		cases = [cases]
	
	# Command line foo
	def class_name(name):
		i = name.rfind(".")
		if i == -1:	
			return name
		else:
			return name[i+1:]
	
	print 'These are the available test cases:', [class_name(str(case)) for case in cases]
	if len(sys.argv) != 2:
		print 'You must run a single test case by appending its name on the command line'
		sys.exit(0)
		
	cases = [case for case in cases if class_name(str(case)) in sys.argv]
	print 'Running the following test case:', [class_name(str(case)) for case in cases]

	results = []
	def finish():
		LOOP.quit()
		print 'Test Case ended.'
		for result in results:
			success, reason = result
			print '-------\nTestCase:\n%s\n--------' % (reason)
			if not success:
				sys.exit(1)
	
	def run_case(case_class):
		print '----\nTestCase: Creating from class %s' % (str(case_class))
		case = case_class()
		print 'TestCase: Starting %s' % (case)
		try:
			case.run()
			results.append((True, "Success"))
		except Exception, msg:
			print 'TestCase: Failing %s----' % (case)
			print msg
			results.append((False, traceback.format_exc()))
			
		print 'TestCase: Ending %s\n-----' % (case)
	
	def run_all_cases():
		for case in cases:
			run_case(case)
		finish()
	
	gobject.idle_add(run_all_cases)
	LOOP.run()
	
class TestingException(Exception):
	pass


#-----------Start of Client Side Scripting -------------------------------------


class ClientSignalProxy:
	def __init__ (self, provider, proxy, name):
		self.provider = provider
		self.proxy = proxy
		self.name = name
		self.connected = False
	
	def _signal_emitted(self, *args):
		print "%s: Signal '%s' emitted with args:%s" % (self.provider, self.name, args)
		self.results.append(args)
	
	def listen(self):
		"""
		Starts listening for the requested signal. The effect is to reset all counters for this
		signal.
		"""
		if not self.connected:
			print '%s: Connecting to signal "%s"' % (self.provider, self.name)
			self.proxy.connect_to_signal(self.name, self._signal_emitted)
			self.connected = True
		
		print "%s: Listening to signal '%s'" % (self.provider, self.name)
		self.results = []
	
	def wait(self, n=1, timeout=DEFAULT_TIMEOUT):
		"""
		Wait for the signal to happen n times. This method is blocking. If after 'timeout'
		seconds the signal hasn't happened 'n' times, an exception is thrown.
		Returns the list (or the single if n = 1) of values returned by successive
		signal emissions
		"""
		if not self.connected:
			raise TestingException("Waiting for a signal before it's listening")
			
		print "%s: Waiting for signal '%s'" % (self.provider, self.name)
		if len(self.results) != n:
			MainloopClock(lambda: (len(self.results) != n)).sleep(timeout)
		
		results = self.results
		if len(results) != n:
			raise TestingException("%s: Signal '%s' failed to happen %d times: %s" % (self.provider, self.name, n, results))
			
		if n == 1:
			results = self.results[0]
		
		print "%s: Signal '%s' happened %d times: %s" % (self.provider, self.name, n, results)
		return results
		
class ClientProxy:
	def __init__ (self, provider, proxy, name):
		self.provider = provider
		self.name = name
		self.proxy = proxy
		self.signal_proxy = ClientSignalProxy(provider, proxy, name)
		self.async_reply = None
	
	def _reset(self):
		ret = self.async_reply
		self.async_reply = None
		return ret
		
	def _handle_reply(self, *args):
		if len(args) == 1:
			args = args[0]
		print '%s: Got method return value: %s' % (self.provider, args)
		self.async_reply = args
		
	def _handle_error(self, e):
		print e
	
	def _call_async(self, *args, **kwargs):
		print "%s: Calling method %s(%s, %s)" % (self.provider, self.name, args, kwargs)
		if "timeout" not in kwargs:
			kwargs["timeout"] = DEFAULT_TIMEOUT*1000
		getattr(self.proxy, self.name)(reply_handler=self._handle_reply, error_handler=self._handle_error, *args, **kwargs)
		
		# Wait to receive the return value
		while self.async_reply == None:
			LOOP.get_context().iteration(True)
		
		ret = self._reset()
		print "%s: Method %s returned: %s" % (self.provider, self.name, ret)
		return ret
		
	def call(self, *args, **kwargs):
		"""
		Call this method with the given arguments. If timeout=xx is used, the method
		will wait at most xx msecs before returning an error. If it's not used
		the default timeout value is used.
		"""
		print "%s: Calling method %s(%s, %s)" % (self.provider, self.name, args, kwargs)
		if "timeout" not in kwargs:
			kwargs["timeout"] = DEFAULT_TIMEOUT*1000
			
		ret = getattr(self.proxy, self.name)(*args, **kwargs)
		print "%s: Method %s returned: %s" % (self.provider, self.name, ret)
		return ret
		
	def listen(self):
		"""
		Start listening for this signal to happen (reset counters, etc). See ClientSignalProxy.listen
		"""
		# Lazy signal connection
		self.signal_proxy.listen()
		
	def wait(self, n=1, timeout=DEFAULT_TIMEOUT):
		"""
		Wait and block until this signal happens. See ClientSignalProxy.wait
		"""
		# Lazy signal wait
		return self.signal_proxy.wait(n, timeout)
	
class ClientProxyProvider:
	def __init__ (self, provider, interface):
		self.provider = provider
		self.interface = interface
		self.cache = {}
		
	def __getitem__(self, name):
		"""
		Return the ClientProxy for the method or signal named 'name'.
		"""
		if name in self.cache:
			return self.cache[name]
			
		proxy = ClientProxy(self.provider, self.interface, name)
		
		# Cache for future reuses
		self.cache[name] = proxy
		
		return proxy
	
	def __str__(self):
		return str(self.provider)
		
class ClientInterfaceProvider:
	def __init__ (self, provider, proxy, interfaces):
		self.provider = provider
		self.interfaces = {}

		for nick, iface in interfaces.items():
			self.interfaces[nick] = ClientProxyProvider(self, dbus.Interface(proxy, iface))
		
	def __getitem__(self, name):
		"""
		Return the ClientProxyProvider for the interface nicknamed 'name'.
		"""
		return self.interfaces[name]
	
	def __str__(self):
		return str(self.provider)
		
class ClientProvider:
	def __init__ (self, introspect=True):
		self.proxies = {}
		self.introspect = introspect
		# FIXME: this should be solved, the deadlock/introspect thing
		#self.introspect = True
		
	def __getitem__(self, name):
		"""
		Return the ClientInterfaceProvider or ClientProxy for the service nicknamed 'name'
		"""
		return self.proxies[name]
	
	def __setitem__(self, name, value):
		"""
		Create a new Client with the given value. value must be a tuple with three items:
		 service: text of the dbus service,
		 obj: path of the dbus remote object,
		 ifaces: either a string of the interface name implemented by the object OR
		         a dictionnary of name:iface pairs, name being a nickname for the interface, and 
		         iface one of the interfaces implemented by the object.
		"""
		service, obj, ifaces = value
		print "%s: Adding service '%s': %s,%s,%s" % (self, name, service, obj, ifaces)
		proxy = ProxyObject(session_bus, service, obj, self.introspect)
		if type(ifaces) == dict:
			self.proxies[name] = ClientInterfaceProvider(self, proxy, ifaces)
		else:
			self.proxies[name] = ClientProxyProvider(self, dbus.Interface(proxy, ifaces))
	
	def __str__(self):
		return str(self.__class__)
		
#-----------End of Client Side scripting ---------------------------------------

#-----------Start of Service Side Scripting ------------------------------------

# This one intercepts calls to the remote dbus object and log the call, register the arguments
# and sometimes override return value
def intercept(obj):
	"""
	This decorator is to be used on exported MockService method and must be applied *after* the
	@dbus.service.method decorator (that means be written *before* it in source code) for example:

	@intercept
	@dbus.service.method("foo.bar.baz")
	def Foobar(self, arg):
		blah
	
	This is used so the testing framework can wrap the actual method call and do some voodoo around it.
	"""
	def _meth_wrapper(self, *args, **kwargs):
		override = self._called(*args, **kwargs)
		real = obj(self, *args, **kwargs)
		if override != None:
			return override
		return real
		
	# FIXME: HACK: this is a hack to get the dbus method decorator working even if we wrap it
	for i in dir(obj):
		if i.startswith("_dbus"):
			setattr(_meth_wrapper, i, getattr(obj, i))
	
	return _meth_wrapper
	
class MockService(dbus.service.Object):
	"""
	This class is the super class for services definition. Each service you want to use
	must define the exported methods and signals by subclassing this class and using
	the appropriate dbus decorators and @intercept when it's a method.
	"""
	def __init__(self, service, obj):
		service = dbus.service.BusName(service, bus=session_bus)
		dbus.service.Object.__init__(self, service, obj)
	
	def _called(self, *args, **kwargs):
		# To be overridden by ServiceProxy implementation
		# Called by the @intercept decorator on method call
		pass
		
class ServiceProxy:
	def __init__ (self, provider, service, name):
		self.provider = provider
		self.name = name
		self.service = service
		service._called = self._called
		self._reset()
		
	def _reset(self):
		self.received_call = None
		self.return_value = None
			
	def _called(self, *args, **kwargs):
		# Dbus callback
		print '%s: Got method call "%s" with args: %s %s' % (self.provider, self.name, args, kwargs)
		self.received_call = (args, kwargs)
		if self.return_value != None:
			return self.return_value(*args, **kwargs)
	
	def _wait_listening_call(self, timeout=DEFAULT_TIMEOUT):
		print '%s: Waiting for Method Call "%s"' % (self.provider, self.name)

		MainloopClock(lambda: self.received_call == None).sleep(timeout)
		if self.received_call == None:
			raise TestingException("%s: Method Call '%s' failed to happen" % (self.provider, self.name))
				
	def listen_call(self, return_val=None):
		"""
		Starts listening for a method call on this service side. Resets counters and stuff.
		If return_val is not None but is a callable, it will be called with the method
		call arguments, and the return value will override the return value as defined in the MockService Subclass.
		
		use like:
		xxx.listen_call(lambda *args, **kwargs: 3)
		
		will return 3 for this method whatever was defined in the MockService imoplementation
		"""
		self._reset()
		if return_val != None:
			print '%s: Listening for Method Call "%s" with overriden return val' % (self.provider, self.name)
			self.return_value = return_val
		else:
			print '%s: Listening for Method Call "%s"' % (self.provider, self.name)
					
	def wait_call(self, listening=False, return_val=None, timeout=DEFAULT_TIMEOUT):
		"""
		This will block waiting for the method call to effectively happen on service side.
		
		If listening is False, it will automatically starts listening for the call and then block
		while the method isn't called.
		
		If listening is True, it will only wait for a previously listened call to happen (after having called
		manually listen_call())
		
		See listen_call for return_val definition.
		
		timeout is the number of seconds to block at most before throwing an Exception 
		"""
		if not listening:
			self.listen_call(return_val)
			
		self._wait_listening_call(timeout)
	
	def emit(self, *args, **kwargs):
		"""
		Make the service emit this signal with the given arguments.
		"""
		print '%s: Emitting signal "%s" with args: %s %s' % (self.provider, self.name, args, kwargs)
		getattr(self.service, self.name)(*args, **kwargs)

	
class ServiceProxyProvider:
	def __init__ (self, provider, service):
		self.provider = provider
		self.service = service
		self.cache = {}
		
	def __getitem__(self, name):
		"""
		Return the ServiceProxy for the method or signal named 'name'
		"""
		if name in self.cache:
			return self.cache[name]
			
		proxy = ServiceProxy(self.provider, self.service, name)
		
		# Cache for future reuses
		self.cache[name] =  proxy
		
		return proxy
	
	def __str__(self):
		return str(self.provider)
		
class ServiceProvider:
	def __init__(self):
		self.services = {}

	def __setitem__(self, name, value):
		"""
		Creates a service implementation with the given value.
		value must be a 3-tuple whith:
		  klass: the class of the MockService subclass defining the service
		  service: the name of the dbus service (eg. org.foo.bar)
		  obj: the path for this exported object on Dbus
		"""
		klass, service, obj = value
		print "%s: Adding service '%s': %s,%s,%s" % (self, name, klass, service, obj)
		self.services[name] = ServiceProxyProvider(self, klass(service, obj))
		
	def __getitem__(self, name):
		"""
		Return the ServiceProxyProvider for service nicknamed 'name'
		"""
		return self.services[name]
	
	def __str__(self):
		return str(self.__class__)
		
#-----------End of Service Side scripting --------------------------------------

#-----------Start of Mixed Side Scripting ------------------------------------

# The classes defined below are used to adapt service/client interfaces to a mixed mode.
# That means you can use both API's depending on wether the nickname refers
# to a client or a service or a mixed object.
# Mixed object are both a service and a client and implement both capabilities.
# see MixedProvider.__setitem__ for how to construct such objects.
# If you want your test case to use both client and service objects, you should use the
# MixedTestCase superclass.
class MixedProxy:
	def __init__(self, provider, client, service, name):
		self.provider = provider
		self.client = client
		self.service = service
		self.name = name

	# Mirror the client+service api's	
	def emit(self, *args, **kwargs):
		self.service.emit(*args, **kwargs)
		
	def wait_call(self, listening=False, return_val=None, timeout=DEFAULT_TIMEOUT):
		return self.service.wait_call(listening, return_val, timeout)
	
	def listen_call(self, return_val=None):
		self.service.listen_call(return_val)
	
	def call(self, *args, **kwargs):
		return self.client._call_async(*args, **kwargs)
	
	def listen(self):
		self.client.listen()
	
	def wait(self, n=1, timeout=DEFAULT_TIMEOUT):
		return self.client.wait(n, timeout)

class MixedProxyProvider:
	def __init__(self, provider, client, service):
		self.provider = provider
		self.client = client
		self.service = service
		self.cache = {}
		
	def __getitem__(self, name):
		if name in self.cache:
			return self.cache[name]
			
		proxy = MixedProxy(self.provider, self.client[name], self.service[name], name)
		
		# Cache for future reuses
		self.cache[name] = proxy
		
		return proxy
	
	def __str__(self):
		return str(self.provider)
		
class MixedProvider:
	def __init__(self):
		# FIXME: the introspect=True causes a massive deadlock, introspect=False is likely to break things too :)
		self.client = ClientProvider(introspect=False)
		self.service = ServiceProvider()
		self.mappings = {}
	
	def __setitem__(self, name, value):
		"""
		Construct a mixed Proxy, depending on the value a client, a service, or a mixed proxy will be created.
		if value is a 3-uple with first elements being a MockService subclass, a service is created
		 (as defined in ServiceProvider.__setitem__)
		if value is a 3-uple with first element being a string, a client is created
		 (as defined in ClientProvider.__setitem__)
		if value is a 4-uple with:
		  klass: the MockProxy subclass of the service to implement
		  service: the name of dbus service to export
		  object: the name of dbus object to export
		  ifaces: either a dic or a string describing the interfaces implemented by this object
		 then a mixed client/service is created allowing to use both API's on it.
		"""
		if len(value) == 4:
			self.service[name] = value[:3]
			self.client[name] = value[1:]
			self.mappings[name] = MixedProxyProvider(self, self.client[name], self.service[name])
		elif issubclass(value[0], MockService):
			# New service
			self.service[name] = value
			self.mappings[name] = self.service
		else:
			self.client[name] = value
			self.mappings[nickname] = self.client
			
	def __getitem__(self, name):
		return self.mappings[name]
	
	def __str__(self):
		return str(self.__class__)
		
#-----------End of Mixed Side scripting --------------------------------------
			
class ClientTestCase(ClientProvider, TestCase):
	"""
	Subclass this to build a test case using only clients
	"""
	def __init__(self):
		TestCase.__init__(self)
		ClientProvider.__init__(self)
	
class ServiceTestCase(ServiceProvider, TestCase):
	"""
	Subclass this to build a test case using only services
	"""
	def __init__(self):
		TestCase.__init__(self)
		ServiceProvider.__init__(self)

class MixedTestCase(MixedProvider, TestCase):
	"""
	Subclass this to build a test case that uses both clients and services
	"""
	def __init__(self):
		TestCase.__init__(self)
		MixedProvider.__init__(self)

