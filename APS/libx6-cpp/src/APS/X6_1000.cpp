#include "X6_1000.h"

#include <IppMemoryUtils_Mb.h>  // for Init::UsePerformanceMemoryFunctions
#include <BufferDatagrams_Mb.h> // for ShortDG
#include <algorithm>            // std::max

/* Provides Main Loop to distribute thunked messages */
/* Current unncessary as of 3/27/2013 */
void thunkLooper() {
while (true) { 
   Innovative::Thunker::MainLoopEvent.WaitFor(); 
   while (!Innovative::Thunker::MainLoopQueue.empty()) { 
     Innovative::Thunker *thnk = Innovative::Thunker::MainLoopQueue.front(); 
     Innovative::Thunker::MainLoopQueue.pop(); 
     thnk->Dispatch(); 
   } 
  }
}

using namespace Innovative;

// flag to enable threaded Malibu opperation with Syncronize and Thunk
// threaded operation current not needed
bool X6_1000::enableThreading_ = false;

// default constructor
X6_1000::X6_1000() {
	X6_1000(0);
}

X6_1000::X6_1000(unsigned int target) :
    deviceID_(target), isOpened_(false), triggerInterval_(1000.0)
{
    numBoards_ = getBoardCount();

    for(int cnt = 0; cnt < get_num_channels(); cnt++) {
        activeChannels_[cnt] = false;
        chData_[cnt].clear(); // initalize vector
    }

    // Use IPP performance memory functions.    
    Init::UsePerformanceMemoryFunctions();

    // Timer Interval in milleseconds
    set_trigger_interval(triggerInterval_);
}

X6_1000::~X6_1000()
{
	if (isOpened_) Close();   
}

unsigned int X6_1000::get_num_channels() {
    return module_.Output().Channels();
}


X6_1000::ErrorCodes X6_1000::set_deviceID(unsigned int deviceID) {
	if (!isOpened_ && deviceID < numBoards_)
		deviceID_ = deviceID;
	else
		return MODULE_ERROR;
    return SUCCESS;
}


unsigned int  X6_1000::getBoardCount() {
    static Innovative::X6_1000M  x6;
    return static_cast<unsigned int>(x6.BoardCount());

}

void X6_1000::get_device_serials(vector<string> & deviceSerials) {
	deviceSerials.clear();

	int numBoards = getBoardCount();

  	// TODO: Identify a way to get serial number from X6 board if possible otherwise get slot id etc
	for (int cnt = 0; cnt < numBoards; cnt++) {

		// SNAFU work around for compiler on MQCO11 reporting that to_string is not part of std
		std::stringstream out;
		out << "S" << cnt;

		deviceSerials.push_back(out.str());
	}
}

bool X6_1000::isOpen() {
	return isOpened_;
}

void X6_1000::setHandler(OpenWire::EventHandler<OpenWire::NotifyEvent> & event, 
    void (X6_1000:: *CallBackFunction)(OpenWire::NotifyEvent & Event),
    bool useSyncronizer) {

    event.SetEvent(this, CallBackFunction );
    if (!enableThreading_) {
        FILE_LOG(logINFO) << "Using event.Unsynchronize";
        event.Unsynchronize();
    } else {
       
        if (useSyncronizer) {
            FILE_LOG(logINFO) << "Using event.Synchronize";
            event.Synchronize();
        } else {
            FILE_LOG(logINFO) << "Using event.Thunk";
            event.Thunk();
        }
    }
}


