cmake_minimum_required(VERSION 3.8)
project(acadbot_courier)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)

include_directories(include)

add_executable(courier_dispatcher src/courier_dispatcher.cpp)
ament_target_dependencies(courier_dispatcher rclcpp)

add_executable(courier_executor src/courier_executor.cpp)
ament_target_dependencies(courier_executor rclcpp)

install(TARGETS
  courier_dispatcher
  courier_executor
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY config
  DESTINATION share/${PROJECT_NAME})

ament_package()
