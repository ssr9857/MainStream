#ifndef RKNNPOOL_H
#define RKNNPOOL_H

#include "ThreadPool.hpp"
#include <vector>
#include <iostream>
#include <chrono>
#include <mutex>
#include <queue>
#include <memory>

// 注： 该模板类的三个模板参数并不代表构造函数的三个参数传入
// 实例化举例：rknnPool<rkYolov5s, inputData, dataEncode> 

// 传入的是一个个推理任务，推理任务和线程池中的线程并没有一一对应关系，只要有新的任务就让空闲的线程去处理，要区分任务队列和线程池。

// 功能： 定义rknn线程池类
template <typename rknnModel, typename inputType, typename outputType>
class rknnPool
{
    public:
        rknnPool(const std::string modelPath, const std::string labelPath, int threadNum); // 构造函数

        int init(); // 初始化

        int put(inputType inputData); // 将推理任务添加进

        int get(outputType &outputData); // 获取任务推理的结果

        ~rknnPool();

    protected:
        int getModelId();

    private:
        int threadNum;
        std::string labelPath;
        std::string modelPath;

        long long id;
        std::mutex idMutex, queueMutex;
        std::unique_ptr<dpool::ThreadPool> pool; // 线程池
        std::queue<std::future<outputType>> futures; // 结果队列
        std::vector<std::shared_ptr<rknnModel>> models; // 智能指针数组，每个数组指向一个rknn模型
        int queue_thresh = 50 ; // 结果队列阈值

};


// 功能： 构造函数：给成员变量赋值，初始化模型和标签的路径、线程个数
template <typename rknnModel, typename inputType, typename outputType>
rknnPool<rknnModel, inputType, outputType>::rknnPool(const std::string modelPath, const std::string labelPath, int threadNum)
{
    this->modelPath = modelPath;
    this->labelPath = labelPath;
    this->threadNum = threadNum;
    this->id = 0;
}

// 功能：初始化线程池和模型实例
template <typename rknnModel, typename inputType, typename outputType>
int rknnPool<rknnModel, inputType, outputType>::init()
{
    std::cout << "rknnPool init start"  << std::endl;
    // try catch用于异常捕获，try: 可能抛出异常的代码  catch:执行代码处理对应异常
    // 若内存分配失败，会抛出 std::bad_alloc 异常
    try{
        // 创建线程池实例，根据传入的线程个数参数创建rkYolov5推理
        this->pool = std::make_unique<dpool::ThreadPool>(this->threadNum); // 创建一个线程池实例，后面是传入的参数，调用线程池的构造函数
        for(int i = 0; i < threadNum ; i++)
            models.push_back(std::make_shared<rknnModel>(this->modelPath.c_str(), this->labelPath.c_str()));
             // 创建一个模型实例，传入的参数，调用rkYolov5s的构造函数
    }
    catch(const std::bad_alloc &e){
        std::cout << "Out of memory" << e.what() <<std::endl;
        return -1;
    }
    for(int i = 0, ret = 0; i < threadNum ;  i++)
    {
        // 原来在这调用了rkYolov5.cpp中的init函数
        ret = models[i]->init(models[0]->get_pctx(), i != 0); // 对模型初始化，从第二个开始共享权重（模型上下文，是否共享）
        if(ret != 0)
            return ret;
    }
    std::cout << "rknnPool init finish" << std::endl;
    return 0;
}

// 功能：获取模型ID  id在构造函数已经初始化
template <typename rknnModel, typename inputType, typename outputType>
int rknnPool<rknnModel, inputType, outputType>::getModelId()
{
    std::unique_lock<std::mutex> lock(idMutex);
    int modelId = id % threadNum;
    id++;
    return modelId;
}

// 功能：用于管理模型推理任务, 借助线程池并行处理推理任务，同时用队列futures存储推理结果
template <typename rknnModel, typename inputType, typename outputType>
int rknnPool<rknnModel, inputType, outputType>::put(inputType inputdata)
{
    std::unique_lock<std::mutex> lock(queueMutex); // 对队列上锁
    if(futures.size() >= queue_thresh)
    {
        std::cout << "input data is too much ,please reduce the input" << std::endl;
        std::queue<std::future<outputType>> temp_empty_queue;
        swap(temp_empty_queue, futures); // 交换两个队列的内容，存储数据
        return 0;
    }
    // 队列，使用push  
    // 加入类的函数格式：（类的函数，类的实体，函数参数）  models[this->getModelId()]本身就是指针
    // 加入函数的格式：（函数，函数参数）
    futures.push(pool->submit(&rknnModel::infer, models[this->getModelId()], inputdata)); // 提交时就触发推理
    return 0;
}

// 功能：从队列中获取推理结果，并将其存储到outputData中
// std::queue<std::future<outputType>> futures; 所以定义futures_data是为了取队列头元素的
template <typename rknnModel, typename inputType, typename outputType>
int rknnPool<rknnModel, inputType, outputType>::get(outputType &outputdata)
{
    std::future<outputType> futures_data; // 定义推理结果结构体
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        if(futures.empty() == true)
            return 1;

        if(futures.size() >= queue_thresh)
        {
            std::queue<std::future<outputType>> temp_empty_queue; // 空结果队列
            swap(temp_empty_queue, futures); // 为了高效清空futures,避免了逐个元素处理的开销
            return 1;
        }
        futures_data = std::move(futures.front()); // 左值强制转换为右值引用，资源的所有权转移
        futures.pop();
    }
    if(!futures_data.valid())
        return 1;
    else    
        outputdata = futures_data.get(); // 将从futures中读取的结果保存  .get()用于获取futures队列的结果
    return 0;
}

// 功能：析构函数
template <typename rknnModel, typename inputType, typename outputType>
rknnPool<rknnModel, inputType, outputType>::~rknnPool()
{
    while(!futures.empty()) // 如果结果队列不为空
    {
        outputType temp = futures.front().get();
        futures.pop();
    }
}
#endif




