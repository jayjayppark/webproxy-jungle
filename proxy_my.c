/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"
#include <stdio.h>
void doit(int fd);
void read_requesthdrs(int clientfd, rio_t *rio_server, void *request_buf, char *hostname, char *port);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  /* Check command-line args */
  if (argc != 2) {
  fprintf(stderr, "usage: %s <port>\n", argv[0]);
  exit(1);
  }
  listenfd = Open_listenfd(argv[1]);
  while (1) {
  clientlen = sizeof(clientaddr);
  connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
  Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE,
  port, MAXLINE, 0);
  printf("Accepted connection from (%s, %s)\n", hostname, port);
  doit(connfd);
  Close(connfd);
  }
}

void doit(int serverfd)
{
  int is_static, clientfd, content_length;
  struct stat sbuf;
  char buf[MAXLINE], request_buf[MAXLINE], response_buf[MAXLINE];
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE], hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
  rio_t rio_server, rio_client;
  /* Read request line and headers */
  Rio_readinitb(&rio_server, serverfd);
  Rio_readlineb(&rio_server, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0)) {
  clienterror(serverfd, method, "501", "Not implemented",
  "Tiny does not implement this method");
  return;
  }
  parse_uri(uri, hostname, port, path);
  printf("!!!!!!! %s %s %s !!!!!!\n", hostname, port, path);
  sprintf(request_buf, "%s %s %s\r\n", method, path, "HTTP/1.0");
  clientfd = Open_clientfd(hostname, port);
  Rio_writen(clientfd, request_buf, strlen(request_buf));
  read_requesthdrs(clientfd, &rio_server, request_buf, hostname, port);

  Rio_readinitb(&rio_client, clientfd);

  while (strcmp(response_buf, "\r\n"))
  {
    Rio_readlineb(&rio_client, response_buf, MAXLINE);
    if (strstr(response_buf, "Content-length")) // Response Body 수신에 사용하기 위해 Content-length 저장
      content_length = atoi(strchr(response_buf, ':') + 1);
    Rio_writen(clientfd, response_buf, strlen(response_buf));
  }

  /* 4️⃣ Response Body 읽기 & 전송 [💻 Server -> 🚒 Proxy -> 🙋‍♀️ Client] */
  char *response_ptr = malloc(content_length);
  Rio_readnb(&rio_server, response_ptr, content_length);
  Rio_writen(clientfd, response_ptr, content_length); // Client에 Response Body 전송
  free(response_ptr); // 캐싱하지 않은 경우만 메모리 반환

  Close(serverfd);

}

void read_requesthdrs(int clientfd, rio_t *rio_server, void *request_buf, char *hostname, char *port)
    {
      int is_host_exist;
      int is_connection_exist;
      int is_proxy_connection_exist;
      int is_user_agent_exist;

      Rio_readlineb(rio_server, request_buf, MAXLINE); // 첫번째 줄 읽기
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

        Rio_writen(clientfd, request_buf, strlen(request_buf)); // Server에 전송
        Rio_readlineb(rio_server, request_buf, MAXLINE);       // 다음 줄 읽기
      }

      // 필수 헤더 미포함 시 추가로 전송
      if (!is_proxy_connection_exist)
      {
        sprintf(request_buf, "Proxy-Connection: close\r\n");
        Rio_writen(clientfd, request_buf, strlen(request_buf));
      }
      if (!is_connection_exist)
      {
        sprintf(request_buf, "Connection: close\r\n");
        Rio_writen(clientfd, request_buf, strlen(request_buf));
      }
      if (!is_host_exist)
      {
        sprintf(request_buf, "Host: %s:%s\r\n", hostname, port);
        Rio_writen(clientfd, request_buf, strlen(request_buf));
      }
      if (!is_user_agent_exist)
      {
        sprintf(request_buf, user_agent_hdr);
        Rio_writen(clientfd, request_buf, strlen(request_buf));
      }

      sprintf(request_buf, "\r\n"); // 종료문
      Rio_writen(clientfd, request_buf, strlen(request_buf));
      return;
    }

void read_responsehdrs(int serverfd, rio_t *rio_client)
{
  char buf[MAXLINE];
  char *ptr;
  int content_length = 0;

  Rio_readlineb(rio_client, buf, MAXLINE);
  while(strcmp(buf, "\r\n")) {
    if (ptr = strstr(buf, "Content-length:")) {
      ptr = strchr(ptr, ':');
      ptr += 1;
      while (isspace(*ptr)) ptr += 1;
      content_length = atoi(ptr);
    }
    Rio_writen(serverfd, buf, strlen(buf));
    printf("%s", buf);
    Rio_readlineb(rio_client, buf, MAXLINE);
  }
  Rio_writen(serverfd, buf, strlen(buf));
  return content_length;
}



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


void parse_uri(char *uri, char *hostname, char *port, char *path) {
  char *ptr = NULL;
  path[0] = '/';
  path[1] = '\0';
  strcpy(port, "80");
  
  if (uri[0] == '/') uri = uri + 1;
  
  ptr = strstr(uri, "//");
  if (ptr) uri = ptr + 2;
  
  ptr = strchr(uri, ':');
  if (ptr) {
    *ptr = '\0';
    strcpy(hostname, uri);
    uri = ptr + 1;
    
    ptr = strchr(uri, '/');
    if (ptr) {
      *ptr = '\0';
      strcpy(port, uri);
      *ptr = '/';
      strcpy(path, ptr);
    } else {
      strcpy(port, uri);
    }
  } else {
    ptr = strchr(uri, '/');
    if (ptr) {
      *ptr = '\0';
      strcpy(hostname, uri);
      *ptr = '/';
      strcpy(path, ptr);
    } else {
      strcpy(hostname, uri);
    }
  }
}