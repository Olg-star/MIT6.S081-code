#include "kernel/types.h"
#include"kernel/param.h"
#include "user/user.h"
#define MAXSIZE 512
void copy(char** argv_add,char* p,int len){
    *argv_add = malloc(len+1);//留一个空格
    memmove(*argv_add,p,len);
}
int readline(char** argvs,int index){
    int end=index;
    char buf[MAXSIZE];
    int j=0;
    while(read(0,buf+j,1)){//读取一行
	//发现必须每次只读取一个字符，否则读取的顺序不一致，怀疑是进程切换问题,通过让该进程sleep可查看
        if(buf[j]=='\n'){
            buf[j]=0;
            j++;
            break;
        }
        else{
            j++;
            if(j>=MAXSIZE){
                fprintf(2,"parameter is too long\n");
                return -1;
            }
        }
    }
    //通过空格分割参数
    int begin=0,finish=0;
    int k=0;
    while(k<j){
        while(k<j && buf[k]==' ')k++;
        begin=k;
        while(k<j && buf[k]!=' ')k++;
        finish=k-1;
        copy(&argvs[end],buf+begin,finish-begin+1);
        end++;
    }
    if(end==index)return -1;
    else return end;

}
int main(int argc,char* argv[]){

    if(argc<2){
        fprintf(2,"Usage:xargs command ...\n");
        exit(1);
    }

    char* argvs[MAXARG];//存取参数
    int index=0,i;
    for(i=1;i<argc;i++){
        copy(&argvs[index],argv[i],strlen(argv[i]));//虽说可以直接char argvs[][],但希望通过动态分配来更合理规划内存
        index++;
    }

    int end;
    while((end=readline(argvs,index))>0){
        argvs[end]=0;
        int pid=fork();
        if(pid==0){
            exec(argvs[0],argvs);
            exit(0);
        }
        else if(pid>0){
            wait(0);
        }
        else{
            fprintf(2,"fork() error\n");
            exit(1);
        }
    }
    exit(0);
}