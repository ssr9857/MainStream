#ifndef _RKNN_YOLOV5_DEMO_PREPROCESS_H_
#define _RKNN_YOLOV5_DEMO_PREPROCESS_H_

#include <stdio.h>
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include "postprocess.h"  // 头文件中不包含.cpp文件

// 传入的图像image,在函数内部经过resize之后，再进行填充，填充后的图像保存在padded_image中，传入的是地址，不需要返回值，返回到了&padded_image（修改）
// <输入图像> <输出图像>  <边框填充> <缩放比例> <调整大小>（前者二选一） <填充颜色> 函数定义别忘了分号；
void letterbox(const cv::Mat &image, cv::Mat &padded_iamge, BOX_RECT &pads, const float scale, const cv::Size &target_size, const cv::Scalar &pad_color);


#endif