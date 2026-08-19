#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "acadbot_courier_msgs/msg/courier_job.hpp"
#include "acadbot_courier_msgs/msg/courier_job_status.hpp"
#include "acadbot_courier_msgs/action/deliver_package.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "tf2/LinearMath/Quaternion.h"

#include "acadbot_courier/locations.hpp"

using namespace std::chrono_literals;
using CourierJob = acadbot_courier_msgs::msg::CourierJob;
using CourierJobStatus = acadbot_courier_msgs::msg::CourierJobStatus;
using DeliverPackage = acadbot_courier_msgs::action::DeliverPackage;
using GoalHandleDeliverPackage = rclcpp_action::ServerGoalHandle<DeliverPackage>;
using NavigateToPose = nav2_msgs::action::NavigateToPose;
using NavGoalHandle = rclcpp_action::ClientGoalHandle<NavigateToPose>;

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
    frame_id_ = declare_parameter<std::string>("frame_id", "map");

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

    nav_client_ = rclcpp_action::create_client<NavigateToPose>(this, "navigate_to_pose");

    feedback_timer_ = create_wall_timer(
      1s, std::bind(&CourierExecutor::publish_feedback, this));
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
    const PendingJob job = pending_jobs_.at(job_id);
    pending_jobs_.erase(job_id);

    active_job_id_ = job_id;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      active_goal_handle_ = goal_handle;
    }
    publish_status(job_id, "RUNNING", "");

    auto result = std::make_shared<DeliverPackage::Result>();

    if (!nav_client_->wait_for_action_server(10s)) {
      result->success = false;
      result->final_leg = "to_pickup";
      result->message = "Nav2 navigate_to_pose action server not available";
      goal_handle->abort(result);
      finish_job(job_id, "FAILED", result->message);
      return;
    }

    const bool reached_pickup =
      navigate_to("to_pickup", job.pickup, locations_.at(job.pickup));

    if (!reached_pickup) {
      result->success = false;
      result->final_leg = "to_pickup";
      result->message = "failed to reach pickup '" + job.pickup + "'";
      goal_handle->abort(result);
      finish_job(job_id, "FAILED", result->message);
      return;
    }

    result->success = true;
    result->final_leg = "";
    result->message = "reached pickup '" + job.pickup + "' (dropoff leg not implemented yet)";
    goal_handle->succeed(result);
    finish_job(job_id, "SUCCEEDED", result->message);
  }

  // Sends one navigate_to_pose goal and blocks (on execute()'s own thread —
  // see handle_accepted) until Nav2 returns a result, a rejection, or the
  // goal fails to even get an accept/reject response. The main executor
  // thread keeps spinning underneath this call, since it's what actually
  // delivers the Nav2 client's callbacks that wake this wait up.
  bool navigate_to(const std::string & leg, const std::string & heading_to,
                    const acadbot_courier::Location & pose)
  {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      current_leg_ = leg;
      current_heading_to_ = heading_to;
      current_distance_remaining_ = 0.0f;
      current_attempt_ = 1;
      nav_done_ = false;
    }

    NavigateToPose::Goal goal;
    goal.pose.header.frame_id = frame_id_;
    goal.pose.header.stamp = now();
    goal.pose.pose.position.x = pose.x;
    goal.pose.pose.position.y = pose.y;

    tf2::Quaternion q;
    q.setRPY(0, 0, pose.yaw);
    goal.pose.pose.orientation.x = q.x();
    goal.pose.pose.orientation.y = q.y();
    goal.pose.pose.orientation.z = q.z();
    goal.pose.pose.orientation.w = q.w();

    RCLCPP_INFO(get_logger(), "  [%s] navigating to '%s' (%.2f, %.2f, yaw=%.2f)",
                leg.c_str(), heading_to.c_str(), pose.x, pose.y, pose.yaw);

    rclcpp_action::Client<NavigateToPose>::SendGoalOptions opts;
    opts.goal_response_callback =
      [this](NavGoalHandle::SharedPtr gh) {
        if (!gh) {
          std::lock_guard<std::mutex> lock(state_mutex_);
          nav_result_code_ = rclcpp_action::ResultCode::ABORTED;
          nav_done_ = true;
          nav_cv_.notify_all();
        }
      };
    opts.feedback_callback =
      [this](NavGoalHandle::SharedPtr,
             const std::shared_ptr<const NavigateToPose::Feedback> fb) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_distance_remaining_ = fb->distance_remaining;
      };
    opts.result_callback =
      [this](const NavGoalHandle::WrappedResult & result) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        nav_result_code_ = result.code;
        nav_done_ = true;
        nav_cv_.notify_all();
      };

    nav_client_->async_send_goal(goal, opts);

    std::unique_lock<std::mutex> lock(state_mutex_);
    nav_cv_.wait(lock, [this] { return nav_done_; });
    return nav_result_code_ == rclcpp_action::ResultCode::SUCCEEDED;
  }

  void finish_job(const std::string & job_id, const std::string & state,
                   const std::string & detail)
  {
    publish_status(job_id, state, detail);
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      active_goal_handle_.reset();
    }
    active_job_id_.clear();
  }

  void publish_feedback()
  {
    std::shared_ptr<GoalHandleDeliverPackage> gh;
    auto fb = std::make_shared<DeliverPackage::Feedback>();
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!active_goal_handle_) {
        return;
      }
      gh = active_goal_handle_;
      fb->leg = current_leg_;
      fb->heading_to = current_heading_to_;
      fb->distance_remaining = current_distance_remaining_;
      fb->attempt = current_attempt_;
    }
    gh->publish_feedback(fb);
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
  std::string frame_id_;
  rclcpp::Subscription<CourierJob>::SharedPtr job_accepted_sub_;
  rclcpp::Publisher<CourierJobStatus>::SharedPtr job_status_pub_;
  rclcpp_action::Server<DeliverPackage>::SharedPtr deliver_package_srv_;
  rclcpp_action::Client<NavigateToPose>::SharedPtr nav_client_;
  rclcpp::TimerBase::SharedPtr feedback_timer_;
  std::string active_job_id_;

  std::mutex state_mutex_;
  std::shared_ptr<GoalHandleDeliverPackage> active_goal_handle_;
  std::string current_leg_;
  std::string current_heading_to_;
  float current_distance_remaining_{0.0f};
  uint32_t current_attempt_{0};
  bool nav_done_{false};
  rclcpp_action::ResultCode nav_result_code_{rclcpp_action::ResultCode::UNKNOWN};
  std::condition_variable nav_cv_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CourierExecutor>());
  rclcpp::shutdown();
  return 0;
}
