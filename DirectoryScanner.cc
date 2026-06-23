#include <iostream>
#include<my_header.h>
#include "DirectoryScanner.h"
using namespace std;
std::vector<std::string> DirectoryScanner::GetFiles(const std::string& path){
    auto deep_fd=opendir(path.c_str());
    if(deep_fd==nullptr)
    {return {};}
    vector<std::string> files;
    while(auto name=readdir(deep_fd)){
        if(name->d_type==DT_REG){
            files.push_back(name->d_name);
        }else if(name->d_type==DT_DIR){
            //.和..的type也是DIR，需要排除
            if(strcmp(name->d_name,".")==0||strcmp(name->d_name,"..")==0){
                continue;
            }
            string sub_path=path+"/"+name->d_name;
            vector<std::string> sub_files=GetFiles(sub_path);
            files.insert(files.end(),sub_files.begin(),sub_files.end());
        }
    }
    closedir(deep_fd);
    return files;
}
std::vector<std::string> DirectoryScanner::scan(const std::string& path){
    auto dir_fd=opendir(path.c_str());
    if(dir_fd==nullptr)
    {return {};}
    std::vector<std::string> files;
    while(auto name=readdir(dir_fd)){
        if(name->d_type==DT_REG)
        {
            files.push_back(name->d_name);
        }else {
            continue;
        }
    }
    closedir(dir_fd);
    return files;  
}
