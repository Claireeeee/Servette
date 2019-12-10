// 此头文件用于处理响应
//#include <string.h>
//#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
//#include <bits/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>

#include "config.h"
#include "header.h"
#include "urldecode.h"
#include "http_header_utils.h"

// 上传下载文件夹位置
extern char *file_base_path;

/*
Description:
    读取html文件，并显示在网页上
Parameters:
    int client_sock [IN] 客户端的socket
    char *file: [IN] 解析的html文件名
Return:
    NULL
*/
void response_webpage(int client_sock, char *file)
{
    int fd;
    char buf[MAX_SIZE], header[MAX_SIZE];
    int size = -1;
    if (strcmp(file, "/") == 0)
    {
        //如果没有指定文件，则默认打开index.html
        sprintf(file, "%s%s", HTML_DIR, DEFAULT_FILE);
    }
    else
    {
        char new_file[NAME_LEN];
        sprintf(new_file, "%s%s", HTML_DIR, file);
        file = new_file;
    }
    //读取文件
    fd = open(file, O_RDONLY);
    if (fd == -1)
    {
        //打开文件失败时构造404的相应
        construct_header(header, 404, "text/html");
        write(client_sock, header, strlen(header));
        return;
    }
    else
    {
        const char *type = get_type_by_name(file);
        construct_header(header, 200, type);
        write(client_sock, header, strlen(header));
        //write(client_sock,echo_str,strlen(echo_str));
        while (size)
        {
            //size代表读取的字节数
            size = read(fd, buf, MAX_SIZE);
            if (size > 0)
            {
                write(client_sock, buf, size);
            }
        }
    }
}

/*
Description:
    对于无法解析的请求，暂用作回声服务器
Parameters:
    int client_sock [IN] 客户端的socket
    char *msg: [IN] 消息内容
Return:
    NULL
*/
void response_echo(int client_sock, char *msg)
{
    write(client_sock, msg, strlen(msg));
}

/*
Description:
    实现简单的CGI功能,真实的CGI复杂得多
    处理列出文件列表的请求/?cgi-bin=[一个目录]
Parameters:
    int client_sock [IN] 客户端的socket
    char *arg: [IN] 解析的参数
Return:
    NULL
*/
void response_cgi(int client_sock, char *arg)
{
    int size = -1;
    char html[MAX_SIZE], header[MAX_SIZE];
    char dir_name[NAME_LEN];
    if (sscanf(arg, "/?cgi-bin=%s", dir_name) == EOF)
    {
        //匹配失败！
        printf("error:%s\n", arg);
        construct_header(header, 404, "text/html");
        write(client_sock, header, strlen(header));
        return;
    }
    else
    {
        //调用CGI程序，完成文件列表显示页面的构造
        char cmd[MIDDLE_SIZE];
        strncpy(cmd, "cgi-bin/filelist ", sizeof(cmd) - 1);
        strncat(cmd, dir_name, sizeof(cmd) - strlen(cmd) - 1);
        strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1);
        strncat(cmd, file_base_path, sizeof(cmd) - strlen(cmd) - 1);
        FILE *fp = popen(cmd, "r");
        if (!fp)
        {
            //匹配失败！
            printf("error:%s\n", arg);
            construct_header(header, 404, "text/html");
            write(client_sock, header, strlen(header));
            return;
        }
        construct_header(header, 200, "text/html");
        write(client_sock, header, strlen(header));
        while (fgets(html, sizeof(html), fp) != NULL)
        {
            // 这里可能还有错误，写的时候对端的TCP连接已经关闭了
            // 会引起servette的异常退出

            int i = write(client_sock, html, strlen(html) - 1);
            //注意sizeof("\r\n") == 3，算上了结尾的'\0'
            i = write(client_sock, "\r\n", sizeof("\r\n") - 1);
        }
        pclose(fp);
    }
}

