#include "rclcpp/rclcpp.hpp"

class CourierDispatcher : public rclcpp::Node
{
public:
  CourierDispatcher() : Node("courier_dispatcher")
  {
    RCLCPP_INFO(get_logger(), "courier_dispatcher started.");
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CourierDispatcher>());
  rclcpp::shutdown();
  return 0;
}
