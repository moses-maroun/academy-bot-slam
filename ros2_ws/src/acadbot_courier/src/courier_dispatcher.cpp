#include <chrono>

#include "rclcpp/rclcpp.hpp"
#include "acadbot_courier_msgs/srv/request_delivery.hpp"
#include "acadbot_courier_msgs/msg/courier_job.hpp"
#include "acadbot_courier_msgs/msg/courier_job_status.hpp"

#include "acadbot_courier/locations.hpp"

using namespace std::chrono_literals;
using RequestDelivery = acadbot_courier_msgs::srv::RequestDelivery;
using CourierJob = acadbot_courier_msgs::msg::CourierJob;
using CourierJobStatus = acadbot_courier_msgs::msg::CourierJobStatus;

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

    pending_job_timeout_ = declare_parameter<double>("pending_job_timeout", 30.0);

    request_delivery_srv_ = create_service<RequestDelivery>(
      "request_delivery",
      std::bind(&CourierDispatcher::handle_request_delivery, this,
                std::placeholders::_1, std::placeholders::_2));

    job_accepted_pub_ = create_publisher<CourierJob>("/courier/job_accepted", 10);

    job_status_sub_ = create_subscription<CourierJobStatus>(
      "/courier/job_status", 10,
      std::bind(&CourierDispatcher::handle_job_status, this, std::placeholders::_1));

    pending_check_timer_ = create_wall_timer(
      1s, std::bind(&CourierDispatcher::check_pending_job_timeout, this));
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
    if (busy_) {
      response->accepted = false;
      response->reason = "robot busy with " + current_job_id_;
      RCLCPP_WARN(get_logger(), "Rejected request: %s", response->reason.c_str());
      return;
    }

    const std::string job_id = "job_" + std::to_string(next_job_id_++);

    CourierJob job;
    job.job_id = job_id;
    job.pickup = request->pickup;
    job.dropoff = request->dropoff;
    job_accepted_pub_->publish(job);

    busy_ = true;
    current_job_id_ = job_id;
    current_job_started_ = false;
    accepted_at_ = now();

    response->accepted = true;
    response->job_id = job_id;
    RCLCPP_INFO(get_logger(), "Accepted request %s: %s -> %s",
                job_id.c_str(), request->pickup.c_str(), request->dropoff.c_str());
  }

  void handle_job_status(const CourierJobStatus::SharedPtr msg)
  {
    if (!busy_ || msg->job_id != current_job_id_) {
      return;
    }
    if (msg->state == "RUNNING") {
      current_job_started_ = true;
      return;
    }
    if (msg->state == "SUCCEEDED" || msg->state == "FAILED" || msg->state == "CANCELED") {
      RCLCPP_INFO(get_logger(), "%s finished: %s", current_job_id_.c_str(), msg->state.c_str());
      busy_ = false;
      current_job_id_.clear();
      current_job_started_ = false;
    }
  }

  void check_pending_job_timeout()
  {
    if (!busy_ || current_job_started_) {
      return;
    }
    if ((now() - accepted_at_).seconds() < pending_job_timeout_) {
      return;
    }
    RCLCPP_WARN(get_logger(),
                "%s was accepted but never claimed within %.1fs — freeing the robot.",
                current_job_id_.c_str(), pending_job_timeout_);
    busy_ = false;
    current_job_id_.clear();
  }

  std::unordered_map<std::string, acadbot_courier::Location> locations_;
  rclcpp::Service<RequestDelivery>::SharedPtr request_delivery_srv_;
  rclcpp::Publisher<CourierJob>::SharedPtr job_accepted_pub_;
  rclcpp::Subscription<CourierJobStatus>::SharedPtr job_status_sub_;
  rclcpp::TimerBase::SharedPtr pending_check_timer_;
  uint64_t next_job_id_{1};
  double pending_job_timeout_{30.0};
  bool busy_{false};
  bool current_job_started_{false};
  std::string current_job_id_;
  rclcpp::Time accepted_at_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CourierDispatcher>());
  rclcpp::shutdown();
  return 0;
}
