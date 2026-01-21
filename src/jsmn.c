#include "jsmn.h"

static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser,
                                  jsmntok_t *tokens,
                                  unsigned int num_tokens) {
  if (parser->toknext >= num_tokens) {
    return NULL;
  }
  jsmntok_t *tok = &tokens[parser->toknext++];
  tok->start = tok->end = -1;
  tok->size = 0;
  tok->type = JSMN_UNDEFINED;
  return tok;
}

static void jsmn_fill_token(jsmntok_t *token,
                            jsmntype_t type,
                            int start,
                            int end) {
  token->type = type;
  token->start = start;
  token->end = end;
  token->size = 0;
}

static int jsmn_parse_primitive(jsmn_parser *parser,
                                const char *js,
                                size_t len,
                                jsmntok_t *tokens,
                                unsigned int num_tokens) {
  int start = (int)parser->pos;
  for (; parser->pos < len; parser->pos++) {
    switch (js[parser->pos]) {
      case '\t':
      case '\r':
      case '\n':
      case ' ':
      case ',':
      case ']':
      case '}': {
        jsmntok_t *tok = jsmn_alloc_token(parser, tokens, num_tokens);
        if (!tok) {
          parser->pos = start;
          return JSMN_ERROR_NOMEM;
        }
        jsmn_fill_token(tok, JSMN_PRIMITIVE, start, (int)parser->pos);
        parser->pos--;
        return 0;
      }
      default:
        break;
    }
  }

  jsmntok_t *tok = jsmn_alloc_token(parser, tokens, num_tokens);
  if (!tok) {
    parser->pos = start;
    return JSMN_ERROR_NOMEM;
  }
  jsmn_fill_token(tok, JSMN_PRIMITIVE, start, (int)parser->pos);
  parser->pos--;
  return 0;
}

static int jsmn_parse_string(jsmn_parser *parser,
                             const char *js,
                             size_t len,
                             jsmntok_t *tokens,
                             unsigned int num_tokens) {
  int start = (int)parser->pos;
  parser->pos++;

  for (; parser->pos < len; parser->pos++) {
    char c = js[parser->pos];
    if (c == '\"') {
      jsmntok_t *tok = jsmn_alloc_token(parser, tokens, num_tokens);
      if (!tok) {
        parser->pos = start;
        return JSMN_ERROR_NOMEM;
      }
      jsmn_fill_token(tok, JSMN_STRING, start + 1, (int)parser->pos);
      return 0;
    }
    if (c == '\\') {
      parser->pos++;
      if (parser->pos >= len) {
        parser->pos = start;
        return JSMN_ERROR_PART;
      }
    }
  }
  parser->pos = start;
  return JSMN_ERROR_PART;
}

void jsmn_init(jsmn_parser *parser) {
  parser->pos = 0;
  parser->toknext = 0;
  parser->toksuper = -1;
}

int jsmn_parse(jsmn_parser *parser,
               const char *js,
               size_t len,
               jsmntok_t *tokens,
               unsigned int num_tokens) {
  int r;
  for (; parser->pos < len; parser->pos++) {
    char c = js[parser->pos];
    switch (c) {
      case '{':
      case '[': {
        jsmntok_t *tok = jsmn_alloc_token(parser, tokens, num_tokens);
        if (!tok) {
          return JSMN_ERROR_NOMEM;
        }
        tok->type = (c == '{' ? JSMN_OBJECT : JSMN_ARRAY);
        tok->start = (int)parser->pos;
        if (parser->toksuper != -1) {
          tokens[parser->toksuper].size++;
        }
        parser->toksuper = (int)(parser->toknext - 1);
        break;
      }
      case '}':
      case ']': {
        jsmntype_t type = (c == '}' ? JSMN_OBJECT : JSMN_ARRAY);
        int i;
        for (i = (int)parser->toknext - 1; i >= 0; i--) {
          if (tokens[i].start != -1 && tokens[i].end == -1) {
            if (tokens[i].type != type) {
              return JSMN_ERROR_INVAL;
            }
            tokens[i].end = (int)parser->pos + 1;
            parser->toksuper = -1;
            for (int j = i - 1; j >= 0; j--) {
              if (tokens[j].start != -1 && tokens[j].end == -1) {
                parser->toksuper = j;
                break;
              }
            }
            break;
          }
        }
        if (i == -1) {
          return JSMN_ERROR_INVAL;
        }
        break;
      }
      case '\"': {
        r = jsmn_parse_string(parser, js, len, tokens, num_tokens);
        if (r < 0) {
          return r;
        }
        if (parser->toksuper != -1) {
          tokens[parser->toksuper].size++;
        }
        break;
      }
      case '\t':
      case '\r':
      case '\n':
      case ' ':
      case ':':
      case ',': {
        break;
      }
      default: {
        r = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
        if (r < 0) {
          return r;
        }
        if (parser->toksuper != -1) {
          tokens[parser->toksuper].size++;
        }
        break;
      }
    }
  }

  for (unsigned int i = parser->toknext; i > 0; i--) {
    if (tokens[i - 1].start != -1 && tokens[i - 1].end == -1) {
      return JSMN_ERROR_PART;
    }
  }

  return (int)parser->toknext;
}