/*
Description:
    处理下载文件的请求/?download=[文件名]
    目前暂时弃用此函数
Parameters:
    int client_sock [IN] 客户端的socket
    char *arg: [IN] 解析的参数
Return:
    NULL
*/
void response_download(int client_sock, char *arg)
{
    int fd;
    int size = -1;
    char buf[MAX_SIZE], header[MAX_SIZE];
    char file_name[NAME_LEN];
    if (sscanf(arg, "/?download=%s", file_name) == EOF)
    {
        //匹配失败！
        printf("error:%s\n", arg);
        construct_header(header, 404, "text/html");
        write(client_sock, header, strlen(header));
        return;
    }
    fd = open(file_name, O_RDONLY);
    if (fd == -1)
    {
        construct_header(header, 404, "text/html");
        write(client_sock, header, strlen(header));
        return;
    }
    //构建下载的相应头部
    printf("\tdownloading %s\n", file_name);
    construct_download_header(header, 200, file_name);
    write(client_sock, header, strlen(header));
    while (size)
    {
        //size代表读取的字节数
        size = read(fd, buf, MAX_SIZE);
        if (size > 0)
        {
            send(client_sock, buf, size, 0);
        }
    }
    printf("\n");
}

/*
Description:
    处理下载文件的请求/?download=[文件名]
    以chunk分块传输
Parameters:
    int client_sock [IN] 客户端的socket
    char *arg: [IN] 解析的参数
Return:
    NULL
*/
void response_download_chunk(int client_sock, char *arg)
{
    int fd;
    ssize_t size = -1;
    char buf[MAX_SIZE], header[MAX_SIZE], *chunk_head;
    char file_name[NAME_LEN];
    if (sscanf(arg, "/?download=%s", file_name) == EOF)
    {
        //匹配失败！
        printf("error:%s\n", arg);
        construct_header(header, 404, "text/html");
        write(client_sock, header, strlen(header));
        return;
    }
    fd = open(file_name, O_RDONLY);
    if (fd == -1)
    {
        construct_header(header, 404, "text/html");
        write(client_sock, header, strlen(header));
        return;
    }
    //构建下载的相应头部
    printf("\tdownloading %s\n", file_name);
    construct_download_header(header, 200, file_name);
    write(client_sock, header, strlen(header));
    while (size)
    {
        //size代表读取的字节数
        size = read(fd, buf, MAX_SIZE);
        chunk_head = (char *)malloc(MIN_SIZE * sizeof(char));
        sprintf(chunk_head, "%x\r\n", size); //需要转换为16进制
        send(client_sock, chunk_head, strlen(chunk_head), 0);
        if (size > 0)
        {
            send(client_sock, buf, size, 0);
        }
        send(client_sock, CRLF, strlen(CRLF), 0);
        free(chunk_head);
    }
    send(client_sock, CRLF, strlen(CRLF), 0);
    printf("\n");
}

/*
Description:
    从输入的buffer字符数组中的下标pos开始，到下一个"\r\n"。
    获取完整的一行。输入的数字代表限制长度。此函数仅仅可以
    在
Parameters:

Return:
    返回下一行第一个字符的位置
*/
int get_next_line(char *temp, const char *buffer, int pos_of_buffer, int limit)
{
    int pos_of_temp = 0;
    for (; pos_of_temp < limit;)
    {
        if (buffer[pos_of_buffer] == '\r' || buffer[pos_of_buffer + 1] == '\n')
            break;
        else
            temp[pos_of_temp++] = buffer[pos_of_buffer++];
    }
    temp[pos_of_temp] = '\0';

    // 这里还需要处理！！！
    return (pos_of_buffer + 2);
}



