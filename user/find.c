#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
void find(char *path, char *name)
 {

    int fd;
    if ((fd = open(path, 0))< 0)
    {
        fprintf(2, "open error\n");
        return;
    }

    struct stat st;
    if (fstat(fd, &st) < 0)
    {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    char buf[128], *p;//注意buf不能开太大，否则容易爆内存，xv6最大内存两百多MB
    struct dirent de;


    int len=strlen(path)-strlen(name);
    switch (st.type)
    {
    case T_FILE:     
        if(len>=0 && strcmp(path+len,name)==0){
            printf("%s\n",path);
        }
        break;
    case T_DIR:
        if (strlen(path) + 2 + DIRSIZ > sizeof buf)
        {
            fprintf(2, "path is too long!\n");
            break;
        }
        strcpy(buf, path);
        p = buf + strlen(path);
        *p++ = '/';
        while(read(fd,&de,sizeof(de))==sizeof(de)){
            if(de.inum==0 || (strcmp(de.name,".")==0) || (strcmp(de.name,"..")==0))
                continue;

            memmove(p,de.name,DIRSIZ);
            p[DIRSIZ]=0;

            if(stat(buf,&st)<0){
                fprintf(2, "find: cannot stat %s\n", buf);
                continue;
            }
            if(st.type==T_FILE){//避免多一次的递归
                if(strcmp(de.name,name)==0){
                    printf("%s\n",buf);
                }
            }
            else if(st.type==T_DIR)
                find(buf,name);
        }
        break;
    }
    close(fd);
}
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(2, "Usage: find path filename...\n");
        exit(1);
    }

    int i;
    for(i=2; i < argc; i++)
    {
        find(argv[1], argv[i]);
    }
    exit(0);
}