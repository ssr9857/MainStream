#include "../include/rknn/rk3588_node.h"

// 延迟初始化且在不同的代码模块中方便地访问 RK3588Node 类的实例。
RK3588Node *globalNodeInstance = nullptr;

// 调用：全局信号接收到信号后（本地显示打开），被动调用
// 功能：成员函数，用于初始化seaway_SDK
void RK3588Node::handleSignal(int signum)
{
    std::cout << "update handleSignal algorithm_config_path = " << edgeI_config.GetEdgeIDate().algorithm_config_path << std::endl;

    edgeI_config.InitEdgeI(edgeI_config.GetEdgeIDate().algorithm_config_path, "./rk3588/resources/config/");
    EdgeInterfaceDate temp_edgeI_data = edgeI_config.GetEdgeIDate();
    {
        std::lock_guard<std::mutex> lock(frame_get_thread_mutex);
        edgeI_data_update = true;
        frame_get_thread_ready = false;
    }
    frame_threads.emplace_back(&RK3588Node::frame_get_thread, this, 0, temp_edgeI_data.camera_url[0]);
}

// 调用：未被任何函数调用
// 功能: SIGTERM 信号处理函数  静态成员函数，全局共享,该函数被相应之后则退出  整个程序未调用
void RK3588Node::signalHandler(int signum) {
    std::cout << "Received SIGTERM. Terminating..." << std::endl;
    PRINT_CURRENT_TIME_WITH_MILLISECONDS("Terminate: ");
    std::lock_guard<std::mutex> lock(quit_mutex);
    m_quit = true;
}

// 调用：初始化rk3588Node时且开启本地显示时自动调用，继承CGraph 。CStatus RK3588Node::init()
// 功能：全局信号处理函数，项目中主要是用于当外设配置文件发生改变时，会发送信号到进程，然后触发全局信号监控函数
void globalSignalHandler(int signal) {

    std::cout << "Global signal handler received signal: " << signal << std::endl;
    // 调用类的成员函数
    if (globalNodeInstance) {
        globalNodeInstance->handleSignal(signal);
    }
}

// 功能：节点初始化
CStatus RK3588Node::init()
{
    CStatus status;
    CGraph::CGRAPH_ECHO("init RK3588Node"); 
    #ifdef SHOW_LOCAL_ENABLE
    // 当程序接收到 SIGUSR1 信号时，操作系统会自动调用 globalSignalHandler 函数，并且将接收到的信号编号作为参数传递给该函数.
        signal(SIGUSR1, globalSignalHandler);
    #endif
    frame_interval_count.resize(16);

    return CStatus();
}

// 功能：图像预处理
cv::Mat RK3588Node::resize_with_padding(const cv::Mat &inputImage, int targetWidth, int targetHeight)
{
    if (inputImage.empty())
    {
        std::cerr << "Error: Input image is empty!" << std::endl;
        return cv::Mat();
    }

    // 计算输入图像的宽高比
    double aspectRatio = static_cast<double>(inputImage.cols) / inputImage.rows;
    
    // 计算保持宽高比的新尺寸
    int newWidth, newHeight;
    if (targetWidth / aspectRatio <= targetHeight) {
        newWidth = targetWidth;
        newHeight = static_cast<int>(targetWidth / aspectRatio);
    } else {
        newWidth = static_cast<int>(targetHeight * aspectRatio);
        newHeight = targetHeight;
    }

    cv::Mat resizeImage;
    cv::resize(inputImage, resizeImage, cv::Size(newWidth, newHeight), 0, 0,cv::INTER_LINEAR); // 有了目标宽高就不需要缩放比例了

    // 计算padding大小
    int top = (targetHeight - newHeight) / 2;
    int bottom = targetHeight - newHeight - top;
    int left = (targetWidth - newWidth) / 2;
    int right = targetWidth - newHeight - left;

    // 填充加边
    cv::Mat outputImage;
    cv::copyMakeBorder(resizeImage, outputImage, top, bottom, left, right, cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));

    return outputImage;
}

// 获取当前时间，包括毫秒时区等
std::string RK3588Node::get_time_ymdhmm()
{
    // 获取当前时间点
    auto now = std::chrono::system_clock::now();
    // 将时间点转换为时间戳
    auto now_c = std::chrono::system_clock::to_time_t(now);
    // 转换为本地时间
    struct tm *stm = std::localtime(&now_c);

    // 获取毫秒部分
    auto duration = now.time_since_epoch();  
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() % 1000; // 自纪元开始时间，取模得毫秒

    // 获取时区偏移量
    int tz_offset = stm->tm_gmtoff;
    char tz_sign = (tz_offset >= 0) ? '+' : '-';
    tz_offset = std::abs(tz_offset);
    int tz_hour = tz_offset / 3600;
    int tz_min = (tz_offset % 3600) / 60;

    // 使用 stringstream 来格式化时间字符串
    std::stringstream ss;
    ss << std::put_time(stm, "%Y-%m-%dT%H:%M:%S"); // 格式化到秒
    ss << '.' << std::setw(3) << std::setfill('0') << millis; // 添加毫秒
    ss << tz_sign << std::setw(2) << std::setfill('0') << tz_hour << ':' << std::setw(2) << std::setfill('0') << tz_min; // 添加时区

    return ss.str();
}

// 获取内存的使用情况，包括总共，空闲区和缓冲区
void RK3588Node::get_memory_usage(long &total, long &free, long &buffer)
{
    struct sysinfo memoryInfo;

    sysinfo(&memoryInfo); // 对结构体赋值

    total = memoryInfo.totalram;
    total = (total * memoryInfo.mem_unit) / 1024; // KB表示

    free = memoryInfo.freeram;
    free = (free * memoryInfo.mem_unit) / 1024;

    buffer = memoryInfo.bufferram;
    buffer = (buffer * memoryInfo.mem_unit) / 1024;
}

