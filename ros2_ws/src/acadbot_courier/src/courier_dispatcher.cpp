#include "rclcpp/rclcpp.hpp"

#include "acadbot_courier/locations.hpp"

class CourierDispatcher : public rclcpp::Node
{
public:
  CourierDispatcher() : Node("courier_dispatcher")
  {
    locations_ = acadbot_courier::load_locations(this);

    RCLCPP_INFO(get_logger(), "courier_dispatcher started. %zu known location(s):",
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
  rclcpp::spin(std::make_shared<CourierDispatcher>());
  rclcpp::shutdown();
  return 0;
}
