cmake_minimum_required(VERSION 3.16)
project(rtkrcv)

set(CMAKE_CXX_STANDARD 17)

add_definitions(-DTRACE -DENAGLO -DENAQZS -DENAGAL -DENACMP -DENAIRN -DENALEO -DNFREQ=7 -DNEXOBS=3 -DSVR_REUSEADDR)
include_directories(./)
include_directories(../../../src)
include_directories(../../../src/rcv)
add_executable(rtkrcv
    rtkrcv.c
    vt.c
    ../../../src/rtklib.h   
    ../../../src/rtkcmn.c
    ../../../src/rtksvr.c
    ../../../src/rtkpos.c
    ../../../src/geoid.c
    ../../../src/solution.c
    ../../../src/lambda.c
    ../../../src/sbas.c
    ../../../src/stream.c
    ../../../src/rcvraw.c
    ../../../src/rtcm.c
    ../../../src/rtcm2.c
    ../../../src/rtcm3.c
    ../../../src/rtcm3e.c
    ../../../src/preceph.c
    ../../../src/options.c
    ../../../src/pntpos.c
    ../../../src/ppp.c
    ../../../src/ppp_ar.c
    ../../../src/rcv/novatel.c
    ../../../src/rcv/ublox.c
    ../../../src/rcv/ss2.c
    ../../../src/rcv/crescent.c
    ../../../src/rcv/skytraq.c
    ../../../src/rcv/javad.c
    ../../../src/rcv/nvs.c
    ../../../src/rcv/binex.c
    ../../../src/rcv/rt17.c
    ../../../src/ephemeris.c
    ../../../src/rinex.c
    ../../../src/ionex.c
    ../../../src/tides.c
    ../../../src/rcv/septentrio.c
    )

if(WIN32)
    target_link_libraries(rtkrcv Winmm.lib)
else()
    target_link_libraries(rtkrcv m)
endif()
