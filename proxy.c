#include <stdio.h>
#include <pthread.h>
#include "csapp.h"
#include "cache.h"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int fd);
void read_requesthdrs(int clientfd, rio_t *rio_server, void *request_buf, char *hostname, char *port);
int parse_uri(char *uri, char *hostname, char *port, char *path);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
void *thread(void *vargp);

int main(int argc, char **argv)
{
  int listenfd, *connfdp;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;
  signal(SIGPIPE, SIG_IGN); // SIGPIPE 예외처리
  /* Check command-line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, connfdp);
  }
}

void *thread(void *vargp)
{
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(connfd);
  Close(connfd);
  return NULL;
}

void doit(int serverfd)
{
  int clientfd, content_length;
  char buf[MAXLINE], request_buf[MAXLINE], response_buf[MAXLINE];
  char method[MAXLINE], uri[MAXLINE], hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
  rio_t rio_server, rio_client;
  /* Read request line and headers */
  rio_readinitb(&rio_server, serverfd);
  rio_readlineb(&rio_server, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s", method, uri);
  if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0))
  {
    clienterror(serverfd, method, "501", "Not implemented",
                "Tiny does not implement this method");
    return;
  }
  // 요청된 uri에 해당하는 데이터가 캐쉬되있는지 확인
  web_object_t *cached_object = find_cache(uri);
  if (cached_object)
  {
    send_cache(cached_object, serverfd);
    read_cache(cached_object);
    return;
  }

  /* Check if the request is for favicon.ico and ignore it */
  if (strstr(uri, "favicon.ico"))
  {
    printf("Ignoring favicon.ico request\n");
    return; // Just return without sending any response
  }
  // Parse URI from GET request
  if (!parse_uri(uri, hostname, port, path))
  {
    clienterror(serverfd, uri, "400", "Bad Request", "Proxy received a malformed request");
    return;
  }
  printf("!!!!!!! %s %s %s !!!!!!\n", hostname, port, path); // hostname, port, path 확인해보기
  sprintf(request_buf, "%s %s %s\r\n", method, path, "HTTP/1.0");
  clientfd = Open_clientfd(hostname, port);
  if (clientfd < 0)
  {
    fprintf(stderr, "Connection to %s on port %s failed.\n", hostname, port);
    clienterror(serverfd, "Connection Failed", "5-3", "Service Unavailable", "The proxy server could not retrieve the resource.");
    return;
  }
  rio_writen(clientfd, request_buf, strlen(request_buf));
  printf("I will read request and write\n");

  /* Request Header 읽기 & 전송 [🙋‍♀️ Client -> 🚒 Proxy -> 💻 Server] */
  read_requesthdrs(clientfd, &rio_server, request_buf, hostname, port);
  printf("reading request end\n");

  // 서버로부터 데이터를 받으며 사이즈가 MAX_OBJECT_SIZE가 넘지 않는다면 캐싱하기
  rio_readinitb(&rio_client, clientfd);
  char cachebuf[MAX_OBJECT_SIZE];
  int sizebuf = 0; // 현재 cachebuf에 저장된 바이트 수
  size_t n;        // 읽은 바이트 수
  while ((n = rio_readnb(&rio_client, response_buf, MAXLINE)) != 0)
  {
    // printf("proxy received %ld bytes, then send\n", n);
    if (sizebuf + n <= MAX_OBJECT_SIZE)
    {                                              // cachebuf에 충분한 공간이 있으면
      memcpy(cachebuf + sizebuf, response_buf, n); // 현재 cachebuf의 끝에 response_buf 복사
      sizebuf += n;                                // cachebuf에 저장된 바이트 수 갱신
    }
    rio_writen(serverfd, response_buf, n);
  }
  Close(clientfd); // 프록시와 서버와의 연결을 끊어주기

  if (sizebuf <= MAX_OBJECT_SIZE)
  {
    void *response_ptr = malloc(sizebuf);
    memcpy(response_ptr, cachebuf, sizebuf);
    // `web_object` 구조체 생성
    web_object_t *web_object = (web_object_t *)calloc(1, sizeof(web_object_t));
    web_object->response_ptr = response_ptr;
    web_object->content_length = sizebuf;
    strcpy(web_object->uri, uri);
    write_cache(web_object); // 캐시 연결 리스트에 추가
  }
}

void read_requesthdrs(int clientfd, rio_t *rio_server, void *request_buf, char *hostname, char *port)
{
  int is_host_exist;
  int is_connection_exist;
  int is_proxy_connection_exist;
  int is_user_agent_exist;
  rio_readlineb(rio_server, request_buf, MAXLINE); // 첫번째 줄 읽기
  while (strcmp(request_buf, "\r\n"))
  {
    if (strstr(request_buf, "Proxy-Connection") != NULL)
    {
      sprintf(request_buf, "Proxy-Connection: close\r\n");
      is_proxy_connection_exist = 1;
    }
    else if (strstr(request_buf, "Connection") != NULL)
    {
      sprintf(request_buf, "Connection: close\r\n");
      is_connection_exist = 1;
    }
    else if (strstr(request_buf, "User-Agent") != NULL)
    {
      sprintf(request_buf, user_agent_hdr);
      is_user_agent_exist = 1;
    }
    else if (strstr(request_buf, "Host") != NULL)
    {
      is_host_exist = 1;
    }
    rio_writen(clientfd, request_buf, strlen(request_buf)); // Server에 전송
    rio_readlineb(rio_server, request_buf, MAXLINE);        // 다음 줄 읽기
  }
  // 필수 헤더 미포함 시 추가로 전송
  if (!is_proxy_connection_exist)
  {
    sprintf(request_buf, "Proxy-Connection: close\r\n");
    rio_writen(clientfd, request_buf, strlen(request_buf));
  }
  if (!is_connection_exist)
  {
    sprintf(request_buf, "Connection: close\r\n");
    rio_writen(clientfd, request_buf, strlen(request_buf));
  }
  if (!is_host_exist)
  {
    sprintf(request_buf, "Host: %s:%s\r\n", hostname, port);
    rio_writen(clientfd, request_buf, strlen(request_buf));
  }
  if (!is_user_agent_exist)
  {
    sprintf(request_buf, user_agent_hdr);
    rio_writen(clientfd, request_buf, strlen(request_buf));
  }
  sprintf(request_buf, "\r\n"); // 종료문
  rio_writen(clientfd, request_buf, strlen(request_buf));
  return;
}

void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];
  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);
  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}
int parse_uri(char *uri, char *hostname, char *port, char *path)
{
  if (uri == NULL)
    return 0;
  char *ptr = NULL;
  path[0] = '/';
  path[1] = '\0';
  strcpy(port, "80");
  if (uri[0] == '/')
    uri = uri + 1;
  ptr = strstr(uri, "//");
  if (ptr)
    uri = ptr + 2;
  ptr = strchr(uri, ':');
  if (ptr)
  {
    *ptr = '\0';
    strcpy(hostname, uri);
    *ptr = ':';
    uri = ptr + 1;
    ptr = strchr(uri, '/');
    if (ptr)
    {
      *ptr = '\0';
      strcpy(port, uri);
      *ptr = '/';
      strcpy(path, ptr);
    }
    else
    {
      strcpy(port, uri);
    }
  }
  else
  {
    ptr = strchr(uri, '/');
    if (ptr)
    {
      *ptr = '\0';
      strcpy(hostname, uri);
      *ptr = '/';
      strcpy(path, ptr);
    }
    else
    {
      strcpy(hostname, uri);
    }
  }
  return 1;
}