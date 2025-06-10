###
 # @Author: Czhazha
 # @Date: 2023-03-14 15:22:59
 # @LastEditTime: 2023-07-28 16:45:49
 # @FilePath: /SeawayStream/run.sh
 # @Description: 
 # 
 # Copyright (c) 2023 by ${git_name_email}, All Rights Reserved. 
### 

# local env vs docker env
if [ $_SEAWAYOS_APP_ ];then
	echo "_SEAWAYOS_APP_" = $_SEAWAYOS_APP_
    CONFIG_JSON_PATH="/opt/seaway/"
else
	export _SEAWAYOS_APP_=test$_SEAWAYOS_APP_
	echo "_SEAWAYOS_APP_ is not exist, set _SEAWAYOS_APP_ as test"
    CONFIG_JSON_PATH="./config/"
fi
if [ $_SEAWAYOS_NAMESPACE_ ];then
	echo "_SEAWAYOS_NAMESPACE_" = $_SEAWAYOS_APP_
else
	export _SEAWAYOS_NAMESPACE_=test$_SEAWAYOS_NAMESPACE_
	echo "_SEAWAYOS_NAMESPACE_ is not exist, set _SEAWAYOS_NAMESPACE_ as test"
fi
echo $_SEAWAYOS_APP_
echo $_SEAWAYOS_NAMESPACE_
echo $CONFIG_JSON_PATH
# local env vs docker env

export GST_VIDEO_CONVERT_USE_RGA=1
export GST_VIDEO_FLIP_USE_RGA=1
export GST_MPP_NO_RGA=0


# LD_LIBRARY_PATH 是一个环境变量，它在程序运行时起作用,在编译链接时还是需要include_directories link_directories target_link_libraries
export LD_LIBRARY_PATH=/home/firefly/Documents/Seaway/SeawayStream/3rdparty/CGraph/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/home/firefly/Documents/Seaway/SeawayStream/3rdparty/glog/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/home/firefly/Documents/Seaway/SeawayStream/3rdparty/mqtt/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/home/firefly/Documents/Seaway/SeawayStream/3rdparty/openssl/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/home/firefly/Documents/Seaway/SeawayStream/3rdparty/jsoncpp/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/home/firefly/Documents/Seaway/SeawayStream/3rdparty/rdkafka/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/home/firefly/Documents/Seaway/SeawayStream/3rdparty/SeawayEdgeSDK/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/home/firefly/Documents/Seaway/SeawayStream/rk3588/lib:$LD_LIBRARY_PATH


quit=""
function killSubproc(){
    # 获取所有正在运行的后台子进程ID
    local pids=$(jobs -p -r)
    # 检查是否有子进程存在
    if [ -n "$pids" ]; then
        echo "Killing the following child processes: $pids"
        # 尝试终止所有子进程
        kill $pids
        # 等待所有子进程结束
        wait $pids
        # 检查每个子进程是否成功终止
        for pid in $pids; do
            if kill -0 $pid 2>/dev/null; then
                echo "Process $pid did not terminate, forcing kill..."
                kill -9 $pid  # 强制终止未成功终止的子进程
            fi
        done
    fi
    # 标记为已退出
    quit="quit"
}

# 捕捉信号并执行命令 当脚本接收到 INT 或 TERM 信号时，会执行 killSubproc 函数
trap killSubproc INT TERM

# 打印当前时间，并在前面加上 start 标记
date +"start "%H:%M:%S.%N

# 使用命令行参数指定执行程序，默认使用 seawaystream，
# ${1:-seawaystream} 是一种参数扩展形式，它的作用是：
#     如果 $1 存在且不为空，则 EXEC_PROGRAM 被赋值为 $1 的值；（执行时传入的第一个参数 例如： ./run.sh myseawaypro）
#     如果 $1 不存在或者为空，则 EXEC_PROGRAM 被赋值为 seawaystream
EXEC_PROGRAM=${1:-seawaystream}
# 用于检查变量 quit 是否为空字符串（zero）
while test -z "$quit"; do
    # 执行程序和参数列表【算法配置路径和检测标签路径】 路径不是完整路径 ，
    # &这个符号表示将命令放到后台执行，这样脚本不会等待该命令执行完毕就会继续执行后续的代码
    # 类同： ./seawaystream ./config/  ./rk3588/resources/config/   【区分/rk3588/resources/config/  这是绝对路径】
    #./$EXEC_PROGRAM $CONFIG_JSON_PATH rk3588/resources/config/ &
    # # 将最后一个后台子进程的 PID 赋值给变量 child
    # child=$!
    # # 等待子进程child结束
    # wait "$child"
    gdb -ex "run $CONFIG_JSON_PATH rk3588/resources/config/" -ex "bt" -batch ./$EXEC_PROGRAM
    read -p "程序已崩溃，按任意键继续重试或 Ctrl+C 退出..." -n 1 -r
    echo

done

date +"end   "%H:%M:%S.%N