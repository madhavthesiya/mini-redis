cmake_minimum_required(VERSION 3.16)
project(mini-redis LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# Main executable (CLI mode)
add_executable(mini_redis
    main.cpp
    InMemoryStorage.cpp
    RedisLite.cpp
)
target_link_libraries(mini_redis Threads::Threads)

# Server executable (TCP + RESP mode)
add_executable(mini_redis_server
    server_main.cpp
    Server.cpp
    InMemoryStorage.cpp
    RedisLite.cpp
)
target_link_libraries(mini_redis_server Threads::Threads)
if(WIN32)
    target_link_libraries(mini_redis_server ws2_32)
endif()

# Unit tests
add_executable(tests
    tests/test_main.cpp
    InMemoryStorage.cpp
    RedisLite.cpp
)
target_link_libraries(tests Threads::Threads)

# Thread safety stress test
add_executable(test_threads
    tests/test_thread_safety.cpp
    InMemoryStorage.cpp
    RedisLite.cpp
)
target_link_libraries(test_threads Threads::Threads)

enable_testing()
add_test(NAME unit_tests COMMAND tests)
add_test(NAME thread_safety_tests COMMAND test_threads)
