#include "rclcpp/rclcpp.hpp"
#include "acadbot_courier_msgs/srv/request_delivery.hpp"

#include "acadbot_courier/locations.hpp"

using RequestDelivery = acadbot_courier_msgs::srv::RequestDelivery;

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

    request_delivery_srv_ = create_service<RequestDelivery>(
      "request_delivery",
      std::bind(&CourierDispatcher::handle_request_delivery, this,
                std::placeholders::_1, std::placeholders::_2));
  }

private:
  void handle_request_delivery(
    const std::shared_ptr<RequestDelivery::Request> request,
    std::shared_ptr<RequestDelivery::Response> response)
  {
    if (locations_.find(request->pickup) == locations_.end()) {
      response->accepted = false;
      response->reason = "unknown pickup location '" + request->pickup + "'";
      RCLCPP_WARN(get_logger(), "Rejected request: %s", response->reason.c_str());
      return;
    }
    if (locations_.find(request->dropoff) == locations_.end()) {
      response->accepted = false;
      response->reason = "unknown dropoff location '" + request->dropoff + "'";
      RCLCPP_WARN(get_logger(), "Rejected request: %s", response->reason.c_str());
      return;
    }

    response->accepted = true;
    RCLCPP_INFO(get_logger(), "Accepted request: %s -> %s",
                request->pickup.c_str(), request->dropoff.c_str());
  }

  std::unordered_map<std::string, acadbot_courier::Location> locations_;
  rclcpp::Service<RequestDelivery>::SharedPtr request_delivery_srv_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CourierDispatcher>());
  rclcpp::shutdown();
  return 0;
}
