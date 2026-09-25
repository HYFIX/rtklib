cmake_minimum_required(VERSION 3.16)
project(geod2rtkp)

add_definitions(-DTRACE -DENAGLO -DENAQZS -DENAGAL -DENACMP -DENAIRN -DENALEO -DNFREQ=7 -DNEXOBS=3)
include_directories(../../../src)

add_executable(geod2rtkp
    geod2rtkp.c
    ../../../src/rtklib.h
    ../../../src/rtkcmn.c
    ../../../src/rtkpos.c
    ../../../src/solution.c
    ../../../src/lambda.c
    ../../../src/sbas.c
    ../../../src/preceph.c
    ../../../src/pntpos.c
    ../../../src/geoid.c
    ../../../src/ephemeris.c
    ../../../src/options.c
    ../../../src/ppp.c
    ../../../src/ppp_ar.c
    ../../../src/rtcm.c
    ../../../src/rtcm2.c
    ../../../src/rtcm3.c
    ../../../src/rtcm3e.c
    ../../../src/ionex.c
    ../../../src/tides.c)

if(WIN32)
    target_link_libraries(geod2rtkp Winmm.lib)
else()
    target_link_libraries(geod2rtkp m)
endif()
