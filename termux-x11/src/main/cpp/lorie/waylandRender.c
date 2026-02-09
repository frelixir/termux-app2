#include <jni.h>
#include <android/log.h>
#include <sys/socket.h>
#include <linux/in.h>
#include <string.h>
#include <sys/endian.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include "waylandRender.h"
#include "buffer.h"
#include "lorie.h"

#define log(prio, ...) __android_log_print(ANDROID_LOG_ ## prio, "LorieNative", __VA_ARGS__)
#define min(a, b) (((a) < (b)) ? (a) : (b))
static int PORT = 7890;
static char MAGIC[] = "0xDEADPORK";
static int connect_fd =-1;
static void startRenderServer() {
    int server_fd, client, count;
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr = {.s_addr = INADDR_ANY}, .sin_port = htons(
            PORT)};
    int addrlen = sizeof(address);

    uint8_t buffer[512] = {0};

    // Even in the case if it will fail for some reason everything will work fine
    // But connection will be delayed a bit

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        log(ERROR, "Socket creation failed: %s", strerror(errno));
        return;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &(int) {1}, sizeof(int));
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &(int) {1}, sizeof(int));

    if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0) {
        log(ERROR, "Socket bind failed: %s", strerror(errno));
        close(server_fd);
        return;
    }

    if (listen(server_fd, 5) < 0) {
        log(ERROR, "Socket listen failed: %s", strerror(errno));
        close(server_fd);
        return;
    }

    while (1) {
        if ((client = accept(server_fd, (struct sockaddr *) &address, (socklen_t *) &addrlen)) <
            0) {
            log(ERROR, "Socket accept failed: %s", strerror(errno));
            continue;
        }

        if ((count = read(client, buffer, sizeof(buffer))) > 0) {
            if (!memcmp(buffer, MAGIC, min(count, (int)sizeof(MAGIC)))) {
                log(DEBUG, "New client connection!\n");
            }
        }
        close(client);
    }
}
static void lorieSendSharedServerState(int memfd) {
    if (conn_fd != -1) {
        lorieEvent e = { .type = EVENT_SHARED_SERVER_STATE };
        write(conn_fd, &e, sizeof(e));
        ancil_send_fd(conn_fd, memfd);
    }
}
static void lorieActivityConnected(void) {
    lorieSendSharedServerState(pvfb->stateFd);
    lorieRegisterBuffer(LORIE_BUFFER_FROM_PIXMAP(pScreenPtr->devPrivate));
}
static int addFd() {
//    InputThreadRegisterDev((int) (int64_t) closure, handleLorieEvents, NULL);
//    conn_fd = (int) (int64_t) closure;
    lorieActivityConnected();
    return 1;
}
void waylandRenderInit(JNIEnv *env){
    pthread_t t;
    JavaVM *vm;

    (*env)->GetJavaVM(env, &vm);

    pthread_create(&t, NULL, (void*(*)(void*)) startRenderServer, vm);
}
