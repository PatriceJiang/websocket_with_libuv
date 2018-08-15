cmake_minimum_required(VERSION 3.0)

project(hello CXX)
set(CMAKE_CXX_STANDARD 11)

set(CC_THREAD "E:/Projects/looper-github/thread")


file(GLOB LOOP_SRC  ${CC_THREAD}/*.cpp ${CC_THREAD}/*.h)
file(GLOB CURR_SRC *.cpp *.h)


include_directories(
    ${CC_THREAD}
    usr/include
)

link_directories("usr/lib")

add_executable(hello ${LOOP_SRC} ${CURR_SRC})


target_link_libraries(hello 
  ws2_32 
  psapi
  iphlpapi
  userenv
  uv_a
  libssl
  libcrypto
  websockets
  zlibstaticd
)

add_custom_command(TARGET hello POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/usr/lib"
        $<TARGET_FILE_DIR:hello>)