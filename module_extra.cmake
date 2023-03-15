if(NOT TARGET soem)
    find_package(soem REQUIRED)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${SOEM_INCLUDE_DIRS})
target_compile_definitions(${PROJECT_NAME} PUBLIC __STDC_LIMIT_MACROS)
target_link_libraries(${PROJECT_NAME} ${SOEM_LIBS})

if(NAP_BUILD_CONTEXT MATCHES "framework_release")
    # Install license into packaged app
    install(FILES ${SOEM_LICENSE_FILES} DESTINATION licenses/soem)
endif()
