#include "APS2.h"

APS2::APS2() :  isOpen{false}, channels_(2), samplingRate_{-1} {};

APS2::APS2(string deviceSerial) :  isOpen{false}, deviceSerial_{deviceSerial}, samplingRate_{-1} {
	channels_.reserve(2);
	for(size_t ct=0; ct<2; ct++) channels_.push_back(Channel(ct));
};

APS2::~APS2() = default;

APSEthernet::EthernetError APS2::connect(){
	if (!isOpen) {
		APSEthernet::EthernetError success = APSEthernet::get_instance().connect(deviceSerial_);

		if (success == APSEthernet::SUCCESS) {
			FILE_LOG(logINFO) << "Opened connection to device: " << deviceSerial_;
			isOpen = true;
		}
		// TODO: restore state information from file
		return success;
	}
	return APSEthernet::SUCCESS;
}

APSEthernet::EthernetError APS2::disconnect(){
	if (isOpen){
		APSEthernet::EthernetError success = APSEthernet::get_instance().disconnect(deviceSerial_);
		if (success == APSEthernet::SUCCESS) {
			FILE_LOG(logINFO) << "Closed connection to device: " << deviceSerial_;
			isOpen = false;
		}
		// TODO: save state information to file
		return success;
	}
	return APSEthernet::SUCCESS;
}

int APS2::reset(const APS_RESET_MODE_STAT & resetMode /* default SOFT_RESET_HOST_USER */) {
	
	APSCommand_t command = { .packed=0 };
	
	command.cmd = static_cast<uint32_t>(APS_COMMANDS::RESET);
	command.mode_stat = static_cast<uint32_t>(resetMode);

	write_command(command);
	/*	TODO: re-enable this when the USER nonvolative image contains the UDP interface
	// After being reset the board should send an acknowledge packet with status bytes
	APSEthernetPacket statusPacket = query(command)[0];
	//Copy the data back into the status type 
	APSStatusBank_t statusRegs;
	std::copy(statusPacket.payload.begin(), statusPacket.payload.end(), statusRegs.array);
	FILE_LOG(logDEBUG) << print_status_bank(statusRegs);
	*/
	return APSEthernet::SUCCESS;
}

int APS2::init(const bool & forceReload, const int & bitFileNum){
	 //TODO: bitfiles will be stored in flash so all we need to do here is the DACs

	if (forceReload || !read_PLL_status()) {
		FILE_LOG(logINFO) << "Resetting instrument";
		FILE_LOG(logINFO) << "Found force: " << forceReload << " bitFile version: " << myhex << get_bitfile_version() << " PLL status: " << read_PLL_status();

		// send hard reset to APS2
		// this will reconfigure the DACs, PLL and VCX0 with EPROM settings
		// reset(RECONFIG_USER_EPROM);
		// alternatively, can just reconfigure the PLL and VCX0 from EPROM
		// run_chip_config();

		// sync DAC clock phase with PLL
		int status = test_PLL_sync();
		if (status) {
			FILE_LOG(logERROR) << "DAC PLLs failed to sync";
		}

		// align DAC data clock boundaries
		setup_DACs();

		// clear channel data
		clear_channel_data();

		write_memory_map();
	}
	samplingRate_ = get_sampleRate();

	return 0;
}

int APS2::setup_DACs() {
	//Call the setup function for each DAC
	for(int dac=0; dac < NUM_CHANNELS; dac++){
		setup_DAC(dac);
	}
	return 0;
}

APSStatusBank_t APS2::read_status_registers(){
	//Query with the status request command
	APSCommand_t command = { .packed=0 };
	command.cmd = static_cast<uint32_t>(APS_COMMANDS::STATUS);
	command.r_w = 1;
	command.mode_stat = APS_STATUS_HOST;
	APSEthernetPacket statusPacket = query(command)[0];
	//Copy the data back into the status type 
	APSStatusBank_t statusRegs;
	std::copy(statusPacket.payload.begin(), statusPacket.payload.end(), statusRegs.array);
	FILE_LOG(logDEBUG) << print_status_bank(statusRegs);
	return statusRegs;
}

double APS2::get_uptime(){
	/*
	* Return the board uptime in seconds.
	*/ 
	//Read the status registers
	APSStatusBank_t statusRegs = read_status_registers();
	//Put together the seconds and nanoseconds parts
	double intPart;
	return static_cast<double>(statusRegs.uptimeSeconds) + modf(static_cast<double>(statusRegs.uptimeNanoSeconds)/1e9, &intPart);
}

int APS2::store_image(const string & bitFile, const int & position) { /* see header for position default = 0 */
	FILE_LOG(logDEBUG) << "Opening bitfile: " << bitFile;

	std::ifstream FID (bitFile, std::ios::in|std::ios::binary);
	if (!FID.is_open()){
		FILE_LOG(logERROR) << "Unable to open bitfile: " << bitFile;
		return -1; // TODO return a proper error code
	}

	//Get the file size in bytes
	FID.seekg(0, std::ios::end);
	size_t fileSize = FID.tellg();
	FILE_LOG(logDEBUG1) << "Bitfile is " << fileSize << " bytes";
	FID.seekg(0, std::ios::beg);

	//Copy over the file data to the data vector
	vector<uint32_t> packedData;
	packedData.reserve(fileSize/4);
	FID.read(reinterpret_cast<char *>(packedData.data()), fileSize);

	// make packedData size even to ensure that the payload will be a multiple of 8 bytes
	if (packedData.size() % 2 != 0) {
		packedData.push_back(0xffffffffu);
	}

	//Convert to big endian byte order - basically because it will be byte-swapped again when the packet is serialized
	for (auto & packet : packedData) {
		packet = htonl(packet);
	}

	FILE_LOG(logDEBUG1) << "Bit file is " << packedData.size() << " 32-bit words long";

	uint32_t addr = 0; // todo: make start address depend on position
	auto packets = pack_data(addr, packedData);

	// send in groups of 20
	APSEthernet::get_instance().send(deviceSerial_, packets, 20);
	return 0;
}

int APS2::select_image(const int & bitFileNum) {
	FILE_LOG(logINFO) << "Selecting bitfile number " << bitFileNum;

	uint32_t addr = 0; // todo: make start address depend on bitFileNum

	APSEthernetPacket packet;
	packet.header.command.r_w = 0;
	packet.header.command.cmd =  static_cast<uint32_t>(APS_COMMANDS::FPGACONFIG_CTRL);
	packet.header.command.cnt = 0;
	packet.header.addr = addr;

	return APSEthernet::get_instance().send(deviceSerial_, packet, 0);
}

int APS2::program_FPGA(const string & bitFile) {
	/**
	 * @param bitFile path to a Xilinx bit file
	 * @param expectedVersion - checks whether version register matches this value after programming. -1 = skip the check
	 */
	int success = store_image(bitFile);
	if (success != 0)
		return success;
	return select_image(0);
}

int APS2::get_bitfile_version() {
	// Reads version information from status registers
	APSStatusBank_t statusRegs = read_status_registers();
	uint32_t version = statusRegs.userFirmwareVersion;

	FILE_LOG(logDEBUG) << "Bitfile version for FPGA is " << myhex << version;
	
	return version;
}

int APS2::set_sampleRate(const int & freq){
	if (samplingRate_ != freq){
		//Set PLL frequency for each fpga
		APS2::set_PLL_freq(freq);

		samplingRate_ = freq;

		//Test the sync
		return APS2::test_PLL_sync();
	}
	else{
		return 0;
	}
}

int APS2::get_sampleRate() {
	//Pass through to FPGA code
	FILE_LOG(logDEBUG2) << "get_sampleRate";
	int freq1 = get_PLL_freq();
	return freq1;
}

int APS2::clear_channel_data() {
	FILE_LOG(logINFO) << "Clearing all channel data for APS2 " << deviceSerial_;
	for (auto & ch : channels_) {
		ch.clear_data();
	}
	// clear waveform length registers
	//TODO: fix me!
	// socket_.write_register(FPGA_ADDR_CHA_WF_LENGTH, 0);
	// socket_.write_register(FPGA_ADDR_CHB_WF_LENGTH, 0);
	
	// // clear LL length registers
	// socket_.write_register(FPGA_ADDR_CHA_LL_LENGTH, 0);
	// socket_.write_register(FPGA_ADDR_CHB_LL_LENGTH, 0);
	
	return 0;
}

