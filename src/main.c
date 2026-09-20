#include "browser.h"
#include <locale.h>
#include <signal.h>
#include <execinfo.h>
#include <stdlib.h>

static gchar **g_saved_argv = NULL;
static gint    g_saved_argc = 0;

static void crash_handler(int sig) {
    void *array[30];
    int size = backtrace(array, 30);
    fprintf(stderr, "\n=== SIGSEGV (signal %d) ===\n", sig);
    backtrace_symbols_fd(array, size, 2);
    _exit(1);
}

gchar **zhi_get_saved_argv(void) { return g_saved_argv; }
gint    zhi_get_saved_argc(void) { return g_saved_argc; }

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);

    g_setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", TRUE);
    g_setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", TRUE);

    g_saved_argc = argc;
    g_saved_argv = g_new(gchar*, argc + 1);
    for (int i = 0; i < argc; i++)
        g_saved_argv[i] = g_strdup(argv[i]);
    g_saved_argv[argc] = NULL;

    gtk_init(&argc, &argv);
    webkit_web_context_set_sandbox_enabled(webkit_web_context_get_default(), FALSE);

    ZhiConfig *init_config = zhi_config_new();
    zhi_config_load(init_config);
    zhi_config_free(init_config);

    ZhiBrowser *browser = zhi_browser_new();
    zhi_browser_show(browser);

    gtk_main();

    zhi_browser_free(browser);
    return 0;
}
