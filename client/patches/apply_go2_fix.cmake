# 应用 Go2 兼容补丁（幂等）：机器狗不回 DCEP ACK，直接开始发数据。
# libdatachannel 的 flushPendingMessages() 在通道未 open 时会丢弃收到的消息，
# 导致 onOpen/onMessage 永远不触发。此补丁在收到首条数据时将通道置为已打开。
# 用法: cmake -DSRC_DIR=<libdatachannel 源码目录> -P apply_go2_fix.cmake

if(NOT DEFINED SRC_DIR OR SRC_DIR STREQUAL "")
    message(FATAL_ERROR "apply_go2_fix.cmake 需要 -DSRC_DIR=<libdatachannel 源码目录>")
endif()

set(F "${SRC_DIR}/src/impl/datachannel.cpp")
if(NOT EXISTS "${F}")
    message(FATAL_ERROR "apply_go2_fix.cmake 找不到 ${F}")
endif()

file(READ "${F}" content)

if(content MATCHES "go2-fix")
    message(STATUS "Go2 DataChannel 补丁已应用，跳过")
    return()
endif()

set(OLD "\t\tmRecvQueue.push(message);")
set(NEW "\t\t// go2-fix: 机器狗不回 DCEP ACK，收到数据即视为通道已打开，\n"
        "\t\t// 否则 Channel::flushPendingMessages 会因 mOpenTriggered==false 丢弃消息\n"
        "\t\tif (!mIsOpen.exchange(true)) {\n"
        "\t\t\ttriggerOpen();\n"
        "\t\t}\n"
        "\t\tmRecvQueue.push(message);")

string(FIND "${content}" "${OLD}" _pos)
if(_pos EQUAL -1)
    message(FATAL_ERROR "apply_go2_fix.cmake 锚点未找到，libdatachannel 版本可能不兼容")
endif()

string(REPLACE "${OLD}" "${NEW}" content "${content}")
file(WRITE "${F}" "${content}")
message(STATUS "Go2 DataChannel 补丁已应用到 ${F}")
