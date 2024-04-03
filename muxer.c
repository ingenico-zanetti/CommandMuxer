// Utility to insert command in the data stream of a modem
// It uses +++ (with the appropriate delays) to go back to command mode
// User has to send ATO before switching back to data mode sendign a CR
// The process start in "DATA" mode, just type in a COMMAND,
// the MODEM will be switched to COMMAND mode using 2s-pause +++ 1s-pause
// then the command will be sent ; end the COMMAND session sending ATO
// followed by an empty line
// During COMMAND mode, data received from pppd are simply not read,
// they will be read again when going back to DATA mode
// You might need some sort of sudo sysctl fs.protected_symlinks=0
// to allow pppd to run from /tmp/modem that is a symlink to the actual pty

#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

int create_pty(char *name, int name_length){
	int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
	if(master != -1){
		grantpt(master);
		unlockpt(master);
		struct termios tio;
		tcgetattr(master, &tio);
		cfmakeraw(&tio);
		tcsetattr(master, TCSANOW, &tio);
		if(name){
			int r = ptsname_r(master, name, name_length);
			if(r){
				fprintf(stderr, "ptsname_r():%d (%s)" "\n", errno, strerror(errno));
			}else{
				chmod(name, 0666);
			}
		}
	}else{
		fprintf(stderr, "posix_openpt():%d (%s)" "\n", errno, strerror(errno));
	}
	return(master);
}

#define DGPRS_PTY_NAME "/tmp/modem"

#define PTSNAME_MAX_LENGTH (64)

int open_tty(char *name){
	int device = open(name, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if(device != -1){
		struct termios tio;
		tcgetattr(device, &tio);
		cfmakeraw(&tio);
		tcsetattr(device, TCSANOW, &tio);
	}else{
		fprintf(stderr, "open(%s):%d (%s)" "\n", name, errno, strerror(errno));
	}
	return(device);
}

typedef enum {
	MODE_DATA = 0,
	MODE_COMMAND = 1
} mode_e;

typedef struct {
	int keyboard;
	int tty;
	int pty;
	int max;
	int screen;
	mode_e mode;
} muxer_context_s;

void update_max(muxer_context_s *ctx, int n){
	if(n > ctx->max){
		ctx->max = n;
	}
}

void transparent_init(muxer_context_s *ctx, int kbd, int tty, int pty, int screen){
	ctx->mode = MODE_DATA;
	ctx->max = -1;
	ctx->keyboard = kbd; update_max(ctx, kbd);
	ctx->tty      = tty; update_max(ctx, tty);
	ctx->pty      = pty; update_max(ctx, pty);
	ctx->screen   = screen;
	fprintf(stderr, "%s(%d, %d, %d):max=%d" "\n", __func__, kbd, tty, pty, ctx->max);
}

void search_and_replace(char *s, int l, char from, char to){
	char *p = s;
	while(l-- > 0){
		char c = *p++;
		if(from == c){
			p[-1] = to;
		}
	}
}

void transparent_run(muxer_context_s *ctx){
	for(;;){
		fd_set fd_in;
		FD_ZERO(&fd_in);
		FD_SET(ctx->keyboard, &fd_in);
		FD_SET(ctx->tty, &fd_in);
		if(MODE_DATA == ctx->mode){
			FD_SET(ctx->pty, &fd_in);
		}
		struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};

		int selected = select(ctx->max + 1, &fd_in, NULL, NULL, &timeout);
		if(-1 == selected){
			fprintf(stderr, "select: %d (%s)" "\n", errno, strerror(errno));
			break;
		}
		if(0 == selected){
			// fprintf(stderr, "timeout" "\n");
		}else{
			if(FD_ISSET(ctx->keyboard, &fd_in)){
				if(MODE_DATA == ctx->mode){
					fprintf(stderr, "switching to COMMAND mode" "\n");
					ctx->mode = MODE_COMMAND;
					sleep(2);
					const char *plusplusplus = "+++";
					write(ctx->tty, plusplusplus, strlen(plusplusplus));
					sleep(1);
				}else{
					char buffer[64];
					int lus = read(ctx->keyboard, buffer, sizeof(buffer));
					if(lus > 0){
						if((1 == lus) && ('\n' == buffer[0])){
							fprintf(stderr, "switching to DATA mode" "\n");
							ctx->mode = MODE_DATA;
						}else{
							search_and_replace(buffer, lus, '\n', '\r');
							int ecrits = write(ctx->tty, buffer, lus);
							// fprintf(stderr, "%s@%d:ecrits=%d" "\n", __func__, __LINE__, ecrits);
							// fprintf(stderr, "%s@%d:buffer[%d]=%d" "\n", __func__, __LINE__, ecrits - 1, buffer[ecrits - 1]);
						}
					}else{
						break;
					}
				}
			}
			if(FD_ISSET(ctx->tty, &fd_in)){
				char buffer[64];
				int lus = read(ctx->tty, buffer, sizeof(buffer));
				if(lus > 0){
					if(MODE_DATA == ctx->mode){
						write(ctx->pty, buffer, lus);
					}else{
						write(ctx->screen, buffer, lus);
					}
				}else{
					break;
				}
			}
			if(FD_ISSET(ctx->pty, &fd_in)){
				char buffer[64];
				int lus = read(ctx->pty, buffer, sizeof(buffer));
				if(lus > 0){
					write(ctx->tty, buffer, lus);
				}else{
					break;
				}
			}
		}
	}
}

int main(int argc, char *argv[]){
	muxer_context_s context;
	int tty = -1;
	if(argc > 1){
		tty = open_tty(argv[1]);
	}else{
		fprintf(stderr, "Usage: % /dev/ttyUSBx" "\n", argv[0]);
	}
	if(-1 == tty){
		return(-1);
	}
	char name[PTSNAME_MAX_LENGTH + 1];
	int master = create_pty(name, sizeof(name) - 1);
	if(master != -1){
		fprintf(stderr, "master=%d, name=%s" "\n", master, name);
		int r = symlink(name, DGPRS_PTY_NAME);
		transparent_init(&context, STDIN_FILENO, tty, master, STDOUT_FILENO);
		transparent_run(&context);
		unlink(DGPRS_PTY_NAME);
		close(master);
	}
	close(tty);
	return(0);
}