/*
Description:
    从输入的buffer字符数组中的下标pos开始，到下一个"\r\n"。
    获取完整的一行。输入的数字代表限制长度。此函数仅仅可以
    在
Parameters:

Return:
    成功返回0，失败返回1
*/
int upload_file(int client_sock, char *buffer , char *arg, http_header_chain headers, int begin_pos_of_http_content, int size_of_buffer)
{
    int content_length = 0;
    int temp_pos = 0;
    char temp[MAX_SIZE];
    char boundary[MIDDLE_SIZE];
    char filename[MIDDLE_SIZE];
    char *char_pointer;

    //首先从首部行Content-Type中获得分隔符boundary
    //执行以下函数，temp数组中存储的是名字为Content-Type的首部行的，首部行内容
    get_http_header_content("Content-Type", temp, &headers, MAX_SIZE);
    // 提取出temp数组中的boundary
    char_pointer = strstr(temp, "boundary");

    // 这里可能空指针
    if (char_pointer == NULL)
    {
        strncpy(temp, "/?cgi-bin=", sizeof(temp));
        strncat(temp, arg, sizeof(temp) - strlen(temp) - 1);
        response_cgi(client_sock, temp);
        return 0;
    }

    char_pointer = char_pointer + 9;
    while ((*char_pointer) != '\r')
    {
        boundary[temp_pos++] = (*char_pointer);
        char_pointer = char_pointer + 1;
    }
    boundary[temp_pos] = '\0';
    // boundary之前还会多两个"-"
    strncpy(temp, boundary, sizeof(temp) - 1);
    temp[strlen(boundary)] = '\0';
    strncpy(boundary, "--", sizeof(boundary) - 1);
    boundary[2] = '\0';
    strncat(boundary, temp, sizeof(boundary) - strlen(boundary) - 1);
    boundary[strlen(temp)] = '\0';
    printf("boundary: %s \n", boundary);

    //begin_pos_of_http_content很重要, HTTP数据包在这一点之后就是http数据包的实体部分
    //使用pos_of_buffer来显示现在读取的buffer的位置
    int pos_of_buffer = begin_pos_of_http_content;
    // 一次写入文件的量
    int written_size = 0;

    //首先读取第一行，这里第一行是boundary
    pos_of_buffer = get_next_line(temp, buffer, pos_of_buffer, MAX_SIZE - 1);
    //假定浏览器发过来的POST请求都是关于上传文件的
    //那么第二行一定包含了文件名这一信息，
    pos_of_buffer = get_next_line(temp, buffer, pos_of_buffer, MAX_SIZE - 1);
    //从temp数组中提取文件名字
    temp_pos = 0;
    char_pointer = strstr(temp, "filename");

    // 这里可能空指针
    if (char_pointer == NULL)
    {
        strncpy(temp, "/?cgi-bin=", sizeof(temp));
        strncat(temp, arg, sizeof(temp) - strlen(temp) - 1);
        response_cgi(client_sock, temp);
        return 0;
    }

    char_pointer = char_pointer + 10;
    while ((*char_pointer) != '\"')
    {
        filename[temp_pos++] = (*char_pointer);
        char_pointer = char_pointer + 1;
    }
    filename[temp_pos] = '\0';
    strncpy(temp, arg, sizeof(temp) - 1);
    strncat(temp, "/", sizeof(temp) - strlen(temp) - 1);
    strncat(temp, filename, sizeof(temp) - strlen(temp) - 1);

    if (access(temp, F_OK) != -1)
    {
        //这个文件已经存在了，就给文件名前面加上"new_"

        //strncpy(temp, "new_", sizeof(temp) - 1);
        //strncat(temp, filename, sizeof(temp) - strlen(temp) - 1);
        //strncpy(filename, temp, sizeof(filename) - 1);
        //strncpy(temp, arg, sizeof(temp) - 1);
        //strncat(temp, "/", sizeof(temp) - strlen(temp) - 1);
        //strncat(temp, filename, sizeof(temp) - strlen(temp) - 1);

        //这个文件已经存在了，就不让上传了
        strncpy(temp, "/?cgi-bin=", sizeof(temp));
        strncat(temp, arg, sizeof(temp) - strlen(temp) - 1);
        response_cgi(client_sock, temp);
        return 0;
    }

    strncpy(filename, temp, sizeof(filename));

    //根据文件名，创建文件
    int fd = open(filename, O_CREAT | O_WRONLY);

    //第三行是文件类型信息
    pos_of_buffer = get_next_line(temp, buffer, pos_of_buffer, MAX_SIZE - 1);

    //第四行是空行
    pos_of_buffer = get_next_line(temp, buffer, pos_of_buffer, MAX_SIZE - 1);

    //从第5行开始时文件内容，直到遇见boundary结束
    while (1)
    {
        // 查找在buffer数组中pos_of_buffer位置之后是否存在
        // boundary，存在证明file结尾在此buffer中，不存在
        // 证明还需要再读socket中的数据
        int pos_of_boundary_after_pos_of_buffer = kmp(buffer + pos_of_buffer, boundary, size_of_buffer - pos_of_buffer);

        // file的结尾不存在于此buffer中
        if (pos_of_boundary_after_pos_of_buffer == -1)
        {
            written_size = write(fd, buffer + pos_of_buffer, size_of_buffer - pos_of_buffer);
            printf("写入的数量: %d\n", written_size);
            if (written_size == -1)
            {
                //删除文件并提示出错
                close(fd);
                unlink(filename);
                break;
            }
            else
            {
                //读socket中的数据
                int size_of_buffer = read(client_sock, buffer, MAX_SIZE);
                printf("从socket中读出的量：%d\n", size_of_buffer);
                pos_of_buffer = 0;
            }
        }
        else //file的结尾存在于此buffer中
        {
            written_size = write(fd, buffer + pos_of_buffer, pos_of_boundary_after_pos_of_buffer);
            printf("写入的数量: %d\n", written_size);
            if (written_size == -1)
            {
                //删除文件并提示出错
                close(fd);
                unlink(filename);
                break;
            }
            else
            {
                //关闭文件
                close(fd);
                break;
            }
        }
    }

    //重新显示这个页面
    strncpy(temp, "/?cgi-bin=", sizeof(temp));
    strncat(temp, arg, sizeof(temp) - strlen(temp) - 1);
    response_cgi(client_sock, temp);
    return 0;
}

