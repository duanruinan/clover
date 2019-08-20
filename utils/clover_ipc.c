#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <clover_utils.h>
#include <clover_log.h>
#include <clover_event.h>
#include <clover_ipc.h>

s32 clv_socket_cloexec(s32 domain, s32 type, s32 protocal)
{
	s32 fd;

	fd = socket(domain, type | SOCK_CLOEXEC, protocal);
	if (fd >= 0)
		return fd;
	if (errno != EINVAL) {
		clv_err("failed to create socket fd. %m");
		return -1;
	}

	fd = socket(domain, type, protocal);
	return clv_set_cloexec_or_close(fd);
}

s32 clv_socket_nonblock(s32 sock)
{
	s32 ret, flag;

	flag = fcntl(sock, F_GETFL, 0);
	if (flag < 0) {
		clv_err("failed to fcntl %m");
		return -errno;
	}

	ret = fcntl(sock, F_SETFL, flag | O_NONBLOCK);
	if (ret < 0) {
		clv_err("failed to fcntl %m");
		return -errno;
	}

	return 0;
}

s32 clv_send_fd(const s32 sock_fd, s32 send_fd)
{
	s32 ret;
	struct msghdr msg;
	struct cmsghdr *p_cmsg;
	struct iovec vec;
	char cmsgbuf[CMSG_SPACE(sizeof(send_fd))];
	s32 *p_fds;
	char sendchar = 0;

	msg.msg_control = cmsgbuf;
	msg.msg_controllen = sizeof(cmsgbuf);
	p_cmsg = CMSG_FIRSTHDR(&msg);
	p_cmsg->cmsg_level = SOL_SOCKET;
	p_cmsg->cmsg_type = SCM_RIGHTS;
	p_cmsg->cmsg_len = CMSG_LEN(sizeof(send_fd));
	p_fds = (s32 *)CMSG_DATA(p_cmsg);
	*p_fds = send_fd;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;

	vec.iov_base = &sendchar;
	vec.iov_len = sizeof(sendchar);
	ret = sendmsg(sock_fd, &msg, 0);
	if (ret != 1) {
		clv_err("sendmsg failed. %m");
		return -errno;
	}

	return 0;
}

s32 clv_recv_fd(const s32 sock_fd)
{
	s32 ret;
	struct msghdr msg;
	char recvchar;
	struct iovec vec;
	s32 recv_fd;
	char cmsgbuf[CMSG_SPACE(sizeof(recv_fd))];
	struct cmsghdr *p_cmsg;
	s32 *p_fd;

	vec.iov_base = &recvchar;
	vec.iov_len = sizeof(recvchar);
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsgbuf;
	msg.msg_controllen = sizeof(cmsgbuf);
	msg.msg_flags = 0;

	p_fd = (s32 *)CMSG_DATA(CMSG_FIRSTHDR(&msg));
	*p_fd = -1;
	ret = recvmsg(sock_fd, &msg, 0);
	if (ret != 1) {
		clv_err("recvmsg failed. %m");
		return -errno;
	}

	p_cmsg = CMSG_FIRSTHDR(&msg);
	if (p_cmsg == NULL) {
		clv_err("there is no fd passed.");
		return -ENOENT;
	}

	p_fd = (s32 *)CMSG_DATA(p_cmsg);
	recv_fd = *p_fd;
	if (recv_fd == -1) {
		clv_err("there is no valid fd passed.");
		return -ENOENT;
	}

	return recv_fd;
}

s32 clv_send(s32 sock, void *buf, s32 sz)
{
	u32 byts_to_wr = sz;
	u8 *p = buf;
	s32 ret;

	while (byts_to_wr) {
		ret = send(sock, p, byts_to_wr, MSG_NOSIGNAL);
		if (ret < 0) {
			if (errno == EINTR) {
				continue;
			} else if (errno == EWOULDBLOCK) {
				usleep(1000);
				continue;
			} else if (errno == EPIPE) {
				clv_notice("connection broken.");
				return -1;
			}
			clv_err("failed to send to socket. %m");
			return -errno;
		}
		p += ret;
		byts_to_wr -= ret;
	}
	return 0;
}

s32 clv_recv(s32 sock, void *buf, s32 sz)
{
	u32 byts_to_rd = sz;
	u8 *p = buf;
	s32 ret;

	while (byts_to_rd) {
		ret = recv(sock, p, byts_to_rd, 0);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			else if (errno == EAGAIN)
				continue;
			clv_err("failed to receive from socket. %m");
			return -errno;
		} else if (ret == 0) {
			clv_notice("connection broken.");
			return -1;
		}
		p += ret;
		byts_to_rd -= ret;
	}
	return 0;
}

s32 clv_socket_bind_listen(const s32 sock, const char *name)
{
	struct sockaddr_un servaddr;

	unlink(name);
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sun_family = AF_LOCAL;
	strcpy(servaddr.sun_path, name);
	if (bind(sock, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
		clv_err("bind socket failed. %m");
		return -errno;
	}

	if (listen(sock, 200) < 0) {
		clv_err("listen socket failed. %m");
		return -errno;
	}

	return 0;
}

s32 clv_socket_accept(const s32 sock)
{
	s32 sk;

	if ((sk = accept(sock, NULL, NULL)) < 0) {
		clv_err("accept socket failed. %m");
		return -errno;
	}

	return sk;
}

s32 clv_socket_connect(const s32 sock, const char *remote)
{
	struct sockaddr_un servaddr;
	s32 i;

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sun_family = AF_LOCAL;
	strcpy(servaddr.sun_path, remote);
	for (i = 0; i < 500; i++) {
		if (connect(sock, (struct sockaddr*)&servaddr,
			    sizeof(servaddr)) < 0) {
			clv_warn("cannot connect to %s %m try:(%d)", remote, i);
			usleep(30000);
			continue;
		}
		return 0;
	}

	clv_err("failed to connect %m %s", remote);
	return -errno;
}

