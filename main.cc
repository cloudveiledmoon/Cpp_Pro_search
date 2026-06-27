#include <iostream>
#include<vector>
#include<string>
#include"DirectoryScanner.h"
#include"KeywordProcessor.h"
#include"PageProcessor.h"
#include"serverMine.h"

using namespace std;

void print(const vector<string>& v){
    for(auto i:v){
        cout<<i<<" ";
    }
    cout<<endl;
}
void testrun(){
    KeywordProcessor kp;
    kp.process("./corpus/CN","./corpus/EN");
}

void testrun2(){
    PageProcessor pp;
    pp.process("./corpus/webpages");
}
int main(int argc, char * argv[]){
    //testrun();
    //testrun2();
    muduo::Logger::setLogLevel(muduo::Logger::ERROR);
    muduo::net::EventLoop loop;
    muduo::net::InetAddress listenAddr(2947);
    ServerMine server(&loop, listenAddr, "ServerMine");
    server.start();
    loop.loop();
    return 0;
}

