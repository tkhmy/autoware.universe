// Copyright 2023 TIER IV, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "vehicle.hpp"

namespace default_ad_api
{

using GearReport = vehicle_interface::GearStatus::Message;
using ApiGear = autoware_adapi_v1_msgs::msg::Gear;
using TurnIndicatorsReport = vehicle_interface::TurnIndicatorStatus::Message;
using ApiTurnIndicator = autoware_adapi_v1_msgs::msg::TurnIndicators;
using HazardLightsReport = vehicle_interface::HazardLightStatus::Message;
using ApiHazardLight = autoware_adapi_v1_msgs::msg::HazardLights;
using MapProjectorInfo = map_interface::MapProjectorInfo::Message;

std::unordered_map<uint8_t, uint8_t> gear_type_ = {
  {GearReport::NONE, ApiGear::UNKNOWN},    {GearReport::NEUTRAL, ApiGear::NEUTRAL},
  {GearReport::DRIVE, ApiGear::DRIVE},     {GearReport::DRIVE_2, ApiGear::DRIVE},
  {GearReport::DRIVE_3, ApiGear::DRIVE},   {GearReport::DRIVE_4, ApiGear::DRIVE},
  {GearReport::DRIVE_5, ApiGear::DRIVE},   {GearReport::DRIVE_6, ApiGear::DRIVE},
  {GearReport::DRIVE_7, ApiGear::DRIVE},   {GearReport::DRIVE_8, ApiGear::DRIVE},
  {GearReport::DRIVE_9, ApiGear::DRIVE},   {GearReport::DRIVE_10, ApiGear::DRIVE},
  {GearReport::DRIVE_11, ApiGear::DRIVE},  {GearReport::DRIVE_12, ApiGear::DRIVE},
  {GearReport::DRIVE_13, ApiGear::DRIVE},  {GearReport::DRIVE_14, ApiGear::DRIVE},
  {GearReport::DRIVE_15, ApiGear::DRIVE},  {GearReport::DRIVE_16, ApiGear::DRIVE},
  {GearReport::DRIVE_17, ApiGear::DRIVE},  {GearReport::DRIVE_18, ApiGear::DRIVE},
  {GearReport::REVERSE, ApiGear::REVERSE}, {GearReport::REVERSE_2, ApiGear::REVERSE},
  {GearReport::PARK, ApiGear::PARK},       {GearReport::LOW, ApiGear::LOW},
  {GearReport::LOW_2, ApiGear::LOW},
};

std::unordered_map<uint8_t, uint8_t> turn_indicator_type_ = {
  {TurnIndicatorsReport::DISABLE, ApiTurnIndicator::DISABLE},
  {TurnIndicatorsReport::ENABLE_LEFT, ApiTurnIndicator::LEFT},
  {TurnIndicatorsReport::ENABLE_RIGHT, ApiTurnIndicator::RIGHT},
};

std::unordered_map<uint8_t, uint8_t> hazard_light_type_ = {
  {HazardLightsReport::DISABLE, ApiHazardLight::DISABLE},
  {HazardLightsReport::ENABLE, ApiHazardLight::ENABLE},
};

VehicleNode::VehicleNode(const rclcpp::NodeOptions & options) : Node("vehicle", options)
{
  const auto adaptor = component_interface_utils::NodeAdaptor(this);
  group_cli_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
  adaptor.init_pub(pub_kinematics_);
  adaptor.init_pub(pub_status_);
  adaptor.init_sub(sub_kinematic_state_, this, &VehicleNode::kinematic_state);
  adaptor.init_sub(sub_acceleration_, this, &VehicleNode::acceleration_status);
  adaptor.init_sub(sub_steering_, this, &VehicleNode::steering_status);
  adaptor.init_sub(sub_gear_state_, this, &VehicleNode::gear_status);
  adaptor.init_sub(sub_turn_indicator_, this, &VehicleNode::turn_indicator_status);
  adaptor.init_sub(sub_map_projector_info_, this, &VehicleNode::map_projector_info);
  adaptor.init_sub(sub_hazard_light_, this, &VehicleNode::hazard_light_status);
  adaptor.init_sub(sub_energy_level_, this, &VehicleNode::energy_status);

  const auto rate = rclcpp::Rate(10);
  timer_ = rclcpp::create_timer(this, get_clock(), rate.period(), [this]() { on_timer(); });
}

uint8_t VehicleNode::mapping(
  std::unordered_map<uint8_t, uint8_t> hash_map, uint8_t input, uint8_t default_value)
{
  if (hash_map.find(input) == hash_map.end()) {
    return default_value;
  } else {
    return hash_map[input];
  }
}

void VehicleNode::kinematic_state(
  const localization_interface::KinematicState::Message::ConstSharedPtr msg_ptr)
{
  vehicle_kinematics_.pose.header = msg_ptr->header;
  vehicle_kinematics_.pose.pose = msg_ptr->pose;
  vehicle_kinematics_.twist.header = msg_ptr->header;
  vehicle_kinematics_.twist.header.frame_id = msg_ptr->child_frame_id;
  vehicle_kinematics_.twist.twist = msg_ptr->twist;
  if (map_projector_info_->type == "MGRS") {
    lanelet::GPSPoint projected_gps_point = lanelet::projection::MGRSProjector::reverse(
      toBasicPoint3dPt(msg_ptr->pose.pose.position), map_projector_info_->mgrs_grid);
    vehicle_kinematics_.geographic_pose.header = msg_ptr->header;
    vehicle_kinematics_.geographic_pose.header.frame_id = "global";
    vehicle_kinematics_.geographic_pose.position.latitude = projected_gps_point.lat;
    vehicle_kinematics_.geographic_pose.position.longitude = projected_gps_point.lon;
    vehicle_kinematics_.geographic_pose.position.altitude = projected_gps_point.ele;
  } else if (map_projector_info_->type == "UTM") {
    lanelet::GPSPoint position{
      map_projector_info_->map_origin.latitude, map_projector_info_->map_origin.longitude};
    lanelet::Origin origin{position};
    lanelet::projection::UtmProjector projector{origin};
    lanelet::GPSPoint projected_gps_point =
      projector.reverse(toBasicPoint3dPt(msg_ptr->pose.pose.position));
    vehicle_kinematics_.geographic_pose.header = msg_ptr->header;
    vehicle_kinematics_.geographic_pose.header.frame_id = "global";
    vehicle_kinematics_.geographic_pose.position.latitude = projected_gps_point.lat;
    vehicle_kinematics_.geographic_pose.position.longitude = projected_gps_point.lon;
    vehicle_kinematics_.geographic_pose.position.altitude = projected_gps_point.ele;
  }
}

Eigen::Vector3d VehicleNode::toBasicPoint3dPt(const geometry_msgs::msg::Point src)
{
  Eigen::Vector3d dst;
  dst.x() = src.x;
  dst.y() = src.y;
  dst.z() = src.z;
  return dst;
}

void VehicleNode::acceleration_status(
  const localization_interface::Acceleration::Message::ConstSharedPtr msg_ptr)
{
  vehicle_kinematics_.accel.header = msg_ptr->header;
  vehicle_kinematics_.accel.accel = msg_ptr->accel;
}

void VehicleNode::steering_status(
  const vehicle_interface::SteeringStatus::Message::ConstSharedPtr msg_ptr)
{
  vehicle_status_.steering_tire_angle = msg_ptr->steering_tire_angle;
}

void VehicleNode::gear_status(const GearReport::ConstSharedPtr msg_ptr)
{
  vehicle_status_.gear.status = mapping(gear_type_, msg_ptr->report, ApiGear::UNKNOWN);
}

void VehicleNode::turn_indicator_status(const TurnIndicatorsReport::ConstSharedPtr msg_ptr)
{
  vehicle_status_.turn_indicators.status =
    mapping(turn_indicator_type_, msg_ptr->report, ApiTurnIndicator::UNKNOWN);
}

void VehicleNode::hazard_light_status(const HazardLightsReport::ConstSharedPtr msg_ptr)
{
  vehicle_status_.hazard_lights.status =
    mapping(hazard_light_type_, msg_ptr->report, ApiHazardLight::UNKNOWN);
}

void VehicleNode::map_projector_info(const MapProjectorInfo::ConstSharedPtr msg_ptr)
{
  map_projector_info_ = msg_ptr;
}

void VehicleNode::energy_status(
  const vehicle_interface::EnergyStatus::Message::ConstSharedPtr msg_ptr)
{
  vehicle_status_.energy_percentage = msg_ptr->energy_level;
}

void VehicleNode::on_timer()
{
  vehicle_status_.stamp = now();
  pub_kinematics_->publish(vehicle_kinematics_);
  pub_status_->publish(vehicle_status_);
}

}  // namespace default_ad_api

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(default_ad_api::VehicleNode)
