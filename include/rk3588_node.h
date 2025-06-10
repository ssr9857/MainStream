#ifndef RK3588_NODE_H
#define RK3588_NODE_H

#include "CGraph.h"  // functionNode 可执行 继承自node  node 不可执行 继承自element
#include <stdio.h>
#include <mutex>
#include <math.h>
#include <queue>
#include <signal.h> // 信号处理
#include <chrono>
#include <sys/inotify.h> // 文件系统事件监控,监控文件或目录的创建、修改、删除、移动出和移动入等
#include <poll.h> // 监控多个文件描述符的 I/O 状态。
#include <termios.h>
#include <sys/sysinfo.h> // 用于获取系统的各种信息，包括内存使用情况


#include "opencv2/opencv.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"

#include "rknn/rkYolvo5s.hpp"
#include "rknn/rknnPool.hpp"

#include "rk_vcodec/rkdecode.h"
#include "rk_vcodec/rkencode.h"
#include "rk_vcodec/rtsp_server.h"

#include "AINode.h"
#include "base64.h"
#include "frame_concate.h"

using namespace CGraph;
using namespace dpool;

// 定义输入帧缓冲区的最大值
#define FRAME_BUFFER_COUNT 120
// 定义推理结果缓冲区的最大值
#define ALGO_FRAME_RESULT_COUNT 120
#define IMGSHOW_BUFFER_COUNT 10

// 返回系统开始时间1970到现在经过的毫秒数
#define TIME_STAMP_MS std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()

// 打印当前时间，精确到毫秒
#define PRINT_CURRENT_TIME_WITH_MILLISECONDS(message) { \
    auto now = std::chrono::system_clock::now(); \
    auto ttime = std::chrono::system_clock::to_time_t(now); \
    auto duration = now.time_since_epoch(); \
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration) % 1000; \
    tm local_time = *localtime(&ttime); \
    std::cout << message << " "; \
    std::cout << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S"); \
    std::cout << '.' << std::setfill('0') << std::setw(3) << milliseconds.count() << std::endl; \
}

// 用于控制线程的退出
#define CHECK_QUIT_AND_BREAK(quit_mutex, quit_flag) \
    { \
        std::unique_lock<std::mutex> lock(quit_mutex);\
        if(quit_flag)\
        {\
            break;\
        }\
    }

static bool m_quit = false;
static std::mutex quit_mutex;

/*
继承GNode的成员变量和成员函数

虚函数重写，虚函数是在基类中使用 virtual 关键字声明的成员函数。只有虚函数能够被重写。实现多态性，具有可维护性
非虚函数在派生类中定义同名函数时，只是对基类函数进行隐藏，而不是重写。

静态成员函数：直接通过类名调用 (该变量为类的所有对象所共享) 生命周期：在程序启动时就已经存在
无论创建多少个类的对象，都只有一份静态成员变量的副本。常用于表示类的公共数据或统计信息等。
成员函数： 需要通过类的实例来调用 生命周期：随着对象的创建与销毁而消亡。   二者调用的方式不一样而已

使用引用而不是值传入：值传递自动会复制一份而占用内存，使用引用是为了减少内存占用
*/

/*
代码处理逻辑：
1. 初始化阶段
2. 线程启动阶段 
    监控线程：按照配置启动 monitor_file_thread  
    帧获取线程：针对每路摄像头，都会启动一个 frame_get_thread 线程
3. 帧获取与缓冲阶段  
    帧获取：在 frame_get_thread 线程中，持续从摄像头读取帧  
    帧缓冲：读取到的帧会被复制到 tmp_frame_buffer 中，同时给其设置 frame_index，之后将其添加到 m_frame_buffer 里
    帧间隔处理：利用 frame_interval_count 对帧间隔进行控制，仅当间隔达到配置值时，才会处理该帧
4. 推理阶段
    在 frame_get_thread 线程中，会把当前帧的时间戳设置到 tmp_frame_buffer 中，然后将其放入 p_algo_infer_pool 进行推理。
    frame_process_thread 线程会从 p_algo_infer_pool 中获取推理结果，再把结果添加到 m_algo_frame_result 里。
5. 帧处理与拼接阶段
    m_frame_buffer 中取出帧数据，还会查找对应的推理结果，把结果合并到帧数据里
    若启用了 RTSP 编码（RTSP_ENCODE_ENABLE 被定义），会把处理后的帧添加到 p_encode_frame_concate 中进行拼接。
    若启用了本地显示（SHOW_LOCAL_ENABLE 被定义），并且该路摄像头的帧需要显示，就会把处理后的帧添加到 m_frame_concate_deque 中
6. 显示与编码阶段
    显示阶段：show_local_thread 线程会从 m_frame_concate_deque 中取出帧，依据显示布局进行缩放和拼接，然后使用 cv::imshow 显示。
    编码阶段：encode_thread 线程会从 p_encode_frame_concate 中获取拼接后的帧，再使用 m_encoder 进行编码并传输到rtsp服务器中
*/


class RK3588Node : public CGraph::GNode 
{
    public:
        CStatus init() override; 

        CStatus run() override;  // 并不是单纯的run函数，而是继承自CGraph::GNode。

        static void signalHandler(int signum); 

        void handleSignal(int signum); 

    private:
        // 成员函数
        // 功能：图像缩放并填充
        cv::Mat resize_with_padding(const cv::Mat &inputImage, int targetWidth, int targetHeight); 

        // 功能：监控文件的线程
        void monitor_file_thread(const std::string &filePath);
        // 功能：监控文件夹的线程
        void monitor_directory_thread(const std::string &path);

