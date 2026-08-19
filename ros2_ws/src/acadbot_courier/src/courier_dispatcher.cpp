#include "rclcpp/rclcpp.hpp"
#include "acadbot_courier_msgs/srv/request_delivery.hpp"
#include "acadbot_courier_msgs/msg/courier_job.hpp"

#include "acadbot_courier/locations.hpp"

using RequestDelivery = acadbot_courier_msgs::srv::RequestDelivery;
using CourierJob = acadbot_courier_msgs::msg::CourierJob;

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

    job_accepted_pub_ = create_publisher<CourierJob>("/courier/job_accepted", 10);
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

    const std::string job_id = "job_" + std::to_string(next_job_id_++);

    CourierJob job;
    job.job_id = job_id;
    job.pickup = request->pickup;
    job.dropoff = request->dropoff;
    job_accepted_pub_->publish(job);

    response->accepted = true;
    response->job_id = job_id;
    RCLCPP_INFO(get_logger(), "Accepted request %s: %s -> %s",
                job_id.c_str(), request->pickup.c_str(), request->dropoff.c_str());
  }

  std::unordered_map<std::string, acadbot_courier::Location> locations_;
  rclcpp::Service<RequestDelivery>::SharedPtr request_delivery_srv_;
  rclcpp::Publisher<CourierJob>::SharedPtr job_accepted_pub_;
  uint64_t next_job_id_{1};
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CourierDispatcher>());
  rclcpp::shutdown();
  return 0;
}
