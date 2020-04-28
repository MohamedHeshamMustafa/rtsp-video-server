cmake_minimum_required(VERSION 3.5)

project(video_server)
find_package(OpenSSL REQUIRED)
find_package(Live555 CONFIG REQUIRED)
find_package(FFmpeg MODULE REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(glog CONFIG REQUIRED)
find_package(Boost COMPONENTS program_options REQUIRED)

file(GLOB SOURCE_FILES "src/*.cpp" "src/config/*.cpp")

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} 
                           PRIVATE 
                           ${CMAKE_CURRENT_SOURCE_DIR}/include
                           ${FFMPEG_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME}
                      PRIVATE
                      Live555::groupsock Live555::liveMedia Live555::UsageEnvironment Live555::BasicUsageEnvironment
                      yaml-cpp
                      glog::glog
                      ${FFMPEG_LIBRARIES}
                      Boost::program_options
)
