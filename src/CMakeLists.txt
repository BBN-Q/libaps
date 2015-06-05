# Build instructions (from project root folder):
#
# mkdir build
# cd build
# cmake -G "MSYS Makefiles" -DHDF5_INCLUDE_DIR:string=/path/to/hdf5 -DCMAKE_BUILD_TYPE=Release/Debug ../src
#
# make
#
# On Mac/Linux you can skip the '-G "MSYS Makefiles" option'

PROJECT( libaps )
CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

#For distributing on Linux hard-code in the r-path
if(NOT WIN32)
SET(CMAKE_SKIP_BUILD_RPATH FALSE)
SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
SET(CMAKE_INSTALL_RPATH "\$ORIGIN:\$ORIGIN/system_libs")
endif(NOT WIN32)

#Look for hdf5 directly passed; otherwise look for system one
if(NOT DEFINED HDF5_INCLUDE_DIR)
	FIND_PACKAGE(HDF5)
else()
	LINK_DIRECTORIES( ${LINK_DIRECTORIES} ${HDF5_INCLUDE_DIR}/../lib)
endif()
INCLUDE_DIRECTORIES( ${HDF5_INCLUDE_DIR} )
INCLUDE_DIRECTORIES( ${HDF5_INCLUDE_DIR}/cpp )

#Grab all the header files in the src directory
INCLUDE_DIRECTORIES("./lib/")
INCLUDE_DIRECTORIES("../ftdi/")

#Add flags for gcc and C++11
ADD_DEFINITIONS(${CMAKE_CXX_FLAGS} "-DGCC")
ADD_DEFINITIONS(${CMAKE_CXX_FLAGS} "-std=c++11")
ADD_DEFINITIONS(${CMAKE_CXX_FLAGS} "-Wall")

SET ( DLL_SRC
	./lib/libaps.cpp
	./lib/APSRack.cpp
	./lib/APS.cpp
	./lib/Channel.cpp
	./lib/LLBank.cpp
	./lib/FPGA.cpp
	./lib/FTDI.cpp
)

SET_SOURCE_FILES_PROPERTIES( ${DLL_SRC} PROPERTIES LANGUAGE CXX )

ADD_LIBRARY( aps SHARED ${DLL_SRC} )

set(APS_VERSION_MAJOR 1)
set(APS_VERSION_MINOR 4)
set(APS_VERSION_PATCH 0)
set(APS_VERSION_STRING ${APS_VERSION_MAJOR}.${APS_VERSION_MINOR}.${APS_VERSION_PATCH})
set_target_properties(aps PROPERTIES VERSION ${APS_VERSION_STRING} SOVERSION ${APS_VERSION_MAJOR})

ADD_EXECUTABLE(test
	./lib/test.cpp
)
TARGET_LINK_LIBRARIES(test aps)

ADD_EXECUTABLE(debug_oscillator	./lib/debug_oscillator.cpp )
TARGET_LINK_LIBRARIES(debug_oscillator aps)

if(WIN32)
FIND_LIBRARY(FTDI_LIBRARY ftd2xx64 "../ftdi/")
TARGET_LINK_LIBRARIES(aps hdf5 hdf5_cpp ${FTDI_LIBRARY})
else(WIN32)
TARGET_LINK_LIBRARIES(aps hdf5 hdf5_cpp ftd2xx)
endif(WIN32)