X6_1000::ErrorCodes X6_1000::Open() {

    setHandler(timer_.OnElapsed,  &X6_1000::HandleTimer, false);

 	// open function based on Innovative Stream Example ApplicationIO.cpp
 	trigger_.OnDisableTrigger.SetEvent(this, &X6_1000::HandleDisableTrigger);
    trigger_.OnExternalTrigger.SetEvent(this, &X6_1000::HandleExternalTrigger);
    trigger_.OnSoftwareTrigger.SetEvent(this, &X6_1000::HandleSoftwareTrigger);
    trigger_.DelayedTrigger(true); // trigger delayed after start
        
    setHandler(module_.OnBeforeStreamStart, &X6_1000::HandleBeforeStreamStart);
    setHandler(module_.OnAfterStreamStart, &X6_1000::HandleAfterStreamStart);
    setHandler(module_.OnAfterStreamStop,  &X6_1000::HandleAfterStreamStop);


    //  Alerts
    module_.Alerts().OnTimeStampRolloverAlert.SetEvent(this, &X6_1000::HandleTimestampRolloverAlert);
    module_.Alerts().OnSoftwareAlert.SetEvent(         this, &X6_1000::HandleSoftwareAlert);
    module_.Alerts().OnWarningTemperature.SetEvent(    this, &X6_1000::HandleWarningTempAlert);
    module_.Alerts().OnOutputUnderflow.SetEvent(       this, &X6_1000::HandleOutputFifoUnderflowAlert);
    module_.Alerts().OnTrigger.SetEvent(               this, &X6_1000::HandleTriggerAlert);
    module_.Alerts().OnOutputOverrange.SetEvent(       this, &X6_1000::HandleOutputOverrangeAlert);
    // Input 
    module_.Alerts().OnInputOverflow.SetEvent(         this, &X6_1000::HandleInputFifoOverrunAlert);
    module_.Alerts().OnInputOverrange.SetEvent(        this, &X6_1000::HandleInputOverrangeAlert);

    //  Configure Stream Event Handlers
    stream_.OnVeloDataRequired.SetEvent(this, &X6_1000::HandleDataRequired);
    stream_.DirectDataMode(false);
    stream_.OnVeloDataAvailable.SetEvent(this, &X6_1000::HandleDataAvailable);

    stream_.RxLoadBalancing(true);
    stream_.TxLoadBalancing(true);


    // Insure BM size is a multiple of four MB
    const int RxBmSize = std::max(BusmasterSize/4, 1) * 4;
    const int TxBmSize = std::max(BusmasterSize/4, 1) * 4;
    module_.IncomingBusMasterSize(RxBmSize * Meg);
    module_.OutgoingBusMasterSize(TxBmSize * Meg);
    module_.Target(deviceID_);

    try {
        module_.Open();
        FILE_LOG(logINFO) << "Opened Device " << deviceID_;
        FILE_LOG(logINFO) << "Bus master size: Input => " << RxBmSize << " MB" << " Output => " << TxBmSize << " MB";
    }
    catch(...) {
        FILE_LOG(logINFO) << "Module Device Open Failure!";
        return MODULE_ERROR;
    }
        
    module_.Reset();
    FILE_LOG(logINFO) << "Module Device Opened Successfully...";
    
    isOpened_ = true;

    log_card_info();

    set_defaults();
    
    //  Connect Stream
    stream_.ConnectTo(&module_);
    
    FILE_LOG(logINFO) << "Stream Connected..." << endl;

    //  Initialize VeloMergeParse
    //VMP.OnDataAvailable.SetEvent(this, &ApplicationIo::VMPDataAvailable);
    //std::vector<int> sids = module_.AllInputVitaStreamIdVector();
    //VMP.Init( sids );
    //
    return SUCCESS;
  }

 
X6_1000::ErrorCodes X6_1000::Close() {
    stream_.Disconnect();
    module_.Close();

    isOpened_ = true;

    FILE_LOG(logINFO) << "Closed X6 Board " << deviceID_;

	return SUCCESS;
}

float X6_1000::get_logic_temperature() {
    return static_cast<float>(module_.Thermal().LogicTemperature());
}

X6_1000::ErrorCodes X6_1000::set_reference(X6_1000::ExtInt ref, float frequency) {
    IX6ClockIo::IIReferenceSource x6ref; // reference source
    if (frequency < 0) return INVALID_FREQUENCY;

    x6ref = (ref == EXTERNAL) ? IX6ClockIo::rsExternal : IX6ClockIo::rsInternal;

    module_.Clock().Reference(x6ref);
    module_.Clock().ReferenceFrequency(frequency * MHz);
    return SUCCESS;
}

