#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id$

"""
VirtualBox Validation Kit - IAppliance Test #1
"""

__copyright__ = \
"""
Copyright (C) 2010-2015 Oracle Corporation

This file is part of VirtualBox Open Source Edition (OSE), as
available from http://www.virtualbox.org. This file is free software;
you can redistribute it and/or modify it under the terms of the GNU
General Public License (GPL) as published by the Free Software
Foundation, in version 2 as it comes in the "COPYING" file of the
VirtualBox OSE distribution. VirtualBox OSE is distributed in the
hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.

The contents of this file may alternatively be used under the terms
of the Common Development and Distribution License Version 1.0
(CDDL) only, as it comes in the "COPYING.CDDL" file of the
VirtualBox OSE distribution, in which case the provisions of the
CDDL are applicable instead of those of the GPL.

You may elect to license modified versions of this file under the
terms and conditions of either the GPL or the CDDL or both.
"""
__version__ = "$Revision$"


# Standard Python imports.
import os
import sys
import tarfile

# Only the main script needs to modify the path.
try:    __file__
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksValidationKitDir);

# Validation Kit imports.
from testdriver import reporter;
from testdriver import base;
from testdriver import vbox;
from testdriver import vboxwrappers;


class tdAppliance1(vbox.TestDriver):
    """
    IAppliance Test #1.
    """

    def __init__(self):
        vbox.TestDriver.__init__(self);
        self.asRsrcs            = None;


    #
    # Overridden methods.
    #

    def actionConfig(self):
        """
        Import the API.
        """
        if not self.importVBoxApi():
            return False;
        return True;

    def actionExecute(self):
        """
        Execute the testcase.
        """
        fRc = True;

        # Import a set of simple OVAs.
        # Note! Manifests generated by ovftool 4.0.0 does not include the ovf, while the ones b 4.1.0 does.
        for sOva in (
            # t1 is a plain VM without any disks, ovftool 4.0 export from fusion
            'tdAppliance1-t1.ova',
            # t2 is a plain VM with one disk. Both 4.0 and 4.1.0 exports.
            'tdAppliance1-t2.ova',
            'tdAppliance1-t2-ovftool-4.1.0.ova',
            # t3 is a VM with one gzipped disk and selecting SHA256 on the ovftool cmdline (--compress=9 --shaAlgorithm=sha256).
            'tdAppliance1-t3.ova',
            'tdAppliance1-t3-ovftool-4.1.0.ova',
            # t4 is a VM with with two gzipped disk, SHA256 and a (self) signed manifest (--privateKey=./tdAppliance1-t4.pem).
            'tdAppliance1-t4.ova',
            'tdAppliance1-t4-ovftool-4.1.0.ova',
            # t5 is a VM with with one gzipped disk, SHA1 and a manifest signed by a valid (2016) DigiCert code signing certificate.
            'tdAppliance1-t5.ova',
            'tdAppliance1-t5-ovftool-4.1.0.ova',
            # t6 is a VM with with one gzipped disk, SHA1 and a manifest signed by a certificate issued by the t4 certificate,
            # thus it should be impossible to establish a trusted path to a root CA.
            'tdAppliance1-t6.ova',
            'tdAppliance1-t6-ovftool-4.1.0.ova',
            ):
            reporter.testStart(sOva);
            try:
                fRc = self.testImportOva(os.path.join(g_ksValidationKitDir, 'tests', 'api', sOva)) and fRc;
                fRc = self.testImportOvaAsOvf(os.path.join(g_ksValidationKitDir, 'tests', 'api', sOva)) and fRc;
            except:
                reporter.errorXcpt();
                fRc = False;
            fRc = reporter.testDone() and fRc;

        ## @todo more stuff
        return fRc;

    #
    # Test execution helpers.
    #

    def testImportOva(self, sOva):
        """ xxx """
        oVirtualBox = self.oVBoxMgr.getVirtualBox();

        #
        # Import it as OVA.
        #
        try:
            oAppliance = oVirtualBox.createAppliance();
        except:
            return reporter.errorXcpt('IVirtualBox::createAppliance failed');
        print "oAppliance=%s" % (oAppliance,)

        try:
            oProgress = vboxwrappers.ProgressWrapper(oAppliance.read(sOva), self.oVBoxMgr, self, 'read "%s"' % (sOva,));
        except:
            return reporter.errorXcpt('IAppliance::read("%s") failed' % (sOva,));
        oProgress.wait();
        if oProgress.logResult() is False:
            return False;

        try:
            oAppliance.interpret();
        except:
            return reporter.errorXcpt('IAppliance::interpret() failed on "%s"' % (sOva,));

        #
        try:
            oProgress = vboxwrappers.ProgressWrapper(oAppliance.importMachines([]),
                                                     self.oVBoxMgr, self, 'importMachines "%s"' % (sOva,));
        except:
            return reporter.errorXcpt('IAppliance::importMachines failed on "%s"' % (sOva,));
        oProgress.wait();
        if oProgress.logResult() is False:
            return False;

        #
        # Export the
        #
        ## @todo do more with this OVA. Like untaring it and loading it as an OVF.  Export it and import it again.

        return True;

    def testImportOvaAsOvf(self, sOva):
        """
        Unpacts the OVA into a subdirectory in the scratch area and imports it as an OVF.
        """
        oVirtualBox = self.oVBoxMgr.getVirtualBox();

        sTmpDir = os.path.join(self.sScratchPath, os.path.split(sOva)[1] + '-ovf');
        sOvf    = os.path.join(sTmpDir, os.path.splitext(os.path.split(sOva)[1])[0] + '.ovf');

        #
        # Unpack
        #
        try:
            os.mkdir(sTmpDir, 0755);
            oTarFile = tarfile.open(sOva, 'r:*');
            oTarFile.extractall(sTmpDir);
            oTarFile.close();
        except:
            return reporter.errorXcpt('Unpacking "%s" to "%s" for OVF style importing failed' % (sOvf, sTmpDir,));

        #
        # Import.
        #
        try:
            oAppliance2 = oVirtualBox.createAppliance();
        except:
            return reporter.errorXcpt('IVirtualBox::createAppliance failed (#2)');
        print "oAppliance2=%s" % (oAppliance2,)

        try:
            oProgress = vboxwrappers.ProgressWrapper(oAppliance2.read(sOvf), self.oVBoxMgr, self, 'read "%s"' % (sOvf,));
        except:
            return reporter.errorXcpt('IAppliance::read("%s") failed' % (sOvf,));
        oProgress.wait();
        if oProgress.logResult() is False:
            return False;

        try:
            oAppliance2.interpret();
        except:
            return reporter.errorXcpt('IAppliance::interpret() failed on "%s"' % (sOvf,));

        try:
            oProgress = vboxwrappers.ProgressWrapper(oAppliance2.importMachines([]),
                                                     self.oVBoxMgr, self, 'importMachines "%s"' % (sOvf,));
        except:
            return reporter.errorXcpt('IAppliance::importMachines failed on "%s"' % (sOvf,));
        oProgress.wait();
        if oProgress.logResult() is False:
            return False;

        return True;

if __name__ == '__main__':
    sys.exit(tdAppliance1().main(sys.argv));

