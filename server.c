#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define ERR_EXIT(a) { perror(a); exit(1); }

typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[512];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    // you don't need to change this.
    int account;
    //char* filename;
    int wait_for_write;  // used by handle_read to know if the header is read or not.
    int open;
    int work;
} request;


typedef struct {
    int id;
    int money;
} Account;

typedef struct {
    //char buf[512];
    int account;
    int num;
} now;

server svr;  // server
request* requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list

const char* accept_read_header = "ACCEPT_FROM_READ";
const char* accept_write_header = "ACCEPT_FROM_WRITE";
const char* reject_header = "REJECT\n";
const char* balance = "Balance :";

// Forwards
int count(int b){
    int i,sum=1;
    if(b==1){
        return 1;
    }
    for(i=1;i<b;i++){
        sum*=10;
    }
    return sum;
}
static void init_server(unsigned short port);
// initailize a server, exit for error

static void init_request(request* reqP);
// initailize a request instance

static void free_request(request* reqP);
// free resources used by a request instance

static int handle_read(request* reqP);
// return 0: socket ended, request done.
// return 1: success, message (without header) got this time is in reqP->buf with reqP->buf_len bytes. read more until got <= 0.
// It's guaranteed that the header would be correctly set after the first read.
// error code:
// -1: client connection error

