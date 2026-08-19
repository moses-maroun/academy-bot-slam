#include "rclcpp/rclcpp.hpp"

class CourierExecutor : public rclcpp::Node
{
public:
  CourierExecutor() : Node("courier_executor")
  {
    RCLCPP_INFO(get_logger(), "courier_executor started.");
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CourierExecutor>());
  rclcpp::shutdown();
  return 0;
}
