cmake_minimum_required(VERSION 3.16)
project(mini-redis LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# Main executable
add_executable(mini_redis
    main.cpp
    InMemoryStorage.cpp
    RedisLite.cpp
)
target_link_libraries(mini_redis Threads::Threads)

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
