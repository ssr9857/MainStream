cmake_minimum_required(VERSION 3.10)
project(seawaystream)

# 添加头文件搜索路径
message("build for RK3588")
message("PROJECT_SOURCE_DIR = ${PROJECT_SOURCE_DIR}") # PROJECT_SOURCE_DIR = 
include_directories(
                    ${PROJECT_SOURCE_DIR}/include/
                    ${PROJECT_SOURCE_DIR}/3rdparty/jsoncpp/include
                    ${PROJECT_SOURCE_DIR}/3rdparty/openssl/include
                    ${PROJECT_SOURCE_DIR}/3rdparty/mqtt/include
                    ${PROJECT_SOURCE_DIR}/3rdparty/httppost/include
                    ${PROJECT_SOURCE_DIR}/3rdparty/rdkafka/include
                    ${PROJECT_SOURCE_DIR}/3rdparty/EdgeSDK/include
                    ${PROJECT_SOURCE_DIR}/3rdparty/rknn/include)

# 添加源文件并生成可执行文件
aux_source_directory(${PROJECT_SOURCE_DIR}/src SRC_FILES)
add_executable(seawaystream 
            ${SRC_FILES}
            main.cpp)

# 将指定的库链接到目标（可执行文件或库文件）上
message("PROJECT_NAME = ${PROJECT_NAME}") # PROJECT_NAME = seawaystream
message("CMAKE_CURRENT_SOURCE_DIR = ${CMAKE_CURRENT_SOURCE_DIR}") # CMAKE_CURRENT_SOURCE_DIR = 

find_package(OpenCV REQUIRED)
set(ENV_LIBS -lpthread -l${OpenCV_LIBS})
target_link_libraries(${PROJECT_NAME}
                    ${ENV_LIBS}
                    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknn/lib/librknnrt.so
                    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/jsoncpp/lib/libjsoncpp.so
                    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/mqtt/lib/libmosquitto.so
                    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/httppost/lib/libhttppost.so
                    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/openssl/lib/libcrypto.so
                    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/openssl/lib/libssl.so
                    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rdkafka/lib/librdkafka.so.1
                    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/EdgeSDK/lib/libedge_sdk.so)

# 安装可执行文件seawaystream到指定位置，并添加权限
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/build/mainstream DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/ PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_WRITE GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