int APS2::load_sequence_file(const string & seqFile){
	/*
	 * Load a sequence file from an H5 file
	 */
	// TODO: fix me!
	/*
	//First open the file
	try {
		FILE_LOG(logINFO) << "Opening sequence file: " << seqFile;
		H5::H5File H5SeqFile(seqFile, H5F_ACC_RDONLY);

		const vector<string> chanStrs = {"chan_1", "chan_2", "chan_3", "chan_4"};
		//For now assume 4 channel data
		//Reset the channel data
		clear_channel_data();
		//TODO: check the channelDataFor attribute
		for(int chanct=0; chanct<4; chanct++){
			//Load the waveform library first
			string chanStr = chanStrs[chanct];
			vector<short> tmpVec = h5array2vector<short>(&H5SeqFile, chanStr + "/waveformLib", H5::PredType::NATIVE_INT16);
			set_waveform(chanct, tmpVec);

			//Check if there is the linklist data and if it is IQ mode style
			H5::Group chanGroup = H5SeqFile.openGroup(chanStr);
			uint16_t isLinkListData, isIQMode;
			isLinkListData = h5element2element<uint16_t>("isLinkListData", &chanGroup, H5::PredType::NATIVE_UINT16);
			isIQMode = h5element2element<uint16_t>("isIQMode", &chanGroup, H5::PredType::NATIVE_UINT16);
			chanGroup.close();

			//Load the linklist data
			if (isLinkListData){
				if (isIQMode){
					channels_[chanct].LLBank_.IQMode = true;
					channels_[chanct].LLBank_.read_state_from_hdf5(H5SeqFile, chanStrs[chanct]+"/linkListData");
					//If the length is less than can fit on the chip then write it to the device
					if (channels_[chanct].LLBank_.length < MAX_LL_LENGTH){
						write_LL_data_IQ(0, 0, channels_[chanct].LLBank_.length, true );
					}

				}
				else{
					channels_[chanct].LLBank_.read_state_from_hdf5(H5SeqFile, chanStrs[chanct]+"/linkListData");
				}
			}
		}
		//Close the file
		H5SeqFile.close();
		return 0;
	}
	catch (H5::FileIException & e) {
		return -1;
	}
	*/
	return 0;
}

int APS2::set_channel_enabled(const int & dac, const bool & enable){
	return channels_[dac].set_enabled(enable);
}

bool APS2::get_channel_enabled(const int & dac) const{
	return channels_[dac].get_enabled();
}

int APS2::set_channel_offset(const int & dac, const float & offset){
	//Update the waveform in driver
	channels_[dac].set_offset(offset);
	//Write to device if necessary
	if (!channels_[dac].waveform_.empty()){
		write_waveform(dac, channels_[dac].prep_waveform());
	}

	//Update TAZ register
	set_offset_register(dac, channels_[dac].get_offset());

	return 0;
}

float APS2::get_channel_offset(const int & dac) const{
	return channels_[dac].get_offset();
}

int APS2::set_channel_scale(const int & dac, const float & scale){
	channels_[dac].set_scale(scale);
	if (!channels_[dac].waveform_.empty()){
		write_waveform(dac, channels_[dac].prep_waveform());
	}
	return 0;
}

float APS2::get_channel_scale(const int & dac) const{
	return channels_[dac].get_scale();
}

int APS2::set_trigger_source(const TRIGGERSOURCE & triggerSource){

	int returnVal=0;
	//TODO: implement with new memory map
	/*
	switch (triggerSource){
	case INTERNAL:
		returnVal = FPGA::clear_bit(socket_, FPGA_ADDR_CSR, CSRMSK_CHA_TRIGSRC);
		break;
	case EXTERNAL:
		returnVal = FPGA::set_bit(socket_, FPGA_ADDR_CSR, CSRMSK_CHA_TRIGSRC);
		break;
	default:
		returnVal = -1;
		break;
	}
	*/
	return returnVal;
}

TRIGGERSOURCE APS2::get_trigger_source() {
//	uint32_t regVal;
	//TODO: fix me!
	// socket_.read_register(FPGA_ADDR_CSR, regVal);
//	return TRIGGERSOURCE((regVal & CSRMSK_CHA_TRIGSRC) == CSRMSK_CHA_TRIGSRC ? 1 : 0);
	return TRIGGERSOURCE(0);
}

int APS2::set_trigger_interval(const double & interval){

	//SM clock is 1/4 of samplingRate so the trigger interval in SM clock periods is
	//note: clockCycles is zero-indexed and has a dead state (so subtract 2)
	int clockCycles = interval*0.25*samplingRate_*1e6 - 2;

	FILE_LOG(logDEBUG) << "Setting trigger interval to " << interval << "s (" << clockCycles << " cycles)";

	//Trigger interval is 32bits wide so have to split up into two 16bit words
	// uint16_t upperWord = clockCycles >> 16;
	// uint16_t lowerWord = 0xFFFF  & clockCycles;
	//TODO: fix me!
	// return write(FPGA_ADDR_TRIG_INTERVAL, {upperWord, lowerWord}, false);
	return 0;
}

double APS2::get_trigger_interval() {

	//Trigger interval is 32bits wide so have to split up into two 16bit words reads
	uint32_t upperWord = 0, lowerWord = 0;
	//TODO: fix me!
	// socket_.read_register(FPGA_ADDR_TRIG_INTERVAL,upperWord );
	// socket_.read_register(FPGA_ADDR_TRIG_INTERVAL+1, lowerWord);
	
	//Put it back together and covert from clock cycles to time (note: trigger interval is zero indexed and has a dead state)
	return static_cast<double>((upperWord << 16) + lowerWord + 2)/(0.25*samplingRate_*1e6);
}


int APS2::run() {
	FILE_LOG(logDEBUG1) << "Releasing pulse sequencer state machine...";
	write_memory(SEQ_CONTROL_ADDR, 1);
	return 0;
}

int APS2::stop() {

	// stop all channels

	//Try to stop in a wait for trigger state by making the trigger interval long
	auto curTriggerInt = get_trigger_interval();
	auto curTriggerSource = get_trigger_source();
	set_trigger_interval(1);
	set_trigger_source(INTERNAL);
	usleep(1000);

	//Put the state machine back in reset
	write_memory(SEQ_CONTROL_ADDR, 0);

	// restore trigger state
	set_trigger_interval(curTriggerInt);
	set_trigger_source(curTriggerSource);

	return 0;

}

int APS2::set_run_mode(const RUN_MODE & mode) {
/********************************************************************
 * Description : Sets run mode
 *
 * Inputs :     mode - 1 = LL mode 0 = waveform mode
 *
 * Returns : 0 on success < 0 on failure
 *
********************************************************************/
 	// TODO: fix me!
	// int dacModeMask = CSRMSK_CHA_OUTMODE;
	
	//Set the run mode bit
	FILE_LOG(logINFO) << "Setting Run Mode ==> " << mode;
//	if (mode) {
//	  FPGA::set_bit(socket_, FPGA_ADDR_CSR, dacModeMask);
//	} else {
//	  FPGA::clear_bit(socket_, FPGA_ADDR_CSR, dacModeMask);
//	}

	return 0;
}

// FPGA memory read/write
int APS2::write_memory(const uint32_t & addr, const uint32_t & data){
	//Create the vector and pass through
	return write_memory(addr, vector<uint32_t>({data}));
}

int APS2::write_memory(const uint32_t & addr, const vector<uint32_t> & data){
	/* APS2::write
	 * addr = start byte of address space 
	 * data = vector<uint32_t> data
	 */

	//Pack the data into APSEthernetFrames
	vector<APSEthernetPacket> dataPackets = pack_data(addr, data);

	//Send the packets out 
	APSEthernet::get_instance().send(deviceSerial_, dataPackets);

	//TOOD: - check for acknowledge in correct order
	auto ackPackets = read_packets(dataPackets.size());
	FILE_LOG(logDEBUG3) << "Got back " << ackPackets.size() << " ACK packets";

	return 0;
}

