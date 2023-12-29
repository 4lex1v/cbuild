
#pragma once

#include "anyfin/base.hpp"

// TODO: Move this API to anyfin

bool contains_key (const u64 *array, usize count, u64 key); 

bool find_offset (usize *result, const u64 *array, usize count, u64 value); 
