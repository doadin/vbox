# -*- coding: utf-8 -*-
# $Id$
"""
VirtualBox Python API Glue.
"""

__copyright__ = \
"""
Copyright (C) 2009-2013 Oracle Corporation

This file is part of VirtualBox Open Source Edition (OSE), as
available from http://www.virtualbox.org. This file is free software;
you can redistribute it and/or modify it under the terms of the GNU
General Public License (GPL) as published by the Free Software
Foundation, in version 2 as it comes in the "COPYING" file of the
VirtualBox OSE distribution. VirtualBox OSE is distributed in the
hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
"""
__version__ = "$Revision$"


# Note! To set Python bitness on OSX use 'export VERSIONER_PYTHON_PREFER_32_BIT=yes'


# Standard Python imports.
import sys, os
import traceback


#
# Globals, environment and sys.path changes.
#
VBoxBinDir = os.environ.get("VBOX_PROGRAM_PATH", None)
VBoxSdkDir = os.environ.get("VBOX_SDK_PATH", None)

if VBoxBinDir is None:
    # Will be set by the installer
    VBoxBinDir = "%VBOX_INSTALL_PATH%"

if VBoxSdkDir is None:
    # Will be set by the installer
    VBoxSdkDir = "%VBOX_SDK_PATH%"

os.environ["VBOX_PROGRAM_PATH"] = VBoxBinDir
os.environ["VBOX_SDK_PATH"] = VBoxSdkDir
sys.path.append(VBoxBinDir)


#
# Import the generated VirtualBox constants.
#
from VirtualBox_constants import VirtualBoxReflectionInfo


class PerfCollector(object):
    """ This class provides a wrapper over IPerformanceCollector in order to
    get more 'pythonic' interface.

    To begin collection of metrics use setup() method.

    To get collected data use query() method.

    It is possible to disable metric collection without changing collection
    parameters with disable() method. The enable() method resumes metric
    collection.
    """

    def __init__(self, mgr, vbox):
        """ Initializes the instance.

        """
        self.mgr = mgr
        self.isMscom = (mgr.type == 'MSCOM')
        self.collector = vbox.performanceCollector

    def setup(self, names, objects, period, nsamples):
        """ Discards all previously collected values for the specified
        metrics, sets the period of collection and the number of retained
        samples, enables collection.
        """
        self.collector.setupMetrics(names, objects, period, nsamples)

    def enable(self, names, objects):
        """ Resumes metric collection for the specified metrics.
        """
        self.collector.enableMetrics(names, objects)

    def disable(self, names, objects):
        """ Suspends metric collection for the specified metrics.
        """
        self.collector.disableMetrics(names, objects)

    def query(self, names, objects):
        """ Retrieves collected metric values as well as some auxiliary
        information. Returns an array of dictionaries, one dictionary per
        metric. Each dictionary contains the following entries:
        'name': metric name
        'object': managed object this metric associated with
        'unit': unit of measurement
        'scale': divide 'values' by this number to get float numbers
        'values': collected data
        'values_as_string': pre-processed values ready for 'print' statement
        """
        # Get around the problem with input arrays returned in output
        # parameters (see #3953) for MSCOM.
        if self.isMscom:
            (values, names, objects, names_out, objects_out, units, scales, sequence_numbers,
                indices, lengths) = self.collector.queryMetricsData(names, objects)
        else:
            (values, names_out, objects_out, units, scales, sequence_numbers,
                indices, lengths) = self.collector.queryMetricsData(names, objects)
        out = []
        for i in xrange(0, len(names_out)):
            scale = int(scales[i])
            if scale != 1:
                fmt = '%.2f%s'
            else:
                fmt = '%d %s'
            out.append({
                'name':str(names_out[i]),
                'object':str(objects_out[i]),
                'unit':str(units[i]),
                'scale':scale,
                'values':[int(values[j]) for j in xrange(int(indices[i]), int(indices[i])+int(lengths[i]))],
                'values_as_string':'['+', '.join([fmt % (int(values[j])/scale, units[i]) for j in xrange(int(indices[i]), int(indices[i])+int(lengths[i]))])+']'
            })
        return out

#
# Attribute hacks.
#
def ComifyName(name):
    return name[0].capitalize()+name[1:]


## This is for saving the original DispatchBaseClass __getattr__ and __setattr__
#  method references.
_g_dCOMForward = {
    'getattr': None,
    'setattr': None,
}

