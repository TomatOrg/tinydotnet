#pragma once

#include "tinydotnet/types/basic.h"
#include "tinydotnet/types/reflection.h"

typedef struct string_builder {
    char* chars;
} string_builder_t;

void string_builder_push_string(string_builder_t* builder, String str);
void string_builder_push_cstr(string_builder_t* builder, const char* str);
void string_builder_push_char(string_builder_t* builder, char c);
char* string_builder_build(string_builder_t* builder);
void string_builder_free(string_builder_t* builder);

void string_builder_push_type_signature(string_builder_t* builder, RuntimeTypeInfo type);
void string_builder_push_method_signature(string_builder_t* builder, RuntimeMethodBase method, bool full);
