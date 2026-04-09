#pragma once
#include "config.h"

// Called after save to apply live-applicable settings (brightness, LED, etc.)
typedef void (*ConfigApplyFn)(const Config &);

// Start the web server. cfg is used to pre-fill the form and updated on save.
// on_apply (optional) is called immediately after each successful save.
void web_server_start(Config &cfg, ConfigApplyFn on_apply = nullptr);

// Must be called in loop() to handle incoming requests.
void web_server_handle();
