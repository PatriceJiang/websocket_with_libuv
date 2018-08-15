cmake_minimum_required(VERSION 3.0)

project(hello CXX)
set(CMAKE_CXX_STANDARD 11)

set(CC_EXTR "E:/Github/cocos2d-x/external")
#set(CC_THREAD "E:/Github/cocos2d-x/cocos/thread")
set(CC_THREAD "E:/Projects/looper-github/thread")
#set(UV_ROOT "E:/Github/libuv-v1.22.0/libuv-v1.22.0")


file(GLOB LOOP_SRC  ${CC_THREAD}/*.cpp ${CC_THREAD}/*.h)
file(GLOB CURR_SRC *.cpp *.h)

set(SSL_ROOT ${CC_EXTR}/openssl)
set(WS_ROOT ${CC_EXTR}/websockets)
set(UV_ROOT ${CC_EXTR}/uv)

include_directories(
    ${SSL_ROOT}/include/win32
    ${WS_ROOT}/include/win32
    ${UV_ROOT}/include
    ${CC_THREAD}
)


link_directories(${SSL_ROOT}/prebuilt/win32)
link_directories(${WS_ROOT}/prebuilt/win32)
#link_directories(${UV_ROOT}/build/Debug)
link_directories(${UV_ROOT}/prebuilt/win32)

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
)