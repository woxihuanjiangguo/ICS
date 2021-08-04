/*
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Li Zhenxin
 *     19302010007
 */ 

#include "csapp.h"
#include "cache.h"

/* MACROS */
typedef struct threadArgs
{
    int *connfd;
    struct sockaddr_in *clientaddr;
} TA;


/* GLOBAL VARIABLES */
sem_t mutex;                // mutex for log writting
static FILE *my_log;        // the open log file
cache_list *list_of_cache;  // hold the list of all the cache


/*
 * Function prototypes
 */
void args_validate(int argc, char **argv);
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
/* wrapper functions for Rio_readlineb and Rio_writen */
ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
/* proxy logic part*/
void *thread(TA *vargp);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void logger(struct sockaddr_in *sockaddr, char *uri, int size, int from_cache);

/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{
    args_validate(argc, argv);
    
    int port, listenfd, clientlen;
    struct sockaddr_in clientaddr;

    clientlen = sizeof(clientaddr);

    // args valid
    port = atoi(argv[1]);

    // ignore SIGPIPE
    Signal(SIGPIPE, SIG_IGN);

    // init mutex
    Sem_init(&mutex, 0, 1);

    // init the cache list
    list_of_cache = init_list();

    // open our file
    my_log = Fopen("proxy.log" ,"a+");

    // open listenfd
    listenfd = Open_listenfd(port);
    // create threads for connection
    while(1){
        pthread_t tid;
		int *connfd = Malloc(sizeof(int));        
        *connfd = Accept(listenfd, (SA *) &clientaddr, (socklen_t *) &clientlen);
        /* for log purpose */
        TA *thread_args = Malloc(sizeof(thread_args));
        thread_args->connfd = connfd;
        thread_args->clientaddr = &clientaddr;
        Pthread_create(&tid, NULL, (void *)thread, thread_args);
    }

    Close(listenfd);
    Fclose(my_log);
    exit(0);
}

/*
 * thread - the function that defines the behavior of threads
 *          detach
 *          read data
 *          log the user info
 *            
 *          close fd
 * 
 */
void *thread(TA *vargp){
    // local variables that keep a record of the request
    char buf[MAXLINE * 40], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char cache_key[MAXLINE];
    char hostname[MAXLINE], pathname[MAXLINE];
    int connfd = *(vargp->connfd);            // holds local copy of the connfd
    struct sockaddr_in *clientaddr_temp = vargp->clientaddr;    //holds local copy of the clientaddr
    int port;
    rio_t rio, rio2;    //rio for connfd, rio2 for clientfd (proxy as a client)

    Pthread_detach(pthread_self());
    Free(vargp->connfd);            // connfd already fetched
    
    Rio_readinitb(&rio, connfd);

    Rio_readlineb(&rio, buf, MAXLINE);

    sscanf(buf, "%s %s %s", method, uri, version);

    // validation
    if (strcmp(method, "GET")) {
        clienterror(connfd, method, "501", "Not Implemented", "Not Implemented");
        Free(vargp);
        Close(connfd);
        return NULL;
    }

    if (parse_uri(uri, hostname, pathname, &port) < 0) {
        clienterror(connfd, method, "400", "Bad Request", "URI Cannot Be Parsed");
        Free(vargp);
        Close(connfd);
        return NULL;
    }

    // continue if valid
    // find cache first according to hostname & pathname
    sprintf(cache_key, "%s %s %d", hostname, pathname, port);
    char content_buf[MAXLINE * 40];
    int cache_length;
    /***** cache found -- return the content immediately *****/

    if((cache_length = get_node(list_of_cache, cache_key, content_buf))){
        Rio_writen_w(connfd, content_buf, cache_length);
        logger(clientaddr_temp, uri, cache_length, 1);
        Free(vargp);
        Close(connfd);
        return NULL;
    }

    /***** cache not found -- send request to the server*****/
    int clientfd = Open_clientfd(hostname, port), total_length = 0, temp_length;
    
    // give clientfd a new buf (METHOD PATHNAME VERSION)
    sprintf(buf, "%s %s %s\r\n", method, pathname, version);
    Rio_writen_w(clientfd, buf, strlen(buf));
    
    // settle the rest headers we are gonna send to the server
    while(strcmp(buf, "\r\n")) {
        Rio_readlineb_w(&rio, buf, MAXLINE);
        Rio_writen_w(clientfd, buf, strlen(buf));
    }

    Rio_writen_w(clientfd, "\r\n", strlen("\r\n"));      // append a \r\n

    // read the response from server, and form the response buf for connfd
    Rio_readinitb(&rio2, clientfd);
    char response_buf[MAXLINE];
    while ((temp_length = Rio_readnb(&rio2, buf, sizeof(buf))) > 0) {
	    Rio_writen_w(connfd, buf, temp_length);
        memcpy(response_buf + total_length, buf, temp_length);      // string appending
	    total_length += temp_length;
    }
    // send back to the connfd
    Rio_writen_w(connfd, response_buf, total_length);
    
    // caching
    insert_node(list_of_cache, cache_key, response_buf);

    // log here
    logger(clientaddr_temp, uri, total_length, 0);
    // close two fds
    Close(clientfd);
    Close(connfd);
    // free the arg struct & the response_buf
    Free(vargp);

    return NULL;
}

