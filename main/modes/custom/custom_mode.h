#pragma once
#include "navigation/mode.h"

typedef struct {
    bool has_content;
    void *view;
} custom_mode_t;

mode_t custom_mode_descriptor(custom_mode_t *custom);
void custom_mode_set_content_available(custom_mode_t *custom, bool available);
