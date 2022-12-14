#include <iostream>
#include <vdb_mapping_ros/VDBMappingROS.h>

template <typename VDBMappingT>
VDBMappingROS<VDBMappingT>::VDBMappingROS() : Node("vdb_mapping_ros")
{

  this->declare_parameter("resolution", 0.1);
  m_resolution = this->get_parameter("resolution").as_double();

  m_vdb_map = std::make_unique<VDBMappingT>(m_resolution);
  m_tf_buffer =
      std::make_unique<tf2_ros::Buffer>(this->get_clock());
  m_tf_listener_ =
      std::make_shared<tf2_ros::TransformListener>(*m_tf_buffer);

  this->declare_parameter("max_range", 15.0);
  m_config.max_range = this->get_parameter("max_range").as_double();
  this->declare_parameter("prob_hit", 0.7);
  m_config.prob_hit = this->get_parameter("prob_hit").as_double();
  this->declare_parameter("prob_miss", 0.4);
  m_config.prob_miss = this->get_parameter("prob_miss").as_double();
  this->declare_parameter("prob_thres_min", 0.12);
  m_config.prob_thres_min = this->get_parameter("prob_thres_min").as_double();
  this->declare_parameter("prob_thres_max", 0.97);
  m_config.prob_thres_max = this->get_parameter("prob_thres_max").as_double();
  this->declare_parameter("map_save_dir", "");
  m_config.map_directory_path = this->get_parameter("map_save_dir").as_string();

  //   // Configuring the VDB map
  m_vdb_map->setConfig(m_config);

  this->declare_parameter("publish_pointcloud", true);
  m_publish_pointcloud = this->get_parameter("publish_pointcloud").as_bool();
  this->declare_parameter("publish_vis_marker", true);
  m_publish_vis_marker = this->get_parameter("publish_vis_marker").as_bool();
  this->declare_parameter("publish_updates", false);
  m_publish_updates = this->get_parameter("publish_updates").as_bool();
  this->declare_parameter("publish_overwrites", false);
  m_publish_overwrites = this->get_parameter("publish_overwrites").as_bool();
  this->declare_parameter("apply_raw_sensor_data", true);
  m_apply_raw_sensor_data = this->get_parameter("apply_raw_sensor_data").as_bool();

  this->declare_parameter("reduce_data", false);
  m_reduce_data = this->get_parameter("reduce_data").as_bool();

  this->declare_parameter("sensor_frame", "");
  m_sensor_frame = this->get_parameter("sensor_frame").as_string();
  if (m_sensor_frame.empty())
  {
    RCLCPP_WARN_STREAM(this->get_logger(), "No sensor frame specified");
  }

  this->declare_parameter("map_frame", "");
  m_map_frame = this->get_parameter("map_frame").as_string();
  if (m_map_frame.empty())
  {
    RCLCPP_WARN_STREAM(this->get_logger(), "No map frame specified");
  }

  std::string raw_points_topic;
  std::string aligned_points_topic;

  this->declare_parameter("raw_points", "");
  raw_points_topic = this->get_parameter("raw_points").as_string();
  this->declare_parameter("aligned_points", "");
  aligned_points_topic = this->get_parameter("aligned_points").as_string();

  /*
      // Setting up remote sources
      std::vector<std::string> source_ids;
      m_priv_nh.param<std::vector<std::string> >(
        "remote_sources", source_ids, std::vector<std::string>());

      for (auto& source_id : source_ids)
      {
        std::string remote_namespace;
        m_priv_nh.param<std::string>(source_id + "/namespace", remote_namespace, "");

        RemoteSource remote_source;
        m_priv_nh.param<bool>(
          source_id + "/apply_remote_updates", remote_source.apply_remote_updates, false);
        m_priv_nh.param<bool>(
          source_id + "/apply_remote_overwrites", remote_source.apply_remote_overwrites, false);

        if (remote_source.apply_remote_updates)
        {
          remote_source.map_update_sub = m_nh.subscribe(
            remote_namespace + "/vdb_map_updates", 1, &VDBMappingROS::mapUpdateCallback, this);
        }
        if (remote_source.apply_remote_overwrites)
        {
          remote_source.map_overwrite_sub = m_nh.subscribe(
            remote_namespace + "/vdb_map_overwrites", 1, &VDBMappingROS::mapOverwriteCallback, this);
        }
        remote_source.get_map_section_client =
          m_nh.serviceClient<vdb_mapping_msgs::GetMapSection>(remote_namespace + "/get_map_section");
        m_remote_sources.insert(std::make_pair(source_id, remote_source));
      }
  */

  if (m_publish_updates)
  {
    m_map_update_pub = this->create_publisher<std_msgs::msg::String>("vdb_map_updates", 1);
  }
  if (m_publish_overwrites)
  {
    m_map_overwrite_pub = this->create_publisher<std_msgs::msg::String>("vdb_map_overwrites", 1);
  }

  if (m_apply_raw_sensor_data)
  {
    m_sensor_cloud_sub = this->create_subscription<sensor_msgs::msg::PointCloud2>(
        raw_points_topic, 10, std::bind(&VDBMappingROS::sensorCloudCallback, this, _1));
    m_aligned_cloud_sub = this->create_subscription<sensor_msgs::msg::PointCloud2>(
        aligned_points_topic, 10, std::bind(&VDBMappingROS::alignedCloudCallback, this, _1));
  }

  m_visualization_marker_pub =
      this->create_publisher<visualization_msgs::msg::Marker>("vdb_map_visualization", 1);
  m_pointcloud_pub = this->create_publisher<sensor_msgs::msg::PointCloud2>("vdb_map_pointcloud", 1);

  m_map_reset_service =
      this->create_service<std_srvs::srv::Trigger>("vdb_map_reset", std::bind(&VDBMappingROS::mapResetCallback, this, _1, _2));

  m_save_map_service_server =
      this->create_service<std_srvs::srv::Trigger>("save_map", std::bind(&VDBMappingROS::saveMap, this, _1, _2));

  m_load_map_service_server =
      this->create_service<vdb_mapping_msgs::srv::LoadMap>("load_map", std::bind(&VDBMappingROS::loadMap, this, _1, _2));

  m_get_map_section_service =
      this->create_service<vdb_mapping_msgs::srv::GetMapSection>("get_map_section", std::bind(&VDBMappingROS::getMapSectionCallback, this, _1, _2));

  // m_trigger_map_section_update_service =
  //   this->create_service<vdb_mapping_msgs::srv::TriggerMapSectionUpdate>("trigger_map_section_update", std::bind(&VDBMappingROS::triggerMapSectionUpdateCallback, this, _1, _2));
}