vector<uint32_t> APS2::read_memory(const uint32_t & addr, const uint32_t & numWords){
	//TODO: handle numWords that require mulitple packets

	//Send the read request
	APSEthernetPacket readReq;
	readReq.header.command.r_w = 1;
	readReq.header.command.cmd =  static_cast<uint32_t>(APS_COMMANDS::USERIO_ACK);
	readReq.header.command.cnt = numWords;
	readReq.header.addr = addr;
	APSEthernet::get_instance().send(deviceSerial_, readReq);

	//Retrieve the data packet(s)
	auto readData = read_packets(1);

	return readData[0].payload;
}

//SPI read/write
int APS2::write_SPI(vector<uint32_t> & msg) {
	// push on "end of message"
	APSChipConfigCommand_t cmd = {.packed=0};
	cmd.target = CHIPCONFIG_IO_TARGET_EOL;
	msg.push_back(cmd.packed);

	// build packet
	APSEthernetPacket packet;
	packet.header.command.r_w = 0;
	packet.header.command.cmd =  static_cast<uint32_t>(APS_COMMANDS::CHIPCONFIGIO);
	packet.header.command.cnt = msg.size();
	packet.payload = msg;

	APSEthernetPacket p = query(packet)[0];
	// TODO: check ACK packet status
	return 0;
}

uint32_t APS2::read_SPI(const CHIPCONFIG_IO_TARGET & target, const uint16_t & addr) {
	// reads a single 32-bit word from the target SPI device

	// build message
	APSChipConfigCommand_t cmd;
	DACCommand_t dacinstr = {.packed = 0};
	PLLCommand_t pllinstr = {.packed = 0};
	// config target and instruction
	switch (target) {
		case CHIPCONFIG_TARGET_DAC_0:
			cmd.target = CHIPCONFIG_IO_TARGET_DAC_0;			
			dacinstr.addr = addr;
			dacinstr.N = 0; // single-byte read
			dacinstr.r_w = 1; // read
			cmd.instr = dacinstr.packed;
			break;
		case CHIPCONFIG_TARGET_DAC_1:
			cmd.target = CHIPCONFIG_IO_TARGET_DAC_1;
			dacinstr.addr = addr;
			dacinstr.N = 0; // single-byte read
			dacinstr.r_w = 1; // read
			cmd.instr = dacinstr.packed;
			break;
		case CHIPCONFIG_TARGET_PLL:
			cmd.target = CHIPCONFIG_IO_TARGET_PLL;
			pllinstr.addr = addr;
			pllinstr.W = 0; // single-byte read
			pllinstr.r_w = 1; // read
			cmd.instr = pllinstr.packed;
			break;
		default:
			FILE_LOG(logERROR) << "Invalid read_SPI target " << myhex << target;
			return 0;
	}
	cmd.spicnt_data = 1; // request 1 byte
	vector<uint32_t> msg = {cmd.packed};
	// interface logic requires at least 3 bytes of data to return anything, so push on the same instruction twice more
	msg.push_back(cmd.packed);
	msg.push_back(cmd.packed);
	msg.push_back(cmd.packed);

	// write the SPI read instruction
	write_SPI(msg);

	// build read packet
	APSEthernetPacket packet;
	packet.header.command.r_w = 1;
	packet.header.command.cmd = static_cast<uint32_t>(APS_COMMANDS::CHIPCONFIGIO);
	packet.header.command.cnt = 1; // single word read

	APSEthernetPacket response = query(packet)[0];
	// TODO: Check status bits
	if (response.payload.size() == 0) {
		return 0;
	} else {
		FILE_LOG(logDEBUG4) << "read_SPI response payload = " << hexn<8> << response.payload[0] << endl;
		return response.payload[0] >> 24; // first response is in MSB of 32-bit word
	}
}

//Flash read/write
int APS2::write_flash(const uint32_t & addr, vector<uint32_t> & data) {
	// erase before write
	erase_flash(addr, sizeof(uint32_t) * data.size());

	vector<APSEthernetPacket> packets;
	APSEthernetPacket packet;
	packet.header.command.r_w = 0;
	packet.header.command.cmd = static_cast<uint32_t>(APS_COMMANDS::EPROMIO);
	packet.header.command.mode_stat = EPROM_RW;
	packet.header.command.cnt = 64;
	packet.payload.resize(64);

	// resize data to a multiple of 64 words (256 bytes)
	int padwords = (64 - (data.size() % 64)) % 64;
	FILE_LOG(logDEBUG3) << "Flash write: padding payload with " << padwords << " words";
	data.resize(data.size() + padwords);

	for (size_t ct = 0; ct < data.size(); ct += 64) {
		std::copy(data.begin() + ct, data.begin() + ct + 64, packet.payload.begin());
		packet.header.addr = addr + ct*64;
		packets.push_back(packet);
	}
	FILE_LOG(logDEBUG2) << "Writing " << packets.size() << " packets of data to flash address " << myhex << addr;
	try {
		APSEthernet::get_instance().send(deviceSerial_, packets);
		APSEthernetPacket p = read_packets(packets.size())[0];
		return p.header.command.mode_stat;	
	} catch (std::exception &e) {
		FILE_LOG(logERROR) << "Flash write failed!";
		return -1;
	}

	// TODO: optionally verify the write

}

int APS2::erase_flash(uint32_t addr, uint32_t numBytes) {
	// each erase command erases 64 KB of data starting at addr
	FILE_LOG(logINFO) << "Erasing " << numBytes << " bytes starting at " << myhex << addr;
	//TODO: check 64KB alignment 
	if ((addr % 65536) != 0){
		FILE_LOG(logERROR) << "Flash memory erase command was not 64KB aligned!";
		return -1;
	}

	APSCommand_t command = { .packed=0 };
	command.r_w = 0;
	command.cmd = static_cast<uint32_t>(APS_COMMANDS::EPROMIO);
	command.mode_stat = EPROM_ERASE;

	uint32_t erasedBytes = 0;

	while(erasedBytes < numBytes) {
		FILE_LOG(logDEBUG2) << "Erasing a 64 KB page at addr: " << myhex << addr;
		write_command(command, addr);
		APSEthernetPacket p = read_packets(1)[0];
		if (p.header.command.mode_stat == EPROM_OPERATION_FAILED){
			FILE_LOG(logERROR) << "Flash memory erase command failed!";
		}
		erasedBytes += 65536;
		addr += 65536;
	}
	return 0;
}

vector<uint32_t> APS2::read_flash(const uint32_t & addr, const uint32_t & numWords) {
	//TODO: handle reads that require multiple packets
	APSCommand_t command = { .packed=0 };
	command.r_w = 1;
	command.cmd = static_cast<uint32_t>(APS_COMMANDS::EPROMIO);
	command.mode_stat = EPROM_RW;
	command.cnt = std::min(numWords, static_cast<const uint32_t>(365));

	vector<uint32_t> data;
	// TODO: loop sending write and read commands, until received at least numWords
	APSEthernetPacket p = query(command, addr)[0];
	// TODO: Check status bits
	data.insert(data.end(), p.payload.begin(), p.payload.end());

	return data;
}

//Create/restore setup SPI sequence
int APS2::write_SPI_setup() {
	FILE_LOG(logINFO) << "Writing SPI startup sequence";
	vector<uint32_t> msg = build_VCXO_SPI_msg(VCXO_INIT);
	vector<uint32_t> pll_msg = build_PLL_SPI_msg(PLL_INIT);
	msg.insert(msg.end(), pll_msg.begin(), pll_msg.end());
	// push on "end of message"
	APSChipConfigCommand_t cmd = {.packed=0};
	cmd.target = CHIPCONFIG_IO_TARGET_EOL;
	msg.push_back(cmd.packed);
	return write_flash(0x0, msg);
}


/*
 *
 * Private Functions
 */

int APS2::write_command(const APSCommand_t & command, const uint32_t & addr /* see header for default value = 0 */){
	/*
	* Write a single command 
	*/
	//TODO: figure out move constructor
	APSEthernetPacket packet(command, addr);
	APSEthernet::get_instance().send(deviceSerial_, packet);
	return 0;
}

