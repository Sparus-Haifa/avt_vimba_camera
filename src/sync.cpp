/// Copyright (c) 2014,
/// Systems, Robotics and Vision Group
/// University of the Balearic Islands
/// All rights reserved.
///
/// Redistribution and use in source and binary forms, with or without
/// modification, are permitted provided that the following conditions are met:
///     * Redistributions of source code must retain the above copyright
///       notice, this list of conditions and the following disclaimer.
///     * Redistributions in binary form must reproduce the above copyright
///       notice, this list of conditions and the following disclaimer in the
///       documentation and/or other materials provided with the distribution.
///     * All advertising materials mentioning features or use of this software
///       must display the following acknowledgement:
///       This product includes software developed by
///       Systems, Robotics and Vision Group, Univ. of the Balearic Islands
///     * Neither the name of Systems, Robotics and Vision Group, University of
///       the Balearic Islands nor the names of its contributors may be used
///       to endorse or promote products derived from this software without
///       specific prior written permission.
///
/// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
/// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
/// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
/// ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
/// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
/// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
/// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
/// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
/// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
/// THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <avt_vimba_camera/sync.h>
#include <stdlib.h>

namespace avt_vimba_camera {

Sync::Sync(ros::NodeHandle nh, ros::NodeHandle nhp): nh_(nh), nhp_(nhp), init_(false), is_resetting_(false), lock_timer_(false), it_(nh)
{
  // Read params
  nhp_.param("camera", camera_, string("/stereo_down"));
  nhp_.param("camera_node_name", camera_node_name_, string("stereo_down"));
  nhp_.param("desired_freq", desired_freq_, 7.5);
  nhp_.param("reset_wait_time", reset_wait_time_, 20.0);
}

void Sync::run()
{
  // Create the approximate sync subscriber
  image_transport::SubscriberFilter left_sub, right_sub;
  message_filters::Subscriber<sensor_msgs::CameraInfo> left_info_sub, right_info_sub;

  left_sub      .subscribe(it_, camera_+"_unsync/left/image_raw", 5);
  right_sub     .subscribe(it_, camera_+"_unsync/right/image_raw", 5);
  left_info_sub .subscribe(nh_, camera_+"_unsync/left/camera_info",  5);
  right_info_sub.subscribe(nh_, camera_+"_unsync/right/camera_info", 5);

  boost::shared_ptr<SyncType> sync_var;
  sync_var.reset(new SyncType(SyncPolicy(5), left_sub, right_sub, left_info_sub, right_info_sub) );
  sync_var->registerCallback(bind(&Sync::msgsCallback, this, _1, _2, _3, _4));

  // Sync timer
  sync_timer_ = nh_.createTimer(ros::Duration(1.0/desired_freq_), &Sync::syncCallback, this);

  // Republish cameras
  left_pub_  = it_.advertiseCamera(camera_+"/left/image_raw",  1);
  right_pub_ = it_.advertiseCamera(camera_+"/right/image_raw", 1);

  // Publish info
  pub_info_ = nhp_.advertise<std_msgs::String>("info", 1, true);

  ros::spin();
}

void Sync::msgsCallback(const sensor_msgs::ImageConstPtr& l_img_msg,
                        const sensor_msgs::ImageConstPtr& r_img_msg,
                        const sensor_msgs::CameraInfoConstPtr& l_info_msg,
                        const sensor_msgs::CameraInfoConstPtr& r_info_msg)
{
  if (!init_)
    ROS_INFO("[SyncNode]: Initialized.");
  init_ = true;

  // Check time sync
  double l_time = l_img_msg->header.stamp.toSec();
  double r_time = r_img_msg->header.stamp.toSec();
  double time_error = fabs(l_time - r_time);
  if (time_error > 0.1)
  {
    ROS_WARN_STREAM("[SyncNode]: Left and right images not properly synced (e=" << time_error << "s.)");
  }
  else
  {
    // Set the same time
    ros::Time now = ros::Time::now();
    sensor_msgs::Image l_img = *l_img_msg;
    sensor_msgs::Image r_img = *r_img_msg;
    sensor_msgs::CameraInfo l_info = *l_info_msg;
    sensor_msgs::CameraInfo r_info = *r_info_msg;

    l_info.header.stamp = now;
    l_img.header.stamp = now;
    r_info.header.stamp = now;
    r_img.header.stamp = now;
    left_pub_.publish(l_img, l_info);
    right_pub_.publish(r_img, r_info);
  }

  last_wall_sync_ = ros::WallTime::now().toSec();
  last_ros_sync_ = ros::Time::now().toSec();
}

void Sync::syncCallback(const ros::TimerEvent&)
{
  if (!init_) return;
  if (lock_timer_) return;
  lock_timer_ = true;

  double now = ros::Time::now().toSec();
  double wall_now = ros::WallTime::now().toSec();

  // Exit if resetting...
  if (is_resetting_)
  {
    if (now - reset_time_ < reset_wait_time_)
    {
      lock_timer_ = false;
      return;
    }
    else
      is_resetting_ = false;
  }

  // Check desired frequency
  if (now - last_ros_sync_ > 40.0/desired_freq_)
  {
    // No sync!
    ROS_WARN_STREAM("[SyncNode]: No sync during " << now - last_ros_sync_ << " sec. Reseting driver...");

    // Publish info
    std_msgs::String msg;
    msg.data = "Reseting camera driver at ROSTIME: " +
               boost::lexical_cast<string>(now) + "s. (ROSWALLTIME: " +
               boost::lexical_cast<string>(wall_now) + "s.).";
    pub_info_.publish(msg);

    // Restart driver
    if (ros::ok())
    {
      string cmd_kill = "rosnode kill " + camera_node_name_;
      system(cmd_kill.c_str());
      ros::WallDuration(5.0).sleep();
      string cmd_launch = "roslaunch turbot avt_vimba_camera.launch &";
      system(cmd_launch.c_str());
      ros::WallDuration(5.0).sleep();
    }

    init_ = false;
    reset_time_ = now;
    is_resetting_ = true;
  }

  lock_timer_ = false;
}


};