#include "frame_concate.h"

// 功能：构造函数，初始化类的成员变量   传入的参数列表 ： 成员变量1（形参1），成员变量2（形参2）{} 或者用this也可以
FrameConcate::FrameConcate(int numInputs, int outputFPS, int width, int height) : inputNums(numInputs), outputFps(outputFPS), frameWidth(width), frameHeight(height)
{
    // 以下在调用还是调用形参
    buffers.resize(numInputs); // 为vector设置初始化大小, 输入流的个数
    if( numInputs == 1)
    {
        show_size_plan = DisplaySize::SIZE_1X1;
    }
    else if( numInputs > 1 && numInputs <= 4)
    {
        show_size_plan = DisplaySize::SIZE_2X2;
    }
    else if( numInputs > 4 && numInputs <= 9)
    {
        show_size_plan = DisplaySize::SIZE_3X3;
    }
    else if( numInputs > 9 && numInputs <= 16)
    {
        show_size_plan = DisplaySize::SIZE_4X4;
    }
    else
    {
        std::cout << "show over size, numInputs is : " << numInputs << std::endl;
    }
}

// 输入每路RTSP解码后的一帧，和对应的输入流编号，存储在对应该流的缓冲区内  inputIndex指的是第几个摄像头
void FrameConcate::addFrame(int inputIndex, const cv::Mat &frame)
{
    // 对缓冲区的锁进行上锁，使用unique_lock锁管理器管理互斥锁mutex
    std::unique_lock<std::mutex> lock(*buffers[inputIndex].mutex); // mutex是一个智能指针，该指针指向一个互斥锁， lock是一个unique_lock对象（锁管理器）
    buffers[inputIndex].frame = frame.clone(); // 克隆：避免直接引用输入帧，防止输入帧被修改
    buffers[inputIndex].hasNewFrame = true;
}


// 按照设定帧率输出帧
cv::Mat FrameConcate::getConcateFrame()
{
    auto startTime = std::chrono::steady_clock::now();
    auto frameDuration = std::chrono::milliseconds( 1000 / outputFps);

    cv::Mat concateFrame = cv::Mat::zeros(frameHeight, frameWidth, CV_8UC3); // 用于存储拼接的帧
    bool allFrameReady = false; // 用于标记所有输入流的帧是否准备好
    std::vector<bool> framesProcessed(inputNums, false); //用于记录每个输入流的帧是否已经处理，初始为false, 大小为inputNums

    while(!allFrameReady) // 开始对所有输入流的帧进行处理, 直到每个输入流均有帧处理好，并拼接到目标帧concatedFrame而结束
    {
        allFrameReady = true; // 默认所有帧都准备好了
        for( int index = 0; index < inputNums; index++)
        {
            std::unique_lock<std::mutex> lock(*buffers[index].mutex);
            if(buffers[index].hasNewFrame && !buffers[index].frame.empty() && !framesProcessed[index])
            { // 进入要求： 有新的帧到达，当前输入流的缓冲区帧不为空，该帧未被处理
                cv::resize(buffers[index].frame, buffers[index].resizeFrame, cv::Size(frameWidth / show_size_plan, frameHeight / show_size_plan));
                buffers[index].hasNewFrame = false; // 该帧处理完了，没新帧了，除非addFrame在buffer中添加新的帧
                framesProcessed[index] = true; // 标记该输入流的缓存区的帧已经被处理了
            }

            if(!framesProcessed[index])
            { 
                allFrameReady = false; // 如果该通道的帧尚未处理，设置为 false表示该index对应的输入流的帧未准备好
            }

            // 如果该输入流的调整后的帧不为空，则将其拼接到目标帧中它该在的位置
            if(!buffers[index].resizeFrame.empty()) 
            {   // 用于表示矩形区域的类 四个参数：矩形区域左上角的x坐标，左上角y坐标，矩形的宽度，矩形的高度（index % show_size_plan和index / show_size_plan）
                buffers[index].resizeFrame.copyTo(concateFrame(cv::Rect(index % show_size_plan * frameWidth / show_size_plan, index / show_size_plan * frameHeight / show_size_plan, 
                    frameWidth / show_size_plan, frameHeight / show_size_plan)));
            }
            else
            { // 如果缓存中没有帧，则默认该位置为空白
                cv::Mat defaultFrame = cv::Mat::zeros(frameHeight / show_size_plan, frameWidth / show_size_plan, CV_8UC3);  // 返回空帧
                defaultFrame.copyTo(concateFrame(cv::Rect(index % show_size_plan * frameWidth / show_size_plan, index / show_size_plan * frameHeight / show_size_plan, 
                    frameWidth / show_size_plan, frameHeight / show_size_plan)));
            }
        }
        auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime);
        if( elapsedTime > frameDuration) // 如果超时，无论是否有输入流的帧准备好，则退出
        {
            break; // 退出while
        }
    }
    nextFrameTime += frameDuration; // 下一帧的到达时间
    std::this_thread::sleep_until(nextFrameTime); // 没到那就等到为止
    return concateFrame;
}