X6_1000::ErrorCodes X6_1000::set_clock(X6_1000::ExtInt src , 
                                       float frequency,
                                       ExtSource extSrc) {

    IX6ClockIo::IIClockSource x6clksrc; // clock source
    IX6ClockIo::IIClockSelect x6extsrc; // external clock source
    if (frequency < 0) return INVALID_FREQUENCY;

    x6clksrc = (src ==  EXTERNAL) ? IX6ClockIo::csExternal : IX6ClockIo::csInternal;
    x6extsrc = (extSrc == FRONT_PANEL) ? IX6ClockIo::cslFrontPanel : IX6ClockIo::cslP16;

    module_.Clock().ExternalClkSelect(x6extsrc);
    module_.Clock().Source(x6clksrc);
    module_.Clock().Frequency(frequency * MHz);
    return SUCCESS;
}

X6_1000::ErrorCodes X6_1000::set_ext_trigger_src(X6_1000::ExtSource extSrc) {
    IX6IoDevice::AfeExtSyncOptions syncsel;
    syncsel = (extSrc == FRONT_PANEL) ? IX6IoDevice::essFrontPanel: IX6IoDevice::essP16;
    module_.Output().Trigger().ExternalSyncSource( syncsel );
    module_.Input().Trigger().ExternalSyncSource( syncsel );
    return SUCCESS;
}

X6_1000::ErrorCodes X6_1000::set_trigger_src(
                                TriggerSource trgSrc,
                                bool framed,
                                bool edgeTrigger,
                                unsigned int frameSize) {
    // cache trigger source
    triggerSource_ = trgSrc;

    FILE_LOG(logINFO) << "Trigger Source set to " << (trgSrc == EXTERNAL_TRIGGER) ? "External" : "Internal";

    trigger_.DelayedTriggerPeriod(triggerDelayPeriod_);
    trigger_.ExternalTrigger( (trgSrc == EXTERNAL_TRIGGER) ? true : false);
    trigger_.AtConfigure();

    module_.Output().Trigger().FramedMode(framed);
    module_.Output().Trigger().Edge(edgeTrigger);
    module_.Output().Trigger().FrameSize(frameSize); 
    return SUCCESS;
}

X6_1000::TriggerSource X6_1000::get_trigger_src() {
    // return cached trigger source until 
    // TODO: identify method for getting source from card
    if (triggerSource_) 
        return EXTERNAL_TRIGGER;
    else
        return SOFTWARE_TRIGGER;
}

X6_1000::ErrorCodes X6_1000::set_trigger_interval(const double & interval) {
    if (interval <= 0) return INVALID_INTERVAL; 
    FILE_LOG(logDEBUG) << "Setting Trigger Interval to: " << interval;
    triggerInterval_ = interval;
    timer_.Interval(triggerInterval_); 
}

double X6_1000::get_trigger_interval() const {return triggerInterval_; }

X6_1000::ErrorCodes X6_1000::set_decimation(bool enabled, int factor) {
    module_.Output().Decimation( (enabled ) ? factor : 0);
    module_.Input().Decimation((enabled ) ? factor : 0); 
    return SUCCESS;
}

X6_1000::ErrorCodes X6_1000::set_channel_enable(int channel, bool enabled) {
    if (channel >= get_num_channels()) return INVALID_CHANNEL;
    FILE_LOG(logINFO) << "Set Channel " << channel << " Enable = " << enabled;
    activeChannels_[channel] = enabled;
    return SUCCESS;
}

bool X6_1000::get_channel_enable(int channel) {
    // TODO get active channel status from board
    if (channel >= get_num_channels()) return false;
    else return activeChannels_[channel];
}


X6_1000::ErrorCodes X6_1000::set_active_channels() {
    ErrorCodes status = SUCCESS;

    module_.Output().ChannelDisableAll();
    module_.Input().ChannelDisableAll();

    for (int cnt = 0; cnt < get_num_channels(); cnt++) { 
        FILE_LOG(logINFO) << "Channel " << cnt << " Enable = " << activeChannels_[cnt];
        module_.Output().ChannelEnabled(cnt, activeChannels_[cnt]);
    }
    return status;
}

double X6_1000::get_pll_frequency() {
    double freq = module_.Clock().FrequencyActual();
    return (freq / MHz);
}

