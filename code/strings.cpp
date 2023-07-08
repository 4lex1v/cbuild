
#include "strings.hpp"
#include "runtime.hpp"

static String build_string (const String_Builder *builder, bool use_separator, char separator) {
  if (not builder->length) return {};
    
  auto reservation_size = builder->length + 1;
  if (use_separator) reservation_size += builder->sections.count;

  auto buffer = reserve_array(builder->arena, reservation_size);

  usize offset = 0;
  for (auto section: builder->sections) {
    assert(section.length > 0);
      
    copy_memory(buffer + offset, section.value, section.length);
    offset += section.length;

    if (use_separator) buffer[offset++] = separator;
  }

  buffer[offset] = '\0';  

  return { buffer, offset };
}

String build_string (const String_Builder *builder) {
  return build_string(builder, false, 0);
}

String build_string_with_separator (const String_Builder *builder, char separator) {
  return build_string(builder, true, separator);
}