// 定时输出当前时间的内存的使用情况
void RK3588Node::time_memory_thread()
{
    auto interval_time = std::chrono::seconds(10);// 定时打印的时间间隔

    while(true)
    {
        CHECK_QUIT_AND_BREAK(quit_mutex, m_quit);
        string currentTime = get_time_ymdhmm();
        long total, free, buffer;
        get_memory_usage(total, free, buffer); // 引用。如果函数为(long *total)，则传入&total.

        std::cout << "CurrentTime: " << currentTime << std::endl;
        std::cout << "TotalMemory: " << total / 1024 << "MB" << "FreeMemory: " << free / 1024 << "MB" << " BufferMemory:" << buffer / 1024 << "MB" << std::endl;
        {
            {
                std::unique_lock<std::mutex> lock(frame_get_thread_mutex);
                std::cout << "m_frame_buffer Size       = " << m_frame_buffer.size() << std::endl;
            }
            {
                std::unique_lock<std::mutex> lock(frame_process_thread_mutex);
                std::cout << "m_algo_frame_result Size  = " << m_algo_frame_result.size() << std::endl;
            }
            {
                std::unique_lock<std::mutex> lock(encode_mutex);
                std::cout << "encode_mat_deque Size     = " << encode_mat_deque.size() << std::endl;
            }
            {
                std::unique_lock<std::mutex> lock(imgshow_mutex);
                std::cout << "imgshow_deque Size        = " << imgshow_deque.size() << std::endl;
            }
        }
        this_thread::sleep_for(interval_time);
    }

}

// 功能：对外设配置文件进行内容监控，若文件发生改变则输出相应的文件日志
void RK3588Node::monitor_file_thread(const std::string &filePath)
{
    size_t last_index = filePath.find_last_of("/\\"); // 返回的是出现/的下标 /rk3588/include/rk_vcodec
    std::string directory = filePath.substr(0, last_index); // /rk3588/include
    std::string filename = filePath.substr(last_index + 1); // rk_vcodec

    // 定义读取文件内容的 lambda 函数，捕获列表为空，参数列表为路径，返回类型为字符串
    // 当仅使用一次且简单的函数，lambda可以直接定义和使用，使代码更简洁
    auto read_file_content = [](const std::string &path) -> std::string
    {
        std::ifstream file(path);
        if(!file.is_open())
        {
            std::cerr << "Failed to open file" << std::endl;
            return "";
        }
        std::stringstream buffer; // 字符串流对象 buffer，用于存储文件内容,这种方式不需要一行一行的去读取
        buffer << file.rdbuf(); // 将文件流的内容读取到字符串流中
        return buffer.str(); // 返回字符串流中的内容，内容中含有换行符
    };

    // 保存原始内容
    std::string previous_content = read_file_content(filePath);
    // 设置定时器
    const std::chrono::milliseconds check_interval(1000);

    while(true)
    {
        CHECK_QUIT_AND_BREAK(quit_mutex, m_quit);
        // 每隔一段时间读取文件内容
        std::this_thread::sleep_for(check_interval);
        // 获取文件内容
        std::string current_content = read_file_content(filePath);
        // 比较当前内容和之前的内容，发生改变则输出内容并发送信号给当前“进程”
        if(current_content != previous_content)
        {
            std::cout << get_time_ymdhmm() << " :file content changed:" << filePath << std::endl;
            previous_content = current_content;
            raise(SIGUSR1);
        }
    }
}

