#ifndef TMPLGEN_PLUGIN_API_H
#define TMPLGEN_PLUGIN_API_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int tmplgen_plugin_main(int argc, char **argv);
void tmplgen_plugin_set_unsaved_source(const char *path,
                                      const char *contents, size_t length);
void tmplgen_plugin_clear_unsaved_source(void);

#ifdef __cplusplus
}
#endif

#endif