def CustomGetAttr(self, attr):
    # fastpath
    if self.__class__.__dict__.get(attr) != None:
        return self.__class__.__dict__.get(attr)

    # try case-insensitivity workaround for class attributes (COM methods)
    for k in self.__class__.__dict__.keys():
        if k.lower() == attr.lower():
            self.__class__.__dict__[attr] = self.__class__.__dict__[k]
            return getattr(self, k)
    try:
        return _g_dCOMForward['getattr'](self, ComifyName(attr))
    except AttributeError:
        return _g_dCOMForward['getattr'](self, attr)

def CustomSetAttr(self, attr, value):
    try:
        return _g_dCOMForward['setattr'](self, ComifyName(attr), value)
    except AttributeError:
        return _g_dCOMForward['setattr'](self, attr, value)



class PlatformBase(object):
    """
    Base class for the platform specific code.
    """

    def __init__(self, aoParams):
        _ = aoParams;

    def getVirtualBox(self):
        """
        Gets a the IVirtualBox singleton.
        """
        return None;

    def getSessionObject(self, oIVBox):
        """
        Get a session object that can be used for opening machine sessions.

        The oIVBox parameter is an getVirtualBox() return value, i.e. an
        IVirtualBox reference.

        See also openMachineSession.
        """
        _ = oIVBox;
        return None;

    def getType(self):
        """ Returns the platform type (class name sans 'Platform'). """
        return None;

    def isRemote(self):
        """
        Returns True if remote (web services) and False if local (COM/XPCOM).
        """
        return False

    def getArray(self, oInterface, sAttrib):
        """
        Retrives the value of the array attribute 'sAttrib' from
        interface 'oInterface'.

        This is for hiding platform specific differences in attributes
        returning arrays.
        """
        _ = oInterface;
        _ = sAttrib;
        return None;

    def initPerThread(self):
        """
        Does backend specific initialization for the calling thread.
        """
        return True;

    def deinitPerThread(self):
        """
        Does backend specific uninitialization for the calling thread.
        """
        return True;

    def createListener(self, oImplClass, dArgs):
        """
        Instantiates and wraps an active event listener class so it can be
        passed to an event source for registration.

        oImplClass is a class (type, not instance) which implements
        IEventListener.

        dArgs is a dictionary with string indexed variables.  This may be
        modified by the method to pass platform specific parameters. Can
        be None.

        This currently only works on XPCOM.  COM support is not possible due to
        shortcuts taken in the COM bridge code, which is not under our control.
        Use passive listeners for COM and web services.
        """
        _ = oImplClass;
        _ = dArgs;
        raise Exception("No active listeners for this platform");
        return None;

    def waitForEvents(self, cMsTimeout):
        """
        Wait for events to arrive and process them.

        The timeout (cMsTimeout) is in milliseconds for how long to wait for
        events to arrive.  A negative value means waiting for ever, while 0
        does not wait at all.

        Returns 0 if events was processed.
        Returns 1 if timed out or interrupted in some way.
        Returns 2 on error (like not supported for web services).

        Raises an exception if the calling thread is not the main thread (the one
        that initialized VirtualBoxManager) or if the time isn't an integer.
        """
        _ = cMsTimeout;
        return 2;

    def interruptWaitEvents(self):
        """
        Interrupt a waitForEvents call.
        This is normally called from a worker thread to wake up the main thread.

        Returns True on success, False on failure.
        """
        return False;

    def deinit(self):
        """
        Unitializes the platform specific backend.
        """
        return None;

    def queryInterface(self, oIUnknown, sClassName):
        """
        IUnknown::QueryInterface wrapper.

        oIUnknown is who to ask.
        sClassName is the name of the interface we're asking for.
        """
        return None;