int main(int argc, char** argv) {
    int i, ret, flag, j, num=1;
    struct timeval timeout;
    timeout.tv_sec=5;
    timeout.tv_usec=0;
    struct sockaddr_in cliaddr;  // used by accept()
    int clilen;
    struct flock lock;
    char acc[512];
    int conn_fd;  // fd for a new connection with client
    FILE* fd;  // fd for file that we open for reading
    FILE* wfd;
    int fp;
    char buf[512];
    now exist[100];
    Account account[20];
    for(i=0;i<100;i++){
        exist[i].account =-1;
    }
    int buf_len;
    fd_set m_set , r_set;
    // Parse args.
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }
    
    // Initialize server
    init_server((unsigned short) atoi(argv[1]));
    
    // Get file descripter table size and initize request table
    maxfd = 256 ; //getdtablesize();
    requestP = (request*) malloc(sizeof(request) * maxfd);
    if (requestP == NULL) {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);
    
    
    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);
    FD_ZERO(&m_set);
    //fd = fopen("account_info","ab");
    
    while (1) {
        // TODO: Add IO multiplexing
        // Check new connection
        FD_ZERO(&r_set);
        FD_SET(svr.listen_fd,&m_set);
        memcpy(&r_set,&m_set,sizeof(m_set));
        timeout.tv_sec=5;
        timeout.tv_usec=0;
        int wait = select(maxfd+1,&r_set,NULL,NULL,&timeout );
        //int fp;
        
        for(i=3;i<maxfd;i++){
            if(wait<=0){
                continue;
            }
            if(FD_ISSET(i, &r_set))
            {
                if(i==svr.listen_fd)
                {
                    clilen = sizeof(cliaddr);
                    conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
                    if (conn_fd < 0) {
                        if (errno == EINTR || errno == EAGAIN)
                            continue;  // try again
                        if (errno == ENFILE) {
                            (void) fprintf(stderr, "out of file descriptor table ...   (maxconn %d)\n",maxfd);
                            continue;
                        }
                        ERR_EXIT("accept")
                    }
                    requestP[conn_fd].conn_fd = conn_fd;
                    strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
                    fprintf(stderr,"getting a new request... fd %d from %s\n",conn_fd,requestP[conn_fd].host);
                    FD_SET(conn_fd,&m_set);
                    wait--;
                }
                else{
                    if(requestP[i].open == -1)
                    {
                        fd = fopen("account_info","rb");
                        //fseek(fd, 0 ,SEEK_SET);
                        fread(account, sizeof(Account),20, fd);
                        //wfd= fopen("account_info","wb");
#ifdef READ_SERVER
                        fp = open("account_info",O_RDONLY);
#else
                        fp = open("account_info",O_WRONLY);
#endif
                        ret = handle_read(&requestP[i]); // parse data from client to requestP[conn_fd].buf
                        if (ret < 0) {
                            fprintf(stderr, "bad request from %s\n", requestP[i].host);
                            continue;
                        }
                        int k = (int)strlen(requestP[i].buf);
                        int temp=0;
                        for(j=0;j<k;j++){
                            temp += (requestP[i].buf[j]-48) * (count(k-j));
                        }
                        requestP[i].account =temp;
                        printf("account : %d\n",requestP[i].account);
                        flag = 0;
                        //sprintf(buf,"%s",requestP[i].buf);
                        //printf("%d\n",num);
#ifndef READ_SERVER
                        for(j=0;j<num;j++){
                            if(exist[j].account == requestP[i].account)
                            {
                                flag=1;
                                //printf("=d=d=d\n");
                                write(requestP[i].conn_fd,reject_header,strlen(reject_header));
                                close(requestP[i].conn_fd);
                                free_request(&requestP[i]);
                                FD_CLR(i,&m_set);
                                break;
                            }
                        }
#endif
                        if(flag == 0){
                            struct flock ll;
#ifdef READ_SERVER
                            ll.l_type=F_RDLCK;
#else
                            ll.l_type=F_WRLCK;
#endif
                            ll.l_len=sizeof(Account);
                            ll.l_whence=SEEK_SET;
                            ll.l_start=(requestP[i].account-1)*sizeof(Account);
                            if(fcntl(fp,F_SETLK,&ll)<0)
                            {
                                write(requestP[i].conn_fd,reject_header,strlen(reject_header));
                                close(requestP[i].conn_fd);
                                free_request(&requestP[i]);
                                FD_CLR(i,&m_set);
                                
                            }else{
                                exist[num-1].account = requestP[i].account;
#ifndef READ_SERVER
                                sprintf(buf,"This account (%s) is available.\n",requestP[conn_fd].buf);
#endif
                                num++;
                                write(requestP[i].conn_fd, buf, strlen(buf));
                                printf("%d\n",account[requestP[i].account-1].money);
                                requestP[i].open = 1;
#ifdef READ_SERVER
                                //write(requestP[i].conn_fd, balance, strlen(balance));
                                char* mess = (char*)malloc(512*sizeof(char));
                                int k;
                                for(k=0 ; k<512;k++){
                                    mess[k] = '\0';
                                }
                                sprintf(mess,"Balance: %d\n",account[requestP[i].account-1].money);
                                //printf("teritueote\n");
                                struct flock ll;
                                ll.l_type=F_UNLCK;
                                ll.l_len=sizeof(Account);
                                ll.l_whence=SEEK_SET;
                                ll.l_start=(requestP[i].account-1)*sizeof(Account);
                                fcntl(fp,F_SETLK,&ll);
                                //close(fp);
                                wfd = fopen("account_info","wb");
                                //fseek(fd, 0 ,SEEK_SET);
                                fwrite(account, sizeof(Account),20, wfd);
                                fclose(wfd);
                                write(requestP[i].conn_fd, mess, strlen(mess));
                                close(requestP[i].conn_fd);
                                free(mess);
                                free_request(&requestP[i]);
                                FD_CLR(i,&m_set);
#endif
                            }
                        }
                    }
#ifndef READ_SERVER
                    else if(FD_ISSET(requestP[i].conn_fd,&r_set)&&requestP[i].open==1){
                        int change=0;
                        ret = handle_read(&requestP[i]); // parse data from client to requestP[conn_fd].buf
                        if (ret < 0) {
                            fprintf(stderr, "bad request from %s\n", requestP[i].host);
                            continue;
                        }
                        else if (ret != 0){
                            int k = (int)strlen(requestP[i].buf);
                            if(requestP[i].buf[0]!='-'&&requestP[i].buf[0]!='+'){
                                j=0;
                            }else{
                                j=1;
                            }
                            for(;j<k;j++){
                                change += (requestP[i].buf[j]-48)*(count(k-j));
                                //printf("|%d %d %d|\n",requestP[i].buf[j]-48,count(k-j),change);
                            }
                            if(requestP[i].buf[0]=='-'){
                                change *= (-1);
                            }
                            if(account[requestP[i].account-1].money + change < 0){
                                sprintf(buf,"Operation error\n");
                                write(requestP[i].conn_fd, buf, strlen(buf));
                            }else{
                                printf("%d %d\n",change, account[requestP[i].account-1].money += change);
                                //printf("%d\n",account[requestP[i].account].money);
                                //printf("%d\n ========\n",requestP[i].account);
                                requestP[i].open++;
                                fprintf(stderr, "Done writing file [%d]\n", requestP[i].account);
                                sprintf(buf,"balance : %d\n",account[requestP[i].account-1].money);
                                write(requestP[i].conn_fd, buf, strlen(buf));
                                struct flock ll;
                                ll.l_type=F_UNLCK;
                                ll.l_len=sizeof(Account);
                                ll.l_whence=SEEK_SET;
                                ll.l_start=(requestP[i].account-1)*sizeof(Account);
                                printf("unlock: %d\n", requestP[i].account);
                                fcntl(fp,F_SETLK,&ll);
                            }
                            for(j=0;j<num;j++)
                            {
                                if(exist[j].account==requestP[i].account)
                                {
                                    if(num==1){
                                        exist[j].account = -1;
                                        requestP[i].open =0;
                                        close(requestP[i].conn_fd);
                                        free_request(&requestP[i]);
                                        FD_CLR(i,&m_set);
                                        break;
                                    }
                                    exist[j].account= exist[num-1].account;
                                    num--;
                                    requestP[i].open =0;
                                    close(requestP[i].conn_fd);
                                    free_request(&requestP[i]);
                                    FD_CLR(i,&m_set);
                                    break;
                                }
                            }
                            wfd = fopen("account_info","wb");
                            //fseek(fd, 0 ,SEEK_SET);
                            fwrite(account, sizeof(Account),20, wfd);
                            fclose(wfd);
                        }/*else{
                            fprintf(stderr, "Done writing file [%d]\n", requestP[i].account);
                            for(j=0;j<num;j++)
                            {
                                if(exist[j].account==requestP[i].account)
                                {
                                    exist[j].account= exist[num-1].account;
                                    num--;
                                    close(requestP[i].conn_fd);
                                    free_request(&requestP[i]);
                                    FD_CLR(i,&m_set);
                                    break;
                                }
                            }
                        }*/
                    }
#endif
                }
            }
        }
    }
    fclose(fd);
    fclose(wfd);
    close(fp);
    free(requestP);
    return 0;
}


