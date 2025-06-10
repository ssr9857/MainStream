#ifndef AINODE_H
#define AINODE_H

#include <chrono>
#include <iostream>
#include <ctime>
#include <iomanip>
#include <list>
#include "edge_interface.h"
#include "json.h"
#include "opencv2/core/core.hpp"
#include "opencv2/highgui.hpp"
#include "seawayedge_interface.h"
#include "json.h"

// #define RTSP_ENCODE_ENABLE //推流
#define ALGO_INFER_ENABLE // 打开算法
// #define SHOW_LOCAL_ENABLE // 打开摄像头
#define PIPELINEINFO_LIST_THRESH 100

// ## 是预处理器的连接符   \是行继续符号  # 操作符会把宏的参数转换为一个字符串常量
#define TIME_START(id) auto time_start_##id = std::chrono::system_clock::now();

// 定义结束时间
#define TIME_END(id) \
        auto time_end_##id = std::chrono::system_clock::now(); \
        auto time_duration_##id = std::chrono::duration_cast<std::chrono::microseconds>(time_end_##id - time_start_##id); \
        std::cout << #id << " : " << double(time_duration_##id.count()) * (std::chrono::microseconds::period::num / std::chrono::microseconds::period::den) << " s" << std::endl;


// std::time_t 是一个表示日历时间的类型  put_time将 std::tm 结构体对象的时间信息按照指定的格式输出
#define LOG_DATE_TIME \
        auto now_time = std::chrono::system_clock::now(); \
        auto ttime = std::chrono::system_clock::to_time_t(now_time); \
        LOG(INFO) << std::put_time(std::localtime(&ttime), "seawaystream start %Y-%m-%d %H:%M:%S");


// 功能：存储设置信息
struct settingInfo
{
    std::string roi;
    std::map<std::string ,float> labelThreshMap;   
    int frameInterval;

    float confThreshold;
    float nmsThreshold;

    int alarmInterval;
    bool alarmSmooth;

    std::string statisiticsStartTime;
    std::string statisiticsEndTime; // 从camera_setting_to_node()函数获取
};

// 功能：存储检测框的信息,左上角坐标，宽高，得分，标签，追踪索引号
struct objectInfo
{
    int x{0}, y{0}; 
    int w{0}, h{0}; // 左上角坐标和宽高  推理拿到的dataEncode中的是左上角坐标和右下角坐标，模型推理实际得到的填充后的左上角坐标和宽高
    int track_id{0}; 
    std::string label; 
    float score {0}; 
};

// 功能：存储每一帧的报警信息, 报警的个数和每个报警目标结构体
struct alarmInfo
{
    int alarm_num {0};
    std::list<objectInfo> alarm_object_list;
};

// 功能：存储结果信息，同上
struct resultInfo
{
    int result_num {0};
    std::list<objectInfo> result_object_list;
};

// 存储管道信息（每个管道可以理解为一个摄像头）,每一帧
struct pipelineInfo  // 这里包含了检测结果信息，在rk3588_node.cpp中并没有进行赋值
{
    int camera_index {0};
    std::string camera_id;  // 摄像头id区分于摄像头的索引序号
    std::string camera_name;
    std::string seawayos_app;
    std::string seawayos_namespace;
    
    int alarm_type {0};
    std::string alarm_date;

    alarmInfo alarm_information;
    resultInfo result_information;

    cv::Mat source_image;
    settingInfo setting_information;  
    bool ready_for_mqtt = false;
};


// 功能：用于封装单个管道信息，用于通过 MQTT 协议发送消息。
struct mqttMessageParam 
{
    pipelineInfo pipelineinfo;

};

// 功能：用于一次性发送多个摄像头的信息。
struct sendInfoParam 
{
    std::list<pipelineInfo>  pipelineinfo_list;
};

// 功能：用于存储 EdgeInterface 类型的对象
struct SEIParam 
{
    EdgeInterface sei;
};
#endif 
