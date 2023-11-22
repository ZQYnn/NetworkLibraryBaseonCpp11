cmake_minimum_required(VERSION 2.5)
project(mymuduo)

# mymuduo 最终编译的so动态库， 设置动态库的路径 放在根目录的lib下
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 设置调试信息
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11")

#定义参与编译的源代码文件  将当前根目录下的源文件的名字组合起来放到 SRC_LIST 中
aux_source_directory(./src SRC_LIST)

#编译生成 动态库mymuduo 
add_library(mymuduo SHARED ${SRC_LIST})

add_library(mymuduo)

