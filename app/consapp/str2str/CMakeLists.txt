cmake_minimum_required(VERSION 3.16)
project(str2str)

set(CMAKE_CXX_STANDARD 17)

add_definitions(-DTRACE -DENAGLO -DENAQZS -DENAGAL -DENACMP -DENAIRN -DENALEO -DNFREQ=7 -DNEXOBS=3 -DSVR_REUSEADDR)
include_directories(./)
include_directories(../../../src)
include_directories(../../../src/rcv)
add_executable(str2str
    str2str.c
    ../../../src/rtklib.h   
    ../../../src/stream.c
    ../../../src/streamsvr.c
    ../../../src/rtkcmn.c
    ../../../src/solution.c
    ../../../src/sbas.c
    ../../../src/geoid.c
    ../../../src/rcvraw.c
    ../../../src/rcv/novatel.c
    ../../../src/rcv/ublox.c
    ../../../src/rcv/ss2.c
    ../../../src/rcv/crescent.c
    ../../../src/rcv/skytraq.c
    ../../../src/rcv/javad.c
    ../../../src/rcv/nvs.c
    ../../../src/rcv/binex.c
    ../../../src/rcv/rt17.c
    ../../../src/rcv/septentrio.c
    ../../../src/rtcm.c
    ../../../src/rtcm2.c
    ../../../src/rtcm3.c
    ../../../src/rtcm3e.c
    ../../../src/preceph.c
    )

if(WIN32)
    target_link_libraries(str2str Winmm.lib ws2_32.lib)
else()
    target_link_libraries(str2str m pthread)
endif()
