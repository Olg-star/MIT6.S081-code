#include"kernel/types.h"
#include"user/user.h"
int main(){
    int P[2];//parent->son
    int S[2];//son->parent
    char buf[1];
    if(pipe(P)<0){
        fprintf(2,"pipe error\n");
        exit(1);
    }
    if(pipe(S)<0){
        fprintf(2,"pipe error\n");
        exit(1);
    }   

    int pid = fork();
    if(pid<0){
        fprintf(2,"fork error\n");
        exit(1);
    }
    else if(pid==0){//son
        close(S[0]);
        close(P[1]);
        if(read(P[0],buf,1)<0){
            fprintf(2,"son read error\n");
            exit(1);
        }
        close(P[0]);
        printf("%d: received ping\n",getpid());
        if(write(S[1]," ",1)<0){
            fprintf(2,"son write error\n");
            exit(1);
        }

        close(S[1]);
        exit(0);
    }
    else{//parent
        close(P[0]);
        close(S[1]);
        if(write(P[1]," ",1)<1){
            fprintf(2,"parent write error\n");
            exit(1);
        }
        close(P[1]);

        if(read(S[0],buf,1)<1){
            fprintf(2,"parent read error\n");
            exit(1);
        }
        printf("%d: received pong\n",getpid());
        close(S[0]);
        exit(0);
    }
}