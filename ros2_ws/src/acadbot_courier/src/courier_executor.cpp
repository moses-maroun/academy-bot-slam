#include "rclcpp/rclcpp.hpp"

#include "acadbot_courier/locations.hpp"

class CourierExecutor : public rclcpp::Node
{
public:
  CourierExecutor() : Node("courier_executor")
  {
    locations_ = acadbot_courier::load_locations(this);

    RCLCPP_INFO(get_logger(), "courier_executor started. %zu known location(s):",
                locations_.size());
    for (const auto & [name, loc] : locations_) {
      RCLCPP_INFO(get_logger(), "  %s -> (%.2f, %.2f, yaw=%.2f)",
                  name.c_str(), loc.x, loc.y, loc.yaw);
    }
  }

private:
  std::unordered_map<std::string, acadbot_courier::Location> locations_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CourierExecutor>());
  rclcpp::shutdown();
  return 0;
}
