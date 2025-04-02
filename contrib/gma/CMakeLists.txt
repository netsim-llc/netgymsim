check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources    
        #test/gma-examples-test-suite.cc
        )
endif()    

build_lib(
    LIBNAME gma
    SOURCE_FILES helper/gma-helper.cc
                 helper/svc-client-server-helper.cc
                 helper/poisson-udp-client-helper.cc
                 model/gma-trailer.cc
                 model/gma-header.cc
                 model/mx-control-header.cc
                 model/gma-protocol.cc
                 model/gma-tx-control.cc
                 model/gma-rx-control.cc
                 model/gma-virtual-interface.cc
                 model/measurement-manager.cc
                 model/qos-measurement-manager.cc
                 model/link-state.cc
                 model/phy-access-control.cc
                 model/fq-ppp-queue-disc.cc
                 model/ppp-queue-disc.cc
                 model/ppp-delay-queue-disc.cc
                 model/ppp-aqm-queue-disc-v2.cc
                 model/ppp-pie-queue-disc.cc                        
                 model/ppp-tag.cc
                 model/svc-trace-client.cc
                 model/ns-pf-ff-mac-scheduler.cc
                 model/gma-ideal-wifi-manager.cc
                 model/gma-minstrel-ht-wifi-manager.cc
                 model/poisson-udp-client.cc
                 model/gma-data-processor.cc
    HEADER_FILES helper/gma-helper.h
                 helper/svc-client-server-helper.h
                 helper/poisson-udp-client-helper.h
                 model/gma-trailer.h
                 model/gma-header.h
                 model/mx-control-header.h
                 model/gma-protocol.h
                 model/gma-tx-control.h
                 model/gma-rx-control.h
                 model/gma-virtual-interface.h
                 model/measurement-manager.h
                 model/qos-measurement-manager.h
                 model/link-state.h
                 model/phy-access-control.h
                 model/fq-ppp-queue-disc.h
                 model/ppp-queue-disc.h
                 model/ppp-delay-queue-disc.h
                 model/ppp-aqm-queue-disc-v2.h
                 model/ppp-pie-queue-disc.h
                 model/ppp-tag.h
                 model/svc-trace-client.h
                 model/ns-pf-ff-mac-scheduler.h
                 model/gma-ideal-wifi-manager.h
                 model/gma-minstrel-ht-wifi-manager.h
                 model/poisson-udp-client.h
                 model/gma-data-processor.h
    LIBRARIES_TO_LINK
        ${libcore}
        ${libnetwork}
        ${libvirtual-net-device}
        ${libinternet}
        ${libmobility}
        ${liblte}
        ${libwifi}
    TEST_SOURCES test/gma-test-suite.cc
                 ${examples_as_tests_sources}
)
    