vector<APSEthernetPacket> APS2::pack_data(const uint32_t & addr, const vector<uint32_t> & data){
	//Break the data up into ethernet frame sized chunks.   
	// ethernet frame payload = 1500bytes - 20bytes IPV4 and 8 bytes UDP and 24 bytes APS header (with address field) = 1448bytes = 362 words
	// for unknown reasons, we see occasional failures when using packets that large. 256 seems to be more stable.
	static const int maxPayload = 256;

	vector<APSEthernetPacket> packets;

	APSEthernetPacket newPacket;
	newPacket.header.command.cmd =  static_cast<uint32_t>(APS_COMMANDS::USERIO_ACK);
	
	auto idx = data.begin();
	uint16_t seqNum = 0;
	uint32_t curAddr = addr;
	while (idx != data.end()){
		if (std::distance(idx, data.end()) > maxPayload){
			newPacket.header.command.cnt = maxPayload;
		}
		else{
			newPacket.header.command.cnt = std::distance(idx, data.end());
		}
		
		newPacket.header.seqNum = seqNum++;
		newPacket.header.addr = curAddr; 
		curAddr += 4*newPacket.header.command.cnt;
		
		newPacket.payload.clear();
		std::copy(idx, idx+newPacket.header.command.cnt, std::back_inserter(newPacket.payload));

		packets.push_back(newPacket);
		idx += newPacket.header.command.cnt;
	}

	return packets;
}


vector<APSEthernetPacket> APS2::read_packets(const size_t & numPackets) {
	return APSEthernet::get_instance().receive(deviceSerial_, numPackets);
}

vector<APSEthernetPacket> APS2::query(const APSCommand_t & command, const uint32_t & addr /* see header for default value = 0 */) {
	//write-read ping-pong
	write_command(command, addr);
	return read_packets(1);
}

vector<APSEthernetPacket> APS2::query(const APSEthernetPacket & pkt) {
	//write-read ping-pong
	APSEthernet::get_instance().send(deviceSerial_, pkt);
	return read_packets(1);
}

vector<uint32_t> APS2::build_DAC_SPI_msg(const CHIPCONFIG_IO_TARGET & target, const vector<SPI_AddrData_t> & addrData) {
	vector<uint32_t> msg;
	APSChipConfigCommand_t cmd;
	// force SINGLE writes for now
	switch (target) {
		case CHIPCONFIG_TARGET_DAC_0:
			cmd.target = CHIPCONFIG_IO_TARGET_DAC_0_SINGLE;
			break;
		case CHIPCONFIG_TARGET_DAC_1:
			cmd.target = CHIPCONFIG_IO_TARGET_DAC_1_SINGLE;
			break;
		default:
			FILE_LOG(logERROR) << "Unexpected CHIPCONFIG_IO_TARGET";
			throw runtime_error("Unexpected CHIPCONFIG_IO_TARGET");
	}
	for (auto ad : addrData) {
		cmd.instr = ad.first;
		cmd.spicnt_data = ad.second;
		msg.push_back(cmd.packed);
	}
	return msg;
}

vector<uint32_t> APS2::build_PLL_SPI_msg(const vector<SPI_AddrData_t> & addrData) {
	vector<uint32_t> msg;
	APSChipConfigCommand_t cmd;
	// force SINGLE writes for now
	cmd.target = CHIPCONFIG_IO_TARGET_PLL_SINGLE;
	for (auto ad : addrData) {
		cmd.instr = ad.first;
		cmd.spicnt_data = ad.second;
		msg.push_back(cmd.packed);
	}
	return msg;
}
vector<uint32_t> APS2::build_VCXO_SPI_msg(const vector<uint8_t> & data) {
	vector<uint32_t> msg;
	APSChipConfigCommand_t cmd;
	cmd.target = CHIPCONFIG_IO_TARGET_VCXO;
	cmd.spicnt_data = 0;

	if (data.size() % 4 != 0) {
		FILE_LOG(logERROR) << "VCXO messages must be 4-byte aligned";
		throw runtime_error("VCXO messages must be 4-byte aligned");
	}

	// pack 4 bytes into 1 32-bit word
	for (size_t ct = 0; ct < data.size(); ct += 4) {
		// alternate commands with data
		msg.push_back(cmd.packed);
		msg.push_back( (data[ct] << 24) | (data[ct+1] << 16) | (data[ct+2] << 8) | data[ct+3] );
	}
	return msg;
}

int APS2::setup_PLL() {
	// set the on-board PLL to its default state (two 1.2 GHz outputs to DAC's, 300 MHz sys_clk to FPGA, and 400 MHz mem_clk to FPGA)
	FILE_LOG(logINFO) << "Running base-line setup of PLL";

	// Disable DDRs
	// int ddrMask = CSRMSK_CHA_DDR | CSRMSK_CHB_DDR;
//	FPGA::clear_bit(socket_, FPGA_ADDR_CSR, ddrMask);
	// disable dac FIFOs
	// for (int dac = 0; dac < NUM_CHANNELS; dac++)
		// disable_DAC_FIFO(dac);

	vector<uint32_t> msg = build_PLL_SPI_msg(PLL_INIT);
	write_SPI(msg);

	// enable the oscillator
//	if (APS2::reset_status_ctrl() != 1)
//		return -1;

	// Enable DDRs
//	FPGA::set_bit(socket_, FPGA_ADDR_CSR, ddrMask);

	//Record that sampling rate has been set to 1200
	samplingRate_ = 1200;

	return 0;
}



int APS2::set_PLL_freq(const int & freq) {
	/* APS2::set_PLL_freq
	 * fpga = FPGA1, FPGA2, or ALL_FPGAS
	 * freq = frequency to set in MHz, allowed values are (1200, 600, 300, 200, 100, 50, and 40)
	 */

	uint32_t pllCyclesAddr, pllBypassAddr;
	uint8_t pllCyclesVal, pllBypassVal;

	FILE_LOG(logDEBUG) << "Setting PLL FPGA: Freq.: " << freq;

	pllCyclesAddr = 0x190;
	pllBypassAddr = 0x191;

	switch(freq) {
//		case 40: pllCyclesVal = 0xEE; break; // 15 high / 15 low (divide by 30)
//		case 50: pllCyclesVal = 0xBB; break;// 12 high / 12 low (divide by 24)
//		case 100: pllCyclesVal = 0x55; break; // 6 high / 6 low (divide by 12)
		case 200: pllCyclesVal = 0x22; break; // 3 high / 3 low (divide by 6)
		case 300: pllCyclesVal = 0x11; break; // 2 high /2 low (divide by 4)
		case 600: pllCyclesVal = 0x00; break; // 1 high / 1 low (divide by 2)
		case 1200: pllCyclesVal = 0x00; break; // value ignored, set bypass below
		default:
			return -2;
	}

	// bypass divider if freq == 1200
	pllBypassVal = (freq==1200) ?  0x80 : 0x00;
	FILE_LOG(logDEBUG2) << "Setting PLL cycles addr: " << myhex << pllCyclesAddr << " val: " << int(pllCyclesVal);
	FILE_LOG(logDEBUG2) << "Setting PLL bypass addr: " << myhex << pllBypassAddr << " val: " << int(pllBypassVal);

	// Disable DDRs
	// int ddr_mask = CSRMSK_CHA_DDR | CSRMSK_CHB_DDR;
	//TODO: fix!
//	FPGA::clear_bit(socket_, FPGA_ADDR_CSR, ddr_mask);
	// disable DAC FIFOs
	// for (int dac = 0; dac < NUM_CHANNELS; dac++)
		// disable_DAC_FIFO(dac);

	// Disable oscillator by clearing APS2_STATUS_CTRL register
	//TODO: fix!
//	if (APS2::clear_status_ctrl() != 1) return -4;

	//Setup of a vector of address-data pairs for all the writes we need for the PLL routine
	const vector<SPI_AddrData_t> PLL_Routine = {
		{pllCyclesAddr, pllCyclesVal},
		{pllBypassAddr, pllBypassVal},
		{0x18, 0x71}, // Initiate Calibration.  Must be followed by Update Registers Command
		{0x232, 0x1}, // Set bit 0 to 1 to simultaneously update all registers with pending writes.
		{0x18, 0x70}, // Clear calibration flag so that next set generates 0 to 1.
		{0x232, 0x1} // Set bit 0 to 1 to simultaneously update all registers with pending writes.
	};

	vector<uint32_t> msg = build_PLL_SPI_msg(PLL_Routine);
	write_SPI(msg);

	// Enable Oscillator
	//TODO: fix!
//	if (APS2::reset_status_ctrl() != 1) return -4;

	// Enable DDRs
	//TODO: fix!
//	FPGA::set_bit(socket_, FPGA_ADDR_CSR, ddr_mask);
	// Enable DAC FIFOs
	// for (int dac = 0; dac < NUM_CHANNELS; dac++)
	// 	enable_DAC_FIFO(dac);

	return 0;
}



