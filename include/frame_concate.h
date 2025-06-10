#ifndef _FRAME_CONCATE_H_
#define _FRAME_CONCATE_H_

#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <cstdint>
#include <condition_variable>

#include "opencv2/core/core.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"

// 功能：枚举类型，显示矩阵, 这里用的，不是；
enum DisplaySize {
    SIZE_1X1 = 1,
    SIZE_2X2 = 2,
    SIZE_3X3 = 3,
    SIZE_4X4 = 4
};

// 功能：处理视频帧的拼接，主要用于在显示时，用于输出一路还是四路，还是16路用的
class FrameConcate
{
    public:
        //构造函数
        FrameConcate(int numInputs, int outputFPS, int width, int height);
        // 输入每路RTSP解码后的一帧，以及对应的输入流编号，将帧添加到输入流缓冲区中
        void addFrame(int inputIndex, const cv::Mat &frame);
        // 获取并输出合成帧
        cv::Mat getConcateFrame();

    private:
        // 帧缓冲区
        struct FrameBuffer
        {
            cv::Mat frame; // 存储输入源的最新帧
            cv::Mat resizeFrame; //存储调整大小后的帧
            bool hasNewFrame = false; // 标志是否有新的帧到达
            std::unique_ptr<std::mutex> mutex; // 智能指针mutex，指针指向一个互斥锁
            // 结构体的构造函数, 初始化列表，为成员变量赋值 ：成员变量1（值）, 成员变量2（值）{}
            FrameBuffer() : hasNewFrame(false), mutex(std::make_unique<std::mutex>()) {}
        };
        int inputNums; // 输入流的数量，一般是1或者2
        int outputFps; // 输出的帧率，一般是30
        int frameWidth, frameHeight;  // 一般是640,640  如果是四路，那就是320x320
        std::vector<FrameBuffer> buffers; // 存储每个输入流的最新帧。
        std::chrono::steady_clock::time_point nextFrameTime = std::chrono::steady_clock::now();
        int show_size_plan = 0;
};
#endif // _FRAME_CONCATE_H_