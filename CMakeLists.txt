# cmake --build . --config Release

cmake_minimum_required(VERSION 3.16)
project(rtklib)

set(CMAKE_CXX_STANDARD 17)

add_definitions(-DTRACE -DENAGLO -DENAQZS -DENAGAL -DENACMP -DENAIRN -DENALEO -DNFREQ=7 -DNEXOBS=3 -DSVR_REUSEADDR)
include_directories(app/consapp/convbin)
include_directories(app/consapp/rnx2rtkp)
include_directories(app/consapp/rtkrcv)
include_directories(app/consapp/str2str)
include_directories(src)
include_directories(src/rcv)

add_executable(convbin
    app/consapp/convbin/convbin.c
    src/rtklib.h   
    src/rtkcmn.c 
    src/rtcm.c 
    src/rtcm2.c 
    src/rtcm3.c 
    src/rtcm3e.c 
    src/preceph.c 
    src/sbas.c 
    src/ephemeris.c 
    src/rcvraw.c 
    src/rinex.c
    src/ionex.c
    src/convrnx.c
    src/pntpos.c
    src/geoid.c
    src/rcv/binex.c 
    src/rcv/crescent.c 
    src/rcv/javad.c 
    src/rcv/novatel.c 
    src/rcv/nvs.c 
    src/rcv/rt17.c 
    src/rcv/septentrio.c 
    src/rcv/skytraq.c 
    src/rcv/ss2.c 
    src/rcv/ublox.c
    )

add_executable(rnx2rtkp
    app/consapp/rnx2rtkp/rnx2rtkp.c
    src/rtklib.h   
    src/rtkcmn.c
    src/rinex.c   
    src/rtkpos.c   
    src/postpos.c  
    src/solution.c 
    src/lambda.c   
    src/sbas.c     
    src/preceph.c  
    src/pntpos.c   
    src/geoid.c
    src/ephemeris.c
    src/options.c  
    src/ppp.c      
    src/ppp_ar.c   
    src/rtcm.c     
    src/rtcm2.c    
    src/rtcm3.c    
    src/rtcm3e.c   
    src/ionex.c    
    src/tides.c   
    )

add_executable(rtkrcv
    app/consapp/rtkrcv/rtkrcv.c
    app/consapp/rtkrcv/vt.c
    src/rtklib.h   
    src/rtkcmn.c
    src/rtksvr.c
    src/rtkpos.c
    src/geoid.c
    src/solution.c
    src/lambda.c
    src/sbas.c
    src/stream.c
    src/rcvraw.c
    src/rtcm.c
    src/rtcm2.c
    src/rtcm3.c
    src/rtcm3e.c
    src/preceph.c
    src/options.c
    src/pntpos.c
    src/ppp.c
    src/ppp_ar.c
    src/rcv/novatel.c
    src/rcv/ublox.c
    src/rcv/ss2.c
    src/rcv/crescent.c
    src/rcv/skytraq.c
    src/rcv/javad.c
    src/rcv/nvs.c
    src/rcv/binex.c
    src/rcv/rt17.c
    src/ephemeris.c
    src/rinex.c
    src/ionex.c
    src/tides.c
    src/rcv/septentrio.c
    )

add_executable(str2str
    app/consapp/str2str/str2str.c
    src/rtklib.h   
    src/stream.c
    src/streamsvr.c
    src/rtkcmn.c
    src/solution.c
    src/sbas.c
    src/geoid.c
    src/rcvraw.c
    src/rcv/novatel.c
    src/rcv/ublox.c
    src/rcv/ss2.c
    src/rcv/crescent.c
    src/rcv/skytraq.c
    src/rcv/javad.c
    src/rcv/nvs.c
    src/rcv/binex.c
    src/rcv/rt17.c
    src/rcv/septentrio.c
    src/rtcm.c
    src/rtcm2.c
    src/rtcm3.c
    src/rtcm3e.c
    src/preceph.c
    )

if(WIN32)
    target_link_libraries(convbin  Winmm.lib)
    target_link_libraries(rnx2rtkp Winmm.lib)
    target_link_libraries(rtkrcv   Winmm.lib)
    target_link_libraries(str2str  Winmm.lib)
else()
    target_link_libraries(convbin  m)
    target_link_libraries(rnx2rtkp m)
    target_link_libraries(rtkrcv   m)
    target_link_libraries(str2str  m)
endif()