int APS2::test_PLL_sync(const int & numRetries /* see header for default */) {
	/*
		APS2_TestPllSync synchronized the phases of the DAC clocks with the following procedure:
		1) Make sure all PLLs have locked.
		2) Test for sync of 600 MHz clocks from DACs. They must be in sync with each other
	    and in sync with the 300 MHz reference. This has the test signature of the 600 MHz
	    XOR being always low and both 300 MHz XORs are low or high.
			- If either the 600 MHz XOR is high or a 300 MHz XOR is in the middle, disable
			 and re-enable the PLL output to one of the DACs connected to the FPGA. Reset
			 the FPGA PLLs, wait for lock, then loop.
		3) Test channel 0/2 PLL against reference PLL. Reset until in phase.
		4) Test channel 1/3 PLL against reference PLL. Reset until in phase.
		5) Verify that sync worked by testing 0/2 XOR 1/3 (global phase).
	 *
	 * Inputs: device
	 *         fpga (1 or 2)
	 *         numRetries - number of times to restart the test if the global sync test fails (step 5)
	 */

/*
	// Test for DAC clock phase match
	bool inSync, globalSync;
	int xorFlagCnts, a_phase, b_phase;
	int dac02Reset, dac13Reset;

	int pllBit;
	// unsigned int pllEnableAddr, pllEnableAddr2;
	// uint8_t writeByte;

	const vector<int> PLL_XOR_TEST = {PLL_02_XOR_BIT, PLL_13_XOR_BIT,PLL_GLOBAL_XOR_BIT};
	const vector<int> CH_PHASE_TESTS = {FPGA_ADDR_A_PHASE, FPGA_ADDR_B_PHASE};
	const vector<int> PLL_LOCK_TEST = {PLL_02_LOCK_BIT, PLL_13_LOCK_BIT, REFERENCE_PLL_LOCK_BIT};
	const vector<int> PLL_RESET = {CSRMSK_CHA_PLLRST, CSRMSK_CHB_PLLRST, 0};

	unsigned int pllResetBit  = CSRMSK_CHA_PLLRST | CSRMSK_CHB_PLLRST;

	FILE_LOG(logINFO) << "Running channel sync on FPGA ";

	//TODO: fix!
	// pllEnableAddr = 0;
	// pllEnableAddr2 = 0;
	
	// Disable DDRs
	// int ddr_mask = CSRMSK_CHA_DDR | CSRMSK_CHB_DDR;
	//TODO: fix!
//	FPGA::clear_bit(socket_, FPGA_ADDR_CSR, ddr_mask);
	// disable DAC FIFOs
	// for (int dac = 0; dac < NUM_CHANNELS; dac++)
		// disable_DAC_FIFO(dac);

	//A little helper function to wait for the PLL's to lock and reset if necessary
	auto wait_PLL_relock = [this, &pllResetBit](bool resetPLL, const int & regAddress, const vector<int> & pllBits) -> bool {
		FILE_LOG(logDEBUG2) << "wait_PLL_relock";

		bool inSync = false;
		int testct = 0;
		while (!inSync && (testct < 20)){
			FILE_LOG(logDEBUG2) << "Reading PLL status for pllBits with size " << pllBits.size() << " and first bit is " << pllBits[0];
//			inSync = (APS2::read_PLL_status(regAddress, pllBits) == 1);
			//If we aren't locked then reset for the next try by clearing the PLL reset bits
			if (resetPLL) {
//				FPGA::clear_bit(socket_, FPGA_ADDR_CSR, pllResetBit);
			}
			//Otherwise just wait
			else{
				usleep(1000);
			}
			testct++;
		}
		return inSync;
	};

	// Step 1: test for the PLL's being locked to the reference
	inSync = wait_PLL_relock(true, FPGA_ADDR_PLL_STATUS, PLL_LOCK_TEST);
	if (!inSync) {
		FILE_LOG(logERROR) << "Reference PLL failed to lock";
		return -5;
	}

	inSync = false; globalSync = false;

	//Step 2:
	// start by testing for a 600 MHz XOR always low

	//First a little helper function to update the PLL registers
	auto update_PLL_register = [this] (){
		// uint32_t address = 0x232;
		// uint8_t data = 0x1;
	//TODO: fix me!
		// socket_.write_SPI(CHIPCONFIG_TARGET_PLL, address, data);
	};

	auto read_DLL_phase = [this] (int addr) {
		// The phase register holds a 9-bit value [0, 511] representing the phase shift.
		// We convert his value to phase in degrees in the range (-180, 180]
		uint32_t regData;
	//TODO: fix me!
		// socket_.read_register(addr, regData);
		double phase = regData;
		if (phase > 256) {
			phase -= 512;
		}
		phase *= 180.0/256.0;
		return phase;
	};

	FILE_LOG(logINFO) << "Testing for DAC clock phase sync";
	//Loop over number of tries
	static const int xorCounts = 20, lowCutoff = 5, lowPhaseCutoff = 45, highPhaseCutoff = 135;
	for (int ct = 0; ct < MAX_PHASE_TEST_CNT; ct++) {
		//Reset the counts
		xorFlagCnts = 0;
		dac02Reset = 0;
		dac13Reset = 0;

		//Take twenty counts of the the xor data
		for(int xorct = 0; xorct < xorCounts; xorct++) {
			uint32_t regData;
	//TODO: fix me!
			// socket_.read_register(FPGA_ADDR_PLL_STATUS, regData);
			pllBit = regData;
			xorFlagCnts += (pllBit >> PLL_GLOBAL_XOR_BIT) & 0x1;
		}

		// read DACA and DACB phases
		a_phase = read_DLL_phase(FPGA_ADDR_A_PHASE);
		b_phase = read_DLL_phase(FPGA_ADDR_B_PHASE);

		FILE_LOG(logDEBUG1) << "DAC A Phase: " << a_phase << ", DAC B Phase: " << b_phase;

		// due to clock skews, need to accept a range of counts as "0" and "1"
		if ( (xorFlagCnts <= lowCutoff ) &&
				(abs(a_phase) < lowPhaseCutoff || abs(a_phase) > highPhaseCutoff) &&
				(abs(b_phase) < lowPhaseCutoff || abs(b_phase) > highPhaseCutoff) ) {
			// 300 MHz clocks on FPGA are either 0 or 180 degrees out of phase and 600 MHz clocks
			// are in phase. Move on.
			FILE_LOG(logDEBUG1) << "DAC clocks in phase with reference, XOR counts : " << xorFlagCnts;
			//Get out of MAX_PHAST_TEST ct loop
			break;
		}
		// TODO: check that we are dealing with the case of in-phase 600 MHz clocks with BOTH 300 MHz clocks 180 out of phase with the reference
		else {
			// 600 MHz clocks out of phase, reset DAC clocks that are 90/270 degrees out of phase with reference
			FILE_LOG(logDEBUG1) << "DAC clocks out of phase; resetting, XOR init: " << xorFlagCnts;
			writeByte = 0x2; //disable clock outputs
			//If ChA is +/-90 degrees out of phase then reset it
			if (abs(a_phase) >= lowPhaseCutoff && abs(a_phase) <= highPhaseCutoff) {
				dac02Reset = 1;
				//TODO: fix me!
				// socket_.write_SPI(CHIPCONFIG_TARGET_PLL, pllEnableAddr, writeByte );
			}
			//If ChB is +/-90 degrees out of phase then reset it
			if (abs(b_phase) >= lowPhaseCutoff && abs(b_phase) <= highPhaseCutoff) {
				dac13Reset = 1;
		//TODO: fix me!
				// socket_.write_SPI(CHIPCONFIG_TARGET_PLL, pllEnableAddr2, writeByte);
			}
			//Actually update things
			update_PLL_register();
			writeByte = 0x0; // enable clock outputs
			if (dac02Reset)
				//TODO: fix me!
				// socket_.write_SPI(CHIPCONFIG_TARGET_PLL, pllEnableAddr, writeByte );
			if (dac13Reset)
				//TODO: fix me!
				// socket_.write_SPI(CHIPCONFIG_TARGET_PLL, pllEnableAddr2, writeByte);
			update_PLL_register();

			// reset FPGA PLLs
			//TODO: fix!
//			FPGA::set_bit(socket_, FPGA_ADDR_CSR, pllResetBit);
//			FPGA::clear_bit(socket_, FPGA_ADDR_CSR, pllResetBit);

			// wait for the PLL to relock
			inSync = wait_PLL_relock(false, FPGA_ADDR_PLL_STATUS, PLL_LOCK_TEST);
			if (!inSync) {
				FILE_LOG(logERROR) << "PLLs did not re-sync after reset";
				return -7;
			}
		}
	}

	//Steps 3,4,5
	const vector<string> chStrs = {"A", "B"};
	for (int ch = 0; ch < 2; ch++) {

		FILE_LOG(logDEBUG) << "Testing channel " << chStrs[ch];
		for (int ct = 0; ct < MAX_PHASE_TEST_CNT; ct++) {

			a_phase = read_DLL_phase(CH_PHASE_TESTS[ch]);

			// here we are looking for in-phase clock
			if (abs(a_phase) < lowPhaseCutoff) {
				globalSync = true;
				break; // passed, move on to next channel
			}
			else {
				// PLLs out of sync, reset
				FILE_LOG(logDEBUG1) << "Channel " << chStrs[ch] << " PLL not in sync.. resetting (phase " << a_phase << " )";
				globalSync = false;

				// reset a single channel PLL
				//TODO: fix!
//				FPGA::set_bit(socket_, FPGA_ADDR_CSR, PLL_RESET[ch]);
//				FPGA::clear_bit(socket_, FPGA_ADDR_CSR, PLL_RESET[ch]);

				// wait for lock
				FILE_LOG(logDEBUG2) << "Waiting for relock of PLL " << ch << " by looking at bit " << PLL_LOCK_TEST[ch];
				inSync = wait_PLL_relock(false, FPGA_ADDR_PLL_STATUS, {PLL_LOCK_TEST[ch]});
				if (!inSync) {
					FILE_LOG(logERROR) << "PLL " << chStrs[ch] << " did not re-sync after reset";
					return -10;
				}
			}
		}
	}

	if (!globalSync) { // failed to sync both channels
		if (numRetries > 0) {
			FILE_LOG(logDEBUG) << "Sync failed; retrying.";
			// restart both DAC clocks and try again
			//TODO: fix me!
			// socket_.write_SPI(CHIPCONFIG_TARGET_PLL, pllEnableAddr, 0x2);  // MAGIC NUMBER
			// socket_.write_SPI(CHIPCONFIG_TARGET_PLL, pllEnableAddr2, 0x2); // MAGIC NUMBER
			// update_PLL_register();
			// writeByte = 0x0;
			// socket_.write_SPI(CHIPCONFIG_TARGET_PLL, pllEnableAddr, 0x0);  // MAGIC NUMBER
			// socket_.write_SPI(CHIPCONFIG_TARGET_PLL, pllEnableAddr2, 0x0); // MAGIC NUMBER
			update_PLL_register();

			//TODO: fix!
//			FPGA::set_bit(socket_, FPGA_ADDR_CSR, pllResetBit);
//			FPGA::clear_bit(socket_, FPGA_ADDR_CSR, pllResetBit);

			//Try again by recursively calling the same function
			return test_PLL_sync(numRetries - 1);
		} else {
			// we failed, but enable DDRs to get a usable state
//			FPGA::set_bit(socket_, FPGA_ADDR_CSR, ddr_mask);
			// enable DAC FIFOs
			//for (int dac = 0; dac < NUM_CHANNELS; dac++)
				//enable_DAC_FIFO(dac);

			FILE_LOG(logERROR) << "Error could not sync PLLs";
			return -9;
		}
	}



	// Enable DDRs
	//TODO: fix!
//	FPGA::set_bit(socket_, FPGA_ADDR_CSR, ddr_mask);
	// enable DAC FIFOs
	//for (int dac = 0; dac < NUM_CHANNELS; dac++)
		//enable_DAC_FIFO(dac);

	FILE_LOG(logINFO) << "Sync test complete";
	*/
	return 0;
}


