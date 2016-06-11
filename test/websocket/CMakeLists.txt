# Part of Beast

GroupSources(extras/beast extras)
GroupSources(include/beast beast)
GroupSources(test/websocket "/")
GroupSources(test/core/zlib zlib)

add_executable (websocket-tests
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    ${ZLIB_SOURCES}
    ../../extras/beast/unit_test/main.cpp
    websocket_async_echo_server.hpp
    websocket_sync_echo_server.hpp
    error.cpp
    option.cpp
    rfc6455.cpp
    stream.cpp
    teardown.cpp
    frame.cpp
    mask.cpp
    stream_base.cpp
    utf8_checker.cpp
)

if (NOT WIN32)
    target_link_libraries(websocket-tests ${Boost_LIBRARIES} Threads::Threads)
endif()

if (MINGW)
    set_target_properties(websocket-tests PROPERTIES COMPILE_FLAGS "-Wa,-mbig-obj -Og")
endif()

add_executable (websocket-echo
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    ${ZLIB_SOURCES}
    websocket_async_echo_server.hpp
    websocket_sync_echo_server.hpp
    websocket_echo.cpp
)

if (NOT WIN32)
    target_link_libraries(websocket-echo ${Boost_LIBRARIES} Threads::Threads)
endif()
