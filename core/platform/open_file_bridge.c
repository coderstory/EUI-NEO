#include "core/platform/open_file_bridge.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <CoreServices/CoreServices.h>
#endif

/* ---------------------------------------------------------------------------
 * Pending queue (portable)
 *
 * A tiny singly linked list of strdup'ed paths. Events arrive on the main
 * thread, but the API is documented as thread-safe because embedders may feed
 * it from worker threads.
 * ------------------------------------------------------------------------ */

typedef struct EuiOpenFileNode {
    struct EuiOpenFileNode* next;
    char path[1]; /* trailing storage for the NUL terminated path */
} EuiOpenFileNode;

static EuiOpenFileNode* g_head = NULL;
static EuiOpenFileNode* g_tail = NULL;
static EuiOpenFileCallback g_callback = NULL;
static void* g_callback_userdata = NULL;

#if defined(_WIN32)
static SRWLOCK g_queue_lock = SRWLOCK_INIT;
static void eui_open_file_lock(void) { AcquireSRWLockExclusive(&g_queue_lock); }
static void eui_open_file_unlock(void) { ReleaseSRWLockExclusive(&g_queue_lock); }
#else
static pthread_mutex_t g_queue_lock = PTHREAD_MUTEX_INITIALIZER;
static void eui_open_file_lock(void) { pthread_mutex_lock(&g_queue_lock); }
static void eui_open_file_unlock(void) { pthread_mutex_unlock(&g_queue_lock); }
#endif

void eui_open_file_push_pending(const char* path) {
    if (path == NULL || path[0] == '\0') {
        return;
    }

    const size_t length = strlen(path);
    EuiOpenFileNode* node = (EuiOpenFileNode*)malloc(sizeof(EuiOpenFileNode) + length);
    if (node == NULL) {
        return;
    }
    memcpy(node->path, path, length + 1u);
    node->next = NULL;

    eui_open_file_lock();
    if (g_tail == NULL) {
        g_head = node;
    } else {
        g_tail->next = node;
    }
    g_tail = node;
    eui_open_file_unlock();
}

static EuiOpenFileNode* eui_open_file_pop(void) {
    eui_open_file_lock();
    EuiOpenFileNode* node = g_head;
    if (node != NULL) {
        g_head = node->next;
        if (g_head == NULL) {
            g_tail = NULL;
        }
    }
    eui_open_file_unlock();
    return node;
}

int eui_platform_consume_pending_open_file(char* out_path, int max_len) {
    if (out_path == NULL || max_len <= 0) {
        return 0;
    }

    EuiOpenFileNode* node = eui_open_file_pop();
    if (node == NULL) {
        return 0;
    }

    const size_t capacity = (size_t)max_len - 1u;
    const size_t length = strlen(node->path);
    const size_t copied = length < capacity ? length : capacity;
    memcpy(out_path, node->path, copied);
    out_path[copied] = '\0';
    free(node);
    return 1;
}

void eui_platform_set_open_file_callback(EuiOpenFileCallback callback, void* userdata) {
    eui_open_file_lock();
    g_callback = callback;
    g_callback_userdata = userdata;
    eui_open_file_unlock();
}

int eui_open_file_dispatch_pending(void) {
    eui_open_file_lock();
    EuiOpenFileCallback callback = g_callback;
    void* userdata = g_callback_userdata;
    eui_open_file_unlock();

    int dispatched = 0;
    for (;;) {
        EuiOpenFileNode* node = eui_open_file_pop();
        if (node == NULL) {
            break;
        }
        ++dispatched;
        if (callback != NULL) {
            callback(node->path, userdata);
        }
        free(node);
    }
    return dispatched;
}

/* ---------------------------------------------------------------------------
 * macOS: kAEOpenDocuments Apple Event handler
 *
 * NSAppleEventManager is used instead of an NSApplicationDelegate because GLFW
 * already owns the application delegate and the tray bridge may replace it;
 * the Apple Event Manager handler is independent of both.
 * ------------------------------------------------------------------------ */

#if defined(__APPLE__)

@interface EUIOpenFileEventHandler : NSObject
- (void)handleOpenDocuments:(NSAppleEventDescriptor*)event
             withReplyEvent:(NSAppleEventDescriptor*)replyEvent;
@end

@implementation EUIOpenFileEventHandler

- (void)handleOpenDocuments:(NSAppleEventDescriptor*)event
             withReplyEvent:(NSAppleEventDescriptor*)replyEvent {
    (void)replyEvent;

    @autoreleasepool {
        /* keyDirectObject holds an AEList of file URLs (typeFileURL) or, for
         * some senders, plain POSIX paths. */
        NSAppleEventDescriptor* list = [event paramDescriptorForKeyword:keyDirectObject];
        const NSInteger itemCount = (list != nil) ? (NSInteger)[list numberOfItems] : 0;

        for (NSInteger index = 1; index <= itemCount; ++index) {
            NSAppleEventDescriptor* item = [list descriptorAtIndex:index];
            if (item == nil) {
                continue;
            }

            NSString* value = [item stringValue];
            if (value == nil || [value length] == 0) {
                continue;
            }

            NSURL* url = [NSURL URLWithString:value];
            NSString* path = (url != nil && [url isFileURL]) ? [url path] : value;
            if (path == nil || [path length] == 0) {
                continue;
            }

            const char* utf8 = [path UTF8String];
            if (utf8 != NULL) {
                eui_open_file_push_pending(utf8);
            }
        }
    }
}

@end

static EUIOpenFileEventHandler* g_event_handler = nil;
static int g_handler_installed = 0;

int eui_open_file_install_handler(void) {
    if (g_handler_installed) {
        return 1;
    }

    @autoreleasepool {
        [NSApplication sharedApplication];

        g_event_handler = [[EUIOpenFileEventHandler alloc] init];
        if (g_event_handler == nil) {
            return 0;
        }

        [[NSAppleEventManager sharedAppleEventManager]
            setEventHandler:g_event_handler
                andSelector:@selector(handleOpenDocuments:withReplyEvent:)
              forEventClass:kCoreEventClass
                 andEventID:kAEOpenDocuments];

        g_handler_installed = 1;
    }
    return 1;
}

#else

int eui_open_file_install_handler(void) {
    return 0;
}

#endif
