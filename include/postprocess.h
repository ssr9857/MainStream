
#ifndef _RKNN_YOLOV5_DEMO_POSTPROCESS_H_
#define _RKNN_YOLOV5_DEMO_POSTPROCESS_H_

#include <iostream>
#include <stdint.h>
#include <vector>

#define OBJ_NAME_MAX_SIZE 16
#define OBJ_NUMS_MAX_SIZE 64

#define CONF_THRESHOLD 0.2
#define NMS_THRESHOLD 0.45


typedef struct _BOX_RECT  // 目标框的四个边距离图的”左边和上边“的距离，这个图是原图，不是缩放后的值
{
    int left;
    int right;
    int top;
    int bottom;
} BOX_RECT;

typedef struct __detect_result_t
{
    char name[OBJ_NAME_MAX_SIZE]; // 该结果名，例如person或者car
    BOX_RECT box; // 检测结果在
    float prop; // 置信度值（原置信度*probability）
    int track_id; 
} detect_result_t; // 每一个检测结果只需要存储四个坐标（box），置信度值即可。

typedef struct _detect_result_group_t
{
    int id; // 对于每一帧的检测结果设置设备索引  这个id指的是该帧属于第几个摄像头
    int count = 0; // 该结果里可存储多个检测结果（一帧里有多个人）
    detect_result_t results[OBJ_NUMS_MAX_SIZE];
} detect_result_group_t;

int post_process(int8_t *input0, int8_t *input1, int8_t *input2, int model_in_h, int model_in_w, float conf_threshold, float nms_threshold,
            BOX_RECT pads, float scale_w, float scale_h, std::vector<int32_t> &qnt_zps, std::vector<float> &qnt_scales,
             detect_result_group_t *group, std::vector<std::string> labels);

void deinitPostProcess();

#endif 