#include <ros/ros.h>
#include <iostream>
#include <cstdio>
#include <unistd.h>
#include <signal.h>
#include <uavcan/uavcan.hpp>

#include <uavcan/protocol/global_time_sync_master.hpp>
#include <uavcan/protocol/global_time_sync_slave.hpp>

extern uavcan::ICanDriver& getCanDriver(const std::string&);
extern uavcan::ISystemClock& getSystemClock();

constexpr unsigned NodeMemoryPoolSize = 16384;
typedef uavcan::Node<NodeMemoryPoolSize> Node;

static Node& getNode(const std::string& can_interface)
{
    static Node node(getCanDriver(can_interface), getSystemClock());
    return node;
}

int main(int argc, char** argv)
{
    ros::init(argc, argv, "uavcan_time_master_node");
    ros::NodeHandle ros_node;

    int self_node_id, clock_period_msec;
    std::string can_interface;
    ros::param::param<int>("~uav_node_id", self_node_id, 115);
    ros::param::param<std::string>("~uav_can_interface", can_interface, "can0");
    ros::param::param<int>("~clock_period_msec", clock_period_msec, 1000);

    auto& uav_node = getNode(can_interface);
    uav_node.setNodeID(self_node_id);
    uav_node.setName("smarc.sam.uavcan_bridge.time_master");

    const int node_start_res = uav_node.start();
    if (node_start_res < 0) {
        ROS_ERROR("Failed to start the node; error: %d", node_start_res);
        exit(0);
    }

    /*
     * Initializing the time sync master object.
     * No more than one time sync master can exist per node.
     */
    uavcan::GlobalTimeSyncMaster master(uav_node);
    const int master_init_res = master.init();
    if (master_init_res < 0) {
        ROS_ERROR("Failed to start the time sync master; error: %d", master_init_res);
        exit(0);
    }

    /*
     * Create a timer to publish the time sync message once a second.
     * Note that in real applications the logic governing time sync master can be even more complex,
     * i.e. if the local time source is not always available (like in GNSS receivers).
     */
    uavcan::Timer master_timer(uav_node);
    master_timer.setCallback([&](const uavcan::TimerEvent&) {
        /*
         * Publish the sync message now, even if we're not a higher priority master.
         * Other nodes will be able to pick the right master anyway.
         */
        const int res = master.publish();
        if (res < 0) {
            ROS_ERROR("Time sync master transient failure: %d", res);
        }
    });

    master_timer.startPeriodic(uavcan::MonotonicDuration::fromMSec(clock_period_msec));

    /*
     * Running the node.
     */
    uav_node.setModeOperational();
    signal(SIGINT, [] (int) { ros::shutdown(); });

    while (ros::ok()) {
        /*
         * The method spin() may return earlier if an error occurs (e.g. driver failure).
         * All error codes are listed in the header uavcan/error.hpp.
         */
        const int res = uav_node.spin(uavcan::MonotonicDuration::getInfinite());
        if (res < 0) {
            ROS_ERROR("Transient failure or shutdown: %d", res);
        }
    }

    // while (true) {
    //     const int spin_res = uav_node.spin(uavcan::MonotonicDuration::getInfinite());
    //     if (spin_res < 0) {
    //         ROS_ERROR("Transient failure or shutdown: %d", spin_res);
    //     }
    // }

    return 0;
}