template <typename VDBMappingT>
bool VDBMappingROS<VDBMappingT>::mapResetCallback(const std::shared_ptr<std_srvs::srv::Trigger::Request> req,
                                                  std::shared_ptr<std_srvs::srv::Trigger::Response> res)
{
  resetMap();
  res->success = true;
  res->message = "Reset map successful.";
  return true;
}

template <typename VDBMappingT>
void VDBMappingROS<VDBMappingT>::resetMap()
{
  RCLCPP_INFO_STREAM(this->get_logger(), "Reseting Map");
  m_vdb_map->resetMap();
  publishMap();
}

template <typename VDBMappingT>
bool VDBMappingROS<VDBMappingT>::saveMap(const std::shared_ptr<std_srvs::srv::Trigger::Request> req,
                                         std::shared_ptr<std_srvs::srv::Trigger::Response> res)
{
  (void)(*req);
  RCLCPP_INFO_STREAM(this->get_logger(), "Saving Map");
  res->success = m_vdb_map->saveMap();
  return res->success;
}

template <typename VDBMappingT>
bool VDBMappingROS<VDBMappingT>::loadMap(const std::shared_ptr<vdb_mapping_msgs::srv::LoadMap::Request> req,
                                         std::shared_ptr<vdb_mapping_msgs::srv::LoadMap::Response> res)
{
  RCLCPP_INFO_STREAM(this->get_logger(), "Loading Map");
  bool success = m_vdb_map->loadMap(req->path);
  publishMap();
  res->success = success;
  return success;
}

template <typename VDBMappingT>
const typename VDBMappingT::GridT::Ptr VDBMappingROS<VDBMappingT>::getMap()
{
  return m_vdb_map->getMap();
}

template <typename VDBMappingT>
bool VDBMappingROS<VDBMappingT>::getMapSectionCallback(const std::shared_ptr<vdb_mapping_msgs::srv::GetMapSection::Request> req,
                                                       std::shared_ptr<vdb_mapping_msgs::srv::GetMapSection::Response> res)
{
  geometry_msgs::msg::TransformStamped source_to_map_tf;
  try
  {
    source_to_map_tf = m_tf_buffer->lookupTransform(
        m_map_frame, req->header.frame_id, tf2::TimePointZero);
  }
  catch (const tf2::TransformException &ex)
  {
    RCLCPP_ERROR_STREAM(this->get_logger(), "Transform from source to map frame failed: " << ex.what()); 
    res->success = false;
    return true;
  }

  res->map = gridToStr(m_vdb_map->getMapSection(req->bounding_box.min_corner.x,
                                                req->bounding_box.min_corner.y,
                                                req->bounding_box.min_corner.z,
                                                req->bounding_box.max_corner.x,
                                                req->bounding_box.max_corner.y,
                                                req->bounding_box.max_corner.z,
                                                tf2::transformToEigen(source_to_map_tf).matrix()));
  res->success = true;

  return true;
}