void X6_1000::set_defaults() {
    set_clock();
    set_reference();
    set_ext_trigger_src();
    set_trigger_src();
    set_decimation();
    set_active_channels();

    // disable test mode 
    module_.Input().TestModeEnabled( false, wfType_);
    module_.Output().TestModeEnabled( false, wfType_);
}

X6_1000::ErrorCodes X6_1000::write_waveform(const int & channel, const vector<short> & wfData) {
    if (channel >= get_num_channels()) return INVALID_CHANNEL;
    // copy data replacing existing data
    chData_[channel] = wfData;
    return SUCCESS;
}

/**************************************************************
* Waveform source demo code
* This code is to demo access to default II FGPA wavform source
* from library.
***************************************************************/

void X6_1000::log_card_info() {

    FILE_LOG(logINFO) << std::hex << "Logic Version: " << module_.Info().FpgaLogicVersion()
        << ", Hdw Variant: " << module_.Info().FpgaHardwareVariant()
        << ", Revision: " << module_.Info().PciLogicRevision()
        << ", Subrevision: " << module_.Info().FpgaLogicSubrevision();

    FILE_LOG(logINFO)  << std::hex << "Board Family: " << module_.Info().PciLogicFamily()
        << ", Type: " << module_.Info().PciLogicType()
        << ", Board Revision: " << module_.Info().PciLogicPcb()
        << ", Chip: " << module_.Info().FpgaChipType();

    FILE_LOG(logINFO)  << "PCI Express Lanes: " << module_.Debug()->LaneCount();
}

X6_1000::ErrorCodes X6_1000::enable_test_generator(X6_1000::FPGAWaveformType wfType, float frequencyMHz) {
    // Mimic Test Generater Mode from Stream Example

    wfType_ = wfType;

    set_active_channels();
    FILE_LOG(logINFO) << "stream_.Preconfigure();";
    stream_.Preconfigure();
    
   //  Output Test Generator Setup
    module_.Output().TestModeEnabled( true, wfType_ );  // enable , mode
    module_.Output().TestFrequency( frequencyMHz * 1e6 ); // frequency in Hz

    // enable software trigger
    module_.Output().SoftwareTrigger(true);

    module_.Output().Pulse().Reset();
    module_.Output().Pulse().Enabled(false);
    
    // disable prefill
    stream_.PrefillPacketCount(0);
    FILE_LOG(logINFO) << "trigger_.AtStreamStart();";
    trigger_.AtStreamStart();
    //  Start Streaming
    FILE_LOG(logINFO) << "stream_.Start();";
    
    // start threadlooper
    if (enableThreading_) {
        threadHandle = new thread(thunkLooper);
    }
    stream_.Start();
    timer_.Enabled(true);


    FILE_LOG(logINFO) << "SUCCESS";
    return SUCCESS;
}

X6_1000::ErrorCodes X6_1000::Start() {
    // Mimic Test Generater Mode from Stream Example

    set_active_channels();
    FILE_LOG(logINFO) << "stream_.Preconfigure();";
    stream_.Preconfigure();
    
   //  Output Test Generator Setup
    module_.Output().TestModeEnabled( false, wfType_ );  // enable , mode

    // enable software trigger
    module_.Output().SoftwareTrigger(true);

    module_.Output().Pulse().Reset();
    module_.Output().Pulse().Enabled(false);
    
    // disable prefill
    stream_.PrefillPacketCount(0);
    FILE_LOG(logINFO) << "trigger_.AtStreamStart();";
    trigger_.AtStreamStart();
    //  Start Streaming
    FILE_LOG(logINFO) << "stream_.Start();";
    
    // start threadlooper
    if (enableThreading_) {
        threadHandle = new thread(thunkLooper);
    }
    stream_.Start();
    timer_.Enabled(true);


    FILE_LOG(logINFO) << "SUCCESS";
    return SUCCESS;
}

X6_1000::ErrorCodes X6_1000::disable_test_generator() {
    module_.Output().TestModeEnabled( false, wfType_);
    return Stop();
}

X6_1000::ErrorCodes X6_1000::Stop() {
    stream_.Stop();
    timer_.Enabled(false);
    trigger_.AtStreamStop();
    module_.Output().SoftwareTrigger(false);
    return SUCCESS;
}

