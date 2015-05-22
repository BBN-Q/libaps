API
===========

libaps exports a C API which can be called from any other language that has a
foreign function interface that can interact with a C shared library.

Interface drivers for the BBN APS are available on several platforms
including: MATLAB, Python, and LabVIEW. Effort has been made to use
consistent naming across these interfaces, so that method signatures
look similar in the various platforms. The following methods are
available on all platforms, and are consider ‘public’ methods, in the
sense that the API for these methods is expected to be consistent across
major software versions. Use of methods outside of this list may result
in broken code when receiving future software updates.

Methods
-------

APS()
~~~~~

Inputs
  None

Outputs
  an APS object

Description
  This method instantiates an APS driver object. Creation of this
  object is the first step in all use cases of the driver.

connect(address)
~~~~~~~~~~~~~~~~

Inputs
  integer or string *address* - device ID (integer) or serial number (string)

Outputs
  None

Description
  Opens the USB connection to the APS. May take as input a device ID or a device
  serial number. The device ID is determined by an alphanumeric sorting of the
  connected APS serial numbers. The first device in that sorted list has ID = 0.
  Consequently, if you only have one APS connected, you can assume that it is
  device 0.

disconnect()
~~~~~~~~~~~~

Inputs
  None

Outputs
  None

Description
  Closes the USB connection to the APS. The APS driver allows only one open
  connection at a time, so it is important to include a call to ``disconnect()``
  in your code.

init(force, bitfile)
~~~~~~~~~~~~~~~~~~~~

Inputs
  integer *force* - (optional) 1 = force loading of FPGA firmware, 0 = do not force load (default); string *bitfile* - (optional) fullpath to a valid APS bitfile.

Outputs
  None

Description
  Performs all initialization tasks on the APS. This method should be called by
  all user code between ``connect()`` and all other commands. The driver attempts
  to detect whether initialization is necessary, and will skip most tasks if it
  detects that the APS is in a ready state. You can override these checks and
  force the driver to re-initialize the APS by calling this method with force = 1.

setAll(settings)
~~~~~~~~~~~~~~~~

Inputs
  struct (Python dictionary) *settings* - complete APS settings structure

Outputs
  None

Description
  A single method for doing all setup tasks for the APS. The settings structure
  has the following elements:

  -  chan\_n.enabled
  -  chan\_n.amplitude
  -  chan\_n.offset
  -  samplingRate
  -  triggerSource
  -  seqfile

  where ‘n’ in the channel elements identifies the channel number (1-4).  You can
  see an example usage of ``setAll()`` in the :ref:`sec-api-example`.

loadConfig(path)
~~~~~~~~~~~~~~~~

Inputs
  string *path* - full path to an APS sequence configuration file

Outputs
  None

Description
  Loads a multi-channel sequence configuration files as described in
  :ref:`sec-sequence-files`. ``loadConfig()`` will enable any channel for which
  there is waveform/sequence data in the configuration file, and will set the run
  mode to RUN\_SEQUENCE.

loadWaveform(channel, waveform[])
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Inputs
  integer *channel* - target channel (1-4);  integer/float array *waveform[]* -
  signed 14-bit waveform data in the range (-8191, 8192) or signed float data in
  the range (-1.0, 1.0)

Outputs
  None

Description
  Loads waveform data onto a channel of the APS. Also enables the channel. To load
  sequence data, see loadConfig() and/or setAll().

run()
~~~~~

Inputs
  None

Outputs
  None

Description
  Starts output on all enabled channels. See ``setEnabled()`` to see how to enable a channel.

stop()
~~~~~~

Inputs
  None

Outputs
  None

Description
  Disables output on all enabled channels and resets the pulse sequencer back to
  the beginning of the sequence.

isRunning()
~~~~~~~~~~~

Inputs
  None

Outputs
  boolean

Description
  Returns *true* if any channel of the APS is currently running.

