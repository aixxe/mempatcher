find_package(Catch2 CONFIG REQUIRED)

add_executable(${PROJECT_NAME}_test
    ${CMAKE_SOURCE_DIR}/src/parser.cc
    ${CMAKE_SOURCE_DIR}/test/parser.cc
)

target_link_libraries(${PROJECT_NAME}_test PRIVATE Catch2::Catch2WithMain)
target_compile_features(${PROJECT_NAME}_test PRIVATE cxx_std_23)