/*
 * debug.cpp
 *
 *  Created on: December 12, 2014
 *      Author: Blake Johnson
 */


#include "headings.h"

#include "libaps.h"

void readStatusCtrl(int id) {
	set_logging_level(logDEBUG2);
	int status = read_status_ctrl(id);
	set_logging_level(logDEBUG1);
	cout << "Read status/control register: " << myhex << status << endl;
}

void enableVCXO(int id) {
	cout << "Enabling VCXO" << endl;
	enable_oscillator(id);
}

void printHelp(){
	string spacing = "   ";
	cout << "Usage: debug_oscillator serial <options>" << endl;
	cout << "Where <options> can be any of the following:" << endl;
	cout << spacing << "-s  read status/control register" << endl;
	cout << spacing << "-vcxo  enable vcxo" << endl;
	cout << spacing << "-h  Print This Help Message" << endl;
}

// command options functions taken from:
// http://stackoverflow.com/questions/865668/parse-command-line-arguments
string getCmdOption(char ** begin, char ** end, const std::string & option)
{
	char ** itr = std::find(begin, end, option);
	if (itr != end && ++itr != end)
	{
		return string(*itr);
	}
	return "";
}

bool cmdOptionExists(char** begin, char** end, const std::string& option)
{
	return std::find(begin, end, option) != end;
}


int main(int argc, char** argv) {

	int err;

	set_logging_level(logDEBUG1);

	if (argc < 2 || cmdOptionExists(argv, argv + argc, "-h")) {
		printHelp();
		return 0;
	}

	char * serial = argv[1];

	//Initialize the APSRack from the DLL
	init();
	int device_id = serial2ID(serial);
	if (device_id == -1) {
		cout << "Could not find APS serial " << serial << endl;
		exit(-1);
	}

	char s[] = "stdout";
	set_log(s);

	//Connect to device
	connect_by_ID(device_id);

	// string bitFile = "../../bitfiles/mqco_aps_latest";
	// err = initAPS(device_id, const_cast<char*>(bitFile.c_str()), true);
	// if (err != APS_OK) {
	// 	cout << "Error initializing APS Rack: " << err << endl;
	// 	exit(-1);
	// }

	// select test to run

	if (cmdOptionExists(argv, argv + argc, "-s")) {
		readStatusCtrl(device_id);
	}

	if (cmdOptionExists(argv, argv + argc, "-vcxo")) {
		enableVCXO(device_id);
	}

	disconnect_by_ID(device_id);

	return 0;

}