int APS2::read_PLL_status(const int & regAddr /*check header for default*/, const vector<int> & pllLockBits  /*check header for default*/ ){
	/*
	 * Helper function to read the status of some PLL bit and whether the main PLL is locked.
	 * fpga = FPGA1, FPGA2, or ALL_FPGAS
	 * regAddr = register to poll for PLL sync status (0x8006 or 0xF006)
	 * PllLockBits = vector of register bit locations to query for lock state
	 */
	
 FILE_LOG(logDEBUG2) << "APS2::read_PLL_status";

	int pllStatus = 1;

//	pll_bit = FPGA::read_FPGA(socket_, FPGA_ADDR_SYNC_REGREAD | FPGA_OFF_VERSION, fpga); // latched to USB clock (has version 0x020)
//	pll_bit = FPGA::read_FPGA(socket_, FPGA_ADDR_REGREAD | FPGA_OFF_VERSION, fpga); // latched to 200 MHz PLL (has version 0x010)

	uint32_t pllRegister = 0;
	//TODO: fix me!
	// socket_.read_register(regAddr, pllRegister);

	//Check each of the clocks in series
	for(int tmpBit : pllLockBits){
		pllStatus &= ((pllRegister >> tmpBit) & 0x1);
		FILE_LOG(logDEBUG2) << "FPGA PLL status: " << ((pllRegister >> tmpBit) & 0x1) << " (bit " << tmpBit << " of " << myhex << pllRegister << " )";
	}
	FILE_LOG(logDEBUG1) << "APS2::read_PLL_status = " << pllStatus;
	return pllStatus;
}

int APS2::get_PLL_freq() {
	// Poll APS2 PLL chip to determine current frequency

	int freq = 0;
	uint16_t pll_cycles_addr = 0x190;
	uint16_t pll_bypass_addr = 0x191;

	FILE_LOG(logDEBUG3) << "get_PLL_freq";

	uint32_t pll_cycles_val = read_SPI(CHIPCONFIG_TARGET_PLL, pll_cycles_addr);
	uint32_t pll_bypass_val = read_SPI(CHIPCONFIG_TARGET_PLL, pll_bypass_addr);

	FILE_LOG(logDEBUG3) << "pll_cycles_val = " << hexn<2> << pll_cycles_val;
	FILE_LOG(logDEBUG3) << "pll_bypass_val = " << hexn<2> << pll_bypass_val;

	// select frequency based on pll cycles setting
	// the values here should match the reverse lookup in FGPA::set_PLL_freq

	if ((pll_bypass_val & 0x80) == 0x80 && pll_cycles_val == 0x00)
		freq =  1200;
	else {
		switch(pll_cycles_val) {
			case 0xEE: freq = 40;  break;
			case 0xBB: freq = 50;  break;
			case 0x55: freq = 100; break;
			case 0x22: freq = 200; break;
			case 0x11: freq = 300; break;
			case 0x00: freq = 600; break;
			default:
				return -2;
		}
	}

	FILE_LOG(logDEBUG2) << "PLL frequency for FPGA:  Freq: " << freq;

	return freq;
}


int APS2::setup_VCXO() {
	// Write the standard VCXO setup

	FILE_LOG(logINFO) << "Setting up VCX0";

	// ensure the oscillator is disabled before programming
	//TODO: fix!
//	if (APS2::clear_status_ctrl() != 1)
//		return -1;

	vector<uint32_t> msg = build_VCXO_SPI_msg(VCXO_INIT);
	return write_SPI(msg);
}

