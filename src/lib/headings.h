/*
 * headings.h
 *
 * Bring all the includes and constants together in one file
 *
 *  Created on: Jun 13, 2012
 *      Author: cryan
 */


// INCLUDES
#ifndef HEADINGS_H_
#define HEADINGS_H_

//Standard library includes
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <stdio.h>
#include <map>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <queue>
using std::vector;
using std::string;
using std::cout;
using std::endl;
using std::map;
using std::runtime_error;

#include <thread>
#include <mutex>
#include <atomic>
#include <utility>
#include <chrono>

//Logger IDs
#define FILE_LOG 1
#define CONSOLE_LOG 2

//Deal with some Windows/Linux difference
#ifdef _WIN32
#include "windows.h"
#include "ftd2xx_win.h"

#else
#include "ftd2xx.h"
#define EXPORT
#endif

#ifdef _MSC_VER
//Visual C++ doesn't have a usleep function so define one

inline void usleep(int waitTime) {
    long int time1 = 0, time2 = 0, freq = 0;

    QueryPerformanceCounter((LARGE_INTEGER *) &time1);
    QueryPerformanceFrequency((LARGE_INTEGER *)&freq);

    do {
        QueryPerformanceCounter((LARGE_INTEGER *) &time2);
    } while((time2-time1) < waitTime);
}
#else
//Needed for usleep on gcc 4.7
#include <unistd.h>

#endif // _MSC_VER

#include <plog/Log.h>

//Simple structure for pairs of address/data checksums
struct CheckSum {
	WORD address;
	WORD data;
};

//PLL routines go through sets of address/data pairs
typedef std::pair<ULONG, UCHAR> PLLAddrData;

//some vectors
typedef vector<unsigned short> WordVec;

//Load all the constants
#include "constants.h"

#include "FTDI.h"
#include "FPGA.h"

#include "LLBank.h"
#include "Channel.h"
#include "BankBouncerThread.h"
#include "APS.h"
#include "APSRack.h"


//Helper function for hex formating with the 0x out front
inline std::ios_base&
myhex(std::ios_base& __base)
{
  __base.setf(std::ios_base::hex, std::ios_base::basefield);
  __base.setf(std::ios::showbase);
  return __base;
}

inline int mymod(int a, int b) {
	int c = a % b;
	if (c < 0)
		c += b;
	return c;
}

#endif /* HEADINGS_H_ */
