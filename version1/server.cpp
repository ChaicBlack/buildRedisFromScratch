#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>


static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}


//如果发生错误，die将会打印错误信息并退出
static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}


//这段代码是一个基本的服务器端处理函数，它从客户端读取数据，打印客户端的消息，并回复 "world"。
static void do_something(int connfd) {
    char rbuf[64] = {};//rbuf: 读取缓冲
    //使用 read 函数从与客户端连接的套接字 connfd 读取数据，读取的数据存储在 rbuf 中
    //read 函数返回读取的字节数，如果出现错误，返回一个负数。
    ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {  //如果读取错误，会调用 msg 函数来输出错误消息。
        msg("read() error");
        return;
    }
    printf("client says: %s\n", rbuf);

    char wbuf[] = "world";//wbuf: 写入缓冲
    write(connfd, wbuf, strlen(wbuf));
}




int main() {
    //socket，又叫套接字
    //系统socket()调用返回一个fd。fd 是一个整数，它引用 Linux 内核中的某些内容，
    //例如 TCP 连接、磁盘文件、侦听端口或其他一些资源等。
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }




    // this is needed for most server applications
    //这段代码是使用 setsockopt 函数来设置套接字选项（Socket Option）
    int val = 1; //val=1表示要去启用SO_REUSEADDR
    //setsockopt: 用于设置套接字选项
    //SOL_SOCKET: 指定要设置的选项属于套接字层。
    //SO_REUSEADDR: 允许重新使用处于 TIME_WAIT 状态的套接字地址。
    //TIME_WAIT 状态是 TCP 协议中的一部分，用于确保可靠的数据传输。
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));



    // bind
    //这段代码用于在服务器端进行套接字绑定和监听，以便开始接受客户端连接请求。
    struct sockaddr_in addr = {}; //sockaddr_in结构体是用于 IPv4 地址的标准结构体。
    addr.sin_family = AF_INET;    //AF_INET: IPv4 Internet protocols
    //ntohl 函数用于将 IP 地址从主机字节顺序转换为网络字节顺序。
    addr.sin_port = ntohs(1234);  //设置 addr 结构体的端口号为 1234。
    addr.sin_addr.s_addr = ntohl(0); //设置 addr 结构体的 IP 地址为通配地址 0.0.0.0
    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));//使用 bind 函数将套接字 fd 绑定到指定的地址。
    if (rv) {
        die("bind()");//如果 bind 函数失败，将返回一个非零值，此时代码会调用 die("bind()") 函数来处理错误。
    }



    // listen
    //使用 listen 函数开始监听套接字 fd 上的连接请求。
    rv = listen(fd, SOMAXCONN);//SOMAXCONN 是一个常量，表示系统允许的最大挂起连接请求队列的长度。
    if (rv) {
        die("listen()");
    }

    while (true) {
        // accept
        struct sockaddr_in client_addr = {};
        socklen_t socklen = sizeof(client_addr);
        //accept：这是一个系统调用或库函数，用于接受来自客户端的连接请求。它在服务器端套接字上调用，以等待客户端的连接请求。一旦有客户端连接请求到达，accept 将创建一个新的套接字，用于与客户端通信。
        //(struct sockaddr *)&client_addr: 这是一个指向 sockaddr 结构体的指针，用于存储客户端的地址信息。在 accept 成功执行后，该结构体将被填充为客户端的地址信息，包括 IP 地址和端口号。
        //&socklen：这是一个指向 socklen_t 类型的指针，用于指定 sockaddr 结构体的长度。在 accept 被调用之前，您应该将其设置为 sockaddr 结构体的长度。一旦 accept 返回，socklen 将包含实际填充的结构体的长度。
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
        if (connfd < 0) {
            continue;   // error
        }

        do_something(connfd);
        close(connfd);
    }

    return 0;
}