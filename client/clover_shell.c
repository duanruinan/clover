#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <clover_utils.h>
#include <clover_ipc.h>
#include <clover_event.h>
#include <clover_protocal.h>

void usage(void)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "clover_shell --log func_level\n");
	fprintf(stderr, "\tSet log level.\n");
	fprintf(stderr, "\t\tclover_shell --log module0,level0:module1,level\n");
	fprintf(stderr, "\t\t\tModule Name:common/compositor/drm/gbm/ps/timer/"
			"gles/egl\n");
	fprintf(stderr, "\t\t\tLevel:0-5\n");
	fprintf(stderr, "clover_shell --info\n");
	fprintf(stderr, "\tGet canvas information\n");
	fprintf(stderr, "clover_shell --set-layout=layout_desc\n");
	fprintf(stderr, "\tSet screen layout\n");
	fprintf(stderr, "\t\tLayout_desc: Mode:Output-0:Output-1\n");
	fprintf(stderr, "\t\t\tMode: duplicated/extended\n");
	fprintf(stderr, "\t\t\tOutput-N: x,y/WidthxHeight\n");
	fprintf(stderr, "\t\t\te.g. clover_shell "
			"--layout=duplicated:0,0/1920x1080:0,0/1920x1080\n");
	fprintf(stderr, "\t\t\t  /--------------------/\n");
	fprintf(stderr, "\t\t\t /--------------------/|\n");
	fprintf(stderr, "\t\t\t |                    ||\n");
	fprintf(stderr, "\t\t\t |      1920x1080     ||\n");
	fprintf(stderr, "\t\t\t |                    ||\n");
	fprintf(stderr, "\t\t\t |--------------------|/\n");
	fprintf(stderr, "\t\t\te.g. clover_shell --layout=extended:"
			"0,0/1920x1080:1920,180/1600x900\n");
	fprintf(stderr, "\t\t\t                    |---------------------|\n");
	fprintf(stderr, "\t\t\t -------------------|                     |\n");
	fprintf(stderr, "\t\t\t |                  |                     |\n");
	fprintf(stderr, "\t\t\t |    #1:1600x900   |    #0:1920x1080     |\n");
	fprintf(stderr, "\t\t\t |                  |                     |\n");
	fprintf(stderr, "\t\t\t |------------------|---------------------|\n");
}

static struct option shell_options[] = {
	{"log", 1, NULL, 'l'},
	{"info", 0, NULL, 'i'},
	{"set-layout", 1, NULL, 's'},
};

static char shell_short_options[] = "l:is:";

static void parse_log_param(char *param, struct clv_shell_info *si)
{
	char *opt = malloc(256);
	char *p, *src;
	u8 *pflag;

	memset(si, 0, sizeof(*si));
	si->cmd = CLV_SHELL_DEBUG_SETTING;
	assert(opt);
	strcpy(opt, param);
	src = opt;
	while (1) {
		p = strtok(src, ",:");
		src = NULL;
		if (!p)
			break;
		if (!strcmp(p, "common"))
			pflag = &si->value.dbg_flags.common_flag;
		else if (!strcmp(p, "compositor"))
			pflag = &si->value.dbg_flags.compositor_flag;
		else if (!strcmp(p, "drm"))
			pflag = &si->value.dbg_flags.drm_flag;
		else if (!strcmp(p, "gbm"))
			pflag = &si->value.dbg_flags.gbm_flag;
		else if (!strcmp(p, "ps"))
			pflag = &si->value.dbg_flags.ps_flag;
		else if (!strcmp(p, "timer"))
			pflag = &si->value.dbg_flags.timer_flag;
		else if (!strcmp(p, "gles"))
			pflag = &si->value.dbg_flags.gles_flag;
		else if (!strcmp(p, "egl"))
			pflag = &si->value.dbg_flags.egl_flag;
		else {
			fprintf(stderr, "illegal module name %s\n", p);
			exit(1);
		}
		p = strtok(src, ",:");
		if (!p) {
			usage();
			exit(1);
		}
		*pflag = (u8)atoi(p);
		if ((*pflag) > 5) {
			fprintf(stderr, "log level out of range %u\n", *pflag);
			exit(1);
		}
	}
	printf("DEBUG SETTING:\n");
	printf("\tcommon: %u\n", si->value.dbg_flags.common_flag);
	printf("\tcompositor: %u\n", si->value.dbg_flags.compositor_flag);
	printf("\tdrm: %u\n", si->value.dbg_flags.drm_flag);
	printf("\tgbm: %u\n", si->value.dbg_flags.gbm_flag);
	printf("\tps: %u\n", si->value.dbg_flags.ps_flag);
	printf("\ttimer: %u\n", si->value.dbg_flags.timer_flag);
	printf("\tgles: %u\n", si->value.dbg_flags.gles_flag);
	printf("\tegl: %u\n", si->value.dbg_flags.egl_flag);
	free(opt);
}

