#include <iostream>
#include<vector>
#include<string>
#include"DirectoryScanner.h"
#include"KeywordProcessor.h"
#include"PageProcessor.h"
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
    testrun2();

    return 0;
}

