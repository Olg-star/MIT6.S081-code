// Shell.

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10

struct cmd {//统一接口
  int type;//上面五个宏
};

struct execcmd {//最基本的命令
  int type;//上面五个宏
  char *argv[MAXARGS];//代表相应的字符串开始的内存位置
  char *eargv[MAXARGS];//代表相应的字符串结束的内存位置
};

struct redircmd {//代表相应的字符串开始的内存位置
  int type;
  struct cmd *cmd;//实际要执行的命令
  char *file;//重定向的文件名在内存中的其实位置
  char *efile;//代表文件名在内存中的结束位置
  int mode;//打开模式
  int fd;//fd代表重定向要替换的文件描述符，可以取0或1，代表是输入重定向或者是输出重定向。
};

struct pipecmd {//管道命令
  int type;
  struct cmd *left;//左命令是提供管道输入的命令
  struct cmd *right;//右命令是管道输出的命令
};

struct listcmd {//并列命令,可以把多个命令合成一个命令发送给shell，命令之间以;间隔，shell会分别执行。
                //例如echo hello; echo world这种形式的命令。
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {//后台命令,在命令的最后面加上&，代表放到后台执行
  int type;
  struct cmd *cmd;//实际要执行的cmd命令
};

int fork1(void);  // Fork but panics on failure.
void panic(char*);
struct cmd *parsecmd(char*);

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    exit(1);

  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit(1);
    exec(ecmd->argv[0], ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    if(open(rcmd->file, rcmd->mode) < 0){
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    if(fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    if(pipe(p) < 0)
      panic("pipe");
    if(fork1() == 0){
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if(fork1() == 0){
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    if(fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}

int
getcmd(char *buf, int nbuf)
{
  fprintf(2, "$ ");
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}

int
main(void)
{
  static char buf[100];
  int fd;

  // Ensure that three file descriptors are open.
  while((fd = open("console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0){
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      // Chdir must be called by the parent, not the child.
      buf[strlen(buf)-1] = 0;  // chop \n
      if(chdir(buf+3) < 0)
        fprintf(2, "cannot cd %s\n", buf+3);
      continue;
    }
    if(fork1() == 0)//创建shell副本，父进程执行wait，子进程运行命令
      runcmd(parsecmd(buf));
    wait(0);
  }
  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd*)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)//把一段字符串提取出来，一般是用来提取基本命令或者重定向的文件名
{
 
  char *s;
  int ret;

  s = *ps;
  while(s < es && strchr(whitespace, *s))//让s指向第一个非空字符
    s++;
  if(q)
    *q = s;//q指向字符串起始位置
  ret = *s;//第一个非空字符的ASCII值
  switch(*s){
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if(*s == '>'){
      ret = '+';
      s++;
    }
    break;
  default://如果遇到实际命令，则返回’a’，代表这是一个真实的基本命令。
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))//*s非空格且非"<|>&;()"其中一个
      s++;
    break;
  }
  if(eq)
    *eq = s;// //eq指向结束位置

  while(s < es && strchr(whitespace, *s))//将s指向一个非空格字符
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while(s < es && strchr(whitespace, *s))//将s指向第一个非空位置
    s++;
  *ps = s;//将*ps指向第一个非空位置
  return *s && strchr(toks, *s);//*s非空 且 *s是toks里的字符
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);
struct cmd *nulterminate(struct cmd*);

struct cmd*
parsecmd(char *s)//命令构造函数，它简单地把工作转交给parseline()函数。
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);//es指向输入字符串的结尾位置，即*es=0
  cmd = parseline(&s, es);

  //检查输入字符串从头开始除空格外的第一个字符是否是给定的字符范围中的一个，返回true或者false，同时移动字符指针指向第一个非空格字符。
  peek(&s, es, "");

  if(s != es){//判断是否到达输入字符串的末端，否则报错
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)//处理一行的输入字符串，把它转化成命令
{
  struct cmd *cmd;

//先以管道为单位划分输入命令字符串，主要的工作都转交给parsepipe()完成。
//parsepipe()里可以处理<>()这几种字符
  cmd = parsepipe(ps, es);

//&;则在parseline()里完成，判断是否有并列命令与列表命令。
  while(peek(ps, es, "&")){
    gettoken(ps, es, 0, 0);//一个词法提取函数，用户提取每一个子命令
    cmd = backcmd(cmd);
  }
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }

//这里gettoken()只是简单地把代表并列命令的;以及后台命令的&跳过，让字符指针指向下一个非空格字符，并没有抽取字符串。
  return cmd;
}

struct cmd*
parsepipe(char **ps, char *es)//把命令以|分成两个个子命令，先构造左边的命令，右边的命令则通过递归调用自身来构造。
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){//先判断是否有重定向参数，如果没有，则不作任何处理。如果有，则将子命令包装成listcmd。
    tok = gettoken(ps, es, 0, 0);
    //经过gettoken()的词法提取后，q和eq分别指向重定向文件名的起始与结束的内存位置，它们都作为参数构造器listcmd。
    if(gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch(tok){
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE|O_TRUNC, 1);
      break;
    case '+':  // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd*
parseblock(char **ps, char *es)//每一个()里面的命令都可以看作一个整体命令，因此主要是递归地把工作转交给parseline()
{
  struct cmd *cmd;

  if(!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if(!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);//()内的整体命令可能跟着重定向参数，因此这里也需要用parseredirs()看能不能包装成redircmd。
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)//构造最基本的命令
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if(peek(ps, es, "("))//如果遇到()的话则把工作转交给parseblock()，否则说明是基本命令
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|)&;")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;//开始位置
    cmd->eargv[argc] = eq;//结束位置
    argc++;
    if(argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);//把execcmd作为子命令传递给parseredirs()函数，看能不能构造redircmd
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
// 在execcmd里用eargv数组保存每一个命令参数的字符串结束位置；在redircmd里用efile保存了重定向文件名的字符串结束位置。
// nulterminate()就是给每一个结束位置指针指向的内存标识上字符串的结束标志\0。
// 让这些参数都成为完整的字符串，那么在命令执行时候就能够正确处理。
struct cmd*
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    return 0;

  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
