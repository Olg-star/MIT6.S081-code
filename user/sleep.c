#include "kernel/types.h"
#include "user/user.h"
int main(int argc,char* argv[]){
    if(argc!=2){
        fprintf(2,"Usage:sleep number\n");
        exit(1);
    }
    int time = atoi(argv[1]);
    if(time<0){
        fprintf(2,"Usage:sleep number\n");
        exit(1);
    }
    sleep(time);
    exit(0);
}