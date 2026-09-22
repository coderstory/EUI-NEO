#pragma once

/*
 * Open-file bridge.
 *
 * Delivers file paths that the desktop environment asks the application to
 * open, i.e. "double click in the file manager" / "Open With":
 *
 *   - macOS: the `kAEOpenDocuments` Apple Event (kCoreEventClass), sent by the
 *     Finder, by `open -a MyApp file.md` and by drag & drop onto the Dock icon.
 *     GLFW/SDL2 never see it, so it is handled here through NSAppleEventManager.
 *   - other platforms: no native source yet; the queue API still works so
 *     embedders can push paths themselves (or read `argv`).
 *
 * Incoming paths are pushed into a thread-safe FIFO queue. The application
 * drains it in one of two ways:
 *
 *   1. callback style (recommended): register with
 *      eui_platform_set_open_file_callback() and call
 *      eui_open_file_dispatch_pending() from the main loop.
 *   2. polling style: call eui_platform_consume_pending_open_file() yourself.
 *
 * Do not mix both styles: each pending path is handed out exactly once.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Invoked on the thread that calls eui_open_file_dispatch_pending(). */
typedef void (*EuiOpenFileCallback)(const char* path, void* userdata);

/*
 * Install the native open-file handler. Idempotent.
 *
 * Call it on the main thread *before* the event loop starts (before the first
 * glfwPollEvents()/SDL_PollEvent()) so that a launch-time "open with" request
 * is queued instead of being dropped. Returns 1 when the platform provides a
 * native source, 0 when it does not.
 */
int eui_open_file_install_handler(void);

/*
 * Register the single global callback used by eui_open_file_dispatch_pending().
 * Passing NULL detaches the callback; pending paths are then dropped by the
 * next dispatch. Installing a handler is not implied: call
 * eui_open_file_install_handler() too when you want native events.
 */
void eui_platform_set_open_file_callback(EuiOpenFileCallback callback, void* userdata);

/*
 * Push one path onto the pending queue. Thread-safe. NULL/empty paths are
 * ignored. Useful for tests, for extra sources (e.g. argv) and for platforms
 * without a native handler.
 */
void eui_open_file_push_pending(const char* path);

/*
 * Pop the oldest pending path into out_path (always NUL terminated, truncated
 * to max_len - 1 bytes). Returns 1 when a path was written, 0 when the queue
 * is empty or the arguments are invalid.
 */
int eui_platform_consume_pending_open_file(char* out_path, int max_len);

/*
 * Drain the queue, invoking the registered callback once per path. Returns the
 * number of paths handled. Call from the main thread.
 */
int eui_open_file_dispatch_pending(void);

#ifdef __cplusplus
}
#endif
