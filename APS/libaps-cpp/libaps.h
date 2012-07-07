/*
 * libaps.h
 *
 *  Created on: Jun 25, 2012
 *      Author: qlab
 */

#ifndef LIBAPS_H_
#define LIBAPS_H_

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif


#ifdef __cplusplus
extern "C" {
#endif

EXPORT int init();

EXPORT int connect_by_ID(int);

EXPORT int connect_by_serial(char *);

EXPORT int disconnect_by_ID(int);

EXPORT int disconnect_by_serial(char *);

EXPORT int serial2ID(char *);

EXPORT int initAPS(int, char*, int);

EXPORT int program_FPGA(int, char *, int, int);

EXPORT int setup_DACs(int);

EXPORT int set_sampleRate(int, int, int, int);

EXPORT int get_sampleRate(int, int);

EXPORT int set_waveform_float(int, int, float*, int);

EXPORT int set_waveform_int(int, int, short*, int);

EXPORT int load_LL_bank(int, int, int, int, unsigned short*, unsigned short*, unsigned short*, unsigned short*);

EXPORT int load_sequence_file(int, char*);


#ifdef __cplusplus
}
#endif

#endif /* LIBAPS_H_ */