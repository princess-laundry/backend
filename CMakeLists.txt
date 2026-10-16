cmake_minimum_required(VERSION 3.21)

project(posture_backend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

find_package(Drogon CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(posture_core STATIC
    src/auth/DeviceId.cc
    src/auth/TokenService.cc
    src/config/AppConfig.cc
)
target_include_directories(posture_core PUBLIC src)
target_link_libraries(posture_core
    PRIVATE jwt-cpp::jwt-cpp JsonCpp::JsonCpp OpenSSL::Crypto
)
target_compile_options(posture_core PRIVATE -Wall -Wextra -Wpedantic)

# Controllers register themselves through static initializers, so they are
# compiled into the executable directly instead of the static library where
# the linker would drop them.
add_executable(posture_server
    src/main.cc
    src/controllers/AuthController.cc
    src/controllers/ConfigController.cc
    src/controllers/HealthController.cc
    src/filters/JwtFilter.cc
    src/http/ErrorResponse.cc
)
target_link_libraries(posture_server PRIVATE posture_core Drogon::Drogon)
target_compile_definitions(posture_server PRIVATE POSTURE_VERSION="${PROJECT_VERSION}")
target_compile_options(posture_server PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)

    add_executable(posture_tests
        tests/AppConfigTest.cc
        tests/DeviceIdTest.cc
        tests/TokenServiceTest.cc
    )
    # jwt-cpp is linked directly so tests can craft tokens the service would
    # never issue (other keys, algorithms or missing claims).
    target_link_libraries(posture_tests
        PRIVATE posture_core jwt-cpp::jwt-cpp JsonCpp::JsonCpp GTest::gtest_main
    )
    gtest_discover_tests(posture_tests)
endif()
