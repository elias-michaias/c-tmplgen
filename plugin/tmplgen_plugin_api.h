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
void tmplgen_plugin_reset_captured_diagnostic(void);
const char *tmplgen_plugin_captured_diagnostic_message(void);
const char *tmplgen_plugin_captured_diagnostic_path(void);
size_t tmplgen_plugin_captured_diagnostic_line(void);

#ifdef __cplusplus
}
#endif

#endif
