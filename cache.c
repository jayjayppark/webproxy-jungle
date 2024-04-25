#include <stdio.h>

#include "csapp.h"
#include "cache.h"

web_object_t *rootp;
web_object_t *lastp;
int total_cache_size = 0;

// 캐싱된 웹 객체 중에 해당 `uri`를 가진 객체를 반환하는 함수
web_object_t *find_cache(char *uri)
{
  if (!rootp) // 캐시가 비었으면
    return NULL;
  web_object_t *current = rootp; // 검사를 시작할 노드
  while (current->next)          // 다음 노드가 NULL이 아닐때만 반복
  {
    if (!strcmp(current->uri, uri)) // method, uri가 같은 노드를 찾았다면 해당 객체 반환
      return current;

    current = current->next; // 다음 노드로 이동
  }
  if (!strcmp(current->uri, uri)) // method, uri가 같은 노드를 찾았다면 해당 객체 반환
    return current;
  return NULL;
}

// `web_object`에 저장된 response를 Client에 전송하는 함수
void send_cache(web_object_t *web_object, int serverfd)
{
  // 캐싱된 Response 전송
  Rio_writen(serverfd, web_object->response_ptr, web_object->content_length);
}

// 사용한 `web_object`를 캐시 연결리스트의 root로 갱신하는 함수
void read_cache(web_object_t *web_object)
{
  if (web_object == rootp) // 현재 노드가 이미 root면 변경 없이 종료
    return;

  // 1️⃣ 현재 노드와 이전 & 다음 노드의 연결 끊기
  if (web_object->next) // '이전 & 다음 노드'가 모두 있는 경우
  {
    // 이전 노드와 다음 노드를 이어줌
    web_object_t *prev_objtect = web_object->prev;
    web_object_t *next_objtect = web_object->next;
    prev_objtect->next = next_objtect;
    next_objtect->prev = prev_objtect;
  }
  else // '다음 노드'가 없는 경우 (현재 노드가 마지막 노드인 경우)
  {
    web_object->prev->next = NULL; // 이전 노드와 현재 노드의 연결을 끊어줌
  }

  // 2️⃣ 현재 노드를 root로 변경
  web_object->next = rootp; // root였던 노드는 현재 노드의 다음 노드가 됨
  rootp->prev = web_object;
  rootp = web_object;
}

// 인자로 전달된 `web_object`를 캐시 연결리스트에 추가하는 함수
void write_cache(web_object_t *web_object)
{
  // total_cache_size에 현재 객체의 크기 추가
  total_cache_size += web_object->content_length;

  // 최대 총 캐시 크기를 초과한 경우 -> 사용한지 가장 오래된 객체부터 제거
  while (total_cache_size > MAX_CACHE_SIZE)
  {
    total_cache_size -= lastp->content_length;
    lastp = lastp->prev; // 마지막 노드를 마지막의 이전 노드로 변경
    free(lastp->next);   // 제거한 노드의 메모리 반환
    lastp->next = NULL;
  }

  if (!rootp) // 캐시 연결리스트가 빈 경우 lastp를 현재 객체로 지정
    lastp = web_object;

  // 현재 객체를 루트로 지정
  if (rootp)
  {
    web_object->next = rootp;
    rootp->prev = web_object;
  }
  rootp = web_object;
}
