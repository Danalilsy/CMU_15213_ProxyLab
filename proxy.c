#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";

void doit(int fd);
int open_clientfd_bind_fake_ip(char *hostname, char *port, char *fake_ip);
int uri_found_f4m(char *uri, char *uri_nolist);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *hostname, int *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
int powerten(int i);
void *thread(void *vargp);
static void request_hdr(char *buf, char *buf2ser, char *hostname);



// global variables
sem_t mutex;
char *listen_port;
char *fake_ip;
char *www_ip;
char server_ip[MAXLINE];
char *video_pku = "video.pku.edu.cn";
char xml[MAXLINE];
int bitrate[50] = {0};
int main(int argc, char **argv) 
{
    signal(SIGPIPE, SIG_IGN); // ignore sigpipe

    int listenfd;
    int *connfd;

    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 4) {
	    fprintf(stderr, "usage: %s <listen-port> <fake-ip> <www-ip>\n", argv[0]);
	    exit(1);
    }
    listen_port = argv[1];
    fake_ip = argv[2];
    www_ip = argv[3];
    printf("fake_ip = %s\n", fake_ip);
    printf("www_ip = %s\n", www_ip);
    sem_init(&mutex, 0, 1);
    listenfd = Open_listenfd(listen_port);
    while (1) {
	    clientlen = sizeof(clientaddr);
        connfd = malloc(sizeof(int));
	    *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        pthread_create(&tid, NULL, thread, connfd);
    }
    return 0;
}

/*
 * doit - handle one HTTP request/response transaction
 */

/* $begin doit */
void doit(int fd) 
{
    int serverfd, len;

    int *port;
    char port2[10]="8080";
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], uri_nolist[MAXLINE], version[MAXLINE];
    char filename[MAXLINE];         // client request filename
    char hostname[MAXBUF];          // client request hostname
    char buf2ser[MAXLINE];          // proxy to server
    char ser_response[MAXLINE];     // server to proxy
    rio_t rio, rio_ser;             // rio: between client and proxy
                                    // rio_ser: between proxy and server
    port = malloc(sizeof(int));
    *port = 80;                      // default port 80

    memset(buf2ser, 0, sizeof(buf2ser)); 
    memset(filename, 0, sizeof(filename)); 
    memset(hostname, 0, sizeof(hostname)); 
    memset(ser_response, 0, sizeof(ser_response));
    memset(uri, 0, sizeof(uri));
    memset(method, 0, sizeof(method));
    memset(buf, 0, sizeof(buf));
    memset(version, 0, sizeof(version));

    // step1: obtain request from client and parse the request
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))  
        return;
    printf("request from client: %s\n", buf);

    // parse request into method, uri, version
    sscanf(buf, "%s %s %s", method, uri, version);       
    
    // check HTTP version, if 1.1, change it into 1.0
    if (!strcasecmp(version, "HTTP/1.1")) {
        strcpy(version, "HTTP/1.0");
    }

    // we only need GET method
    if (strcasecmp(method, "GET")) {     
        clienterror(fd, method, "501", "Not Implemented",
                    "This proxy does not implement this method");
        return;
    }               
    read_requesthdrs(&rio);

    /* Parse URI from GET request */
    parse_uri(uri, hostname, port);       
    strcpy(filename, uri);
    sprintf(buf2ser, "%s %s %s\r\n", method, filename, version);
    printf("proxy to server: %s\n", buf2ser);

    // request header
    request_hdr(buf, buf2ser, hostname);
    if(strcmp(hostname, video_pku) == 0){
        strcpy(hostname,www_ip);
    }
    else{
        fprintf(stderr, "wrong hostname\n");
        return;
    }
    // find .f4m in uri
    if (uri_found_f4m(uri, uri_nolist) != 0){
        // step2 : from proxy to server
        //sprintf(port2, "%d", *port);
        if ((serverfd = open_clientfd_bind_fake_ip(hostname, port2, fake_ip)) < 0){
            fprintf(stderr, "open server fd error\n");
            return;
        }

        Rio_readinitb(&rio_ser, serverfd);

        // send request to server
        Rio_writen(serverfd, buf2ser, strlen(buf2ser));

        // step3: recieve the response from the server
        while ((len = rio_readnb(&rio_ser, ser_response,sizeof(ser_response))) > 0) {
            Rio_writen(fd, ser_response, len);
            printf("ser_response=\n%s\n",ser_response);
            strcpy(xml, ser_response);
            parse_bitrates(xml);
            memset(ser_response, 0, sizeof(ser_response));
        }
        close(serverfd);
    }
    // other requests
    else {
        if ((serverfd = open_clientfd_bind_fake_ip(hostname, port2, fake_ip)) < 0){
            fprintf(stderr, "open server fd error\n");
            return;
        }
        
        Rio_readinitb(&rio_ser, serverfd);
        
        // send request to server
        Rio_writen(serverfd, buf2ser, strlen(buf2ser));
        
        // step3: recieve the response from the server
        while ((len = rio_readnb(&rio_ser, ser_response,sizeof(ser_response))) > 0) {
            Rio_writen(fd, ser_response, len);
            memset(ser_response, 0, sizeof(ser_response));
        }
        close(serverfd);
    }
    
}
/* $end doit */
void parse_bitrates(char *xml){
    char* p;
    for(p = xml; *p; p++){
        if(strncmp(p, "bitrate=\"", strlen("bitrate=\"")) == 0){
            p += strlen("bitrate=\"");
            char tmp_bitrate[10];
            int index = 0;
            while(*p != "\""){
                tmp_bitrate[index] = *p;
                index++;
                p++;
            }
            printf("tmp_bitrate=%s\n",tmp_bitrate);
        }
    }
}
int uri_found_f4m(char *uri, char *uri_nolist){
    char uri_tmp[MAXLINE];
    strcpy(uri_tmp, uri);
    char *p;
    for (p = uri_tmp; *p; p++){
        if (strncmp(p, ".f4m", strlen(".f4m")) == 0){
            strcpy(p, "_nolist.f4m");
            strcpy(uri_nolist, uri_tmp);
            printf("uri find f4m! and convert to nolist\n");
            return 1;
        }
    }
    return 0;
}
/* $begin open_clientfd_bind_fake_ip */
int open_clientfd_bind_fake_ip(char *hostname, char *port, char *fake_ip) {
    int clientfd;
    struct addrinfo hints, *listp, *p;
    
    /* Get a list of potential server addresses */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;  /* Open a connection */
    hints.ai_flags = AI_NUMERICSERV;  /* ... using a numeric port arg. */
    hints.ai_flags |= AI_ADDRCONFIG;  /* Recommended for connections */
    Getaddrinfo(hostname, port, &hints, &listp);
    
    /* Walk the list for one that we can successfully connect to */
    for (p = listp; p; p = p->ai_next) {
        /* Create a socket descriptor */
        if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
            continue; /* Socket failed, try the next */
        printf("%x  %x %x\n",p->ai_family, p->ai_socktype, p->ai_protocol);
        /* Bind fake_ip*/
        struct sockaddr_in saddr;
        memset((void *) &saddr, 0, sizeof(saddr));
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(0);
        saddr.sin_addr.s_addr = inet_addr(fake_ip); //bind ip
        if (bind(clientfd, (struct sockaddr *) &saddr, sizeof(saddr)) < 0) {
            fprintf(stderr, "Bind clientfd error.\n");
            Close(clientfd);
            continue;
        }
        /* Connect to the server */
        Inet_ntop(AF_INET,&((struct sockaddr_in*)(p->ai_addr))->sin_addr,server_ip, p->ai_addrlen);
        if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
            break; /* Success */
        Close(clientfd); /* Connect failed, try another */  //line:netp:openclientfd:closefd
    }
    
    /* Clean up */
    Freeaddrinfo(listp);
    if (!p) /* All connects failed */
        return -1;
    else    /* The last connect succeeded */
        return clientfd;
}
/* $end open_clientfd_bind_fake_ip */

