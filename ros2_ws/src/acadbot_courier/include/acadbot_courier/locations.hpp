#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "rclcpp/rclcpp.hpp"

namespace acadbot_courier
{

struct Location
{
  double x{0.0};
  double y{0.0};
  double yaw{0.0};
};

// Reads `location_names` plus a `locations.<name>.x/.y/.yaw` triple per name
// from the node's parameters (see config/locations.yaml). Declared this way,
// rather than as a fixed message type, so any node loading the same YAML
// ends up with an identical name -> pose table with no coordinates compiled
// into either node.
inline std::unordered_map<std::string, Location> load_locations(rclcpp::Node * node)
{
  const auto names = node->declare_parameter<std::vector<std::string>>(
    "location_names", std::vector<std::string>{});

  std::unordered_map<std::string, Location> locations;
  for (const auto & name : names) {
    Location loc;
    loc.x = node->declare_parameter<double>("locations." + name + ".x", 0.0);
    loc.y = node->declare_parameter<double>("locations." + name + ".y", 0.0);
    loc.yaw = node->declare_parameter<double>("locations." + name + ".yaw", 0.0);
    locations[name] = loc;
  }
  return locations;
}

}  // namespace acadbot_courier