struct shell_obj {
	s32 sock;
	struct clv_shell_info si;
	struct clv_event_loop *loop;
	struct clv_event_source *source;
	u64 linkid;
	u8 *rx_buf;
	s32 run;
};

static s32 sock_cb(s32 fd, u32 mask, void *data)
{
	struct shell_obj *so = data;
	struct clv_tlv *tlv;
	u8 *tx_buf, *rx_p;
	u32 flag, length, n;
	s32 ret;

	ret = clv_recv(fd, so->rx_buf, sizeof(*tlv) + sizeof(u32));
	if (ret == -1) {
		fprintf(stderr, "server exit.\n");
		so->run = 0;
		return -1;
	} else if (ret < 0) {
		fprintf(stderr, "failed to receive server cmd\n");
		so->run = 0;
		return -1;
	}

	tlv = (struct clv_tlv *)(so->rx_buf + sizeof(u32));
	length = tlv->length;
	rx_p = so->rx_buf + sizeof(u32) + sizeof(*tlv);
	flag = *((u32 *)(so->rx_buf));
	ret = clv_recv(fd, rx_p, length);
	if (ret == -1) {
		fprintf(stderr, "server exit.\n");
		so->run = 0;
		return -1;
	} else if (ret < 0) {
		fprintf(stderr, "failed to receive server cmd\n");
		so->run = 0;
		return -1;
	}
	if (tlv->tag == CLV_TAG_INPUT) {
	} else if (tlv->tag == CLV_TAG_WIN) {
		if (flag & (1 << CLV_CMD_LINK_ID_ACK_SHIFT)) {
			so->linkid = clv_client_parse_link_id(so->rx_buf);
			tx_buf = clv_create_shell_cmd(&so->si, &n);
			so->run = 0;
			if (clv_send(fd, tx_buf, n) < 0) {
				fprintf(stderr, "server exit.\n");
				return -1;
			}
		}
	}

	return 0;
}

s32 main(s32 argc, char **argv)
{
	s32 ch;
	struct shell_obj so;

	while ((ch = getopt_long(argc, argv, shell_short_options,
				 shell_options, NULL)) != -1) {
		switch (ch) {
		case 'l':
			parse_log_param(optarg, &so.si);
			break;
		case 'i':
			so.si.cmd = CLV_SHELL_CANVAS_LAYOUT_QUERY;
			break;
		case 's':
			so.si.cmd = CLV_SHELL_CANVAS_LAYOUT_SETTING;
			break;
		default:
			usage();
			return -1;
		}
	}

	so.rx_buf = malloc(1024);
	if (!so.rx_buf)
		exit(1);

	so.loop = clv_event_loop_create();
	assert(so.loop);

	so.sock = clv_socket_cloexec(PF_LOCAL, SOCK_STREAM, 0);
	clv_socket_connect(so.sock, "/tmp/CLV_SERVER");

	so.source = clv_event_loop_add_fd(so.loop, so.sock, CLV_EVT_READABLE,
					  sock_cb, &so);

	so.run = 1;
	while (so.run) {
		clv_event_loop_dispatch(so.loop, -1);
	}

	clv_event_source_remove(so.source);

	close(so.sock);
	free(so.rx_buf);
	return 0;
}

