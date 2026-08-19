#include <memory>
#include <string>
#include <thread>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "acadbot_courier_msgs/msg/courier_job.hpp"
#include "acadbot_courier_msgs/msg/courier_job_status.hpp"
#include "acadbot_courier_msgs/action/deliver_package.hpp"

#include "acadbot_courier/locations.hpp"

using CourierJob = acadbot_courier_msgs::msg::CourierJob;
using CourierJobStatus = acadbot_courier_msgs::msg::CourierJobStatus;
using DeliverPackage = acadbot_courier_msgs::action::DeliverPackage;
using GoalHandleDeliverPackage = rclcpp_action::ServerGoalHandle<DeliverPackage>;

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

    job_status_pub_ = create_publisher<CourierJobStatus>("/courier/job_status", 10);

    deliver_package_srv_ = rclcpp_action::create_server<DeliverPackage>(
      this,
      "deliver_package",
      std::bind(&CourierExecutor::handle_goal, this,
                std::placeholders::_1, std::placeholders::_2),
      std::bind(&CourierExecutor::handle_cancel, this, std::placeholders::_1),
      std::bind(&CourierExecutor::handle_accepted, this, std::placeholders::_1));
  }

private:
  void handle_job_accepted(const CourierJob::SharedPtr msg)
  {
    pending_jobs_[msg->job_id] = PendingJob{msg->pickup, msg->dropoff};
    RCLCPP_INFO(get_logger(), "Queued %s: %s -> %s (%zu pending)",
                msg->job_id.c_str(), msg->pickup.c_str(), msg->dropoff.c_str(),
                pending_jobs_.size());
  }

  rclcpp_action::GoalResponse handle_goal(
    const rclcpp_action::GoalUUID &,
    std::shared_ptr<const DeliverPackage::Goal> goal)
  {
    if (!active_job_id_.empty()) {
      RCLCPP_WARN(get_logger(), "Rejecting goal %s: already executing %s",
                  goal->job_id.c_str(), active_job_id_.c_str());
      return rclcpp_action::GoalResponse::REJECT;
    }
    if (pending_jobs_.find(goal->job_id) == pending_jobs_.end()) {
      RCLCPP_WARN(get_logger(), "Rejecting goal %s: not a known accepted job",
                  goal->job_id.c_str());
      return rclcpp_action::GoalResponse::REJECT;
    }
    return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  }

  rclcpp_action::CancelResponse handle_cancel(
    const std::shared_ptr<GoalHandleDeliverPackage>)
  {
    return rclcpp_action::CancelResponse::ACCEPT;
  }

  void handle_accepted(const std::shared_ptr<GoalHandleDeliverPackage> goal_handle)
  {
    std::thread{&CourierExecutor::execute, this, goal_handle}.detach();
  }

  void execute(const std::shared_ptr<GoalHandleDeliverPackage> goal_handle)
  {
    const std::string job_id = goal_handle->get_goal()->job_id;

    pending_jobs_.erase(job_id);
    active_job_id_ = job_id;
    publish_status(job_id, "RUNNING", "");

    auto result = std::make_shared<DeliverPackage::Result>();
    result->success = true;
    result->final_leg = "";
    result->message = "delivered (stub)";
    goal_handle->succeed(result);

    publish_status(job_id, "SUCCEEDED", "");
    active_job_id_.clear();
  }

  void publish_status(const std::string & job_id, const std::string & state,
                       const std::string & detail)
  {
    CourierJobStatus status;
    status.job_id = job_id;
    status.state = state;
    status.detail = detail;
    job_status_pub_->publish(status);
  }

  std::unordered_map<std::string, acadbot_courier::Location> locations_;
  std::unordered_map<std::string, PendingJob> pending_jobs_;
  rclcpp::Subscription<CourierJob>::SharedPtr job_accepted_sub_;
  rclcpp::Publisher<CourierJobStatus>::SharedPtr job_status_pub_;
  rclcpp_action::Server<DeliverPackage>::SharedPtr deliver_package_srv_;
  std::string active_job_id_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CourierExecutor>());
  rclcpp::shutdown();
  return 0;
}
