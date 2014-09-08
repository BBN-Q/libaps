Sequence File Specification
============================

Sequences are programmed using an HDF5 file with the following layout (n is an integer between 1-4):

| /version - attribute indicating file version number
| /channelDataFor - attribute containing array of integers specifying channels for which data is supplied in the file
| /miniLLRepeat - attribute containing default number of times to repeat each mini link list (0 = play without repeats)
| /chan_n/isListListData - integer attribute specifying whether link list data is supplied for this channel
| /chan_n/isIQMode - integer attribute specifying whether this channel contains data for an I/Q pair (default = 1)
| /chan_n/waveformLib - int16 vector of 14-bit waveform values, sign-extended to 16-bits
| /chan_n/linkListData/length - integer attribute specifying number of link list entries
| /chan_n/linkListData/addr - int16 vector of waveform addresses
| /chan_n/linkListData/count - int16 vector of waveform lengths
| /chan_n/linkListData/repeat - int16 vector of repeats
| /chan_n/linkListData/trigger1 - int16 vector of offset counts for trigger1 pulses
| /chan_n/linkListData/trigger2 - int16 vector of offset counts for trigger2 pulses


Link list field formats
-----------------------

An individual sequence entry consists of a value from each of the addr, count,
repeat, trigger1, and trigger2 fields. Data is encoded in these fields in the
following way:

Address: 16-bit offset in quad samples into waveform memory.

Count: length of waveform in quad samples minus one. For example, a waveform
that is 16 samples long has count = 3. The minimum count is 2.

Repeat: 10-bit repeat count for the waveform. Bits 10-11 are reserved. Bit 15
is the START_MINILL flag. Bit 14 is the END_MINILL flag. Bit 13 is the
WAIT_FOR_TRIG flag. Bit 12 is the TA_PAIR flag.

Trigger1/2: offset in quad samples to output a trigger pulse on the
corresponding marker output channel. A value of zero means no pulse.
Accordingly, it is not possible to have a pulse aligned with the first sample
of a waveform.
