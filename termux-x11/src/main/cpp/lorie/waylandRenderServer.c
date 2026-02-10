#include <jni.h>
#include <android/log.h>
#include <sys/socket.h>
#include <linux/in.h>
#include <string.h>
#include <sys/endian.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <list.h>
#include "waylandRenderServer.h"
#include "buffer.h"
#include "lorie.h"

#define log(prio, ...) __android_log_print(ANDROID_LOG_ ## prio, "LorieNative", __VA_ARGS__)
#define min(a, b) (((a) < (b)) ? (a) : (b))
static int WAYLAND_PORT = 7890;
static char WAYLAND_MAGIC[] = "0xDEADPORK";
extern int conn_fd;
static struct xorg_list registeredWaylandBuffers;
static void startRenderServer() {
    int server_fd, client, count;
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr = {.s_addr = INADDR_ANY}, .sin_port = htons(
            WAYLAND_PORT)};
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
            if (!memcmp(buffer, WAYLAND_MAGIC, min(count, (int)sizeof(WAYLAND_MAGIC)))) {
                log(DEBUG, "New client connection!\n");
            }
        }
        close(client);
    }
}
void waylandRegisterBuffer(LorieBuffer* buffer) {
    unsigned long id = LorieBuffer_description(buffer)->id;
    if (conn_fd == -1 || LorieBufferList_findById(&registeredWaylandBuffers, id))
        return; // Already registered

    if (conn_fd != -1 && buffer) {
        lorieEvent e = { .type = EVENT_ADD_BUFFER };
        write(conn_fd, &e, sizeof(e));
        LorieBuffer_sendHandleToUnixSocket(buffer, conn_fd);
        LorieBuffer_addToList(buffer, &registeredWaylandBuffers);
        const LorieBuffer_Desc* desc = LorieBuffer_description(buffer);
        log(INFO, "Sent shared buffer width %d stride %d height %d format %d type %d id %llu", desc->width, desc->stride, desc->height, desc->format, desc->type, desc->id);
    }
}

void waylandUnregisterBuffer(LorieBuffer* buffer) {
    unsigned long id;
    if (!buffer || (!LorieBufferList_findById(&registeredWaylandBuffers, (id = LorieBuffer_description(buffer)->id))))
        return;  // Not exist or not registered so no need to unregister

    if (conn_fd != -1 && buffer) {
        lorieEvent e = { .removeBuffer = { .t = EVENT_REMOVE_BUFFER, .id = id } };
        write(conn_fd, &e, sizeof(e));
        LorieBuffer_removeFromList(buffer);
    }
}
static void waylandSendSharedServerState(int memfd) {
    if (conn_fd != -1) {
        lorieEvent e = { .type = EVENT_SHARED_SERVER_STATE };
        write(conn_fd, &e, sizeof(e));
        ancil_send_fd(conn_fd, memfd);
    }
}
static void waylandActivityConnected(void) {
    waylandRegisterBuffer(NULL);
}

void waylandRenderInit(JNIEnv *env){
    pthread_t t;
    JavaVM *vm;

    (*env)->GetJavaVM(env, &vm);
    xorg_list_init(&registeredWaylandBuffers);
    pthread_create(&t, NULL, (void*(*)(void*)) startRenderServer, vm);
}
