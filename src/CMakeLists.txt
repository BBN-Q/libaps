# Build instructions (from project root folder):
#
# mkdir build
# cd build
# cmake -G "MSYS Makefiles" -DHDF5_INCLUDE_DIR:string=/path/to/hdf5 -DCMAKE_BUILD_TYPE=Release/Debug ../src
#
# make
#
# On Mac/Linux you can skip the '-G "MSYS Makefiles" option'
#
# If your HDF5 install is in a non-standard location set the HDF5_ROOT
# environment variable


PROJECT( libaps )
CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

# Look for HDF5
FIND_PACKAGE(HDF5 REQUIRED COMPONENTS C CXX)
INCLUDE_DIRECTORIES( ${HDF5_INCLUDE_DIRS} )
LINK_DIRECTORIES( ${LINK_DIRECTORIES} ${HDF5_LIBRARY_DIRS} )

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
set_target_properties(aps PROPERTIES MACOSX_RPATH 1)

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

# installation section
IF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	SET(CMAKE_INSTALL_PREFIX "${PROJECT_BINARY_DIR}/install" CACHE PATH "default install path" FORCE)
ENDIF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)

SET(bin_targets test debug_oscillator)
INSTALL(TARGETS ${bin_targets} aps
	    LIBRARY DESTINATION lib
	    RUNTIME DESTINATION bin)

INSTALL(FILES "${PROJECT_SOURCE_DIR}/lib/libaps.h"
	    DESTINATION include)

# On linux we need to ship the system libraries
# TODO: do something similar for windows
IF(UNIX AND NOT APPLE)
	GET_FILENAME_COMPONENT(GCC_PATH ${CMAKE_CXX_COMPILER} DIRECTORY)
	FIND_LIBRARY(LIBGCC NAMES libgcc_s.so.1 HINTS ${GCC_PATH}/../lib64)
	FIND_LIBRARY(STDCPP NAMES libstdc++.so.6 HINTS ${GCC_PATH}/../lib64)
	FILE(GLOB STDCPP_ALL ${STDCPP}*)
	FIND_LIBRARY(HDF5LIB NAMES hdf5 HINTS ${HDF5_LIBRARY_DIRS})
	FILE(GLOB HDF5LIB_ALL ${HDF5LIB}.*)
	FIND_LIBRARY(HDF5CPPLIB NAMES hdf5_cpp HINTS ${HDF5_LIBRARY_DIRS})
	FILE(GLOB HDF5CPPLIB_ALL ${HDF5CPPLIB}.*)
	SET(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS
		${LIBGCC}
		${STDCPP_ALL}
		${HDF5LIB_ALL}
		${HDF5CPPLIB_ALL})
ENDIF()
INCLUDE( InstallRequiredSystemLibraries )

if(APPLE)
	set_target_properties(${bin_targets} PROPERTIES INSTALL_RPATH "@loader_path/../lib")
	set_target_properties(aps PROPERTIES INSTALL_RPATH "@loader_path")
elseif(UNIX)
	set_target_properties(${bin_targets} PROPERTIES INSTALL_RPATH "\$ORIGIN:\$ORIGIN/../lib")
	set_target_properties(aps PROPERTIES INSTALL_RPATH "\$ORIGIN")
endif()

# Basic "package" target
SET(CPACK_GENERATOR "TGZ")
SET(CPACK_PACKAGE_VERSION_MAJOR ${APS_VERSION_MAJOR})
SET(CPACK_PACKAGE_VERSION_MINOR ${APS_VERSION_MINOR})
SET(CPACK_PACKAGE_VERSION_PATCH ${APS_VERSION_PATCH})
INCLUDE(CPack)