/*
 * request_hdr - request header
 * if the request does not contain header, add request header
 */
static void request_hdr(char *buf, char *buf2ser, char *hostname)
{
    if(strcmp(buf, "Host"))
    {
          strcat(buf2ser, "Host: ");
          strcat(buf2ser, hostname);
          strcat(buf2ser, "\r\n");
    }
    if(strcmp(buf, "Accept:")) {
        strcat(buf2ser, accept_hdr);
    }
    if(strcmp(buf, "Accept-Encoding:")) {
        strcat(buf2ser, accept_encoding_hdr);
    }
    if(strcmp(buf, "User-Agent:")) {
        strcat(buf2ser, user_agent_hdr);
    }
    if(strcmp(buf, "Proxy-Connection:")) {
        strcat(buf2ser, "Proxy-Connection: close\r\n");
    }
    if(strcmp(buf, "Connection:")) {
        strcat(buf2ser, "Connection: close\r\n");
    }
    memset(buf, 0, sizeof(buf));
    strcat(buf2ser, "\r\n");
}

/*
 * thread - thread funciton
 *
 */
void *thread(void *vargp)
{
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self());
    free(vargp);
    doit(connfd);
    close(connfd);
    return NULL;
}

/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while(strcmp(buf, "\r\n")) {      
	Rio_readlineb(rp, buf, MAXLINE);
	printf("%s", buf);
    }
    return;
}
/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *hostname, int *port) 
{
    // in this lab all requests are static 

    char tmp[MAXLINE];          // holds local copy of uri
    char *buf;                  // ptr that traverses uri
    char *endbuf;               // ptr to end of the cmdline string
    int port_tmp[10];
    int i, j;                   // loop
    char num[2];                // store port value

    buf = tmp;
    for (i = 0; i < 10; i++) {
        port_tmp[i] = 0;
    }
    (void) strncpy(buf, uri, MAXLINE);
    endbuf = buf + strlen(buf);
    buf += 7;                   // 'http://' has 7 characters
    while (buf < endbuf) {
    // take host name out
        if (buf >= endbuf) {
            strcpy(uri, "");
            strcat(hostname, " ");
            // no other character found
            break;
        }
        if (*buf == ':') {  // if port number exists
            buf++;
            *port = 0;
            i = 0;
            while (*buf != '/') {
                num[0] = *buf;
                num[1] = '\0';
                port_tmp[i] = atoi(num);
                buf++;
                i++;
            }
            j = 0;
            while (i > 0) {
                *port += port_tmp[j] * powerten(i - 1);
                j++;
                i--;
            }
        }
        if (*buf != '/') {

            sprintf(hostname, "%s%c", hostname, *buf);
        }
        else { // host name done
            strcat(hostname, "\0");
            strcpy(uri, buf);
            break;
        }
        buf++;
    }
    return 1;
}
/* $end parse_uri */

/*
 * powerten - return ten to the power of i
 */
int powerten(int i) {
    int num = 1;
    while (i > 0) {
        num *= 10;
        i--;
    }
    return num;
}

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */
