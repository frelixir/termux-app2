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
#include <sys/un.h>
#include <android/looper.h>
#include <sys/mman.h>
#include <asm-generic/ioctls.h>
#include "waylandRenderServer.h"
#include "buffer.h"
#include "lorie.h"

#define MAX_WAITING_CONNECT_CLIENTS 5
#define SOCKET_PATH "/data/data/com.termux/files/home/.wayland/unix_socket"
#define log(prio, ...) __android_log_print(ANDROID_LOG_ ## prio, "LorieNative", __VA_ARGS__)
#define min(a, b) (((a) < (b)) ? (a) : (b))

extern int conn_fd;
static struct xorg_list registeredWaylandBuffers;

static struct {
    jclass self;
    jmethodID getInstance, clientConnectedStateChanged, resetIme;
} MainActivity = {0};

static struct {
    jclass self;
    jmethodID forName;
    jmethodID decode;
} Charset = {0};

static struct {
    jclass self;
    jmethodID toString;
} CharBuffer = {0};

static JNIEnv *guienv = NULL;
static jobject globalThiz = NULL;

static void waylandSendSharedServerState(int memfd) {
    if (conn_fd != -1) {
        lorieEvent e = { .type = EVENT_SHARED_SERVER_STATE };
        write(conn_fd, &e, sizeof(e));
        ancil_send_fd(conn_fd, memfd);
    }
}

static void waylandRegisterBuffer(LorieBuffer* buffer) {
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

static void waylandUnregisterBuffer(LorieBuffer* buffer) {
    unsigned long id;
    if (!buffer || (!LorieBufferList_findById(&registeredWaylandBuffers, (id = LorieBuffer_description(buffer)->id))))
        return;  // Not exist or not registered so no need to unregister

    if (conn_fd != -1 && buffer) {
        lorieEvent e = { .removeBuffer = { .t = EVENT_REMOVE_BUFFER, .id = id } };
        write(conn_fd, &e, sizeof(e));
        LorieBuffer_removeFromList(buffer);
    }
}
static int process(int fd, int events, __unused void* data) {
    JNIEnv *env = guienv;
    jobject thiz = globalThiz;

    if (events & (ALOOPER_EVENT_ERROR | ALOOPER_EVENT_HANGUP)) {
        jobject instance = (*env)->CallStaticObjectMethod(env, MainActivity.self, MainActivity.getInstance);
        if (instance)
            (*env)->CallVoidMethod(env, instance, MainActivity.clientConnectedStateChanged);

        ALooper_removeFd(ALooper_forThread(), fd);
        close(conn_fd);
        conn_fd = -1;
        rendererSetSharedState(NULL);
        rendererRemoveAllBuffers();
        log(DEBUG, "disconnected");
        return 1;
    }

    if (conn_fd != -1) {
        lorieEvent e = {0};

        again:
        if (read(conn_fd, &e, sizeof(e)) == sizeof(e)) {
            switch(e.type) {
                case EVENT_APPLY_SERVER_STATE: {
                    struct lorie_shared_server_state* state = NULL;
                    int stateFd = -1;
                    if (-1 == (stateFd = LorieBuffer_createRegion("wayland", sizeof(*state)))) {
                        dprintf(2, "FATAL: Failed to allocate server state.\n");
                        _exit(1);
                    }

                    if (!(state = mmap(NULL, sizeof(*state), PROT_READ|PROT_WRITE, MAP_SHARED, stateFd, 0))) {
                        dprintf(2, "FATAL: Failed to map server state.\n");
                        _exit(1);
                    }
                    waylandSendSharedServerState(stateFd);
                    rendererSetSharedState(state);
                    break;
                }
                case EVENT_APPLY_BUFFER:{
                    LorieBuffer* buffer = LorieBuffer_allocate(e.screenSize.width, e.screenSize.height,e.screenSize.format, e.screenSize.pixel_type);
                    waylandRegisterBuffer(buffer);
                    break;
                }
                case EVENT_DESTROY_BUFFER: {
                    waylandUnregisterBuffer(LorieBufferList_findById(&registeredWaylandBuffers,e.removeBuffer.id));
                    break;
                }
                case EVENT_WINDOW_FOCUS_CHANGED: {
                    (*env)->CallVoidMethod(env, thiz, MainActivity.resetIme);
                }
            }
        }

        int n;
        if (ioctl(conn_fd, FIONREAD, &n) >= 0 && n > sizeof(e))
            goto again;
    }
    return 1;
}

static void serv(JavaVM *vm, jint fd) {
    if (conn_fd != -1) {
        ALooper_removeFd(ALooper_forThread(), conn_fd);
        close(conn_fd);
        rendererSetSharedState(NULL);
        rendererRemoveAllBuffers();
        log(DEBUG, "disconnected");
    }
    JNIEnv* env;
    (*vm)->AttachCurrentThread(vm, &env, NULL);
    if ((conn_fd = fd) != -1) {
        ALooper_addFd(ALooper_forThread(), fd, 0, ALOOPER_EVENT_INPUT | ALOOPER_EVENT_ERROR | ALOOPER_EVENT_HANGUP, process, NULL);
        log(DEBUG, "XCB connection is successfull");
    }
}

static void startRenderServer(JavaVM *vm) {
    conn_fd=-1;
    int server_fd, client_fd, count;
    struct sockaddr_un address;
    uint8_t buffer[512] = {0};

    // 创建socket
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log(ERROR,"Socket creation failed: %s", strerror(errno));
        return;
    }

    // 绑定socket文件路径，先unlink避免路径已存在
    unlink(SOCKET_PATH);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, SOCKET_PATH, sizeof(address.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        log(ERROR,"Socket bind failed: %s", strerror(errno));
        close(server_fd);
        return;
    }

    // 监听连接
    if (listen(server_fd, MAX_WAITING_CONNECT_CLIENTS) < 0) {
        log(ERROR,"Socket listen failed: %s", strerror(errno));
        close(server_fd);
        unlink(SOCKET_PATH);
        return;
    }

    log(DEBUG,"Unix domain socket server listening at %s", SOCKET_PATH);

    while (1) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            log(ERROR,"Socket accept failed: %s", strerror(errno));
            continue;
        }

        count = read(client_fd, buffer, sizeof(buffer));
        if (count > 0) {
            if (!memcmp(buffer, MAGIC, count < (int)sizeof(MAGIC) ? count : (int)sizeof(MAGIC))) {
                log(DEBUG,"New client connection!");
                lorieEvent e = {.type = EVENT_VERIFY_SUCCEED};
                write(client_fd, &e, sizeof(e));
                serv(vm, client_fd);
            }else{
                close(client_fd);
                log(ERROR,"Invalid client connection!");
            }
        }
    }

    close(server_fd);
    unlink(SOCKET_PATH);
}
static jclass FindClassOrDie(JNIEnv *env, const char* name) {
    jclass clazz = (*env)->FindClass(env, name);
    if (!clazz) {
        char buffer[1024] = {0};
        sprintf(buffer, "class %s not found", name);
        log(ERROR, "%s", buffer);
        (*env)->FatalError(env, buffer);
        return NULL;
    }

    return (*env)->NewGlobalRef(env, clazz);
}

