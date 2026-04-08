#pragma once
#include "config.h"

// Start the web server. cfg is used to pre-fill the form and updated on save.
void web_server_start(Config &cfg);

// Must be called in loop() to handle incoming requests.
void web_server_handle();
