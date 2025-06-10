#ifndef INTERFACE_H_
#define INTERFACE_H_

#include<stdio.h>
#include<string>
#include<unistd.h>
#include<memory>
#include<vector>
#include<map>

typedef struct{
    std::string edge_interface_version;                             //接口版本  
    std::string algorithm_namespace;                                //算法单元的命名空间
    std::string algorithm_deployname;                               //算法单元的部署名称
    std::string algorithm_config_path;                              //配置文件路径 [InitEdgeI()得到]

    bool        global_config_enable;                               //全局配置开关
    int         global_encode_frame_rate;                           //全局推流帧率设置,向RTSP推流

    bool        global_nvr_enable;                                  //全局nvr开关，NVR（设备网络视频录像机）海康威视
    std::string global_nvr_ip;                                      //全局nvrIP地址
    int         global_nvr_port;                                    //全局nvr接口
    std::string global_nvr_user;                                    //全局nvr的用户名
    std::string global_nvr_password;                                //全局nvr的密码
    int         global_nvr_duration;                                //全局nvr的录像时间持续时间（限制）
    
    std::string global_kafka_broker;                                //全局kafka broker, ip:port broker是kafka实例，默认为一台服务器
    std::string global_kafka_topic;                                 //全局kafka_topic

    std::string global_minio_server;                                //全局minio服务器，ip:port 存储视频数据
    std::string global_minio_access_key;                            //全局minio key
    std::string global_minio_secret_key;                            //全局minio key  
    
    std::vector<std::string> camera_id;                             //摄像头id  对应配置文件的id eg:"1716975926"
    std::vector<std::string> camera_type;                           //摄像头类型
    std::vector<std::string> camera_name;                           //摄像头名称 对应配置文件的name eg:"169_0"
    std::vector<std::string> camera_roi;                            //摄像头roi 对应配置文件中的roi
    std::vector<std::string> camera_url;                            //输入流的完整地址 对应配置文件的url
    
    std::vector<int> camera_frame_interval;                         //摄像头的帧间隔，每路摄像头帧序号的最大值
    std::vector<bool> camera_frame_show_local;                      //本地显示
    bool        global_osd_enable;                                  //全局本地osd画面开关，在视频画面上叠加显示文本、图形等信息的技术

    std::vector<bool> camera_alarm_smooth;		                    //首次上报
	std::vector<int> camera_alarm_interval;		                    //报警时间间隔

    std::vector<std::string> camera_statistics_start_time;          //统计开始时间  对应配置文件的statisticsStartTime
    std::vector<std::string> camera_statistics_end_time;            //统计结束时间 对应配置文件的StatisticsEndTime
    
    std::vector<float> camera_conf_config_threshold;                //各个类别的置信度阈值
    std::vector<float> camera_nms_config_threshold;                 //各个类别的NMS阈值

    std::map<int, std::map<std::string, float>> labels_thresh = {}; //各个摄像头下各个labels的阈值 例如：string = PersonThreshold 嵌套map 对应配置文件的xxxhreshold
    std::vector<std::string> labels_string;                         //标签  string = person  读取的不是json,而是labels.txt文件中的值

    std::string output_mount_url;                                   //输出流后缀

}EdgeInterfaceData;

class EdgeInterface
{
    public:
        EdgeInterface();
        ~EdgeInterface();
        void InitEdgeI(const std::string & algorithm_config_path, const std::string & algorithm_labels_path); //初始化函数定义
        EdgeInterfaceData GetEdgeIData();  

        std::string GetEdgeIBase64Encode(unsigned char const* byte_to_encode, unsigned int byte_length); 
        
        bool SendEdgeIMqttMessage(const std::string & mqtt_message); 

        bool InitEdgeIkafka(const std::string &brokers, const std::string &topic, int partition); 

        bool SendEdgeIKafkaMessage(const std::string &kafka_message, const std::string &key); 

    private:
        struct Impl;
        // 减少头文件之间的依赖，加快编译速度
        std::shared_ptr<Impl> p_impl_; 
};

#endif // INTERFACE_H_