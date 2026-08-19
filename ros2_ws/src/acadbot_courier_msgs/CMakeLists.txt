cmake_minimum_required(VERSION 3.8)
project(acadbot_courier_msgs)

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "srv/RequestDelivery.srv"
  "action/DeliverPackage.action"
  "msg/CourierJob.msg"
  "msg/CourierJobStatus.msg"
)

ament_package()
