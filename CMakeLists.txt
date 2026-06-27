cmake_minimum_required(VERSION 3.10)
project(MyProject)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ../)

# 查找依赖库（如 muduo）
find_package(tinyxml2 REQUIRED)

# 添加可执行文件，源文件用空格分隔
add_executable(server main.cc KeywordProcessor.cc DirectoryScanner.cc PageProcessor.cc serverMine.cc cacheService.cc)

# 链接需要的库
target_link_libraries(server
 muduo_net 
 muduo_base 
 pthread 
 tinyxml2 

 )