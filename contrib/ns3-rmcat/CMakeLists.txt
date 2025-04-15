check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources    
        #test/ns3-rmcat-examples-test-suite.cc
        )
endif()    

build_lib(
    LIBNAME ns3-rmcat
    SOURCE_FILES model/apps/rmcat-sender.cc
                 model/apps/rmcat-receiver.cc
                 model/apps/rtp-header.cc
                 model/syncodecs/syncodecs.cc
                 model/syncodecs/traces-reader.cc
                 model/congestion-control/sender-based-controller.cc
                 model/congestion-control/dummy-controller.cc
                 model/congestion-control/nada-controller.cc
                 model/topo/topo.cc
                 model/topo/wired-topo.cc
                 model/topo/wifi-topo.cc
    HEADER_FILES model/apps/rmcat-constants.h
                 model/apps/rmcat-sender.h
                 model/apps/rmcat-receiver.h
                 model/apps/rtp-header.h
                 model/syncodecs/syncodecs.h
                 model/syncodecs/traces-reader.h
                 model/congestion-control/sender-based-controller.h
                 model/congestion-control/dummy-controller.h
                 model/congestion-control/nada-controller.h
                 model/topo/topo.h
                 model/topo/wired-topo.h
                 model/topo/wifi-topo.h
    LIBRARIES_TO_LINK ${libcore}
    TEST_SOURCES test/rmcat-common-test.cc
                 test/rmcat-wired-test-case.cc
                 test/rmcat-wired-test-suite.cc
                 test/rmcat-wired-varyparam-test-suite.cc
                 test/rmcat-wifi-test-case.cc
                 test/rmcat-wifi-test-suite.cc
                 ${examples_as_tests_sources}
)
    
