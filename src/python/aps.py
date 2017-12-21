#!/usr/bin/env python

# Copyright 2010 Raytheon BBN Technologies
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import ctypes
from ctypes.util import find_library
import numpy.ctypeslib as npct
import sys
import os

import numpy as np
import h5py

APS_ROOT = os.path.realpath(os.path.join(os.path.dirname(os.path.realpath( __file__ )), '../../'))

# load the shared library
# try with and without "lib" prefix
libpath = find_library("aps")
if libpath is None:
    libpath = find_library("libaps")
# if we still can't find it, then look in python prefix (where conda stores binaries)
if libpath is None:
    try:
        libaps = npct.load_library("libaps", libpath)
    #Finally... load it from this directory
    except OSError:
        libpath = os.path.join(APS_ROOT, 'build')
        libaps = npct.load_library("libaps", libpath)
else:
    libaps = ctypes.CDLL(libpath)

# set up argtypes and restype for functions with arguments that aren't ints or strings
libaps.set_channel_scale.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_float]
libaps.get_channel_scale.restype = ctypes.c_float
libaps.set_channel_offset.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_float]
libaps.get_channel_offset.restype = ctypes.c_float
libaps.set_trigger_interval.argtypes = [ctypes.c_int, ctypes.c_double]
libaps.get_trigger_interval.restype = ctypes.c_double

# initialize the library
libaps.init()

#DAC2 devices use a different bit file
DAC2_SERIALS = ('A6UQZB7Z', 'A6001nBU', 'A6001ixV', 'A6001nBT', 'A6001nBS')

def is_dacii(serial):
    """Check if a given serial number is for a DACII, as these use different bitfiles."""
    return serial in DAC2_SERIALS


