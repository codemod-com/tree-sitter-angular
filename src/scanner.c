#include "tag.h"
#include "tree_sitter/parser.h"

#include <wctype.h>

enum TokenType
{
  START_TAG_NAME,
  SCRIPT_START_TAG_NAME,
  STYLE_START_TAG_NAME,
  END_TAG_NAME,
  ERRONEOUS_END_TAG_NAME,
  SELF_CLOSING_TAG_DELIMITER,
  IMPLICIT_END_TAG,
  RAW_TEXT,
  COMMENT,
  INTERPOLATION_START,
  INTERPOLATION_END,
  // CONTROL_FLOW_START,

  IF_START,
  ELSE_START,
  FOR_START,
  SWITCH_START,
  CASE_START,
  DEFAULT_START,
  DEFER_START,
  LET_START,
  EMPTY_START,
  PLACEHOLDER_START,
  LOADING_START,
  ERROR_START,
  ELSE_IF_START,
  AT_SIGN,
};

static const char *CONTROL_FLOW_KEYWORDS[] = {
    "if",
    "else",
    "for",
    "switch",
    "case",
    "default",
    "defer",
    "let",
    "empty",
    "placeholder",
    "loading",
    "error",
    "else if" // Multi-word keyword
};

typedef struct
{
  Array(Tag) tags;
  bool seen_at;
} Scanner;

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static unsigned serialize(Scanner *scanner, char *buffer)
{
  uint16_t tag_count =
      scanner->tags.size > UINT16_MAX ? UINT16_MAX : scanner->tags.size;
  uint16_t serialized_tag_count = 0;

  unsigned size = sizeof(tag_count);
  memcpy(&buffer[size], &tag_count, sizeof(tag_count));
  size += sizeof(tag_count);

  for (; serialized_tag_count < tag_count; serialized_tag_count++)
  {
    Tag tag = scanner->tags.contents[serialized_tag_count];

    if (tag.type == CUSTOM)
    {
      unsigned name_length = tag.custom_tag_name.size;
      if (name_length > UINT8_MAX)
      {
        name_length = UINT8_MAX;
      }

      if (size + 2 + name_length >= TREE_SITTER_SERIALIZATION_BUFFER_SIZE)
      {
        break;
      }

      // TODO try to cast to char
      buffer[size++] =
          (unsigned char)tag.type; // <-- This is because otherwise it is read
                                   // as a negative value
      buffer[size++] = (unsigned char)name_length;
      strncpy(&buffer[size], tag.custom_tag_name.contents, name_length);
      size += name_length;
    }
    else
    {
      if (size + 1 >= TREE_SITTER_SERIALIZATION_BUFFER_SIZE)
      {
        break;
      }
      buffer[size++] = (char)tag.type;
    }
  }

  memcpy(&buffer[0], &serialized_tag_count, sizeof(serialized_tag_count));
  return size;
}

static void deserialize(Scanner *scanner, const char *buffer, unsigned length)
{
  for (unsigned i = 0; i < scanner->tags.size; i++)
  {
    tag_free(&scanner->tags.contents[i]);
  }

  array_clear(&scanner->tags);

  if (length > 0)
  {
    unsigned size = 0;
    uint16_t tag_count = 0;
    uint16_t serialized_tag_count = 0;

    memcpy(&serialized_tag_count, &buffer[size], sizeof(serialized_tag_count));
    size += sizeof(serialized_tag_count);

    memcpy(&tag_count, &buffer[size], sizeof(tag_count));
    size += sizeof(tag_count);

    array_reserve(&scanner->tags, tag_count);

    if (tag_count > 0)
    {
      unsigned iter = 0;

      for (iter = 0; iter < serialized_tag_count; iter++)
      {
        Tag tag = tag_new();

        // Try to cast to char
        tag.type =
            (unsigned char)buffer[size++]; // <-- This is because otherwise it
                                           // is read as a negative value
        if (tag.type == CUSTOM)
        {
          uint16_t name_length = (uint8_t)buffer[size++];
          array_reserve(&tag.custom_tag_name, name_length);
          tag.custom_tag_name.size = name_length;
          memcpy(tag.custom_tag_name.contents, &buffer[size], name_length);
          size += name_length;
        }
        array_push(&scanner->tags, tag);
      }
      // add zero tags if we didn't read enough, this is because the
      // buffer had no more room but we held more tags.
      for (; iter < tag_count; iter++)
      {
        array_push(&scanner->tags, tag_new());
      }
    }
  }
}