/****************************************************************************
 * Event Handlers 
 ****************************************************************************/

 void  X6_1000::HandleDisableTrigger(OpenWire::NotifyEvent & /*Event*/) {
    FILE_LOG(logINFO) << "X6_1000::HandleDisableTrigger";
    module_.Output().Trigger().External(false);
    module_.Input().Trigger().External(false);
}


void  X6_1000::HandleExternalTrigger(OpenWire::NotifyEvent & /*Event*/) {
    FILE_LOG(logINFO) << "X6_1000::HandleExternalTrigger";
    //if (Settings.Rx.ExternalTrigger == 1) 
    // module_.Input().Trigger().External(true);
    if (triggerSource_ == EXTERNAL_TRIGGER)
        module_.Output().Trigger().External(true);
}


void  X6_1000::HandleSoftwareTrigger(OpenWire::NotifyEvent & /*Event*/) {
    FILE_LOG(logINFO) << "X6_1000::HandleSoftwareTrigger";
    //if (Settings.Rx.ExternalTrigger == 0) 
    //    module_.Input().SoftwareTrigger(true);
    if (triggerSource_ == SOFTWARE_TRIGGER) 
        module_.Output().SoftwareTrigger(true);
}

void X6_1000::HandleBeforeStreamStart(OpenWire::NotifyEvent & /*Event*/) {
    FILE_LOG(logINFO) << "X6_1000::HandleBeforeStreamStart";
}

void X6_1000::HandleAfterStreamStart(OpenWire::NotifyEvent & /*Event*/) {
    FILE_LOG(logINFO) << "X6_1000::HandleAfterStreamStart";
    timer_.Enabled(true);
}

void X6_1000::HandleAfterStreamStop(OpenWire::NotifyEvent & /*Event*/) {
    FILE_LOG(logINFO) << "X6_1000::HandleAfterStreamStop";
    // Disable external triggering initially
    module_.Input().SoftwareTrigger(false);
    module_.Input().Trigger().External(false);
    //  Output Remaining Data
    // VMP.Flush();
    // VMPLogger.Stop();
    // InitVMPBddFile(VMPGraph);
    //Player.Stop();
}

void X6_1000::HandlePackedDataAvailable(Innovative::VitaPacketPackerDataAvailable & Event) { 
    outputPacket_ = Event.Data; 
}

void X6_1000::HandleDataRequired(VitaPacketStreamDataEvent & Event) {
    FILE_LOG(logINFO) << "X6_1000::HandleDataRequired";
    return;
    
    // the total VITA packet size is limited by 2^16-1 words (32bit), out of which 8
    // are used for header and trailer
    const size_t MAX_VITA_PACKET_DATA_SIZE = ( 0xffff - 8 ) * 4;


    Innovative::VitaBuffer vitaPacket;
    Innovative::ShortDG DG(vitaPacket);

    size_t leftToWrite = 0;
    
    // get channel 0 / 1 stream ID
    int streamID = module_.VitaOut().VitaStreamId(0);

    Innovative::VitaPacketPacker VPPk(leftToWrite/4+100);
    VPPk.OnDataAvailable.SetEvent(this, &X6_1000::HandlePackedDataAvailable);

    unsigned int numActiveChannels = 0;
    size_t maxSampleNum = 0;

    // determine number of active channels and max sample
    for (int ch = 0; ch < get_num_channels(); ch++) {
        if (activeChannels_[ch]) {
            maxSampleNum = max(maxSampleNum, chData_[ch].size());
        }
    }

    leftToWrite = numActiveChannels * maxSampleNum;

    unsigned int waveformIndex = 0;
    unsigned int numChannels = get_num_channels();
    
    size_t packet = 0;
    // send data for each channel
    
    while (leftToWrite > 0)
    {
        ClearHeader(vitaPacket);
        ClearTrailer(vitaPacket);

        size_t writeNow = min(leftToWrite,MAX_VITA_PACKET_DATA_SIZE);
        DG.Resize(writeNow);

        unsigned int numSamples = writeNow / numActiveChannels;

        for(int sample = 0; sample < numSamples; sample++) {
            for(int ch = 0; ch < numChannels; ch++) {
                int idx = sample*get_num_channels()+ch;
                DG[idx] = (waveformIndex < chData_[ch].size() && activeChannels_[ch]) ? chData_[ch][waveformIndex] : 0;
            }
            waveformIndex++;
        }
            
        InitHeader(vitaPacket);
        InitTrailer(vitaPacket);

        Innovative::VitaHeaderDatagram VitaH( vitaPacket );

        VitaH.StreamId(streamID);
        VitaH.PacketCount(static_cast<int>(packet));

        //  Shove in the new VITA packet
        VPPk.Pack( vitaPacket );

        leftToWrite -= writeNow;
        packet++;
    }

    VPPk.Flush();   // outputs the one waveform buffer into outputPacket_

    Innovative::ClearHeader(outputPacket_);
    Innovative::InitHeader(outputPacket_);   // make sure header packet size is valid...

    Innovative::VeloHeaderDatagram headerDG(outputPacket_);

    stream_.Send(0,outputPacket_); // send to peripheral ID 0
}