class PlatformMSCOM(PlatformBase):
    """
    Platform specific code for MS COM.
    """

    ## @name VirtualBox COM Typelib definitions (should be generate)
    #
    # @remarks Must be updated when the corresponding VirtualBox.xidl bits
    #          are changed.  Fortunately this isn't very often.
    # @{
    VBOX_TLB_GUID  = '{D7569351-1750-46F0-936E-BD127D5BC264}'
    VBOX_TLB_LCID  = 0
    VBOX_TLB_MAJOR = 1
    VBOX_TLB_MINOR = 3
    ## @}


    class ConstantFake(object):
        """ Class to fake access to constants in style of foo.bar.boo """

        def __init__(self, parent, name):
            self.__dict__['_parent'] = parent
            self.__dict__['_name'] = name
            self.__dict__['_consts'] = {}
            try:
                self.__dict__['_depth']=parent.__dict__['_depth']+1
            except:
                self.__dict__['_depth']=0
                if self.__dict__['_depth'] > 4:
                    raise AttributeError

        def __getattr__(self, attr):
            import win32com
            from win32com.client import constants

            if attr.startswith("__"):
                raise AttributeError

            consts = self.__dict__['_consts']

            fake = consts.get(attr, None)
            if fake != None:
               return fake
            try:
               name = self.__dict__['_name']
               parent = self.__dict__['_parent']
               while parent != None:
                  if parent._name is not None:
                    name = parent._name+'_'+name
                  parent = parent._parent

               if name is not None:
                  name += "_" + attr
               else:
                  name = attr
               return win32com.client.constants.__getattr__(name)
            except AttributeError, e:
               fake = PlatformMSCOM.ConstantFake(self, attr)
               consts[attr] = fake
               return fake


    class InterfacesWrapper:
            def __init__(self):
                self.__dict__['_rootFake'] = PlatformMSCOM.ConstantFake(None, None)

            def __getattr__(self, a):
                import win32com
                from win32com.client import constants
                if a.startswith("__"):
                    raise AttributeError
                try:
                    return win32com.client.constants.__getattr__(a)
                except AttributeError, e:
                    return self.__dict__['_rootFake'].__getattr__(a)

    def __init__(self, dParams):
        PlatformBase.__init__(self, dParams);

        #
        # Since the code runs on all platforms, we have to do a lot of
        # importing here instead of at the top of the file where it's normally located.
        #
        from win32com import universal
        from win32com.client import gencache, DispatchBaseClass
        from win32com.client import constants, getevents
        import win32com
        import pythoncom
        import win32api
        from win32con import DUPLICATE_SAME_ACCESS
        from win32api import GetCurrentThread, GetCurrentThreadId, DuplicateHandle, GetCurrentProcess
        import threading

        pid      = GetCurrentProcess()
        self.tid = GetCurrentThreadId()
        handle = DuplicateHandle(pid, GetCurrentThread(), pid, 0, 0, DUPLICATE_SAME_ACCESS)
        self.handles = []
        self.handles.append(handle)

        # Hack the COM dispatcher base class so we can modify method and
        # attribute names to match those in xpcom.
        if _g_dCOMForward['setattr'] is None:
            _g_dCOMForward['getattr'] = DispatchBaseClass.__dict__['__getattr__']
            DispatchBaseClass.__dict__['__getattr__'] = CustomGetAttr
            _g_dCOMForward['setattr'] = DispatchBaseClass.__dict__['__setattr__']
            DispatchBaseClass.__dict__['__setattr__'] = CustomSetAttr

        # Hack the exception base class so the users doesn't need to check for
        # XPCOM or COM and do different things.
        ## @todo


        win32com.client.gencache.EnsureDispatch('VirtualBox.Session')
        win32com.client.gencache.EnsureDispatch('VirtualBox.VirtualBox')

        self.oIntCv = threading.Condition()
        self.fInterrupted = False;

        _ = dParams;

    def getSessionObject(self, oIVBox):
        _ = oIVBox
        import win32com
        from win32com.client import Dispatch
        return win32com.client.Dispatch("VirtualBox.Session")

    def getVirtualBox(self):
        import win32com
        from win32com.client import Dispatch
        return win32com.client.Dispatch("VirtualBox.VirtualBox")

    def getType(self):
        return 'MSCOM'

    def getArray(self, oInterface, sAttrib):
        return oInterface.__getattr__(sAttrib)

    def initPerThread(self):
        import pythoncom
        pythoncom.CoInitializeEx(0)

    def deinitPerThread(self):
        import pythoncom
        pythoncom.CoUninitialize()

    def createListener(self, oImplClass, dArgs):
        if True:
            raise Exception('no active listeners on Windows as PyGatewayBase::QueryInterface() '
                            'returns new gateway objects all the time, thus breaking EventQueue '
                            'assumptions about the listener interface pointer being constants between calls ');
        # Did this code ever really work?
        d = {}
        d['BaseClass'] = oImplClass
        d['dArgs']     = dArgs
        d['tlb_guid']  = PlatformMSCOM.VBOX_TLB_GUID
        d['tlb_major'] = PlatformMSCOM.VBOX_TLB_MAJOR
        d['tlb_minor'] = PlatformMSCOM.VBOX_TLB_MINOR
        str = ""
        str += "import win32com.server.util\n"
        str += "import pythoncom\n"

        str += "class ListenerImpl(BaseClass):\n"
        str += "   _com_interfaces_ = ['IEventListener']\n"
        str += "   _typelib_guid_ = tlb_guid\n"
        str += "   _typelib_version_ = tlb_major, tlb_minor\n"
        str += "   _reg_clsctx_ = pythoncom.CLSCTX_INPROC_SERVER\n"
        # Maybe we'd better implement Dynamic invoke policy, to be more flexible here
        str += "   _reg_policy_spec_ = 'win32com.server.policy.EventHandlerPolicy'\n"

        # capitalized version of listener method
        str += "   HandleEvent=BaseClass.handleEvent\n"
        str += "   def __init__(self): BaseClass.__init__(self, dArgs)\n"
        str += "result = win32com.server.util.wrap(ListenerImpl())\n"
        exec(str, d, d)
        return d['result']

    def waitForEvents(self, timeout):
        from win32api import GetCurrentThreadId
        from win32event import INFINITE
        from win32event import MsgWaitForMultipleObjects, \
                               QS_ALLINPUT, WAIT_TIMEOUT, WAIT_OBJECT_0
        from pythoncom import PumpWaitingMessages
        import types

        if not isinstance(timeout, types.IntType):
            raise TypeError("The timeout argument is not an integer")
        if (self.tid != GetCurrentThreadId()):
            raise Exception("wait for events from the same thread you inited!")

        if timeout < 0:
            cMsTimeout = INFINITE
        else:
            cMsTimeout = timeout
        rc = MsgWaitForMultipleObjects(self.handles, 0, cMsTimeout, QS_ALLINPUT)
        if rc >= WAIT_OBJECT_0 and rc < WAIT_OBJECT_0+len(self.handles):
            # is it possible?
            rc = 2;
        elif rc==WAIT_OBJECT_0 + len(self.handles):
            # Waiting messages
            PumpWaitingMessages()
            rc = 0;
        else:
            # Timeout
            rc = 1;

        # check for interruption
        self.oIntCv.acquire()
        if self.fInterrupted:
            self.fInterrupted = False
            rc = 1;
        self.oIntCv.release()

        return rc;

    def interruptWaitEvents(self):
        """
        Basically a python implementation of NativeEventQueue::postEvent().

        The magic value must be in sync with the C++ implementation or this
        won't work.

        Note that because of this method we cannot easily make use of a
        non-visible Window to handle the message like we would like to do.
        """
        from win32api import PostThreadMessage
        from win32con import WM_USER
        self.oIntCv.acquire()
        self.fInterrupted = True
        self.oIntCv.release()
        try:
            PostThreadMessage(self.tid, WM_USER, None, 0xf241b819)
        except:
            return False;
        return True;

    def deinit(self):
        import pythoncom
        from win32file import CloseHandle

        for h in self.handles:
           if h is not None:
              CloseHandle(h)
        self.handles = None
        pythoncom.CoUninitialize()
        pass

    def queryInterface(self, oIUnknown, sClassName):
        from win32com.client import CastTo
        return CastTo(oIUnknown, sClassName)


