#pragma once
#include "config.h"
#include "github_api.h"

void rgb_led_init(const Config &cfg);
void rgb_led_update_params(const GithubData &data, const Config &cfg);
void rgb_led_tick();
