#include <stdio.h>
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include "rknn/preprocess.h"

void letterbox(const cv::Mat &image, cv::Mat &padded_image, BOX_RECT &pads, const float scale, const cv::Size &target_size, const cv::Scalar &pad_coloar)
{   
    // 图像缩放
    cv::Mat resized_image;
    // 缩放比例，和目标大小还是不一样的，例如（1280，640）的sacle的值在（640,640）时为0.5
    // void resize() 参数<原图><目标图><目标大小><水平缩放比例><垂直缩放比例><插值方式>
    cv::resize(image, resized_image, cv::Size(), scale, scale ,cv::INTER_NEAREST);

    // 获取填充大小
    int pad_width = target_size.width - resized_image.cols;
    int pad_heigh = target_size.height - resized_image.rows;
    pads.left = pad_width / 2;
    pads.right = pad_width - pads.left;
    pads.top = pad_heigh / 2;
    pads.bottom = pad_heigh - pads.top;

    // 填充 resize后的图，填充后的图，四个边填充大小，边框类型，边框颜色
    cv::copyMakeBorder(resized_image, padded_image, pads.top, pads.bottom, pads.left, pads.right, cv::BORDER_CONSTANT, pad_coloar); // 这里是边框颜色
    

}