static String scan_tag_name(TSLexer *lexer)
{
  String tag_name = array_new();

  while (iswalnum(lexer->lookahead) || lexer->lookahead == '-' ||
         lexer->lookahead == ':')
  {
    array_push(&tag_name, towupper(lexer->lookahead));
    advance(lexer);
  }

  return tag_name;
}

static bool scan_comment(TSLexer *lexer)
{
  if (lexer->lookahead != '-')
  {
    return false;
  }
  advance(lexer);

  if (lexer->lookahead != '-')
  {
    return false;
  }
  advance(lexer);

  unsigned dashes = 0;
  while (lexer->lookahead)
  {
    switch (lexer->lookahead)
    {
    case '-':
      ++dashes;
      break;
    case '>':
      if (dashes >= 2)
      {
        lexer->result_symbol = COMMENT;
        advance(lexer);
        lexer->mark_end(lexer);
        return true;
      }
    default:
      dashes = 0;
    }
    advance(lexer);
  }
  return false;
}

static bool scan_raw_text(Scanner *scanner, TSLexer *lexer)
{
  if (scanner->tags.size == 0)
  {
    return false;
  }

  lexer->mark_end(lexer);

  const char *end_delimiter = array_back(&scanner->tags)->type == SCRIPT ? "</SCRIPT" : "</STYLE";

  unsigned delimiter_index = 0;
  while (lexer->lookahead)
  {
    if (towupper(lexer->lookahead) == end_delimiter[delimiter_index])
    {
      delimiter_index++;
      if (delimiter_index == strlen(end_delimiter))
      {
        break;
      }
      advance(lexer);
    }
    else
    {
      delimiter_index = 0;
      advance(lexer);
      lexer->mark_end(lexer);
    }
  }

  lexer->result_symbol = RAW_TEXT;
  return true;
}

static void pop_tag(Scanner *scanner)
{
  Tag popped_tag = array_pop(&scanner->tags);
  tag_free(&popped_tag);
}

static bool scan_implicit_end_tag(Scanner *scanner, TSLexer *lexer)
{
  Tag *parent = scanner->tags.size == 0 ? NULL : array_back(&scanner->tags);

  bool is_closing_tag = false;
  if (lexer->lookahead == '/')
  {
    is_closing_tag = true;
    advance(lexer);
  }
  else
  {
    if (parent && tag_is_void(parent))
    {
      pop_tag(scanner);
      lexer->result_symbol = IMPLICIT_END_TAG;
      return true;
    }
  }

  String tag_name = scan_tag_name(lexer);
  if (tag_name.size == 0 && !lexer->eof(lexer))
  {
    array_delete(&tag_name);
    return false;
  }

  Tag next_tag = tag_for_name(tag_name);

  if (is_closing_tag)
  {
    // The tag correctly closes the topmost element on the stack
    if (scanner->tags.size > 0 && tag_eq(array_back(&scanner->tags), &next_tag))
    {
      tag_free(&next_tag);
      return false;
    }

    // Otherwise, dig deeper and queue implicit end tags (to be nice in
    // the case of malformed HTML)
    for (unsigned i = scanner->tags.size; i > 0; i--)
    {
      if (scanner->tags.contents[i - 1].type == next_tag.type)
      {
        pop_tag(scanner);
        lexer->result_symbol = IMPLICIT_END_TAG;
        tag_free(&next_tag);
        return true;
      }
    }
  }
  else if (
      parent &&
      (!tag_can_contain(parent, &next_tag) ||
       ((parent->type == HTML || parent->type == HEAD || parent->type == BODY) && lexer->eof(lexer))))
  {
    pop_tag(scanner);
    lexer->result_symbol = IMPLICIT_END_TAG;
    tag_free(&next_tag);
    return true;
  }

  tag_free(&next_tag);
  return false;
}

static bool scan_start_tag_name(Scanner *scanner, TSLexer *lexer)
{
  String tag_name = scan_tag_name(lexer);
  if (tag_name.size == 0)
  {
    array_delete(&tag_name);
    return false;
  }

  Tag tag = tag_for_name(tag_name);
  array_push(&scanner->tags, tag);

  switch (tag.type)
  {
  case SCRIPT:
    lexer->result_symbol = SCRIPT_START_TAG_NAME;
    break;
  case STYLE:
    lexer->result_symbol = STYLE_START_TAG_NAME;
    break;
  default:
    lexer->result_symbol = START_TAG_NAME;
    break;
  }
  return true;
}