/*
template <typename VDBMappingT>
bool VDBMappingROS<VDBMappingT>::triggerMapSectionUpdateCallback(
  vdb_mapping_msgs::TriggerMapSectionUpdate::Request& req,
  vdb_mapping_msgs::TriggerMapSectionUpdate::Response& res)
{
  auto remote_source = m_remote_sources.find(req.remote_source);
  if (remote_source == m_remote_sources.end())
  {
    std::stringstream ss;
    ss << "Key " << req.remote_source << " not found. Available sources are: ";
    for (auto& source : m_remote_sources)
    {
      ss << source.first << ", ";
    }
    ROS_INFO_STREAM(ss.str());
    res.success = false;
    return true;
  }

  vdb_mapping_msgs::GetMapSection srv;
  srv.request.header       = req.header;
  srv.request.bounding_box = req.bounding_box;
  remote_source->second.get_map_section_client.call(srv);

  if (srv.response.success)
  {
    m_vdb_map->overwriteMap(strToGrid(srv.response.map));
    publishMap();
  }

  res.success = srv.response.success;
  return true;
}
*/

template <typename VDBMappingT>
void VDBMappingROS<VDBMappingT>::alignedCloudCallback(const sensor_msgs::msg::PointCloud2::SharedPtr cloud_msg)
{
  typename VDBMappingT::PointCloudT::Ptr cloud(new typename VDBMappingT::PointCloudT);
  pcl::fromROSMsg(*cloud_msg, *cloud);
  geometry_msgs::msg::TransformStamped sensor_to_map_tf;
  try
  {
    // Get sensor origin transform in map coordinates
    sensor_to_map_tf =
        m_tf_buffer->lookupTransform(m_map_frame, m_sensor_frame, cloud_msg->header.stamp);
  }
  catch (const tf2::TransformException &ex)
  {
    RCLCPP_ERROR_STREAM(this->get_logger(), "Transform to map frame failed: " << ex.what());
    return;
  }

  // If aligned map is not already in correct map frame, transform it
  if (m_map_frame != cloud_msg->header.frame_id)
  {
    geometry_msgs::msg::TransformStamped map_to_map_tf;
    try
    {
      map_to_map_tf = m_tf_buffer->lookupTransform(
          m_map_frame, cloud_msg->header.frame_id, cloud_msg->header.stamp);
    }
    catch (const tf2::TransformException &ex)
    {
      RCLCPP_ERROR_STREAM(this->get_logger(), "Transform to map frame failed: " << ex.what());
      return;
    }
    pcl::transformPointCloud(*cloud, *cloud, tf2::transformToEigen(map_to_map_tf).matrix());
    cloud->header.frame_id = m_map_frame;
  }

  insertPointCloud(cloud, sensor_to_map_tf);
}

template <typename VDBMappingT>
void VDBMappingROS<VDBMappingT>::sensorCloudCallback(const sensor_msgs::msg::PointCloud2::SharedPtr cloud_msg)
{
  typename VDBMappingT::PointCloudT::Ptr cloud(new typename VDBMappingT::PointCloudT);
  pcl::fromROSMsg(*cloud_msg, *cloud);

  geometry_msgs::msg::TransformStamped sensor_to_map_tf;
  try
  {
    // Get sensor origin transform in map coordinates
    sensor_to_map_tf =
        m_tf_buffer->lookupTransform(m_map_frame, cloud_msg->header.frame_id, cloud_msg->header.stamp);
  }
  catch (tf2::TransformException &ex)
  {
    RCLCPP_ERROR_STREAM(this->get_logger(), "Transform to map frame failed:" << ex.what());
    return;
  }
  // Transform pointcloud into map reference system
  pcl::transformPointCloud(*cloud, *cloud, tf2::transformToEigen(sensor_to_map_tf).matrix());
  cloud->header.frame_id = m_map_frame;

  insertPointCloud(cloud, sensor_to_map_tf);
}