// ======================================================================================================
// You don't need to know how the following codes are working
#include <fcntl.h>

static void* e_malloc(size_t size);


static void init_request(request* reqP) {
    reqP->conn_fd = -1;
    reqP->buf_len = 0;
    reqP->account = 0;
    reqP->wait_for_write = 0;
    reqP->open = -1;
    reqP->work = 0;
}

static void free_request(request* reqP) {
    /*if (reqP->filename != NULL) {
     free(reqP->filename);
     reqP->filename = NULL;
     }*/
    init_request(reqP);
}

// return 0: socket ended, request done.
// return 1: success, message (without header) got this time is in reqP->buf with reqP->buf_len bytes. read more until got <= 0.
// It's guaranteed that the header would be correctly set after the first read.
// error code:
// -1: client connection error
static int handle_read(request* reqP) {
    int r,l;
    char buf[512];
    
    // Read in request from client
    r = read(reqP->conn_fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r == 0) return 0;
    char* p1 = strstr(buf, "\015\012");
    int newline_len = 2;
    // be careful that in Windows, line ends with \015\012
    if (p1 == NULL) {
        p1 = strstr(buf, "\012");
        newline_len = 1;
        if (p1 == NULL) {
            ERR_EXIT("this really should not happen...");
        }
    }
    size_t len = p1 - buf + 1;
    memmove(reqP->buf, buf, len);
    reqP->buf[len - 1] = '\0';
    reqP->buf_len = len-1;
    return 1;
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;
    
    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;
    
    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");
    
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }
}

static void* e_malloc(size_t size) {
    void* ptr;
    
    ptr = malloc(size);
    if (ptr == NULL) ERR_EXIT("out of memory");
    return ptr;
}

