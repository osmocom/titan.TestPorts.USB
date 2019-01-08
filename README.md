titan.TestPorts.USB
===================

This implements a TTCN-3 USB test port for Eclipse TITAN.  Running on a (Linux) USB host computer,
it allows you to write TTCN-3 tests for USB device.  This means that the USB host (including
the Linux kernel USB stack, libusb-1.0 and titan.TestPorts.USB form the tester, and the
USB device with its built-in firmware forms the IUT(Implementation under Test).

The idea of this module is to be able to write abstract test suites in TTCN-3 which verify
the functionality, and or interoperability of the interface/protocol they expose.  The first
such implementation for which this TestPort is going to be used is the osmo-ccid program,
a USB device side implementation of the USB CCID (Chipcard Interface Device) class.


GIT repository
--------------

You can clone from the official titan.TestPorts.USB repository using

	git clone https://git.osmocom.org/titan.TestPorts.USB

There's a cgit interface at <https://cgit.osmocom.org/titan.TestPorts.USB/>

Documentation
-------------

This is still very much a Work-In-Progress, and hence there's no documentation yet, sorry.