class PlatformXPCOM(PlatformBase):
    """
    Platform specific code for XPCOM.
    """

    def __init__(self, dParams):
        PlatformBase.__init__(self, dParams);
        sys.path.append(VBoxSdkDir+'/bindings/xpcom/python/')
        import xpcom.vboxxpcom
        import xpcom
        import xpcom.components
        _ = dParams;

    def getSessionObject(self, oIVBox):
        _ = oIVBox;
        import xpcom.components
        return xpcom.components.classes["@virtualbox.org/Session;1"].createInstance()

    def getVirtualBox(self):
        import xpcom.components
        return xpcom.components.classes["@virtualbox.org/VirtualBox;1"].createInstance()

    def getType(self):
        return 'XPCOM'

    def getArray(self, oInterface, sAttrib):
        return oInterface.__getattr__('get'+ComifyName(sAttrib))()

    def initPerThread(self):
        import xpcom
        xpcom._xpcom.AttachThread()

    def deinitPerThread(self):
        import xpcom
        xpcom._xpcom.DetachThread()

    def createListener(self, oImplClass, dArgs):
        d = {}
        d['BaseClass'] = oImplClass
        d['dArgs']     = dArgs
        str = ""
        str += "import xpcom.components\n"
        str += "class ListenerImpl(BaseClass):\n"
        str += "   _com_interfaces_ = xpcom.components.interfaces.IEventListener\n"
        str += "   def __init__(self): BaseClass.__init__(self, dArgs)\n"
        str += "result = ListenerImpl()\n"
        exec (str, d, d)
        return d['result']

    def waitForEvents(self, timeout):
        import xpcom
        return xpcom._xpcom.WaitForEvents(timeout)

    def interruptWaitEvents(self):
        import xpcom
        return xpcom._xpcom.InterruptWait()

    def deinit(self):
        import xpcom
        xpcom._xpcom.DeinitCOM()

    def queryInterface(self, oIUnknown, sClassName):
        import xpcom.components
        return oIUnknown.queryInterface(getattr(xpcom.components.interfaces, sClassName))


