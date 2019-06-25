#ifndef CLOVER_IPC_H
#define CLOVER_IPC_H

#include <clover_utils.h>

#ifdef __cplusplus
extern "C" {
#endif

s32 clv_socket_cloexec(s32 domain, s32 type, s32 protocal);
s32 clv_socket_nonblock(s32 sock);
s32 clv_send_fd(const s32 sock_fd, s32 send_fd);
s32 clv_recv_fd(const s32 sock_fd);
s32 clv_send(s32 sock, void *buf, s32 sz);
s32 clv_recv(s32 sock, void *buf, s32 sz);
s32 clv_socket_bind_listen(const s32 sock, const char *name);
s32 clv_socket_accept(const s32 sock);
s32 clv_socket_connect(const s32 sock, const char *remote);

#ifdef __cplusplus
}
#endif

#endif

