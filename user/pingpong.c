#include "kernel/types.h"
#include "user.h"

int main(int argc, char* argv[]) {


    int c2f[2]; 
    int f2c[2]; 
    pipe(c2f);
    pipe(f2c);
    char buf[50];

    if (fork() == 0) { 
        char pid_c[10];
        itoa(getpid(),pid_c);
        close(f2c[1]); 
        close(c2f[0]); 
        read(f2c[0], buf, sizeof(buf));
        close(f2c[0]);
        printf("%d: received ping from pid %d\n", getpid(), atoi(buf));
        write(c2f[1], pid_c, strlen(pid_c) + 1);
        close(c2f[1]);
    } else { //父线程
        close(f2c[0]); 
        close(c2f[1]); 
        char pid_f[10];
        itoa(getpid(),pid_f);//字符串类型
        write(f2c[1], pid_f, strlen(pid_f) + 1);
        close(f2c[1]);
        read(c2f[0], buf, sizeof(buf));
        close(c2f[0]);
        printf("%d: received pong from pid %d\n", getpid(), atoi(buf));
    }

    exit(0);
}