        // 功能: 判断一个字符串是否包含另一个字符串
        bool contain_IP_address(const std::string &str, const std::string &ipAddress);
        // 功能：从指定的输入URL中获取视频帧
        void frame_get_thread(int input_url_index, std::string camera_url); // put
        // 功能：对帧进行处理
        void frame_process_thread();  // get
        // 功能：对视频帧进行编码处理
        void encode_thread(); 
        // 功能：将帧在本地显示，本设备上
        void show_local_thread();
        // 功能：设置跟踪信息
        CStatus set_track_info(const std::map<int, std::string>& track_info, const int framerate_thres, const int trackbuffer_thres);
        // 功能： 对帧进行跟踪
        CStatus track(dataEncode &input_frame);

        // 功能：获取当前系统时间
        std::string get_time_ymdhmm();
        // 功能：获取系统的内存使用情况
        void get_memory_usage(long &total, long &free, long &buffer);
        // 功能：定时输出当前时间，内存使用情况和各个缓冲区大小
        void time_memory_thread();

        // 功能：将配置拷贝到AI管道中
        void camera_setting_to_node(int camera_index, pipelineInfo &output);
        // 功能：对浮点数限制位数
        std::string formatFloatValue(float val, int fixed);
        
        // 变量
        bool edgeI_data_update = false;
        bool frame_get_thread_ready = false;

        // seawayEdge类实例
        SeawayEdgeInterface edgeI_config;

        // Edge结构体实例（主要用来读取配置文件信息）
        EdgeInterfaceDate edgeI_data;

        // 指针，指向线程池(模型推理) rknnPool<rkYolov5s, inputData, dataEncode>是rknnPool模板类的一个实例
        std::unique_ptr< rknnPool<rkYolov5s, inputData, dataEncode> > p_algo_infer_pool;
        std::unique_ptr< FrameConcate > p_encode_frame_concate; // 指向拼接帧的指针,类型为视频帧拼接类
        // 单纯定义，并没有初始化，上面俩在代码中已经初始化了
        // std::shared_ptr<RKDecoder> p_decoder; // 解码器指针(共享智能指针)，因为有多个frame_get_thread线程去从rtsp解码
        // std::shared_ptr<RKEncoder> p_encoder; // 编码器指针(共享指针) 可是只有一个encode_thread()线程去调用 问题：
        // 在这里直接定义，作为共享指针，因为在run函数中用于open编码, 在encode_thread中用于写入拼接后的帧。 先写入后编码上传推流
        std::shared_ptr<RKEncoder> p_encoder = std::make_shared<RKEncoder>();
        
        // 整型变量，定义fps打印间隔时间
        int fps_print_interval_ms = 5 * 1000;
        int display_size_plan = SIZE_1X1;

        // 本地显示的大小
        cv::Mat m_show_mat = cv::Mat::zeros(1080, 1920, CV_8UC3);

        // 锁
        std::mutex frame_get_thread_mutex; // 帧获取线程的锁
        std::mutex frame_process_thread_mutex; // 帧处理线程的锁
        std::mutex encode_mutex; // 处理后的帧编码锁
        std::mutex imgshow_mutex; // 图像本地显示的锁

        // 容器
        std::vector<std::thread> frame_threads; // 用于管理和控制程序中的不同线程，例如文件监控线程、帧处理线程、显示线程等。
        std::vector<int> frame_interval_count; // 存储每个通道的帧间隔计数，预留的成员变量，实际未应用

        std::vector<std::deque<dataEncode>> m_frame_concate_deque; // 帧拼接时的数组，每个元素都是推理结果队列
        std::vector<bool> m_concate_data_update_flag; // 拼接帧是否更新的标志
        std::vector<dataEncode> m_concate_data_to_encode; //准备用于去进行编码的结果
        // 这些结果类型都是dataEncode类型【左上角坐标，右下角坐标】，而不是objectInfo类型【左上角坐标，宽高】

        // 队列  
        std::deque<inputData>  m_frame_buffer; // 输入帧帧缓冲区队列  
        std::deque<dataEncode> m_algo_frame_result; // 推理后的的结果队列
        std::deque<cv::Mat> encode_mat_deque; // 编码帧的缓冲区队列  临时存储待处理帧，用于进行编码以便通过 RTSP 服务器进行传输
        std::deque<dataEncode> imgshow_deque; // 用于图像本地显示的队列 
        std::deque<std::mutex> m_frame_concate_mutex; // 帧拼接时用到的锁的队列

        // 图
        std::map<int, std::string> m_track_info;
        // std::map<int, std::shared_ptr<BYTETracker>> m_tracker_per_channel;

        // 模板函数，清空队列容器中的内容，队列中传入的数据类型不一样。
        // 当一个成员函数被声明为 const 时，它不能修改调用对象的非 mutable 成员变量，可以修改通过引用传入进来的，并非直接修改调用对象的成员变量。
        template <typename dequeType>
        void check_and_clear_buffer(std::deque<dequeType> &buffer, size_t buffer_count, const std::string &buffer_name) const {
            if(buffer.size() >= buffer_count)
            {
                std::cout << "WARNING: " << buffer_name << " size = " << buffer.size() << " is overload" << std::endl;
                // 移除队列所有元素，队列大小为0，但是并不会释放双端队列内部为存储元素而预先分配的内存（size 变为 0，但 capacity 可能保持不变）。
                buffer.clear(); 
                // std::queue<dequeType>()创建一个临时的空双端队列，交换内存，临时队列有了内容，但是没有获取原队列预先分配的内存。
                std::deque<dequeType>().swap(buffer);
            }
        }
};
#endif //RK3588_NODE_H