int APS2::setup_DAC(const int & dac) 
/*
 * Description: Aligns the data valid window of the DAC with the output of the FPGA.
 * inputs: dac = 0 or 1
 */
{
	
	uint8_t data;
	vector<uint32_t> msg;
	uint8_t SD, MSD, MHD;
	uint8_t edgeMSD, edgeMHD;
	uint8_t interruptAddr, controllerAddr, sdAddr, msdMhdAddr;

	// relevant DAC registers
	interruptAddr = 0x1; // LVDS[7] SYNC[6]
	controllerAddr = 0x6; // LSURV[7] LAUTO[6] LFLT[5:2] LTRH[1:0]
	sdAddr = 0x5; // SD[7:4] CHECK[0]
	msdMhdAddr = 0x4; // MSD[7:4] MHD[3:0]

	if (dac < 0 || dac >= NUM_CHANNELS) {
		FILE_LOG(logERROR) << "FPGA::setup_DAC: unknown DAC, " << dac;
		return -1;
	}
	FILE_LOG(logINFO) << "Setting up DAC " << dac;

	const vector<CHIPCONFIG_IO_TARGET> targets = {CHIPCONFIG_TARGET_DAC_0, CHIPCONFIG_TARGET_DAC_1};
	
	// Step 1: calibrate and set the LVDS controller.
	// get initial states of registers
	
	// TODO: remove int(... & 0x1F)
	data = read_SPI(targets[dac], interruptAddr);
	FILE_LOG(logDEBUG2) <<  "Reg: " << myhex << int(interruptAddr & 0x1F) << " Val: " << int(data & 0xFF);
	data = read_SPI(targets[dac], msdMhdAddr);
	FILE_LOG(logDEBUG2) <<  "Reg: " << myhex << int(msdMhdAddr & 0x1F) << " Val: " << int(data & 0xFF);
	data = read_SPI(targets[dac], sdAddr);
	FILE_LOG(logDEBUG2) <<  "Reg: " << myhex << int(sdAddr & 0x1F) << " Val: " << int(data & 0xFF);

	// Ensure that surveilance and auto modes are off
	data = read_SPI(targets[dac], controllerAddr);
	FILE_LOG(logDEBUG2) <<  "Reg: " << myhex << int(controllerAddr & 0x1F) << " Val: " << int(data & 0xFF);
	data = 0;
	msg = build_DAC_SPI_msg(targets[dac], {{controllerAddr, data}});
	write_SPI(msg);

	// Slide the data valid window left (with MSD) and check for the interrupt
	SD = 0;  //(sample delay nibble, stored in Reg. 5, bits 7:4)
	MSD = 0; //(setup delay nibble, stored in Reg. 4, bits 7:4)
	MHD = 0; //(hold delay nibble,  stored in Reg. 4, bits 3:0)
	data = SD << 4;

	msg = build_DAC_SPI_msg(targets[dac], {{sdAddr, data}});
	write_SPI(msg);

	for (MSD = 0; MSD < 16; MSD++) {
		FILE_LOG(logDEBUG2) <<  "Setting MSD: " << int(MSD);
		
		data = (MSD << 4) | MHD;
		msg = build_DAC_SPI_msg(targets[dac], {{msdMhdAddr, data}});
		write_SPI(msg);
		FILE_LOG(logDEBUG2) <<  "Write Reg: " << myhex << int(msdMhdAddr & 0x1F) << " Val: " << int(data & 0xFF);
		
		data = read_SPI(targets[dac], sdAddr);
		FILE_LOG(logDEBUG2) <<  "Read Reg: " << myhex << int(sdAddr & 0x1F) << " Val: " << int(data & 0xFF);
		
		bool check = data & 1;
		FILE_LOG(logDEBUG2) << "Check: " << check;
		if (!check)
			break;
	}
	edgeMSD = MSD;
	FILE_LOG(logDEBUG) << "Found MSD: " << int(edgeMSD);

	// Clear the MSD, then slide right (with MHD)
	MSD = 0;
	for (MHD = 0; MHD < 16; MHD++) {
		FILE_LOG(logDEBUG2) <<  "Setting MHD: " << int(MHD);
		
		data = (MSD << 4) | MHD;
		msg = build_DAC_SPI_msg(targets[dac], {{msdMhdAddr, data}});
		write_SPI(msg);
		
		data = read_SPI(targets[dac], sdAddr);
		FILE_LOG(logDEBUG2) << "Read: " << myhex << int(data & 0xFF);
		bool check = data & 1;
		FILE_LOG(logDEBUG2) << "Check: " << check;
		if (!check)
			break;
	}
	edgeMHD = MHD;
	FILE_LOG(logDEBUG) << "Found MHD = " << int(edgeMHD);
	SD = (edgeMHD - edgeMSD) / 2;
	FILE_LOG(logDEBUG) << "Setting SD = " << int(SD);

	// Clear MSD and MHD
	MHD = 0;
	data = (MSD << 4) | MHD;
	msg = build_DAC_SPI_msg(targets[dac], {{msdMhdAddr, data}});
	write_SPI(msg);

	// Set the optimal sample delay (SD)
	data = SD << 4;
	msg = build_DAC_SPI_msg(targets[dac], {{sdAddr, data}});
	write_SPI(msg);

	// AD9376 data sheet advises us to enable surveilance and auto modes, but this
	// has introduced output glitches in limited testing
	// set the filter length, threshold, and enable surveilance mode and auto mode
	// int filter_length = 12;
	// int threshold = 1;
	// data = (1 << 7) | (1 << 6) | (filter_length << 2) | (threshold & 0x3);
	// msg = build_DAC_SPI_msg(targets[dac], {{controllerAddr, data}});
	// write_SPI(msg);
	
	// turn on SYNC FIFO (limited testing doesn't show it to help)
	// enable_DAC_FIFO(dac);

	return 0;
}

int APS2::run_chip_config(const uint32_t & addr /* default = 0 */) {
	FILE_LOG(logINFO) << "Running chip config from address " << hexn<8> << addr;
	// construct the chip config command
	APSEthernetPacket packet;
	packet.header.command.r_w = 0;
	packet.header.command.cmd =  static_cast<uint32_t>(APS_COMMANDS::RUNCHIPCONFIG);
	packet.header.command.cnt = 0;
	packet.header.addr = addr;
	APSEthernet::get_instance().send(deviceSerial_, packet);

	//Retrieve the data packet(s)
	auto response = read_packets(1)[0];
	if (response.header.command.mode_stat == RUNCHIPCONFIG_SUCCESS) {
		FILE_LOG(logDEBUG1) << "Chip config successful";
	}
	return response.header.command.mode_stat;
}

int APS2::enable_DAC_FIFO(const int & dac) {

	if (dac < 0 || dac >= NUM_CHANNELS) {
		FILE_LOG(logERROR) << "FPGA::setup_DAC: unknown DAC, " << dac;
		return -1;
	}

	uint8_t data = 0;
	uint16_t syncAddr = 0x0;
	uint16_t fifoStatusAddr = 0x7; // FIFOSTAT[7] FIFOPHASE[6:4]
	FILE_LOG(logDEBUG) << "Enabling DAC " << dac << " FIFO";
	const vector<CHIPCONFIG_IO_TARGET> targets = {CHIPCONFIG_TARGET_DAC_0, CHIPCONFIG_TARGET_DAC_1};

	// set sync bit (Reg 0, bit 2)
	data = read_SPI(targets[dac], syncAddr);
	data = data | (1 << 2);
	vector<uint32_t> msg = build_DAC_SPI_msg(targets[dac], {{syncAddr, data}});
	write_SPI(msg);

	// read back FIFO phase to ensure we are in a safe zone
	data = read_SPI(targets[dac], fifoStatusAddr);

	// phase (FIFOSTAT) is in bits <6:4>
	FILE_LOG(logDEBUG2) << "Read: " << myhex << int(data & 0xFF);
	FILE_LOG(logDEBUG) << "FIFO phase = " << ((data & 0x70) >> 4);

	//TODO: return an error code if the FIFO phase is bad
	return 0;
}