template <typename VDBMappingT>
void VDBMappingROS<VDBMappingT>::insertPointCloud(
    const typename VDBMappingT::PointCloudT::Ptr cloud,
    const geometry_msgs::msg::TransformStamped transform)
{
  Eigen::Matrix<double, 3, 1> sensor_to_map_eigen = tf2::transformToEigen(transform).translation();
  typename VDBMappingT::UpdateGridT::Ptr update;
  typename VDBMappingT::UpdateGridT::Ptr overwrite;
  // Integrate data into vdb grid
  m_vdb_map->insertPointCloud(cloud, sensor_to_map_eigen, update, overwrite, m_reduce_data);
  if (m_publish_updates)
  {
    m_map_update_pub->publish(gridToMsg(update));
  }
  if (m_publish_overwrites)
  {
    m_map_overwrite_pub->publish(gridToMsg(overwrite));
  }
  publishMap();
}

template <typename VDBMappingT>
std_msgs::msg::String
VDBMappingROS<VDBMappingT>::gridToMsg(const typename VDBMappingT::UpdateGridT::Ptr update) const
{
  std_msgs::msg::String msg;
  msg.data = gridToStr(update);
  return msg;
}

template <typename VDBMappingT>
std::string
VDBMappingROS<VDBMappingT>::gridToStr(const typename VDBMappingT::UpdateGridT::Ptr update) const
{
  openvdb::GridPtrVec grids;
  grids.push_back(update);
  std::ostringstream oss(std::ios_base::binary);
  openvdb::io::Stream(oss).write(grids);
  return oss.str();
}

template <typename VDBMappingT>
typename VDBMappingT::UpdateGridT::Ptr
VDBMappingROS<VDBMappingT>::msgToGrid(const std_msgs::msg::String::SharedPtr msg) const
{
  return strToGrid(msg->data);
}

template <typename VDBMappingT>
typename VDBMappingT::UpdateGridT::Ptr
VDBMappingROS<VDBMappingT>::strToGrid(const std::string &msg) const
{
  std::istringstream iss(msg);
  openvdb::io::Stream strm(iss);
  openvdb::GridPtrVecPtr grids;
  grids = strm.getGrids();
  // This cast might fail if different VDB versions are used.
  // Corresponding error messages are generated by VDB directly
  typename VDBMappingT::UpdateGridT::Ptr update_grid =
      openvdb::gridPtrCast<typename VDBMappingT::UpdateGridT>(grids->front());
  return update_grid;
}

template <typename VDBMappingT>
void VDBMappingROS<VDBMappingT>::mapUpdateCallback(const std_msgs::msg::String::SharedPtr update_msg)
{
  if (!m_reduce_data)
  {
    m_vdb_map->updateMap(msgToGrid(update_msg));
  }
  else
  {
    m_vdb_map->updateMap(m_vdb_map->raycastUpdateGrid(msgToGrid(update_msg)));
  }
  publishMap();
}

template <typename VDBMappingT>
void VDBMappingROS<VDBMappingT>::mapOverwriteCallback(const std_msgs::msg::String::SharedPtr update_msg)
{
  m_vdb_map->overwriteMap(msgToGrid(update_msg));
  publishMap();
}

template <typename VDBMappingT>
void VDBMappingROS<VDBMappingT>::publishMap() const
{
  if (!(m_publish_pointcloud || m_publish_vis_marker))
  {
    return;
  }

  typename VDBMappingT::GridT::Ptr grid = m_vdb_map->getMap();

  bool publish_vis_marker;
  publish_vis_marker = (m_publish_vis_marker && m_visualization_marker_pub->get_subscription_count() > 0);
  bool publish_pointcloud;
  publish_pointcloud = (m_publish_pointcloud && m_pointcloud_pub->get_subscription_count() > 0);

  visualization_msgs::msg::Marker visualization_marker_msg;
  sensor_msgs::msg::PointCloud2 cloud_msg;

  VDBMappingTools<VDBMappingT>::createMappingOutput(m_vdb_map->getMap(),
                                                    m_map_frame,
                                                    visualization_marker_msg,
                                                    cloud_msg,
                                                    publish_vis_marker,
                                                    publish_pointcloud,
                                                    m_lower_visualization_z_limit,
                                                    m_upper_visualization_z_limit,
                                                    *(this->get_clock()));

  if (publish_vis_marker)
  {
    m_visualization_marker_pub->publish(visualization_marker_msg);
  }
  if (publish_pointcloud)
  {
    m_pointcloud_pub->publish(cloud_msg);
  }
}
