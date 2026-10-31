cmake_minimum_required(VERSION 3.24)
project(fgb_pico_face LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/module/fgb-native.json" FGB_MANIFEST)
string(JSON FGB_MODULE_VERSION GET "${FGB_MANIFEST}" Version)

add_library(pico-connect-face MODULE
    src/main.cpp
    src/pico_face_mapping.cpp
    src/pico_face_protocol.cpp
)
target_include_directories(pico-connect-face PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/fgb"
)
target_link_libraries(pico-connect-face PRIVATE ws2_32)
set_target_properties(pico-connect-face PROPERTIES
    PREFIX ""
    OUTPUT_NAME "pico-connect-face"
)

add_custom_command(TARGET pico-connect-face POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/module/fgb-native.json"
            "$<TARGET_FILE_DIR:pico-connect-face>/fgb-native.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/module/icon.svg"
            "$<TARGET_FILE_DIR:pico-connect-face>/icon.svg"
)

enable_testing()
add_executable(pico-face-protocol-tests
    tests/test_protocol.cpp
    src/pico_face_protocol.cpp
)
target_include_directories(pico-face-protocol-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(NAME pico-face-protocol-tests COMMAND pico-face-protocol-tests)

add_executable(pico-face-mapping-tests
    tests/test_mapping.cpp
    src/pico_face_mapping.cpp
    src/pico_face_protocol.cpp
)
target_include_directories(pico-face-mapping-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/fgb"
)
add_test(NAME pico-face-mapping-tests COMMAND pico-face-mapping-tests)

# 需要 29765/29763 空闲（先关 VRCFT），所以不挂到 ctest。
add_executable(pico-face-abi-smoke tests/abi_smoke.cpp)
target_include_directories(pico-face-abi-smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/fgb"
)
target_link_libraries(pico-face-abi-smoke PRIVATE ws2_32)
add_dependencies(pico-face-abi-smoke pico-connect-face)
add_custom_target(smoke-test
    COMMAND pico-face-abi-smoke "$<TARGET_FILE:pico-connect-face>"
    DEPENDS pico-face-abi-smoke pico-connect-face
    COMMENT "Loading pico-connect-face.dll through the ABI with synthetic UDP"
    VERBATIM
)

set(FGB_PACKAGE_STAGE "${CMAKE_BINARY_DIR}/package")
set(FGB_PACKAGE_NAME "pico-connect-face-${FGB_MODULE_VERSION}.zip")
set(FGB_PACKAGE_PATH "${CMAKE_BINARY_DIR}/$<CONFIG>/${FGB_PACKAGE_NAME}")
add_custom_target(package-module
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${FGB_PACKAGE_STAGE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${FGB_PACKAGE_STAGE}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:pico-connect-face>"
            "${FGB_PACKAGE_STAGE}/pico-connect-face.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/module/fgb-native.json"
            "${FGB_PACKAGE_STAGE}/fgb-native.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/module/icon.svg"
            "${FGB_PACKAGE_STAGE}/icon.svg"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${FGB_PACKAGE_STAGE}"
            "${CMAKE_COMMAND}" -E tar cf "${FGB_PACKAGE_PATH}" --format=zip --
            fgb-native.json pico-connect-face.dll icon.svg
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    DEPENDS pico-connect-face
    COMMENT "Packaging ${FGB_PACKAGE_NAME}"
    VERBATIM
)