int APS2::disable_DAC_FIFO(const int & dac) {
	uint8_t data, mask;
	uint32_t syncAddr = 0x0;
	const vector<CHIPCONFIG_IO_TARGET> targets = {CHIPCONFIG_TARGET_DAC_0, CHIPCONFIG_TARGET_DAC_1};

	FILE_LOG(logDEBUG1) << "Disable DAC " << dac << " FIFO";
	// clear sync bit
	data = read_SPI(targets[dac], syncAddr);
	mask = (0x1 << 2);
	vector<uint32_t> msg = build_DAC_SPI_msg(targets[dac], {{syncAddr, data & ~mask}});
	write_SPI(msg);

	return 0;
}

int APS2::set_offset_register(const int & dac, const float & offset) {
	/* APS2::set_offset_register
	 * Write the zero register for the associated channel
	 * offset - offset in normalized full range (-1, 1)
	 */
	 /*

	uint32_t zeroRegisterAddr;
	uint16_t scaledOffset;

	switch (dac) {
		case 0:
		case 2:
			zeroRegisterAddr = FPGA_ADDR_CHA_ZERO;
			break;
		case 1:
			// fall through
		case 3:
			zeroRegisterAddr = FPGA_ADDR_CHB_ZERO;
			break;
		default:
			return -2;
	}

	scaledOffset = uint16_t(offset * MAX_WF_AMP);
	FILE_LOG(logINFO) << "Setting DAC " << dac << "  zero register to " << scaledOffset;

	//TODO: fix me!
	// socket_.write_register(zeroRegisterAddr, scaledOffset);
	*/
	return 0;
}

int APS2::write_waveform(const int & ch, const vector<int16_t> & wfData) {
	/*Write waveform data to FPGA memory
	 * ch = channel (0-1)
	 * wfData = signed int16_t waveform data
	 */

	uint32_t startAddr = (ch == 0) ? MEMORY_ADDR+WFA_OFFSET : MEMORY_ADDR+WFB_OFFSET;

	//Waveform length used by FPGA must be an integer multiple of WF_MODULUS and is 0 counted
	// uint32_t wfLength = wfData.size() / WF_MODULUS - 1;
	// FILE_LOG(logINFO) << "Loading Waveform length " << wfData.size() << " (FPGA count = " << wfLength << " ) into DAC " << dac;

	// disable cache
	write_memory(CACHE_CONTROL_ADDR, 0);

	FILE_LOG(logDEBUG2) << "Loading waveform at " << myhex << startAddr;
	vector<uint32_t> packedData;
	for (size_t ct=0; ct < wfData.size(); ct += 2) {
		packedData.push_back(((uint32_t)wfData[ct] << 16) | (uint32_t)wfData[ct+1]);
	}
	write_memory(startAddr, packedData);

	// enable cache
	write_memory(CACHE_CONTROL_ADDR, 1);

	return 0;
}

int APS2::write_sequence(const vector<uint32_t> & data) {
	FILE_LOG(logDEBUG2) << "Loading sequence of length " << data.size();

	// disable cache
	write_memory(CACHE_CONTROL_ADDR, 0);

	write_memory(MEMORY_ADDR+SEQ_OFFSET, data);

	// enable cache
	write_memory(CACHE_CONTROL_ADDR, 1);

	return 0;
}

int APS2::write_memory_map(const uint32_t & wfA, const uint32_t & wfB, const uint32_t & seq) { /* see header for defaults */
	/* Writes the partitioning of external memory to registers. Takes 3 offsets 
	 * (in bytes) for wfA/B and seq data */
	FILE_LOG(logDEBUG2) << "Writing memory map with offsets wfA: " << wfA << ", wfB: " << wfB << ", seq: " << seq;

	write_memory(WFA_OFFSET_ADDR, MEMORY_ADDR + wfA);
	write_memory(WFB_OFFSET_ADDR, MEMORY_ADDR + wfB);
	write_memory(SEQ_OFFSET_ADDR, MEMORY_ADDR + seq);

	return 0;
}

//TODO: implement
/*
int APS2::save_state_file(string & stateFile){

	if (stateFile.length() == 0) {
		stateFile += "cache_" + deviceSerial_ + ".h5";
	}

	FILE_LOG(logDEBUG) << "Writing State For Device: " << deviceSerial_ << " to hdf5 file: " << stateFile;
	H5::H5File H5StateFile(stateFile, H5F_ACC_TRUNC);
	string rootStr = "";
	write_state_to_hdf5(H5StateFile, rootStr);
	//Close the file
	H5StateFile.close();
	return 0;
}

int APS2::read_state_file(string & stateFile){

	if (stateFile.length() == 0) {
		stateFile += "cache_" + deviceSerial_ + ".h5";
	}

	FILE_LOG(logDEBUG) << "Reading State For Device: " << deviceSerial_ << " from hdf5 file: " << stateFile;
	H5::H5File H5StateFile(stateFile, H5F_ACC_RDONLY);
	string rootStr = "";
	read_state_from_hdf5(H5StateFile, rootStr);
	//Close the file
	H5StateFile.close();
	return 0;
}

int APS2::write_state_to_hdf5(H5::H5File & H5StateFile, const string & rootStr){
	std::ostringstream tmpStream;
	//For now assume 4 channel data
	for(int chanct=0; chanct<4; chanct++){
		tmpStream.str("");
		tmpStream << rootStr << "/chan_" << chanct+1;
		FILE_LOG(logDEBUG) << "Writing State For Channel " << chanct + 1 << " to hdf5 file";
		FILE_LOG(logDEBUG) << "Creating Group: " << tmpStream.str();
		H5::Group tmpGroup = H5StateFile.createGroup(tmpStream.str());
		tmpGroup.close();
		channels_[chanct].write_state_to_hdf5(H5StateFile,tmpStream.str());
	}
	return 0;
}

int APS2::read_state_from_hdf5(H5::H5File & H5StateFile, const string & rootStr){
	//For now assume 4 channel data
	std::ostringstream tmpStream;
	for(int chanct=0; chanct<4; chanct++){
		tmpStream.str("");
		tmpStream << rootStr << "/chan_" << chanct+1;
		FILE_LOG(logDEBUG) << "Reading State For Channel " << chanct + 1<< " from hdf5 file";
		channels_[chanct].read_state_from_hdf5(H5StateFile,tmpStream.str());
	}
	return 0;
}

*/



string APS2::print_status_bank(const APSStatusBank_t & status) {
	std::ostringstream ret;

	ret << "Host Firmware Version = " << std::hex << status.hostFirmwareVersion << endl;
	ret << "User Firmware Version = " << status.userFirmwareVersion << endl;
	ret << "Configuration Source  = " << status.configurationSource << endl;
	ret << "User Status           = " << status.userStatus << endl;
	ret << "DAC 0 Status          = " << status.dac0Status << endl;
	ret << "DAC 1 Status          = " << status.dac1Status << endl;
	ret << "PLL Status            = " << status.pllStatus << endl;
	ret << "VCXO Status           = " << status.vcxoStatus << endl;
	ret << "Send Packet Count     = " << std::dec << status.sendPacketCount << endl;
	ret << "Recv Packet Count     = " << status.receivePacketCount << endl;
	ret << "Seq Skip Count        = " << status.sequenceSkipCount << endl;
	ret << "Seq Dup.  Count       = " << status.sequenceDupCount << endl;
	ret << "FCS Overrun Count     = " << status.fcsOverrunCount << endl;
	ret << "Packet Overrun Count  = " << status.packetOverrunCount << endl;
	ret << "Uptime (s)            = " << status.uptimeSeconds << endl;
	ret << "Uptime (ns)           = " << status.uptimeNanoSeconds << endl;
	return ret.str();
}



string APS2::printAPSChipCommand(APSChipConfigCommand_t & cmd) {
    std::ostringstream ret;

    ret << std::hex << cmd.packed << " =";
    ret << " Target: " << cmd.target;
    ret << " SPICNT_DATA: " << cmd.spicnt_data;
    ret << " INSTR: " << cmd.instr;
    return ret.str();
}



