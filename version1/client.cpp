#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>


static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

int main() {
    //获取套接字
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }
    

    //连接到服务器
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);  // 127.0.0.1, 表示连接到本地主机。
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv) {
        die("connect");
    }

    // 写信息
    char msg[] = "hello";
    /*write() writes up to count bytes from the buffer starting 
    at buf to the file referred to by the file descriptor fd.
    On success, the number of bytes written is returned.  On error,
    -1 is returned, and errno is set to indicate the error.*/
    write(fd, msg, strlen(msg));

    //接收信息
    /*read() attempts to read up to count bytes from file 
    descriptor fdinto the buffer starting at buf.
    On success, the number of bytes read is returned (zero indicates
    end of file), and the file position is advanced by this number.*/
    char rbuf[64] = {};
    ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        die("read");
    }
    printf("server says: %s\n", rbuf);

    //关闭套接字
    close(fd);
    return 0;
}