/*
Description:
    从客户端读取请求并对请求的内容进行处理
    分割为GET和POST请求
Parameters:
    void *p_client_sock [IN] 客户端的socket
Return:
    NULL
*/
void *do_Method(void *p_client_sock)
{
    //令当前线程分离
    pthread_detach(pthread_self());
    int client_sock = *(int *)p_client_sock;
    int tid = pthread_self();
    int break_cnt = 0; //用于指定tcp连接断开,一定次数的无连接即断开tcp
    char *Connection;  //记录Connection的连接信息
    //设置非阻塞
    int flags;
    flags = fcntl(client_sock, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(client_sock, F_SETFL, flags);
    while (1)
    {
        char methods[5];
        ; //GET or POST
        char *buffer,*message;
        buffer = (char *)malloc(MAX_SIZE * sizeof(char));
        message = (char *)malloc(MIDDLE_SIZE * sizeof(char));
        int size_of_buffer = read(client_sock, buffer, MAX_SIZE);
        printf("%s\n", buffer);
        break_cnt++;
        if (break_cnt > BREAK_CNT)
        {
            break;
        }

        //因为设置了非阻塞，所以需要轮询
        if ((size_of_buffer < 0) && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        {
            usleep(300000);
            continue;
        }
        else if (size_of_buffer <= 0)
        {
            //此处大部分情况为出现空的包，以后考虑如何处理
            printf("buffer长度异常,长度为%d\n", size_of_buffer);
            continue;
        }
#ifdef _DEBUG
        printf("tid:%d size:%d\n", tid, size_of_buffer);
        printf("BUFFER:%s\n", buffer);
#endif
        break_cnt = 0;
        //buffer是接收到的请求，需要处理
        //从buffer中分离出请求的方法和请求的参数
        int s_ret = sscanf(buffer, "%s %s", methods, message);
        if (s_ret != 2)
        {
            //如果buffer中的数据无法被分割为两个字符串，则默认echo
            response_echo(client_sock, buffer);
            continue;
        }
        //获得所有的首部行组成的链表
        http_header_chain headers = (http_header_chain)malloc(sizeof(_http_header_chain));
        //begin_pos_of_http_content是buffer中可能存在的HTTP内容部分的起始位置, GET报文是没有的，POST报文有
        int begin_pos_of_http_content = get_http_headers(buffer, &headers);
        //print_http_headers(&headers);
        Connection = (char *)malloc(MIN_SIZE * sizeof(char));
        int keep_alive = get_http_header_content("Connection", Connection, &headers, MAX_SIZE);
        char *decode_msg = urldecode(message);
        message = decode_msg;
        switch (methods[0])
        {
        // GET
        case 'G':
            // 当message字符串开始就出现"/?download="子串时
            if (kmp(message, "/?download=", strlen(message)) == 0)
            {
                response_download_chunk(client_sock, message);
            }
            else if (kmp(message, "/?cgi-bin=", strlen(message)) == 0)
            // 当message字符串开始就出现"/?cgi-bin="子串时
            {
                response_cgi(client_sock, message);
            }
            else
            {
                response_webpage(client_sock, message);
            }
            break;
            // POST
        case 'P':
            upload_file(client_sock, buffer, message, headers, begin_pos_of_http_content, size_of_buffer);
            break;
        default:
            printf("不支持的请求:\n");
            response_echo(client_sock, buffer);
        }

#ifdef _DEBUG
        if (!keep_alive)
        {
            printf("testConnection:%s\n", Connection);
        }
#endif
        //如果接受到close的connection请求，则关闭socket
        if (!(keep_alive || strcmp(Connection, "close")))
        {
            break;
        }
        free(Connection);
    }
    close(client_sock);
}