static jclass FindMethodOrDie(JNIEnv *env, jclass clazz, const char* name, const char* signature, jboolean isStatic) {
    __typeof__((*env)->GetMethodID) getMethodID = isStatic ? (*env)->GetStaticMethodID : (*env)->GetMethodID;
    jmethodID method = getMethodID(env, clazz, name, signature);
    if (!method) {
        char buffer[1024] = {0};
        sprintf(buffer, "method %s %s not found", name, signature);
        log(ERROR, "%s", buffer);
        (*env)->FatalError(env, buffer);
        return NULL;
    }

    return method;
}
void setGlobalEnv(JNIEnv* env,jobject obj){
    guienv=env;
    globalThiz=obj;
    if (!Charset.self) {
        // Init clipboard-related JNI stuff
        Charset.self = FindClassOrDie(env, "java/nio/charset/Charset");
        Charset.forName = FindMethodOrDie(env, Charset.self, "forName", "(Ljava/lang/String;)Ljava/nio/charset/Charset;", JNI_TRUE);
        Charset.decode = FindMethodOrDie(env, Charset.self, "decode", "(Ljava/nio/ByteBuffer;)Ljava/nio/CharBuffer;", JNI_FALSE);

        CharBuffer.self = FindClassOrDie(env,  "java/nio/CharBuffer");
        CharBuffer.toString = FindMethodOrDie(env, CharBuffer.self, "toString", "()Ljava/lang/String;", JNI_FALSE);

        MainActivity.self = FindClassOrDie(env,  "com/termux/x11/MainActivity");
        MainActivity.getInstance = FindMethodOrDie(env, MainActivity.self, "getInstance", "()Lcom/termux/x11/MainActivity;", JNI_TRUE);
        MainActivity.clientConnectedStateChanged = FindMethodOrDie(env, MainActivity.self, "clientConnectedStateChanged", "()V", JNI_FALSE);
        MainActivity.resetIme = FindMethodOrDie(env, (*env)->GetObjectClass(env, globalThiz), "resetIme", "()V", JNI_FALSE);
    }
}
void waylandRenderInit(JavaVM *vm) {
    pthread_t t;
    xorg_list_init(&registeredWaylandBuffers);
    JNIEnv* env;
    (*vm)->AttachCurrentThread(vm, &env, NULL);
    pthread_create(&t, NULL, (void *(*)(void *)) startRenderServer, vm);
}
