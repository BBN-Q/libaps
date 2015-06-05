Installation Guide
==================

Hardware
--------

The BBN APS has separate enclosures for the power supply and analog
front-end. A single power supply module has two 5.5V outputs and two
3.3V outputs, which is sufficient to power two analog modules. Using the
supplied cables, connect a pair of power outputs with the same label
(PS1 or PS2) to the power inputs on the rear of an analog module. It is
important to maintain the labeled pairing when connecting to the analog
modules in order to ensure proper power sequencing and optimal noise
performance. These outputs are not hot-pluggable; one must ensure that
the power switch on the front of the supply module is in the off
position before connecting or disconnecting the power cables.

.. _fig-aps-rear-panel:
.. figure::
  images/BBNAPS-rear-panel.jpg
  :scale: 100%
  :align: center

  **BBN APS rear panel.** The rear panel of the APS has a USB connect as well as
  3.3V and 5V power supply inputs. Be careful to connect power outputs on the
  power supply with the same number in order to obtain the best noise
  performance from the device. Never unplug the cables from the power inputs
  while the power supply is on.

Once the power supply has been connected, turn the APS on with the power
switch on the front of the power supply. At this point, the FPGAs in the
analog module are in a blank state, awaiting upload of the pulse
sequencer firmware over the USB interface.

While the APS can run in a standalone configuration, we recommend
running with a 10 MHz (+7 dBm) external reference. This reference must
be supplied at the corresponding front panel input before powering on
the device. Multiple devices can be syncronized by supplying an
appropriate external trigger.

Software
--------

USB Driver
~~~~~~~~~~

The BBN APS requires a USB driver in order to communicate with the host PC.
Prior to plugging the APS into the host computer, you should download and unzip
the driver from the `FTDI website <http://www.ftdichip.com/Drivers/D2XX.htm>'_.
After connecting to a Windows XP/Vista/7 machine, the ‘new hardware’ wizard will
open. Occasionally Windows will find an appropriate driver without further
input, but more often you will need to supply the path to the FTDI driver
folder.

On Linux, for normal user access to the device you will have to add a udev rule.
Adding a file to /etc/rules.d such as ``50-aps-usb-rules`` with the line

.. code:: bash

  # Make available to non-root users
  SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6001", GROUP="users", MODE="0666"

should work.  The kernel bundles a USB virtual com port driver that will take
precedence over the FTDI driver libaps uses. See `this FTDI guide
<http://www.ftdichip.com/Support/Documents/AppNotes/AN_220_FTDI_Drivers_Installation_Guide_for_Linux%20.pdf>`_
for details but running the following commands after every plug-in or power
cycle event will work.

.. code:: bash

  sudo rmmod ftdi_sio
  sudo rmmod usbserial

It should also be possible to automate unbinding the VCO driver using a more
sophisticated udev rule.

libaps
~~~~~~

The APS is driven by a C++ library with a C API. We have provided MATLAB,
python, and LabVIEW bindings to this library such that use of the APS is as
similar as possible in the various instrument control environments. The library
is bundled into a release package that is available in `release tab
<https://github.com/BBN-Q/libaps/releases>`_ of the GitHub site. You simply need
to add the relevant paths to your MATLAB or Python code. In particular for
Python on Linux you will need to add the folder containing ``libaps.so`` to your
``LD_LIBRARY_PATH``.

Python requirements
~~~~~~~~~~~~~~~~~~~

The BBN APS driver for Python requires Python 2.7 or later (but not
Python 3+). You also need a working installation of NumPy and h5py.  We recommend using the `Anaconda python distribution <https://store.continuum.io/cshop/anaconda/>`_.

MATLAB requirements
~~~~~~~~~~~~~~~~~~~

The BBN APS driver for MATLAB requires MATLAB 2010a or later. The driver
does not depend on any toolkits, so a vanilla install is sufficient.

LabVIEW requirements
~~~~~~~~~~~~~~~~~~~~

The BBN APS driver for LabVIEW requires only a relatively recent LabVIEW
installation that supports object-oriented instrument classes (2008 or
later).

Standalone GUI control program
------------------------------

A standalone Win32 application is available for controlling the BBN APS.
This application is available on the downloads section of the BBN Qlab
repository on Github.
