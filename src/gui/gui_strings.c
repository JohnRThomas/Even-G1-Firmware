/*
 * GUI language strings: per-language string tables and the lookup by string id.
 */
#include <glasses_state.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#include "../storage/storage.h"
#include "gui.h"
#include "gui_strings.h"

const char *get_string_in_language(enum language language, uint32_t string_id) {
  if (string_id > ((uint32_t)GUI_STRING_COUNT - 1U)) {
    return "ERR_STRING";
  }

  switch (language) {
    case LANGUAGE_CHINESE:
      return strings_chinese[string_id];
    case LANGUAGE_GERMAN:
      return strings_german[string_id];
    case LANGUAGE_SPANISH:
      return strings_spanish[string_id];
    case LANGUAGE_FRENCH:
      return strings_french[string_id];
    case LANGUAGE_ITALIAN:
      return strings_italian[string_id];
    case LANGUAGE_JAPANESE:
      return strings_japanese[string_id];
    case LANGUAGE_KOREAN:
      return strings_korean[string_id];
    default:
      return strings_english[string_id];
  }
}

const char *get_string(uint8_t string_id) {
  enum language language = get_current_language();

  switch (language) {
    case LANGUAGE_ENGLISH:
    case LANGUAGE_FRENCH:
    case LANGUAGE_GERMAN:
    case LANGUAGE_SPANISH:
    case LANGUAGE_ITALIAN:
      break;
    default:
      language = LANGUAGE_ENGLISH;
      break;
  }
  return get_string_in_language(language, string_id);
}