void  X6_1000::HandleDataAvailable(VitaPacketStreamDataEvent & Event) {
    FILE_LOG(logINFO) << "X6_1000::HandleDataRequired";

    // if (Stopped)
    //     return;

    // VeloBuffer Packet;

    // //
    // //  Extract the packet from the Incoming Queue...
    // Event.Sender->Recv(Packet);

    // if (Settings.Rx.MergeParseEnable==false)
    //     {
    //     //  normal processing
    //     if (Settings.Rx.LoggerEnable)
    //         if (FWordCount < WordsToLog)
    //             {
    //             Logger.LogWithHeader(Packet);
    //             }
    //     }
    // else
    //     {
    //     //  merge parse processing
    //     VMP.Append(Packet);
    //     VMP.Parse();
    //     }
 

    // IntegerDG Packet_DG(Packet);

    // TallyBlock(Packet_DG.size()*sizeof(int));

    // FWordCount += Packet.SizeInInts();
    // // Per block triggering actions
    // Trig.AtBlockProcess(static_cast<unsigned int>(Packet_DG.size()*sizeof(int)));
}

void X6_1000::HandleTimer(OpenWire::NotifyEvent & /*Event*/) {
    FILE_LOG(logINFO) << "X6_1000::HandleTimer";
    trigger_.AtTimerTick();
}

void  X6_1000::VMPDataAvailable(VeloMergeParserDataAvailable & Event) {
    FILE_LOG(logINFO) << "X6_1000::VMPDataAvailable";
    // VMP_VeloCount++;

    // if (Settings.Rx.LoggerEnable)
    //     if (FWordCount < WordsToLog)
    //         {
    //         //  Log Data To VMP file - oops, use ceiling?
    //         VMPLogger.LogWithHeader(Event.Data);
    //         }
                
}



void X6_1000::HandleTimestampRolloverAlert(Innovative::AlertSignalEvent & event) {
    LogHandler("HandleTimestampRolloverAlert");
}

void X6_1000::HandleSoftwareAlert(Innovative::AlertSignalEvent & event) {
    LogHandler("HandleSoftwareAlert");
}

void X6_1000::HandleWarningTempAlert(Innovative::AlertSignalEvent & event) {
    LogHandler("HandleWarningTempAlert");
}

void X6_1000::HandleInputFifoOverrunAlert(Innovative::AlertSignalEvent & event) {
    LogHandler("HandleInputFifoOverrunAlert");
}

void X6_1000::HandleInputOverrangeAlert(Innovative::AlertSignalEvent & event) {
    LogHandler("HandleInputOverrangeAlert");
}

void X6_1000::HandleOutputFifoUnderflowAlert(Innovative::AlertSignalEvent & event) {
    LogHandler("HandleOutputFifoUnderflowAlert");
}

void X6_1000::HandleTriggerAlert(Innovative::AlertSignalEvent & event) {
    LogHandler("HandleTriggerAlert");
}

void X6_1000::HandleOutputOverrangeAlert(Innovative::AlertSignalEvent & event) {
    LogHandler("HandleOutputOverrangeAlert");
}


void X6_1000::LogHandler(string handlerName) {
    FILE_LOG(logINFO) << "Alert:" << handlerName;
}