class APS(object):
    """Implements an interface to the BBN APS unit via the libaps C library."""

    ## class properties
    device_id = 0
    device_serial = ''
    Address = 0
    is_open = False
    lastSeqFile = ''

    ## constants
    # run modes
    RUN_SEQUENCE = 1
    RUN_WAVEFORM = 0
    # repeat modes
    CONTINUOUS = 0
    TRIGGERED = 1
    # trigger modes
    TRIGGER_INTERNAL = 0
    TRIGGER_EXTERNAL = 1
    VALID_FREQUENCIES = [1200,600,300,100,40]

    def __init__(self):
        pass

    def __del__(self):
        if self.is_open:
            self.disconnect()

    def enumerate(self):
        """List the number of devices attached and their serial numbers.

        Args:
            - None

        Returns:
            A tuple (int, list(str)). The first element is the number of APS
            units found, and the list contains their serial numbers.
        """
        numDevices = libaps.get_numDevices()
        deviceSerials = []
        #For each device, get the associated serial number
        charBuffer = ctypes.create_string_buffer(64)
        for ct in range(numDevices):
            libaps.get_deviceSerial(ct,charBuffer)
            deviceSerials.append(charBuffer.value.decode())
        return numDevices, deviceSerials

    def connect(self, address):
        """Connect to a specific APS unit. If an APS is already connected, it
        will be disconnected.

        Args:
            - address: Serial number of device that should be connected to.
        Returns:
            Status code: 0 if success, 2 if device ID not found.
        """
        if self.is_open:
            self.disconnect()
        numDevices, deviceSerials = self.enumerate()
        ##TODO: Decide if we want to keep the address-by-ID in here, as it's confusing.
        if type(address) is int:
            if address + 1 > numDevices:
                print('APS Device: ', ID, 'not found')
                return 2
            self.device_id = address
            self.device_serial = deviceSerials[address]
            val = libaps.connect_by_ID(self.device_id)
        else:
            assert address in deviceSerials, 'Ooops!  I cannot find that device.'
            self.device_id = deviceSerials.index(address);
            self.device_serial = address
            val = libaps.connect_by_serial(address.encode())
        if val == 0:
            self.is_open = True
        return val

    def disconnect(self):
        """Disconnect from the APS unit.

        Args:
            - None.
        """
        if self.is_open:
            libaps.disconnect_by_ID(self.device_id)
            self.is_open = False


    def read_bitfile_version(self):
        """Read the bitfile version from the APS unit.

        Args:
            - None.
        Returns:
            - Bitfile version
        """
        return self.librarycall('read_bitfile_version')

    def _get_default_bitfile_name(self):
        """Return name of default bitfile.

        Args:
            - None.
        """
        if is_dacii(self.device_serial):
            return os.path.join(APS_ROOT, 'bitfiles/mqco_dac2_latest')
        else:
            return os.path.join(APS_ROOT, 'bitfiles/mqco_aps_latest')

    def init(self, force = False, filename = None):
        """Initialize the APS unit and load the FPGA bitfile. If the default bitfile
        matches the version already loaded, will not reload unless force is True.

        Args:
            - force: Optional, if True forces the APS to reload the bitfile from disk.
            - filename: Optional filename of bitfile to load. If None loads default from
                libaps/bitfiles/ directory.
        Returns:
            - Status code: 0 for success, -1 for failure.
        """
        if not self.is_open:
            return -1
        if filename is None:
            filename = self._get_default_bitfile_name()
        return self.librarycall('initAPS', filename, force)

    def load_waveform(self, ch, waveform):
        """Load a waveform (as numpy array) to a paricular channel on the connected APS.

        Waveform data type must be int16, int32, float32 or float64. Integers must be in
        range (-8191, 8191) and will be cast to int16. Floats must be in range (-1, 1) and
        will be cast to float32. The casts are done using numpy.ndarray.astype(casting='unsafe')
        so data conversions may be done, and ranges are unchecked.

        The channel will also be enabled.

        Args:
            - ch: Channel, integer 1-4
            - waveform: Numpy array of waveform data.
        Returns:
            - Status code: -1 for failure, 0 for success.
        """
        if not self.is_open:
            return -1
        if waveform.dtype == np.dtype('int16') or waveform.dtype == np.dtype('int32'):
            waveform = waveform.astype('int16')
            c_int_p = ctypes.POINTER(ctypes.c_int16)
            waveform_p = waveform.ctypes.data_as(c_int_p)
            val = self.librarycall('set_waveform_int', ch-1, waveform_p, waveform.size)
        elif waveform.dtype == np.dtype('float32') or waveform.dtype == np.dtype('float64'):
            # libaps-cpp expects float rather than double
            waveform = waveform.astype('float32')
            c_float_p = ctypes.POINTER(ctypes.c_float)
            waveform_p = waveform.ctypes.data_as(c_float_p)
            val = self.librarycall('set_waveform_float', ch-1, waveform_p, waveform.size)
        else:
            raise NameError('Unhandled waveform data type. Use int16 or float64')
        self.set_enabled(ch, True)
        return val

    def load_config(self, filename):
        """Load a complete 4 channel configuration file.

        Args:
            - filename: Config file.
        """
        #Pass through to C
        val = self.librarycall('load_sequence_file', str(filename))
        if val < 0:
            raise IOError('Unable to load sequence file {0}. Returned error code: {1}'.format(filename, val))

    def load_LL(self, ch, addr, count, trigger1, trigger2, repeat):
        """ Directly loads link list data into memory

        Args:
            - ch: Channel to load (1-4)
            - addr: Vector of addresses
            - count: vector of counts
            - trigger1: vector of I channel triggers
            - trigger2: vector of Q channel triggers
            - repeat: vector of repeats
        Returns:
            Status code.
        """
        if not self.is_open:
            return -1
        #TODO: we are assuming the arrays are contiguous should we check this?
        # convert each array to uint16 pointer
        c_uint16_p = ctypes.POINTER(ctypes.c_uint16)
        addr = offsets.astype(np.uint16)
        addr_p = offsets.ctypes.data_as(c_uint16_p)
        count = counts.astype(np.uint16)
        count_p = counts.ctypes.data_as(c_uint16_p)
        trigger1 = trigger.astype(np.uint16)
        trigger1_p = trigger.ctypes.data_as(c_uint16_p)
        trigger2 = trigger.astype(np.uint16)
        trigger2_p = trigger.ctypes.data_as(c_uint16_p)
        repeat = repeat.astype(np.uint16)
        repeat_p = repeat.ctypes.data_as(c_uint16_p)
        return self.librarycall('set_LL_data_IQ', ch-1, length(addr), addr_p, count_p, trigger1_p, trigger2_p, repeat_p)

    def run(self):
        """Set the trigger and start things going.

        Args:
            - None.
        """
        self.librarycall('run')

    def stop(self):
        """Stop everything

        Args:
            - None.
        """
        self.librarycall('stop')

    def set_run_mode(self, ch, mode):
        """Set APS channel run mode.

        Args:
            - ch: DAC channel (1-4)
            - mode: 1 = sequence, 0 = waveform
        """
        if mode not in (self.RUN_SEQUENCE, self.RUN_WAVEFORM):
            raise ValueError("Unrecognized run mode: {}.".format(mode))
        self.librarycall('set_run_mode', ch-1, mode)

    def set_repeat_mode(self, ch, mode):
        """Set APS repeat mode.

        Args:
            - ch : DAC channel (1-4)
            - mode : 1 = continuous, 0 = triggered
        """
        if mode not in (self.CONTINUOUS, self.TRIGGERED):
            self.librarycall('set_repeat_mode', ch-1, mode)

    def set_link_list_repeat(self, repeat):
        """Set link list repeat number. 0 indicates no repeats.

        Args:
            - repeat: number of times to loop each miniLL (0 = no repeats)
        """
        self.librarycall('set_miniLL_repeat', repeat)

    @property
    def samplingRate(self):
        return self.librarycall('get_sampleRate')

    @samplingRate.setter
    def samplingRate(self, freq):
        self.librarycall('set_sampleRate', freq)

    @property
    def triggerSource(self):
        valueMap = {self.TRIGGER_INTERNAL: 'internal', self.TRIGGER_EXTERNAL: 'external'}
        return valueMap[self.librarycall('get_trigger_source')]

    @triggerSource.setter
    def triggerSource(self, source):
        allowedValues = {'internal': self.TRIGGER_INTERNAL, 'external': self.TRIGGER_EXTERNAL}
        assert source in allowedValues, 'Unrecognized trigger source.'
        self.librarycall('set_trigger_source', allowedValues[source])

    @property
    def triggerInterval(self):
        return self.librarycall('get_trigger_interval')

    @triggerInterval.setter
    def triggerInterval(self, interval):
        self.librarycall('set_trigger_interval', interval)

    def set_offset(self, ch, offset):
        return self.librarycall('set_channel_offset', ch-1, offset)

    def set_amplitude(self, ch, amplitude):
        return self.librarycall('set_channel_scale', ch-1, amplitude)

    def set_enabled(self, ch, enabled):
        return self.librarycall('set_channel_enabled', ch-1, enabled)

    def set_trigger_delay(self, ch, delay):
        return self.librarycall('set_channel_trigDelay', ch-1, delay)

    def load_waveform_from_file(self, ch, filename):
        '''
        Loads a single channel waveform from an HDF5 file
        Expects data in variable 'WFVec'
        '''
        with h5py.File(filename, 'r') as FID:
            self.loadWaveform(ch, FID['WFVec'].value)

    def setAll(self, settings):
        '''
        Again mimicing the Matlab driver to load all the settings from a dictionary.
        '''

        #First load all the channel offsets, scalings, enabled
        CHANNELNAMES = ('chan_1','chan_2','chan_3','chan_4')
        for ch, channelName in enumerate(CHANNELNAMES):
            self.set_amplitude(ch+1, settings[channelName]['amplitude'])
            self.set_offset(ch+1, settings[channelName]['offset'])
            self.set_enabled(ch+1, settings[channelName]['enabled'])
            self.setRunMode(ch+1, settings['runMode'])
            if 'seqfile' in settings[channelName] and settings[channelName]['seqfile']:
                self.load_waveform_from_file(ch+1, settings[channelName]['seqfile'])

        #Load the sequence file information
        if 'chAll' in settings and settings['chAll']['seqfile']:
            self.load_config(settings['chAll']['seqfile'])
        self.samplingRate = settings['frequency']
        self.triggerSource = settings['triggerSource']
        self.triggerInterval = settings['triggerInterval']

    def set_log_level(self, level):
        '''
        set logging level (info = 2, debug = 3, debug1 = 4, debug2 = 5)
        '''
        libaps.set_logging_level(level)

    def librarycall(self, functionName, *args):
        if not self.is_open:
            print('APS unit is not open')
            return -1
        return getattr(libaps, functionName)(self.device_id, *args)

    def read_PLL_status(self):
        # TODO
        ##Read FPGA1
        #val1 = self.librarycall('Read PLL Sync FPGA1','APS_ReadPllStatus', 1)
        ##Read FPGA2
        #val2 = self.librarycall('Read PLL Sync FPGA2','APS_ReadPllStatus', 2)
        ## functions return 0 on success
        #return val1 and val2
        pass

    def read_register(self, fpga, address):
        return self.librarycall('read_register', fpga, address)

    def unitTestBasic(self):
        self.connect(0)
        print("Initializing")
        self.init(False)
        print("Current Bit File Version: ", self.readBitFileVersion())

        wf = np.hstack((np.zeros((2000),dtype=np.float64), 0.7*np.ones((2000),dtype=np.float64)))

        for ct in range(4):
            self.loadWaveform(ct+1, wf)
            self.setRunMode(ct+1, self.RUN_WAVEFORM)
            self.set_amplitude(ct+1, 1.0)

        print('Done with load Waveform')
        self.run()
        print('Done with Trigger')
        raw_input("Press Enter to continue...")
        self.stop()

        self.samplingRate = 1200

        aps.load_config(APS_ROOT + '/src/lib/UnitTest.h5');
        aps.triggerSource = 'external';
        self.run()
        raw_input("Press Enter to continue...")
        self.stop()
        self.disconnect();


if __name__ == '__main__':
    aps = APS()
    aps.unitTestBasic()
