#ifndef _RKYOLOV5S_H_
#define _RKYOLOV5S_H_

#include <iostream>
#include <thread>
#include <fstream>

#include "rknn/rknn_api.h"
#include "rknn/preprocess.h"
#include "rknn/postprocess.h"
#include "opencv2/core/core.hpp"
/*
typedef struct _BOX_RECT
{
    int left;
    int right;
    int top;
    int bottom;
} BOX_RECT;

typedef struct __detect_result_t
{
    char name[OBJ_NAME_MAX_SIZE];
    BOX_RECT box;
    float prop;
    int track_id;
} detect_result_t; // 每一个检测结果只需要存储四个坐标（box），置信度值即可。

typedef struct _detect_result_group_t
{
    int id; // 对于每一帧的检测结果设置索引，该结果里可存储多个检测结果（一帧里有多个人）
    int count = 0;
    detect_result_t results[OBJ_NUMS_MAX_SIZE];
} detect_result_group_t;
*/

// 流程：inputData类型输入到模型中进行推理，然后经过postprocess.cpp处理得到的是dataEncode类型
    //  在rk3588_node.cpp中的run函数将dataEncode类型赋值给objectInfo结构体，进而形成resultInfo和alarmInfo结构体


// 功能：推理前输入数据的结构体
struct inputData{
    cv::Mat frame_data;
    int frame_index;
    int64_t frame_time_stamp;
};

//功能：推理结束后的输出结构体
struct dataEncode{
    detect_result_group_t detect_result_group;  // 这里存放的是detect_result_group_t 里面是BOX
    cv::Mat image; // 这里存储的是原图，这个有可能多余   问题：源代码这里未给赋值（rkYolov5s::infer）
    bool isInit = false;
    bool isNeedTrack = false;
    int64_t frame_time_stamp;
};

// static修饰函数的作用域也被限制在当前源文件内
static void dump_tensor_attr(rknn_tensor_attr *attr);

// 加载数据，打开的是fp文件，ofst是定位的偏移量，sz是读取的字节个数
static unsigned char *load_data(FILE *fp, size_t ofst, size_t sz);

// char指的是指针指向的数据类型，const表示指向的内容不能修改； int *指向int类型的指针，表示模型大小
static unsigned char *load_model(const char *filename, int *model_size);

static int saveFloat(const char *file_name, float *output, int element_size);


class rkYolov5s
{
    private:
        int ret;
        std::mutex mtx;
        std::string model_path;
        std::string label_path;
        std::vector<std::string> labels;
        unsigned char *model_data; // 模型数据

        rknn_context ctx; // 上下文环境
        rknn_input_output_num io_num; // 输入输出的个数，输入的个数默认为1，输出的个数默认为3
        rknn_tensor_attr *input_attrs;  // rknn_tensor_attr结构体描述张量属性信息 ,输入群组张量的属性
        rknn_tensor_attr *output_attrs; // 输出群组张量的属性
        rknn_input inputs[1]; // 创建一个输入的结构体类型

        int channel, width, height; // 目标图的宽高
        int img_width, img_height; // 原图的宽高

        float nms_threshold, conf_threshold;

        void load_label(const std::string &label_path, std::vector<std::string> &labels);


    public:
        rkYolov5s(const std::string &model_path, const std::string &label_path);
        
        int init(rknn_context *ctx_in, bool isChild); // 第一个参数传入地址，对地址进行解引用得到值

        rknn_context *get_pctx(); // 返回上下文地址
        
        dataEncode infer(inputData input_data);
        
        ~rkYolov5s();
};


#endif