setRunMode(channel, mode)
~~~~~~~~~~~~~~~~~~~~~~~~~

Inputs
  integer *channel* - target channel (1-4); integer *mode* - RUN\_WAVEFORM (0) or
  RUN\_SEQUENCE (1)

Outputs
  None

Description
  Sets the run mode to either directly output the contents of waveform memory,
  or to function as a pulse sequencer, stepping through the loaded link list
  entries.

setOffset(channel, offset)
~~~~~~~~~~~~~~~~~~~~~~~~~~

Inputs
  integer *channel* - target channel (1-4); float *offset* - normalized channel
  offset in range (-1.0, 1.0)

Outputs
  None

Description
  Sets the voltage offset of the specified channel. Note: the APS mimics a
  voltage offset by shifting the waveform data. Consequently, it is possible to
  introduce clipping of the waveform by using this method.

setAmplitude(channel, offset)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Inputs
  integer *channel* - target channel (1-4); float *offset* - channel amplitude/scale factor

Outputs
  None

Description
  Sets the channel scale factor. Note: the APS mimics channel amplitude by
  multiplying the waveform data by the channel scale factor. It is possible to
  introduce clipping of the waveform by using this method.

setEnabled(channel, enabled)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Inputs
  integer *channel* - target channel (1-4); bool *enabled* - enabled state of channel

Outputs
  None

Description
  Enables or disables the specified channel.

setTriggerDelay(channel, delay)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

*Deprecated - will not be supported in future releases*

Inputs
  integer *channel* - target channel (1-4); integer *delay* - channel
  trigger/marker delay with respect to the analog output, specified in units of
  4 sample increments (e.g. delay = 3 is a 12 sample delay)

Description
  Sets a fixed delay of the marker channel associated with a given analog output
  channel.

Properties
----------

samplingRate
~~~~~~~~~~~~

Description
  Set or get the sampling rate (in MS/s). Valid inputs are (1200, 600, 300, 100,
  or 40).

triggerSource
~~~~~~~~~~~~~

Description
  Set the trigger source. Valid inputs are ‘internal’ or ‘external’.

.. _sec-api-example:

Example
-------

This example uses ``setAll()`` rather than calling individual methods.

.. code:: matlab

  % create settings structure
  settings = struct();
  settings.chan_1.enabled = true;
  settings.chan_1.amplitude = 1.0;
  settings.chan_1.offset = 0;
  settings.chan_2.enabled = true;
  settings.chan_2.amplitude = 1.0;
  settings.chan_2.offset = 0;
  settings.chan_3.enabled = true;
  settings.chan_3.amplitude = 0.8;
  settings.chan_3.offset = 0.1;
  settings.chan_4.enabled = true;
  settings.chan_4.amplitude = 1.2;
  settings.chan_4.offset = -0.05;
  settings.samplingRate = 1200;
  settings.triggerSource = `external';
  settings.seqfile = `Ramsey/Ramsey.h5';

  aps = deviceDrivers.APS();
  aps.connect(0);
  aps.init();
  aps.setAll(settings);
  aps.run();

  % acquire data...

  aps.stop();
  aps.disconnect();


The same thing could be accomplished with calls to individual methods:

.. code:: matlab

  aps = deviceDrivers.APS();
  aps.connect(0);
  aps.init();

  % configure the APS
  % set up channels
  aps.setAmplitude(1, 1.0);
  aps.setOffset(1, 0);
  aps.setAmplitude(2, 1.0);
  aps.setOffset(2, 0);
  aps.setAmplitude(3, 0.8);
  aps.setOffset(3, 0.1);
  aps.setAmplitude(4, 1.2);
  aps.setOffset(4, -0.05);

  % load pulse sequence
  aps.loadConfig(`Ramsey/Ramsey.h5');

  % configure output rate and trigger source
  aps.samplingRate = 1200;
  aps.triggerSource = `external';

  aps.run();

  % acquire data...

  aps.stop();
  aps.disconnect();