class PlatformWEBSERVICE(PlatformBase):
    """
    VirtualBox Web Services API specific code.
    """

    def __init__(self, dParams):
        PlatformBase.__init__(self, dParams);
        # Import web services stuff.  Fix the sys.path the first time.
        sWebServLib = os.path.join(VBoxSdkDir, 'bindings', 'webservice', 'python', 'lib');
        if sWebServLib not in sys.path:
            sys.path.append(sWebServLib);
        import VirtualBox_wrappers
        from VirtualBox_wrappers import IWebsessionManager2

        # Initialize instance variables from parameters.
        if dParams is not None:
            self.user     = dParams.get("user", "")
            self.password = dParams.get("password", "")
            self.url      = dParams.get("url", "")
        else:
            self.user     = ""
            self.password = ""
            self.url      = None
        self.vbox  = None
        self.wsmgr = None;

    #
    # Base class overrides.
    #

    def getSessionObject(self, oIVBox):
        return self.wsmgr.getSessionObject(oIVBox)

    def getVirtualBox(self):
        return self.connect(self.url, self.user, self.password)

    def getType(self):
        return 'WEBSERVICE'

    def isRemote(self):
        """ Returns True if remote VBox host, False if local. """
        return True

    def getArray(self, oInterface, sAttrib):
        return oInterface.__getattr__(sAttrib)

    def waitForEvents(self, timeout):
        # Webservices cannot do that yet
        return 2;

    def interruptWaitEvents(self, timeout):
        # Webservices cannot do that yet
        return False;

    def deinit(self):
        try:
           disconnect()
        except:
           pass

    def queryInterface(self, oIUnknown, sClassName):
        d = {}
        d['oIUnknown'] = oIUnknown
        str = ""
        str += "from VirtualBox_wrappers import "+sClassName+"\n"
        str += "result = "+sClassName+"(oIUnknown.mgr, oIUnknown.handle)\n"
        # wrong, need to test if class indeed implements this interface
        exec (str, d, d)
        return d['result']

    #
    # Web service specific methods.
    #

    def connect(self, url, user, passwd):
        if self.vbox is not None:
             self.disconnect()
        from VirtualBox_wrappers import IWebsessionManager2
        if url is None:
            url = ""
        self.url = url
        if user is None:
            user = ""
        self.user = user
        if passwd is None:
            passwd = ""
        self.password = passwd
        self.wsmgr = IWebsessionManager2(self.url)
        self.vbox = self.wsmgr.logon(self.user, self.password)
        if not self.vbox.handle:
            raise Exception("cannot connect to '"+self.url+"' as '"+self.user+"'")
        return self.vbox

    def disconnect(self):
        if self.vbox is not None and self.wsmgr is not None:
            self.wsmgr.logoff(self.vbox)
            self.vbox  = None
            self.wsmgr = None



