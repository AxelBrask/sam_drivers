#include <rclcpp/rclcpp.hpp>
#include <uavcan_ros_bridge.h>
#include <std_srvs/srv/set_bool.hpp>
#include <time_utils.h>
#include <uavcan_ros_msgs/msg/uavcan_node_status.hpp>
#include <uavcan_ros_msgs/srv/uavcan_get_node_info.hpp>
#include <uavcan_node_info.h>
// Include necessary UAVCAN and ROS service message types here
// For example:
// #include <uavcan/protocol/param/GetSet.h>
// #include <your_ros_package/srv/your_service.hpp>
DEFINE_HANDLER_LIST_HEADS();
DEFINE_TRANSFER_OBJECT_HEADS();
class ServiceConversionBridge : public rclcpp::Node
{
public:
    ServiceConversionBridge() : Node("service_conversion_bridge_node")
    {
        self_node_id_ = this->declare_parameter<int>("uav_node_id", 115);
        can_interface_ = this->declare_parameter<std::string>("uav_can_interface", "vcan0");
    }

    void start_node(const char *can_interface_, uint8_t node_id);
    void start_canard_node();

private:
    void handle_GetNodeInfo(const CanardRxTransfer& transfer, const uavcan_protocol_GetNodeInfoRequest& req);
    Canard::ObjCallback<ServiceConversionBridge, uavcan_protocol_GetNodeInfoRequest> node_info_req_cb{this, &ServiceConversionBridge::handle_GetNodeInfo};
    Canard::Server<uavcan_protocol_GetNodeInfoRequest> node_info_server{canard_interface, node_info_req_cb};
    void send_NodeStatus(void);
    int self_node_id_;
    rclcpp::TimerBase::SharedPtr timer_;
    std::string can_interface_;
    uavcan_protocol_NodeStatus msg;
    CanardInterface canard_interface{0};
    Canard::Publisher<uavcan_protocol_NodeStatus> node_status_pub{canard_interface};

    // Declare your service conversion servers here
    // For example:
    // std::unique_ptr<ros_to_uav::ServiceConversionServer<uavcan::protocol::param::GetSet, your_ros_package::srv::YourService>> param_get_set_server_;
    std::unique_ptr<ros_to_uav::ServiceConversionServer<uavcan_protocol_GetNodeInfoRequest,uavcan_protocol_GetNodeInfoResponse, uavcan_ros_msgs::srv::UavcanGetNodeInfo>> node_info_server_service_;
    void setup_service_servers();
};


//Function to send NodeStatus to the Dronecan Gui
void ServiceConversionBridge::send_NodeStatus(void)
 {
    msg.uptime_sec = millis32() / 1000UL;
    msg.health = UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK;
    msg.mode = UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
    msg.sub_mode = 0;
    
    node_status_pub.broadcast(msg);
}


static void getUniqueID(uint8_t id[16])
{
    memset(id, 0, 16);
    FILE *f = fopen("/etc/machine-id", "r");
    if (f) {
        fread(id, 1, 16, f);
        fclose(f);
    }
}
void ServiceConversionBridge::handle_GetNodeInfo(const CanardRxTransfer& transfer, const uavcan_protocol_GetNodeInfoRequest& req) {
    uavcan_protocol_GetNodeInfoResponse response{};
    
    response.name.len = snprintf((char*)response.name.data, sizeof(response.name.data), "ServiceConversionBridge");
    response.software_version.major = 1;
    response.software_version.minor = 2;
    response.hardware_version.major = 3;
    response.hardware_version.minor = 7;
    getUniqueID(response.hardware_version.unique_id);
    response.status = msg;
    response.status.uptime_sec = millis32() / 1000UL;
    node_info_server.respond(transfer, response);
    
}


void ServiceConversionBridge::start_canard_node() {
    start_node(can_interface_.c_str(), self_node_id_);
}

void ServiceConversionBridge::start_node(const char *can_interface_, uint8_t node_id) {
    canard_interface.init(can_interface_, node_id);
    setup_service_servers();
}

void ServiceConversionBridge::setup_service_servers() {
    auto shared_this = shared_from_this();
     timer_ = this->create_wall_timer(
        std::chrono::milliseconds(50), // Adjust the period as needed
        [this]() {
            canard_interface.process(1);
            const uint64_t ts = micros64();
            static uint64_t next_50hz_service_at = ts;
            if (ts >= next_50hz_service_at) {
                next_50hz_service_at += 1000000ULL / 50U;
                this->send_NodeStatus();
            }
        }
    );

    // Initialize your service conversion servers here
    // For example:
    // param_get_set_server_ = std::make_unique<ros_to_uav::ServiceConversionServer<uavcan::protocol::param::GetSet, your_ros_package::srv::YourService>>(
    //     &canard_interface, shared_this, "param_get_set");

    // Add more service conversion servers as needed
    node_info_server_service_ = std::make_unique<ros_to_uav::ServiceConversionServer<uavcan_protocol_GetNodeInfoRequest,uavcan_protocol_GetNodeInfoResponse, uavcan_ros_msgs::srv::UavcanGetNodeInfo>>(
        &canard_interface, shared_this, "get_node_info");
}

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<ServiceConversionBridge>();
    node->start_canard_node();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}