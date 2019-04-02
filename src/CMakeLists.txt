# Build instructions (from project root folder):
#
# mkdir build
# cd build
# cmake -G "MSYS Makefiles" -DHDF5_INCLUDE_DIR:string=/path/to/hdf5 -DCMAKE_BUILD_TYPE=Release/Debug ../src
#
# make
#
# On Mac/Linux you can skip the '-G "MSYS Makefiles" option'

CMAKE_MINIMUM_REQUIRED(VERSION 3.1)

PROJECT( libaps )

# Make sure we have C++11 support
set (CMAKE_CXX_STANDARD 11)

set(APS_VERSION_MAJOR 1)
set(APS_VERSION_MINOR 5)
set(APS_VERSION_PATCH 0)
set(APS_VERSION_STRING ${APS_VERSION_MAJOR}.${APS_VERSION_MINOR}.${APS_VERSION_PATCH})

# Look for FTDI
FIND_LIBRARY(FTDI_LIBRARY NAMES ftd2xx64 ftd2xx HINTS "../ftdi/")

# Grab all the header files in the src directory
INCLUDE_DIRECTORIES("./lib/")
INCLUDE_DIRECTORIES("../ftdi/")
INCLUDE_DIRECTORIES("../deps/plog/include")

# Add flags for gcc and warnings
if(MSVC)
	add_definitions(${CMAKE_CXX_FLAGS} "/W3 /wd4127 /wd4201 /wd4244 /wd4267 /wd4996")
else()
	ADD_DEFINITIONS(${CMAKE_CXX_FLAGS} "-DGCC")
	ADD_DEFINITIONS(${CMAKE_CXX_FLAGS} "-Wall")
endif()

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
TARGET_LINK_LIBRARIES(aps ${FTDI_LIBRARY})

set_target_properties(aps PROPERTIES
	VERSION ${APS_VERSION_STRING}
	SOVERSION ${APS_VERSION_MAJOR}
	PREFIX "lib" # force lib prefix even on Windows
	MACOSX_RPATH 1
)

ADD_DEFINITIONS(-DVERSION="${APS_VERSION_STRING}")

ADD_EXECUTABLE(run_tests
	./lib/test.cpp
)
TARGET_LINK_LIBRARIES(run_tests aps)

ADD_EXECUTABLE(debug_oscillator	./lib/debug_oscillator.cpp )
TARGET_LINK_LIBRARIES(debug_oscillator aps)

# installation section
# IF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
# 	SET(CMAKE_INSTALL_PREFIX "${PROJECT_BINARY_DIR}/install" CACHE PATH "default install path" FORCE)
# ENDIF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)

INSTALL(TARGETS aps
	    LIBRARY DESTINATION lib
	    RUNTIME DESTINATION bin)

SET(bin_targets run_tests debug_oscillator)
IF(NOT ${CONDA_BUILD})
	INSTALL(TARGETS ${bin_targets} DESTINATION ${PROJECT_BINARY_DIR}/bin)
ENDIF()

INSTALL(FILES "${PROJECT_SOURCE_DIR}/lib/libaps.h"
	    DESTINATION include)

# On linux we need to ship the system libraries
# TODO: do something similar for windows
IF(UNIX AND NOT APPLE)
	GET_FILENAME_COMPONENT(GCC_PATH ${CMAKE_CXX_COMPILER} DIRECTORY)
	FIND_LIBRARY(LIBGCC NAMES libgcc_s.so.1 HINTS ${GCC_PATH}/../lib64)
	FIND_LIBRARY(STDCPP NAMES libstdc++.so.6 HINTS ${GCC_PATH}/../lib64)
	FILE(GLOB STDCPP_ALL ${STDCPP}*)
	SET(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS
		${LIBGCC}
		${STDCPP_ALL})
ENDIF()

IF(NOT ${CONDA_BUILD})
	INCLUDE( InstallRequiredSystemLibraries )
ENDIF()

if(APPLE)
	# set_target_properties(${bin_targets} PROPERTIES INSTALL_RPATH "@loader_path/../lib")
	set_target_properties(aps PROPERTIES INSTALL_RPATH "@loader_path")
elseif(UNIX)
	# set_target_properties(${bin_targets} PROPERTIES INSTALL_RPATH "\$ORIGIN:\$ORIGIN/../lib")
	set_target_properties(aps PROPERTIES INSTALL_RPATH "\$ORIGIN")
endif()

# Basic "package" target
SET(CPACK_GENERATOR "TGZ")
SET(CPACK_PACKAGE_VERSION_MAJOR ${APS_VERSION_MAJOR})
SET(CPACK_PACKAGE_VERSION_MINOR ${APS_VERSION_MINOR})
SET(CPACK_PACKAGE_VERSION_PATCH ${APS_VERSION_PATCH})
INCLUDE(CPack)