static bool scan_end_tag_name(Scanner *scanner, TSLexer *lexer)
{
  String tag_name = scan_tag_name(lexer);

  if (tag_name.size == 0)
  {
    array_delete(&tag_name);
    return false;
  }

  Tag tag = tag_for_name(tag_name);
  if (scanner->tags.size > 0 && tag_eq(array_back(&scanner->tags), &tag))
  {
    pop_tag(scanner);
    lexer->result_symbol = END_TAG_NAME;
  }
  else
  {
    lexer->result_symbol = ERRONEOUS_END_TAG_NAME;
  }

  tag_free(&tag);
  return true;
}

static bool scan_self_closing_tag_delimiter(Scanner *scanner, TSLexer *lexer)
{
  advance(lexer);
  if (lexer->lookahead == '>')
  {
    advance(lexer);
    if (scanner->tags.size > 0)
    {
      pop_tag(scanner);
      lexer->result_symbol = SELF_CLOSING_TAG_DELIMITER;
    }
    return true;
  }
  return false;
}

static bool scan(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols)
{
  if (valid_symbols[RAW_TEXT] && !valid_symbols[START_TAG_NAME] &&
      !valid_symbols[END_TAG_NAME])
  {
    return scan_raw_text(scanner, lexer);
  }

  while (iswspace(lexer->lookahead))
  {
    skip(lexer);
  }

  switch (lexer->lookahead)
  {
  case '<':
    lexer->mark_end(lexer);
    advance(lexer);

    if (lexer->lookahead == '!')
    {
      advance(lexer);
      return scan_comment(lexer);
    }

    if (valid_symbols[IMPLICIT_END_TAG])
    {
      return scan_implicit_end_tag(scanner, lexer);
    }
    break;

  case '\0':
    if (valid_symbols[IMPLICIT_END_TAG])
    {
      return scan_implicit_end_tag(scanner, lexer);
    }
    break;

  case '/':
    if (valid_symbols[SELF_CLOSING_TAG_DELIMITER])
    {
      return scan_self_closing_tag_delimiter(scanner, lexer);
    }
    break;

  case '{':
    if (valid_symbols[INTERPOLATION_START])
    {
      lexer->mark_end(lexer);
      advance(lexer);

      if (lexer->lookahead == '{')
      {
        advance(lexer);
        lexer->mark_end(lexer);
        Tag tag = (Tag){INTERPOLATION, {0}};
        array_push(&scanner->tags, tag);
        lexer->result_symbol = INTERPOLATION_START;
        return true;
      }
    }
    break;

  case '}':
    if (valid_symbols[INTERPOLATION_END])
    {
      lexer->mark_end(lexer);
      advance(lexer);

      if (lexer->lookahead == '}' &&
          scanner->tags.size > 0 &&
          array_back(&scanner->tags)->type == INTERPOLATION)
      {
        advance(lexer);
        lexer->mark_end(lexer);
        // VEC_POP(scanner->tags);
        pop_tag(scanner);
        lexer->result_symbol = INTERPOLATION_END;
        return true;
      }
    }

    break;

  case '@':
    if (scanner->seen_at)
    {
      scanner->seen_at = false;
      advance(lexer);
      lexer->result_symbol = AT_SIGN;
      lexer->mark_end(lexer);
      return true;
    }
    else if (valid_symbols[IF_START] || valid_symbols[ELSE_START] || valid_symbols[FOR_START] ||
             valid_symbols[SWITCH_START] || valid_symbols[CASE_START] || valid_symbols[DEFAULT_START] ||
             valid_symbols[DEFER_START] || valid_symbols[LET_START] || valid_symbols[EMPTY_START] ||
             valid_symbols[PLACEHOLDER_START] || valid_symbols[LOADING_START] || valid_symbols[ERROR_START] ||
             valid_symbols[ELSE_IF_START])
    {
      advance(lexer);
      lexer->mark_end(lexer);

      String buffer = array_new();
      bool is_else = false;

      // Collect potential keyword
      while (iswalnum(lexer->lookahead) && !lexer->eof(lexer))
      {
        array_push(&buffer, lexer->lookahead);
        advance(lexer);
        lexer->mark_end(lexer);
      }

      // Check single-word keywords
      for (int i = 0; i < sizeof(CONTROL_FLOW_KEYWORDS) / sizeof(CONTROL_FLOW_KEYWORDS[0]); i++)
      {
        const char *keyword = CONTROL_FLOW_KEYWORDS[i];
        size_t keyword_len = strlen(keyword);
        if (buffer.size == keyword_len && strncmp(keyword, buffer.contents, keyword_len) == 0)
        {
          if (strcmp(keyword, "else") == 0)
          {
            is_else = true; // Special case for "else"
            break;
          }
          if (i != 12)
          { // Not "else if"
            int symbol;
            switch (i)
            {
            case 0:
              symbol = IF_START;
              break;
            case 1:
              symbol = ELSE_START;
              break;
            case 2:
              symbol = FOR_START;
              break;
            case 3:
              symbol = SWITCH_START;
              break;
            case 4:
              symbol = CASE_START;
              break;
            case 5:
              symbol = DEFAULT_START;
              break;
            case 6:
              symbol = DEFER_START;
              break;
            case 7:
              symbol = LET_START;
              break;
            case 8:
              symbol = EMPTY_START;
              break;
            case 9:
              symbol = PLACEHOLDER_START;
              break;
            case 10:
              symbol = LOADING_START;
              break;
            case 11:
              symbol = ERROR_START;
              break;
            default:
              continue;
            }
            if (valid_symbols[symbol])
            {
              lexer->result_symbol = symbol;
              array_delete(&buffer);
              return true;
            }
          }
        }
      }

      if (is_else && valid_symbols[ELSE_START])
      {
        if (lexer->lookahead == ' ')
        {
          advance(lexer); // Consume space
          lexer->mark_end(lexer);
          String if_buffer = array_new();
          while (iswalnum(lexer->lookahead) && !lexer->eof(lexer))
          {
            array_push(&if_buffer, lexer->lookahead);
            advance(lexer);
            lexer->mark_end(lexer);
          }
          if (if_buffer.size == 2 && strncmp("if", if_buffer.contents, 2) == 0)
          {
            array_delete(&if_buffer);
            array_delete(&buffer);
            if (valid_symbols[ELSE_IF_START])
            {
              lexer->result_symbol = ELSE_IF_START;
              return true;
            }
          }
          else
          {
            array_delete(&if_buffer);
            array_delete(&buffer);
            if (valid_symbols[ELSE_START])
            {
              lexer->result_symbol = ELSE_START;
              return true;
            }
          }
        }
        else if (lexer->lookahead == '{' || lexer->lookahead == '(')
        {
          // "else" followed directly by '{' or '('
          array_delete(&buffer);
          if (valid_symbols[ELSE_START])
          {
            lexer->result_symbol = ELSE_START;
            return true;
          }
        }
      }
      else if (is_else && valid_symbols[ELSE_START])
      {
        array_delete(&buffer);
        lexer->result_symbol = ELSE_START;
        return true;
      }

      scanner->seen_at = true;
      array_delete(&buffer);
      return false;
    }
    break;

  default:
    if ((valid_symbols[START_TAG_NAME] || valid_symbols[END_TAG_NAME]) &&
        !valid_symbols[RAW_TEXT])
    {
      return valid_symbols[START_TAG_NAME] ? scan_start_tag_name(scanner, lexer)
                                           : scan_end_tag_name(scanner, lexer);
    }
  }

  return false;
}

void *tree_sitter_angular_external_scanner_create()
{
  Scanner *scanner = (Scanner *)ts_calloc(1, sizeof(Scanner));
  return scanner;
}

bool tree_sitter_angular_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols)
{
  Scanner *scanner = (Scanner *)payload;
  return scan(scanner, lexer, valid_symbols);
}

unsigned tree_sitter_angular_external_scanner_serialize(void *payload, char *buffer)
{
  Scanner *scanner = (Scanner *)payload;
  return serialize(scanner, buffer);
}

void tree_sitter_angular_external_scanner_deserialize(void *payload, const char *buffer, unsigned length)
{
  Scanner *scanner = (Scanner *)payload;
  deserialize(scanner, buffer, length);
}

void tree_sitter_angular_external_scanner_destroy(void *payload)
{
  Scanner *scanner = (Scanner *)payload;
  for (unsigned i = 0; i < scanner->tags.size; i++)
  {
    tag_free(&scanner->tags.contents[i]);
  }
  array_delete(&scanner->tags);
  ts_free(scanner);
}
