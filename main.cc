#include <iostream>
#include<vector>
#include<string>
#include"DirectoryScanner.h"
#include"KeywordProcessor.h"
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
int main(int argc, char * argv[]){
    testrun();
    return 0;
}

