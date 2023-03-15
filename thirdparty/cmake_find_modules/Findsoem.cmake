if(WIN32)
  # default SOEM directory
  find_path(SOEM_DIR
            NO_CMAKE_FIND_ROOT_PATH
            NAMES include/soem/ethercat.h
            HINTS ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/soem/msvc/x86_64
            )

    # wpcap dir
    set(WPCAP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/soem/source/oshw/win32/wpcap)

    # library
    set(SOEM_LIBS 
        ${SOEM_DIR}/lib/soem.lib 
        ${WPCAP_DIR}/Lib/x64/wpcap.lib
        ${WPCAP_DIR}/Lib/x64/Packet.lib
        Ws2_32 winmm)

    # all includes for SOEM Windows
    set(SOEM_INCLUDE_DIRS 
        ${SOEM_DIR}/include 
        ${SOEM_DIR}/include/soem 
        ${WPCAP_DIR}/Include)
elseif(APPLE)
  # default SOEM directory
  find_path(SOEM_DIR
          NO_CMAKE_FIND_ROOT_PATH
          NAMES include/soem/ethercat.h
          HINTS ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/soem/macos/x86_64
          )
    # library
    set(SOEM_LIBS 
        ${SOEM_DIR}/lib/libsoem.a
        pcap
        pthread)

    # all includes for SOEM
    set(SOEM_INCLUDE_DIRS 
        ${SOEM_DIR}/include 
        ${SOEM_DIR}/include/soem)
else()
  # default SOEM directory
  find_path(SOEM_DIR
          NO_CMAKE_FIND_ROOT_PATH
          NAMES include/soem/ethercat.h
          HINTS ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/soem/linux/${ARCH}
          )
    # library
    set(SOEM_LIBS 
        ${SOEM_DIR}/lib/libsoem.a
        pthread 
        rt)

    # all includes for SOEM
    set(SOEM_INCLUDE_DIRS 
        ${SOEM_DIR}/include 
        ${SOEM_DIR}/include/soem)
endif()

# SOEM Source Code Directory
find_path(SOEM_SOURCE_DIR
    NO_CMAKE_FIND_ROOT_PATH
    NAMES LICENSE
    HINTS ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/soem/source
)

# SOEM License
set(SOEM_LICENSE_FILES ${SOEM_SOURCE_DIR}/LICENSE)

mark_as_advanced(SOEM_INCLUDE_DIRS)

# promote package for find
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(soem REQUIRED_VARS SOEM_DIR)

# SOEM is always linked static
add_library(soem STATIC IMPORTED)
set_target_properties(soem PROPERTIES
                      IMPORTED_CONFIGURATIONS "Debug;Release"
                      IMPORTED_LOCATION_RELEASE ${SOEM_LIBS}
                      IMPORTED_LOCATION_DEBUG ${SOEM_LIBS}
                      )