/*
 * parse_uri - URI parser
 * 
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
	    hostname[0] = '\0';
	    return -1;
    }
       
    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';
    
    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
	    pathname[0] = '/';
        pathname[1] = '\0';
    }
    else {
	    strcpy(pathname, pathbegin);
    }

    if (*hostend == '\0') {
        // nothing after .com
        *port = 80;
        return 0;
    }
    
    int port_length = 1;
    while((hostend + port_length)[0] != '/'){
        port_length ++;
    }
    port_length --;
    char *port_temp = (char *)malloc(sizeof(char) * port_length);

    /* Extract the port number, should be modified*/
    
    if (*hostend == ':' && (port_length >= 1)){
        memcpy(port_temp, hostend + 1, port_length);
        port_temp[port_length] = '\0';
        if(strspn(port_temp,"0123456789") != strlen(port_temp)){
            // contain something other than numbers
            Free(port_temp);
            return -1;
        }
        *port = atoi(port_temp);
    } else if(*hostend == ':' && (port_length == 0)){
        // case -- host:/
        Free(port_temp);
        return -1;
    } else{
        // case no ':' after host
        *port = 80;
    }
    Free(port_temp);
    if (*port <= 0 || *port >= 65536) {
        return -1;
    }
    return 0;
}

/*
 * logger (altered from format_log_entry) - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 * 
 * handle the entire log process
 */
void logger(struct sockaddr_in *sockaddr, char *uri, int size, int from_cache)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;
    char logstring[MAXLINE];
    char cache_info[MAXLINE];

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /* 
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;

    if(from_cache){
        sprintf(cache_info, " --fetched from cache");
    }else{
        sprintf(cache_info, " --fetched from server");
    }

    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s %d %s\n", time_str, a, b, c, d, uri, size, cache_info);

    // write the log info to the file & dump helper info
    P(&mutex);
    fputs(logstring, my_log);
    fflush(my_log);
    V(&mutex);
}

/*
 * Rio_readlineb_w - Wrap the rio_readlineb from csapp.c
 *                   so that the proxy does not terminate unexpectedly
*/
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen){
    ssize_t ret;
	if((ret = rio_readlineb(rp, usrbuf, maxlen)) >= 0) {
		return ret;
	} else {
        printf("Function rio_readlineb encounters an error!\n");
		return 0;
	}
}

/*
 * Rio_readlineb_w - Wrap the rio_writen from csapp.c
 *                   so that the proxy does not terminate unexpectedly
*/
ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n){
    ssize_t ret;
	if((ret = rio_writen(fd, usrbuf, n)) >= 0){
		return ret;
	} else {
        printf("Function rio_writen encounters an error!\n");
		return -1;
	}
}

/* 
 *  args_validate - validate the commandline arguments 
 *                  and dump error info
*/ 
void args_validate(int argc, char **argv){
    /* Check arguments */
    if (argc != 2) {
	    fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
	    exit(0);
    }
    /* invalid port number
     *2 cases - containing something other than numbers
     *          out of range
    */
    if(strspn(argv[1], "0123456789") != strlen(argv[1])){
        fprintf(stderr, "The second arg must be a port number\n");
	    exit(0);
    }

    int temp = atoi(argv[1]);
    if(temp <= 0 || temp >= 65536){
        fprintf(stderr, "Range for port number: 1 - 65535\n");
	    exit(0);
    }
}

/*
 * clienterror - returns an error message to the client
 *               modified according to the tiny server
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen_w(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen_w(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>My Proxy Error</title>");
    Rio_writen_w(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen_w(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen_w(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen_w(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>My Proxy</em>\r\n");
    Rio_writen_w(fd, buf, strlen(buf));
}