class VirtualBoxManager(object):
    """
    VirtualBox API manager class.

    The API users will have to instantiate this.  If no parameters are given,
    it will default to interface with the VirtualBox running on the local
    machine.  sStyle can be None (default), MSCOM, XPCOM or WEBSERVICES.  Most
    users will either be specifying None or WEBSERVICES.

    The dPlatformParams is an optional dictionary for passing parameters to the
    WEBSERVICE backend.
    """

    def __init__(self, sStyle = None, dPlatformParams = None):
        if sStyle is None:
            if sys.platform == 'win32':
                sStyle = "MSCOM"
            else:
                sStyle = "XPCOM"
        if sStyle == 'XPCOM':
            self.platform = PlatformXPCOM(dPlatformParams);
        elif sStyle == 'MSCOM':
            self.platform = PlatformMSCOM(dPlatformParams);
        elif sStyle == 'WEBSERVICE':
            self.platform = PlatformWEBSERVICE(dPlatformParams);
        else:
            raise Exception('Unknown sStyle=%s' % (sStyle,));
        self.style     = sStyle
        self.type      = self.platform.getType()
        self.remote    = self.platform.isRemote()
        # for webservices, enums are symbolic
        self.constants = VirtualBoxReflectionInfo(sStyle == "WEBSERVICE")

        try:
            self.vbox = self.platform.getVirtualBox()
        except NameError, ne:
            print "Installation problem: check that appropriate libs in place"
            traceback.print_exc()
            raise ne
        except Exception, e:
            print "init exception: ", e
            traceback.print_exc()
            if self.remote:
                self.vbox = None
            else:
                raise e
        ## @deprecated
        # This used to refer to a session manager class with only one method
        # called getSessionObject.  The method has moved into this call.
        self.mgr = self;

    def __del__(self):
        self.deinit()


    #
    # Wrappers for self.platform methods.
    #

    def getVirtualBox(self):
        """ See PlatformBase::getVirtualBox(). """
        return self.platform.getVirtualBox()

    def getSessionObject(self, oIVBox):
        """ See PlatformBase::getSessionObject(). """
        return self.platform.getSessionObject(oIVBox);

    def getArray(self, oInterface, sAttrib):
        """ See PlatformBase::getArray(). """
        return self.platform.getArray(oInterface, sAttrib)

    def createListener(self, oImplClass, dArgs = None):
        """ See PlatformBase::createListener(). """
        return self.platform.createListener(oImplClass, dArgs)

    def waitForEvents(self, cMsTimeout):
        """ See PlatformBase::waitForEvents(). """
        return self.platform.waitForEvents(cMsTimeout)

    def interruptWaitEvents(self):
        """ See PlatformBase::interruptWaitEvents(). """
        return self.platform.interruptWaitEvents()

    def queryInterface(self, oIUnknown, sClassName):
        """ See PlatformBase::queryInterface(). """
        return self.platform.queryInterface(oIUnknown, sClassName)


    #
    # Init and uninit.
    #

    def initPerThread(self):
        """ See PlatformBase::deinitPerThread(). """
        self.platform.initPerThread()

    def deinitPerThread(self):
        """ See PlatformBase::deinitPerThread(). """
        return self.platform.deinitPerThread()

    def deinit(self):
        """
        For unitializing the manager.
        Do not access it after calling this method.
        """
        if hasattr(self, "vbox"):
            del self.vbox
            self.vbox = None
        if hasattr(self, "platform"):
            self.platform.deinit()
            self.platform = None
        return True;


    #
    # Utility methods.
    #

    def openMachineSession(self, oIMachine, fPermitSharing = True):
        """
        Attemts to open the a session to the machine.
        Returns a session object on success.
        Raises exception on failure.
        """
        oSession = self.mgr.getSessionObject(self.vbox);
        if fPermitSharing:
            type = self.constants.LockType_Shared;
        else:
            type = self.constants.LockType_Write;
        oIMachine.lockMachine(oSession, type);
        return oSession;

    def closeMachineSession(self, oSession):
        """
        Closes a session opened by openMachineSession.
        Ignores None parameters.
        """
        if oSession is not None:
            oSession.unlockMachine()
        return True;

    def getPerfCollector(self, oIVBox):
        """
        Returns a helper class (PerfCollector) for accessing performance
        collector goodies.  See PerfCollector for details.
        """
        return PerfCollector(self, oIVBox)

    def getBinDir(self):
        """
        Returns the VirtualBox binary directory.
        """
        global VBoxBinDir
        return VBoxBinDir

    def getSdkDir(self):
        """
        Returns the VirtualBox SDK directory.
        """
        global VBoxSdkDir
        return VBoxSdkDir

