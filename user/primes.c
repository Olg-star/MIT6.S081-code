#include "kernel/types.h"
#include"user/user.h"
void processor(int fd[2]){
    close(fd[1]);
    int prime;
    if(read(fd[0],&prime,sizeof(int))==0){
        close(fd[0]);
        return;
    }

    int fd1[2];
    pipe(fd1);
   
    int pid=fork();
    if(pid==0){
        close(fd[0]);
        processor(fd1);
    }
    else if(pid>0){
        close(fd1[0]);//注意这一步必须在fork后才关掉，如果在fork前会被拷贝给子进程，使其无法读数据

        printf("prime %d\n",prime);
        int number;
        while(read(fd[0],&number,sizeof(number))==sizeof(number)){
            if(number%prime){
                write(fd1[1],&number,sizeof(int));
            }
        }
        close(fd[0]);
        close(fd1[1]);
        wait(0);
    }
    else if(pid<0){
        close(fd1[0]);
        fprintf(2,"fork error\n");
        close(fd1[1]);
    }
}
int main(){
    int fd[2];
    pipe(fd);

    int i;


    int pid=fork();
    if(pid==0){
        processor(fd);
    }
    else if(pid>0){
        close(fd[0]);//注意这一步必须在fork后才关掉，如果在fork前会被拷贝给子进程，使其无法读数据
        printf("prime 2\n");
        for(i=3;i<=35;i++){
            if(i%2){
                write(fd[1],&i,sizeof(i));
            }
        }
        close(fd[1]);//需要在wait前关掉，否则子进程一直在等待读数据
        wait(0);
        
    }
    else if(pid<0){
        close(fd[0]);
        fprintf(2,"fork error\n");    
        close(fd[1]);
    }
    exit(0);
}