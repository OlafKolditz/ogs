append_source_files(SOURCES)

add_library(RichardsFlow ${SOURCES})
if(BUILD_SHARED_LIBS)
    install(TARGETS RichardsFlow LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
target_link_libraries(RichardsFlow PUBLIC ProcessLib PRIVATE ParameterLib)

if(BUILD_TESTING)
    include(Tests.cmake)
endif()