// <sys/inotify.h> 1.初始化实例：inotify_init()  2.添加监控项：inotify_add_watch()  3.读取事件：read()  4.移除监控项：inotify_rm_watch()  5.关闭实例：close()
// <poll.h> 监控的是文件描述符状态  1. POLLIN：表示有数据可读  POLLOUT：表示可以写入数据  POLLERR：表示发生了错误  POLLHUP：表示被挂起
// 功能：主要功能是监控指定目录及其子目录下文件和目录的变化，并且在检测到变化时触发相应的处理逻辑。
void RK3588Node::monitor_directory_thread(const std::string &path)  // 传入的是文件是要引用的
{
    int inotifyFd = inotify_init(); // 系统调用函数 内核中的文件通知机制,返回一个文件描述符
    std::cout << "inotyfy_init" << std::endl; // 调用成功
    if(inotifyFd == -1) // 调用失败
    {
        perror("inotify_init"); // 用是把最近一次系统调用产生的错误信息输出
        return ;
    }

    std::unordered_map<int, std::string> watch_descriptors; // 存储监控的文件描述符和路径 （无序，基于哈希表，时间复杂度O(1)）
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> event_times; // 记录每个文件的发生时间
    const std::chrono::milliseconds event_threshold(1500); // 事件去重，如果1.5s内发生同一事件，视为同一事件

    // 添加监控，监控文件或目录的创建、修改、删除、移动出和移动入
    int watchFd = inotify_add_watch(inotifyFd, path.c_str(), IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
    if(watchFd == -1)
    {
        perror("inotify_add_watch");
        close(inotifyFd);
        return;
    }

    // 存储监控的文件描述符和路径,初始化缓冲区和结构体
    watch_descriptors[watchFd] = path;
    char buf[1024 * (sizeof(inotify_event) + 16)];
    // pollfd 结构体用于指定 poll 函数要监控的文件描述符以及相关事件。
    struct pollfd fds = {inotifyFd, POLLIN, 0}; // POLLIN监控是否有可读数据  修改：打错

    while(true)
    {
        CHECK_QUIT_AND_BREAK(quit_mutex, m_quit);

        int pollNum = poll(&fds, 1, 500);// poll 是一个系统调用，它的作用是同时监控多个文件描述符的 I/O 状态。检查描述符上是否有可读事件。
        if (pollNum <= 0) {
            if (pollNum == -1) perror("poll");
            continue; // 超时或错误，继续检查退出条件
        }
        ssize_t numRead = read(fds.fd, buf, sizeof(buf)); // 读取事件数据到缓冲区
        if (numRead <= 0) {
            perror("read");
            break;
        }
        auto now = std::chrono::steady_clock::now();

        for (char* ptr = buf; ptr < buf + numRead;) {
            inotify_event* event = reinterpret_cast<inotify_event*>(ptr);
            std::string filename = watch_descriptors[event->wd] + "/" + event->name;

            // 去重检查
            auto it = event_times.find(filename);
            if (it == event_times.end() || std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second) >= event_threshold) {
                event_times[filename] = now; // 更新事件时间

                if (event->mask & IN_CREATE) {
                    std::cout << "File or directory " << filename << " was created." << std::endl;
                    // 如果创建的是目录，则为该目录添加监控
                    if (event->mask & IN_ISDIR) {
                        int new_watchFd = inotify_add_watch(inotifyFd, filename.c_str(), IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
                        if (new_watchFd != -1) {
                            watch_descriptors[new_watchFd] = filename;
                        }
                    }
                } else if (event->mask & IN_MODIFY) {
                    std::cout << "File " << filename << " was modified." << std::endl;
                } else if (event->mask & IN_CLOSE_WRITE) {
                    std::cout << "File " << filename << " was closed after writing." << std::endl;
                } else if (event->mask & IN_DELETE || event->mask & IN_MOVED_FROM) {
                    std::cout << "File or directory " << filename << " was deleted or moved from." << std::endl;
                    // 如果删除的是目录，则移除该目录的监控
                    if (event->mask & IN_ISDIR) {
                        watch_descriptors.erase(event->wd);
                    }
                } else if (event->mask & IN_MOVED_TO) {
                    std::cout << "File or directory " << filename << " was moved to." << std::endl;
                }

                std::cout << "get a signal***** : " << get_time_ymdhmm() << std::endl;
                // 调用自定义信号处理程序
                raise(SIGUSR1);
                // break;
            }

            ptr += sizeof(inotify_event) + event->len;
        }
    }
    close(inotifyFd);
}

// 功能：协助frame_get_thread用于判断一个字符串里面是否有指定IP地址  [引用，如果直接传入就是值拷贝]
bool RK3588Node::contain_IP_address(const std::string &str, const std::string &ipAddress) // 引用避免值拷贝
{
    if(str.find(ipAddress) != std::string::npos)
        return true;
    else    
        return false;
}



















// 功能：帧获取线程，从对应的输入流（摄像头）获取帧【解码】
// 依据输入的 URL 地址，从视频流里获取帧数据。若输入 URL 包含 192.168.1.58，就使用 cv::VideoCapture 来解码；反之，则采用 RKDecoder 进行解码
// 获取到的帧数据会被存入 m_frame_buffer 中，在满足条件时还会将其放入算法推理池。
void RK3588Node::frame_get_thread(int camera_url_index, std::string camera_url)
{
    cv::Mat frame; 
    int reopen_count = 0; // 记录解码器重新打开的次数(opencv_decoder读取帧失败计算一次)
    bool ready_to_update = false; // 是否准备好更新数据
    bool new_thread_start = true;  // 是否有新的线程开始
    inputData tmp_frame_buffer; // 临时存储输入

    if(contain_IP_address(camera_url, "192.168.1.58"))// 根据输入 URL 选择解码器
    {   
        std::cout << "get the frame from opencv_decoder" << std::endl;
        cv::VideoCapture opencv_decoder(camera_url);  // 解码器实例
        while(!opencv_decoder.isOpened()) // 判断是否可以打开摄像头
        {
            std::cout << "cannot open opencv_decoder:" << camera_url << std::endl;
            sleep(1);
        }
        std::cout << camera_url << " is open success" << std::endl;

        while(true)
        {
            CHECK_QUIT_AND_BREAK(quit_mutex, m_quit);
            bool ret = false;
            ret = opencv_decoder.read(frame); // 从视频流读取一帧,读取到的帧保存在frame中
            if(!ret) // 如果读取失败
            {
                std::cout << "get frame error from camera_url " << std::endl;
                {
                    std::unique_lock<std::mutex> lock(frame_get_thread_mutex);
                    if(edgeI_data_update) // 检查是否需要更新数据 edgeI_data（EdgeInterfaceDate实例）
                    {
                        break;
                    }
                }
                sleep(1);
                reopen_count++;
                if(reopen_count == 3) // 连续三帧读取不到
                {
                    reopen_count = 0;
                    opencv_decoder.release(); // 释放当前解码器
                    std::cout << " reopen opencv_decoder " << std::endl;
                    if(!opencv_decoder.open(camera_url)) // 重新打开指定的视频源,系统重新分配资源
                        // 这里并没有退出机制，而是一直尝试去打开摄像头读取帧，设置reopen三次是为了打不开时输出错误信息。
                        std::cout << "cannot open opencv_decoder:" << camera_url << std::endl;
                }
                continue; // 如果读取失败，重新读取新的帧
            }
            reopen_count = 0; // 遇到了读取成功的就重新打开清零了
            {
                std::unique_lock<std::mutex> lock(frame_get_thread_mutex);
                if(edgeI_data_update) // 检查是否有数据更新
                {
                    ready_to_update = true;
                    edgeI_data_update = false;
                }
                if(ready_to_update)
                {
                    if(edgeI_data_update)
                        break;
                    if(frame_get_thread_ready)
                        break;
                }
                else if(new_thread_start)
                {
                    frame_get_thread_ready = true;
                    std::cout << "new thread get frame" << std::endl;
                    new_thread_start = false;
                }
                tmp_frame_buffer.frame_data = frame.clone(); // 开始给模型输入结构体赋值,并存储到输入队列中
                tmp_frame_buffer.frame_index = camera_url_index;
                m_frame_buffer.push_back(tmp_frame_buffer);

                // 帧间隔，如果已经达到了帧间隔技术，则重新计算
                if(frame_interval_count[camera_url_index] == edgeI_data.camera_frame_interval[camera_url_index])
                    frame_interval_count[camera_url_index] = 0;
                else
                {
                    frame_interval_count[camera_url_index]++;
                    continue;;
                }
            }
#ifdef ALGO_INFER_ENABLE
            {
                // 其实这一部分放在
                tmp_frame_buffer.frame_time_stamp = TIME_STAMP_MS; // 帧的时间戳，推理时会用到
                if(p_algo_infer_pool->put(tmp_frame_buffer)!=0) // 推理成功并添加结果进futures队列则返回0
                {
                    std::cout << "infer frame error" << std::endl;
                    continue;
                }
            }
#endif
        }
    }
    // 这个时候表示，摄像头不行了，使用rtsp协议，从rtsp服务器获取视频流,在摄像头处进行如 H.264、H.265 编码
    // rtsp只负责传输，不负责编解码，通过rtsp服务器获取到的数据后然后进行解码decoder
    else
    {
        // 获取视频流 bool Open(std::string url_filepath);
        std::cout << "get the frame from p_decoder" << std::endl;
        std::shared_ptr<RKDecoder> p_decoder = std::make_shared<RKDecoder>();
        // 监测是否可以打开该地址的rtsp服务  rtsp://admin:admin@192.168.1.167:554/Streaming/Channels/101
        while(!p_decoder->Open(camera_url)) 
        {
            std::cout << "cannot open Decoder:  " << camera_url << std::endl;
            sleep(1);
        }
        std::cout << "can open Decoder:  " << camera_url << std::endl;

        while(true)
        {
            CHECK_QUIT_AND_BREAK(quit_mutex, m_quit);
            bool ret = false;
            ret = p_decoder->GetFrame(frame); // 从rtsp服务器读取帧
            if (!ret)
            {
                std::cout << "get frame error from camera_url" << std::endl;
                {
                    std::unique_lock<std::mutex> lock(frame_get_thread_mutex);
                    if(edgeI_data_update)
                    {
                        break;
                    }
                }
                sleep(1);
                reopen_count++;
                if(reopen_count == 3)
                {
                    reopen_count = 0;
                    p_decoder->Close(); // 释放资源
                    std::cout << "reopen Decoder" << std::endl;
                    if(!p_decoder->Open(camera_url)) // 再次尝试打开
                        std::cout << "cannot open Decoder:  " << camera_url << std::endl;
                }
                continue;
            }
            reopen_count = 0;
            {
                std::unique_lock<std::mutex> lock(frame_get_thread_mutex);
                if(edgeI_data_update)
                {
                    ready_to_update = true;
                    edgeI_data_update = false;
                }
                if(ready_to_update)
                {
                    if(edgeI_data_update)
                        break;
                    if(frame_get_thread_ready)
                        break;
                }
                else if (new_thread_start)
                {
                    frame_get_thread_ready = true;
                    std::cout << "new thread get frame" << std::endl;
                    new_thread_start = false;
                }

                tmp_frame_buffer.frame_data = frame.clone();
                tmp_frame_buffer.frame_index = camera_url_index;
                m_frame_buffer.push_back(tmp_frame_buffer); // ****将获取的帧读入到输入帧缓冲区中*****

                // 设置帧间隔的目的是为了间隔多少帧，则对帧进行重新计数，否则一天下来，该通道的帧个数会非常大
                if(frame_interval_count[camera_url_index] == edgeI_data.camera_frame_interval[camera_url_index])
                    frame_interval_count[camera_url_index] = 0;
                else{
                    frame_interval_count[camera_url_index]++;
                    continue;
                }
            }
#ifdef ALGO_INFER_ENABLE
            {
                // 其实这一部分放在frame_process_thread更合适，因为put中就执行了推理操作，get只是对推理后的结果进行拿取
                tmp_frame_buffer.frame_time_stamp = TIME_STAMP_MS;
                std::cout << "add input to rknnPool"  << std::endl;
                // futures.push(pool->submit(&rknnModel::infer, models[this->getModelId()], inputdata));
                std::cout << "Call the put function of rknnPool" << std::endl;
                if (p_algo_infer_pool->put(tmp_frame_buffer) != 0)
                {
                    std::cout << "infer frame error" << std::endl;
                    continue;
                }
                // TIMER_END(algo_put);
            }
#endif
        }// while循环会一直获取帧，并将其作为推理任务添加进线程池中进行处理
    }
    {
        std::unique_lock<std::mutex> lock(frame_get_thread_mutex);
        frame_get_thread_ready = false;
        edgeI_data_update = false;
    }
    std::cout <<  "frame_get_thread release" << std::endl;
}


// 功能：对线程池中推理得到的数据进行处理
void RK3588Node::frame_process_thread()
{
    int algo_fps = 0; // 帧率  如果是四路，则camera_url_index[0-4]
    std::chrono::milliseconds interval(fps_print_interval_ms);
    auto now = std::chrono::steady_clock::now();
    auto nextExecution = std::chrono::steady_clock::now() + interval; // 定义帧率输出间隔时间

    dataEncode data_to_encode; // 准备用于编码的数据，【帧处理输出数据结构】
    while(true)
    {
        CHECK_QUIT_AND_BREAK(quit_mutex, m_quit); // 该宏里面有break
        now = std::chrono::steady_clock::now();
        {
#ifdef ALGO_INFER_ENABLE
            // std::cout << "Call the get function of rknnPool" << std::endl;
            if(p_algo_infer_pool->get(data_to_encode) != 0) // 将推理结果futures队列头复制给data_to_encode
            {
                continue;
            }
            {
                std::unique_lock<std::mutex> lock(frame_process_thread_mutex);
                m_algo_frame_result.push_back(data_to_encode); // ******将获取的推理结果添加进推理结果队列中******
            }
#endif
        }
        if(now >= nextExecution)
        {
            algo_fps = 0;
            std::cout << "Algo fps: " << algo_fps / (fps_print_interval_ms / 1000) << std::endl;
            now = std::chrono::steady_clock::now();
            nextExecution = now + interval;
        }
        algo_fps ++;
    }
    std::cout << " frame_process_thread release" << std::endl;
}

// 功能：用于在本设备上即实时显示画面
void RK3588Node::show_local_thread()
{
    cv::Mat image_show_mat;
    int image_show_index = 0;
    bool init_flag = false;

    int show_size = std::count(edgeI_data.camera_frame_show_local.begin(), edgeI_data.camera_frame_show_local.end(), true);
    int show_size_plan = 0;
    if (show_size == 1)
    {
        show_size_plan = DisplaySize::SIZE_1X1;
    } else if (show_size > 1 && show_size <= 4)
    {
        show_size_plan = DisplaySize::SIZE_2X2;
    } else if (show_size > 4 && show_size <= 9)
    {
        show_size_plan = DisplaySize::SIZE_3X3;
    } else if (show_size > 9 && show_size <= 16)
    {
        show_size_plan = DisplaySize::SIZE_4X4;
    } else
    {
        std::cout << "show over size" << std::endl;
    }

    int show_fps = 0;
    std::chrono::milliseconds interval(fps_print_interval_ms);
    auto now = std::chrono::steady_clock::now();
    auto nextExecution = std::chrono::steady_clock::now() + interval;

    std::string windows_name = get_time_ymdhmm();
    cv::namedWindow(windows_name, cv::WINDOW_NORMAL);
    cv::setWindowProperty(windows_name, cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);
    PRINT_CURRENT_TIME_WITH_MILLISECONDS("start_show_local_thread: ");
    while(true)
    {
        bool update_frame = false;
        // TIMER_START(imgshow_frame);
        int result_count = 0;
        CHECK_QUIT_AND_BREAK(quit_mutex, m_quit);
        now = std::chrono::steady_clock::now();
        for(int image_show_index = 0; image_show_index < edgeI_data.camera_url.size(); image_show_index ++)
        {
            if( edgeI_data.camera_frame_show_local[image_show_index] == true )
            {
                std::lock_guard<std::mutex> lock(m_frame_concate_mutex[image_show_index]);
                if (m_frame_concate_deque[image_show_index].size() != 0)
                {
                    update_frame = true;
                    cv::Mat temp_image;
                    temp_image = m_frame_concate_deque[image_show_index].front().image;
                    m_frame_concate_deque[image_show_index].pop_front();
                    switch (show_size_plan)
                    {
                        case SIZE_1X1:
                            cv::resize(temp_image, m_show_mat, cv::Size(1920, 1080), 0, 0, cv::INTER_LINEAR);
                            break;
                        default:
                            temp_image = resize_with_padding(temp_image, 960, 540);
                            // cv::resize(temp_image, temp_image, cv::Size(temp_image.cols / show_size_plan, temp_image.rows / show_size_plan), 0, 0, cv::INTER_NEAREST);
                            temp_image.copyTo(m_show_mat(cv::Rect(image_show_index % show_size_plan * 1920 / show_size_plan, image_show_index / show_size_plan * 1080 / show_size_plan,
                                                1920 / show_size_plan, 1080 / show_size_plan)));
                            break;
                    }
                }
                check_and_clear_buffer(m_frame_concate_deque[image_show_index], IMGSHOW_BUFFER_COUNT, "imshow buffer");
            }
        }
        if(!init_flag)
            PRINT_CURRENT_TIME_WITH_MILLISECONDS("imshow: ");
        init_flag = true;

        if(!update_frame)
            continue;

        cv::imshow(windows_name, m_show_mat);
        cv::waitKey(20);
        if(now >= nextExecution)
        {
            std::cout << "Show fps: " << show_fps / (fps_print_interval_ms / 1000) << std::endl;
            show_fps = 0;
            now = std::chrono::steady_clock::now();
            nextExecution = now + interval;
        }
        show_fps ++;
        // TIMER_END(imgshow_frame);
    }
    std::cout << "destroyWindow " << windows_name << std::endl;
    cv::destroyWindow(windows_name);
    std::cout << "show_local_thread release" << std::endl;
}

// 功能：持续从 p_encode_frame_concate 对象中获取拼接好的帧图像，将这些帧图像写入编码器 m_encoder 进行编码
void RK3588Node::encode_thread()
{
    cv::Mat image_show_mat;
    sleep(1);

    int encode_fps = 0; // 编码的帧率
    std::chrono::milliseconds interval(fps_print_interval_ms);

    auto now = std::chrono::steady_clock::now();
    auto nextExecution = std::chrono::steady_clock::now() + interval;

    while(true)
    {
        CHECK_QUIT_AND_BREAK(quit_mutex, m_quit);
        now = std::chrono::steady_clock::now();
        image_show_mat = p_encode_frame_concate->getConcateFrame(); // 有可能是四个320*320组成的640*640的图像
        if(image_show_mat.empty())
        {
            continue;
        }
        p_encoder->WriteFrame(image_show_mat); // 将拼接后的帧通过编码器传输到rtsp服务器中
        if(now >= nextExecution)
        {
            std::cout << "Encode fps: " << encode_fps / (fps_print_interval_ms / 1000) << std::endl;
            encode_fps = 0;
            now = std::chrono::steady_clock::now();
            nextExecution = now + interval;
        }
        encode_fps ++;
        // TIMER_END(encode_frame);
    }
    std::cout <<  "encode_thread release" << std::endl;
}

// 功能：将一个浮点数按照指定的小数位进行格式化
std::string RK3588Node::formatFloatValue(float val, int fixed)
{
    std::ostringstream oss; // 输出字符串流，允许将各种数据类型插入到流中
    oss << std::setprecision(fixed) << val; // std::setprecision 是一个操纵符，它用于设置流的输出精度
    return oss.str();
}

// 没有对警告信息和结果信息进行赋值
// 功能：将seawayEdge从json文件获取到的配置文件拷贝到PipelineInfo中。参数：【相机编号，目标管道信息结构体】
void RK3588Node::camera_setting_to_node(int camera_index, pipelineInfo &output)
{
    output.seawayos_namespace = edgeI_data.algorithm_namespace; // 命名空间
    output.seawayos_app = edgeI_data.algorithm_deployname; // 部署名字
    output.camera_index = camera_index; // 摄像头索引
    output.camera_id = edgeI_data.camera_id[camera_index]; // 摄像头对应id
    output.camera_name = edgeI_data.camera_name[camera_index]; // 摄像头名字

    output.setting_information.frameInterval = edgeI_data.camera_frame_interval[camera_index]; // 帧间隔
    output.setting_information.roi = edgeI_data.camera_roi[camera_index]; // ROI

    output.setting_information.confThreshold = edgeI_data.camera_conf_config_threshold[camera_index]; // 置信度阈值
    output.setting_information.nmsThreshold = edgeI_data.camera_nms_config_threshold[camera_index]; // NMS阈值
    output.setting_information.labelThreshMap = edgeI_data.labels_thresh[camera_index];// 标签阈值   std::map<int, std::map<std::string, float>> labels_thresh = {};

    output.setting_information.alarmInterval = edgeI_data.camera_alarm_interval[camera_index]; // 警告间隔时间
    output.setting_information.alarmSmooth = edgeI_data.camera_alarm_smooth[camera_index]; // 警告是否首次上报

    output.setting_information.statisiticsStartTime = edgeI_data.camera_statistics_start_time[camera_index];// 开始统计时间
    output.setting_information.statisiticsEndTime = edgeI_data.camera_statistics_end_time[camera_index]; // 结束统计时间
}

// 功能：rk3588对模型进行全流程推理的函数
// 各种初始化
// 从rtsp解码拿到帧，并将该帧加入到m_frame_buffer（输入帧缓冲区）中用于后续处理
// 将帧缓冲区（双端队列）头帧逐次放入到线程池中进行推理
// 从线程池get拿到推理结果放到m_algo_frame_result（输出结果缓冲区）
// 从输出结果缓冲区头部取帧，用于拼接，拼接后传输给rtsp服务器中
CStatus RK3588Node::run()
{
    CStatus status; // 存储函数执行状态
    CGraph::CGRAPH_ECHO("run RK3588Node");
    auto p_sei_param = CGRAPH_GET_GPARAM_WITH_NO_EMPTY(SEIParam, "sei_data"); // 获取指定类型和键名的参数信息,若参数为空则抛出异常。 this 对象为当前RK3588Node对象
    std::string seawayedge_interface_version;
    {
        CGRAPH_PARAM_READ_CODE_BLOCK(p_sei_param);
        edgeI_config = p_sei_param->sei; // seawayEdge对象
        std::cout <<edgeI_config.GetEdgeIDate().camera_id[0] << std::endl;
        std::cout <<edgeI_config.GetEdgeIDate().camera_conf_config_threshold[0] << std::endl;
        
    }// edgeI_data是EdgeInterfaceDate结构体实例
    edgeI_data = edgeI_config.GetEdgeIDate();// 问题：获取之前不应该先初始化调用json文件内容吗？ 

// 是否本地设备上显示............................................................
#ifdef SHOW_LOCAL_ENABLE
    frame_threads.emplace_back(&RK3588Node::monitor_file_thread, this, edgeI_data.algorithm_config_path + "peripheral_config.json");
#else
    frame_threads.emplace_back(&RK3588Node::monitor_directory_thread, this, edgeI_data.algorithm_config_path);
#endif

// 是否开启算法推理..............................................................
#ifdef ALGO_INFER_ENABLE

    // 前三个是传入的模板参数，后边的是rknnPool构造函数传入的参数  调用了构造函数
    p_algo_infer_pool = std::make_unique< rknnPool<rkYolov5s, inputData, dataEncode> >("rk3588/resources/model/rk3588model.rknn","rk3588/resources/config/labels.txt", 2);
    std::cout << "Thread pool creation, start calling init function" << std::endl;

    // 调用线程池的初始化函数,里面也包含了rkYolvo5的init函数的调用
    p_algo_infer_pool->init(); 

    // 添加帧获取的线程，通过p_decoder获取帧
    for(int camera_url_index = 0; camera_url_index < edgeI_data.camera_url.size(); camera_url_index++ )
    {
        // 与线程池区分开，线程池是用来推理模型的
        // rtsp://admin:admin@192.168.1.167:554/Streaming/Channels/101
        frame_threads.emplace_back(&RK3588Node::frame_get_thread, this, camera_url_index, edgeI_data.camera_url[camera_url_index]);// 添加帧获取线程，几路摄像头就有几个线程。 
    }

    // 在调用成员函数时，需要一个对象实例来调用,this 作为调用 frame_process_thread 成员函数的对象实例。
    frame_threads.emplace_back(&RK3588Node::frame_process_thread, this); // 添加帧处理线程
#endif

// 是否使用rtsp编码服务器..............................................................
#ifdef RTSP_ENCODE_ENABLE
    PRINT_CURRENT_TIME_WITH_MILLISECONDS("rtsp————server start: ");
    // 打开rtsp服务，在frame_get_thread中，通过p_decoder->Open(camera_url)打开，然后通过getFrame逐帧获取
    rtsp_server_start(RTSP_SERVER_PORT_DEFAULT, "rk3588/resources/config/config.ini");

    ImageSize size;
    size.width = 1920;
    size.height = 1080;
    // 这个端口不是解码用的端口，而是编码用的
    if(!p_encoder->Open(ENCODE_TYPE_H264, edgeI_data.global_encode_frame_rate, size, 8554, "stream")) // 打开编码
    {
        std::cout << "cannot open Encoder " << std::endl;
    }
    frame_threads.emplace_back(&RK3588Node::encode_thread, this); // 加入编码处理的线程
    PRINT_CURRENT_TIME_WITH_MILLISECONDS("encode_thread start: ");

    // 初始化，并传入构造函数的参数
    p_encode_frame_concate = std::make_unique< FrameConcate >(edgeI_data.camera_url.size(), edgeI_data.global_encode_frame_rate, 1920, 1080); // 帧拼接对象的指针
#endif

// 是否本设备上显示...................................................................
#ifdef SHOW_LOCAL_ENABLE
    // lambda 表达式： [](bool show_flag){return show_flag==true;}
    // any_of：用于检查指定范围内的元素是否至少有一个满足给定的条件，检查是否有摄像头要本地显示
    if(std::any_of(edgeI_data.camera_frame_show_local.begin(), edgeI_data.camera_frame_show_local.end(), [](bool show_flag){return show_flag==true;} ))
    {
        std::cout << "show local frame" << std::endl;
        frame_threads.emplace_back(&RK3588Node::show_local_thread, this); // 添加本地显示的线程
    }
#endif

    int instance_count = edgeI_data.camera_url.size();
    // 初始化拼接帧mutex deque
    m_frame_concate_mutex.resize(instance_count);
    m_frame_concate_deque.resize(instance_count); // 问题：在非show_local下如何使用，难道只有本地显示的时候才拼接帧吧
    // 问题：这个本地显示不会就是在seawayEdge上显示吧

    // 初始化编码buffer vector
    m_concate_data_update_flag.resize(instance_count);
    m_concate_data_to_encode.resize(instance_count);

    if (instance_count == 1)
    {
        display_size_plan = DisplaySize::SIZE_1X1; // 区分大小写，注意
    } else if (instance_count > 1 && instance_count <= 4)
    {
        display_size_plan = DisplaySize::SIZE_2X2;
    } else if (instance_count > 4 && instance_count <= 9)
    {
        display_size_plan = DisplaySize::SIZE_3X3;
    } else if (instance_count > 9 && instance_count <= 16)
    {
        display_size_plan = DisplaySize::SIZE_4X4;
    } else
    {
        std::cout << "display over size" << std::endl;
    }

    // 在该循环中仍然是一帧一帧的去处理，因为有这个data_to_encode，先获取输入帧的id，image, frame_time_stamp 后来又去比较m_algo_frame_result中的内容
    // 幸亏不是去对比m_algo_frame_result.front() 否则慢死
    while(true) 
    {
        CHECK_QUIT_AND_BREAK(quit_mutex, m_quit);
        dataEncode data_to_encode; // 用于从m_algo_frame_result拿到推理结果用于后续的拼接编码
        {
            std::unique_lock<std::mutex> lock(frame_get_thread_mutex); // 帧获取线程锁
// 是否开启算法推理
#ifdef ALGO_INFER_ENABLE
            if(m_frame_buffer.size() <= 15 * instance_count)
#else
            if(m_frame_buffer.empty())
#endif
                continue; // 问题：为什么输入缓冲区没达到就要continue, 可能为了帧够多才行
            // data_to_encode是DataEncode结构体，m_frame_buffer是inputData结构体
            // 这里并非全部克隆，只克隆了源帧，帧索引（第几路摄像头来的帧），帧时间戳
            data_to_encode.image = m_frame_buffer.front().frame_data.clone();
            data_to_encode.detect_result_group.id = m_frame_buffer.front().frame_index;
            data_to_encode.frame_time_stamp = m_frame_buffer.front().frame_time_stamp;
            m_frame_buffer.pop_front(); // 取出头部
            check_and_clear_buffer(m_frame_buffer, FRAME_BUFFER_COUNT, "frame buffer"); // 这里是检查并清空，没超过buffer就不用清空
        }
        int frame_source_index = data_to_encode.detect_result_group.id;

#ifdef ALGO_INFER_ENABLE
        {
            std::unique_lock<std::mutex> lock(frame_process_thread_mutex); // 帧处理线程锁
            if(!m_algo_frame_result.empty()) // 在帧处理线程中将处理结果加入到p_algo_frame_result队列中
            {
                for(auto it = m_algo_frame_result.begin(); it != m_algo_frame_result.end(); it++) // 迭代器遍历容器元素
                {

                    // begin() 是容器（这里是 std::deque）的成员函数，它返回一个指向容器中第一个元素的迭代器。
                    // std::deque 的内存结构不是连续的，erase 操作返回的迭代器指向被删除元素之后的元素。
                    // std::vector内存结构连续，删除元素后，被删除元素之后的所有元素都会向前移动，以填补空缺，erase 函数会返回一个指向被删除元素后面元素的迭代器，但是后面的迭代器全部失效。
                    // 所以，如果使用std::vector使用迭代器删除元素，从后到前删除可以避免后面的迭代器失效的问题。
                    if( (it->detect_result_group.id == data_to_encode.detect_result_group.id) && (it->frame_time_stamp == data_to_encode.frame_time_stamp) )
                    {
                        data_to_encode.detect_result_group = it->detect_result_group; // 迭代器是个指针
                        data_to_encode.isNeedTrack = true;
                        m_algo_frame_result.erase(it);
                        break;
                    }
                    check_and_clear_buffer(m_algo_frame_result, ALGO_FRAME_RESULT_COUNT, "algo frame result buffer");
                }
            }
        }

        // 跟踪帧
        if(data_to_encode.isNeedTrack)  // data_to_encode就表示一帧的数据，而不是多帧
        {
            track(data_to_encode);
        }
        // 结果转换并赋值到pipeline中
        if(data_to_encode.detect_result_group.count > 0)
        {
            // 智能指针，离开其作用域时，它所指向的对象会被自动销毁
            std::unique_ptr<pipelineInfoMessageParam> tempdata(new pipelineInfoMessageParam);
            // 在这将dataEncode类型，转换为pipelineinfo类型
            // 目的在于更新pipelineinfo中的result_information和alarm_information
            for(auto object : data_to_encode.detect_result_group.results) 
            {
                if(object.prop == 0)
                    continue;
                objectInfo temp_object; // 这里将box的左上角坐标和右下角坐标转换为左上角坐标和宽高
                temp_object.x = object.box.left; // 注：坐标第二次转换
                temp_object.y = object.box.top;
                temp_object.w = object.box.right - object.box.left;
                temp_object.h = object.box.bottom - object.box.top;
                temp_object.track_id = object.track_id; // 将检测坐标赋值
                if(object.track_id == 0)
                {
                    continue;
                }
                temp_object.label = object.name;
                temp_object.score = object.prop; // 将检测结果类型和置信度赋值

                tempdata->pipelineinfo.alarm_date = get_time_ymdhmm();  // 字符串类型，包括毫秒和时区
                tempdata->pipelineinfo.result_information.result_object_list.push_back(temp_object);
                tempdata->pipelineinfo.result_information.result_num++;
                tempdata->pipelineinfo.alarm_information.alarm_object_list.push_back(temp_object);
                tempdata->pipelineinfo.alarm_information.alarm_num++;
            }

            if(tempdata->pipelineinfo.alarm_information.alarm_num != 0)
            {   
                // 管道里的摄像头信息也是实时更新的啊
                tempdata->pipelineinfo.source_image = data_to_encode.image.clone();
                camera_setting_to_node(data_to_encode.detect_result_group.id, tempdata->pipelineinfo);
                // 发送一个 message param  参数列表：(Type, topic, value, strategy) 
                status = CGRAPH_SEND_MPARAM(pipelineInfoMessageParam, "send-recv", tempdata, CGraph::GMessagePushStrategy::DROP);
            }
            // 是否开启本地图像的绘画显示，各种描述信息
            if(edgeI_data.global_osd_enable)
            {
                for(auto object : data_to_encode.detect_result_group.results)
                {
                    std::string label_name_str(object.name);

                    cv::putText(data_to_encode.image, label_name_str + " " + formatFloatValue(object.prop, 2),

                    cv::Point(object.box.left, object.box.top - 5), 0, 2, cv::Scalar(0, 0, 255), 8);
                    // cv::rectangle的参数需要传入两个坐标   cv::Rect的参数是左上角坐标和宽高
                    cv::rectangle(data_to_encode.image, cv::Rect(object.box.left, object.box.top, 
                        object.box.right - object.box.left, object.box.bottom - object.box.top), cv::Scalar(0, 0, 255), 8);
                }
            }

        }
#endif

// 用于将该帧添加到拼接帧的算法中
#ifdef RTSP_ENCODE_ENABLE
            // 问题：这里的id到底是指的是几号摄像头，还是指的是该输入流下的第几个帧  【初步认为是第几个摄像头的意思，因为addFrame的实现】
            p_encode_frame_concate->addFrame(data_to_encode.detect_result_group.id, data_to_encode.image);
#endif

#ifdef SHOW_LOCAL_ENABLE
        {
            if(std::any_of( edgeI_data.camera_frame_show_local.begin(), edgeI_data.camera_frame_show_local.end(), [](bool show_flag){return show_flag==true;} ))
            {
                if( edgeI_data.camera_frame_show_local[frame_source_index] == true )
                {
                    std::lock_guard<std::mutex> lock(m_frame_concate_mutex[frame_source_index]);
                    m_frame_concate_deque[frame_source_index].push_back(data_to_encode);
                }
            }
        }
#endif
    } // while循环

    // 释放线程
    for( auto &thread : frame_threads) // 用于检查线程对象是否表示一个可执行的线程
    {
        if(thread.joinable())
        {
            thread.join(); // 用于阻塞当前线程，直到被调用的线程执行完毕
        }
    }
    std::cout << "rk3588 node thread release" << std::endl;

    return CStatus();
}

CStatus RK3588Node::track(dataEncode &input_frame)
{
    // auto chan_id = input_frame.detect_result_group.id;
    // auto object_results = input_frame.detect_result_group.results;
    // int image_width = input_frame.image.cols;
    // int image_height = input_frame.image.rows;
    // std::vector<Object> objs;
    // objs.resize(input_frame.detect_result_group.count);
    // std::vector<STrack> obj_tracks;
    // for (int k = 0; k < input_frame.detect_result_group.count; ++k) {
    //     std::vector<std::string>::iterator label_index = std::find(edgeI_data.labels_string.begin(), edgeI_data.labels_string.end(), object_results[k].name);
    //     objs[k].label = label_index - edgeI_data.labels_string.begin();//object_results[k].class_id;
    //     objs[k].prob = object_results[k].prop;
    //     // ATTENTION:box point1(left, top) point2(right, bottom) 
    //     float x1 = object_results[k].box.left;
    //     float y1 = object_results[k].box.top;
    //     float x2 = object_results[k].box.right;
    //     float y2 = object_results[k].box.bottom;
    //     // std::cout << "get track x1 = " << x1 << " y1 = " << y1 << " x2 = " << x2 << " y2 = " << y2 << std::endl;
    //     objs[k].rect = cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2));
    // }
    // obj_tracks = m_tracker_per_channel[chan_id]->update(objs);
    // for(int object_track_index = 0; object_track_index < obj_tracks.size(); object_track_index++)
    // {
    //     std::vector<float> tlwh = obj_tracks[object_track_index].tlwh;
    //     if(tlwh[2] * tlwh[3] > 20)
    //     {
    //         input_frame.detect_result_group.results[object_track_index].track_id = obj_tracks[object_track_index].track_id;
    //         input_frame.detect_result_group.results[object_track_index].box.left = tlwh[0];
    //         input_frame.detect_result_group.results[object_track_index].box.top = tlwh[1];
    //         input_frame.detect_result_group.results[object_track_index].box.right = tlwh[2] + tlwh[0];
    //         input_frame.detect_result_group.results[object_track_index].box.bottom = tlwh[3] + tlwh[1];
    //         // std::cout << "obj_tracks[object_track_index].track_id = " << obj_tracks[object_track_index].track_id << std::endl;
    //         // std::cout << "******id = " << obj_tracks[object_track_index].track_id << " top = " << tlwh[0] << " left = " << tlwh[1] << " width = " << tlwh[2] << " height = " << tlwh[3] << std::endl;
    //     //     cv::putText(input_frame.image, cv::format("%d", obj_tracks[object_track_index].track_id),
    //     //                 cv::Point(tlwh[0], tlwh[1] - 5), 0, 2, cv::Scalar(0, 0, 255), 2);
    //     //     cv::rectangle(input_frame.image, cv::Rect(tlwh[0], tlwh[1], tlwh[2], tlwh[3]), cv::Scalar(0, 0, 255), 2);
    //     }
    //     // std::cout << "******id = " << obj_tracks[object_track_index].track_id << " top = " << tlwh[0] << " left = " << tlwh[1] << " width = " << tlwh[2] << " height = " << tlwh[3] << std::endl;
    // }
    // // cv::imwrite("result.jpg", input_frame.image);
    return CStatus();
}

// 功能：设置跟踪信息
CStatus RK3588Node::set_track_info(const std::map<int, std::string>& track_info, const int framerate_thres, const int trackbuffer_thres) {
    // m_track_info = track_info;
    // for (auto it = m_track_info.begin(); it != m_track_info.end(); ++it) {
    //     m_tracker_per_channel.insert(std::make_pair(it->first, std::make_shared<BYTETracker>(framerate_thres, trackbuffer_thres)));
    // }

    return CStatus();
}