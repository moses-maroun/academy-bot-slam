#include "rclcpp/rclcpp.hpp"
#include "acadbot_courier_msgs/msg/courier_job.hpp"

#include "acadbot_courier/locations.hpp"

using CourierJob = acadbot_courier_msgs::msg::CourierJob;

struct PendingJob
{
  std::string pickup;
  std::string dropoff;
};

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

    job_accepted_sub_ = create_subscription<CourierJob>(
      "/courier/job_accepted", 10,
      std::bind(&CourierExecutor::handle_job_accepted, this, std::placeholders::_1));
  }

private:
  void handle_job_accepted(const CourierJob::SharedPtr msg)
  {
    pending_jobs_[msg->job_id] = PendingJob{msg->pickup, msg->dropoff};
    RCLCPP_INFO(get_logger(), "Queued %s: %s -> %s (%zu pending)",
                msg->job_id.c_str(), msg->pickup.c_str(), msg->dropoff.c_str(),
                pending_jobs_.size());
  }

  std::unordered_map<std::string, acadbot_courier::Location> locations_;
  std::unordered_map<std::string, PendingJob> pending_jobs_;
  rclcpp::Subscription<CourierJob>::SharedPtr job_accepted_sub_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CourierExecutor>());
  rclcpp::shutdown();
  return 0;
}
