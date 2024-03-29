#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <float.h>
#include "marpaESLIF/internal/config.h"
#include "marpaESLIF/internal/structures.h"
#include "marpaESLIF/internal/logging.h"
#include "marpaESLIF/internal/bootstrap.h"
#include "marpaESLIF/internal/lua.h"

/* Taken from od.c */
/* The number of decimal digits of precision in a float.  */
#ifndef FLT_DIG
# define FLT_DIG 7
#endif

/* The number of decimal digits of precision in a double.  */
#ifndef DBL_DIG
# define DBL_DIG 15
#endif

#ifndef LDBL_DIG
  /* Compiler will optimize that */
#define LDBL_DIG ((sizeof(long double) == 10) ? 18 : (sizeof(long double) == 12) ? 18 : (sizeof(long double) == 16) ? 33 : DBL_DIG)
#endif

static const char *MARPAESLIF_VERSION_STATIC       = MARPAESLIF_VERSION;
static const int   MARPAESLIF_VERSION_MAJOR_STATIC = MARPAESLIF_VERSION_MAJOR;
static const int   MARPAESLIF_VERSION_MINOR_STATIC = MARPAESLIF_VERSION_MINOR;
static const int   MARPAESLIF_VERSION_PATCH_STATIC = MARPAESLIF_VERSION_PATCH;

#define MARPAESLIF_ENCODING_IS_UTF8(encodings, encodingl)               \
  (                                                                     \
    /* UTF-8 */                                                         \
    (                                                                   \
      (encodingl == 5)                                 &&               \
      ((encodings[0] == 'U') || (encodings[0] == 'u')) &&               \
      ((encodings[1] == 'T') || (encodings[1] == 't')) &&               \
      ((encodings[2] == 'F') || (encodings[2] == 'f')) &&               \
       (encodings[3] == '-') &&                                         \
       (encodings[4] == '8')                                            \
    )                                                                   \
    ||                                                                  \
    /* UTF8 */                                                          \
    (                                                                   \
      (encodingl == 4)                                 &&               \
      ((encodings[0] == 'U') || (encodings[0] == 'u')) &&               \
      ((encodings[1] == 'T') || (encodings[1] == 't')) &&               \
      ((encodings[2] == 'F') || (encodings[2] == 'f')) &&               \
       (encodings[3] == '8')                                            \
    )                                                                   \
  )

#define MARPAESLIF_ENCODING_IS_UTF16(encodings, encodingl)              \
  (                                                                     \
    /* UTF-16 */                                                        \
    (                                                                   \
      (encodingl == 6)                                 &&               \
      ((encodings[0] == 'U') || (encodings[0] == 'u')) &&               \
      ((encodings[1] == 'T') || (encodings[1] == 't')) &&               \
      ((encodings[2] == 'F') || (encodings[2] == 'f')) &&               \
       (encodings[3] == '-')                           &&               \
       (encodings[4] == '1')                           &&               \
       (encodings[5] == '6')                                            \
    )                                                                   \
    ||                                                                  \
    /* UTF16 */                                                         \
    (                                                                   \
      (encodingl == 5)                                 &&               \
      ((encodings[0] == 'U') || (encodings[0] == 'u')) &&               \
      ((encodings[1] == 'T') || (encodings[1] == 't')) &&               \
      ((encodings[2] == 'F') || (encodings[2] == 'f')) &&               \
       (encodings[3] == '1')                           &&               \
       (encodings[4] == '6')                                            \
    )                                                                   \
  )

#define MARPAESLIF_ENCODING_IS_UTF32(encodings, encodingl)              \
  (                                                                     \
    /* UTF-32 */                                                        \
    (                                                                   \
      (encodingl == 6)                                 &&               \
      ((encodings[0] == 'U') || (encodings[0] == 'u')) &&               \
      ((encodings[1] == 'T') || (encodings[1] == 't')) &&               \
      ((encodings[2] == 'F') || (encodings[2] == 'f')) &&               \
       (encodings[3] == '-')                           &&               \
       (encodings[4] == '3')                           &&               \
       (encodings[5] == '2')                                            \
    )                                                                   \
    ||                                                                  \
    /* UTF32 */                                                         \
    (                                                                   \
      (encodingl == 5)                                 &&               \
      ((encodings[0] == 'U') || (encodings[0] == 'u')) &&               \
      ((encodings[1] == 'T') || (encodings[1] == 't')) &&               \
      ((encodings[2] == 'F') || (encodings[2] == 'f')) &&               \
       (encodings[3] == '3')                           &&               \
       (encodings[4] == '2')                                            \
    )                                                                   \
  )

#ifndef MARPAESLIF_VALUEERRORPROGRESSREPORT
#define MARPAESLIF_VALUEERRORPROGRESSREPORT 0 /* Left in the code, although not needed IMHO */
#endif

#ifndef MARPAESLIF_HASH_SIZE
#define MARPAESLIF_HASH_SIZE 16 /* Subjective number - raising too high leads to unnecessary CPU when doing relax */
#endif

/* Internal marpaESLIFValueResult used to do lazy row transformation: we use an INVALID type */
/* that only marpaESLIF can set because its contextp is NULL, and fill its u.p.p with the */
/* marpaESLIFValueResultp that got lazy */
#define MARPAESLIF_VALUE_TYPE_LAZY -1
static marpaESLIFValueResult_t marpaESLIFValueResultLazy = {
  NULL,                      /* contextp */
  NULL,                      /* representationp */
  MARPAESLIF_VALUE_TYPE_LAZY /* type */
};

/* -------------------------------------------------------------------------------------------- */
/* Check if a marpaESLIFValueResult is a well-formed lexeme                                     */
/* -------------------------------------------------------------------------------------------- */
#define MARPAESLIF_VALUECHECK_IF_LEXEME_MODE(marpaESLIFp, marpaESLIFValueResult) do { \
    if (marpaESLIFValueResult.type != MARPAESLIF_VALUE_TYPE_ARRAY) {    \
      MARPAESLIF_ERRORF(marpaESLIFp, "marpaESLIFValueResult.type is %d instead of %d (MARPAESLIF_VALUE_TYPE_ARRAY)", marpaESLIFValueResult.type, MARPAESLIF_VALUE_TYPE_ARRAY); \
      goto err;                                                         \
    }                                                                   \
    if ((marpaESLIFValueResult.u.a.p == NULL) || (marpaESLIFValueResult.u.a.sizel <= 0)) { \
      MARPAESLIF_ERRORF(marpaESLIFp, "marpaESLIFValueResult array is {%p,%ld}", marpaESLIFValueResult.u.a.p, (unsigned long) marpaESLIFValueResult.u.a.sizel); \
      goto err;                                                         \
    }                                                                   \
  } while (0)

/* -------------------------------------------------------------------------------------------- */
/* Exhaustion event name is hardcoded                                                           */
/* -------------------------------------------------------------------------------------------- */
#define MARPAESLIF_EVENTTYPE_EXHAUSTED_NAME "'exhausted'"

/* -------------------------------------------------------------------------------------------- */
/* Util macros on symbol                                                                        */
/* -------------------------------------------------------------------------------------------- */
#define MARPAESLIF_IS_LEXEME(symbolp)  (((symbolp)->type == MARPAESLIF_SYMBOL_TYPE_META) && (! (symbolp)->lhsb))
#define MARPAESLIF_IS_TERMINAL(symbolp)  ((symbolp)->type == MARPAESLIF_SYMBOL_TYPE_TERMINAL)
#define MARPAESLIF_IS_DISCARD(symbolp) (symbolp)->discardb

/* -------------------------------------------------------------------------------------------- */
/* In theory, when rci is MARPAESLIF_MATCH_OK, marpaESLIFValueResult.type must be a valid ARRAY */
/* -------------------------------------------------------------------------------------------- */
#define _MARPAESLIF_CHECK_MATCH_RESULT(funcs, marpaESLIFRecognizerp, symbolp, rci, marpaESLIFValueResult) do { \
    if (rci == MARPAESLIF_MATCH_OK) {                                   \
      if (marpaESLIFValueResult.type != MARPAESLIF_VALUE_TYPE_ARRAY) {  \
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Match result type for %s is %d instead of ARRAY (%d)", symbolp->descp->asciis, marpaESLIFValueResult.type, MARPAESLIF_VALUE_TYPE_ARRAY); \
        goto err;                                                       \
      }                                                                 \
      if ((marpaESLIFValueResult.u.a.p == NULL) || (marpaESLIFValueResult.u.a.sizel <= 0)) { \
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Match result for %s is {%p,%ld}", symbolp->descp->asciis, marpaESLIFValueResult.u.a.p, (unsigned long) marpaESLIFValueResult.u.a.sizel); \
        goto err;                                                       \
      }                                                                 \
      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Match result for %s is ARRAY: {%p,%ld}", symbolp->descp->asciis, marpaESLIFValueResult.u.a.p, (unsigned long) marpaESLIFValueResult.u.a.sizel); \
    }                                                                   \
  } while (0)

#ifndef MARPAESLIF_NTRACE
#define MARPAESLIF_CHECK_MATCH_RESULT(funcs, marpaESLIFRecognizerp, inputs, symbolp, rci, marpaESLIFValueResult) do { \
    _MARPAESLIF_CHECK_MATCH_RESULT(funcs, marpaESLIFRecognizerp, symbolp, rci, marpaESLIFValueResult); \
    if (rci == MARPAESLIF_MATCH_OK) {                                   \
      MARPAESLIF_HEXDUMPV(marpaESLIFRecognizerp, "Match dump for ", symbolp->descp->asciis, marpaESLIFValueResult.u.a.p, marpaESLIFValueResult.u.a.sizel, 1); \
    }                                                                   \
  } while (0)
#else
#define MARPAESLIF_CHECK_MATCH_RESULT(funcs, marpaESLIFRecognizerp, inputs, symbolp, rci, marpaESLIFValueResult)
#endif

/* -------------------------------------------------------------------------------------------- */
/* Reset recognizer events                                                                      */
/* -------------------------------------------------------------------------------------------- */
#define MARPAESLIFRECOGNIZER_RESET_EVENTS(marpaESLIFRecognizerp) (marpaESLIFRecognizerp)->eventArrayl = 0

/* -------------------------------------------------------------------------------------------- */
/* This macro makes sure we return a multiple of chunk of always at least 1 BYTE more than size */
/* -------------------------------------------------------------------------------------------- */
#define MARPAESLIF_CHUNKED_SIZE_UPPER(size, chunk) ((size) < (chunk)) ? (chunk) : ((1 + ((size) / (chunk))) * (chunk))

/* -------------------------------------------------------------------------------------------- */
/* Get a symbol from stack - with an extra check when not in production mode                    */
/* -------------------------------------------------------------------------------------------- */
#ifndef MARPAESLIF_NTRACE
#define MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli) do { \
    if ((symboli < 0) || (! GENERICSTACK_IS_PTR(symbolStackp, symboli))) { \
      MARPAESLIF_ERRORF(marpaESLIFp, "Symbol no %d is unknown from symbolStackp", symboli); \
      errno = EINVAL;                                                   \
      goto err;                                                         \
    }                                                                   \
    symbolp = (marpaESLIF_symbol_t *) GENERICSTACK_GET_PTR(symbolStackp, symboli); \
  } while (0)
#else
#define MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli) \
  symbolp = (marpaESLIF_symbol_t *) GENERICSTACK_GET_PTR(symbolStackp, symboli)
#endif

/* -------------------------------------------------------------------------------------------- */
/* Get a rule from stack - with an extra check when not in production mode                      */
/* -------------------------------------------------------------------------------------------- */
#ifndef MARPAESLIF_NTRACE
#define MARPAESLIF_INTERNAL_GET_RULE_FROM_STACK(marpaESLIFp, rulep, ruleStackp, rulei) do { \
    if ((rulei < 0) || (! GENERICSTACK_IS_PTR(ruleStackp, rulei))) {    \
      MARPAESLIF_ERRORF(marpaESLIFp, "Rule no %d is unknown from ruleStackp", rulei); \
      errno = EINVAL;                                                   \
      goto err;                                                         \
    }                                                                   \
    rulep = (marpaESLIF_rule_t *) GENERICSTACK_GET_PTR(ruleStackp, rulei); \
  } while (0)
#else
#define MARPAESLIF_INTERNAL_GET_RULE_FROM_STACK(marpaESLIFp, rulep, ruleStackp, rulei) \
  rulep = (marpaESLIF_rule_t *) GENERICSTACK_GET_PTR(ruleStackp, rulei)
#endif

/* -------------------------------------------------------------------------------------------- */
/* Make a marpaESLIFValueResult shallow                                                         */
/* -------------------------------------------------------------------------------------------- */
#define MARPAESLIF_MAKE_MARPAESLIFVALUERESULT_SHALLOW(marpaESLIFValueResult) do { \
    switch (marpaESLIFValueResult.type) {                               \
    case MARPAESLIF_VALUE_TYPE_PTR:                                     \
      (marpaESLIFValueResult).u.p.shallowb       = 1;			\
      (marpaESLIFValueResult).u.p.freeUserDatavp = NULL;                \
      (marpaESLIFValueResult).u.p.freeCallbackp  = NULL;                \
      break;                                                            \
    case MARPAESLIF_VALUE_TYPE_ARRAY:                                   \
      (marpaESLIFValueResult).u.a.shallowb       = 1;			\
      (marpaESLIFValueResult).u.a.freeUserDatavp = NULL;                \
      (marpaESLIFValueResult).u.a.freeCallbackp  = NULL;                \
      break;                                                            \
    case MARPAESLIF_VALUE_TYPE_STRING:                                  \
      (marpaESLIFValueResult).u.s.shallowb       = 1;			\
      (marpaESLIFValueResult).u.s.freeUserDatavp = NULL;                \
      (marpaESLIFValueResult).u.s.freeCallbackp  = NULL;                \
      break;                                                            \
    case MARPAESLIF_VALUE_TYPE_ROW:                                     \
      (marpaESLIFValueResult).u.r.shallowb       = 1;			\
      (marpaESLIFValueResult).u.r.freeUserDatavp = NULL;                \
      (marpaESLIFValueResult).u.r.freeCallbackp  = NULL;                \
      break;                                                            \
    case MARPAESLIF_VALUE_TYPE_TABLE:                                   \
      (marpaESLIFValueResult).u.t.shallowb       = 1;			\
      (marpaESLIFValueResult).u.t.freeUserDatavp = NULL;                \
      (marpaESLIFValueResult).u.t.freeCallbackp  = NULL;                \
      break;                                                            \
    default:                                                            \
      break;                                                            \
    }                                                                   \
  } while (0)

/* -------------------------------------------------------------------------------------------- */
/* For logging                                                                                  */
/* -------------------------------------------------------------------------------------------- */
#undef  FILENAMES
#define FILENAMES "marpaESLIF.c"

typedef struct marpaESLIF_stringGenerator {
  marpaESLIF_t *marpaESLIFp;
  char         *s;      /* Pointer */
  size_t        l;      /* Used size */
  short         okb;    /* Status */
  size_t        allocl; /* Allocated size */
} marpaESLIF_stringGenerator_t;

typedef short (*_marpaESLIFRecognizer_valueResultCallback_t)(void *userDatavp, marpaESLIFValueResult_t *marpaESLIFValueResultp);
typedef struct marpaESLIF_concat_valueResultContext {
  void                         *userDatavp;
  marpaESLIFValue_t            *marpaESLIFValuep;
  marpaESLIF_stringGenerator_t *marpaESLIF_stringGeneratorp;
  short                         stringb;
  short                         jsonb;
} marpaESLIF_concat_valueResultContext_t;

static const char *GENERICSTACKITEMTYPE_NA_STRING           = "NA";
static const char *GENERICSTACKITEMTYPE_CHAR_STRING         = "CHAR";
static const char *GENERICSTACKITEMTYPE_SHORT_STRING        = "SHORT";
static const char *GENERICSTACKITEMTYPE_INT_STRING          = "INT";
static const char *GENERICSTACKITEMTYPE_LONG_STRING         = "LONG";
static const char *GENERICSTACKITEMTYPE_FLOAT_STRING        = "FLOAT";
static const char *GENERICSTACKITEMTYPE_DOUBLE_STRING       = "DOUBLE";
static const char *GENERICSTACKITEMTYPE_PTR_STRING          = "PTR";
static const char *GENERICSTACKITEMTYPE_ARRAY_STRING        = "ARRAY";
static const char *GENERICSTACKITEMTYPE_CUSTOM_STRING       = "CUSTOM";
static const char *GENERICSTACKITEMTYPE_LONG_DOUBLE_STRING  = "LONG_DOUBLE";
static const char *GENERICSTACKITEMTYPE_UNKNOWN_STRING      = "UNKNOWN";

static const char *MARPAESLIF_VALUE_TYPE_UNDEF_STRING       = "UNDEF";
static const char *MARPAESLIF_VALUE_TYPE_CHAR_STRING        = "CHAR";
static const char *MARPAESLIF_VALUE_TYPE_SHORT_STRING       = "SHORT";
static const char *MARPAESLIF_VALUE_TYPE_INT_STRING         = "INT";
static const char *MARPAESLIF_VALUE_TYPE_LONG_STRING        = "LONG";
static const char *MARPAESLIF_VALUE_TYPE_FLOAT_STRING       = "FLOAT";
static const char *MARPAESLIF_VALUE_TYPE_DOUBLE_STRING      = "DOUBLE";
static const char *MARPAESLIF_VALUE_TYPE_PTR_STRING         = "PTR";
static const char *MARPAESLIF_VALUE_TYPE_ARRAY_STRING       = "ARRAY";
static const char *MARPAESLIF_VALUE_TYPE_BOOL_STRING        = "BOOL";
static const char *MARPAESLIF_VALUE_TYPE_STRING_STRING      = "STRING";
static const char *MARPAESLIF_VALUE_TYPE_ROW_STRING         = "ROW";
static const char *MARPAESLIF_VALUE_TYPE_TABLE_STRING       = "TABLE";
static const char *MARPAESLIF_VALUE_TYPE_LONG_DOUBLE_STRING = "LONG_DOUBLE";
#ifdef MARPAESLIF_HAVE_LONG_LONG
static const char *MARPAESLIF_VALUE_TYPE_LONG_LONG_STRING   = "LONG_LONG";
#endif
static const char *MARPAESLIF_VALUE_TYPE_UNKNOWN_STRING     = "UNKNOWN";

static const size_t copyl    = 6; /* strlen("::copy"); */
static const size_t convertl = 9; /* strlen("::convert"); */

static const char *MARPAESLIF_TRANSFER_INTERNAL_STRING = "::transfer (internal)";
static const char *MARPAESLIF_CONCAT_INTERNAL_STRING = "::concat (internal)";

const marpaESLIF_uint32_t pcre2_option_binary_default  = PCRE2_NOTEMPTY;
const marpaESLIF_uint32_t pcre2_option_char_default    = PCRE2_NOTEMPTY|PCRE2_NO_UTF_CHECK;
const marpaESLIF_uint32_t pcre2_option_partial_default = PCRE2_NOTEMPTY|PCRE2_NO_UTF_CHECK|PCRE2_PARTIAL_HARD;

/* For reset of values in the stack, it is okay to not care about the union -; */
static const marpaESLIFValueResult_t marpaESLIFValueResultUndef = {
  NULL,                       /* contextp */
  NULL,                       /* representationp */
  MARPAESLIF_VALUE_TYPE_UNDEF /* type */
  /* Here is the union */
};

/* In C89 only the first member of a union can be initialized, this is what we need */
static const marpaESLIFValueResult_t marpaESLIFValueResultLeftBracket = {
  NULL,                       /* contextp */
  NULL,                       /* representationp */
  MARPAESLIF_VALUE_TYPE_CHAR, /* type */
  '{'                         /* u.c */
};
static const marpaESLIFValueResult_t marpaESLIFValueResultRightBracket = {
  NULL,                       /* contextp */
  NULL,                       /* representationp */
  MARPAESLIF_VALUE_TYPE_CHAR, /* type */
  '}'                         /* u.c */
};
static const marpaESLIFValueResult_t marpaESLIFValueResultLeftSquare = {
  NULL,                       /* contextp */
  NULL,                       /* representationp */
  MARPAESLIF_VALUE_TYPE_CHAR, /* type */
  '['                         /* u.c */
};

/* In C89 only the first member of a union can be initialized, this is what we need */
static const marpaESLIFValueResult_t marpaESLIFValueResultRightSquare = {
  NULL,                       /* contextp */
  NULL,                       /* representationp */
  MARPAESLIF_VALUE_TYPE_CHAR, /* type */
  ']'                         /* u.c */
};
/* In C89 only the first member of a union can be initialized, this is what we need */
static const marpaESLIFValueResult_t marpaESLIFValueResultComma = {
  NULL,                       /* contextp */
  NULL,                       /* representationp */
  MARPAESLIF_VALUE_TYPE_CHAR, /* type */
  ','                         /* u.c */
};
/* In C89 only the first member of a union can be initialized, this is what we need */
static const marpaESLIFValueResult_t marpaESLIFValueResultColon = {
  NULL,                       /* contextp */
  NULL,                       /* representationp */
  MARPAESLIF_VALUE_TYPE_CHAR, /* type */
  ':'                         /* u.c */
};

/* Prefilled string generator, to gain also few instructions that are always the same */
static const marpaESLIF_stringGenerator_t  marpaESLIF_stringGeneratorTemplate = {
  NULL, /* marpaESLIFp */
  NULL, /* s */
  0,    /* l */
  0,    /* okb */
  0     /* allocl */
};

static const char *MARPAESLIF_EMPTY_STRING = "";
static const char *MARPAESLIF_UTF8_STRING = "UTF-8";
static const char *MARPAESLIF_UNKNOWN_STRING = "???";

/* Please note that EVERY _marpaESLIFRecognizer_xxx() method is logging at start and at return */

static inline marpaESLIF_t          *_marpaESLIF_newp(marpaESLIFOption_t *marpaESLIFOptionp, short validateb);
static inline marpaESLIF_string_t   *_marpaESLIF_string_newp(marpaESLIF_t *marpaESLIFp, char *encodingasciis, char *bytep, size_t bytel);
static inline marpaESLIF_string_t   *_marpaESLIF_string_clonep(marpaESLIF_t *marpaESLIFp, marpaESLIF_string_t *stringp);
static inline void                   _marpaESLIF_string_freev(marpaESLIF_string_t *stringp, short onStstackb);
static inline short                  _marpaESLIF_string_utf8_eqb(marpaESLIF_string_t *string1p, marpaESLIF_string_t *string2p);
static inline short                  _marpaESLIF_string_eqb(marpaESLIF_string_t *string1p, marpaESLIF_string_t *string2p);
static inline marpaESLIF_string_t   *_marpaESLIF_string2utf8p(marpaESLIF_t *marpaESLIFp, marpaESLIF_string_t *stringp, short tconvsilentb);
static inline marpaESLIF_terminal_t *_marpaESLIF_terminal_newp(marpaESLIF_t *marpaESLIFp, marpaESLIF_grammar_t *grammarp, int eventSeti, char *descEncodings, char *descs, size_t descl, marpaESLIF_terminal_type_t type, char *modifiers, char *utf8s, size_t utf8l, char *testFullMatchs, char *testPartialMatchs);
static inline void                   _marpaESLIF_terminal_freev(marpaESLIF_terminal_t *terminalp);

static inline marpaESLIF_meta_t     *_marpaESLIF_meta_newp(marpaESLIF_t *marpaESLIFp, marpaESLIF_grammar_t *grammarp, int eventSeti, char *asciinames, char *descEncodings, char *descs, size_t descl);
static inline void                   _marpaESLIF_meta_freev(marpaESLIF_meta_t *metap);

static inline marpaESLIF_grammar_t  *_marpaESLIF_grammar_newp(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaWrapperGrammarOption_t *marpaWrapperGrammarOptionp, int leveli, char *descEncodings, char *descs, size_t descl, marpaESLIF_action_t *defaultSymbolActionp, marpaESLIF_action_t *defaultRuleActionp);
static inline void                   _marpaESLIF_grammar_freev(marpaESLIF_grammar_t *grammarp);

static inline void                   _marpaESLIF_ruleStack_freev(genericStack_t *ruleStackp);
static inline void                   _marpaESLIFrecognizer_lexemeStack_freev(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, genericStack_t *lexemeStackp);
static inline void                   _marpaESLIFrecognizer_lexemeStack_resetv(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, genericStack_t *lexemeStackp);

static inline short                  _marpaESLIFRecognizer_lexemeStack_i_p_and_sizeb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, genericStack_t *lexemeStackp, int i, char **pp, size_t *sizelp);
static inline short                  _marpaESLIFRecognizer_lexemeStack_i_setb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, genericStack_t *lexemeStackp, int i, marpaESLIFValueResult_t *marpaESLIFValueResultp);
static inline marpaESLIFValueResult_t *_marpaESLIFRecognizer_lexemeStack_i_getp(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, genericStack_t *lexemeStackp, int i);
static inline const char            *_marpaESLIF_genericStack_i_types(genericStack_t *stackp, int i);
static inline const char            *_marpaESLIF_value_types(int typei);

static inline marpaESLIF_rule_t     *_marpaESLIF_rule_newp(marpaESLIF_t *marpaESLIFp, marpaESLIF_grammar_t *grammarp, char *descEncodings, char *descs, size_t descl, int lhsi, size_t nrhsl, int *rhsip, int exceptioni, int ranki, short nullRanksHighb, short sequenceb, int minimumi, int separatori, short properb, marpaESLIF_action_t *actionp, short passthroughb, short hideseparatorb, short *skipbp);
static inline void                   _marpaESLIF_rule_freev(marpaESLIF_rule_t *rulep);

static inline marpaESLIF_symbol_t   *_marpaESLIF_symbol_newp(marpaESLIF_t *marpaESLIFp);
static inline void                   _marpaESLIF_symbol_freev(marpaESLIF_symbol_t *symbolp);

static inline void                   _marpaESLIF_symbolStack_freev(genericStack_t *symbolStackp);

static inline marpaESLIF_grammar_t  *_marpaESLIF_bootstrap_grammar_L0p(marpaESLIFGrammar_t *marpaESLIFGrammarp);
static inline marpaESLIF_grammar_t  *_marpaESLIF_bootstrap_grammar_G1p(marpaESLIFGrammar_t *marpaESLIFGrammarp);
static inline marpaESLIF_grammar_t  *_marpaESLIF_bootstrap_grammarp(marpaESLIFGrammar_t *marpaESLIFGrammarp,
                                                                    int leveli,
                                                                    char *descEndocings,
                                                                    char  *descs,
                                                                    size_t descl,
                                                                    short warningIsErrorb,
                                                                    short warningIsIgnoredb,
                                                                    short autorankb,
                                                                    int bootstrap_grammar_terminali, bootstrap_grammar_terminal_t *bootstrap_grammar_terminalp,
                                                                    int bootstrap_grammar_metai, bootstrap_grammar_meta_t *bootstrap_grammar_metap,
                                                                    int bootstrap_grammar_rulei, bootstrap_grammar_rule_t *bootstrap_grammar_rulep,
                                                                    marpaESLIF_action_t *defaultSymbolActionp,
                                                                    marpaESLIF_action_t *defaultRuleActionp);
static inline short                  _marpaESLIFGrammar_validateb(marpaESLIFGrammar_t *marpaESLIFGrammarp);
static inline short                  _marpaESLIFGrammar_haveLexemeb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int grammari, marpaWrapperGrammar_t *marpaWrapperGrammarp, short *haveLexemebp);
static inline marpaESLIFGrammar_t   *_marpaESLIFGrammar_newp(marpaESLIF_t *marpaESLIFp, marpaESLIFGrammarOption_t *marpaESLIFGrammarOptionp, marpaESLIFGrammar_t *marpaESLIfGrammarPreviousp, short unsafeb);

static inline short                  _marpaESLIFRecognizer_terminal_matcherb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_terminal_t *terminalp, char *inputs, size_t inputl, short eofb, marpaESLIF_matcher_value_t *rcip, marpaESLIFValueResult_t *marpaESLIFValueResultp, size_t *matchedLengthlp);
static inline short                  _marpaESLIFRecognizer_meta_matcherb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_symbol_t *symbolp, marpaESLIF_matcher_value_t *rcip, marpaESLIFValueResult_t *marpaESLIFValueResultp, short *isExhaustedbp, int maxStartCompletionsi, size_t *lastSizeBeforeCompletionlp, int *numberOfStartCompletionsip);
static inline short                  _marpaESLIFRecognizer_symbol_matcherb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_symbol_t *symbolp, marpaESLIF_matcher_value_t *rcip, marpaESLIFValueResult_t *marpaESLIFValueResultp, int maxStartCompletionsi, size_t *lastSizeBeforeCompletionlp, int *numberOfStartCompletionsip);

static const  char                  *_marpaESLIF_utf82printableascii_defaultp = "<!NOT TRANSLATED!>";
#ifndef MARPAESLIF_NTRACE
static        void                   _marpaESLIF_tconvTraceCallback(void *userDatavp, const char *msgs);
#endif

static inline char                  *_marpaESLIF_charconvb(marpaESLIF_t *marpaESLIFp, char *toEncodings, char *fromEncodings, char *srcs, size_t srcl, size_t *dstlp, char **fromEncodingsp, tconv_t *tconvpp, short eofb, char **byteleftsp, size_t *byteleftlp, size_t *byteleftalloclp, short tconvsilentb);

static inline char                  *_marpaESLIF_utf82printableascii_newp(marpaESLIF_t *marpaESLIFp, char *descs, size_t descl);
static inline void                   _marpaESLIF_utf82printableascii_freev(char *utf82printableasciip);
static        short                  _marpaESLIFReader_grammarReader(void *userDatavp, char **inputsp, size_t *inputlp, short *eofbp, short *characterStreambp, char **encodingsp, size_t *encodinglp);
static inline short                  _marpaESLIFRecognizer_resume_oneb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short initialEventsb, short *canContinuebp, short *isExhaustedbp);
static inline short                  _marpaESLIF_recognizer_start_is_completeb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short *completebp);
static inline short                  _marpaESLIFRecognizer_resumeb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, size_t deltaLengthl, short initialEventsb, short *continuebp, short *isExhaustedbp);
static inline marpaESLIF_grammar_t  *_marpaESLIFGrammar_grammar_findp(marpaESLIFGrammar_t *marpaESLIFGrammarp, int leveli, marpaESLIF_string_t *descp);
static inline marpaESLIF_rule_t     *_marpaESLIF_rule_findp(marpaESLIF_t *marpaESLIFp, marpaESLIF_grammar_t *grammarp, int rulei);
static inline marpaESLIF_symbol_t   *_marpaESLIF_symbol_findp(marpaESLIF_t *marpaESLIFp, marpaESLIF_grammar_t *grammarp, char *asciis, int symboli, int *symbolip);
static inline short                  _marpaESLIFRecognizer_lexeme_alternativeb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_alternative_t *alternativep);
static inline short                  _marpaESLIFRecognizer_lexeme_completeb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, size_t lengthl);
static inline short                  _marpaESLIFRecognizer_lexeme_tryb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_grammar_t *grammarp, marpaESLIF_symbol_t *symbolp, short *matchbp);
static inline short                  _marpaESLIFRecognizer_discard_tryb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_grammar_t *grammarp, marpaESLIF_symbol_t *symbolp, short *matchbp);

static inline void                   _marpaESLIFRecognizer_alternativeStackSymbol_freev(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, genericStack_t *alternativeStackSymbolp);
static inline short                  _marpaESLIFRecognizer_alternativeStackSymbol_setb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, genericStack_t *alternativeStackSymbolp, marpaESLIF_alternative_t *alternativep, int indicei);
static inline short                  _marpaESLIFRecognizer_alternative_and_valueb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_alternative_t *alternativep, int valuei);
static inline short                  _marpaESLIFRecognizer_push_eventb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIFEventType_t type, marpaESLIFSymbol_t *symbolp, char *events, marpaESLIFValueResultArray_t *discardArrayp);
static inline short                  _marpaESLIFRecognizer_last_lexemeDatab(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char *lexemes, char **bytesp, size_t *bytelp, marpaESLIF_lexeme_data_t **lexemeDatapp, short forPauseb);
static inline short                  _marpaESLIFRecognizer_discard_lastb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char **bytesp, size_t *bytelp);
static inline short                  _marpaESLIFRecognizer_set_lexemeDatab(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_grammar_t *grammarp, marpaESLIF_symbol_t *symbolp, char *xsbytes, size_t bytel, marpaESLIF_lexeme_data_t **lexemeDatapp);
static inline short                  _marpaESLIFRecognizer_set_pauseb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_grammar_t *grammarp, marpaESLIF_symbol_t *symbolp, char *bytes, size_t bytel);
static inline short                  _marpaESLIFRecognizer_set_tryb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_grammar_t *grammarp, marpaESLIF_symbol_t *symbolp, char *bytes, size_t bytel);
static inline short                  _marpaESLIFRecognizer_push_grammar_eventsb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp);
static inline void                   _marpaESLIFRecognizer_clear_grammar_eventsb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp);
static        short                  _marpaESLIFRecognizer_value_validb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIFValueResult_t *marpaESLIFValueResultp, void *userDatavp, _marpaESLIFRecognizer_valueResultCallback_t callbackp);
static        short                  _marpaESLIFRecognizer_concat_valueResultCallbackb(void *userDatavp, marpaESLIFValueResult_t *marpaESLIFValueResultp);

static inline void                   _marpaESLIFRecognizer_sort_eventsb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp);
static inline short                  _marpaESLIF_stream_initb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, size_t bufsizl, int buftriggerperci, short eofb, short utfb);
static inline void                   _marpaESLIF_stream_disposev(marpaESLIFRecognizer_t *marpaESLIFRecognizerp);
static inline marpaESLIFRecognizer_t *_marpaESLIFRecognizer_newp(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaESLIFRecognizerOption_t *marpaESLIFRecognizerOptionp, short discardb, short noEventb, short silentb, marpaESLIFRecognizer_t *marpaESLIFRecognizerParentp, short fakeb, int maxStartCompletionsi, short utfb, short grammmarIsOnStackb);
static inline short                  _marpaESLIFRecognizer_shareb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIFRecognizer_t *marpaESLIFRecognizerSharedp);
static inline short                  _marpaESLIFGrammar_parseb(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaESLIFRecognizerOption_t *marpaESLIFRecognizerOptionp, marpaESLIFValueOption_t *marpaESLIFValueOptionp, short discardb, short noEventb, short silentb, marpaESLIFRecognizer_t *marpaESLIFRecognizerParentp, short *isExhaustedbp, marpaESLIFValueResult_t *marpaESLIFValueResultp, int maxStartCompletionsi, size_t *lastSizeBeforeCompletionlp, int *numberOfStartCompletionsip, short grammarIsOnStackb);
static        void                   _marpaESLIF_generateStringWithLoggerCallback(void *userDatavp, genericLoggerLevel_t logLeveli, const char *msgs);
static        void                   _marpaESLIF_generateSeparatedStringWithLoggerCallback(void *userDatavp, genericLoggerLevel_t logLeveli, const char *msgs);
static        void                   _marpaESLIF_traceLoggerCallback(void *userDatavp, genericLoggerLevel_t logLeveli, const char *msgs);
static inline short                  _marpaESLIF_appendOpaqueDataToStringGenerator(marpaESLIF_stringGenerator_t *marpaESLIF_stringGeneratorp, char *p, size_t sizel);
static inline short                  _marpaESLIFRecognizer_readb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp);
static inline short                  _marpaESLIFRecognizer_flush_charconvb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp);
static inline short                  _marpaESLIFRecognizer_start_charconvb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char *encodings, size_t encodingl, char *srcs, size_t srcl, short eofb);

/* All wrappers, even the Lexeme and Grammar wrappers go through these routines */
static        short                  _marpaESLIFValue_ruleCallbackWrapperb(void *userDatavp, int rulei, int arg0i, int argni, int resulti);
static inline short                  _marpaESLIFValue_ruleActionCallbackb(marpaESLIFValue_t *marpaESLIFValuep, char *asciishows, marpaESLIF_action_t *actionp, marpaESLIFValueRuleCallback_t *ruleCallbackpp);
static        short                  _marpaESLIFValue_symbolCallbackWrapperb(void *userDatavp, int symboli, int argi, int resulti);
static        short                  _marpaESLIFValue_nullingCallbackWrapperb(void *userDatavp, int symboli, int resulti);
static inline short                  _marpaESLIFValue_anySymbolCallbackWrapperb(void *userDatavp, int symboli, int argi, int resulti, short nullableb);
static inline short                  _marpaESLIFValue_symbolActionCallbackb(marpaESLIFValue_t *marpaESLIFValuep, char *asciishows, short nullableb, marpaESLIF_action_t *nullableActionp, marpaESLIFValueSymbolCallback_t *symbolCallbackpp, marpaESLIFValueRuleCallback_t *ruleCallbackpp, marpaESLIF_action_t *symbolActionp);
static inline short                  _marpaESLIFRecognizer_recognizerIfActionCallbackb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char *asciishows, marpaESLIF_action_t *ifActionp, marpaESLIFRecognizerIfCallback_t *ifCallbackpp);
static inline short                  _marpaESLIFValue_eslif2hostb(marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, marpaESLIFValueResult_t *marpaESLIFValueResultResolvedp, void *forcedUserDatavp, marpaESLIFValueResultImport_t forcedImporterp);

static inline short                  _marpaESLIFValue_valueb(marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp);

static inline void                   _marpaESLIFGrammar_freev(marpaESLIFGrammar_t *marpaESLIFGrammarp, short onStackb);
static inline void                   _marpaESLIFGrammar_grammarStack_freev(marpaESLIFGrammar_t *marpaESLIFGrammarp, genericStack_t *grammarStackp);
static        char                  *_marpaESLIFGrammar_symbolDescriptionCallback(void *userDatavp, int symboli);
static        short                  _marpaESLIFGrammar_symbolOptionSetterInitb(void *userDatavp, int symboli, marpaWrapperGrammarSymbolOption_t *marpaWrapperGrammarSymbolOptionp);
static        short                  _marpaESLIFGrammar_symbolOptionSetterDiscardb(void *userDatavp, int symboli, marpaWrapperGrammarSymbolOption_t *marpaWrapperGrammarSymbolOptionp);
static        short                  _marpaESLIFGrammar_symbolOptionSetterInternalb(void *userDatavp, int symboli, marpaWrapperGrammarSymbolOption_t *marpaWrapperGrammarSymbolOptionp);
static        short                  _marpaESLIFGrammar_symbolOptionSetterInternalNoeventb(void *userDatavp, int symboli, marpaWrapperGrammarSymbolOption_t *marpaWrapperGrammarSymbolOptionp);
static        short                  _marpaESLIFGrammar_grammarOptionSetterNoLoggerb(void *userDatavp, marpaWrapperGrammarOption_t *marpaWrapperGrammarOptionp);
static inline void                   _marpaESLIF_rule_createshowv(marpaESLIF_t *marpaESLIFp, marpaESLIF_grammar_t *grammarp, marpaESLIF_rule_t *rulep, char *asciishows, size_t *asciishowlp);
static inline void                   _marpaESLIF_grammar_createshowv(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaESLIF_grammar_t *grammarp, char *asciishows, size_t *asciishowlp);
static inline int                    _marpaESLIF_utf82ordi(PCRE2_SPTR8 utf8bytes, marpaESLIF_uint32_t *uint32p);
static inline short                  _marpaESLIFRecognizer_matchPostProcessingb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, size_t matchl);
static inline short                  _marpaESLIFRecognizer_appendDatab(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char *datas, size_t datal, short eofb);
static inline short                  _marpaESLIFRecognizer_createDiscardStateb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp);
static inline short                  _marpaESLIFRecognizer_createBeforeStateb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp);
static inline short                  _marpaESLIFRecognizer_createAfterStateb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp);
static inline short                  _marpaESLIFRecognizer_createLexemeDatab(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_lexeme_data_t ***lexemeDatappp, short forPauseb);
static inline void                   _marpaESLIFrecognizer_lexemeData_freev(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_lexeme_data_t **lexemeDatapp);
static inline short                  _marpaESLIFRecognizer_createLastPauseb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp);
static inline void                   _marpaESLIFrecognizer_lastPause_freev(marpaESLIFRecognizer_t *marpaESLIFRecognizerp);
static inline short                  _marpaESLIFRecognizer_createLastTryb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp);
static inline void                   _marpaESLIFrecognizer_lastTry_freev(marpaESLIFRecognizer_t *marpaESLIFRecognizerp);
static inline short                  _marpaESLIFRecognizer_encoding_eqb(marpaESLIFRecognizer_t *marpaESLIFRecognizerParentp, marpaESLIF_terminal_t *terminalp, char *inputs, size_t inputl);
static inline short                  _marpaESLIFRecognizer_isExhaustedb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short *isExhaustedbp);
static inline short                  _marpaESLIFRecognizer_isCanContinueb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short *isCanContinuebp, short *eofbp, short *isExhaustedbp);
static inline short                  _marpaESLIFRecognizer_isEofb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short *eofbp);
static inline short                  _marpaESLIFRecognizer_inputb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char **inputsp, size_t *inputlp);
static inline short                  _marpaESLIFRecognizer_hook_discardb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short discardOnOffb);
static inline short                  _marpaESLIFRecognizer_hook_discard_switchb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp);
#if MARPAESLIF_VALUEERRORPROGRESSREPORT
static inline void                   _marpaESLIFValueErrorProgressReportv(marpaESLIFValue_t *marpaESLIFValuep);
#endif
static inline marpaESLIF_symbol_t   *_marpaESLIF_resolveSymbolp(marpaESLIF_t *marpaESLIFp, genericStack_t *grammarStackp, marpaESLIF_grammar_t *current_grammarp, char *asciis, int lookupLevelDeltai, marpaESLIF_string_t *lookupGrammarStringp, marpaESLIF_grammar_t **grammarpp);

static inline char                  *_marpaESLIF_ascii2ids(marpaESLIF_t *marpaESLIFp, char *asciis);
static        short                  _marpaESLIF_lexeme_transferb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti);
static inline short                  _marpaESLIF_generic_literal_transferb(marpaESLIFValue_t *marpaESLIFValuep, marpaESLIF_string_t *stringp, int resulti);
static        short                  _marpaESLIF_symbol_literal_transferb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultNotUsedp, int resulti);
static        short                  _marpaESLIF_rule_literal_transferb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb);
static        short                  _marpaESLIF_lexeme_concatb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb);
static        void                   _marpaESLIF_lexeme_freeCallbackv(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp);
static        void                   _marpaESLIF_rule_freeCallbackv(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp);
static        void                   _marpaESLIF_generic_freeCallbackv(void *userDatavp, marpaESLIFValueResult_t *marpaESLIFValueResultp);
static inline marpaESLIFValue_t     *_marpaESLIFValue_newp(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIFValueOption_t *marpaESLIFValueOptionp, short silentb, short fakeb);
static inline short                  _marpaESLIFValue_stack_newb(marpaESLIFValue_t *marpaESLIFValuep);
static inline short                  _marpaESLIFValue_stack_freeb(marpaESLIFValue_t *marpaESLIFValuep);
static inline short                  _marpaESLIFValue_stack_setb(marpaESLIFValue_t *marpaESLIFValuep, int indicei, marpaESLIFValueResult_t *marpaESLIFValueResultp);
static inline short                  _marpaESLIFValue_stack_getb(marpaESLIFValue_t *marpaESLIFValuep, int indicei, marpaESLIFValueResult_t *marpaESLIFValueResultp);
static inline short                  _marpaESLIFValue_stack_getAndForgetb(marpaESLIFValue_t *marpaESLIFValuep, int indicei, marpaESLIFValueResult_t *marpaESLIFValueResultp);
static inline marpaESLIFValueResult_t *_marpaESLIFValue_stack_getp(marpaESLIFValue_t *marpaESLIFValuep, int indicei);
static inline short                  _marpaESLIFValue_stack_forgetb(marpaESLIFValue_t *marpaESLIFValuep, int indicei);
static inline short                  _marpaESLIFValueResult_stack_i_setb(marpaESLIF_t *marpaESLIFp, genericStack_t *valueResultStackp, int indicei, marpaESLIFValueResult_t *marpaESLIFValueResultp, short forgetb, genericStack_t *beforePtrStackp, genericHash_t *beforePtrHashp, genericHash_t *afterPtrHashp, marpaESLIFValueResult_t *marpaESLIFValueResultOrigp);
static inline short                  _marpaESLIF_generic_action___concatb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultLexemep, int arg0i, int argni, int resulti, short nullableb, char *toEncodings, short jsonb);
static        short                  _marpaESLIF_generic_action_copyb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int argi, int resulti, short nullableb);
static        short                  _marpaESLIF_rule_action___shiftb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb);
static        short                  _marpaESLIF_rule_action___undefb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb);
static        short                  _marpaESLIF_rule_action___asciib(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb);
static        short                  _marpaESLIF_rule_action___convertb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb);
static        short                  _marpaESLIF_rule_action___concatb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb);
static        short                  _marpaESLIF_rule_action___copyb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb);
static        short                  _marpaESLIF_rule_action___trueb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb);
static        short                  _marpaESLIF_rule_action___falseb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb);
static        short                  _marpaESLIF_rule_action___jsonb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb);
static        short                  _marpaESLIF_rule_action___rowb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb);
static        short                  _marpaESLIF_rule_action___tableb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb);
static        short                  _marpaESLIF_rule_action___astb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb);
static        short                  _marpaESLIF_symbol_action___transferb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti);
static        short                  _marpaESLIF_symbol_action___undefb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti);
static        short                  _marpaESLIF_symbol_action___asciib(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti);
static        short                  _marpaESLIF_symbol_action___convertb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti);
static        short                  _marpaESLIF_symbol_action___concatb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti);
static        short                  _marpaESLIF_symbol_action___trueb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti);
static        short                  _marpaESLIF_symbol_action___falseb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti);
static        short                  _marpaESLIF_symbol_action___jsonb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti);
static        int                    _marpaESLIF_event_sorti(const void *p1, const void *p2);
static inline unsigned long          _marpaESLIF_djb2_s(unsigned char *str, size_t lengthl);
int                                  _marpaESLIF_ptrhashi(void *userDatavp, genericStackItemType_t itemType, void **pp);
int                                  _marpaESLIF_string_hash_callbacki(void *userDatavp, genericStackItemType_t itemType, void **pp);
short                                _marpaESLIF_string_cmp_callbackb(void *userDatavp, void **pp1, void **pp2);
void                                *_marpaESLIF_string_copy_callbackp(void *userDatavp, void **pp);
void                                 _marpaESLIF_string_free_callbackv(void *userDatavp, void **pp);
static        void                   _marpaESLIFRecognizerHash_freev(void *userDatavp, void **pp);
static inline void                   _marpaESLIFRecognizer_freev(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short forceb);
static inline marpaESLIFRecognizer_t *_marpaESLIFRecognizer_getFromCachep(marpaESLIF_t *marpaESLIFp, marpaESLIFGrammar_t *marpaESLIFGrammarp, short discardb, short noEventb, short silentb, marpaESLIFRecognizer_t *marpaESLIFRecognizerParentp, short fakeb, short grammarIsOnStackb);
static inline short                   _marpaESLIFRecognizer_putToCacheb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp);
static inline void                    _marpaESLIFRecognizer_redoGrammarv(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIFGrammar_t *marpaESLIFGrammarp, short fakeb, short grammarIsOnStackb);
static inline char                   *_marpaESLIF_action2asciis(marpaESLIF_action_t *actionp);
static inline short                   _marpaESLIF_action_validb(marpaESLIF_t *marpaESLIFp, marpaESLIF_action_t *actionp);
static inline short                   _marpaESLIF_action_eqb(marpaESLIF_action_t *action1p, marpaESLIF_action_t *action2p);
static inline marpaESLIF_action_t    *_marpaESLIF_action_clonep(marpaESLIF_t *marpaESLIFp, marpaESLIF_action_t *actionp);
static inline void                    _marpaESLIF_action_freev(marpaESLIF_action_t *actionp);
static inline short                   _marpaESLIF_string_removebomb(marpaESLIF_t *marpaESLIFp, char *bytep, size_t *bytelp, char *encodingasciis, size_t *bomsizelp);
static inline void                    _marpaESLIF_codepoint_to_json(marpaESLIF_uint32_t codepoint, genericLogger_t *genericLoggerp);
static inline short                   _marpaESLIF_flatten_pointers(marpaESLIF_t *marpaESLIFp, genericStack_t *flattenPtrStackp, genericHash_t *flattenPtrHashp, marpaESLIFValueResult_t *marpaESLIFValueResultp, short noShallowb);

/*****************************************************************************/
static inline marpaESLIF_string_t *_marpaESLIF_string_newp(marpaESLIF_t *marpaESLIFp, char *encodingasciis, char *bytep, size_t bytel)
/*****************************************************************************/
/* Caller is responsible to set coherent values of bytes and bytel.          */
/* In particular an empty string must be set with bytep = NULL and bytel = 0 */
/*****************************************************************************/
{
  static const char   *funcs = "_marpaESLIF_string_newp";
  marpaESLIF_string_t *stringp = NULL;
  char                *dstbytep;

  stringp = (marpaESLIF_string_t *) malloc(sizeof(marpaESLIF_string_t));
  if (stringp == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
    goto err;
  }

  stringp->encodingasciis = NULL;
  stringp->asciis         = NULL;

  if (bytel > 0) {
    /* bytel + 1 for the hiden NUL byte */
    if ((stringp->bytep = dstbytep = (char *) calloc(1, bytel + 1)) == NULL) {
      /* We always add a NUL byte for convenience */
      MARPAESLIF_ERRORF(marpaESLIFp, "calloc failure, %s", strerror(errno));
      goto err;
    }
    memcpy(dstbytep, bytep, bytel);
    stringp->bytel = bytel;
  } else {
    stringp->bytep = (char *) MARPAESLIF_EMPTY_STRING;
    stringp->bytel = 0;
  }

  if (bytel > 0) {
    /* This will fill stringp->encodingasciis if not already set */
    if ((stringp->asciis = _marpaESLIF_charconvb(marpaESLIFp, "ASCII//TRANSLIT//IGNORE", encodingasciis, bytep, bytel, NULL, &(stringp->encodingasciis), NULL /* tconvpp */, 1 /* eofb */, NULL /* byteleftsp */, NULL /* byteleftlp */, NULL /* byteleftalloclp */, 0 /* tconvsilentb */)) == NULL) {
      goto err;
    }
  } else {
    /* ASCII version is an empty string */
    stringp->asciis = (char *) MARPAESLIF_EMPTY_STRING;
    /* Copy encodingasciis if any */
    if (encodingasciis != NULL) {
      if (encodingasciis == MARPAESLIF_UTF8_STRING) {
        /* Internal variable used to avoid unnecessary strdup() call */
        stringp->encodingasciis = (char *) MARPAESLIF_UTF8_STRING;
      } else {
        if ((stringp->encodingasciis = strdup(encodingasciis)) == NULL) {
          MARPAESLIF_ERRORF(marpaESLIFp, "strdup failure, %s", strerror(errno));
          goto err;
        }
      }
    }
  }

  goto done;

 err:
  _marpaESLIF_string_freev(stringp, 0 /* onStackb */);
  stringp = NULL;

 done:
  return stringp;
}

/*****************************************************************************/
static inline marpaESLIF_string_t *_marpaESLIF_string_clonep(marpaESLIF_t *marpaESLIFp, marpaESLIF_string_t *stringp)
/*****************************************************************************/
{
  marpaESLIF_string_t *rcp = NULL;
  char                *bytep;
  size_t               bytel;
  
  if (stringp == NULL) {
    goto err;
  }

  rcp = (marpaESLIF_string_t *) malloc(sizeof(marpaESLIF_string_t));
  if (rcp == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
    goto err;
  }

  rcp->encodingasciis = NULL;
  rcp->asciis         = NULL;

  if ((rcp->bytel = stringp->bytel) > 0) {
    bytep = rcp->bytep = (char *) calloc(1, (bytel = stringp->bytel) + 1); /* We always add a NUL byte for convenience */
    if (bytep == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFp, "calloc failure, %s", strerror(errno));
      goto err;
    }
    memcpy(bytep, stringp->bytep, bytel);
  } else {
    rcp->bytep = (char *) MARPAESLIF_EMPTY_STRING;
  }

  if (stringp->asciis != NULL) {
    if (stringp->asciis == MARPAESLIF_EMPTY_STRING) {
      rcp->asciis = (char *) MARPAESLIF_EMPTY_STRING;
    } else {
      if ((rcp->asciis = strdup(stringp->asciis)) == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "strdup failure, %s", strerror(errno));
        goto err;
      }
    }
  }

  if (stringp->encodingasciis != NULL) {
    if (stringp->encodingasciis == MARPAESLIF_UTF8_STRING) {
      /* Internal variable used to avoid unnecesary strdup() call */
      rcp->encodingasciis = (char *) MARPAESLIF_UTF8_STRING;
    } else {
      if ((rcp->encodingasciis = strdup(stringp->encodingasciis)) == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "strdup failure, %s", strerror(errno));
        goto err;
      }
    }
  }

  goto done;

 err:
  _marpaESLIF_string_freev(rcp, 0 /* onStackb */);
  rcp = NULL;

 done:
  return rcp;
}

/*****************************************************************************/
static inline void _marpaESLIF_string_freev(marpaESLIF_string_t *stringp, short onStackb)
/*****************************************************************************/
{
  if (stringp != NULL) {
    if ((stringp->bytep != NULL) && (stringp->bytep != MARPAESLIF_EMPTY_STRING)) {
      free(stringp->bytep);
    }
    if ((stringp->encodingasciis != NULL) && (stringp->encodingasciis != MARPAESLIF_UTF8_STRING)) {
      free(stringp->encodingasciis);
    }
    if ((stringp->asciis != NULL) && (stringp->asciis != MARPAESLIF_EMPTY_STRING)) {
      free(stringp->asciis);
    }
    if (! onStackb) {
      free(stringp);
    }
  }
}

/*****************************************************************************/
static inline short _marpaESLIF_string_utf8_eqb(marpaESLIF_string_t *string1p, marpaESLIF_string_t *string2p)
/*****************************************************************************/
{
  /* It is assumed the caller compare strings with the same encoding - UTF-8 in our case */
  char  *byte1p;
  char  *byte2p;
  size_t bytel;

  if ((string1p == NULL) || (string2p == NULL)) {
    return 0;
  }
  if (((byte1p = string1p->bytep) == NULL) || ((byte2p = string2p->bytep) == NULL)) {
    return 0;
  }
  if ((bytel = string1p->bytel) != string2p->bytel) {
    return 0;
  }
  return (memcmp(byte1p, byte2p, bytel) == 0) ? 1 : 0;
}

/*****************************************************************************/
static inline short _marpaESLIF_string_eqb(marpaESLIF_string_t *string1p, marpaESLIF_string_t *string2p)
/*****************************************************************************/
{
  /* It is assumed the caller compare strings with the same encoding - UTF-8 in our case */
  char  *byte1p;
  char  *byte2p;
  size_t bytel;

  if ((string1p == NULL) || (string2p == NULL)) {
    return 0;
  }
  if (((byte1p = string1p->bytep) == NULL) || ((byte2p = string2p->bytep) == NULL)) {
    return 0;
  }
  if ((bytel = string1p->bytel) != string2p->bytel) {
    return 0;
  }
  return (memcmp(byte1p, byte2p, bytel) == 0) ? 1 : 0;
}

/*****************************************************************************/
static inline marpaESLIF_terminal_t *_marpaESLIF_terminal_newp(marpaESLIF_t *marpaESLIFp, marpaESLIF_grammar_t *grammarp, int eventSeti, char *descEncodings, char *descs, size_t descl, marpaESLIF_terminal_type_t type, char *modifiers, char *utf8s, size_t utf8l, char *testFullMatchs, char *testPartialMatchs)
/*****************************************************************************/
/* This method is bootstraped at marpaESLIFp creation itself to have the internal regexps, with grammarp being NULL... */
/*****************************************************************************/
{
  static const char                *funcs = "_marpaESLIF_terminal_newp";
  char                             *strings               = NULL;
  marpaESLIFRecognizer_t           *marpaESLIFRecognizerp = NULL;
#ifndef MARPAESLIF_NTRACE
  marpaESLIFRecognizer_t           *marpaESLIFRecognizerTestp = NULL;
#endif
  marpaESLIF_string_t              *content2descp         = NULL;
  char                             *generatedasciis       = NULL;
  short                             memcmpb               = 0;
  marpaESLIF_terminal_t            *terminalp             = NULL;
  marpaWrapperGrammarSymbolOption_t marpaWrapperGrammarSymbolOption;
  marpaESLIF_uint32_t               pcre2Optioni = PCRE2_ANCHORED;
  int                               pcre2Errornumberi;
  PCRE2_SIZE                        pcre2ErrorOffsetl;
  PCRE2_UCHAR                       pcre2ErrorBuffer[256];
  int                               i;
  marpaESLIFGrammar_t               marpaESLIFGrammar;
  char                             *inputs;
  size_t                            inputl;
  marpaESLIF_matcher_value_t        rci;
  marpaESLIF_uint32_t               codepointi;
  marpaESLIF_uint32_t               firstcodepointi;
  marpaESLIF_uint32_t               lastcodepointi;
  short                             backslashb;
  short                             utfflagb;
  size_t                            stringl;
  char                             *tmps;
  size_t                            hexdigitl;
  int                               utf82ordi;
  marpaESLIFValueResult_t           marpaESLIFValueResult;
  char                             *matchedp;
  size_t                            matchedl;
  char                             *modifiersp;
  char                              modifierc;
  short                             asciisafeb;
  char                             *bytes;
  size_t                            bytel;

  /* Check some required parameters */
  if ((utf8s == NULL) || (utf8l <= 0)) {
    MARPAESLIF_ERROR(marpaESLIFp, "Invalid terminal origin");
    goto err;
  }

  /* Please note the "fakeb" parameter below */
  terminalp = (marpaESLIF_terminal_t *) malloc(sizeof(marpaESLIF_terminal_t));
  if (terminalp == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
    goto err;
  }

  terminalp->idi                 = -1;
  terminalp->descp               = NULL;
  terminalp->modifiers           = NULL;
  terminalp->patterns            = NULL;
  terminalp->patternl            = 0;
  terminalp->patterni            = 0;
  terminalp->regex.patternp      = NULL;
  terminalp->regex.match_datap   = NULL;
#ifdef PCRE2_CONFIG_JIT
  terminalp->regex.jitCompleteb  = 0;
  terminalp->regex.jitPartialb   = 0;
#endif
  terminalp->regex.isAnchoredb   = 0;
  terminalp->regex.utfb          = 0;
  terminalp->memcmpb             = 0;
  terminalp->bytes               = NULL;
  terminalp->bytel               = 0;

  /* ----------- Modifiers ------------ */
  if (modifiers != NULL) {
    terminalp->modifiers = strdup(modifiers);
    if (terminalp->modifiers == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFp, "strdup failure, %s", strerror(errno));
      goto err;
    }
  }

  /* ----------- Terminal Identifier ------------ */
  if (grammarp != NULL) { /* Here is the bootstrap dependency with grammarp == NULL */
    marpaWrapperGrammarSymbolOption.terminalb = 1;
    marpaWrapperGrammarSymbolOption.startb    = 0;
    marpaWrapperGrammarSymbolOption.eventSeti = eventSeti;
    terminalp->idi = marpaWrapperGrammar_newSymboli(grammarp->marpaWrapperGrammarStartp, &marpaWrapperGrammarSymbolOption);
    if (terminalp->idi < 0) {
      goto err;
    }
  }

  /* ----------- Terminal Description ------------ */
  if (descs == NULL) {
    /* Get an ASCII version of the content */
    content2descp = _marpaESLIF_string_newp(marpaESLIFp, (char *) MARPAESLIF_UTF8_STRING, utf8s, utf8l);
    if (content2descp == NULL) {
      goto err;
    }
    if (type == MARPAESLIF_TERMINAL_TYPE_STRING) {
      /* Use already escaped version -; */
      if (modifiers != NULL) {
        /* ":xxxx */
        generatedasciis = (char *) malloc(strlen(content2descp->asciis) + 1 + strlen(modifiers) + 1);
        if (generatedasciis == NULL) {
          MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
          goto err;
        }
        strcpy(generatedasciis, content2descp->asciis);
        strcat(generatedasciis, ":");
        strcat(generatedasciis, modifiers);
      } else {
        generatedasciis = strdup(content2descp->asciis);
        if (generatedasciis == NULL) {
          MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
          goto err;
        }
      }
    } else {
      /* "/" + XXX + "/" (without escaping) */
      if (modifiers != NULL) {
        /* xxxx */
        generatedasciis = (char *) malloc(1 + strlen(content2descp->asciis) + 1 + strlen(modifiers) + 1);
      } else {
        generatedasciis = (char *) malloc(1 + strlen(content2descp->asciis) + 1 + 1);
      }
      if (generatedasciis == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
        goto err;
      }
      strcpy(generatedasciis, "/");
      strcat(generatedasciis, content2descp->asciis);
      strcat(generatedasciis, "/");
      if (modifiers != NULL) {
        strcat(generatedasciis, modifiers);
      }
    }
    terminalp->descp = _marpaESLIF_string_newp(marpaESLIFp, "ASCII", generatedasciis, strlen(generatedasciis));
  } else {
    terminalp->descp = _marpaESLIF_string_newp(marpaESLIFp, descEncodings, descs, descl);
  }
  if (terminalp->descp == NULL) {
    goto err;
  }

  /* ----------- Terminal Implementation ------------ */
  switch (type) {

  case MARPAESLIF_TERMINAL_TYPE_STRING:

    /* If there are no modifiers, then the terminals as taken as-is */
    /* The only string modifier allowed is case-insensitive, that WILL */
    /* require regex. */
    if (modifiers == NULL) {
      /* Per definition the real string cannot be longer than bytel. At most */
      /* it is exactly this size. Everytime there is a backslashed character */
      /* the backslash itself is skipped. This mean that allocating bytel+1 */
      /* bytes is always guaranteed to get the raw string entirely. We overestimate */
      /* this in other was: the {bytesl,bytel} array must contain the delimiters, that */
      /* are also skipped when parsing it. */
      bytes = (char *) malloc(utf8l+1);
      if (bytes == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
        goto err;
      }
      memcmpb = terminalp->memcmpb = 1;
      terminalp->bytes = bytes;  /* bytes will move everytime we append to the buffer */
      bytel   = 0;               /* bytel will increase everytime we append to the buffer */

      /* If there is a failure, _marpaESLIF_terminal_freev() will take care of bytes */
    }
    

    /* We convert a string terminal into a regexp */
    /* By construction we are coming from the parsing of a grammar, that previously translated the whole */
    /* grammar into an UTF-8 string. We use PCRE2 to extract all code points, and create a new string that */
    /* is a concatenation of \x{} thingies. By doing so, btw, we are able to know if we need PCRE2_UTF flag. */
    /* We are also able to reconstruct the raw string, because we recognizer the escape characters. */

    marpaESLIFGrammar.marpaESLIFp = marpaESLIFp;
    inputs = utf8s;
    inputl = utf8l;

    /* Fake a recognizer. EOF flag will be set automatically in fake mode */
    marpaESLIFRecognizerp = _marpaESLIFRecognizer_newp(&marpaESLIFGrammar,
                                                       NULL /* marpaESLIFRecognizerOptionp */,
                                                       0 /* discardb - not used anyway because we are in fake mode */,
                                                       1 /* noEventb - not used anyway because we are in fake mode */,
                                                       0 /* silentb */,
                                                       NULL /* marpaESLIFRecognizerParentp */,
                                                       1, /* fakeb */
                                                       0, /* maxStartCompletionsi */
                                                       1, /* Here, we know input is UTF-8 valid */
                                                       1 /* grammmarIsOnStackb */);
    if (marpaESLIFRecognizerp == NULL) {
      goto err;
    }
#ifndef MARPAESLIF_NTRACE
    MARPAESLIF_HEXDUMPV(marpaESLIFRecognizerp, "String conversion to regexp for ", terminalp->descp->asciis, utf8s, utf8l, 1 /* traceb */);
#endif

    /* Please note that at the very very early startup, when we create marpaESLIFp, there is NO marpaESLIFp->anycharp yet! */
    /* But we will never crash because marpaESLIFp never create its internal terminals using the STRING type -; */
    while (inputl > 0) {
      if (! _marpaESLIFRecognizer_terminal_matcherb(marpaESLIFRecognizerp, marpaESLIFp->anycharp, inputs, inputl, 1 /* eofb */, &rci, &marpaESLIFValueResult, NULL /* matchedLengthlp */)) {
        goto err;
      }
      if (rci != MARPAESLIF_MATCH_OK) {
        MARPAESLIF_ERROR(marpaESLIFp, "Failed to detect all characters of terminal string");
        goto err;
      }
#ifndef MARPAESLIF_NTRACE
      MARPAESLIF_VALUECHECK_IF_LEXEME_MODE(marpaESLIFp, marpaESLIFValueResult);
#endif
      if (! _marpaESLIFRecognizer_lexemeStack_i_setb(marpaESLIFRecognizerp, marpaESLIFRecognizerp->lexemeInputStackp, GENERICSTACK_USED(marpaESLIFRecognizerp->lexemeInputStackp), &marpaESLIFValueResult)) {
        free(marpaESLIFValueResult.u.a.p);
        goto err;
      }
      inputs += marpaESLIFValueResult.u.a.sizel;
      inputl -= marpaESLIFValueResult.u.a.sizel;
    }
    /* All matches are in the recognizer's lexeme input stack, in order. Take all unicode code points to generate a regex out of this string. */
    utfflagb = 0;
    stringl = 0;
    backslashb = 0;
    /* Remember that lexeme input stack is putting a fake value at indice 0, because marpa does not like it */
    for (i = 1; i < GENERICSTACK_USED(marpaESLIFRecognizerp->lexemeInputStackp); i++) {
      if (! _marpaESLIFRecognizer_lexemeStack_i_p_and_sizeb(marpaESLIFRecognizerp, marpaESLIFRecognizerp->lexemeInputStackp, i, &matchedp, &matchedl)) {
        goto err;
      }
      /* Get the code point from the UTF-8 representation */
      utf82ordi = _marpaESLIF_utf82ordi((PCRE2_SPTR8) matchedp, &codepointi);
      if (utf82ordi <= 0) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Malformed UTF-8 character at offset %d", -utf82ordi);
        goto err;
      } else if (utf82ordi != (int) matchedl) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Not all bytes consumed: %d instead of %ld", utf82ordi, (unsigned long) matchedl);
        goto err;
      }
      if (i == 1) {
        /* We want to skip first and last characters, and we will use that to detect the backslash... Note that it cannot be backslash per def as per the regexps */
        firstcodepointi = codepointi;
        /* First codepoints are known in advance */
        switch (firstcodepointi) {
        case '\'':
          lastcodepointi = '\'';
          break;
        case '"':
          lastcodepointi = '"';
          break;
        case 0x201c:
          lastcodepointi = 0x201d;
          break;
        default:
          {
            if (isprint((unsigned char) codepointi)) {
              MARPAESLIF_ERRORF(marpaESLIFp, "Impossible first codepoint %c (0x%02lx), should be 0x201c, \"'\" or '\"'", (unsigned char) codepointi, (unsigned long) codepointi);
            } else {
              MARPAESLIF_ERRORF(marpaESLIFp, "Impossible first codepoint 0x%02lx, should be 0x201c, \"'\" or '\"'", (unsigned long) codepointi);
            }
            goto err;
          }
        }
        continue;
      } else if (i == (GENERICSTACK_USED(marpaESLIFRecognizerp->lexemeInputStackp) - 1)) {
        /* Trailing backslash ? */
        if (backslashb) {
          MARPAESLIF_ERROR(marpaESLIFp, "Trailing backslash in string is not allowed");
          goto err;
        }
        /* Non-sense to not have the same value */
        if (lastcodepointi != codepointi) {
          /* Note that we know that our regexp start and end with printable characters */
          if (isprint((unsigned char) codepointi)) {
            MARPAESLIF_ERRORF(marpaESLIFp, "First and last characters do not correspond: %c (0x%02lx) v.s. %c (0x%02lx) (wanted %c (0x%lx))",
                              (unsigned char) firstcodepointi, (unsigned long) firstcodepointi,
                              (unsigned char) codepointi, (unsigned long) codepointi,
                              (unsigned char) lastcodepointi, (unsigned long) lastcodepointi);
          } else {
            MARPAESLIF_ERRORF(marpaESLIFp, "First and last characters do not correspond: %c (0x%02lx) v.s. 0x%02lx (wanted %c (0x%lx))",
                              (unsigned char) firstcodepointi, (unsigned long) firstcodepointi,
                              (unsigned long) codepointi,
                              (unsigned char) lastcodepointi, (unsigned long) lastcodepointi);
          }
          goto err;
        }
        break;
      } else {
        /* Backslash stuff */
        if (codepointi == '\\') {
          if (! backslashb) {
            /* Next character MAY BE escaped. Only backslash itself or the first character is considered as per the regexp. */
            MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "Backslash character remembered");
            backslashb = 1;
            continue;
          } else {
            /* This is escaped backslash */
            MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "Escaped backslash character accepted");
            backslashb = 0;
          }
        } else if (codepointi == lastcodepointi) {
          if (! backslashb) {
            /* This is a priori impossible to not have the first or backslash character if it is not preceeded by backslash */
            if (codepointi == lastcodepointi) {
              MARPAESLIF_ERRORF(marpaESLIFp, "First character %c found but no preceeding backslash", (unsigned char) codepointi);
            } else {
              MARPAESLIF_ERROR(marpaESLIFp, "Backslash character found but no preceeding backslash");
            }
            goto err;
          }
          /* This is escaped first character */
          MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "Escaped quote character accepted");
          backslashb = 0;
        } else {
          if (backslashb) {
            /* Here the backslash flag must not be true */
            if (isprint((unsigned char) codepointi)) {
              MARPAESLIF_ERRORF(marpaESLIFp, "Got character %c (0x%02lx) preceeded by backslash: in your string only backslash character (\\) or the string delimitor (%c) can be escaped", (unsigned char) codepointi, (unsigned long) codepointi, (unsigned char) firstcodepointi);
            } else {
              MARPAESLIF_ERRORF(marpaESLIFp, "Got character 0x%02lx (non printable) preceeded by backslash: in your string only backslash character (\\) or the string delimitor (%c) can be escaped", (unsigned long) codepointi, (unsigned char) firstcodepointi);
            }
            goto err;
          }
          /* All is well */
#ifndef MARPAESLIF_NTRACE
          if (isprint((unsigned char) codepointi)) {
            MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Got character %c (0x%02lx)", (unsigned char) codepointi, (unsigned long) codepointi);
          } else {
            MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Got character 0x%02lx (non printable)", (unsigned long) codepointi);
          }
#endif
        }
      }
      /* The recognizerd UTF-8 character start at matchedp and is of size matchedl */
      if (memcmpb) {
        /* bytes is guaranteed to have enough room for the matched character. */
        memcpy(bytes, matchedp, matchedl);
        bytes += matchedl;
        bytel += matchedl;
      }
      
      /* Determine the number of hex digits to fully represent the code point, remembering if we need PCRE2_UTF flag */
      if ((codepointi >= 0x20) && (codepointi <= 0x7E)) {
        /* Characters [0x20-0x7E] are considered safe */
        /* Since we are not doing a character-class thingy, we escape all PCRE2 metacharacters */
        /* that are recognized outside of a character class */
        switch ((unsigned char) codepointi) {
        case '\\':
        case '^':
        case '$':
        case '.':
        case '[':
        case '|':
        case '(':
        case ')':
        case '?':
        case '*':
        case '+':
        case '{':
          asciisafeb = 2;
          break;
        default:
          asciisafeb = 1;
          break;
        }
      } else {
        asciisafeb = 0;
        hexdigitl = 4; /* \x{} */
        if ((codepointi & 0xFF000000) != 0x00000000) {
          hexdigitl += 8;
          utfflagb = 1;
        } else if ((codepointi & 0x00FF0000) != 0x00000000) {
          hexdigitl += 6;
          utfflagb = 1;
        } else if ((codepointi & 0x0000FF00) != 0x00000000) {
          hexdigitl += 4;
          utfflagb = 1;
        } else {
          hexdigitl += 2;
        }
      }
      /* Append the ASCII representation */
      stringl += (asciisafeb > 0) ? asciisafeb : hexdigitl;
      if (strings == NULL) {
        strings = (char *) malloc(stringl + 1);
        if (strings == NULL) {
          MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
          goto err;
        }
        strings[0] = '\0'; /* Start with an empty string */
      } else {
        tmps = (char *) realloc(strings, stringl + 1);
        if (tmps == NULL) {
          MARPAESLIF_ERRORF(marpaESLIFp, "realloc failure, %s", strerror(errno));
          goto err;
        }
        strings = tmps;
      }
      strings[stringl] = '\0'; /* Makes sure the string always end with NUL */
      if (asciisafeb > 0) {
        if (asciisafeb > 1) {
          sprintf(strings + strlen(strings), "\\%c", (unsigned char) codepointi);
        } else {
          sprintf(strings + strlen(strings), "%c", (unsigned char) codepointi);
        }
      } else {
        hexdigitl -= 4; /* \x{} */
        sprintf(strings + strlen(strings), "\\x{%0*lx}", (int) hexdigitl, (unsigned long) codepointi);
      }
    }
    /* Done - now we can generate a regexp out of that UTF-8 compatible string */
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s: content string converted to regex %s (UTF=%d)", terminalp->descp->asciis, strings, utfflagb);
    utf8s = strings;
    utf8l = stringl;
    /* opti for string is compatible with opti for regex - just that the lexer accept less options - in particular the UTF flag */
    if (utfflagb) {
      pcre2Optioni |= PCRE2_UTF;
    }

    /* ********************************************************************************************************** */
    /*                                   THERE IS NO BREAK INTENTIONALY HERE                                      */
    /* ********************************************************************************************************** */
    /* break; */

    /* Please note that we do not unescape: in character class, if "]" was escaped it has to be left as is. In */
    /* a regular expression, if "/" is escaped, this is has no impact. */

  case MARPAESLIF_TERMINAL_TYPE_REGEX:

    if ((type != MARPAESLIF_TERMINAL_TYPE_STRING) && (marpaESLIFp->anycharp != NULL)) {
      /* Coming directly there, try to determine the need of PCRE2_UTF. This will not work with */
      /* character classes containing codepoints in the form \x{}, but then PCRE2 will yell on its own. */
      /* This does not work when we build internal marpaESLIF regex itself -; */
      if (marpaESLIFRecognizerp != NULL) {
        marpaESLIFRecognizer_freev(marpaESLIFRecognizerp);
        marpaESLIFRecognizerp = NULL;
      }

      marpaESLIFGrammar.marpaESLIFp = marpaESLIFp;
      inputs = utf8s;
      inputl = utf8l;

      /* Fake a recognizer. EOF flag will be set automatically in fake mode */
      marpaESLIFRecognizerp = _marpaESLIFRecognizer_newp(&marpaESLIFGrammar,
                                                         NULL /* marpaESLIFRecognizerOptionp */,
                                                         0 /* discardb - not used anyway because we are in fake mode */,
                                                         1 /* noEventb - not used anyway because we are in fake mode */,
                                                         0 /* silentb */,
                                                         NULL /* marpaESLIFRecognizerParentp */,
                                                         1, /* fakeb */
                                                         0, /* maxStartCompletionsi */
                                                         1, /* Here we know input is UTF-8 valid */
                                                         1 /* grammmarIsOnStackb */);
      if (marpaESLIFRecognizerp == NULL) {
        goto err;
      }

      while (inputl > 0) {
        if (! _marpaESLIFRecognizer_terminal_matcherb(marpaESLIFRecognizerp, marpaESLIFp->anycharp, inputs, inputl, 1 /* eofb */, &rci, &marpaESLIFValueResult, NULL /* matchedLengthlp */)) {
          goto err;
        }
        if (rci != MARPAESLIF_MATCH_OK) {
          MARPAESLIF_ERROR(marpaESLIFp, "Failed to detect all characters of terminal string");
          goto err;
        }
#ifndef MARPAESLIF_NTRACE
        MARPAESLIF_VALUECHECK_IF_LEXEME_MODE(marpaESLIFp, marpaESLIFValueResult);
#endif
        if (! _marpaESLIFRecognizer_lexemeStack_i_setb(marpaESLIFRecognizerp, marpaESLIFRecognizerp->lexemeInputStackp, GENERICSTACK_USED(marpaESLIFRecognizerp->lexemeInputStackp), &marpaESLIFValueResult)) {
          free(marpaESLIFValueResult.u.a.p);
          goto err;
        }
        inputs += marpaESLIFValueResult.u.a.sizel;
        inputl -= marpaESLIFValueResult.u.a.sizel;
      }
      /* All matches are in the recognizer's lexeme input stack, in order. Take all unicode code points. */
      utfflagb = 0;
      /* Remember that lexeme input stack is putting a fake value at indice 0, because marpa does not like it */
      for (i = 1; i < GENERICSTACK_USED(marpaESLIFRecognizerp->lexemeInputStackp); i++) {
        if (! _marpaESLIFRecognizer_lexemeStack_i_p_and_sizeb(marpaESLIFRecognizerp, marpaESLIFRecognizerp->lexemeInputStackp, i, &matchedp, &matchedl)) {
          goto err;
        }
        /* Get the code point from the UTF-8 representation */
        utf82ordi = _marpaESLIF_utf82ordi((PCRE2_SPTR8) matchedp, &codepointi);
        if (utf82ordi <= 0) {
          MARPAESLIF_ERRORF(marpaESLIFp, "Malformed UTF-8 character at offset %d", -utf82ordi);
          goto err;
        } else if (utf82ordi != (int) matchedl) {
          MARPAESLIF_ERRORF(marpaESLIFp, "Not all bytes consumed: %d instead of %ld", utf82ordi, (unsigned long) matchedl);
          goto err;
        }
        /* Determine the number of hex digits to fully represent the code point, remembering if we need PCRE2_UTF flag */
        if (codepointi > 0xFF) {
          utfflagb = 1;
          break;
        }
      }
      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s: regex content scanned and give UTF=%d", terminalp->descp->asciis, strings, utfflagb);
      /* Detected the need of UTF flag in the regex ? */
      if (utfflagb) {
        pcre2Optioni |= PCRE2_UTF;
      }
    }

    /* Apply user options */
    switch (type) {
    case MARPAESLIF_TERMINAL_TYPE_STRING:
    case MARPAESLIF_TERMINAL_TYPE_REGEX:
      if (modifiers != NULL) {
        modifiersp = modifiers;
        while ((modifierc = *modifiersp++) != '\0') {
          for (i = 0; i < (sizeof(marpaESLIF_regex_option_map) / sizeof(marpaESLIF_regex_option_map[0])); i++) {
            if (modifierc == marpaESLIF_regex_option_map[i].modifierc) {
              /* It is important to process pcre2OptionNoti first */
              if (marpaESLIF_regex_option_map[i].pcre2OptionNoti != 0) {
                MARPAESLIF_TRACEF(marpaESLIFp, funcs, "%s: regex modifier %c: removing %s", terminalp->descp->asciis, marpaESLIF_regex_option_map[i].modifierc, marpaESLIF_regex_option_map[i].pcre2OptionNots);
                pcre2Optioni &= ~marpaESLIF_regex_option_map[i].pcre2OptionNoti;
              }
              if (marpaESLIF_regex_option_map[i].pcre2Optioni != 0) {
                MARPAESLIF_TRACEF(marpaESLIFp, funcs, "%s: regex modifier %c: adding %s", terminalp->descp->asciis, marpaESLIF_regex_option_map[i].modifierc, marpaESLIF_regex_option_map[i].pcre2Options);
                pcre2Optioni |= marpaESLIF_regex_option_map[i].pcre2Optioni;
              }
            }
          }
        }
      }
      break;
    default:
      MARPAESLIF_ERRORF(marpaESLIFp, "Unsupported terminal type %d", type);
      goto err;
    }

    if (utf8s == NULL) {
      /* Case of the empty string => empty pattern */
      /* Note that this is different from // in the grammar: // is NOT recognized as a valid regex */
      terminalp->regex.patternp = pcre2_compile(
                                                (PCRE2_SPTR) "",
                                                (PCRE2_SIZE) 0,
                                                pcre2Optioni,
                                                &pcre2Errornumberi, /* for error number */
                                                &pcre2ErrorOffsetl, /* for error offset */
                                                NULL);        /* use default compile context */
    } else {
      terminalp->regex.patternp = pcre2_compile(
                                                (PCRE2_SPTR) utf8s,      /* An UTF-8 pattern */
                                                (PCRE2_SIZE) utf8l,      /* In code units (!= code points) - in UTF-8 a code unit is a byte */
                                                pcre2Optioni,
                                                &pcre2Errornumberi, /* for error number */
                                                &pcre2ErrorOffsetl, /* for error offset */
                                                NULL);        /* use default compile context */
    }
    if (terminalp->regex.patternp == NULL) {
      pcre2_get_error_message(pcre2Errornumberi, pcre2ErrorBuffer, sizeof(pcre2ErrorBuffer));
      MARPAESLIF_ERRORF(marpaESLIFp, "%s: pcre2_compile failure at offset %ld: %s", terminalp->descp->asciis, (unsigned long) pcre2ErrorOffsetl, pcre2ErrorBuffer);
      if (marpaESLIFRecognizerp != NULL) {
        MARPAESLIF_HEXDUMPV(marpaESLIFRecognizerp, "Dump of PCRE2 pattern", " as an UTF-8 sequence of bytes", utf8s, utf8l, 0 /* traceb */);
      }
      goto err;
    }
    terminalp->regex.match_datap = pcre2_match_data_create(1 /* We are interested in the string that matched the full pattern */,
                                                             NULL /* Default memory allocation */);
    if (terminalp->regex.match_datap == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFp, "%s: pcre2_match_data_create_from_pattern failure, %s", terminalp->descp->asciis, strerror(errno));
      goto err;
    }
    /* Determine if we can do JIT */
#ifdef PCRE2_CONFIG_JIT
    if ((pcre2_config(PCRE2_CONFIG_JIT, &pcre2Optioni) >= 0) && (pcre2Optioni == 1)) {
#ifdef PCRE2_JIT_COMPLETE
      terminalp->regex.jitCompleteb = (pcre2_jit_compile(terminalp->regex.patternp, PCRE2_JIT_COMPLETE) == 0) ? 1 : 0;
#else
      terminalp->regex.jitCompleteb = 0;
#endif
#ifdef PCRE2_JIT_PARTIAL_HARD
      terminalp->regex.jitPartialb = (pcre2_jit_compile(terminalp->regex.patternp, PCRE2_JIT_PARTIAL_HARD) == 0) ? 1 : 0;
#else
      terminalp->regex.jitPartialb = 0;
#endif /*  PCRE2_CONFIG_JIT */
    } else {
      terminalp->regex.jitCompleteb = 0;
      terminalp->regex.jitPartialb = 0;
    }
#endif /*  PCRE2_CONFIG_JIT */
    /* And some modes after the pattern was allocated */
    pcre2Errornumberi = pcre2_pattern_info(terminalp->regex.patternp, PCRE2_INFO_ALLOPTIONS, &pcre2Optioni);
    if (pcre2Errornumberi != 0) {
      pcre2_get_error_message(pcre2Errornumberi, pcre2ErrorBuffer, sizeof(pcre2ErrorBuffer));
      MARPAESLIF_ERRORF(marpaESLIFp, "%s: pcre2_pattern_info failure: %s", terminalp->descp->asciis, pcre2ErrorBuffer);
      goto err;
    }
    terminalp->regex.utfb        = ((pcre2Optioni & PCRE2_UTF) == PCRE2_UTF);
    terminalp->regex.isAnchoredb = ((pcre2Optioni & PCRE2_ANCHORED) == PCRE2_ANCHORED);
    MARPAESLIF_TRACEF(marpaESLIFp, funcs, "%s: UTF mode is %s, Anchored mode is %s",
                      terminalp->descp->asciis,
                      terminalp->regex.utfb ? "on" : "off",
                      terminalp->regex.isAnchoredb ? "on" : "off"
                      );
    break;

  default:
    MARPAESLIF_ERRORF(marpaESLIFp, "%s: Unsupported terminal type %d", terminalp->descp->asciis, type);
    goto err;
    break;
  }

  if (memcmpb) {
    /* Commit bytel and put a NUL byte for convenience */
    /* We guaranteed that this buffer is large enough at the beginning */
    terminalp->bytel = bytel;
    terminalp->bytes[bytel] = '\0';
  }

#ifndef MARPAESLIF_NTRACE
  {
    marpaESLIFGrammar_t     marpaESLIFGrammar;

    marpaESLIFGrammar.marpaESLIFp        = marpaESLIFp;
    marpaESLIFGrammar.grammarStackp      = NULL;
    marpaESLIFGrammar.grammarp           = grammarp;
    marpaESLIFGrammar.luabytep           = NULL;
    marpaESLIFGrammar.luabytel           = 0;
    marpaESLIFGrammar.luaprecompiledp    = NULL;
    marpaESLIFGrammar.luaprecompiledl    = 0;
    marpaESLIFGrammar.luadescp           = NULL;
    marpaESLIFGrammar.internalRuleCounti = 0;

    /* Fake a recognizer. EOF flag will be set automatically in fake mode */
    marpaESLIFRecognizerTestp = _marpaESLIFRecognizer_newp(&marpaESLIFGrammar,
                                                           NULL /* marpaESLIFRecognizerOptionp */,
                                                           0 /* discardb - not used anyway because we are in fake mode */,
                                                           1 /* noEventb - not used anyway because we are in fake mode */,
                                                           0 /* silentb */,
                                                           NULL /* marpaESLIFRecognizerParentp */,
                                                           1, /* fakeb */
                                                           0, /* maxStartCompletionsi */
                                                           1, /* Internal tests are always done on UTF-8 valid strings */
                                                           1 /* grammmarIsOnStackb */);
    if (marpaESLIFRecognizerTestp == NULL) {
      goto err;
    }

    /* Look to the implementations of terminal matchers: they NEVER use leveli, nor marpaWrapperGrammarp, nor grammarStackp -; */
    /* Also, note that we always end up with a regex. */
    
    if (testFullMatchs != NULL) {

      if (! _marpaESLIFRecognizer_terminal_matcherb(marpaESLIFRecognizerTestp, terminalp, testFullMatchs, strlen(testFullMatchs), 1, &rci, NULL /* marpaESLIFValueResultp */, NULL /* matchedLengthlp */)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "%s: testing full match: matcher general failure", terminalp->descp->asciis);
        goto err;
      }
      if (rci != MARPAESLIF_MATCH_OK) {
        MARPAESLIF_ERRORF(marpaESLIFp, "%s: testing full match: matcher returned rci = %d", terminalp->descp->asciis, rci);
        goto err;
      }
      /* MARPAESLIF_TRACEF(marpaESLIFp, funcs, "%s: testing full match is successful on %s", terminalp->descp->asciis, testFullMatchs); */
    }

    if (testPartialMatchs != NULL) {

      if (! _marpaESLIFRecognizer_terminal_matcherb(marpaESLIFRecognizerTestp, terminalp, testPartialMatchs, strlen(testPartialMatchs), 0, &rci, NULL /* marpaESLIFValueResultp */, NULL /* matchedLengthlp */)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "%s: testing partial match: matcher general failure", terminalp->descp->asciis);
        goto err;
      }
      if (rci != MARPAESLIF_MATCH_AGAIN) {
        MARPAESLIF_ERRORF(marpaESLIFp, "%s: testing partial match: matcher returned rci = %d", terminalp->descp->asciis, rci);
        goto err;
      }
      /* MARPAESLIF_TRACEF(marpaESLIFp, funcs, "%s: testing partial match is successful on %s when not at EOF", terminalp->descp->asciis, testPartialMatchs); */
    }
  }

#endif

  /* Creation of PCRE2 pattern is ok - keep it for bootstrap comparison when creating grammars */
  terminalp->patterns = (char *) malloc(utf8l + 1); /* We always add a NUL byte for convenience */
  if (terminalp->patterns == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
    goto err;
  }
  memcpy(terminalp->patterns, utf8s, utf8l);
  terminalp->patterns[utf8l] = '\0';
  terminalp->patternl = utf8l;
  terminalp->patterni = pcre2Optioni;
  terminalp->type     = type;

  goto done;
  
 err:
  _marpaESLIF_terminal_freev(terminalp);
  terminalp = NULL;

 done:
  if (strings != NULL) {
    free(strings);
  }
  if (generatedasciis != NULL) {
    free(generatedasciis);
  }
  _marpaESLIF_string_freev(content2descp, 0 /* onStackb */);
#ifndef MARPAESLIF_NTRACE
  marpaESLIFRecognizer_freev(marpaESLIFRecognizerTestp);
#endif
  marpaESLIFRecognizer_freev(marpaESLIFRecognizerp);
  /* MARPAESLIF_TRACEF(marpaESLIFp, funcs, "return %p", terminalp); */
  return terminalp;
}

/*****************************************************************************/
static inline marpaESLIF_meta_t *_marpaESLIF_meta_newp(marpaESLIF_t *marpaESLIFp, marpaESLIF_grammar_t *grammarp, int eventSeti, char *asciinames, char *descEncodings, char *descs, size_t descl)
/*****************************************************************************/
{
  static const char                *funcs = "_marpaESLIF_meta_newp";
  marpaESLIF_meta_t                *metap = NULL;
  marpaWrapperGrammarSymbolOption_t marpaWrapperGrammarSymbolOption;

  /* MARPAESLIF_TRACE(marpaESLIFp, funcs, "Building meta"); */

  if (asciinames == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "No name for meta symbol");
    goto err;
  }
  if (strlen(asciinames) <= 0) {
    MARPAESLIF_ERROR(marpaESLIFp, "Meta symbol name is empty");
    goto err;
  }

  metap = (marpaESLIF_meta_t *) malloc(sizeof(marpaESLIF_meta_t));
  if (metap == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
    goto err;
  }

  metap->idi                             = -1;
  metap->asciinames                      = NULL;
  metap->descp                           = NULL;
  metap->marpaWrapperGrammarLexemeClonep = NULL; /* Eventually changed when validating the grammar */
  metap->lexemeIdi                       = -1;   /* Ditto */
  metap->prioritizedb                    = 0;    /* Internal flag to prevent a prioritized symbol to appear more than once as an LHS */
  metap->marpaESLIFGrammarLexemeClonep   = NULL; /* Eventually changed when validating the grammar */
  metap->nSymbolStartl                   = 0;    /* Number of lexemes at the very beginning of marpaWrapperGrammarStartp */
  metap->symbolArrayStartp               = NULL; /* Lexemes at the very beginning of marpaWrapperGrammarStartp */

  marpaWrapperGrammarSymbolOption.terminalb = 0;
  marpaWrapperGrammarSymbolOption.startb    = 0;
  marpaWrapperGrammarSymbolOption.eventSeti = eventSeti;

  /* -------- Meta name -------- */
  metap->asciinames = strdup(asciinames);
  if (metap->asciinames == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "strdup failure, %s", strerror(errno));
    goto err;
  }

  /* -------- Meta Description - default to meta name -------- */
  if ((descs == NULL) || (descl <= 0)) {
    metap->descp = _marpaESLIF_string_newp(marpaESLIFp, "ASCII", asciinames, strlen(asciinames));
  } else {
    metap->descp = _marpaESLIF_string_newp(marpaESLIFp, descEncodings, descs, descl);
  }
  if (metap->descp == NULL) {
    goto err;
  }

  /* ----------- Meta Identifier ------------ */
  metap->idi = marpaWrapperGrammar_newSymboli(grammarp->marpaWrapperGrammarStartp, &marpaWrapperGrammarSymbolOption);
  if (metap->idi < 0) {
    goto err;
  }

  goto done;

 err:
  _marpaESLIF_meta_freev(metap);
  metap = NULL;

 done:
  /* MARPAESLIF_TRACEF(marpaESLIFp, funcs, "return %p", metap); */
  return metap;
}

/*****************************************************************************/
static inline void _marpaESLIF_meta_freev(marpaESLIF_meta_t *metap)
/*****************************************************************************/
{
  if (metap != NULL) {
    if (metap->asciinames != NULL) {
      free(metap->asciinames);
    }
    _marpaESLIF_string_freev(metap->descp, 0 /* onStackb */);
    if (metap->marpaWrapperGrammarLexemeClonep != NULL) {
      marpaWrapperGrammar_freev(metap->marpaWrapperGrammarLexemeClonep);
    }
    if (metap->symbolArrayStartp != NULL) {
      free(metap->symbolArrayStartp);
    }
    /* All the rest are shallow pointers - in particular marpaESLIFGrammarLexemeClonep is a hack for performance reasons */
    free(metap);
  }
}

/*****************************************************************************/
static inline marpaESLIF_grammar_t *_marpaESLIF_bootstrap_grammar_L0p(marpaESLIFGrammar_t *marpaESLIFGrammarp)
/*****************************************************************************/
{
  return _marpaESLIF_bootstrap_grammarp(marpaESLIFGrammarp,
					1, /* L0 in Marpa::R2 terminology is level No 1 for us */
                                        "ASCII", /* "L0" is an ASCII thingy */
                                        "L0",
                                        strlen("L0"),
					0, /* warningIsErrorb */
					1, /* warningIsIgnoredb */
					0, /* autorankb */
					sizeof(bootstrap_grammar_L0_terminals) / sizeof(bootstrap_grammar_L0_terminals[0]),
					bootstrap_grammar_L0_terminals,
					sizeof(bootstrap_grammar_L0_metas) / sizeof(bootstrap_grammar_L0_metas[0]),
					bootstrap_grammar_L0_metas,
					sizeof(bootstrap_grammar_L0_rules) / sizeof(bootstrap_grammar_L0_rules[0]),
					bootstrap_grammar_L0_rules,
                                        NULL, /* defaultSymbolActionp */
                                        NULL /* defaultRuleActionp */);
}

/*****************************************************************************/
static inline marpaESLIF_grammar_t *_marpaESLIF_bootstrap_grammar_G1p(marpaESLIFGrammar_t *marpaESLIFGrammarp)
/*****************************************************************************/
{
  return _marpaESLIF_bootstrap_grammarp(marpaESLIFGrammarp,
					0, /* G1 in Marpa::R2 terminology is level No 0 for us */
                                        "ASCII", /* "G1" is an ASCII thingy */
                                        "G1",
                                        strlen("G1"),
					0, /* warningIsErrorb */
					1, /* warningIsIgnoredb */
					0, /* autorankb */
					sizeof(bootstrap_grammar_G1_terminals) / sizeof(bootstrap_grammar_G1_terminals[0]),
					bootstrap_grammar_G1_terminals,
					sizeof(bootstrap_grammar_G1_metas) / sizeof(bootstrap_grammar_G1_metas[0]),
					bootstrap_grammar_G1_metas,
					sizeof(bootstrap_grammar_G1_rules) / sizeof(bootstrap_grammar_G1_rules[0]),
					bootstrap_grammar_G1_rules,
                                        NULL, /* defaultSymbolActionp */
                                        NULL /* defaultRuleActionp */);
}

/*****************************************************************************/
static inline marpaESLIF_grammar_t *_marpaESLIF_bootstrap_grammarp(marpaESLIFGrammar_t *marpaESLIFGrammarp,
								   int leveli,
                                                                   char *descEncodings,
                                                                   char *descs,
                                                                   size_t descl,
								   short warningIsErrorb,
								   short warningIsIgnoredb,
								   short autorankb,
								   int bootstrap_grammar_terminali, bootstrap_grammar_terminal_t *bootstrap_grammar_terminalp,
								   int bootstrap_grammar_metai, bootstrap_grammar_meta_t *bootstrap_grammar_metap,
								   int bootstrap_grammar_rulei, bootstrap_grammar_rule_t *bootstrap_grammar_rulep,
                                                                   marpaESLIF_action_t *defaultSymbolActionp,
                                                                   marpaESLIF_action_t *defaultRuleActionp)
/*****************************************************************************/
{
  static const char          *funcs        = "_marpaESLIF_bootstrap_grammarp";
  marpaESLIF_t               *marpaESLIFp  = marpaESLIFGrammarp->marpaESLIFp;
  marpaESLIF_symbol_t        *symbolp      = NULL;
  marpaESLIF_rule_t          *rulep        = NULL;
  marpaESLIF_terminal_t      *terminalp    = NULL;
  marpaESLIF_meta_t          *metap        = NULL;
  marpaESLIF_grammar_t       *grammarp;
  marpaWrapperGrammarOption_t marpaWrapperGrammarOption;
  int                         i;
  marpaESLIF_action_t         ruleAction;

  MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Bootstrapping grammar at level %d", (int) leveli);

  marpaWrapperGrammarOption.genericLoggerp    = marpaESLIFp->marpaESLIFOption.genericLoggerp;
  marpaWrapperGrammarOption.warningIsErrorb   = warningIsErrorb;
  marpaWrapperGrammarOption.warningIsIgnoredb = warningIsIgnoredb;
  marpaWrapperGrammarOption.autorankb         = autorankb;

  /* Default type of rule action, value itself is filled in the loop */
  ruleAction.type = MARPAESLIF_ACTION_TYPE_NAME;
  
  grammarp = _marpaESLIF_grammar_newp(marpaESLIFGrammarp, &marpaWrapperGrammarOption, leveli, descEncodings, descs, descl, defaultSymbolActionp, defaultRuleActionp);
  if (grammarp == NULL) {
    goto err;
  }

  /* First the terminals */
  for (i = 0; i < bootstrap_grammar_terminali; i++) {
    symbolp = _marpaESLIF_symbol_newp(marpaESLIFp);
    if (symbolp == NULL) {
      goto err;
    }

    terminalp = _marpaESLIF_terminal_newp(marpaESLIFp,
					  grammarp,
					  MARPAWRAPPERGRAMMAR_EVENTTYPE_NONE,
                                          NULL, /* descEncodings */
					  NULL, /* descs */
                                          0, /* descl */
					  bootstrap_grammar_terminalp[i].terminalType,
					  bootstrap_grammar_terminalp[i].modifiers,
					  bootstrap_grammar_terminalp[i].utf8s,
					  (bootstrap_grammar_terminalp[i].utf8s != NULL) ? strlen(bootstrap_grammar_terminalp[i].utf8s) : 0,
					  bootstrap_grammar_terminalp[i].testFullMatchs,
					  bootstrap_grammar_terminalp[i].testPartialMatchs
					  );
    if (terminalp == NULL) {
      goto err;
    }
    /* When bootstrapping the grammar, we expect terminal IDs to be exactly the value of the enum */
    if (terminalp->idi != bootstrap_grammar_terminalp[i].idi) {
      MARPAESLIF_ERRORF(marpaESLIFp, "Got symbol ID %d from Marpa while we were expecting %d", terminalp->idi, bootstrap_grammar_terminalp[i].idi);
      goto err;
    }

    symbolp->type        = MARPAESLIF_SYMBOL_TYPE_TERMINAL;
    symbolp->u.terminalp = terminalp;
    symbolp->idi         = terminalp->idi;
    symbolp->descp       = terminalp->descp;
    /* Terminal is now in symbol */
    terminalp = NULL;

    GENERICSTACK_SET_PTR(grammarp->symbolStackp, symbolp, symbolp->idi);
    if (GENERICSTACK_ERROR(grammarp->symbolStackp)) {
      MARPAESLIF_ERRORF(marpaESLIFp, "symbolStackp push failure, %s", strerror(errno));
      goto err;
    }
    /* Push is ok: symbolp is in grammarp->symbolStackp */
    symbolp = NULL;
  }

  /* Then the non-terminals */
  for (i = 0; i < bootstrap_grammar_metai; i++) {
    symbolp = _marpaESLIF_symbol_newp(marpaESLIFp);
    if (symbolp == NULL) {
      goto err;
    }
    metap = _marpaESLIF_meta_newp(marpaESLIFp,
				  grammarp,
				  MARPAWRAPPERGRAMMAR_EVENTTYPE_NONE,
                                  bootstrap_grammar_metap[i].descs,
                                  NULL, /* descEncodings */
				  NULL, /* descs */
				  0 /* descl */
				  );
    if (metap == NULL) {
      goto err;
    }
    /* When bootstrapping the grammar, we expect meta IDs to be exactly the value of the enum */
    if (metap->idi != bootstrap_grammar_metap[i].idi) {
      MARPAESLIF_ERRORF(marpaESLIFp, "Got symbol ID %d from Marpa while we were expecting %d", metap->idi, bootstrap_grammar_metap[i].idi);
      goto err;
    }

    symbolp->type     = MARPAESLIF_SYMBOL_TYPE_META;
    symbolp->startb   = bootstrap_grammar_metap[i].startb;
    symbolp->discardb = bootstrap_grammar_metap[i].discardb;
    symbolp->u.metap  = metap;
    symbolp->idi      = metap->idi;
    symbolp->descp    = metap->descp;
    /* Meta is now in symbol */
    metap = NULL;

    /* Symbol :discard event ? We use only the nulled event for that btw */
    if (bootstrap_grammar_metap[i].discardonb) {
      symbolp->eventNulleds = strdup(":discard[on]");
      if (symbolp->eventNulleds == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "strdup failure, %s", strerror(errno));
        goto err;
      }
      symbolp->eventNulledb = 1;
    }
    if (bootstrap_grammar_metap[i].discardoffb) {
      symbolp->eventNulleds = strdup(":discard[off]");
      if (symbolp->eventNulleds == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "strdup failure, %s", strerror(errno));
        goto err;
      }
      symbolp->eventNulledb = 1;
    }

    GENERICSTACK_SET_PTR(grammarp->symbolStackp, symbolp, symbolp->idi);
    if (GENERICSTACK_ERROR(grammarp->symbolStackp)) {
      MARPAESLIF_ERRORF(marpaESLIFp, "symbolStackp set failure, %s", strerror(errno));
      goto err;
    }
    /* Push is ok: symbolp is in grammarp->symbolStackp */
    symbolp = NULL;

  }

  /* Then the rules - bootstrap action are all external */
  for (i = 0; i < bootstrap_grammar_rulei; i++) {
    ruleAction.u.names = bootstrap_grammar_rulep[i].actions;
    rulep = _marpaESLIF_rule_newp(marpaESLIFp,
				  grammarp,
                                  NULL, /* descEncodings */
                                  bootstrap_grammar_rulep[i].descs,
                                  strlen(bootstrap_grammar_rulep[i].descs),
				  bootstrap_grammar_rulep[i].lhsi,
				  bootstrap_grammar_rulep[i].nrhsl,
				  bootstrap_grammar_rulep[i].rhsip,
				  -1, /* exceptioni */
				  0, /* ranki */
				  0, /* nullRanksHighb */
				  (bootstrap_grammar_rulep[i].type == MARPAESLIF_RULE_TYPE_ALTERNATIVE) ? 0 : 1, /* sequenceb */
				  bootstrap_grammar_rulep[i].minimumi,
				  bootstrap_grammar_rulep[i].separatori,
				  bootstrap_grammar_rulep[i].properb,
                                  (ruleAction.u.names != NULL) ? &ruleAction : NULL,
                                  0, /* passthroughb */
                                  bootstrap_grammar_rulep[i].hideseparatorb,
                                  NULL /* skipbp */
				  );
    if (rulep == NULL) {
      goto err;
    }
    GENERICSTACK_SET_PTR(grammarp->ruleStackp, rulep, rulep->idi);
    if (GENERICSTACK_ERROR(grammarp->ruleStackp)) {
      MARPAESLIF_ERRORF(marpaESLIFp, "ruleStackp set failure, %s", strerror(errno));
      goto err;
    }
    /* Push is ok: rulep is in grammarp->ruleStackp */
    rulep = NULL;
  }

  goto done;
  
 err:
  _marpaESLIF_terminal_freev(terminalp);
  _marpaESLIF_meta_freev(metap);
  _marpaESLIF_rule_freev(rulep);
  _marpaESLIF_symbol_freev(symbolp);
  _marpaESLIF_grammar_freev(grammarp);
  grammarp = NULL;

 done:
  /* MARPAESLIF_TRACEF(marpaESLIFp, funcs, "return %p", grammarp); */
  return grammarp;
}

/*****************************************************************************/
static inline short _marpaESLIFGrammar_validateb(marpaESLIFGrammar_t *marpaESLIFGrammarp)
/*****************************************************************************/
{
  static const char                *funcs                     = "_marpaESLIFGrammar_validateb";
  marpaESLIF_t                     *marpaESLIFp               = marpaESLIFGrammarp->marpaESLIFp;
  genericStack_t                   *grammarStackp             = marpaESLIFGrammarp->grammarStackp;
  marpaWrapperGrammar_t            *marpaWrapperGrammarClonep = NULL;
  marpaESLIF_meta_t                *metap;
  genericStack_t                   *symbolStackp;
  genericStack_t                   *ruleStackp;
  genericStack_t                   *lhsRuleStackp;
  int                               grammari;
  int                               grammarj;
  marpaESLIF_symbol_t              *symbolp;
  marpaESLIF_symbol_t              *subSymbolp;
  int                               symboli;
  marpaESLIF_rule_t                *rulep;
  marpaESLIF_rule_t                *ruletmpp;
  int                               rulei;
  int                               rulej;
  marpaESLIF_grammar_t             *grammarp;
  marpaESLIF_grammar_t             *subgrammarp;
  short                             lhsb;
  marpaESLIF_symbol_t              *lhsp;
  marpaESLIF_symbol_t              *startp;
  marpaESLIF_symbol_t              *discardp;
  marpaESLIF_symbol_t              *exceptionp;
  short                             rcb;
  int                               rhsi;
  size_t                            asciishowl;
  short                             haveLexemeb;
  marpaESLIF_cloneContext_t         marpaESLIF_cloneContext;
  marpaWrapperGrammarCloneOption_t  marpaWrapperGrammarCloneOption;
  marpaWrapperRecognizerOption_t    marpaWrapperRecognizerOption;
  marpaWrapperRecognizer_t         *marpaWrapperRecognizerp;
  size_t                            nSymboll;
  int                              *symbolArrayp;
  size_t                            tmpl;

  marpaESLIF_cloneContext.marpaESLIFp = marpaESLIFp;
  marpaESLIF_cloneContext.grammarp = NULL;

  marpaWrapperGrammarCloneOption.userDatavp = (void *) &marpaESLIF_cloneContext;
  marpaWrapperGrammarCloneOption.grammarOptionSetterp = NULL; /* Changed at run-time see below */
  marpaWrapperGrammarCloneOption.symbolOptionSetterp = NULL; /* Changed at run-time see below */
  marpaWrapperGrammarCloneOption.ruleOptionSetterp = NULL; /* Always NULL */

  marpaWrapperRecognizerOption.genericLoggerp    = marpaESLIFp->marpaESLIFOption.genericLoggerp;
  marpaWrapperRecognizerOption.disableThresholdb = 0;
  marpaWrapperRecognizerOption.exhaustionEventb  = 0;

  /* MARPAESLIF_TRACE(marpaESLIFp, funcs, "Validating ESLIF grammar"); */

  /* The rules are:

   1. There must be a grammar at level 0
   1.b Exceptions are rewriten, i.e.:

       X  = A - B

       is changed to

       X   = A' - B
       A'  = A AOK
       AOK = /(*FAIL)A/+
       event ^AOK = predicted AOK

       plus:

       - event ^AOK is an internal event, never seen by the user
       - any event on A is transfered to A'

   2. Grammar at any level must precompute at its start symbol and its eventual discard symbol
     a. Only one symbol can have the start flag
     b. Only one symbol can have the discard flag
     d. Default symbol action is ::transfer, and default rule action is ::concat
   3. At any grammar level n, if a symbol never appear as an LHS of a rule, then
      it must be an LHS of grammar at level leveli, which must de-factor must also exist.
      Predicted lexemes for pristine recognizers can always be precomputed.
   4. Exception rules:
      - must consist only of lexemes
      - left side of the exception is unique in the whole grammar
      - right side of the exception is unique in the whole grammar
      - both sides must must not have sub-lexemes

      Note that these constraints makes the russel paradox impossible, because they apply to
      any grammar at any level.

   5. For every rule that is a passthrough, then it is illegal to have its lhs appearing as an lhs is any other rule
   6. The semantic of a nullable LHS must be unique
   7. lexeme and terminal events are meaningul only on lexemes or terminals
   8. Grammar names must all be different
   9. :discard events are possible only if the RHS of the :discard rule is not a lexeme
  10. Precompile lua script if needed

      It is not illegal to have sparse items in grammarStackp.

      The end of this routine is filling grammar information.

      =================================================================================
      Note that when we are extending a grammar, the same marpaESLIFGrammarp is reused.
      For this reason, all pointers that are the result of an explicit or an implicit
      malloc are explicitely checked before being writen.
      =================================================================================

  */

  /*
   1. There must be a grammar at level 0
  */
  if (! GENERICSTACK_IS_PTR(grammarStackp, 0)) {
    MARPAESLIF_ERROR(marpaESLIFp, "No top-level grammar");
    goto err;
  }
  grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, 0);
  /*
   2. Grammar at level 0 must precompute at its start symbol, grammar at level n at its eventual discard symbol
     a. Only one symbol can have the start flag
     b. Only one symbol can have the discard flag
  */
  /* Pre-scan all grammars to set the topb attribute of every symbol */
  for (grammari = 0; grammari < GENERICSTACK_USED(grammarStackp); grammari++) {
    if (! GENERICSTACK_IS_PTR(grammarStackp, grammari)) {
      /* Sparse item in grammarStackp -; */
      continue;
    }
    grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, grammari);
    symbolStackp = grammarp->symbolStackp;
    ruleStackp = grammarp->ruleStackp;
    for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
      MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);
      symbolp->topb = 1;
      for (rulei = 0; rulei < GENERICSTACK_USED(ruleStackp); rulei++) {
        MARPAESLIF_INTERNAL_GET_RULE_FROM_STACK(marpaESLIFp, rulep, ruleStackp, rulei);
        for (rhsi = 0; rhsi < GENERICSTACK_USED(rulep->rhsStackp); rhsi++) {
          if (! GENERICSTACK_IS_PTR(rulep->rhsStackp, rhsi)) {
            continue;
          }
          if (GENERICSTACK_GET_PTR(rulep->rhsStackp, rhsi) == (void *) symbolp) {
            symbolp->topb = 0;
            break;
          }
        }
        if (! symbolp->topb) {
          break;
        }
      }
    }
  }

  for (grammari = 0; grammari < GENERICSTACK_USED(grammarStackp); grammari++) {
    if (! GENERICSTACK_IS_PTR(grammarStackp, grammari)) {
      /* Sparse item in grammarStackp -; */
      continue;
    }
    grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, grammari);
    symbolStackp = grammarp->symbolStackp;
    ruleStackp = grammarp->ruleStackp;

    if (grammarp->defaultSymbolActionp == NULL) {
      grammarp->defaultSymbolActionp = (marpaESLIF_action_t *) malloc(sizeof(marpaESLIF_action_t));
      if (grammarp->defaultSymbolActionp == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
        goto err;
      }
      grammarp->defaultSymbolActionp->type    = MARPAESLIF_ACTION_TYPE_NAME;
      grammarp->defaultSymbolActionp->u.names = strdup("::transfer");
      if (grammarp->defaultSymbolActionp->u.names == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "strdup failure, %s", strerror(errno));
        goto err;
      }
    }

    if (grammarp->defaultRuleActionp == NULL) {
      grammarp->defaultRuleActionp = (marpaESLIF_action_t *) malloc(sizeof(marpaESLIF_action_t));
      if (grammarp->defaultRuleActionp == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
        goto err;
      }
      grammarp->defaultRuleActionp->type    = MARPAESLIF_ACTION_TYPE_NAME;
      grammarp->defaultRuleActionp->u.names = strdup("::concat");
      if (grammarp->defaultRuleActionp->u.names == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "strdup failure, %s", strerror(errno));
        goto err;
      }
    }

    /* :start meta symbol check */
    startp = NULL;
    for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
      MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);
      if (symbolp->startb) {
        if (startp == NULL) {
          startp = symbolp;
        } else {
          MARPAESLIF_ERRORF(marpaESLIFp, "More than one :start symbol at grammar level %d (%s): symbols %d <%s> and %d <%s>", grammari, grammarp->descp->asciis, startp->idi, startp->descp->asciis, symbolp->idi, symbolp->descp->asciis);
          goto err;
        }
      }
    }
    /* Before precomputing we have to clone. Why ? This is because the bootstrap is changing symbols event behaviours after creating them. */
    /* But Marpa does not know about it. */
    marpaESLIF_cloneContext.grammarp = grammarp;
    marpaWrapperGrammarCloneOption.grammarOptionSetterp = NULL;
    marpaWrapperGrammarCloneOption.symbolOptionSetterp = _marpaESLIFGrammar_symbolOptionSetterInitb;
    marpaWrapperGrammarClonep = marpaWrapperGrammar_clonep(grammarp->marpaWrapperGrammarStartp, &marpaWrapperGrammarCloneOption);
    if (marpaWrapperGrammarClonep == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Grammar level %d (%s): cloning failure", grammari, grammarp->descp->asciis);
        goto err;
    }
    if (grammarp->marpaWrapperGrammarStartp != NULL) {
      marpaWrapperGrammar_freev(grammarp->marpaWrapperGrammarStartp);
    }
    grammarp->marpaWrapperGrammarStartp = marpaWrapperGrammarClonep;
    marpaWrapperGrammarClonep = NULL;

    /* Same but with no event */
    marpaWrapperGrammarCloneOption.grammarOptionSetterp = NULL;
    marpaWrapperGrammarCloneOption.symbolOptionSetterp = _marpaESLIFGrammar_symbolOptionSetterInternalb; /* No event but internal :discard[on/off/switch] */
    marpaWrapperGrammarClonep = marpaWrapperGrammar_clonep(grammarp->marpaWrapperGrammarStartp, &marpaWrapperGrammarCloneOption);
    if (marpaWrapperGrammarClonep == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Grammar level %d (%s): cloning failure", grammari, grammarp->descp->asciis);
        goto err;
    }
    if (grammarp->marpaWrapperGrammarStartNoEventp != NULL) {
      marpaWrapperGrammar_freev(grammarp->marpaWrapperGrammarStartNoEventp);
    }
    grammarp->marpaWrapperGrammarStartNoEventp = marpaWrapperGrammarClonep;
    marpaWrapperGrammarClonep = NULL;

    if (startp == NULL) {
      /* Use the first rule */
      rulep = NULL;
      for (rulei = 0; rulei < GENERICSTACK_USED(ruleStackp); rulei++) {
        MARPAESLIF_INTERNAL_GET_RULE_FROM_STACK(marpaESLIFp, rulep, ruleStackp, rulei);
        /* Take care! discard is an internal rule that should never be the start symbol... */
        if (rulep->lhsp->discardb) {
          rulep = NULL;
          continue;
        }
        break;
      }
      if (rulep == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Precomputing grammar level %d (%s) is impossible: no rule", grammari, grammarp->descp->asciis);
        goto err;
      }
      MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Precomputing grammar level %d (%s) at its default start symbol %d <%s>", grammari, grammarp->descp->asciis, rulep->lhsp->idi, rulep->lhsp->descp->asciis);
      if (! marpaWrapperGrammar_precompute_startb(grammarp->marpaWrapperGrammarStartp, rulep->lhsp->idi)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Precomputing grammar level %d (%s) at its default start symbol %d <%s> failure", grammari, grammarp->descp->asciis, rulep->lhsp->idi, rulep->lhsp->descp->asciis);
        goto err;
      }
      MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Precomputing \"no event\" grammar level %d (%s) at its default start symbol %d <%s>", grammari, grammarp->descp->asciis, rulep->lhsp->idi, rulep->lhsp->descp->asciis);
      if (! marpaWrapperGrammar_precompute_startb(grammarp->marpaWrapperGrammarStartNoEventp, rulep->lhsp->idi)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Precomputing \"no event\" grammar level %d (%s) at its default start symbol %d <%s> failure", grammari, grammarp->descp->asciis, rulep->lhsp->idi, rulep->lhsp->descp->asciis);
        goto err;
      }
      grammarp->starti = rulep->lhsp->idi;
      grammarp->starts = rulep->lhsp->descp->asciis;
    } else {
      MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Precomputing grammar level %d (%s) at start symbol %d <%s>", grammari, grammarp->descp->asciis, startp->idi, startp->descp->asciis);
      if (! marpaWrapperGrammar_precompute_startb(grammarp->marpaWrapperGrammarStartp, startp->idi)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Precomputing grammar level %d (%s) at start symbol %d <%s> failure", grammari, grammarp->descp->asciis, startp->idi, startp->descp->asciis);
        goto err;
      }
      MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Precomputing \"no event\" grammar level %d (%s) at start symbol %d <%s>", grammari, grammarp->descp->asciis, startp->idi, startp->descp->asciis);
      if (! marpaWrapperGrammar_precompute_startb(grammarp->marpaWrapperGrammarStartNoEventp, startp->idi)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Precomputing \"no event\" grammar level %d (%s) at start symbol %d <%s> failure", grammari, grammarp->descp->asciis, startp->idi, startp->descp->asciis);
        goto err;
      }
      grammarp->starti = startp->idi;
      grammarp->starts = startp->descp->asciis;
    }

    MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Getting start first lexemes in grammar level %d (%s)", grammari, grammarp->descp->asciis);
    marpaWrapperRecognizerp = marpaWrapperRecognizer_newp(grammarp->marpaWrapperGrammarStartp, &marpaWrapperRecognizerOption);
    if (marpaWrapperRecognizerp == NULL) {
      goto err;
    }
    if (! marpaWrapperRecognizer_expectedb(marpaWrapperRecognizerp, &nSymboll, &symbolArrayp)) {
      marpaWrapperRecognizer_freev(marpaWrapperRecognizerp);
      goto err;
    }
    if ((nSymboll > 0) && (symbolArrayp != NULL)) {
      tmpl = nSymboll * sizeof(int);
      grammarp->symbolArrayStartp = (int *) malloc(tmpl);
      if (grammarp->symbolArrayStartp == NULL) {
	MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
	marpaWrapperRecognizer_freev(marpaWrapperRecognizerp);
	goto err;
      }
      grammarp->nSymbolStartl = nSymboll;
      memcpy(grammarp->symbolArrayStartp, symbolArrayp, tmpl);
      marpaWrapperRecognizer_freev(marpaWrapperRecognizerp);
    }

    /* :discard meta symbol check */
    discardp = NULL;
    for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
      MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);
      if (symbolp->discardb) {
        if (discardp == NULL) {
          discardp = symbolp;
        } else {
          MARPAESLIF_ERRORF(marpaESLIFp, "More than one :discard symbol at grammar level %d (%s): symbols %d <%s> and %d <%s>", grammari, grammarp->descp->asciis, discardp->idi, discardp->descp->asciis, symbolp->idi, symbolp->descp->asciis);
          goto err;
        }
      }
    }

    if (discardp != NULL) {
      /* The :discard symbol itself never have any event */
      if ((discardp->eventBefores    != NULL) ||
          (discardp->eventAfters     != NULL) ||
          (discardp->eventPredicteds != NULL) ||
          (discardp->eventNulleds    != NULL) ||
          (discardp->eventCompleteds != NULL) ||
          (discardp->discardEvents   != NULL)) {
        MARPAESLIF_ERRORF(marpaESLIFp, ":discard symbol at grammar level %d (%s) must have no event", grammari, grammarp->descp->asciis);
        goto err;
      }
      /* Per def a :discard rule has only one RHS, we mark its discardRhsb flag and copy the rule's discard settings */
      /* (Note that saying :discard :[x]:= RHS event => EVENT twice will overwrite first setting) */
      for (rulei = 0; rulei < GENERICSTACK_USED(ruleStackp); rulei++) {
        MARPAESLIF_INTERNAL_GET_RULE_FROM_STACK(marpaESLIFp, rulep, ruleStackp, rulei);
        if (rulep->lhsp != discardp) {
          continue;
        }
        if (GENERICSTACK_USED(rulep->rhsStackp) != 1) {
          MARPAESLIF_ERRORF(marpaESLIFp, "Looking at grammar level %d (%s) and discard symbol %d <%s>: a :discard rule must have exactly one RHS", grammari, grammarp->descp->asciis, discardp->idi, discardp->descp->asciis);
          goto err;
        }
        symbolp = (marpaESLIF_symbol_t *) GENERICSTACK_GET_PTR(rulep->rhsStackp, 0);
        symbolp->discardRhsb = 1;
        symbolp->discardEvents = rulep->discardEvents;
        symbolp->discardEventb = rulep->discardEventb;
      }
      MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Precomputing grammar level %d (%s) at discard symbol %d <%s>", grammari, grammarp->descp->asciis, discardp->idi, discardp->descp->asciis);
      marpaESLIF_cloneContext.grammarp = grammarp;
      /* Clone for the discard mode at grammar level */
      marpaWrapperGrammarCloneOption.grammarOptionSetterp = _marpaESLIFGrammar_grammarOptionSetterNoLoggerb;
      marpaWrapperGrammarCloneOption.symbolOptionSetterp  = _marpaESLIFGrammar_symbolOptionSetterDiscardb; /* No event but internal discard completion */
      marpaWrapperGrammarClonep = marpaWrapperGrammar_clonep(grammarp->marpaWrapperGrammarStartp, &marpaWrapperGrammarCloneOption);
      if (marpaWrapperGrammarClonep == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Grammar level %d (%s) at discard symbol %d <%s>: cloning failure", grammari, grammarp->descp->asciis, discardp->idi, discardp->descp->asciis);
        goto err;
      }
      if (! marpaWrapperGrammar_precompute_startb(marpaWrapperGrammarClonep, discardp->idi)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Precomputing grammar level %d (%s) at discard symbol %d <%s> failure", grammari, grammarp->descp->asciis, discardp->idi, discardp->descp->asciis);
        goto err;
      }
      if (grammarp->marpaWrapperGrammarDiscardp != NULL) {
        marpaWrapperGrammar_freev(grammarp->marpaWrapperGrammarDiscardp);
      }      
      grammarp->marpaWrapperGrammarDiscardp = marpaWrapperGrammarClonep;
      marpaWrapperGrammarClonep = NULL;
      /* Same but with no event */
      marpaWrapperGrammarCloneOption.grammarOptionSetterp = _marpaESLIFGrammar_grammarOptionSetterNoLoggerb;
      marpaWrapperGrammarCloneOption.symbolOptionSetterp  = _marpaESLIFGrammar_symbolOptionSetterInternalNoeventb;
      marpaWrapperGrammarClonep = marpaWrapperGrammar_clonep(grammarp->marpaWrapperGrammarStartp, &marpaWrapperGrammarCloneOption);
      if (marpaWrapperGrammarClonep == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Grammar level %d (%s) at discard symbol %d <%s>: cloning failure", grammari, grammarp->descp->asciis, discardp->idi, discardp->descp->asciis);
        goto err;
      }
      if (! marpaWrapperGrammar_precompute_startb(marpaWrapperGrammarClonep, discardp->idi)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Precomputing grammar level %d (%s) at discard symbol %d <%s> failure", grammari, grammarp->descp->asciis, discardp->idi, discardp->descp->asciis);
        goto err;
      }
      if (grammarp->marpaWrapperGrammarDiscardNoEventp != NULL) {
        marpaWrapperGrammar_freev(grammarp->marpaWrapperGrammarDiscardNoEventp);
      }      
      grammarp->marpaWrapperGrammarDiscardNoEventp = marpaWrapperGrammarClonep;
      marpaWrapperGrammarClonep = NULL;

      grammarp->discardp = discardp;
      grammarp->discardi = discardp->idi;

      MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Getting discard first lexemes in grammar level %d (%s)", grammari, grammarp->descp->asciis);
      marpaWrapperRecognizerp = marpaWrapperRecognizer_newp(grammarp->marpaWrapperGrammarDiscardp, &marpaWrapperRecognizerOption);
      if (marpaWrapperRecognizerp == NULL) {
	goto err;
      }
      if (! marpaWrapperRecognizer_expectedb(marpaWrapperRecognizerp, &nSymboll, &symbolArrayp)) {
	marpaWrapperRecognizer_freev(marpaWrapperRecognizerp);
	goto err;
      }
      if ((nSymboll > 0) && (symbolArrayp != NULL)) {
	tmpl = nSymboll * sizeof(int);
	grammarp->symbolArrayDiscardp = (int *) malloc(tmpl);
	if (grammarp->symbolArrayDiscardp == NULL) {
	  MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
	  marpaWrapperRecognizer_freev(marpaWrapperRecognizerp);
	  goto err;
	}
	grammarp->nSymbolDiscardl = nSymboll;
	memcpy(grammarp->symbolArrayDiscardp, symbolArrayp, tmpl);
	marpaWrapperRecognizer_freev(marpaWrapperRecognizerp);
      }
    }
  }
  
  /*
    3. In any rule of any grammar, an RHS can be at any level as well. Default being the current one.
    When the RHS level is the current level, if this RHS never appear as an LHS of another rule at the
    same level, then it must be an LHS of grammar at a resolved level, which must de-factor must also exist.
    
    Therefore every grammar is first scanned to detect all symbols that are truely LHS's at this level.
    Then every RHS of every rule is verified: it is must be an LHS at its specified grammar level. When found,
    This resolved grammar is precomputed at this found LHS and the result is attached to the symbol of the
    parent grammar.
  */
  for (grammari = 0; grammari < GENERICSTACK_USED(grammarStackp); grammari++) {
    if (! GENERICSTACK_IS_PTR(grammarStackp, grammari)) {
      /* Sparse item in grammarStackp -; */
      continue;
    }
    grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, grammari);
    MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Looking at symbols in grammar level %d (%s)", grammari, grammarp->descp->asciis);

    /* Loop on symbols */
    symbolStackp = grammarp->symbolStackp;
    ruleStackp = grammarp->ruleStackp;
    for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
      MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);
      /* Only meta symbols should be looked at: if not an LHS then it is a dependency on a LHS of another grammar */
      if (symbolp->type != MARPAESLIF_SYMBOL_TYPE_META) {
        continue;
      }

      lhsb = 0;
      lhsRuleStackp = symbolp->lhsRuleStackp;
      for (rulei = 0; rulei < GENERICSTACK_USED(ruleStackp); rulei++) {
        MARPAESLIF_INTERNAL_GET_RULE_FROM_STACK(marpaESLIFp, rulep, ruleStackp, rulei);
        lhsp = rulep->lhsp;
        if (_marpaESLIF_string_utf8_eqb(lhsp->descp, symbolp->descp)) {
          /* Found */
          MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Grammar level %d (%s): symbol %d (%s) marked as LHS", grammari, grammarp->descp->asciis, lhsp->idi, lhsp->descp->asciis);
          lhsb = 1;
          GENERICSTACK_PUSH_PTR(lhsRuleStackp, rulep);
          if (GENERICSTACK_ERROR(lhsRuleStackp)) {
            MARPAESLIF_ERRORF(marpaESLIFp, "lhsRuleStackp push failure, %s", strerror(errno));
            goto err;
          }
        }
      }
      symbolp->lhsb = lhsb;
    }
  }

  /* From grammar point of view, an expected symbol will always be either symbols explicitely created as terminals,
     either symbols not being an LHS. Per definition symbols created as terminals cannot be LHS symbols: precomputing
     the grammar will automatically fail. This is made sure by always precomputing at least grammar at level 0, and
     by precomputing any needed grammar at any other level with an alternative starting symbol.
  */
  for (grammari = 0; grammari < GENERICSTACK_USED(grammarStackp); grammari++) {

    if (! GENERICSTACK_IS_PTR(grammarStackp, grammari)) {
      continue;
    }
    grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, grammari);
    MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Looking at rules in grammar level %d (%s)", grammari, grammarp->descp->asciis);

    symbolStackp = grammarp->symbolStackp;
    for (symboli = 0; symboli <= GENERICSTACK_USED(symbolStackp); symboli++) {
      if (symboli < GENERICSTACK_USED(symbolStackp)) {
        MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);
      } else {
        /* Faked additional entry */
        if (rulep->separatorp == NULL) {
          break;
        } else {
          symbolp = rulep->separatorp;
        }
      }
      /* Only non LHS meta symbols should be looked at */
      if ((symbolp->type != MARPAESLIF_SYMBOL_TYPE_META) || symbolp->lhsb) {
        /* This is always resolved in the same grammar */
        symbolp->lookupResolvedLeveli = grammarp->leveli;
        continue;
      }
      metap = symbolp->u.metap;
      /* Since we loop on symbols of every rule, it can very well happen that we hit */
      /* the same meta symbol more than once.                                        */
      if ((metap->marpaWrapperGrammarLexemeClonep != NULL)) {
        MARPAESLIF_TRACEF(marpaESLIFp, funcs, "... Grammar level %d (%s): symbol %d (%s) already processed", grammari, grammarp->descp->asciis, symbolp->idi, symbolp->descp->asciis);
        continue;
      }

      /* Resolve the symbol */
      if (symbolp->lookupMetas == NULL) {
        /* If lookup name is not forced, then it must have the same name in the lookedup grammar */
        symbolp->lookupMetas = symbolp->u.metap->asciinames;
      }
      MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Grammar level %d (%s): symbol %d <%s> must be symbol <%s> in grammar level %d", grammari, grammarp->descp->asciis, symbolp->idi, symbolp->descp->asciis, symbolp->lookupMetas, grammarp->leveli + symbolp->lookupLevelDeltai);
      subSymbolp = _marpaESLIF_resolveSymbolp(marpaESLIFp, grammarStackp, grammarp, symbolp->lookupMetas, symbolp->lookupLevelDeltai, NULL, &subgrammarp);
      if (subSymbolp == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Looking at rules in grammar level %d (%s): symbol %d (%s) must be resolved as <%s> in grammar at level %d", grammari, grammarp->descp->asciis, symbolp->idi, symbolp->descp->asciis, symbolp->lookupMetas, grammarp->leveli + symbolp->lookupLevelDeltai);
        goto err;
      }

      if (subSymbolp == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Looking at rules in grammar level %d (%s): symbol %d (%s) is referencing a non-existing symbol at grammar level %d (%s)", grammari, grammarp->descp->asciis, symbolp->idi, symbolp->descp->asciis, subgrammarp->leveli, subgrammarp->descp->asciis);
        goto err;
      }
      if (! subSymbolp->lhsb) {
        /* When sub grammar is current grammar, this mean that we require that this RHS is also an LHS - which is correct because we restricted symbol loop on meta symbols */
        MARPAESLIF_ERRORF(marpaESLIFp, "Looking at rules in grammar level %d (%s): symbol %d <%s> is referencing existing symbol %d <%s> at grammar level %d (%s) but it is not an LHS", grammari, grammarp->descp->asciis, symbolp->idi, symbolp->descp->asciis, subSymbolp->idi, subSymbolp->descp->asciis, subgrammarp->leveli, subgrammarp->descp->asciis);
        goto err;
      }
      
      /* Clone for the symbol in lexeme mode: no event except when this is on the left side of an exception character '-' */
      marpaESLIF_cloneContext.grammarp = subgrammarp;
      marpaWrapperGrammarCloneOption.grammarOptionSetterp = NULL; /* _marpaESLIFGrammar_grammarOptionSetterNoLoggerb; */
      marpaWrapperGrammarCloneOption.symbolOptionSetterp  = _marpaESLIFGrammar_symbolOptionSetterInternalb;
      marpaWrapperGrammarClonep = marpaWrapperGrammar_clonep(subgrammarp->marpaWrapperGrammarStartp, &marpaWrapperGrammarCloneOption);
      if (marpaWrapperGrammarClonep == NULL) {
        goto err;
      }
      if (! marpaWrapperGrammar_precompute_startb(marpaWrapperGrammarClonep, subSymbolp->idi)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Precomputing grammar level %d (%s) at symbol %d <%s> failure", subgrammarp->leveli, subgrammarp->descp->asciis, subSymbolp->idi, subSymbolp->descp->asciis);
        goto err;
      }

      MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Getting start first lexemes in grammar level %d (%s) at symbol %d <%s> failure", subgrammarp->leveli, subgrammarp->descp->asciis, subSymbolp->idi, subSymbolp->descp->asciis);
      marpaWrapperRecognizerp = marpaWrapperRecognizer_newp(marpaWrapperGrammarClonep, &marpaWrapperRecognizerOption);
      if (marpaWrapperRecognizerp == NULL) {
	goto err;
      }
      if (! marpaWrapperRecognizer_expectedb(marpaWrapperRecognizerp, &nSymboll, &symbolArrayp)) {
	marpaWrapperRecognizer_freev(marpaWrapperRecognizerp);
	goto err;
      }
      if ((nSymboll > 0) && (symbolArrayp != NULL)) {
	tmpl = nSymboll * sizeof(int);
	metap->symbolArrayStartp = (int *) malloc(tmpl);
	if (metap->symbolArrayStartp == NULL) {
	  MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
	  marpaWrapperRecognizer_freev(marpaWrapperRecognizerp);
	  goto err;
	}
	metap->nSymbolStartl = nSymboll;
	memcpy(metap->symbolArrayStartp, symbolArrayp, tmpl);
	marpaWrapperRecognizer_freev(marpaWrapperRecognizerp);
      }

      if (metap->marpaWrapperGrammarLexemeClonep != NULL) {
        marpaWrapperGrammar_freev(metap->marpaWrapperGrammarLexemeClonep);
      }
      metap->marpaWrapperGrammarLexemeClonep = marpaWrapperGrammarClonep;
      metap->lexemeIdi                       = subSymbolp->idi;
      /* lexeme matches all goes through the _marpaESLIFRecognizer_meta_matcherb() method, that creates internally */
      /* a recognizer. Recognizers allocation is quite optimized using a hash of pristine recognizers, but in any case */
      /* whereas the final recognizers comes from a hash or a malloc, there is a marpaESLIFGrammarp in it that have */
      /* a big cost if it is on the stack and not in fake mode. */
      /* So we arrange for this grammar to be on the heap instead: */
      metap->_grammar                                  = *subgrammarp;
      metap->_grammar.marpaWrapperGrammarStartNoEventp = marpaWrapperGrammarClonep;
      metap->_grammar.starti                           = metap->lexemeIdi;
      metap->_grammar.nSymbolStartl                    = metap->nSymbolStartl;
      metap->_grammar.symbolArrayStartp                = metap->symbolArrayStartp;
      /* This is a safe measure, although this will NEVER be used: when trying to match a meta symbol, discard never happens */
      metap->_grammar.nSymbolDiscardl                  = 0;
      metap->_grammar.symbolArrayDiscardp              = NULL;
      metap->_marpaESLIFGrammarLexemeClone             = *marpaESLIFGrammarp;
      metap->_marpaESLIFGrammarLexemeClone.grammarp    = &(metap->_grammar);
      metap->marpaESLIFGrammarLexemeClonep             = &(metap->_marpaESLIFGrammarLexemeClone);

      marpaWrapperGrammarClonep = NULL;

      /* Commit resolved level in symbol */
      symbolp->lookupResolvedLeveli = subgrammarp->leveli;
      /* Makes sure this RHS is an LHS in the sub grammar, ignoring the case where sub grammar would be current grammar */
      if (subgrammarp == grammarp) {
        continue;
      }

      MARPAESLIF_TRACEF(marpaESLIFp,  funcs, "... Grammar level %d (%s): symbol %d (%s) have grammar resolved level set to   %d", grammari, grammarp->descp->asciis, symbolp->idi, symbolp->descp->asciis, symbolp->lookupResolvedLeveli);
    }
  }

  /*
   4. Exception rules:
      - must consist only of lexemes
      - left side of the exception is unique in the whole grammar
      - right side of the exception is unique in the whole grammar
      - both sides must must not have any sub lexeme

      Note that these constraints makes the russel paradox impossible, because they apply to
      any grammar at any level.

  */
  for (grammari = 0; grammari < GENERICSTACK_USED(grammarStackp); grammari++) {
    if (! GENERICSTACK_IS_PTR(grammarStackp, grammari)) {
      /* Sparse item in grammarStackp -; */
      continue;
    }
    grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, grammari);
    ruleStackp = grammarp->ruleStackp;
    for (rulei = 0; rulei < GENERICSTACK_USED(ruleStackp); rulei++) {
      MARPAESLIF_INTERNAL_GET_RULE_FROM_STACK(marpaESLIFp, rulep, ruleStackp, rulei);
      exceptionp = rulep->exceptionp;
      if (exceptionp == NULL) {
        continue;
      }
      symbolp = (marpaESLIF_symbol_t *) GENERICSTACK_GET_PTR(rulep->rhsStackp, 0);
      /* -------------------------- */
      /* Both sides must be lexemes */
      /* -------------------------- */
      if (! MARPAESLIF_IS_LEXEME(symbolp)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "At grammar level %d (%s), symbol %d <%s> is on the left side of an exception and must be a lexeme", grammari, grammarp->descp->asciis, symbolp->idi, symbolp->descp->asciis);
        goto err;
      }
      if (! MARPAESLIF_IS_LEXEME(exceptionp)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "At grammar level %d (%s), symbol %d <%s> is on the right side of an exception and must be a lexeme", grammari, grammarp->descp->asciis, exceptionp->idi, exceptionp->descp->asciis);
        goto err;
      }
      /* --------------------------------------------------------- */
      /* left side of the exception is unique in the whole grammar */
      /* --------------------------------------------------------- */
      for (rulej = 0; rulej < GENERICSTACK_USED(ruleStackp); rulej++) {
        if (rulei == rulej) {
          continue;
        }
        MARPAESLIF_INTERNAL_GET_RULE_FROM_STACK(marpaESLIFp, ruletmpp, ruleStackp, rulej);
        for (rhsi = 0; rhsi < GENERICSTACK_USED(ruletmpp->rhsStackp); rhsi++) {
          if (! GENERICSTACK_IS_PTR(ruletmpp->rhsStackp, rhsi)) {
            continue;
          }
          if (GENERICSTACK_GET_PTR(ruletmpp->rhsStackp, rhsi) == (void *) symbolp) {
            MARPAESLIF_ERRORF(marpaESLIFp, "At grammar level %d (%s), symbol %d <%s> is on the left side of an exception: it must be a lexeme that does not appear anywhere else in the grammar, because the exception is considered as being part of the lexeme definition", grammari, grammarp->descp->asciis, symbolp->idi, symbolp->descp->asciis);
            goto err;
          }
        }
      }
      /* ---------------------------------------------------------- */
      /* right side of the exception is unique in the whole grammar */
      /* ---------------------------------------------------------- */
      for (rulej = 0; rulej < GENERICSTACK_USED(ruleStackp); rulej++) {
        if (rulei == rulej) {
          continue;
        }
        MARPAESLIF_INTERNAL_GET_RULE_FROM_STACK(marpaESLIFp, ruletmpp, ruleStackp, rulej);
        for (rhsi = 0; rhsi < GENERICSTACK_USED(ruletmpp->rhsStackp); rhsi++) {
          if (! GENERICSTACK_IS_PTR(ruletmpp->rhsStackp, rhsi)) {
            continue;
          }
          if (GENERICSTACK_GET_PTR(ruletmpp->rhsStackp, rhsi) == (void *) exceptionp) {
            MARPAESLIF_ERRORF(marpaESLIFp, "At grammar level %d (%s), symbol %d <%s> is on the right side of an exception: it must be a lexeme that does not appear anywhere else in the grammar, because the exception is considered as being part of the lexeme definition", grammari, grammarp->descp->asciis, exceptionp->idi, exceptionp->descp->asciis);
            goto err;
          }
        }
      }
      /* -------------------------------------------- */
      /* both sides must must not have any sub lexeme */
      /* -------------------------------------------- */
      /* They are lexemes, so per def metap->marpaWrapperGrammarLexemeClonep is not NULL */
      if (! _marpaESLIFGrammar_haveLexemeb(marpaESLIFGrammarp, grammarp->leveli + symbolp->lookupLevelDeltai, symbolp->u.metap->marpaWrapperGrammarLexemeClonep, &haveLexemeb)) {
        goto err;
      }
      if (haveLexemeb) {
        MARPAESLIF_ERRORF(marpaESLIFp, "At grammar level %d (%s), symbol %d <%s> is on the left side of an exception: it must have no sub-lexeme", grammari, grammarp->descp->asciis, symbolp->idi, symbolp->descp->asciis);
        goto err;
      }
      if (! _marpaESLIFGrammar_haveLexemeb(marpaESLIFGrammarp, grammarp->leveli + exceptionp->lookupLevelDeltai, exceptionp->u.metap->marpaWrapperGrammarLexemeClonep, &haveLexemeb)) {
        goto err;
      }
      if (haveLexemeb) {
        MARPAESLIF_ERRORF(marpaESLIFp, "At grammar level %d (%s), symbol %d <%s> is on the left side of an exception: it must have no sub-lexeme", grammari, grammarp->descp->asciis, exceptionp->idi, exceptionp->descp->asciis);
        goto err;
      }

      /* Remember that this RHS is the member of an exception */
      symbolp->exceptionp = exceptionp;
    }
  }

  /*
   5. For every rule that is a passthrough, then it is illegal to have its lhs appearing as an lhs is any other rule
  */
  for (grammari = 0; grammari < GENERICSTACK_USED(grammarStackp); grammari++) {
    if (! GENERICSTACK_IS_PTR(grammarStackp, grammari)) {
      /* Sparse item in grammarStackp -; */
      continue;
    }
    grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, grammari);
    MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Looking at passthroughs in grammar level %d (%s)", grammari, grammarp->descp->asciis);

    /* Loop on rules */
    ruleStackp = grammarp->ruleStackp;
    for (rulei = 0; rulei < GENERICSTACK_USED(ruleStackp); rulei++) {
      MARPAESLIF_INTERNAL_GET_RULE_FROM_STACK(marpaESLIFp, rulep, ruleStackp, rulei);
      if (rulep->passthroughb) {
        for (rulej = 0; rulej < GENERICSTACK_USED(ruleStackp); rulej++) {
          if (rulei == rulej) {
            continue;
          }
          MARPAESLIF_INTERNAL_GET_RULE_FROM_STACK(marpaESLIFp, ruletmpp, ruleStackp, rulej);
          if (rulep->lhsp == ruletmpp->lhsp) {
            MARPAESLIF_ERRORF(marpaESLIFp, "Looking at rules in grammar level %d (%s): symbol %d (%s) is an LHS of a prioritized rule and cannot be appear as an LHS is any other rule", grammari, grammarp->descp->asciis, rulep->lhsp->idi, rulep->lhsp->descp->asciis);
            goto err;
          }
        }
      }
    }
  }

  /*
   6. The semantic of a nullable LHS must be unique
  */
  for (grammari = 0; grammari < GENERICSTACK_USED(grammarStackp); grammari++) {
    if (! GENERICSTACK_IS_PTR(grammarStackp, grammari)) {
      continue;
    }
    grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, grammari);
    MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Checking nullable LHS semantic in grammar level %d (%s)", grammarp->leveli, grammarp->descp->asciis);
    /* First we collect the nullable rule Ids by LHS Id */
    ruleStackp = grammarp->ruleStackp;
    for (rulei = 0; rulei < GENERICSTACK_USED(ruleStackp); rulei++) {
      MARPAESLIF_INTERNAL_GET_RULE_FROM_STACK(marpaESLIFp, rulep, ruleStackp, rulei);
      if (! marpaWrapperGrammar_rulePropertyb(grammarp->marpaWrapperGrammarStartp, rulep->idi, &(rulep->propertyBitSet))) {
        MARPAESLIF_ERRORF(marpaESLIFp, "marpaWrapperGrammar_rulePropertyb failure for grammar level %d (%s)", grammarp->leveli, grammarp->descp->asciis);
        goto err;
      }
      if ((rulep->propertyBitSet & MARPAWRAPPER_RULE_IS_NULLABLE) == MARPAWRAPPER_RULE_IS_NULLABLE) {
        GENERICSTACK_PUSH_PTR(rulep->lhsp->nullableRuleStackp, rulep);
        if (GENERICSTACK_ERROR(rulep->lhsp->nullableRuleStackp)) {
          MARPAESLIF_ERRORF(marpaESLIFp, "rulep->lhsp->nullableRuleStackp push failure, %s", strerror(errno));
          goto err;
        }
      }
    }

    /* Then we determine the nullable semantic */
    symbolStackp = grammarp->symbolStackp;
    for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
      MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);
      /* Always fetch properties and events - this is used in the grammar show */
      if (! marpaWrapperGrammar_symbolPropertyb(grammarp->marpaWrapperGrammarStartp, symbolp->idi, &(symbolp->propertyBitSet))) {
        goto err;
      }
      if (! marpaWrapperGrammar_symbolEventb(grammarp->marpaWrapperGrammarStartp, symbolp->idi, &(symbolp->eventBitSet))) {
        goto err;
      }
      if (GENERICSTACK_USED(symbolp->nullableRuleStackp) <= 0) {
        continue;
      }
      if (GENERICSTACK_USED(symbolp->nullableRuleStackp) == 1) {
        /* Just one nullable rule: nullable semantic is this rule's semantic */
        if (! GENERICSTACK_IS_PTR(symbolp->nullableRuleStackp, 0)) {
          /* Impossible */
          MARPAESLIF_ERRORF(marpaESLIFp, "symbolp->nullableRuleStackp at indice 0 is not PTR (got %s, value %d)", _marpaESLIF_genericStack_i_types(symbolp->nullableRuleStackp, 0), GENERICSTACKITEMTYPE(symbolp->nullableRuleStackp, 0));
          goto err;
        }
        rulep = (marpaESLIF_rule_t *) GENERICSTACK_GET_PTR(symbolp->nullableRuleStackp, 0);
        symbolp->nullableActionp = rulep->actionp;
#ifndef MARPAESLIF_NTRACE
        if (symbolp->nullableActionp != NULL) {
          MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Nullable semantic of symbol %d (%s) is %s", symbolp->idi, symbolp->descp->asciis, _marpaESLIF_action2asciis(symbolp->nullableActionp));
        } else {
          MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Nullable semantic of symbol %d (%s) is grammar's default", symbolp->idi, symbolp->descp->asciis);
        }
#endif
      } else {
        short foundEmptyb = 0;
        /* More than one rule. If there is an empty rule, use it. Please note that Marpa precomputation made sure that the */
        /* empty rule is unique (there cannot be LHS ::= ; twice). */
        for (rulei = 0; rulei < GENERICSTACK_USED(symbolp->nullableRuleStackp); rulei++) {
          if (! GENERICSTACK_IS_PTR(symbolp->nullableRuleStackp, rulei)) {
            /* Impossible */
            MARPAESLIF_ERRORF(marpaESLIFp, "symbolp->nullableRuleStackp at indice %d is not PTR (got %s, value %d)", rulei, _marpaESLIF_genericStack_i_types(symbolp->nullableRuleStackp, rulei), GENERICSTACKITEMTYPE(symbolp->nullableRuleStackp, rulei));
            goto err;
          }
          rulep = (marpaESLIF_rule_t *) GENERICSTACK_GET_PTR(symbolp->nullableRuleStackp, rulei);
          if (GENERICSTACK_USED(rulep->rhsStackp) <= 0) {
            foundEmptyb = 1;
            symbolp->nullableActionp = rulep->actionp;
#ifndef MARPAESLIF_NTRACE
            if (symbolp->nullableActionp != NULL) {
              MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Nullable semantic of symbol %d (%s) is %s", symbolp->idi, symbolp->descp->asciis, _marpaESLIF_action2asciis(symbolp->nullableActionp));
            } else {
              MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Nullable semantic of symbol %d (%s) is grammar's default", symbolp->idi, symbolp->descp->asciis);
            }
#endif
          }
        }
        if (! foundEmptyb) {
          short                doneFirstSemanticb = 0;
          marpaESLIF_action_t *firstSemanticp;

          /* None of the rules is empty. Then the all must have the same semantic */
          for (rulei = 0; rulei < GENERICSTACK_USED(symbolp->nullableRuleStackp); rulei++) {
            if (! GENERICSTACK_IS_PTR(symbolp->nullableRuleStackp, rulei)) {
              /* Impossible */
              MARPAESLIF_ERRORF(marpaESLIFp, "symbolp->nullableRuleStackp at indice %d is not PTR (got %s, value  %d)", rulei, _marpaESLIF_genericStack_i_types(symbolp->nullableRuleStackp, rulei), GENERICSTACKITEMTYPE(symbolp->nullableRuleStackp, rulei));
              goto err;
            }
            rulep = (marpaESLIF_rule_t *) GENERICSTACK_GET_PTR(symbolp->nullableRuleStackp, rulei);
            if (! doneFirstSemanticb) {
              firstSemanticp = rulep->actionp;
              doneFirstSemanticb = 1;
            } else {
              /* This is is ok if it is NULL btw */
              if (! _marpaESLIF_action_eqb(firstSemanticp, rulep->actionp)) {
                MARPAESLIF_ERRORF(marpaESLIFp, "When nulled, symbol %d (%s) can have more than one semantic, and this is not allowed", symbolp->idi, symbolp->descp->asciis);
                goto err;
              }
            }
          }
          symbolp->nullableActionp = firstSemanticp;
#ifndef MARPAESLIF_NTRACE
          if (symbolp->nullableActionp != NULL) {
            MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Nullable semantic of symbol %d (%s) is %s", symbolp->idi, symbolp->descp->asciis, _marpaESLIF_action2asciis(symbolp->nullableActionp));
          } else {
            MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Nullable semantic of symbol %d (%s) is grammar's default", symbolp->idi, symbolp->descp->asciis);
          }
#endif
        }
      }
    }
  }

  /*
    7. lexeme events are meaningul only on lexemes -; Non-lexeme events are meaningful only on non-lexemes.
       The second case is a bit vicious because marpa allows terminals to be predicted, but not to be completed.
       We restrict the "event" keyword to non-terminals, and the "lexeme event" to terminals.
  */
  for (grammari = 0; grammari < GENERICSTACK_USED(grammarStackp); grammari++) {
    if (! GENERICSTACK_IS_PTR(grammarStackp, grammari)) {
      continue;
    }
    grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, grammari);
    MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Checking lexeme events in grammar level %d (%s)", grammarp->leveli, grammarp->descp->asciis);

    symbolStackp = grammarp->symbolStackp;
    for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
      MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);
      if (MARPAESLIF_IS_LEXEME(symbolp)) {
        if ((symbolp->eventPredicteds != NULL) || (symbolp->eventNulleds != NULL) || (symbolp->eventCompleteds != NULL)) {
          MARPAESLIF_ERRORF(marpaESLIFp, "Event on symbol <%s> at grammar level %d (%s) but it is a lexeme, you must use the \":lexeme <%s> pause => eventType event => eventName\" form", symbolp->descp->asciis, grammari, grammarp->descp->asciis, symbolp->descp->asciis);
          goto err;
        }
      } else if (MARPAESLIF_IS_TERMINAL(symbolp)) {
        if ((symbolp->eventPredicteds != NULL) || (symbolp->eventNulleds != NULL) || (symbolp->eventCompleteds != NULL)) {
          MARPAESLIF_ERRORF(marpaESLIFp, "Event on symbol <%s> at grammar level %d (%s) but it is a terminal, you must use the \":terminal <%s> pause => eventType event => eventName\" form", symbolp->descp->asciis, grammari, grammarp->descp->asciis, symbolp->descp->asciis);
          goto err;
        }
      } else {
        if ((symbolp->eventBefores != NULL) || (symbolp->eventAfters != NULL)) {
          MARPAESLIF_ERRORF(marpaESLIFp, "Lexeme or terminal event on symbol <%s> at grammar level %d (%s) but it is not a lexeme nor a terminal, you must use the \"event eventName = eventType <%s>\" form", symbolp->descp->asciis, grammari, grammarp->descp->asciis, symbolp->descp->asciis);
          goto err;
        }
      }
    }
  }

  /*
    8. Grammar names must all be different
  */
  for (grammari = 0; grammari < GENERICSTACK_USED(grammarStackp); grammari++) {
    if (! GENERICSTACK_IS_PTR(grammarStackp, grammari)) {
      continue;
    }
    grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, grammari);
    MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Checking name of of grammar level %d (%s)", grammarp->leveli, grammarp->descp->asciis);

    for (grammarj = 0; grammarj < GENERICSTACK_USED(grammarStackp); grammarj++) {
      if (grammari == grammarj) {
        continue;
      }
      if (! GENERICSTACK_IS_PTR(grammarStackp, grammarj)) {
        continue;
      }
      subgrammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, grammarj);
      if (_marpaESLIF_string_utf8_eqb(grammarp->descp, subgrammarp->descp)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "Grammars at level %d and %d have the same name (%s)", grammarp->leveli, subgrammarp->leveli, grammarp->descp->asciis);
        goto err;
      }
    }
  }

  /*
   9. :discard events are possible only if the RHS of the :discard rule is not a lexeme
  */
  for (grammari = 0; grammari < GENERICSTACK_USED(grammarStackp); grammari++) {
    if (! GENERICSTACK_IS_PTR(grammarStackp, grammari)) {
      continue;
    }
    grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, grammari);
    MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Checking :discard events in grammar level %d (%s)", grammarp->leveli, grammarp->descp->asciis);

    symbolStackp = grammarp->symbolStackp;
    for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
      MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);
      if (! symbolp->discardRhsb) {
        continue;
      }
      if (symbolp->discardEvents == NULL) {
        continue;
      }

      if (! symbolp->lhsb) {
        /* This symbol is not an lhs in this grammar */
        MARPAESLIF_ERRORF(marpaESLIFp, "Discard event \"%s\" is not possible unless the RHS is also an LHS at grammar level %d (%s)", symbolp->discardEvents, grammari, grammarp->descp->asciis);
        goto err;
      }
    }
  }

  /*
    10. Precompile lua script
  */
  if (! _marpaESLIFGrammar_lua_precompileb(marpaESLIFGrammarp)) {
    MARPAESLIF_ERROR(marpaESLIFp, "Lua precompilation failure");
    goto err;
  }

  /* Fill grammars information */
  /* - rule IDs, rule show (ASCII) */
  /* - symbol IDs, symbol show (ASCII) */
  /* - grammar show (ASCII) */
  for (grammari = 0; grammari < GENERICSTACK_USED(grammarStackp); grammari++) {
    if (! GENERICSTACK_IS_PTR(grammarStackp, grammari)) {
      continue;
    }
    grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, grammari);

    MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Filling rule IDs array in grammar level %d (%s)", grammari, grammarp->descp->asciis);
    ruleStackp = grammarp->ruleStackp;
    grammarp->nrulel = GENERICSTACK_USED(ruleStackp);
    if (grammarp->nrulel > 0) {
      if (grammarp->ruleip != NULL) {
        free(grammarp->ruleip);
      }
      grammarp->ruleip = (int *) malloc(grammarp->nrulel * sizeof(int));
      if (grammarp->ruleip == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
        goto err;
      }
      for (rulei = 0; rulei < GENERICSTACK_USED(ruleStackp); rulei++) {
        MARPAESLIF_INTERNAL_GET_RULE_FROM_STACK(marpaESLIFp, rulep, ruleStackp, rulei);
        grammarp->ruleip[rulei] = rulep->idi;
        _marpaESLIF_rule_createshowv(marpaESLIFp, grammarp, rulep, NULL, &asciishowl);
        if (rulep->asciishows != NULL) {
          free(rulep->asciishows);
        }
        rulep->asciishows = (char *) malloc(asciishowl);
        if (rulep->asciishows == NULL) {
          MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
          goto err;
        }
        /* It is guaranteed that asciishowl is >= 1 - c.f. _marpaESLIF_rule_createshowv() */
        rulep->asciishows[0] = '\0';
        _marpaESLIF_rule_createshowv(marpaESLIFp, grammarp, rulep, rulep->asciishows, NULL);
      }
    }

    MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Filling symbol IDs array in grammar level %d (%s)", grammari, grammarp->descp->asciis);
    symbolStackp = grammarp->symbolStackp;
    grammarp->nsymboll = GENERICSTACK_USED(symbolStackp);
    if (grammarp->nsymboll > 0) {
      if (grammarp->symbolip != NULL) {
        free(grammarp->symbolip);
      }
      grammarp->symbolip = (int *) malloc(grammarp->nsymboll * sizeof(int));
      if (grammarp->symbolip == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
        goto err;
      }
      for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
        MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);
        grammarp->symbolip[symboli] = symbolp->idi;
      }
    }
  }

  rcb = 1;
  goto done;
  
 err:
  rcb = 0;

 done:
  if (marpaWrapperGrammarClonep != NULL) {
    marpaWrapperGrammar_freev(marpaWrapperGrammarClonep);
  }

  MARPAESLIF_TRACEF(marpaESLIFp, funcs, "return %d", (int) rcb);
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFGrammar_haveLexemeb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int grammari, marpaWrapperGrammar_t *marpaWrapperGrammarp, short *haveLexemebp)
/*****************************************************************************/
{
  static const char    *funcs         = "_marpaESLIF_haveLexemeb";
  marpaESLIF_t         *marpaESLIFp   = marpaESLIFGrammarp->marpaESLIFp;
  genericStack_t       *grammarStackp = marpaESLIFGrammarp->grammarStackp;
  short                 haveLexemeb   = 0;
  marpaESLIF_grammar_t *grammarp;
  genericStack_t       *symbolStackp;
  short                 rcb;
  int                   symboli;
  marpaESLIF_symbol_t  *symbolp;
  int                   marpaWrapperSymbolPropertyBitSet;

  /* Get grammar at the wanted level */
  if (grammari < 0) {
    MARPAESLIF_ERRORF(marpaESLIFp, "grammari must be >= 0", grammari);
    goto err;
  }
  if (! GENERICSTACK_IS_PTR(grammarStackp, grammari)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "No grammar level at level %d", grammari);
    goto err;
  }
  grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, grammari);

  symbolStackp = grammarp->symbolStackp;
  for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
    MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);

    if (! marpaWrapperGrammar_symbolPropertyb(marpaWrapperGrammarp, symboli, &marpaWrapperSymbolPropertyBitSet)) {
      goto err;
    }

    if ((marpaWrapperSymbolPropertyBitSet & MARPAWRAPPER_SYMBOL_IS_PRODUCTIVE) != MARPAWRAPPER_SYMBOL_IS_PRODUCTIVE) {
      continue;
    }

    if (MARPAESLIF_IS_LEXEME(symbolp)) {
      haveLexemeb = 1;
      break;
    }
  }
  
  rcb = 1;
  if (haveLexemebp != NULL) {
    *haveLexemebp = haveLexemeb;
  }
  goto done;
  
 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static inline marpaESLIF_grammar_t *_marpaESLIF_grammar_newp(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaWrapperGrammarOption_t *marpaWrapperGrammarOptionp, int leveli, char *descEncodings, char *descs, size_t descl, marpaESLIF_action_t *defaultSymbolActionp, marpaESLIF_action_t *defaultRuleActionp)
/*****************************************************************************/
{
  static const char             *funcs          = "_marpaESLIF_grammar_newp";
  marpaESLIF_t                  *marpaESLIFp    = marpaESLIFGrammarp->marpaESLIFp;
  genericLogger_t               *genericLoggerp = NULL;
  marpaESLIF_grammar_t          *grammarp       = NULL;
  marpaESLIF_stringGenerator_t   marpaESLIF_stringGenerator;

  /* MARPAESLIF_TRACE(marpaESLIFp, funcs, "Building ESLIF grammar"); */

  if (leveli < 0) {
    MARPAESLIF_ERRORF(marpaESLIFp, "Grammar level must be >= 0, current value is %d", leveli);
    goto err;
  }
  
  grammarp = (marpaESLIF_grammar_t *) malloc(sizeof(marpaESLIF_grammar_t));
  if (grammarp == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
    goto err;
  }

  grammarp->marpaESLIFGrammarp                 = marpaESLIFGrammarp;
  grammarp->leveli                             = leveli;
  grammarp->descp                              = NULL;
  grammarp->descautob                          = 0;
  grammarp->latmb                              = 1;    /* latmb true is the default */
  grammarp->marpaWrapperGrammarStartp          = NULL;
  grammarp->marpaWrapperGrammarStartNoEventp   = NULL;
  grammarp->nSymbolStartl                      = 0;
  grammarp->symbolArrayStartp                  = NULL;
  grammarp->marpaWrapperGrammarDiscardp        = NULL;
  grammarp->marpaWrapperGrammarDiscardNoEventp = NULL;
  grammarp->nSymbolDiscardl                    = 0;
  grammarp->symbolArrayDiscardp                = NULL;
  grammarp->discardp                           = NULL;
  grammarp->symbolStackp                       = NULL; /* Take care, pointer to a stack inside grammar structure */
  grammarp->ruleStackp                         = NULL; /* Take care, pointer to a stack inside grammar structure */
  grammarp->defaultSymbolActionp               = NULL;
  grammarp->defaultRuleActionp                 = NULL;
  grammarp->starti                             = 0;    /* Filled during grammar validation */
  grammarp->starts                             = NULL; /* Filled during grammar validation - shallow pointer */
  grammarp->symbolip                           = NULL; /* Filled by grammar validation */
  grammarp->nsymboll                           = 0;    /* Filled by grammar validation */
  grammarp->ruleip                             = NULL; /* Filled by grammar validation */
  grammarp->nrulel                             = 0;    /* Filled by grammar validation */
  grammarp->nbupdatei                          = 0;    /* Used by ESLIF grammar bootstrap */
  grammarp->asciishows                         = NULL;
  grammarp->discardi                           = -1;   /* Eventually filled to a value >= 0 during grammar validation */

  grammarp->marpaWrapperGrammarStartp = marpaWrapperGrammar_newp(marpaWrapperGrammarOptionp);
  if (grammarp->marpaWrapperGrammarStartp == NULL) {
    goto err;
  }

  /* ----------- Grammar description ------------- */
  if ((descs == NULL) || (descl <= 0)) {
    /* Generate a default description */
    marpaESLIF_stringGenerator.marpaESLIFp = marpaESLIFp;
    marpaESLIF_stringGenerator.s           = NULL;
    marpaESLIF_stringGenerator.l           = 0;
    marpaESLIF_stringGenerator.okb         = 0;
    marpaESLIF_stringGenerator.allocl      = 0;

    genericLoggerp = GENERICLOGGER_CUSTOM(_marpaESLIF_generateStringWithLoggerCallback, (void *) &marpaESLIF_stringGenerator, GENERICLOGGER_LOGLEVEL_TRACE);
    if (genericLoggerp == NULL) {
      goto err;
    }
    GENERICLOGGER_TRACEF(genericLoggerp, "Grammar level %d", leveli);
    if (! marpaESLIF_stringGenerator.okb) {
      goto err;
    }
    grammarp->descp = _marpaESLIF_string_newp(marpaESLIFp, "ASCII" /* We KNOW we generated an ASCII stringy */, marpaESLIF_stringGenerator.s, strlen(marpaESLIF_stringGenerator.s));
    free(marpaESLIF_stringGenerator.s);
    grammarp->descautob = 1;
  } else {
    grammarp->descp = _marpaESLIF_string_newp(marpaESLIFp, descEncodings, descs, descl);
    grammarp->descautob = 0;
  }
  if (grammarp->descp == NULL) {
    goto err;
  }

  grammarp->symbolStackp = &(grammarp->_symbolStack);
  GENERICSTACK_INIT(grammarp->symbolStackp);
  if (GENERICSTACK_ERROR(grammarp->symbolStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "symbolStackp initialization failure, %s", strerror(errno));
    grammarp->symbolStackp = NULL;
    goto err;
  }

  grammarp->ruleStackp = &(grammarp->_ruleStack);
  GENERICSTACK_INIT(grammarp->ruleStackp);
  if (GENERICSTACK_ERROR(grammarp->ruleStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "ruleStackp initialization failure, %s", strerror(errno));
    grammarp->ruleStackp = NULL;
    goto err;
  }

  if (defaultSymbolActionp != NULL) {
    if (! _marpaESLIF_action_validb(marpaESLIFp, defaultSymbolActionp)) {
      goto err;
    }
    grammarp->defaultSymbolActionp = _marpaESLIF_action_clonep(marpaESLIFp, defaultSymbolActionp);
    if (grammarp->defaultSymbolActionp == NULL) {
      goto err;
    }
  }

  if (defaultRuleActionp != NULL) {
    if (! _marpaESLIF_action_validb(marpaESLIFp, defaultRuleActionp)) {
      goto err;
    }
    grammarp->defaultRuleActionp = _marpaESLIF_action_clonep(marpaESLIFp, defaultRuleActionp);
    if (grammarp->defaultRuleActionp == NULL) {
      goto err;
    }
  }

  goto done;

 err:
  _marpaESLIF_grammar_freev(grammarp);
  grammarp = NULL;

 done:
  /* MARPAESLIF_TRACEF(marpaESLIFp, funcs, "return %p", grammarp); */
  GENERICLOGGER_FREE(genericLoggerp);
  return grammarp;
}

/*****************************************************************************/
static inline void _marpaESLIF_grammar_freev(marpaESLIF_grammar_t *grammarp)
/*****************************************************************************/
{
  if (grammarp != NULL) {
    _marpaESLIF_string_freev(grammarp->descp, 0 /* onStackb */);
    if (grammarp->marpaWrapperGrammarStartp != NULL) {
      marpaWrapperGrammar_freev(grammarp->marpaWrapperGrammarStartp);
    }
    if (grammarp->marpaWrapperGrammarStartNoEventp != NULL) {
      marpaWrapperGrammar_freev(grammarp->marpaWrapperGrammarStartNoEventp);
    }
    if (grammarp->symbolArrayStartp != NULL) {
      free(grammarp->symbolArrayStartp);
    }
    if (grammarp->marpaWrapperGrammarDiscardp != NULL) {
      marpaWrapperGrammar_freev(grammarp->marpaWrapperGrammarDiscardp);
    }
    if (grammarp->marpaWrapperGrammarDiscardNoEventp != NULL) {
      marpaWrapperGrammar_freev(grammarp->marpaWrapperGrammarDiscardNoEventp);
    }
    if (grammarp->symbolArrayDiscardp != NULL) {
      free(grammarp->symbolArrayDiscardp);
    }
    _marpaESLIF_symbolStack_freev(grammarp->symbolStackp);
    _marpaESLIF_ruleStack_freev(grammarp->ruleStackp);
    if (grammarp->ruleip != NULL) {
      free(grammarp->ruleip);
    }
    if (grammarp->symbolip != NULL) {
      free(grammarp->symbolip);
    }
    _marpaESLIF_action_freev(grammarp->defaultSymbolActionp);
    _marpaESLIF_action_freev(grammarp->defaultRuleActionp);
    if (grammarp->asciishows != NULL) {
      free(grammarp->asciishows);
    }
    free(grammarp);
  }
}

/*****************************************************************************/
static inline void _marpaESLIF_ruleStack_freev(genericStack_t *ruleStackp)
/*****************************************************************************/
{
  if (ruleStackp != NULL) {
    while (GENERICSTACK_USED(ruleStackp) > 0) {
      if (GENERICSTACK_IS_PTR(ruleStackp, GENERICSTACK_USED(ruleStackp) - 1)) {
	marpaESLIF_rule_t *rulep = (marpaESLIF_rule_t *) GENERICSTACK_POP_PTR(ruleStackp);
	_marpaESLIF_rule_freev(rulep);
      } else {
	GENERICSTACK_USED(ruleStackp)--;
      }
    }
    GENERICSTACK_RESET(ruleStackp); /* Take care, ruleStackp is a pointer to a stack inside grammar structure */
  }
}

/*****************************************************************************/
static inline void _marpaESLIFrecognizer_lexemeStack_freev(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, genericStack_t *lexemeStackp)
/*****************************************************************************/
{
  static const char *funcs = "_marpaESLIFrecognizer_lexemeStack_freev";

  if (lexemeStackp != NULL) {
    MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
    MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

    _marpaESLIFrecognizer_lexemeStack_resetv(marpaESLIFRecognizerp, lexemeStackp);
    GENERICSTACK_RESET(lexemeStackp); /* Take care, lexemeStackp is a pointer to a static genericStack_t in recognizer's structure */

    MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "return");
    MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  }

}

/*****************************************************************************/
static inline void _marpaESLIFrecognizer_lexemeStack_resetv(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, genericStack_t *lexemeStackp)
/*****************************************************************************/
{
  static const char *funcs = "_marpaESLIFrecognizer_lexemeStack_resetv";
  marpaESLIF_t      *marpaESLIFp     = marpaESLIFRecognizerp->marpaESLIFp;
  genericStack_t    *beforePtrStackp = marpaESLIFRecognizerp->beforePtrStackp;
  genericHash_t     *beforePtrHashp  = marpaESLIFRecognizerp->beforePtrHashp;
  genericHash_t     *afterPtrHashp   = marpaESLIFRecognizerp->afterPtrHashp;
  int                i;
  int                usedi;

  if (lexemeStackp != NULL) {

    usedi = (int) GENERICSTACK_USED(lexemeStackp);
    for (i = usedi - 1; i >= 0; i--) {
      _marpaESLIFValueResult_stack_i_setb(marpaESLIFp,
                                          lexemeStackp,
                                          i,
                                          NULL, /* marpaESLIFValueResultp */
                                          0, /* forgetb */
                                          beforePtrStackp,
                                          beforePtrHashp,
                                          afterPtrHashp,
                                          NULL /* marpaESLIFValueResultOrigp */);
      GENERICSTACK_USED(lexemeStackp)--;
    }

  }

}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_lexemeStack_i_setb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, genericStack_t *lexemeStackp, int i, marpaESLIFValueResult_t *marpaESLIFValueResultp)
/*****************************************************************************/
{
  static const char *funcs = "_marpaESLIFRecognizer_lexemeStack_i_setb";
  short              rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  /* Validate the input */
  if (! _marpaESLIFRecognizer_value_validb(marpaESLIFRecognizerp, marpaESLIFValueResultp, NULL /* userDatavp */, NULL /* callbackp */)) {
    goto err;
  }

  /* Lexeme input stack is a stack of marpaESLIFValueResult */
  rcb = _marpaESLIFValueResult_stack_i_setb(marpaESLIFRecognizerp->marpaESLIFp,
                                            marpaESLIFRecognizerp->lexemeInputStackp,
                                            i,
                                            marpaESLIFValueResultp,
                                            0, /* forgetb */
                                            marpaESLIFRecognizerp->beforePtrStackp,
                                            marpaESLIFRecognizerp->beforePtrHashp,
                                            marpaESLIFRecognizerp->afterPtrHashp,
                                            NULL /* marpaESLIFValueResultOrigp */);
  
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;

  return rcb;
}

/*****************************************************************************/
static inline marpaESLIF_rule_t *_marpaESLIF_rule_newp(marpaESLIF_t *marpaESLIFp, marpaESLIF_grammar_t *grammarp, char *descEncodings, char *descs, size_t descl, int lhsi, size_t nrhsl, int *rhsip, int exceptioni, int ranki, short nullRanksHighb, short sequenceb, int minimumi, int separatori, short properb, marpaESLIF_action_t *actionp, short passthroughb, short hideseparatorb, short *skipbp)
/*****************************************************************************/
{
  static const char               *funcs          = "_marpaESLIF_rule_newp";
  genericStack_t                  *symbolStackp   = grammarp->symbolStackp;
  marpaESLIF_rule_t               *rulep          = NULL;
  short                            symbolFoundb   = 0;
  genericLogger_t                 *genericLoggerp = NULL;
  marpaESLIF_stringGenerator_t     marpaESLIF_stringGenerator;
  short                            separatorFoundb;
  marpaESLIF_symbol_t             *symbolp;
  marpaWrapperGrammarRuleOption_t  marpaWrapperGrammarRuleOption;
  size_t                           i;
  int                              symboli;

  /* MARPAESLIF_TRACE(marpaESLIFp, funcs, "Building rule"); */

  /* It is a non-sense to provide skipbp without rhsip */
  if ((skipbp != NULL) && (rhsip == NULL)) {
    MARPAESLIF_ERROR(marpaESLIFp, "(skipbp != NULL) && (rhsip == NULL)");
    goto err;
  }

  rulep = (marpaESLIF_rule_t *) malloc(sizeof(marpaESLIF_rule_t));
  if (rulep == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
    goto err;
  }

  rulep->idi            = -1;
  rulep->descp          = NULL;
  rulep->descautob      = 0;
  rulep->asciishows     = NULL; /* Filled by grammar validation */
  rulep->lhsp           = NULL;
  rulep->separatorp     = NULL;
  rulep->rhsStackp      = NULL; /* Take care, pointer to a stack inside rule structure */
  rulep->rhsip          = NULL; /* Convenience array of RHS ids for rule introspection */
  rulep->skipbp         = NULL; /* Convenience array of RHS ids for rule introspection and action arguments generation */
  rulep->exceptionp     = NULL;
  rulep->exceptionIdi   = -1;
  rulep->actionp        = NULL;
  rulep->discardEvents  = NULL;
  rulep->discardEventb  = 0;
  rulep->ranki          = ranki;
  rulep->nullRanksHighb = nullRanksHighb;
  rulep->sequenceb      = sequenceb;
  rulep->properb        = properb;
  rulep->minimumi       = minimumi;
  rulep->passthroughb   = passthroughb;
  rulep->propertyBitSet = 0; /* Filled by grammar validation */
  rulep->hideseparatorb = hideseparatorb;

  /* Look to the symbol itself, and remember it is an LHS - this is used when validating the grammar */
  for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
    MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);
    if (symbolp->idi == lhsi) {
      symbolFoundb = 1;
      break;
    }
  }
  if (! symbolFoundb) {
    MARPAESLIF_ERRORF(marpaESLIFp, "At grammar level %d: LHS symbol No %d does not exist", grammarp->leveli, lhsi);
    goto err;
  }
  symbolp->lhsb = 1;
  rulep->lhsp = symbolp;

  /* Idem for the separator */
  if (sequenceb && (separatori >= 0)) {
    separatorFoundb = 0;
    for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
      MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);
      if (symbolp->idi == separatori) {
        separatorFoundb = 1;
        break;
      }
    }
    if (! separatorFoundb) {
      MARPAESLIF_ERRORF(marpaESLIFp, "At grammar level %d: Rule <%s>: LHS separator No %d does not exist", grammarp->leveli, rulep->lhsp->descp->asciis, separatori);
      goto err;
    }
    rulep->separatorp = symbolp;
  }

  rulep->rhsStackp = &(rulep->_rhsStack);
  GENERICSTACK_INIT(rulep->rhsStackp);
  if (GENERICSTACK_ERROR(rulep->rhsStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "rhsStackp initialization failure, %s", strerror(errno));
    rulep->rhsStackp = NULL;
    goto err;
  }

  /* Fill rhs symbol stack */
  if (rhsip != NULL) {
    for (i = 0; i < nrhsl; i++) {
      MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, rhsip[i]);
      GENERICSTACK_PUSH_PTR(rulep->rhsStackp, symbolp);
      if (GENERICSTACK_ERROR(rulep->rhsStackp)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "rhsStackp push failure, %s", strerror(errno));
        goto err;
      }
    }
    /* And duplicate this array */
    rulep->rhsip = (int *) malloc(sizeof(int) * nrhsl);
    if (rulep->rhsip == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
      goto err;
    }
    memcpy(rulep->rhsip, rhsip, sizeof(int) * nrhsl);

    /* Also the eventual skip information */
    if (skipbp != NULL) {
      rulep->skipbp = (short *) malloc(sizeof(short) * nrhsl);
      memcpy(rulep->skipbp, skipbp, sizeof(short) * nrhsl);
    }
  }
  
  /* Fill exception symbol */
  if (exceptioni >= 0) {
    MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, rulep->exceptionp, symbolStackp, exceptioni);
    /* ... and make sure that there is only one RHS */
    if (nrhsl != 1) {
      MARPAESLIF_ERRORF(marpaESLIFp, "At grammar level %d: Rule <%s>: There must be exactly one RHS, instead of %ld, before the '-' exception sign", grammarp->leveli, rulep->lhsp->descp->asciis, (unsigned long) nrhsl);
      goto err;
    }
  }
  
  marpaWrapperGrammarRuleOption.ranki            = ranki;
  marpaWrapperGrammarRuleOption.nullRanksHighb   = nullRanksHighb;
  marpaWrapperGrammarRuleOption.sequenceb        = sequenceb;
  marpaWrapperGrammarRuleOption.separatorSymboli = separatori;
  marpaWrapperGrammarRuleOption.properb          = properb;
  marpaWrapperGrammarRuleOption.minimumi         = minimumi;

  /* ----------- Meta Identifier ------------ */
  rulep->idi = marpaWrapperGrammar_newRulei(grammarp->marpaWrapperGrammarStartp, &marpaWrapperGrammarRuleOption, lhsi, nrhsl, rhsip);
  if (rulep->idi < 0) {
    MARPAESLIF_ERRORF(marpaESLIFp, "At grammar level %d: Rule <%s>: Creation failure", grammarp->leveli, rulep->lhsp->descp->asciis);
    goto err;
  }

  /* ---------------- Action ---------------- */
  if (actionp != NULL) {
    if (! _marpaESLIF_action_validb(marpaESLIFp, actionp)) {
      MARPAESLIF_ERRORF(marpaESLIFp, "At grammar level %d: Rule <%s>: Invalid action", grammarp->leveli, rulep->lhsp->descp->asciis);
      goto err;
    }
    rulep->actionp = _marpaESLIF_action_clonep(marpaESLIFp, actionp);
    if (rulep->actionp == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFp, "At grammar level %d: Rule <%s>: Clone failure", grammarp->leveli, rulep->lhsp->descp->asciis);
      goto err;
    }
  }

  /* -------- Rule Description -------- */
  if ((descs == NULL) || (descl <= 0)) {
    /* Generate a default description */
    marpaESLIF_stringGenerator.marpaESLIFp = marpaESLIFp;
    marpaESLIF_stringGenerator.s           = NULL;
    marpaESLIF_stringGenerator.l           = 0;
    marpaESLIF_stringGenerator.okb         = 0;
    marpaESLIF_stringGenerator.allocl      = 0;

    genericLoggerp = GENERICLOGGER_CUSTOM(_marpaESLIF_generateStringWithLoggerCallback, (void *) &marpaESLIF_stringGenerator, GENERICLOGGER_LOGLEVEL_TRACE);
    if (genericLoggerp == NULL) {
      goto err;
    }
    GENERICLOGGER_TRACEF(genericLoggerp, "Rule No %d", rulep->idi);
    if (! marpaESLIF_stringGenerator.okb) {
      goto err;
    }
    rulep->descp = _marpaESLIF_string_newp(marpaESLIFp, "ASCII" /* We KNOW we generated an ASCII stringy */, marpaESLIF_stringGenerator.s, strlen(marpaESLIF_stringGenerator.s));
    rulep->descautob = 1;
    free(marpaESLIF_stringGenerator.s);
  } else {
    rulep->descp = _marpaESLIF_string_newp(marpaESLIFp, descEncodings, descs, descl);
    rulep->descautob = 0;
  }
  if (rulep->descp == NULL) {
    goto err;
  }

  goto done;

 err:
  _marpaESLIF_rule_freev(rulep);
  rulep = NULL;

 done:
  /* MARPAESLIF_TRACEF(marpaESLIFp, funcs, "return %p", rulep); */
  GENERICLOGGER_FREE(genericLoggerp);
  return rulep;
}

/*****************************************************************************/
static inline void _marpaESLIF_rule_freev(marpaESLIF_rule_t *rulep)
/*****************************************************************************/
{
  if (rulep != NULL) {
    _marpaESLIF_string_freev(rulep->descp, 0 /* onStackb */);
    if (rulep->asciishows != NULL) {
      free(rulep->asciishows);
    }
    _marpaESLIF_action_freev(rulep->actionp);
    if (rulep->rhsip != NULL) {
      free(rulep->rhsip);
    }
    if (rulep->skipbp != NULL) {
      free(rulep->skipbp);
    }
    if (rulep->discardEvents) {
      free(rulep->discardEvents);
    }
    /* In the rule structure, lhsp, rhsStackp and exceptionp contain shallow pointers */
    /* Only the stack themselves should be freed. */
    /*
    _marpaESLIF_symbol_freev(marpaESLIFp, rulep->lhsp);
    _marpaESLIF_symbolStack_freev(rulep->rhsStackp);
    _marpaESLIF_symbol_freev(marpaESLIFp, exceptionp);
    */
    GENERICSTACK_RESET(rulep->rhsStackp); /* Take care, this is a pointer to a stack inside rule structure */
    free(rulep);
  }
}

/*****************************************************************************/
static inline marpaESLIF_symbol_t *_marpaESLIF_symbol_newp(marpaESLIF_t *marpaESLIFp)
/*****************************************************************************/
{
  static const char   *funcs   = "_marpaESLIF_symbol_newp";
  marpaESLIF_symbol_t *symbolp = NULL;

  /* MARPAESLIF_TRACE(marpaESLIFp, funcs, "Building symbol"); */

  symbolp = (marpaESLIF_symbol_t *) malloc(sizeof(marpaESLIF_symbol_t));
  if (symbolp == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
    goto err;
  }

  symbolp->type   = MARPAESLIF_SYMBOL_TYPE_NA;
  /* Union itself is undetermined at this stage */
  symbolp->startb                 = 0;
  symbolp->discardb               = 0;
  symbolp->discardRhsb            = 0;
  symbolp->lhsb                   = 0;
  symbolp->topb                   = 0; /* Revisited by grammar validation */
  symbolp->idi                    = -1;
  symbolp->descp                  = NULL;
  symbolp->eventBefores           = NULL;
  symbolp->eventBeforeb           = 1; /* An event is on by default */
  symbolp->eventAfters            = NULL;
  symbolp->eventAfterb            = 1; /* An event is on by default */
  symbolp->eventPredicteds        = NULL;
  symbolp->eventPredictedb        = 1; /* An event is on by default */
  symbolp->eventNulleds           = NULL;
  symbolp->eventNulledb           = 1; /* An event is on by default */
  symbolp->eventCompleteds        = NULL;
  symbolp->eventCompletedb        = 1; /* An event is on by default */
  symbolp->discardEvents          = NULL; /* Shallow copy */
  symbolp->discardEventb          = 1; /* An event is on by default */
  symbolp->lookupLevelDeltai      = 1;   /* Default lookup is the next grammar level */
  symbolp->lookupMetas            = NULL;
  symbolp->lookupResolvedLeveli   = 0; /* This will be overwriten by _marpaESLIFGrammar_validateb() and used only when symbol is a lexeme from another grammar */
  symbolp->priorityi              = 0; /* Default priority is 0 */
  symbolp->nullableRuleStackp     = NULL; /* Take care, this is a pointer to an stack inside symbol structure */
  symbolp->nullableActionp        = NULL;
  symbolp->propertyBitSet         = 0; /* Filled by grammar validation */
  symbolp->eventBitSet            = 0; /* Filled by grammar validation */
  symbolp->lhsRuleStackp          = NULL;
  symbolp->exceptionp             = NULL;
  symbolp->symbolActionp          = NULL;
  symbolp->ifActionp              = NULL;

  symbolp->nullableRuleStackp = &(symbolp->_nullableRuleStack);
  GENERICSTACK_INIT(symbolp->nullableRuleStackp);
  if (GENERICSTACK_ERROR(symbolp->nullableRuleStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "symbolp->nullableRuleStackp initialization failure, %s", strerror(errno));
    symbolp->nullableRuleStackp = NULL;
    goto err;
  }

  symbolp->lhsRuleStackp = &(symbolp->_lhsRuleStack);
  GENERICSTACK_INIT(symbolp->lhsRuleStackp);
  if (GENERICSTACK_ERROR(symbolp->lhsRuleStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "symbolp->lhsRuleStackp initialization failure, %s", strerror(errno));
    symbolp->lhsRuleStackp = NULL;
    goto err;
  }
  
  goto done;

 err:
  _marpaESLIF_symbol_freev(symbolp);
  symbolp = NULL;

 done:
  /* MARPAESLIF_TRACEF(marpaESLIFp, funcs, "return %p", symbolp); */
  return symbolp;
}

/*****************************************************************************/
static inline void _marpaESLIF_symbol_freev(marpaESLIF_symbol_t *symbolp)
/*****************************************************************************/
{
  if (symbolp != NULL) {
    /* All pointers are the top level of this structure are shallow pointers */
    switch (symbolp->type) {
    case MARPAESLIF_SYMBOL_TYPE_TERMINAL:
      _marpaESLIF_terminal_freev(symbolp->u.terminalp);
      break;
    case MARPAESLIF_SYMBOL_TYPE_META:
      _marpaESLIF_meta_freev(symbolp->u.metap);
      break;
    default:
      break;
    }
    if (symbolp->eventBefores != NULL) {
      free(symbolp->eventBefores);
    }
    if (symbolp->eventAfters != NULL) {
      free(symbolp->eventAfters);
    }
    if (symbolp->eventPredicteds) {
      free(symbolp->eventPredicteds);
    }
    if (symbolp->eventNulleds) {
      free(symbolp->eventNulleds);
    }
    if (symbolp->eventCompleteds) {
      free(symbolp->eventCompleteds);
    }
    _marpaESLIF_action_freev(symbolp->symbolActionp);
    _marpaESLIF_action_freev(symbolp->ifActionp);

    GENERICSTACK_RESET(symbolp->nullableRuleStackp); /* Take care, this is a pointer to stack internal to symbol structure */
    GENERICSTACK_RESET(symbolp->lhsRuleStackp); /* Take care, this is a pointer to stack internal to symbol structure */

    free(symbolp);
  }
}

/*****************************************************************************/
static inline void _marpaESLIF_symbolStack_freev(genericStack_t *symbolStackp)
/*****************************************************************************/
{
  if (symbolStackp != NULL) {
    while (GENERICSTACK_USED(symbolStackp) > 0) {
      if (GENERICSTACK_IS_PTR(symbolStackp, GENERICSTACK_USED(symbolStackp) - 1)) {
	marpaESLIF_symbol_t *symbolp = (marpaESLIF_symbol_t *) GENERICSTACK_POP_PTR(symbolStackp);
	_marpaESLIF_symbol_freev(symbolp);
      } else {
	GENERICSTACK_USED(symbolStackp)--;
      }
    }
    GENERICSTACK_RESET(symbolStackp); /* Take care, this is a pointer to a stack inside symbol structure */
  }
}

/*****************************************************************************/
static inline void _marpaESLIF_terminal_freev(marpaESLIF_terminal_t *terminalp)
/*****************************************************************************/
{
  if (terminalp != NULL) {
    _marpaESLIF_string_freev(terminalp->descp, 0 /* onStackb */);
    if (terminalp->patterns != NULL) {
      free(terminalp->patterns);
    }
    if (terminalp->regex.match_datap != NULL) {
      pcre2_match_data_free(terminalp->regex.match_datap);
    }
    if (terminalp->modifiers != NULL) {
      free(terminalp->modifiers);
    }
    if (terminalp->regex.patternp != NULL) {
      pcre2_code_free(terminalp->regex.patternp);
    }
    if (terminalp->bytes != NULL) {
      free(terminalp->bytes);
    }
    free(terminalp);
  }
}

/*****************************************************************************/
short marpaESLIF_versionb(marpaESLIF_t *marpaESLIFp, char **versionsp)
/*****************************************************************************/
{
  short rcb;
  
  if (marpaESLIFp == NULL) {
    errno = EINVAL;
    goto err;
  }

  if (versionsp != NULL) {
    *versionsp = marpaESLIFp->versions;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIF_versionMajorb(marpaESLIF_t *marpaESLIFp, int *versionMajorip)
/*****************************************************************************/
{
  short rcb;
  
  if (marpaESLIFp == NULL) {
    errno = EINVAL;
    goto err;
  }

  if (versionMajorip != NULL) {
    *versionMajorip = marpaESLIFp->versionMajori;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIF_versionMinorb(marpaESLIF_t *marpaESLIFp, int *versionMinorip)
/*****************************************************************************/
{
  short rcb;
  
  if (marpaESLIFp == NULL) {
    errno = EINVAL;
    goto err;
  }

  if (versionMinorip != NULL) {
    *versionMinorip = marpaESLIFp->versionMinori;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIF_versionPatchb(marpaESLIF_t *marpaESLIFp, int *versionPatchip)
/*****************************************************************************/
{
  short rcb;
  
  if (marpaESLIFp == NULL) {
    errno = EINVAL;
    goto err;
  }

  if (versionPatchip != NULL) {
    *versionPatchip = marpaESLIFp->versionPatchi;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
marpaESLIF_t *marpaESLIF_newp(marpaESLIFOption_t *marpaESLIFOptionp)
/*****************************************************************************/
{
  return _marpaESLIF_newp(marpaESLIFOptionp, 1 /* validateb */);
}

/*****************************************************************************/
static inline marpaESLIF_t *_marpaESLIF_newp(marpaESLIFOption_t *marpaESLIFOptionp, short validateb)
/*****************************************************************************/
{
  static const char     *funcs       = "marpaESLIF_newp";
  marpaESLIF_grammar_t  *grammarp    = NULL;
  marpaESLIF_t          *marpaESLIFp = NULL;
  genericLogger_t       *genericLoggerp;
  genericLoggerLevel_t   genericLoggerLeveli;
  void                  *NULLp       = NULL;
  void                  *p           = NULL;

  if (marpaESLIFOptionp == NULL) {
    marpaESLIFOptionp = &marpaESLIFOption_default_template;
  }

  genericLoggerp = marpaESLIFOptionp->genericLoggerp;
  if (genericLoggerp != NULL) {
    genericLoggerLeveli = genericLogger_logLevel_geti(genericLoggerp);
  }

#ifndef MARPAESLIF_NTRACE
  if (genericLoggerp != NULL) {
    GENERICLOGGER_TRACEF(genericLoggerp, "[%s] Building ESLIF", funcs);
  }
#endif

  marpaESLIFp = (marpaESLIF_t *) malloc(sizeof(marpaESLIF_t));
  if (marpaESLIFp == NULL) {
    if (marpaESLIFOptionp->genericLoggerp != NULL) {
      GENERICLOGGER_ERRORF(marpaESLIFOptionp->genericLoggerp, "malloc failure, %s", strerror(errno));
      goto err;
    }
  }

  marpaESLIFp->marpaESLIFOption          = *marpaESLIFOptionp;
  marpaESLIFp->marpaESLIFGrammarp        = NULL;
  marpaESLIFp->anycharp                  = NULL;
  marpaESLIFp->newlinep                  = NULL;
  marpaESLIFp->stringModifiersp          = NULL;
  marpaESLIFp->characterClassModifiersp  = NULL;
  marpaESLIFp->regexModifiersp           = NULL;
  marpaESLIFp->traceLoggerp              = NULL;
  marpaESLIFp->NULLisZeroBytesb          = 0;
  marpaESLIFp->versions                  = (char *) MARPAESLIF_VERSION_STATIC;
  marpaESLIFp->versionMajori             = (int) MARPAESLIF_VERSION_MAJOR_STATIC;
  marpaESLIFp->versionMinori             = (int) MARPAESLIF_VERSION_MINOR_STATIC;
  marpaESLIFp->versionPatchi             = (int) MARPAESLIF_VERSION_PATCH_STATIC;

  marpaESLIFp->marpaESLIFValueResultTrue.contextp         = NULL;
  marpaESLIFp->marpaESLIFValueResultTrue.representationp  = NULL;
  marpaESLIFp->marpaESLIFValueResultTrue.type             = MARPAESLIF_VALUE_TYPE_BOOL;
  marpaESLIFp->marpaESLIFValueResultTrue.u.y              = MARPAESLIFVALUERESULTBOOL_TRUE;

  marpaESLIFp->marpaESLIFValueResultFalse.contextp        = NULL;
  marpaESLIFp->marpaESLIFValueResultFalse.representationp = NULL;
  marpaESLIFp->marpaESLIFValueResultFalse.type            = MARPAESLIF_VALUE_TYPE_BOOL;
  marpaESLIFp->marpaESLIFValueResultFalse.u.y             = MARPAESLIFVALUERESULTBOOL_FALSE;

  sprintf(marpaESLIFp->float_fmts, "%%%d.%de", FLT_DIG + 8, FLT_DIG);
  sprintf(marpaESLIFp->double_fmts, "%%%d.%de", DBL_DIG + 8, DBL_DIG);
  sprintf(marpaESLIFp->long_double_fmts, "%%%d.%dLe", LDBL_DIG + 8, LDBL_DIG);

  /* Check if zero bytes (.i.e calloc'ed memory) is the same thing as NULL */
  p = calloc(1, sizeof(void *));
  if (p == NULL) {
    if (marpaESLIFOptionp->genericLoggerp != NULL) {
      GENERICLOGGER_ERRORF(marpaESLIFOptionp->genericLoggerp, "calloc failure, %s", strerror(errno));
      goto err;
    }
  }
  marpaESLIFp->NULLisZeroBytesb = (memcmp(p, &NULLp, sizeof(void *)) == 0);

  /* **************************************************************** */
  /* It is very important to NOT create terminals of type STRING here */
  /* **************************************************************** */
  
  /* Create internal anychar regex */
  marpaESLIFp->anycharp = _marpaESLIF_terminal_newp(marpaESLIFp,
                                                    NULL, /* grammarp */
                                                    MARPAESLIF_EVENTTYPE_NONE, /* eventSeti */
                                                    "ASCII", /* We KNOW this is an ASCII thingy */
                                                    INTERNAL_ANYCHAR_PATTERN, /* descs */
                                                    strlen(INTERNAL_ANYCHAR_PATTERN), /* descl */
                                                    MARPAESLIF_TERMINAL_TYPE_REGEX, /* type */
                                                    "su", /* modifiers */
                                                    INTERNAL_ANYCHAR_PATTERN, /* utf8s */
                                                    strlen(INTERNAL_ANYCHAR_PATTERN), /* utf8l */
                                                    NULL, /* testFullMatchs */
                                                    NULL  /* testPartialMatchs */
                                                    );
  if (marpaESLIFp->anycharp == NULL) {
    goto err;
  }

  /* Create internal newline regex */
  /* Please note that the newline regexp does NOT require UTF-8 correctness -; */
  marpaESLIFp->newlinep = _marpaESLIF_terminal_newp(marpaESLIFp,
                                                    NULL /* grammarp */,
                                                    MARPAESLIF_EVENTTYPE_NONE, /* eventSeti */
                                                    "ASCII", /* We KNOW this is an ASCII thingy */
                                                    INTERNAL_NEWLINE_PATTERN /* descs */,
                                                    strlen(INTERNAL_NEWLINE_PATTERN) /* descl */,
                                                    MARPAESLIF_TERMINAL_TYPE_REGEX, /* type */
                                                    NULL, /* modifiers */
                                                    INTERNAL_NEWLINE_PATTERN, /* utf8s */
                                                    strlen(INTERNAL_NEWLINE_PATTERN), /* utf8l */
                                                    NULL, /* testFullMatchs */
                                                    NULL  /* testPartialMatchs */
                                                    );
  if (marpaESLIFp->newlinep == NULL) {
    goto err;
  }

  /* Create internal anychar regex */
  marpaESLIFp->stringModifiersp = _marpaESLIF_terminal_newp(marpaESLIFp,
                                                            NULL, /* grammarp */
                                                            MARPAESLIF_EVENTTYPE_NONE, /* eventSeti */
                                                            "ASCII", /* We KNOW this is an ASCII thingy */
                                                            INTERNAL_STRINGMODIFIERS_PATTERN, /* descs */
                                                            strlen(INTERNAL_STRINGMODIFIERS_PATTERN), /* descl */
                                                            MARPAESLIF_TERMINAL_TYPE_REGEX, /* type */
                                                            "Au", /* modifiers */
                                                            INTERNAL_STRINGMODIFIERS_PATTERN, /* utf8s */
                                                            strlen(INTERNAL_STRINGMODIFIERS_PATTERN), /* utf8l */
                                                            NULL, /* testFullMatchs */
                                                            NULL  /* testPartialMatchs */
                                                            );
  if (marpaESLIFp->stringModifiersp == NULL) {
    goto err;
  }

  /* Create internal anychar regex */
  marpaESLIFp->characterClassModifiersp = _marpaESLIF_terminal_newp(marpaESLIFp,
                                                                    NULL, /* grammarp */
                                                                    MARPAESLIF_EVENTTYPE_NONE, /* eventSeti */
                                                                    "ASCII", /* We KNOW this is an ASCII thingy */
                                                                    INTERNAL_CHARACTERCLASSMODIFIERS_PATTERN, /* descs */
                                                                    strlen(INTERNAL_CHARACTERCLASSMODIFIERS_PATTERN), /* descl */
                                                                    MARPAESLIF_TERMINAL_TYPE_REGEX, /* type */
                                                                    "Au", /* modifiers */
                                                                    INTERNAL_CHARACTERCLASSMODIFIERS_PATTERN, /* utf8s */
                                                                    strlen(INTERNAL_CHARACTERCLASSMODIFIERS_PATTERN), /* utf8l */
                                                                    NULL, /* testFullMatchs */
                                                                    NULL  /* testPartialMatchs */
                                                                    );
  if (marpaESLIFp->characterClassModifiersp == NULL) {
    goto err;
  }

  /* Create internal anychar regex */
  marpaESLIFp->regexModifiersp = _marpaESLIF_terminal_newp(marpaESLIFp,
                                                           NULL, /* grammarp */
                                                           MARPAESLIF_EVENTTYPE_NONE, /* eventSeti */
                                                           "ASCII", /* We KNOW this is an ASCII thingy */
                                                           INTERNAL_REGEXMODIFIERS_PATTERN, /* descs */
                                                           strlen(INTERNAL_REGEXMODIFIERS_PATTERN), /* descl */
                                                           MARPAESLIF_TERMINAL_TYPE_REGEX, /* type */
                                                           "Au", /* modifiers */
                                                           INTERNAL_REGEXMODIFIERS_PATTERN, /* utf8s */
                                                           strlen(INTERNAL_REGEXMODIFIERS_PATTERN), /* utf8l */
                                                           NULL, /* testFullMatchs */
                                                           NULL  /* testPartialMatchs */
                                                           );
  if (marpaESLIFp->regexModifiersp == NULL) {
    goto err;
  }

  marpaESLIFp->traceLoggerp = GENERICLOGGER_CUSTOM(_marpaESLIF_traceLoggerCallback, (void *) marpaESLIFp, GENERICLOGGER_LOGLEVEL_TRACE);
  /* Although this should never happen, it is okay if the trace logger is NULL */
  if (marpaESLIFp->traceLoggerp == NULL) {
    GENERICLOGGER_TRACEF(marpaESLIFOptionp->genericLoggerp, "genericLogger initialization failure, %s", strerror(errno));
  }

  /* Create internal ESLIF grammar - it is important to set the option first */
  marpaESLIFp->marpaESLIFGrammarp = (marpaESLIFGrammar_t *) malloc(sizeof(marpaESLIFGrammar_t));

  if (marpaESLIFp->marpaESLIFGrammarp == NULL) {
    GENERICLOGGER_ERRORF(marpaESLIFOptionp->genericLoggerp, "malloc failure, %s", strerror(errno));
    goto err;
  }

  marpaESLIFp->marpaESLIFGrammarp->marpaESLIFp             = marpaESLIFp;
  marpaESLIFp->marpaESLIFGrammarp->marpaESLIFGrammarOption = marpaESLIFGrammarOption_default_template;
  marpaESLIFp->marpaESLIFGrammarp->grammarStackp           = NULL;
  marpaESLIFp->marpaESLIFGrammarp->grammarp                = NULL;
  marpaESLIFp->marpaESLIFGrammarp->luabytep                = NULL; /* There is no "script" in marpaESLIF grammar */
  marpaESLIFp->marpaESLIFGrammarp->luabytel                = 0;
  marpaESLIFp->marpaESLIFGrammarp->luaprecompiledp         = NULL;
  marpaESLIFp->marpaESLIFGrammarp->luaprecompiledl         = 0;
  marpaESLIFp->marpaESLIFGrammarp->luadescp                = NULL;
  marpaESLIFp->marpaESLIFGrammarp->internalRuleCounti      = 0;

  marpaESLIFp->marpaESLIFGrammarp->grammarStackp = &(marpaESLIFp->marpaESLIFGrammarp->_grammarStack);
  GENERICSTACK_INIT(marpaESLIFp->marpaESLIFGrammarp->grammarStackp);
  if (GENERICSTACK_ERROR(marpaESLIFp->marpaESLIFGrammarp->grammarStackp)) {
    GENERICLOGGER_ERRORF(marpaESLIFOptionp->genericLoggerp, "marpaESLIFp->marpaESLIFGrammarp->grammarStackp initialization failure, %s", strerror(errno));
    marpaESLIFp->marpaESLIFGrammarp->grammarStackp = NULL;
    goto err;
  }

  GENERICSTACK_INIT(marpaESLIFp->marpaESLIFGrammarp->grammarStackp);
  if (GENERICSTACK_ERROR(marpaESLIFp->marpaESLIFGrammarp->grammarStackp)) {
    GENERICLOGGER_ERRORF(marpaESLIFOptionp->genericLoggerp, "marpaESLIFp->marpaESLIFGrammarp->grammarStackp initialization failure, %s", strerror(errno));
    marpaESLIFp->marpaESLIFGrammarp->grammarStackp = NULL;
    goto err;
  }

  /* When we bootstrap we do no want to log unless there is an error */
  if (marpaESLIFOptionp->genericLoggerp != NULL) {
    genericLogger_logLevel_seti(marpaESLIFOptionp->genericLoggerp, GENERICLOGGER_LOGLEVEL_INFO);
  }
  
  /* G1 */
  grammarp = _marpaESLIF_bootstrap_grammar_G1p(marpaESLIFp->marpaESLIFGrammarp);
  if (grammarp == NULL) {
    goto err;
  }
  GENERICSTACK_SET_PTR(marpaESLIFp->marpaESLIFGrammarp->grammarStackp, grammarp, grammarp->leveli);
  if (GENERICSTACK_ERROR(marpaESLIFp->marpaESLIFGrammarp->grammarStackp)) {
    GENERICLOGGER_ERRORF(marpaESLIFOptionp->genericLoggerp, "marpaESLIFp->marpaESLIFGrammarp->grammarStackp set failure, %s", strerror(errno));
    goto err;
  }
  grammarp = NULL;

  /* L0 */
  grammarp = _marpaESLIF_bootstrap_grammar_L0p(marpaESLIFp->marpaESLIFGrammarp);
  if (grammarp == NULL) {
    goto err;
  }
  GENERICSTACK_SET_PTR(marpaESLIFp->marpaESLIFGrammarp->grammarStackp, grammarp, grammarp->leveli);
  if (GENERICSTACK_ERROR(marpaESLIFp->marpaESLIFGrammarp->grammarStackp)) {
    GENERICLOGGER_ERRORF(marpaESLIFOptionp->genericLoggerp, "marpaESLIFp->marpaESLIFGrammarp->grammarStackp set failure, %s", strerror(errno));
    goto err;
  }
  grammarp = NULL;

  if (validateb) {
    /* Validate the bootstrap grammar - this will precompute it: it can never be modified */
    if (! _marpaESLIFGrammar_validateb(marpaESLIFp->marpaESLIFGrammarp)) {
      goto err;
    }
  }

  /* Check there is a top-level grammar */
#ifndef MARPAESLIF_NTRACE
  /* Should never happen */
  if (! GENERICSTACK_IS_PTR(marpaESLIFp->marpaESLIFGrammarp->grammarStackp, 0)) {
    GENERICLOGGER_ERROR(marpaESLIFOptionp->genericLoggerp, "No top level grammar after bootstrap");
    goto err;
  }
#endif
  marpaESLIFp->marpaESLIFGrammarp->grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(marpaESLIFp->marpaESLIFGrammarp->grammarStackp, 0);
  goto done;
  
 err:
  marpaESLIF_freev(marpaESLIFp);
  marpaESLIFp = NULL;

 done:
  if (p != NULL) {
    free(p);
  }
  /* Restore log-level if user provided one */
  if (marpaESLIFOptionp->genericLoggerp != NULL) {
    genericLogger_logLevel_seti(marpaESLIFOptionp->genericLoggerp, genericLoggerLeveli);
  }
  
  _marpaESLIF_grammar_freev(grammarp);
#ifndef MARPAESLIF_NTRACE
  if ((marpaESLIFp != NULL) && (genericLoggerp != NULL)) {
    int     ngrammari;
    int    *ruleip;
    size_t  rulel;
    int     leveli;
    size_t  l;

    if (marpaESLIFGrammar_ngrammarib(marpaESLIFp->marpaESLIFGrammarp, &ngrammari)) {
      for (leveli = 0; leveli < ngrammari; leveli++) {
        if (marpaESLIFGrammar_rulearray_by_levelb(marpaESLIFp->marpaESLIFGrammarp, &ruleip, &rulel, leveli, NULL /* descp */)) {
          GENERICLOGGER_TRACEF(genericLoggerp, "[%s] -------------------------", funcs);
          GENERICLOGGER_TRACEF(genericLoggerp, "[%s] ESLIF grammar at level %d:", funcs, leveli);
          GENERICLOGGER_TRACEF(genericLoggerp, "[%s] -------------------------", funcs);
          for (l = 0; l < rulel; l++) {
            char *ruleshows;
            if (marpaESLIFGrammar_ruleshowform_by_levelb(marpaESLIFp->marpaESLIFGrammarp, l, &ruleshows, leveli, NULL /* descp */)) {
              GENERICLOGGER_TRACEF(genericLoggerp, "[%s] %s", funcs, ruleshows);
            }
          }
        }
      }
    }
    GENERICLOGGER_TRACEF(genericLoggerp, "[%s] return %p", funcs, marpaESLIFp);
  }
#endif
	
  return marpaESLIFp;
}

/*****************************************************************************/
short marpaESLIF_extend_builtin_actionb(marpaESLIF_t *marpaESLIFp, char **actionsArrayp, size_t actionsArrayl)
/*****************************************************************************/
{
  /* Exensions are limited to things that we can entirely control, because bootstrap have */
  /* hardcoded options. Every action is transformed into a string "<action name> => XXX action => '::ascii'. */
  /* ONLY action names are totally supported. */
  static const char         *funcs          = "marpaESLIF_extend_builtin_actionb";
  marpaESLIF_t              *marpaESLIFTmpp = NULL;
  char                      *extensions     = NULL;
  size_t                     actionIteratorl;
  size_t                     extensionl;
  marpaESLIFGrammarOption_t  marpaESLIFGrammarOption;
  short                      rcb;

  if (marpaESLIFp == NULL) {
    errno = EINVAL;
    goto err;
  }
  if ((actionsArrayp == NULL) || (actionsArrayl <= 0)) {
    MARPAESLIF_ERROR(marpaESLIFp, "No action extension given");
    goto err;
  }

  /* Compute needed size for full extension */
  extensionl = 0;
  for (actionIteratorl = 0; actionIteratorl < actionsArrayl; actionIteratorl++) {
    extensionl +=
      1 /* newline */ +
      strlen("<action name> ::= ") +
      strlen(actionsArrayp[actionIteratorl]) +
      1 /* space */ +
      strlen("action => ::ascii");
  }
  extensions = (char *) malloc(extensionl + 1);
  if (extensions == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
    goto err;
  }
  extensions[0] = '\0';
  for (actionIteratorl = 0; actionIteratorl < actionsArrayl; actionIteratorl++) {
    strcat(extensions, "\n");
    strcat(extensions, "<action name> ::= ");
    strcat(extensions, actionsArrayp[actionIteratorl]);
    strcat(extensions, " ");
    strcat(extensions, "action => ::ascii");
  }

  /* Extension is done in three steps:
   * - create a pristine marpaESLIF_t (i.e. without validation)
   * - inject this pristine's marpaESLIF's marpaESLIFGrammar into normal marpaESLIFGrammar_newp
   * - validate
   */
  marpaESLIFTmpp = _marpaESLIF_newp(&(marpaESLIFp->marpaESLIFOption), 0 /* validateb */);
  if (marpaESLIFTmpp == NULL) {
    /* In theory this can NEVER fail, because we start with a marpaESLIFp that already */
    /* passed this step. */
    goto err;
  }

  marpaESLIFGrammarOption.bytep               = (void *) extensions;
  marpaESLIFGrammarOption.bytel               = strlen(extensions);
  marpaESLIFGrammarOption.encodings           = NULL;
  marpaESLIFGrammarOption.encodingl           = 0;
  /* Per def the output of _marpaESLIFGrammar_newp() will be marpaESLIFTmpp->marpaESLIFGrammarp if success */
  if (_marpaESLIFGrammar_newp(marpaESLIFp, &marpaESLIFGrammarOption, marpaESLIFTmpp->marpaESLIFGrammarp /* marpaESLIfGrammarPreviousp */, 0 /* unsafeb */) == NULL) {
    goto err;
  }

  if (! _marpaESLIFGrammar_validateb(marpaESLIFTmpp->marpaESLIFGrammarp)) {
    goto err;
  }

  /* Just free the underlying grammar */
  marpaESLIFGrammar_freev(marpaESLIFp->marpaESLIFGrammarp);
  /* Copy the new grammar */
  marpaESLIFp->marpaESLIFGrammarp = marpaESLIFTmpp->marpaESLIFGrammarp;
  /* Remember this was copied */
  marpaESLIFTmpp->marpaESLIFGrammarp = NULL;
  /* Though take care, marpaESLIFGrammarp maintains a pointer to its parent... */
  marpaESLIFp->marpaESLIFGrammarp->marpaESLIFp = marpaESLIFp;

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  /* Free marpaESLIFTmpp in any case */
  marpaESLIF_freev(marpaESLIFTmpp);
  if (extensions != NULL) {
    free(extensions);
  }
  return rcb;
}

/*****************************************************************************/
marpaESLIFOption_t *marpaESLIF_optionp(marpaESLIF_t *marpaESLIFp)
/*****************************************************************************/
{
  static const char *funcs = "marpaESLIF_optionp";

  if (marpaESLIFp == NULL) {
    errno = EINVAL;
    return NULL;
  }

  return &(marpaESLIFp->marpaESLIFOption);
}

/*****************************************************************************/
void marpaESLIF_freev(marpaESLIF_t *marpaESLIFp)
/*****************************************************************************/
{
  if (marpaESLIFp != NULL) {
    marpaESLIFGrammar_freev(marpaESLIFp->marpaESLIFGrammarp);
    _marpaESLIF_terminal_freev(marpaESLIFp->anycharp);
    _marpaESLIF_terminal_freev(marpaESLIFp->newlinep);
    _marpaESLIF_terminal_freev(marpaESLIFp->stringModifiersp);
    _marpaESLIF_terminal_freev(marpaESLIFp->characterClassModifiersp);
    _marpaESLIF_terminal_freev(marpaESLIFp->regexModifiersp);
    if (marpaESLIFp->traceLoggerp != NULL) {
      genericLogger_freev(&(marpaESLIFp->traceLoggerp));
    }
    free(marpaESLIFp);
  }
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_terminal_matcherb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_terminal_t *terminalp, char *inputs, size_t inputl, short eofb, marpaESLIF_matcher_value_t *rcip, marpaESLIFValueResult_t *marpaESLIFValueResultp, size_t *matchedLengthlp)
/*****************************************************************************/
{
  static const char              *funcs              = "_marpaESLIFRecognizer_terminal_matcherb";
  marpaESLIF_stream_t            *marpaESLIF_streamp = marpaESLIFRecognizerp->marpaESLIF_streamp;
  marpaESLIF_matcher_value_t      rci;
  marpaESLIF_regex_t              marpaESLIF_regex;
  int                             pcre2Errornumberi;
  PCRE2_UCHAR                     pcre2ErrorBuffer[256];
  PCRE2_SIZE                     *pcre2_ovectorp;
  size_t                          matchedLengthl;
  char                           *matchedp;
  marpaESLIF_uint32_t             pcre2_optioni;
  short                           binmodeb;
  short                           needUtf8Validationb;
  short                           rcb;
  char                           *bytes;
  size_t                          bytel;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  /*********************************************************************************/
  /* A matcher tries to match a terminal v.s. input that is eventually incomplete. */
  /* It return 1 on success, 0 on failure, -1 if more data is needed.              */
  /*********************************************************************************/

  if (inputl > 0) {

    marpaESLIF_regex = terminalp->regex;

    /* If the regexp is working in UTF mode then we check that character conversion   */
    /* was done. This is how we are sure that calling regexp with PCRE2_NO_UTF_CHECK  */
    /* is ok: we have done ourself the UTF-8 validation on the subject.               */
    if (marpaESLIF_regex.utfb) {                     /* UTF-8 correctness is required */
      if (! marpaESLIF_streamp->utfb) {
        pcre2_optioni = pcre2_option_binary_default;  /* We have done no check : PCRE2 will do it */
        binmodeb = 1;
        needUtf8Validationb = 1;
      } else {
        pcre2_optioni = pcre2_option_char_default;    /* We made sure this is ok */
        binmodeb = 0;
        needUtf8Validationb = 0;
      }
    } else {
      pcre2_optioni = pcre2_option_binary_default;    /* Not needed */
      binmodeb = 1;
      needUtf8Validationb = 0;
    }

    /* --------------------------------------------------------- */
    /* Is is a true string and UTF-8 validation is not needed ?  */
    /* Then do a direct memcmp -;                                */
    /* --------------------------------------------------------- */
    if ((! needUtf8Validationb) && terminalp->memcmpb) {
      bytes = terminalp->bytes;
      bytel = terminalp->bytel;

      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Doing memcmp on %ld bytes, inputl=%ld", (unsigned long) bytel, (unsigned long) inputl);
      /* Empty string is allowed and never matches */
      if (bytel <= 0) {
        rci = MARPAESLIF_MATCH_FAILURE;
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s for %s", "MARPAESLIF_MATCH_FAILURE", terminalp->descp->asciis);
      } else {
        if (inputl >= bytel) {
          if (memcmp(inputs, bytes, bytel) == 0) {
            rci = MARPAESLIF_MATCH_OK;
            MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s for %s", "MARPAESLIF_MATCH_OK", terminalp->descp->asciis);
            matchedp       = inputs;
            matchedLengthl = bytel;
          } else {
            rci = MARPAESLIF_MATCH_FAILURE;
            MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s for %s", "MARPAESLIF_MATCH_FAILURE", terminalp->descp->asciis);
          }
        } else {
          if (memcmp(inputs, bytes, inputl) == 0) {
            /* Partial match */
            if (eofb) {
              rci = MARPAESLIF_MATCH_FAILURE;
              MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s for %s", "MARPAESLIF_MATCH_FAILURE", terminalp->descp->asciis);
            } else {
              rci = MARPAESLIF_MATCH_AGAIN;
              MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s for %s", "MARPAESLIF_MATCH_AGAIN", terminalp->descp->asciis);
            }
          } else {
            rci = MARPAESLIF_MATCH_FAILURE;
            MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s for %s", "MARPAESLIF_MATCH_FAILURE", terminalp->descp->asciis);
          }
        }
      }
      goto string_done;
    }

    /* --------------------------------------------------------- */
    /* Anchored regex...                                         */
    /* --------------------------------------------------------- */
    /*
     Patterns are always compiled with PCRE2_ANCHORED by default,
     except when there is the "A" modifier. In this case, we allow
     to execute the regex ONLY if the whole stream was read in one
     call to the user's read callback.
    */
    if (! marpaESLIF_regex.isAnchoredb) {
      if (! marpaESLIF_streamp->noAnchorIsOkb) {
        /* This is an error unless we are at EOF */
        if (! eofb) {
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "%s: You used the \"A\" modifier to set the pattern non-anchored, but then you must read the whole input in one go, and you have not reached EOF yet", terminalp->descp->asciis);
          goto err;
        }
      }
    }
    
    /* --------------------------------------------------------- */
    /* EOF mode:                                                 */
    /* return full match status: OK or FAILURE.                  */
    /* --------------------------------------------------------- */
    /* NOT EOF mode:                                             */
    /* If the full match is successful:                          */
    /* - if it reaches the end of the buffer, return EGAIN.      */
    /* - if it does not reach the end of the buffer, return OK.  */
    /* Else if the partial match is successul:                   */
    /* - return EGAIN.                                           */
    /* Else                                                      */
    /* - return FAILURE.                                         */
    /*                                                           */
    /* In conclusion we always start with the full match.        */
    /* --------------------------------------------------------- */
#ifdef PCRE2_CONFIG_JIT
    if (marpaESLIF_regex.jitCompleteb && !needUtf8Validationb) {        /* JIT fast path is never doing UTF-8 validation */
      pcre2Errornumberi = pcre2_jit_match(marpaESLIF_regex.patternp,    /* code */
                                          (PCRE2_SPTR) inputs,          /* subject */
                                          (PCRE2_SIZE) inputl,          /* length */
                                          (PCRE2_SIZE) 0,               /* startoffset */
                                          pcre2_optioni,                /* options */
                                          marpaESLIF_regex.match_datap, /* match data */
                                          NULL                          /* match context - used default */
                                          );
      if (pcre2Errornumberi == PCRE2_ERROR_JIT_STACKLIMIT) {
        /* Back luck, out of stack for JIT */
        pcre2_get_error_message(pcre2Errornumberi, pcre2ErrorBuffer, sizeof(pcre2ErrorBuffer));
        goto eof_nojitcomplete;
      }
    } else {
    eof_nojitcomplete:
#endif
      pcre2Errornumberi = pcre2_match(marpaESLIF_regex.patternp,    /* code */
                                      (PCRE2_SPTR) inputs,          /* subject */
                                      (PCRE2_SIZE) inputl,          /* length */
                                      (PCRE2_SIZE) 0,               /* startoffset */
                                      pcre2_optioni,                /* options */
                                      marpaESLIF_regex.match_datap, /* match data */
                                      NULL                          /* match context - used default */
                                      );
#ifdef PCRE2_CONFIG_JIT
    }
#endif

    /* In any case - set UTF buffer correctness if needed and if possible */
    if (binmodeb && marpaESLIF_regex.utfb) {
      if ((pcre2Errornumberi >= 0) || (pcre2Errornumberi == PCRE2_ERROR_NOMATCH)) {
        /* Either regex is successful, either it failed with the accepted failure code PCRE2_ERROR_NOMATCH */
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s: UTF-8 correctness successful and remembered", terminalp->descp->asciis);
        marpaESLIF_streamp->utfb = 1;
      }
    }

    if (eofb) {
      if (pcre2Errornumberi < 0) {
        /* Only PCRE2_ERROR_NOMATCH is an acceptable error. */
        if (pcre2Errornumberi != PCRE2_ERROR_NOMATCH) {
          pcre2_get_error_message(pcre2Errornumberi, pcre2ErrorBuffer, sizeof(pcre2ErrorBuffer));
          MARPAESLIF_WARNF(marpaESLIFRecognizerp->marpaESLIFp, "%s: Uncaught pcre2 match failure: %s", terminalp->descp->asciis, pcre2ErrorBuffer);
        }
        rci = MARPAESLIF_MATCH_FAILURE;
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s for %s", "MARPAESLIF_MATCH_FAILURE", terminalp->descp->asciis);
      } else {
        /* Check the length of matched data */
        if (pcre2_get_ovector_count(marpaESLIF_regex.match_datap) <= 0) {
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "%s: pcre2_get_ovector_count returned no number of pairs of values", terminalp->descp->asciis);
          goto err;
        }
        pcre2_ovectorp = pcre2_get_ovector_pointer(marpaESLIF_regex.match_datap);
        if (pcre2_ovectorp == NULL) {
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "%s: pcre2_get_ovector_pointer returned NULL", terminalp->descp->asciis);
          goto err;
        }
        /* We said PCRE2_NOTEMPTY so this cannot be empty */
        matchedLengthl = pcre2_ovectorp[1] - pcre2_ovectorp[0];
        if (matchedLengthl <= 0) {
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "%s: Empty match when it is configured as not possible", terminalp->descp->asciis);
          goto err;
        }
        /* Very good -; */
        matchedp = inputs + pcre2_ovectorp[0];
        rci = MARPAESLIF_MATCH_OK;
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s for %s", "MARPAESLIF_MATCH_OK", terminalp->descp->asciis);
      }
    } else {
      if (pcre2Errornumberi >= 0) {
        /* Full match is successful. */
        /* Check the length of matched data */
        if (pcre2_get_ovector_count(marpaESLIF_regex.match_datap) <= 0) {
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "%s: pcre2_get_ovector_count returned no number of pairs of values", terminalp->descp->asciis);
          goto err;
        }
        pcre2_ovectorp = pcre2_get_ovector_pointer(marpaESLIF_regex.match_datap);
        if (pcre2_ovectorp == NULL) {
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "%s: pcre2_get_ovector_pointer returned NULL", terminalp->descp->asciis);
          goto err;
        }
        /* We said PCRE2_NOTEMPTY so this cannot be empty */
        matchedLengthl = pcre2_ovectorp[1] - pcre2_ovectorp[0];
        if (matchedLengthl <= 0) {
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "%s: Empty match when it is configured as not possible", terminalp->descp->asciis);
          goto err;
        }
        if (matchedLengthl >= inputl) {
          /* But end of the buffer is reached, and we are not at the eof! We have to ask for more bytes. */
          rci = MARPAESLIF_MATCH_AGAIN;
          MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s for %s", "MARPAESLIF_MATCH_AGAIN", terminalp->descp->asciis);
        } else {
          /* And end of the buffer is not reached */
          matchedp = inputs + pcre2_ovectorp[0];
          rci = MARPAESLIF_MATCH_OK;
          MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s for %s", "MARPAESLIF_MATCH_OK", terminalp->descp->asciis);
        }
      } else {
        /* Do a partial match. This section cannot return MARPAESLIF_MATCH_OK. */
        /* Please note that we explicitely NEVER ask for UTF-8 correctness here, because previous section */
        /* made sure it has always been done. */
#ifdef PCRE2_CONFIG_JIT
        if (marpaESLIF_regex.jitPartialb) {
          pcre2Errornumberi = pcre2_jit_match(marpaESLIF_regex.patternp,    /* code */
                                              (PCRE2_SPTR) inputs,          /* subject */
                                              (PCRE2_SIZE) inputl,          /* length */
                                              (PCRE2_SIZE) 0,               /* startoffset */
                                              pcre2_option_partial_default, /* options - this one is supported in JIT mode */
                                              marpaESLIF_regex.match_datap, /* match data */
                                              NULL                          /* match context - used default */
                                              );
          if (pcre2Errornumberi == PCRE2_ERROR_JIT_STACKLIMIT) {
            /* Back luck, out of stack for JIT */
            pcre2_get_error_message(pcre2Errornumberi, pcre2ErrorBuffer, sizeof(pcre2ErrorBuffer));
            goto eof_nojitpartial;
          }
        } else {
        eof_nojitpartial:
#endif
          pcre2Errornumberi = pcre2_match(marpaESLIF_regex.patternp,    /* code */
                                          (PCRE2_SPTR) inputs,          /* subject */
                                          (PCRE2_SIZE) inputl,          /* length */
                                          (PCRE2_SIZE) 0,               /* startoffset */
                                          pcre2_option_partial_default, /* options - this one is supported in JIT mode */
                                          marpaESLIF_regex.match_datap, /* match data */
                                          NULL                          /* match context - used default */
                                          );
#ifdef PCRE2_CONFIG_JIT
        }
#endif
        /* Only PCRE2_ERROR_PARTIAL is an acceptable error */
        if (pcre2Errornumberi == PCRE2_ERROR_PARTIAL) {
          /* Partial match is successful */
          rci = MARPAESLIF_MATCH_AGAIN;
          MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s for %s", "MARPAESLIF_MATCH_AGAIN", terminalp->descp->asciis);
        } else {
          /* Partial match is not successful */
          rci = MARPAESLIF_MATCH_FAILURE;
          MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s for %s", "MARPAESLIF_MATCH_FAILURE", terminalp->descp->asciis);
        }
      }
    }
  } else {
    rci = eofb ? MARPAESLIF_MATCH_FAILURE : MARPAESLIF_MATCH_AGAIN;
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s (inputl <= 0)", eofb ? "MARPAESLIF_MATCH_FAILURE" : "MARPAESLIF_MATCH_AGAIN");
  }

 string_done:
  if (rcip != NULL) {
    *rcip = rci;
  }

  if (rci == MARPAESLIF_MATCH_OK) {
    if (marpaESLIFValueResultp != NULL) {
      marpaESLIFValueResultp->contextp        = NULL;
      marpaESLIFValueResultp->representationp = NULL;
      marpaESLIFValueResultp->type            = MARPAESLIF_VALUE_TYPE_ARRAY;
      marpaESLIFValueResultp->u.a.sizel       = matchedLengthl;
      if (marpaESLIFRecognizerp->parentRecognizerp != NULL) {
        /* lexeme mode - caller's responsibility to take care -; */
        marpaESLIFValueResultp->u.a.p              = matchedp;
        marpaESLIFValueResultp->u.a.freeUserDatavp = NULL;
        marpaESLIFValueResultp->u.a.freeCallbackp  = NULL;
        marpaESLIFValueResultp->u.a.shallowb       = 1;
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Lexeme mode: returning {%p,%ld}", matchedp, matchedLengthl);
      } else {
        /* alloc mode */
        marpaESLIFValueResultp->u.a.p = malloc(matchedLengthl + 1); /* We always add a NUL byte for convenience */
        if (marpaESLIFValueResultp->u.a.p == NULL) {
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "malloc failure, %s", strerror(errno));
          goto err;
        }
        memcpy(marpaESLIFValueResultp->u.a.p, (void *) matchedp, matchedLengthl);
        marpaESLIFValueResultp->u.a.p[matchedLengthl] = '\0';
        marpaESLIFValueResultp->u.a.freeUserDatavp = marpaESLIFRecognizerp;
        marpaESLIFValueResultp->u.a.freeCallbackp  = _marpaESLIF_generic_freeCallbackv;
        marpaESLIFValueResultp->u.a.shallowb       = 0;
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Alloc mode: returning {%p,%ld}", marpaESLIFValueResultp->u.a.p, matchedLengthl);
      }
    }
    if (matchedLengthlp != NULL) {
      *matchedLengthlp = matchedLengthl;
    }
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_meta_matcherb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_symbol_t *symbolp, marpaESLIF_matcher_value_t *rcip, marpaESLIFValueResult_t *marpaESLIFValueResultp, short *isExhaustedbp, int maxStartCompletionsi, size_t *lastSizeBeforeCompletionlp, int *numberOfStartCompletionsip)
/*****************************************************************************/
{
  /* All in all, this routine is the core of this module, and the cause of recursion -; */
  static const char              *funcs                      = "_marpaESLIFRecognizer_meta_matcherb";
#ifndef MARPAESLIF_NTRACE
  marpaESLIFGrammar_t            *marpaESLIFGrammarp         = marpaESLIFRecognizerp->marpaESLIFGrammarp;
#endif
  marpaESLIFRecognizerOption_t    marpaESLIFRecognizerOption = marpaESLIFRecognizerp->marpaESLIFRecognizerOption; /* This is an internal recognizer */
  marpaESLIFValueOption_t         marpaESLIFValueOption      = marpaESLIFValueOption_default_template;
  short                           rcb;
  marpaESLIF_meta_t              *metap;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

#ifndef MARPAESLIF_NTRACE
  /* Safe check - should never happen though */
  if (symbolp->type != MARPAESLIF_SYMBOL_TYPE_META) {
    MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "%s called for a symbol that is not a meta symbol (type %d)", funcs, symbolp->type);
    goto err;
  }
#endif

  /* A meta matcher is always using ANOTHER grammar at level symbolp->grammarLeveli (validator guaranteed that is exists) that is sent on the stack. */
  /* Though the precomputed grammar is known to the symbol that called us, also sent on the stack. */
#ifndef MARPAESLIF_NTRACE
  /* Should never happen */
  if (! GENERICSTACK_IS_PTR(marpaESLIFGrammarp->grammarStackp, symbolp->lookupResolvedLeveli)) {
    MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "At grammar No %d (%s), meta symbol %d <%s> resolve to a grammar level %d that do not exist", marpaESLIFGrammarp->grammarp->leveli, marpaESLIFGrammarp->grammarp->descp->asciis, symbolp->u.metap->idi, symbolp->descp->asciis, symbolp->lookupResolvedLeveli);
    goto err;
  }
#endif

  metap                                        = symbolp->u.metap;
  marpaESLIFRecognizerOption.disableThresholdb = 1;
  marpaESLIFRecognizerOption.exhaustedb        = 1;

  if (! _marpaESLIFGrammar_parseb(metap->marpaESLIFGrammarLexemeClonep,
                                  &marpaESLIFRecognizerOption,
                                  &marpaESLIFValueOption,
                                  0 /* discardb */,
                                  1 /* noEventb - to make sure that the recognizer uses marpaWrapperGrammarStartNoEventp that we overwrote few lines upper... */,
                                  1 /* silentb */,
                                  marpaESLIFRecognizerp /* parentRecognizerp */,
                                  isExhaustedbp,
                                  marpaESLIFValueResultp,
                                  maxStartCompletionsi,
                                  lastSizeBeforeCompletionlp,
                                  numberOfStartCompletionsip,
                                  0 /* grammarIsOnStackb */)) {
    goto err;
  }

  if (rcip != NULL) {
    *rcip = MARPAESLIF_MATCH_OK;
  }

  rcb = 1;
  goto done;
  
 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_symbol_matcherb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_symbol_t *symbolp, marpaESLIF_matcher_value_t *rcip, marpaESLIFValueResult_t *marpaESLIFValueResultp, int maxStartCompletionsi, size_t *lastSizeBeforeCompletionlp, int *numberOfStartCompletionsip)
/*****************************************************************************/
/* This function can call for more data. If the later fails, it returns -1 and this is fatal, 0 is a normal error, 1 is ok. */
/*****************************************************************************/
{
  static const char          *funcs              = "_marpaESLIFRecognizer_symbol_matcherb";
  marpaESLIF_stream_t        *marpaESLIF_streamp = marpaESLIFRecognizerp->marpaESLIF_streamp;
  /* offset flag is meaningful only if the symbol is a terminal */
  short                       rcb;
  marpaESLIF_matcher_value_t  rci;
  marpaESLIFValueResult_t     marpaESLIFValueResult;
  size_t                      lastSizeBeforeCompletionl;
  int                         numberOfStartCompletionsi;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

 match_again:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Trying to match %s, eofb=%d, inputl=%ld", symbolp->descp->asciis, (int) marpaESLIF_streamp->eofb, marpaESLIF_streamp->inputl);
  switch (symbolp->type) {
  case MARPAESLIF_SYMBOL_TYPE_TERMINAL:
    lastSizeBeforeCompletionl = 0;
    /* A terminal can match only once and have consumed nothing before completion */
    if (maxStartCompletionsi > 1) {
      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "maxStartCompletionsi=%d is not possible with a terminal", maxStartCompletionsi);
      rci = MARPAESLIF_MATCH_FAILURE;
      rcb = 1;
      goto done;
    }
    /* A terminal matcher NEVER updates the stream : inputs, inputl and eof can be passed as is. */
    if (! _marpaESLIFRecognizer_terminal_matcherb(marpaESLIFRecognizerp,
                                                  symbolp->u.terminalp,
                                                  marpaESLIF_streamp->inputs,
                                                  marpaESLIF_streamp->inputl,
                                                  marpaESLIF_streamp->eofb,
                                                  &rci,
                                                  &marpaESLIFValueResult,
                                                  NULL /* matchedLengthlp */)) {
      goto err;
    }
    switch (rci) {
    case MARPAESLIF_MATCH_AGAIN:
      /* We have to load more unless already at EOF */
      if (! marpaESLIF_streamp->eofb) {
        if (! _marpaESLIFRecognizer_readb(marpaESLIFRecognizerp)) {
          /* We will return -1 */
          goto fatal;
        } else {
          goto match_again;
        }
      }
      break;
    case MARPAESLIF_MATCH_OK:
      /* A terminal matcher completes once only per definition */
      numberOfStartCompletionsi = 1;
      break;
    default:
      break;
    }

    break;
 
  case MARPAESLIF_SYMBOL_TYPE_META:
    /* A meta matcher calls recursively other recognizers, reading new data, etc... : this will update current recognizer inputs, inputl and eof. */
    /* The result will be a parse tree value, at indice 0 of outputStackp */
    if (! _marpaESLIFRecognizer_meta_matcherb(marpaESLIFRecognizerp,
                                              symbolp,
                                              &rci,
                                              &marpaESLIFValueResult,
                                              NULL /* isExhaustedbp */,
                                              maxStartCompletionsi,
                                              &lastSizeBeforeCompletionl,
                                              &numberOfStartCompletionsi)) {
      goto err;
    }
    break;
  default:
    MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "Unknown symbol type %d", symbolp->type);
    goto err;
  }
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "After %s try: eofb=%d, inputl=%ld", symbolp->descp->asciis, (int) marpaESLIF_streamp->eofb, marpaESLIF_streamp->inputl);

  /* If there is match, value type cannot be anything else but MARPAESLIF_VALUE_TYPE_ARRAY */
  MARPAESLIF_CHECK_MATCH_RESULT(funcs, marpaESLIFRecognizerp, marpaESLIF_streamp->inputs, symbolp, rci, marpaESLIFValueResult);

  if (rci == MARPAESLIF_MATCH_OK) {
    if (marpaESLIFValueResultp != NULL) {
      *marpaESLIFValueResultp = marpaESLIFValueResult;
    } else {
      /* Guaranteed to be non-NULL via MARPAESLIF_CHECK_MATCH_RESULT() macro */
      free(marpaESLIFValueResult.u.a.p);
    }
    if (lastSizeBeforeCompletionlp != NULL) {
      *lastSizeBeforeCompletionlp = lastSizeBeforeCompletionl;
    }
    if (numberOfStartCompletionsip != NULL) {
      *numberOfStartCompletionsip = numberOfStartCompletionsi;
    }
  }
  if (rcip != NULL) {
    *rcip = rci;
  }
  rcb = 1;
  goto done;

 fatal:
  rcb = -1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

#ifndef MARPAESLIF_NTRACE
/*****************************************************************************/
static void _marpaESLIF_tconvTraceCallback(void *userDatavp, const char *msgs)
/*****************************************************************************/
{
  static const char *funcs  = "_marpaESLIF_tconvTraceCallback";
  marpaESLIF_t *marpaESLIFp = (marpaESLIF_t *) userDatavp;

  MARPAESLIF_TRACEF(marpaESLIFp, funcs, "%s", msgs);
}
#endif

/*****************************************************************************/
static inline char *_marpaESLIF_utf82printableascii_newp(marpaESLIF_t *marpaESLIFp, char *descs, size_t descl)
/*****************************************************************************/
{
  static const char      *funcs  = "_marpaESLIF_utf82printableascii_newp";
  size_t                  asciil;
  char                   *p;
  char                   *asciis;
  unsigned char           c;

  asciis = _marpaESLIF_charconvb(marpaESLIFp, "ASCII//TRANSLIT//IGNORE", (char *) MARPAESLIF_UTF8_STRING, descs, descl, &asciil, NULL /* fromEncodingsp */, NULL /* tconvpp */, 1 /* eofb */, NULL /* byteleftsp */, NULL /* byteleftlp */, NULL /* byteleftalloclp */, 1 /* tconvsilentb */);
  if (asciis == NULL) {
    asciis = (char *) _marpaESLIF_utf82printableascii_defaultp;
    asciil = strlen(asciis);
  } else {
    /* We are doing this only on descriptions - which are always small amount of bytes  */
    /* (will the user ever write a description taking megabytes !?). Therefore if it ok */
    /* to remove by hand bom and realloc if necessary.                                  */

    /* Remove by hand any ASCII character not truely printable.      */
    /* Only the historical ASCII table [0-127] is a portable thingy. */
    p = asciis;
    while ((c = (unsigned char) *p) != '\0') {
      if ((c >= 128) || (! isprint(c & 0xFF))) {
        *p = ' ';
      }
      p++;
    }
  }

  /* MARPAESLIF_TRACEF(marpaESLIFp, funcs, "return \"%s\"", asciis); */
  return asciis;
}

/*****************************************************************************/
static inline void _marpaESLIF_utf82printableascii_freev(char *asciis)
/*****************************************************************************/
{
  if ((asciis != NULL) && (asciis != _marpaESLIF_utf82printableascii_defaultp)) {
    free(asciis);
  }
}

/*****************************************************************************/
static inline char *_marpaESLIF_charconvb(marpaESLIF_t *marpaESLIFp, char *toEncodings, char *fromEncodings, char *srcs, size_t srcl, size_t *dstlp, char **fromEncodingsp, tconv_t *tconvpp, short eofb, char **byteleftsp, size_t *byteleftlp, size_t *byteleftalloclp, short tconvsilentb)
/*****************************************************************************/
/* The default is to work with the input as given in the arguments. This is the most efficient. */
/* Sometimes some bytes remained left over from a previous round. In this case, we have to prepend */
/* them to the area given in the parameters. Then we will work with a private area. */
/*****************************************************************************/
/* _marpaESLIF_charconvb is ALWAYS returning a non-NULL pointer in case of success (it allocates always one byte more, and put a NUL in it). */
/* Still, the number of converted bytes remain correct. */
/*****************************************************************************/
/* If caller provides byteleftsp != NULL he is RESPONSIBLE to also provide byteleftlp != NULL and byteleftalloclp != NULL */
{
  static const char *funcs       = "_marpaESLIF_charconvb";
  char              *outbuforigp = NULL;
  size_t             outbuforigl = 0;
  tconv_option_t     tconvOption = { NULL /* charsetp */, NULL /* convertp */, NULL /* traceCallbackp */, NULL /* traceUserDatavp */ };
  tconv_t            tconvp      = NULL;
  char              *tmps;
  char              *inbuforigp;
  size_t             inleftorigl;
  char              *bytelefts;
  size_t             byteleftl;
  size_t             byteleftallocl;
  char              *inbufp;
  size_t             inleftl;
  char              *outbufp;
  size_t             outleftl;
  size_t             nconvl;
  size_t             tmpoutbuforigl;

  if (byteleftsp != NULL) {
    bytelefts      = *byteleftsp;
    byteleftl      = *byteleftlp;
    byteleftallocl = *byteleftalloclp;

    if (byteleftl > 0) {
      /* By definition, then, byteleftsp is != NULL : this will be a realloc, eventually */
      if (byteleftallocl < (byteleftl + srcl)) {
	tmps = (char *) realloc(bytelefts, byteleftl + srcl);
	if (tmps == NULL) {
          MARPAESLIF_ERRORF(marpaESLIFp, "realloc failure, %s", strerror(errno));
          goto err;
	}
	bytelefts = tmps;
	byteleftallocl = byteleftl + srcl;
      }
      memcpy(bytelefts + byteleftl, srcs, srcl);
      byteleftl += srcl;

      inbuforigp = bytelefts;
      inleftorigl = byteleftl;
    } else {
      /* No byte left: we work directly on user's buffer */
      inbuforigp  = srcs;
      inleftorigl = srcl;
    }
  } else {
    /* No byte left supported: we work directly on user's buffer */
    inbuforigp  = srcs;
    inleftorigl = srcl;
  }

#ifndef MARPAESLIF_NTRACE
  tconvOption.traceCallbackp  = _marpaESLIF_tconvTraceCallback;
  tconvOption.traceUserDatavp = marpaESLIFp;
#endif
  if (tconvpp != NULL) {
    tconvp = *tconvpp;
  }
  if (tconvp == NULL) {
    tconvp = tconv_open_ext(toEncodings, fromEncodings, &tconvOption);
    if (tconvp == (tconv_t)-1) {
      MARPAESLIF_ERRORF(marpaESLIFp, "tconv_open failure, %s", strerror(errno));
      /* Well, I use NULL instead of -1 */
      tconvp = NULL;
      goto err;
    }
  }
#ifndef MARPAESLIF_NTRACE
  tconv_trace_on(tconvp);
#endif

  /* We start with an output buffer of the same size of input buffer.                  */
  /* Whatever the destination encoding, we always reserve one byte more to place a NUL */
  /* just in case. This NUL is absolutetly harmless but is usefull if one want to look */
  /* at the variables via a debugger -;.                                               */
  /* It is more than useful when the destination encoding is ASCII: string will be NUL */
  /* terminated by default.                                                            */
  outbuforigp = (char *) malloc(srcl + 1);
  if (outbuforigp == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
    goto err;
  }
  /* This setting is NOT necessary because *outbufp is always set to '\0' as well. But */
  /* I do this just to ease inspection in a debugger. */
  outbuforigp[srcl] = '\0';
  outbuforigl = srcl;

  /* We want to translate descriptions in trace or error cases - these are short things, and */
  /* it does not really harm if we redo the whole translation stuff in case of E2BIG:        */
  /* - in trace mode it is documented that performance is awful                              */
  /* - in error mode this call will happen once                                              */

  inbufp   = inbuforigp;
  inleftl  = inleftorigl;
  outbufp  = outbuforigp;
  outleftl = outbuforigl;
  
  while (1) {
  again:
    nconvl = tconv(tconvp, (inbufp != NULL) ? &inbufp : NULL, &inleftl, &outbufp, &outleftl);

    if (nconvl == (size_t) -1) {
      char  *tmps;
      size_t outleftdeltal;
      size_t outbufdeltal;

      switch (errno) {
      case EINVAL:
        /* Malformed multibyte character but eof of conversion buffer - this is not fatal unless we are ourself at eof! */
        if (eofb) {
          errno = EILSEQ;
	  if (! tconvsilentb) {
	    MARPAESLIF_ERRORF(marpaESLIFp, "tconv failure, %s", tconv_error(tconvp));
	  }
          goto err;
        } else {
          goto end_of_loop;
        }
        break; /* Code never reach but this is ok */
      case E2BIG:
        /* Try to alloc more. outleftdeltal is the number of bytes added to output buffer */
        /* Default is to double allocate space, else use arbitrarily 1023 bytes (because of the +1 for the hiden NUL byte)*/
        outleftdeltal = (outbuforigl > 0) ? outbuforigl : 1023;
        tmpoutbuforigl = outbuforigl;
        outbuforigl += outleftdeltal;
        /* Will this ever happen ? */
        if (outbuforigl < tmpoutbuforigl) {
          MARPAESLIF_ERROR(marpaESLIFp, "size_t flip");
          goto err;
        }
        /* Make outbuforigl a mulitple of 1024 (-1 because of the +1 below) */
        /* outbuforigl = MARPAESLIF_CHUNKED_SIZE_UPPER(outbuforigl, 1024) - 1; */
        /* Remember current position in output buffer so that we can repostion after the realloc */
        outbufdeltal = outbufp - outbuforigp; /* Always >= 0 */
        /* Note the "+ 1" */
        tmps = realloc(outbuforigp, outbuforigl + 1); /* Still the +1 to put a NUL just to ease debug of UTF-8 but also its makes sure that ASCII string are ALWAYS NUL terminated */
        if (tmps == NULL) {
          MARPAESLIF_ERRORF(marpaESLIFp, "realloc failure, %s", strerror(errno));
          goto err;
        }
        outbuforigp              = tmps;
        outbuforigp[outbuforigl] = '\0';
        outleftl                += outleftdeltal;
        outbufp                  = outbuforigp + outbufdeltal;
        goto again;
      default:
        /* Unsupported error code - this is fatal (includes EILSEQ of course) */
	if (! tconvsilentb) {
	  MARPAESLIF_ERRORF(marpaESLIFp, "tconv failure, %s", tconv_error(tconvp));
	}
	goto err;
      }
    }

    if (inbufp == NULL) {
      /* This was the last round */
      break;
    }

    if (inleftl <= 0) {
      /* Nothing left in input buffer. */
      if (tconvpp == NULL) {
        /* Caller does not want to know about tconvp: we flush */
        inbufp = NULL;
      } else {
        /* Caller wants to remember. He is responsible to call for flush */
        break;
      }
    }
  }

 end_of_loop:
  /* Remember that we ALWAYS allocate one byte more. This mean that outbufp points exactly at this extra byte */
  *outbufp = '\0';

  if (fromEncodingsp != NULL) {
    if (fromEncodings != NULL) {
      if (fromEncodings == MARPAESLIF_UTF8_STRING) {
        /* Internal constant used to avoid unnecessary strdup() call */
        *fromEncodingsp = (char *) MARPAESLIF_UTF8_STRING;
      } else {
        if ((*fromEncodingsp = strdup(fromEncodings)) == NULL) {
          MARPAESLIF_ERRORF(marpaESLIFp, "strdup failure, %s", strerror(errno));
          goto err;
        }
      }
    } else {
      /* Get the guess from tconv */
      if ((*fromEncodingsp = tconv_fromcode(tconvp)) == NULL) {
        /* Should never happen */
	MARPAESLIF_ERROR(marpaESLIFp, "tconv returned a NULL origin encoding");
        errno = EINVAL;
	goto err;
      }
      MARPAESLIF_TRACEF(marpaESLIFp, funcs, "Encoding guessed to %s", *fromEncodingsp);
      /* We do not mind if we loose the original - it is inside tconv that will be freed */
      if ((*fromEncodingsp = strdup(*fromEncodingsp)) == NULL) {
	MARPAESLIF_ERRORF(marpaESLIFp, "strdup failure, %s", strerror(errno));
	goto err;
      }
    }
  }

  if (byteleftsp != NULL) {
    /* User said we want to keep track of remaining bytes */
    if (byteleftl > 0) {
      /* And we already started with at least one remaining byte: */
      /* the workbuffer was bytelefts, its allocated size does not change, */
      /* the remaining bytes in it in inleftl. */
      byteleftl = inleftl;
      if (byteleftl > 0) {
        /* We want to move unconsumed bytes at the beginning */
        /* so that next round will see them. */
	size_t consumedl = inleftorigl - inleftl;
        memmove(bytelefts, bytelefts + consumedl, byteleftl);
      }
    } else {
      /* And there was nothing to pick from previous round */
      if (inleftl > 0) {
	/* But there is from current round */
	size_t consumedl = inleftorigl - inleftl;
	if (bytelefts == NULL) {
	  /* And this is the first time this happens */
	  bytelefts = (char *) malloc(inleftl);
	  if (bytelefts == NULL) {
	    MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
	    goto err;
	  }
	  memcpy(bytelefts, inbuforigp + consumedl, inleftl);
	  byteleftl = inleftl;
	  byteleftallocl = inleftl;
	} else {
	  /* And this has already happened in a round older than previous round */
	  if (byteleftallocl < inleftl) {
	    tmps = (char *) realloc(bytelefts, inleftl);
	    if (tmps == NULL) {
	      MARPAESLIF_ERRORF(marpaESLIFp, "realloc failure, %s", strerror(errno));
	      goto err;
	    }
	    bytelefts = tmps;
	    byteleftallocl = inleftl;
	  }
	  memcpy(bytelefts, inbuforigp + consumedl, inleftl);
	  byteleftl = inleftl;
	}
      }
    }
  }

  if (dstlp != NULL) {
    *dstlp = outbufp - outbuforigp;
  }
  goto done;

 err:
  /* If errno is EILSEQ then input pointer should be at the last successful converted location */
  if ((tconvp != NULL) && (errno == EILSEQ) && (inbuforigp != NULL) && (inleftorigl > 0) && (! tconvsilentb)) {
    char                   *fromCodes = tconv_fromcode(tconvp);
    size_t                  consumedl = inleftorigl - inleftl;
    /* We have to fake a recognizer - this is how the MARPAESLIF_HEXDUMPV() macros works */
    marpaESLIFRecognizer_t  marpaESLIFRecognizer;
    marpaESLIFRecognizer_t *marpaESLIFRecognizerp = &marpaESLIFRecognizer;

    marpaESLIFRecognizerp->marpaESLIFp = marpaESLIFp;

    /* If there is some information before, show it */
    if (consumedl > 0) {
      char  *dumps;
      size_t dumpl;

      if (consumedl > 128) {
        dumps = inbufp - 128;
        dumpl = 128;
      } else {
        dumps = inbuforigp;
        dumpl = consumedl;
      }
      MARPAESLIF_HEXDUMPV(marpaESLIFRecognizerp,
                          (fromCodes != NULL) ? fromCodes : "", /* In theory, it is impossible to have fromCodes == NULL here */
                          " data before the failure",
                          dumps,
                          dumpl,
                          0 /* traceb */);
    }
    MARPAESLIF_ERROR(marpaESLIFp, "<<<<<< CHARACTER FAILURE HERE: >>>>>>");
    /* If there is some information after, show it */
    if (inleftl > 0) {
      char  *dumps;
      size_t dumpl;

      dumps = inbuforigp + consumedl;
      dumpl = inleftl > 128 ? 128 : inleftl;
      MARPAESLIF_HEXDUMPV(marpaESLIFRecognizerp,
                          (fromCodes != NULL) ? fromCodes : "", /* In theory, it is impossible to have fromCodes == NULL here */
                          " data after the failure",
                          dumps,
                          dumpl,
                          0 /* traceb */);
    }
  }
  if (outbuforigp != NULL) {
    free(outbuforigp);
  }
  outbuforigp = NULL;

 done:
  if (tconvpp != NULL) {
    *tconvpp = tconvp;
  } else {
    if (tconvp != NULL) {
      if (tconv_close(tconvp) != 0) {
        MARPAESLIF_ERRORF(marpaESLIFp, "tconv_close failure, %s", strerror(errno));
      }
    }
  }
  if (byteleftsp != NULL) {
    /* Note that in case of an error we do not mind if *byteleftlp is NOT correct: the processing will stop anwyay */
    *byteleftsp      = bytelefts;
    *byteleftlp      = byteleftl;
    *byteleftalloclp = byteleftallocl;
  }

  /* MARPAESLIF_TRACEF(marpaESLIFp, funcs, "return %p", outbuforigp); */
  return outbuforigp;
}

/*****************************************************************************/
marpaESLIFGrammar_t *marpaESLIFGrammar_newp(marpaESLIF_t *marpaESLIFp, marpaESLIFGrammarOption_t *marpaESLIFGrammarOptionp)
/*****************************************************************************/
{
  if (marpaESLIFp == NULL) {
    errno = EINVAL;
    return NULL;
  }
  
  return _marpaESLIFGrammar_newp(marpaESLIFp, marpaESLIFGrammarOptionp, NULL /* marpaESLIfGrammarPreviousp */, 0 /* unsafeb */);
}

/*****************************************************************************/
marpaESLIFGrammar_t *marpaESLIFGrammar_unsafe_newp(marpaESLIF_t *marpaESLIFp, marpaESLIFGrammarOption_t *marpaESLIFGrammarOptionp)
/*****************************************************************************/
{
  if (marpaESLIFp == NULL) {
    errno = EINVAL;
    return NULL;
  }
  
  return _marpaESLIFGrammar_newp(marpaESLIFp, marpaESLIFGrammarOptionp, NULL /* marpaESLIfGrammarPreviousp */, 1 /* unsafeb */);
}

/*****************************************************************************/
static inline marpaESLIFGrammar_t *_marpaESLIFGrammar_newp(marpaESLIF_t *marpaESLIFp, marpaESLIFGrammarOption_t *marpaESLIFGrammarOptionp, marpaESLIFGrammar_t *marpaESLIfGrammarPreviousp, short unsafeb)
/*****************************************************************************/
{
  static const char                *funcs                      = "_marpaESLIFGrammar_newp";
  marpaESLIF_t                     *marpaESLIFTmpp             = NULL;
  marpaESLIFRecognizerOption_t      marpaESLIFRecognizerOption = marpaESLIFRecognizerOption_default_template;
  marpaESLIFValueOption_t           marpaESLIFValueOption      = marpaESLIFValueOption_default_template;
  marpaESLIFGrammar_t              *marpaESLIFGrammarp         = NULL;
  marpaESLIF_readerContext_t        marpaESLIF_readerContext;
  int                               grammari;
  marpaESLIF_grammar_t             *grammarp;
  genericStack_t                   *symbolStackp;
  int                               symboli;
  marpaESLIF_symbol_t              *symbolp;
  marpaESLIF_meta_t                *metap;

  if (! unsafeb) {
    MARPAESLIF_TRACE(marpaESLIFp, funcs, "Cloning ESLIF");
    /* We say that ESLIF is thread-safe, this is entirely true if building a grammar from it is thread-safe */
    /* thus we have to clone ESLIF, because this is itself a grammar, and we will use a recognizer from it */
    marpaESLIFTmpp = _marpaESLIF_newp(&(marpaESLIFp->marpaESLIFOption), 1 /* validateb */);
    if (marpaESLIFTmpp == NULL) {
      goto err;
    }
    MARPAESLIF_TRACE(marpaESLIFTmpp, funcs, "Building Grammar using ESLIF clone");
  } else {
    marpaESLIFTmpp = marpaESLIFp;
    MARPAESLIF_TRACE(marpaESLIFTmpp, funcs, "Building Grammar using ESLIF");
  }

  if (marpaESLIFGrammarOptionp == NULL) {
    MARPAESLIF_ERROR(marpaESLIFTmpp, "marpaESLIFGrammarOptionp must be set");
    goto err;
  }

  if (marpaESLIFGrammarOptionp->bytep == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFTmpp, funcs, "Null source pointer");
    goto err;
  }

  if (marpaESLIfGrammarPreviousp == NULL) {
    marpaESLIFGrammarp = (marpaESLIFGrammar_t *) malloc(sizeof(marpaESLIFGrammar_t));
    if (marpaESLIFGrammarp == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFTmpp, "malloc failure, %s", strerror(errno));
      goto err;
    }
    marpaESLIFGrammarp->marpaESLIFp             = marpaESLIFTmpp;
    marpaESLIFGrammarp->marpaESLIFGrammarOption = *marpaESLIFGrammarOptionp;
    marpaESLIFGrammarp->grammarStackp           = NULL;
    marpaESLIFGrammarp->grammarp                = NULL;
    marpaESLIFGrammarp->warningIsErrorb         = 0;
    marpaESLIFGrammarp->warningIsIgnoredb       = 0;
    marpaESLIFGrammarp->autorankb               = 0;
    marpaESLIFGrammarp->luabytep                = NULL;
    marpaESLIFGrammarp->luabytel                = 0;
    marpaESLIFGrammarp->luaprecompiledp         = NULL;
    marpaESLIFGrammarp->luaprecompiledl         = 0;
    marpaESLIFGrammarp->luadescp                = NULL;
    marpaESLIFGrammarp->internalRuleCounti      = 0;
  } else {
    marpaESLIFGrammarp = marpaESLIfGrammarPreviousp;
  }

  /* Our internal grammar reader callback */
  marpaESLIF_readerContext.marpaESLIFp              = marpaESLIFTmpp;
  marpaESLIF_readerContext.marpaESLIFGrammarOptionp = marpaESLIFGrammarOptionp;

  /* Overwrite things not setted in the template, or with which we want a change */
  marpaESLIFRecognizerOption.userDatavp        = (void *) &marpaESLIF_readerContext;
  marpaESLIFRecognizerOption.readerCallbackp   = _marpaESLIFReader_grammarReader;
  marpaESLIFRecognizerOption.disableThresholdb = 1; /* No threshold warning when parsing a grammar */
  marpaESLIFRecognizerOption.newlineb          = 1; /* Grammars are short - we can count line/columns numbers */
  marpaESLIFRecognizerOption.trackb            = 0; /* Track absolute position - recognizer is never accessible at this stage */

  marpaESLIFValueOption.userDatavp            = (void *) marpaESLIFGrammarp; /* Used by _marpaESLIF_bootstrap_freeCallbackv and statement rule actions */
  marpaESLIFValueOption.ruleActionResolverp   = _marpaESLIF_bootstrap_ruleActionResolver;
  marpaESLIFValueOption.symbolActionResolverp = NULL; /* We use ::transfer */

  /* Parser will automatically create marpaESLIFValuep and assign an internal recognizer to its userDatavp */
  /* The value of our internal parser is a grammar stack */
  if (! _marpaESLIFGrammar_parseb(marpaESLIFTmpp->marpaESLIFGrammarp,
                                  &marpaESLIFRecognizerOption,
                                  &marpaESLIFValueOption,
                                  0, /* discardb */
                                  1, /* noEventb - no effect anyway since our internal grammar have no event indeed -; */
                                  0, /* silentb */
                                  NULL, /* marpaESLIFRecognizerParentp */
                                  NULL, /* isExhaustedbp */
                                  NULL, /* marpaESLIFValueResultp */
                                  0, /* maxStartCompletionsi */
                                  NULL, /* lastSizeBeforeCompletionlp */
                                  NULL /* numberOfStartCompletionsip */,
                                  0 /* grammarIsOnStackb */)) {
    goto err;
  }

  /* The result is directly stored in the context - validate it */
  if (! _marpaESLIFGrammar_validateb(marpaESLIFGrammarp)) {
    goto err;
  }
  /* Put in current grammar the first from the grammar stack */
  for (grammari = 0; grammari < GENERICSTACK_USED(marpaESLIFGrammarp->grammarStackp); grammari++) {
    if (! GENERICSTACK_IS_PTR(marpaESLIFGrammarp->grammarStackp, grammari)) {
      /* Sparse array -; */
      continue;
    }
    marpaESLIFGrammarp->grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(marpaESLIFGrammarp->grammarStackp, grammari);
    break;
  }

  if (! unsafeb) {
    /* Success. We have to take care of one thing: the new grammar maintains a pointer to its parent's ESLIF */
    marpaESLIFGrammarp->marpaESLIFp = marpaESLIFp;
    /* This applies to any argument of type marpaESLIFGrammar_t in the grammar */
    for (grammari = 0; grammari < GENERICSTACK_USED(marpaESLIFGrammarp->grammarStackp); grammari++) {
      if (! GENERICSTACK_IS_PTR(marpaESLIFGrammarp->grammarStackp, grammari)) {
        /* Sparse array -; */
        continue;
      }
      grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(marpaESLIFGrammarp->grammarStackp, grammari);
      symbolStackp = grammarp->symbolStackp;
      for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
        MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);
        if (symbolp->type != MARPAESLIF_SYMBOL_TYPE_META) {
          continue;
        }
        metap = symbolp->u.metap;
        if (metap->marpaESLIFGrammarLexemeClonep != NULL) {
          metap->marpaESLIFGrammarLexemeClonep->marpaESLIFp = marpaESLIFp;
        }
      }
    }
  }
  
  goto done;

 err:
  /* We do not want to free it, if it was injected: parent should take care of that */
  if (marpaESLIfGrammarPreviousp == NULL) {
    marpaESLIFGrammar_freev(marpaESLIFGrammarp);
  }
  marpaESLIFGrammarp = NULL;

 done:
  if (! unsafeb) {
    if (marpaESLIFTmpp != NULL) {
      marpaESLIF_freev(marpaESLIFTmpp);
    }
  }

  return marpaESLIFGrammarp;
}

/*****************************************************************************/
marpaESLIF_t *marpaESLIFGrammar_eslifp(marpaESLIFGrammar_t *marpaESLIFGrammarp)
/*****************************************************************************/
{
  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return NULL;
  }

  return marpaESLIFGrammarp->marpaESLIFp;
}

/*****************************************************************************/
marpaESLIFGrammarOption_t *marpaESLIFGrammar_optionp(marpaESLIFGrammar_t *marpaESLIFGrammarp)
/*****************************************************************************/
{
  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return NULL;
  }

  return &(marpaESLIFGrammarp->marpaESLIFGrammarOption);
}

/*****************************************************************************/
short marpaESLIFGrammar_grammar_currentb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int *levelip, marpaESLIFString_t **descpp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = marpaESLIFGrammarp->grammarp;

  if (grammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return marpaESLIFGrammar_grammar_by_levelb(marpaESLIFGrammarp, grammarp->leveli, NULL /* descp */, levelip, descpp);
}

/*****************************************************************************/
short marpaESLIFGrammar_grammar_by_levelb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int leveli, marpaESLIFString_t *descp, int *levelip, marpaESLIFString_t **descpp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = _marpaESLIFGrammar_grammar_findp(marpaESLIFGrammarp, leveli, descp);
  if (grammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  if (levelip != NULL) {
    *levelip = grammarp->leveli;
  }
  if (descpp != NULL) {
    *descpp = grammarp->descp;
  }

  return 1;
}

/*****************************************************************************/
short marpaESLIFGrammar_rulearray_currentb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int **ruleipp, size_t *rulelp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = marpaESLIFGrammarp->grammarp;

  if (grammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return marpaESLIFGrammar_rulearray_by_levelb(marpaESLIFGrammarp, ruleipp, rulelp, grammarp->leveli, NULL /* descp */);
}

/*****************************************************************************/
short marpaESLIFGrammar_rulearray_by_levelb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int **ruleipp, size_t *rulelp, int leveli, marpaESLIFString_t *descp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;
  short                 rcb;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = _marpaESLIFGrammar_grammar_findp(marpaESLIFGrammarp, leveli, descp);
  if (grammarp == NULL) {
    goto err;
  }

  if (ruleipp != NULL) {
    *ruleipp = grammarp->ruleip;
  }
  if (rulelp != NULL) {
    *rulelp = grammarp->nrulel;
  }
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIFGrammar_symbolarray_currentb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int **symbolipp, size_t *symbollp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = marpaESLIFGrammarp->grammarp;

  if (grammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return marpaESLIFGrammar_symbolarray_by_levelb(marpaESLIFGrammarp, symbolipp, symbollp, grammarp->leveli, NULL /* descp */);
}

/*****************************************************************************/
short marpaESLIFGrammar_symbolarray_by_levelb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int **symbolipp, size_t *symbollp, int leveli, marpaESLIFString_t *descp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;
  short                 rcb;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = _marpaESLIFGrammar_grammar_findp(marpaESLIFGrammarp, leveli, descp);
  if (grammarp == NULL) {
    goto err;
  }

  if (symbolipp != NULL) {
    *symbolipp = grammarp->symbolip;
  }
  if (symbollp != NULL) {
    *symbollp = grammarp->nsymboll;
  }
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIFGrammar_grammarproperty_currentb(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaESLIFGrammarProperty_t *grammarPropertyp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = marpaESLIFGrammarp->grammarp;

  if (grammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return marpaESLIFGrammar_grammarproperty_by_levelb(marpaESLIFGrammarp, grammarPropertyp, grammarp->leveli, NULL);
}

/*****************************************************************************/
short marpaESLIFGrammar_grammarproperty_by_levelb(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaESLIFGrammarProperty_t *grammarPropertyp, int leveli, marpaESLIFString_t *descp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;
  genericStack_t       *grammarStackp;
  short                 rcb;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = _marpaESLIFGrammar_grammar_findp(marpaESLIFGrammarp, leveli, descp);
  if (grammarp == NULL) {
    goto err;
  }
  grammarStackp = marpaESLIFGrammarp->grammarStackp;
 
  if (grammarPropertyp != NULL) {
    grammarPropertyp->leveli               = grammarp->leveli;
    grammarPropertyp->maxLeveli            = GENERICSTACK_USED(grammarStackp) - 1; /* Per def it is > 0 here */
    grammarPropertyp->descp                = grammarp->descp;
    grammarPropertyp->latmb                = grammarp->latmb;
    grammarPropertyp->defaultSymbolActionp = grammarp->defaultSymbolActionp;
    grammarPropertyp->defaultRuleActionp   = grammarp->defaultRuleActionp;
    grammarPropertyp->starti               = grammarp->starti;
    grammarPropertyp->discardi             = grammarp->discardi;
    grammarPropertyp->nrulel               = grammarp->nrulel;
    grammarPropertyp->ruleip               = grammarp->ruleip;
    grammarPropertyp->nsymboll             = grammarp->nsymboll;
    grammarPropertyp->symbolip             = grammarp->symbolip;
  }
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIFGrammar_ruleproperty_currentb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int rulei, marpaESLIFRuleProperty_t *rulePropertyp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = marpaESLIFGrammarp->grammarp;

  if (grammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return marpaESLIFGrammar_ruleproperty_by_levelb(marpaESLIFGrammarp, rulei, rulePropertyp, grammarp->leveli, NULL /* descp */);
}

/*****************************************************************************/
short marpaESLIFGrammar_ruleproperty_by_levelb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int rulei, marpaESLIFRuleProperty_t *rulePropertyp, int leveli, marpaESLIFString_t *descp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;
  marpaESLIF_rule_t    *rulep;
  short                 rcb;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = _marpaESLIFGrammar_grammar_findp(marpaESLIFGrammarp, leveli, descp);
  if (grammarp == NULL) {
    goto err;
  }
  rulep = _marpaESLIF_rule_findp(marpaESLIFGrammarp->marpaESLIFp, grammarp, rulei);
  if (rulep == NULL) {
    goto err;
  }
 
  if (rulePropertyp != NULL) {
    rulePropertyp->idi            = rulep->idi;
    rulePropertyp->descp          = rulep->descp;
    rulePropertyp->asciishows     = rulep->asciishows;
    rulePropertyp->lhsi           = rulep->lhsp->idi;    /* rulep->lhsp is never NULL */
    rulePropertyp->separatori     = (rulep->separatorp != NULL) ? rulep->separatorp->idi : -1;
    rulePropertyp->rhsip          = rulep->rhsip;
    rulePropertyp->nrhsl          = (size_t) GENERICSTACK_USED(rulep->rhsStackp); /* Can be == 0 */
    rulePropertyp->rhsip          = rulep->rhsip;  /* Can be NULL if nullable */
    rulePropertyp->skipbp         = rulep->skipbp;  /* Can be NULL if nullable or a sequence */
    rulePropertyp->exceptioni     = (rulep->exceptionp != NULL) ? rulep->exceptionp->idi : -1;
    rulePropertyp->actionp        = rulep->actionp; /* Can be NULL */
    rulePropertyp->discardEvents  = rulep->discardEvents; /* Can be NULL */
    rulePropertyp->discardEventb  = rulep->discardEventb;
    rulePropertyp->ranki          = rulep->ranki;
    rulePropertyp->nullRanksHighb = rulep->nullRanksHighb;
    rulePropertyp->sequenceb      = rulep->sequenceb;
    rulePropertyp->properb        = rulep->properb;
    rulePropertyp->minimumi       = rulep->minimumi;
    rulePropertyp->internalb      = rulep->passthroughb;  /* Currently only passthrough rules are internal */
    /* I could have copied rulep->propertyBitSet directly, though I believe the code is more maintanable */
    /* and extensible doing that bit per bit. */
    rulePropertyp->propertyBitSet = 0;
    if ((rulep->propertyBitSet & MARPAWRAPPER_RULE_IS_ACCESSIBLE) == MARPAWRAPPER_RULE_IS_ACCESSIBLE) { rulePropertyp->propertyBitSet |= MARPAESLIF_RULE_IS_ACCESSIBLE; }
    if ((rulep->propertyBitSet & MARPAWRAPPER_RULE_IS_NULLABLE)   == MARPAWRAPPER_RULE_IS_NULLABLE  ) { rulePropertyp->propertyBitSet |= MARPAESLIF_RULE_IS_NULLABLE; }
    if ((rulep->propertyBitSet & MARPAWRAPPER_RULE_IS_NULLING)    == MARPAWRAPPER_RULE_IS_NULLING   ) { rulePropertyp->propertyBitSet |= MARPAESLIF_RULE_IS_NULLING; }
    if ((rulep->propertyBitSet & MARPAWRAPPER_RULE_IS_LOOP)       == MARPAWRAPPER_RULE_IS_LOOP      ) { rulePropertyp->propertyBitSet |= MARPAESLIF_RULE_IS_LOOP; }
    if ((rulep->propertyBitSet & MARPAWRAPPER_RULE_IS_PRODUCTIVE) == MARPAWRAPPER_RULE_IS_PRODUCTIVE) { rulePropertyp->propertyBitSet |= MARPAESLIF_RULE_IS_PRODUCTIVE; }
    rulePropertyp->hideseparatorb = rulep->hideseparatorb;
  }
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIFGrammar_ruledisplayform_currentb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int rulei, char **ruledisplaysp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = marpaESLIFGrammarp->grammarp;

  if (grammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return marpaESLIFGrammar_ruledisplayform_by_levelb(marpaESLIFGrammarp, rulei, ruledisplaysp, grammarp->leveli, NULL /* descp */);
}

/*****************************************************************************/
short marpaESLIFGrammar_ruledisplayform_by_levelb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int rulei, char **ruledisplaysp, int leveli, marpaESLIFString_t *descp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;
  marpaESLIF_rule_t    *rulep;
  short                 rcb;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = _marpaESLIFGrammar_grammar_findp(marpaESLIFGrammarp, leveli, descp);
  if (grammarp == NULL) {
    goto err;
  }
  rulep = _marpaESLIF_rule_findp(marpaESLIFGrammarp->marpaESLIFp, grammarp, rulei);
  if (rulep == NULL) {
    goto err;
  }
 
  if (ruledisplaysp != NULL) {
    *ruledisplaysp = rulep->descp->asciis;
  }
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIFGrammar_grammarshowform_currentb(marpaESLIFGrammar_t *marpaESLIFGrammarp, char **grammarshowsp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = marpaESLIFGrammarp->grammarp;

  if (grammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return marpaESLIFGrammar_grammarshowform_by_levelb(marpaESLIFGrammarp, grammarshowsp, grammarp->leveli, NULL /* descp */);
}

/*****************************************************************************/
short marpaESLIFGrammar_grammarshowform_by_levelb(marpaESLIFGrammar_t *marpaESLIFGrammarp, char **grammarshowsp, int leveli, marpaESLIFString_t *descp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;
  short                 rcb;
  size_t                asciishowl;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = _marpaESLIFGrammar_grammar_findp(marpaESLIFGrammarp, leveli, descp);
  if (grammarp == NULL) {
    goto err;
  }
 
  if (grammarshowsp != NULL) {
    /* Grammar show is delayed until requested because it have a cost -; */
    if (grammarp->asciishows == NULL) {
      _marpaESLIF_grammar_createshowv(marpaESLIFGrammarp, grammarp, NULL /* asciishows */, &asciishowl);
      grammarp->asciishows = (char *) malloc(asciishowl);
      if (grammarp->asciishows == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFGrammarp->marpaESLIFp, "malloc failure, %s", strerror(errno));
        goto err;
      }
      /* It is guaranteed that asciishowl is >= 1 - c.f. _marpaESLIF_grammar_createshowv() */
      grammarp->asciishows[0] = '\0';
      _marpaESLIF_grammar_createshowv(marpaESLIFGrammarp, grammarp, grammarp->asciishows, NULL);
    }
    *grammarshowsp = grammarp->asciishows;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIFGrammar_grammarshowscriptb(marpaESLIFGrammar_t *marpaESLIFGrammarp, char **grammarscriptsp)
/*****************************************************************************/
{
  char *grammarscripts;
  short rcb;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }
  
  if ((marpaESLIFGrammarp->luadescp == NULL) && (marpaESLIFGrammarp->luabytep != NULL) && (marpaESLIFGrammarp->luabytel > 0)) {
    marpaESLIFGrammarp->luadescp = _marpaESLIF_string_newp(marpaESLIFGrammarp->marpaESLIFp, (char *) MARPAESLIF_UTF8_STRING /* Came from the grammar, we know it is UTF-8 */, marpaESLIFGrammarp->luabytep, marpaESLIFGrammarp->luabytel);
    if (marpaESLIFGrammarp->luadescp == NULL) {
      goto err;
    }
    grammarscripts = marpaESLIFGrammarp->luadescp->asciis;
  } else {
    grammarscripts = (char *) MARPAESLIF_EMPTY_STRING;
  }

  if (grammarscriptsp != NULL) {
    *grammarscriptsp = grammarscripts;
  }
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIFGrammar_ruleshowform_currentb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int rulei, char **ruleshowsp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = marpaESLIFGrammarp->grammarp;

  if (grammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return marpaESLIFGrammar_ruleshowform_by_levelb(marpaESLIFGrammarp, rulei, ruleshowsp, grammarp->leveli, NULL /* descp */);
}

/*****************************************************************************/
short marpaESLIFGrammar_ruleshowform_by_levelb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int rulei, char **ruleshowsp, int leveli, marpaESLIFString_t *descp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;
  marpaESLIF_rule_t    *rulep;
  short                 rcb;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = _marpaESLIFGrammar_grammar_findp(marpaESLIFGrammarp, leveli, descp);
  if (grammarp == NULL) {
    goto err;
  }
  rulep = _marpaESLIF_rule_findp(marpaESLIFGrammarp->marpaESLIFp, grammarp, rulei);
  if (rulep == NULL) {
    goto err;
  }
 
  if (ruleshowsp != NULL) {
    *ruleshowsp = rulep->asciishows;
  }
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIFGrammar_symboldisplayform_currentb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int symboli, char **symboldisplaysp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = marpaESLIFGrammarp->grammarp;

  if (grammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return marpaESLIFGrammar_symboldisplayform_by_levelb(marpaESLIFGrammarp, symboli, symboldisplaysp, grammarp->leveli, NULL /* descp */);
}

/*****************************************************************************/
short marpaESLIFGrammar_symboldisplayform_by_levelb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int symboli, char **symboldisplaysp, int leveli, marpaESLIFString_t *descp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;
  marpaESLIF_symbol_t  *symbolp;
  short                 rcb;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = _marpaESLIFGrammar_grammar_findp(marpaESLIFGrammarp, leveli, descp);
  if (grammarp == NULL) {
    goto err;
  }
  symbolp = _marpaESLIF_symbol_findp(marpaESLIFGrammarp->marpaESLIFp, grammarp, NULL /* asciis */, symboli, NULL /* symbolip */);
  if (symbolp == NULL) {
    goto err;
  }
 
  if (symboldisplaysp != NULL) {
    *symboldisplaysp = symbolp->descp->asciis;
  }
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIFGrammar_symbolproperty_currentb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int symboli, marpaESLIFSymbolProperty_t *marpaESLIFSymbolPropertyp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = marpaESLIFGrammarp->grammarp;

  if (grammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return marpaESLIFGrammar_symbolproperty_by_levelb(marpaESLIFGrammarp, symboli, marpaESLIFSymbolPropertyp, grammarp->leveli, NULL /* descp */);
}

/*****************************************************************************/
short marpaESLIFGrammar_symbolproperty_by_levelb(marpaESLIFGrammar_t *marpaESLIFGrammarp, int symboli, marpaESLIFSymbolProperty_t *marpaESLIFSymbolPropertyp, int leveli, marpaESLIFString_t *descp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t       *grammarp;
  marpaESLIF_symbol_t        *symbolp;
  marpaESLIFSymbolProperty_t  marpaESLIFSymbolProperty;
  short                       rcb;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = _marpaESLIFGrammar_grammar_findp(marpaESLIFGrammarp, leveli, descp);
  if (grammarp == NULL) {
    goto err;
  }
  symbolp = _marpaESLIF_symbol_findp(marpaESLIFGrammarp->marpaESLIFp, grammarp, NULL /* asciis */, symboli, NULL /* symbolip */);
  if (symbolp == NULL) {
    goto err;
  }

  switch (symbolp->type) {
  case MARPAESLIF_SYMBOL_TYPE_TERMINAL:
    marpaESLIFSymbolProperty.type = MARPAESLIF_SYMBOLTYPE_TERMINAL;
    break;
  case MARPAESLIF_SYMBOL_TYPE_META:
    marpaESLIFSymbolProperty.type = MARPAESLIF_SYMBOLTYPE_META;
    break;
  default:
    MARPAESLIF_ERRORF(marpaESLIFGrammarp->marpaESLIFp, "Unsupported symbol type %d", symbolp->type);
    goto err;
  }
  marpaESLIFSymbolProperty.startb               = symbolp->startb;
  marpaESLIFSymbolProperty.discardb             = symbolp->discardb;
  marpaESLIFSymbolProperty.discardRhsb          = symbolp->discardRhsb;
  marpaESLIFSymbolProperty.lhsb                 = symbolp->lhsb;
  marpaESLIFSymbolProperty.topb                 = symbolp->topb;
  marpaESLIFSymbolProperty.idi                  = symbolp->idi;
  marpaESLIFSymbolProperty.descp                = symbolp->descp;
  marpaESLIFSymbolProperty.eventBefores         = symbolp->eventBefores;
  marpaESLIFSymbolProperty.eventBeforeb         = symbolp->eventBeforeb;
  marpaESLIFSymbolProperty.eventAfters          = symbolp->eventAfters;
  marpaESLIFSymbolProperty.eventAfterb          = symbolp->eventAfterb;
  marpaESLIFSymbolProperty.eventPredicteds      = symbolp->eventPredicteds;
  marpaESLIFSymbolProperty.eventPredictedb      = symbolp->eventPredictedb;
  marpaESLIFSymbolProperty.eventNulleds         = symbolp->eventNulleds;
  marpaESLIFSymbolProperty.eventNulledb         = symbolp->eventNulledb;
  marpaESLIFSymbolProperty.eventCompleteds      = symbolp->eventCompleteds;
  marpaESLIFSymbolProperty.eventCompletedb      = symbolp->eventCompletedb;
  marpaESLIFSymbolProperty.discardEvents        = symbolp->discardEvents;
  marpaESLIFSymbolProperty.discardEventb        = symbolp->discardEventb;
  marpaESLIFSymbolProperty.lookupResolvedLeveli = symbolp->lookupResolvedLeveli;
  marpaESLIFSymbolProperty.priorityi            = symbolp->priorityi;
  marpaESLIFSymbolProperty.nullableActionp      = symbolp->nullableActionp;
  marpaESLIFSymbolProperty.symbolActionp        = symbolp->symbolActionp;
  marpaESLIFSymbolProperty.ifActionp            = symbolp->ifActionp;
  /* I could have copied symbolp->propertyBitSet directly, though I believe the code is more maintanable */
  /* and extensible doing that bit per bit. */
  marpaESLIFSymbolProperty.propertyBitSet = 0;
  if ((symbolp->propertyBitSet & MARPAESLIF_SYMBOL_IS_ACCESSIBLE) == MARPAESLIF_SYMBOL_IS_ACCESSIBLE) { marpaESLIFSymbolProperty.propertyBitSet |= MARPAESLIF_SYMBOL_IS_ACCESSIBLE; }
  if ((symbolp->propertyBitSet & MARPAESLIF_SYMBOL_IS_NULLABLE)   == MARPAESLIF_SYMBOL_IS_NULLABLE  ) { marpaESLIFSymbolProperty.propertyBitSet |= MARPAESLIF_SYMBOL_IS_NULLABLE; }
  if ((symbolp->propertyBitSet & MARPAESLIF_SYMBOL_IS_NULLING)    == MARPAESLIF_SYMBOL_IS_NULLING   ) { marpaESLIFSymbolProperty.propertyBitSet |= MARPAESLIF_SYMBOL_IS_NULLING; }
  if ((symbolp->propertyBitSet & MARPAESLIF_SYMBOL_IS_PRODUCTIVE) == MARPAESLIF_SYMBOL_IS_PRODUCTIVE) { marpaESLIFSymbolProperty.propertyBitSet |= MARPAESLIF_SYMBOL_IS_PRODUCTIVE; }
  if ((symbolp->propertyBitSet & MARPAESLIF_SYMBOL_IS_START)      == MARPAESLIF_SYMBOL_IS_START)      { marpaESLIFSymbolProperty.propertyBitSet |= MARPAESLIF_SYMBOL_IS_START; }
  if ((symbolp->propertyBitSet & MARPAESLIF_SYMBOL_IS_TERMINAL)   == MARPAESLIF_SYMBOL_IS_TERMINAL)   { marpaESLIFSymbolProperty.propertyBitSet |= MARPAESLIF_SYMBOL_IS_TERMINAL; }
  marpaESLIFSymbolProperty.eventBitSet = 0;
  if ((symbolp->eventBitSet & MARPAESLIF_SYMBOL_EVENT_COMPLETION) == MARPAESLIF_SYMBOL_EVENT_COMPLETION) { marpaESLIFSymbolProperty.eventBitSet |= MARPAESLIF_SYMBOL_EVENT_COMPLETION; }
  if ((symbolp->eventBitSet & MARPAESLIF_SYMBOL_EVENT_NULLED) == MARPAESLIF_SYMBOL_EVENT_NULLED)         { marpaESLIFSymbolProperty.eventBitSet |= MARPAESLIF_SYMBOL_EVENT_NULLED; }
  if ((symbolp->eventBitSet & MARPAESLIF_SYMBOL_EVENT_PREDICTION) == MARPAESLIF_SYMBOL_EVENT_PREDICTION) { marpaESLIFSymbolProperty.eventBitSet |= MARPAESLIF_SYMBOL_EVENT_PREDICTION; }

  if (marpaESLIFSymbolPropertyp != NULL) {
    *marpaESLIFSymbolPropertyp = marpaESLIFSymbolProperty;
  }
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
marpaESLIFRecognizer_t *marpaESLIFRecognizer_newp(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaESLIFRecognizerOption_t *marpaESLIFRecognizerOptionp)
/*****************************************************************************/
{
  return _marpaESLIFRecognizer_newp(marpaESLIFGrammarp,
                                    marpaESLIFRecognizerOptionp, 0, /* discardb */
                                    0, /* noEventb */
                                    0, /* silentb */
                                    NULL, /* marpaESLIFRecognizerParentp */
                                    0, /* fakeb */
                                    0 /* maxStartCompletionsi */,
                                    0, /* utfb */
                                    0 /* grammmarIsOnStackb */);
}

/*****************************************************************************/
short marpaESLIFRecognizer_shareb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIFRecognizer_t *marpaESLIFRecognizerSharedp)
/*****************************************************************************/
{
  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return _marpaESLIFRecognizer_shareb(marpaESLIFRecognizerp, marpaESLIFRecognizerSharedp);
}

/*****************************************************************************/
marpaESLIF_t *marpaESLIFRecognizer_eslifp(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return NULL;
  }

  return marpaESLIFRecognizerp->marpaESLIFp;
}

/*****************************************************************************/
short marpaESLIFRecognizer_scanb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short initialEventsb, short *continuebp, short *isExhaustedbp)
/*****************************************************************************/
{
  static const char *funcs = "marpaESLIFRecognizer_scanb";
  marpaESLIF_t      *marpaESLIFp;
  short              rcb;

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }
  marpaESLIFp = marpaESLIFRecognizerp->marpaESLIFp;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (marpaESLIFRecognizerp->scanb) {
    MARPAESLIF_ERROR(marpaESLIFp, "Scan can be done only once");
    goto err;
  }

  if (! marpaESLIFRecognizerp->marpaESLIFGrammarp->grammarp->latmb) {
    MARPAESLIF_ERRORF(marpaESLIFp,
                      "Scan requires your grammar at level %d (%s) to have: latm => 1",
                      marpaESLIFRecognizerp->marpaESLIFGrammarp->grammarp->leveli,
                      marpaESLIFRecognizerp->marpaESLIFGrammarp->grammarp->descp->asciis
                      );
    goto err;
  }

  marpaESLIFRecognizerp->scanb = 1;
  rcb = _marpaESLIFRecognizer_resumeb(marpaESLIFRecognizerp, 0, initialEventsb, continuebp, isExhaustedbp);
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
short marpaESLIFRecognizer_resumeb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, size_t deltaLengthl, short *continuebp, short *isExhaustedbp)
/*****************************************************************************/
{
  static const char *funcs = "marpaESLIFRecognizer_resumeb";
  short              rcb;


  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  rcb = _marpaESLIFRecognizer_resumeb(marpaESLIFRecognizerp, deltaLengthl, 0 /* initialEventsb */, continuebp, isExhaustedbp);

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_resumeb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, size_t deltaLengthl, short initialEventsb, short *canContinuebp, short *isExhaustedbp)
/*****************************************************************************/
{
  static const char   *funcs              = "_marpaESLIFRecognizer_resumeb";
  marpaESLIF_stream_t *marpaESLIF_streamp = marpaESLIFRecognizerp->marpaESLIF_streamp;
  short                canContinueb;
  short                isExhaustedb;
  short                rcb;
  /* Top level resume is looping on _marpaESLIFRecognizer_resume_oneb() until:
     - failure
     - event
  */

  /* Eventually read until the delta offset is available */
  if (deltaLengthl > 0) {
    while (deltaLengthl > marpaESLIF_streamp->inputl) {
      if (! marpaESLIF_streamp->eofb) {
        if (! _marpaESLIFRecognizer_readb(marpaESLIFRecognizerp)) {
          goto err;
        }
      } else {
        MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "Resume delta offset %ld must be <= current remaining bytes in recognizer buffer, currently %ld", (unsigned long) deltaLengthl, (unsigned long) marpaESLIF_streamp->inputl);
        goto err;
      }
    }
    /* If there is newline is the skipped data, we suppose we should account for it for debug/trace purposes... */
    if (! _marpaESLIFRecognizer_matchPostProcessingb(marpaESLIFRecognizerp, deltaLengthl)) {
      goto err;
    }
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Resume: advancing stream internal position %p by %ld bytes", marpaESLIF_streamp->inputs, (unsigned long) deltaLengthl);
    marpaESLIF_streamp->inputs += deltaLengthl;
    marpaESLIF_streamp->inputl -= deltaLengthl;
  }

  do {
    /* do NEVER call this method with NULL parameters */
    rcb = _marpaESLIFRecognizer_resume_oneb(marpaESLIFRecognizerp, initialEventsb, &canContinueb, &isExhaustedb);
    if (! rcb) {
      goto err;
    }
    /* Makes sure initialEvents is true once only */
    if (initialEventsb) {
      initialEventsb = 0;
    }
    if (marpaESLIFRecognizerp->eventArrayl > 0) {
      break;
    }
  } while (canContinueb);

  if (canContinuebp != NULL) {
    *canContinuebp = canContinueb;
  }
  if (isExhaustedbp != NULL) {
    *isExhaustedbp = isExhaustedb;
  }
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static inline void _marpaESLIFRecognizer_alternativeStackSymbol_freev(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, genericStack_t *alternativeStackSymbolp)
/*****************************************************************************/
{
  static const char        *funcs           = "_marpaESLIFRecognizer_alternativeStackSymbol_freev";
  marpaESLIF_t             *marpaESLIFp     = marpaESLIFRecognizerp->marpaESLIFp;
  genericStack_t           *beforePtrStackp = marpaESLIFRecognizerp->beforePtrStackp;
  genericHash_t            *beforePtrHashp  = marpaESLIFRecognizerp->beforePtrHashp;
  genericHash_t            *afterPtrHashp   = marpaESLIFRecognizerp->afterPtrHashp;
  int                       i;
  marpaESLIF_alternative_t *alternativep;

  if (alternativeStackSymbolp != NULL) {
    for (i = 0; i < GENERICSTACK_USED(alternativeStackSymbolp); i++) {
      if (GENERICSTACK_IS_PTR(alternativeStackSymbolp, i)) {
        alternativep = (marpaESLIF_alternative_t *) GENERICSTACK_GET_PTR(alternativeStackSymbolp, i);
        if (alternativep != NULL) {
          _marpaESLIFValueResult_stack_i_setb(marpaESLIFp,
                                              NULL, /* valueResultStackp */
                                              -1, /* indicei */
                                              NULL, /* marpaESLIFValueResultp */
                                              0, /* forgetb */
                                              beforePtrStackp,
                                              beforePtrHashp,
                                              afterPtrHashp,
                                              &(alternativep->marpaESLIFValueResult));
          free(alternativep);
        }
      }
    }
    GENERICSTACK_RESET(alternativeStackSymbolp); /* Take care, alternativeStackSymbolp is a pointer to a static stack in recognizer's structure */
  }
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_alternativeStackSymbol_setb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, genericStack_t *alternativeStackSymbolp, marpaESLIF_alternative_t *alternativep, int indicei)
/*****************************************************************************/
/* This method is called ONLY by _marpaESLIFRecognizer_resume_oneb() that owns totally everything that is in it: */
/* Always marpaESLIFValueResult entries of type MARPAESLIF_VALUE_TYPE_ARRAY (shallow or not) */
/*****************************************************************************/
{
  static const char        *funcs           = "_marpaESLIFRecognizer_alternativeStackSymbol_setb";
  marpaESLIF_t             *marpaESLIFp     = marpaESLIFRecognizerp->marpaESLIFp;
  genericStack_t           *beforePtrStackp = marpaESLIFRecognizerp->beforePtrStackp;
  genericHash_t            *beforePtrHashp  = marpaESLIFRecognizerp->beforePtrHashp;
  genericHash_t            *afterPtrHashp   = marpaESLIFRecognizerp->afterPtrHashp;
  marpaESLIF_alternative_t *p; /* It is guaranteed that p is set whatever happens - see below */
  short                     rcb;

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Setting alternativeStackSymbolp[%d]", indicei);
  if (GENERICSTACK_IS_PTR(alternativeStackSymbolp, indicei)) {
    p = (marpaESLIF_alternative_t *) GENERICSTACK_GET_PTR(alternativeStackSymbolp, indicei);
    if (p->marpaESLIFValueResult.type != MARPAESLIF_VALUE_TYPE_ARRAY) {
      MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "marpaESLIFValueResultp->type is not ARRAY (got %d, %s)", p->marpaESLIFValueResult.type, _marpaESLIF_value_types(p->marpaESLIFValueResult.type));
      goto err;
    }
    if ((p->marpaESLIFValueResult.u.a.p != NULL) && (! p->marpaESLIFValueResult.u.a.shallowb)) {
      free(p->marpaESLIFValueResult.u.a.p);
    }
  } else {
    p = (marpaESLIF_alternative_t *) malloc(sizeof(marpaESLIF_alternative_t));
    if (p == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "malloc failure, %s", strerror(errno));
      goto err;
    }
  }

  *p = *alternativep;
  GENERICSTACK_SET_PTR(alternativeStackSymbolp, p, indicei);
  if (GENERICSTACK_ERROR(alternativeStackSymbolp)) {
    MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "alternativeStackSymbolp set failure, %s", strerror(errno));
    goto err;
  }

  rcb = 1;
  goto done;

 err:
  if (p != NULL) {
    free(p);
  }
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_resume_oneb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short initialEventsb, short *canContinuebp, short *isExhaustedbp)
/*****************************************************************************/
/* Note: latmb check are left in this method, even if it can be reached only if latmb is true */
/* This method is called very very often, therefore any optimization here is welcome. */
/*****************************************************************************/
{
  static const char               *funcs                             = "_marpaESLIFRecognizer_resume_oneb";
  marpaESLIF_t                    *marpaESLIFp                       = marpaESLIFRecognizerp->marpaESLIFp;
  marpaESLIFGrammar_t             *marpaESLIFGrammarp                = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  marpaESLIF_grammar_t            *grammarp                          = marpaESLIFGrammarp->grammarp;
  genericStack_t                  *symbolStackp                      = grammarp->symbolStackp;
  short                            latmb                             = grammarp->latmb;
  int                              alternativeStackSymboli           = 0;
  genericStack_t                  *alternativeStackSymbolp           = marpaESLIFRecognizerp->alternativeStackSymbolp;
  marpaWrapperRecognizer_t        *marpaWrapperRecognizerp           = marpaESLIFRecognizerp->marpaWrapperRecognizerp;
  short                            maxPriorityInitializedb           = 0;
  size_t                           maxMatchedl                       = 0;
  int                              maxStartCompletionsi              = marpaESLIFRecognizerp->maxStartCompletionsi;
  short                            havePriorityb                     = 0;
  marpaESLIF_stream_t             *marpaESLIF_streamp                = marpaESLIFRecognizerp->marpaESLIF_streamp;
  int                              maxPriorityi;
  size_t                           nSymboll;
  int                             *symbolArrayp;
  size_t                           symboll;
  int                              symboli;
  int                              alternativei;
  marpaESLIF_symbol_t             *symbolp;
  marpaESLIF_symbol_t             *exceptionp;
  marpaESLIF_matcher_value_t       rci;
  marpaESLIF_matcher_value_t       exceptionRci;
  short                            rcb;
  size_t                           sizel;
  marpaESLIFValueResult_t          marpaESLIFValueResult;
  marpaESLIFValueResult_t          exceptionMarpaESLIFValueResult;
  marpaESLIF_alternative_t         alternative;
  marpaESLIF_alternative_t        *alternativep;
  short                            completeb;
  int                              previousNumberOfStartCompletionsi;
  int                              numberOfStartCompletionsi;
  int                              numberOfExceptionCompletionsi;
  size_t                           lastSizeBeforeCompletionl;
  int                              symbolMaxStartCompletionsi;
  int                              exceptionMaxStartCompletionsi;
  short                            rcMatcherb;
  short                            canContinueb;
  short                            isExhaustedb;
  char                            *previnputs;
  size_t                           offsetl;
  marpaESLIFRecognizerIfCallback_t ifCallbackp;
  marpaESLIFValueResultBool_t      marpaESLIFValueResultBool;

  /* This macro is to avoid the memcpy() of *grammarp which have a true cost in this method */
  /* We fake a marpaESLIFGrammar using grammar sent in the stack */
#undef MARPAESLIFRECOGNIZER_GRAMMARDISCARD_INITIALIZER
#define MARPAESLIFRECOGNIZER_GRAMMARDISCARD_INITIALIZER() do {          \
    if (! marpaESLIFRecognizerp->grammarDiscardInitializedb) {          \
      marpaESLIFRecognizerp->marpaESLIFGrammarDiscard                            = *marpaESLIFGrammarp; \
      marpaESLIFRecognizerp->grammarDiscard                                      = *grammarp; \
      marpaESLIFRecognizerp->grammarDiscard.starti                               = marpaESLIFRecognizerp->grammarDiscard.discardi; \
      marpaESLIFRecognizerp->marpaESLIFGrammarDiscard.grammarp                   = &(marpaESLIFRecognizerp->grammarDiscard); \
      marpaESLIFRecognizerp->marpaESLIFRecognizerOptionDiscard                   = marpaESLIFRecognizerp->marpaESLIFRecognizerOption; \
      marpaESLIFRecognizerp->marpaESLIFRecognizerOptionDiscard.disableThresholdb = 1; \
      marpaESLIFRecognizerp->marpaESLIFRecognizerOptionDiscard.exhaustedb        = 1; \
      marpaESLIFRecognizerp->marpaESLIFRecognizerOptionDiscard.newlineb          = 0; \
      marpaESLIFRecognizerp->marpaESLIFRecognizerOptionDiscard.trackb            = 0; \
      marpaESLIFRecognizerp->marpaESLIFValueOptionDiscard                        = marpaESLIFValueOption_default_template; \
      marpaESLIFRecognizerp->marpaESLIFValueOptionDiscard.userDatavp             = (void *) marpaESLIFRecognizerp; \
      marpaESLIFRecognizerp->grammarDiscardInitializedb                          = 1; \
    }                                                                   \
  } while (0)

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "start, maxStartCompletionsi=%d", maxStartCompletionsi);

  /* Checks */
  if (! marpaESLIFRecognizerp->scanb) {
    MARPAESLIF_ERROR(marpaESLIFp, "Scan must be called first");
    goto err;
  }
  if (! latmb) {
    MARPAESLIF_ERRORF(marpaESLIFp, "Grammar No %d (%s) must be in LATM mode", grammarp->leveli, grammarp->descp->asciis);
    goto err;
  }

  /* Initializations */
  MARPAESLIFRECOGNIZER_RESUMECOUNTER_INC; /* Increment internal counter for tracing */
  marpaESLIFRecognizerp->completedb      = 0;

  /* We always start by resetting and collecting current events */
  MARPAESLIFRECOGNIZER_RESET_EVENTS(marpaESLIFRecognizerp);
  /* We break immediately if there are events and the initialEventsb is set. This can happen once */
  /* only in the whole lifetime of a recognizer. */
  if (initialEventsb) {
    if (! _marpaESLIFRecognizer_push_grammar_eventsb(marpaESLIFRecognizerp)) {
      goto err;
    }
    if (marpaESLIFRecognizerp->eventArrayl > 0) {
      rcb = 1;
      goto done;
    }
  }
  
  /* Ask for expected TERMINALS (and not lexemes) - we do a direct call to marpa because we want to know  */
  /* not only about meta symbols (the lexemes) but also about the explicit terminals. */
  if (marpaESLIFRecognizerp->pristineb) {
    nSymboll     = marpaESLIFRecognizerp->nSymbolPristinel;
    symbolArrayp = marpaESLIFRecognizerp->symbolArrayPristinep;
  } else {
    if (! marpaWrapperRecognizer_expectedb(marpaWrapperRecognizerp, &nSymboll, &symbolArrayp)) {
      goto err;
    }
  }

  if (nSymboll <= 0) {
    /* No symbol expected: this is an error unless:
       - discard mode and completion is reached, or
       - grammar is exhausted and exhaustion support is on
       (Note that exception mode setted support of exhaustion mode)
    */
    if (! _marpaESLIFRecognizer_isExhaustedb(marpaESLIFRecognizerp, &isExhaustedb)) {
      goto err;
    }
    if ((marpaESLIFRecognizerp->discardb && marpaESLIFRecognizerp->completedb)
        ||
        (marpaESLIFRecognizerp->marpaESLIFRecognizerOption.exhaustedb && isExhaustedb)) {
      rcb = 1;
      goto done;
    } else {
      goto err;
    }
  }

  /* Try to match */
  retry:
  for (symboll = 0; symboll < nSymboll; symboll++) {
    symboli = symbolArrayp[symboll];
    MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);

    exceptionp = symbolp->exceptionp;
    if (exceptionp == NULL) {
      /* No need to track anything */
      rcMatcherb = _marpaESLIFRecognizer_symbol_matcherb(marpaESLIFRecognizerp, symbolp, &rci, &marpaESLIFValueResult, 0 /* maxStartCompletionsi */, NULL /* lastSizeBeforeCompletionlp */, &numberOfStartCompletionsi);
      if (rcMatcherb < 0) {
        goto err;
      }
      if (! rcMatcherb) {
        continue;
      }
    } else {
      rcMatcherb = _marpaESLIFRecognizer_symbol_matcherb(marpaESLIFRecognizerp, symbolp, &rci, &marpaESLIFValueResult, -1, NULL /* lastSizeBeforeCompletionlp */, &numberOfStartCompletionsi);
      if (rcMatcherb < 0) {
        goto err;
      }
      if (! rcMatcherb) {
        continue;
      }

      symbolMaxStartCompletionsi    = numberOfStartCompletionsi;
      exceptionMaxStartCompletionsi = -1;
    exception_again:
      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Symbol match on %ld bytes using %d completions", (unsigned long) marpaESLIFValueResult.u.a.sizel, numberOfStartCompletionsi);
      previnputs = marpaESLIF_streamp->inputs;
      offsetl = ((char *) marpaESLIFValueResult.u.a.p) - previnputs;
      /* Take care: this may move the stream */
      rcMatcherb = _marpaESLIFRecognizer_symbol_matcherb(marpaESLIFRecognizerp, exceptionp, &exceptionRci, &exceptionMarpaESLIFValueResult, exceptionMaxStartCompletionsi, &lastSizeBeforeCompletionl, &numberOfExceptionCompletionsi);
      if ((marpaESLIFRecognizerp->parentRecognizerp == NULL) && (marpaESLIF_streamp->inputs != previnputs) && (symbolp->type == MARPAESLIF_SYMBOL_TYPE_META)) {
        /* Stream have moved and marpaESLIFValueResult.u.a.p is a pointer within the stream... */
        marpaESLIFValueResult.u.a.p = marpaESLIF_streamp->inputs + offsetl;
      }
      if (rcMatcherb < 0) {
        goto err;
      }
      /* We take current marpaESLIFValueResult and look if it matches exception. */
      if (rcMatcherb) {
        if (exceptionRci == MARPAESLIF_MATCH_OK) {
          /* In any case we do not mind about the exception value itself, just the size - and in lexeme mode exceptionMarpaESLIFValueResult.u.a.p is a shallow pointer */
          /* free(exceptionMarpaESLIFValueResult.u.a.p); */
          exceptionMarpaESLIFValueResult.u.a.p = NULL;
          if (exceptionMarpaESLIFValueResult.u.a.sizel == marpaESLIFValueResult.u.a.sizel) {
            /* The lexeme value, taken as if it was a separate input, is matching the exception */
            MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Exception match on %ld bytes", (unsigned long) exceptionMarpaESLIFValueResult.u.a.sizel);
            /* A lexeme value is always a shallow pointer */
            /*
            free(marpaESLIFValueResult.u.a.p);
            */
            marpaESLIFValueResult.u.a.p = NULL;
            marpaESLIFValueResult.u.a.sizel = 0;

            /* We have to rollback on the number of completions until symbol size is <= lastSizeBeforeCompletionl */
            if (lastSizeBeforeCompletionl <= 0) {
              MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Exception match on %ld bytes: no room is left for symbol match", (unsigned long) exceptionMarpaESLIFValueResult.u.a.sizel);
              rci = MARPAESLIF_MATCH_FAILURE;
            } else {
              MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Exception match on %ld bytes: asking for less symbol completions until its size is <= %ld", (unsigned long) exceptionMarpaESLIFValueResult.u.a.sizel, (unsigned long) lastSizeBeforeCompletionl);
	      if (symbolMaxStartCompletionsi <= 1) {
		MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Exception match on %ld bytes and symbol cannot match on a smaller input", (unsigned long) exceptionMarpaESLIFValueResult.u.a.sizel);
		rci = MARPAESLIF_MATCH_FAILURE;
	      } else {
		while (--symbolMaxStartCompletionsi > 0) {
		  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Exception match on %ld bytes: asking for %d symbol start completions", (unsigned long) exceptionMarpaESLIFValueResult.u.a.sizel, symbolMaxStartCompletionsi);
                  previnputs = marpaESLIF_streamp->inputs;
                  offsetl = ((char *) marpaESLIFValueResult.u.a.p) - previnputs;
		  rcMatcherb = _marpaESLIFRecognizer_symbol_matcherb(marpaESLIFRecognizerp, symbolp, &rci, &marpaESLIFValueResult, symbolMaxStartCompletionsi, NULL /* lastSizeBeforeCompletionlp */, NULL /* numberOfStartCompletionsi */);
                  if ((marpaESLIFRecognizerp->parentRecognizerp == NULL) && (marpaESLIF_streamp->inputs != previnputs) && (symbolp->type == MARPAESLIF_SYMBOL_TYPE_META)) {
                    /* Stream have moved and marpaESLIFValueResult.u.a.p is a pointer within the stream... */
                    marpaESLIFValueResult.u.a.p = marpaESLIF_streamp->inputs + offsetl;
                  }
		  if (rcMatcherb < 0) {
		    goto err;
		  }
		  if (! rcMatcherb) {
		    /* This should never happen since we already had completions before */
		    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Exception match on %ld bytes: symbol match failure when asking for %d start completions !?", (unsigned long) exceptionMarpaESLIFValueResult.u.a.sizel, symbolMaxStartCompletionsi);
		    break;
		  }
		  if (rci != MARPAESLIF_MATCH_OK) {
		    /* Ditto */
		    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Exception match on %ld bytes: symbol match did not return MARPAESLIF_MATCH_OK when asking for %d start completions !?", (unsigned long) exceptionMarpaESLIFValueResult.u.a.sizel, symbolMaxStartCompletionsi);
		    break;
		  }
		  if (marpaESLIFValueResult.u.a.sizel > lastSizeBeforeCompletionl) {
		    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Exception match on %ld bytes: asking for %d symbol start completions ok but with size %ld > %ld", (unsigned long) exceptionMarpaESLIFValueResult.u.a.sizel, symbolMaxStartCompletionsi, (unsigned long) marpaESLIFValueResult.u.a.sizel, (unsigned long) lastSizeBeforeCompletionl);
		    /* A lexeme value is always a shallow pointer */
		    /*
		      free(marpaESLIFValueResult.u.a.p);
		    */
		    marpaESLIFValueResult.u.a.p = NULL;
		    marpaESLIFValueResult.u.a.sizel = 0;
		  } else {
		    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Exception match on %ld bytes: asking for %d symbol start completions ok with size %ld <= %ld", (unsigned long) exceptionMarpaESLIFValueResult.u.a.sizel, symbolMaxStartCompletionsi, (unsigned long) marpaESLIFValueResult.u.a.sizel, (unsigned long) lastSizeBeforeCompletionl);
		    exceptionMaxStartCompletionsi = numberOfExceptionCompletionsi - 1;
		    goto exception_again;
		  }
		}
	      }
	    }
          } else {
            MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Exception match on %ld bytes != symbol match on %ld bytes", (unsigned long) exceptionMarpaESLIFValueResult.u.a.sizel, (unsigned long) marpaESLIFValueResult.u.a.sizel);
          }
        }
      }
    }
    
    switch (rci) {
    case MARPAESLIF_MATCH_FAILURE:
      break;
    case MARPAESLIF_MATCH_OK:
#ifndef MARPAESLIF_NTRACE
      MARPAESLIF_VALUECHECK_IF_LEXEME_MODE(marpaESLIFp, marpaESLIFValueResult);
#endif
      alternative.symbolp = symbolp;
      if (marpaESLIFRecognizerp->parentRecognizerp != NULL) {
        /* Internal alternatives that remains internal are always offsets */
        alternative.marpaESLIFValueResult.type               = MARPAESLIF_VALUE_TYPE_ARRAY;
        alternative.marpaESLIFValueResult.contextp           = NULL;
        alternative.marpaESLIFValueResult.representationp    = NULL;
        alternative.marpaESLIFValueResult.u.a.p              = marpaESLIFValueResult.u.a.p - (size_t) marpaESLIF_streamp->inputs;
        alternative.marpaESLIFValueResult.u.a.sizel          = marpaESLIFValueResult.u.a.sizel;
        alternative.marpaESLIFValueResult.u.a.freeUserDatavp = NULL;
        alternative.marpaESLIFValueResult.u.a.freeCallbackp  = NULL;
        alternative.marpaESLIFValueResult.u.a.shallowb       = 1;

        marpaESLIFValueResultBool = MARPAESLIFVALUERESULTBOOL_TRUE;
      } else {
        switch (symbolp->type) {
        case MARPAESLIF_SYMBOL_TYPE_META:
          /* Internal alternatives that can be exposed to the end-user are explictly malloced when this is a meta symbol */
          alternative.marpaESLIFValueResult.type               = MARPAESLIF_VALUE_TYPE_ARRAY;
          alternative.marpaESLIFValueResult.contextp           = NULL;
          alternative.marpaESLIFValueResult.representationp    = NULL;
          alternative.marpaESLIFValueResult.u.a.p              = malloc(marpaESLIFValueResult.u.a.sizel + 1); /* Hiden NUL byte for convenience */
          if (alternative.marpaESLIFValueResult.u.a.p == NULL) {
            MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
            goto err;
          }
          memcpy(alternative.marpaESLIFValueResult.u.a.p, marpaESLIFValueResult.u.a.p, marpaESLIFValueResult.u.a.sizel);
          alternative.marpaESLIFValueResult.u.a.p[marpaESLIFValueResult.u.a.sizel] = '\0';
          alternative.marpaESLIFValueResult.u.a.sizel          = marpaESLIFValueResult.u.a.sizel;
          alternative.marpaESLIFValueResult.u.a.freeUserDatavp = marpaESLIFRecognizerp;
          alternative.marpaESLIFValueResult.u.a.freeCallbackp  = _marpaESLIF_generic_freeCallbackv;
          alternative.marpaESLIFValueResult.u.a.shallowb       = 0;
          break;
        default:
          /* Else this is a terminal - and terminal matcher already allocated the area */
          alternative.marpaESLIFValueResult = marpaESLIFValueResult;
          break;
        }

        /* If symbol has a if-action, check it */
        if (symbolp->ifActionp != NULL) {
          if (! _marpaESLIFRecognizer_recognizerIfActionCallbackb(marpaESLIFRecognizerp, symbolp->descp->asciis, symbolp->ifActionp, &ifCallbackp)) {
            if ((marpaESLIFRecognizerp->parentRecognizerp == NULL) && (symbolp->type == MARPAESLIF_SYMBOL_TYPE_META)) {
              /* We may have malloced for the alternative */
              free(alternative.marpaESLIFValueResult.u.a.p);
            }
            goto err;
          }
          if (! ifCallbackp(marpaESLIFRecognizerp->marpaESLIFRecognizerOption.userDatavp, marpaESLIFRecognizerp, &(alternative.marpaESLIFValueResult), &marpaESLIFValueResultBool)) {
            if ((marpaESLIFRecognizerp->parentRecognizerp == NULL) && (symbolp->type == MARPAESLIF_SYMBOL_TYPE_META)) {
              /* We may have malloced for the alternative */
              free(alternative.marpaESLIFValueResult.u.a.p);
            }
            goto err;
          }
          if (marpaESLIFValueResultBool == MARPAESLIFVALUERESULTBOOL_FALSE) {
            /* This symbol is rejected -; */
            MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Symbol %s is rejected by if-action", symbolp->descp->asciis);
          }
        } else {
          marpaESLIFValueResultBool = MARPAESLIFVALUERESULTBOOL_TRUE;
        }
      }

      if (marpaESLIFValueResultBool != MARPAESLIFVALUERESULTBOOL_FALSE) {
        alternative.grammarLengthi = 1; /* Scan mode is in the token-stream model */
        alternative.usedb          = 1;

        if (! _marpaESLIFRecognizer_alternativeStackSymbol_setb(marpaESLIFRecognizerp, alternativeStackSymbolp, &alternative, alternativeStackSymboli)) {
          goto err;
        }

        /* Remember at least one alternative is ok */
        alternativeStackSymboli++;

        /* Remember if this alternative have priority - this allows us to skip a block of code */
        /* that have some cost, the usual pattern is to not have priorities on lexemes -; */
        if (symbolp->priorityi != 0) {
          havePriorityb = 1;
        }

        /* Remember max matched length */
        if (marpaESLIFValueResult.u.a.sizel > maxMatchedl) {
          maxMatchedl = marpaESLIFValueResult.u.a.sizel;
          MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Setted maxMatchedl to %ld", (unsigned long) maxMatchedl);
        }
      }

      break;
    default:
      /* The case MARPAESLIF_MATCH_AGAIN is handled in the terminal section of _marpaESLIFRecognizer_symbol_matcherb() */
      MARPAESLIF_ERRORF(marpaESLIFp, "Unsupported matcher return code %d", rci);
      goto err;
    }
  }
  
  if (alternativeStackSymboli <= 0) {
    /* If we are not already in the discard mode, try to discard if discardOnOffb is true */
    if ((! marpaESLIFRecognizerp->discardb) && marpaESLIFRecognizerp->discardOnOffb && (grammarp->discardi >= 0)) {
      /* Always reset this shallow pointer */
      marpaESLIFRecognizerp->discardEvents  = NULL;
      marpaESLIFRecognizerp->discardSymbolp = NULL;
      MARPAESLIFRECOGNIZER_GRAMMARDISCARD_INITIALIZER();
      if (_marpaESLIFGrammar_parseb(&(marpaESLIFRecognizerp->marpaESLIFGrammarDiscard),
                                    &(marpaESLIFRecognizerp->marpaESLIFRecognizerOptionDiscard),
                                    &(marpaESLIFRecognizerp->marpaESLIFValueOptionDiscard),
                                    1, /* discardb */
                                    marpaESLIFRecognizerp->noEventb, /* This will select marpaWrapperGrammarDiscardNoEventp or marpaWrapperGrammarDiscardp */
                                    1, /* silentb */
                                    marpaESLIFRecognizerp, /* marpaESLIFRecognizerParentp */
                                    NULL, /* isExhaustedbp */
                                    &marpaESLIFValueResult,
                                    0, /* maxStartCompletionsi */
                                    NULL, /* lastSizeBeforeCompletionlp */
                                    NULL /* numberOfStartCompletionsip */,
                                    0 /* grammarIsOnStackb - because marpaESLIFRecognizerp itself is not on the stack */)) {
#ifndef MARPAESLIF_NTRACE
        MARPAESLIF_VALUECHECK_IF_LEXEME_MODE(marpaESLIFp, marpaESLIFValueResult);
#endif
        /* New line processing, etc... */
        if (! _marpaESLIFRecognizer_matchPostProcessingb(marpaESLIFRecognizerp, marpaESLIFValueResult.u.a.sizel)) {
          goto err;
        }
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Discard successful: advancing stream internal position %p by %ld bytes", marpaESLIF_streamp->inputs, (unsigned long) marpaESLIFValueResult.u.a.sizel);
        marpaESLIF_streamp->inputs += marpaESLIFValueResult.u.a.sizel;
        marpaESLIF_streamp->inputl -= marpaESLIFValueResult.u.a.sizel;
        /* We NEVER free because we called for parsing in lexeme mode */
        /* free(marpaESLIFValueResult.u.a.p); */

        /* We want to do as if we would have done a lexeme complete before */
        MARPAESLIFRECOGNIZER_RESET_EVENTS(marpaESLIFRecognizerp);
        if (! _marpaESLIFRecognizer_push_grammar_eventsb(marpaESLIFRecognizerp)) {
          goto err;
        }
        if ((marpaESLIFRecognizerp->discardEvents != NULL) && (marpaESLIFRecognizerp->discardSymbolp != NULL)) {
          /* Push discard event */
          if (! _marpaESLIFRecognizer_push_eventb(marpaESLIFRecognizerp, MARPAESLIF_EVENTTYPE_DISCARD, marpaESLIFRecognizerp->discardSymbolp, marpaESLIFRecognizerp->discardEvents, &(marpaESLIFValueResult.u.a))) {
            goto err;
          }
        }

        /* If there is an event, get out of this method */
        if (marpaESLIFRecognizerp->eventArrayl > 0) {
          rcb = 1;
          goto done;
        } else {
          goto retry;
        }
      }
    }

    /* Discard failure - this is an error unless lexemes were read and:
       - exhaustion is on, or
       - eof flag is true and all the data is consumed
    */
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp,
                                funcs,
                                "No alternative, current state is: haveLexemeb=%d, marpaESLIFRecognizerOption.exhaustedb=%d, eofb=%d, inputl=%ld",
                                (int) marpaESLIFRecognizerp->haveLexemeb,
                                (int) marpaESLIFRecognizerp->marpaESLIFRecognizerOption.exhaustedb,
                                (int) marpaESLIF_streamp->eofb,
                                (unsigned long) marpaESLIF_streamp->inputl);
    if (marpaESLIFRecognizerp->haveLexemeb && (
                                               marpaESLIFRecognizerp->marpaESLIFRecognizerOption.exhaustedb
                                               ||
                                               (marpaESLIF_streamp->eofb && (marpaESLIF_streamp->inputl <= 0))
                                               )
        ) {
      /* If exhaustion option is on, we fake an exhaustion event if grammar itself is not exhausted */
      if (marpaESLIFRecognizerp->marpaESLIFRecognizerOption.exhaustedb) {
        if (! _marpaESLIFRecognizer_isExhaustedb(marpaESLIFRecognizerp, &isExhaustedb)) {
          goto err;
        }
        if (! isExhaustedb) {
          /* Fake an exhaustion event */
          if (! _marpaESLIFRecognizer_push_eventb(marpaESLIFRecognizerp, MARPAESLIF_EVENTTYPE_EXHAUSTED, NULL /* symbolp */, MARPAESLIF_EVENTTYPE_EXHAUSTED_NAME, NULL /* discardARrayp */)) {
            goto err;
          }
        }
      }
      marpaESLIFRecognizerp->cannotcontinueb = 1;
      rcb = 1;
      goto done;
    } else {
      rcb = 0;
      goto err;
    }
  }

  /* Filter by priority */
  if (havePriorityb) {
    for (alternativei = 0; alternativei < alternativeStackSymboli; alternativei++) {
      alternativep = (marpaESLIF_alternative_t *) GENERICSTACK_GET_PTR(alternativeStackSymbolp, alternativei);
      symbolp = alternativep->symbolp;
      if ((! maxPriorityInitializedb) || (symbolp->priorityi > maxPriorityi)) {
        maxPriorityi = symbolp->priorityi;
      }
    }

    for (alternativei = 0; alternativei < alternativeStackSymboli; alternativei++) {
      alternativep = (marpaESLIF_alternative_t *) GENERICSTACK_GET_PTR(alternativeStackSymbolp, alternativei);
      symbolp = alternativep->symbolp;
      if (symbolp->priorityi < maxPriorityi) {
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp,
                                    funcs,
                                    "Alternative %s is out-prioritized (priority %d < max priority %d)",
                                    symbolp->descp->asciis,
                                    symbolp->priorityi,
                                    maxPriorityi);
        /* No need to set it to NULL, we use the alternativep->usedb flag */
        alternativep->usedb = 0;
        /* This will trigger maxMatchedl recomputation */
        maxMatchedl = 0;
      }
    }

    if (maxMatchedl <= 0) {
      for (alternativei = 0; alternativei < alternativeStackSymboli; alternativei++) {
        alternativep = (marpaESLIF_alternative_t *) GENERICSTACK_GET_PTR(alternativeStackSymbolp, alternativei);
        if (! alternativep->usedb) {
          /* Out-prioritized */
          continue;
        }
        /* By definition here we should handle only the internal alternatives, that are ALWAYS of type */
        /* MARPAESLIF_VALUE_TYPE_ARRAY */
        if (alternativep->marpaESLIFValueResult.type != MARPAESLIF_VALUE_TYPE_ARRAY) {
          continue;
        }

        sizel = alternativep->marpaESLIFValueResult.u.a.sizel;
        if (sizel > maxMatchedl) {
          maxMatchedl = sizel;
        }
      }
      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp,
                                  funcs,
                                  "maxMatchedl revisited to %ld",
                                  (unsigned long) maxMatchedl);
    }
  }

  /* Filter by length (LATM) - the test on latmb is "useless" in the sense that latmb is forced to be true, i.e. maxMatchedl is always meaningful */
  if (latmb) {
    for (alternativei = 0; alternativei < alternativeStackSymboli; alternativei++) {
      alternativep = (marpaESLIF_alternative_t *) GENERICSTACK_GET_PTR(alternativeStackSymbolp, alternativei);
      if (! alternativep->usedb) {
        /* Out-prioritized */
        continue;
      }
      /* By definition here we should handle only the internal alternatives, that are ALWAYS of type */
      /* MARPAESLIF_VALUE_TYPE_ARRAY */
      if (alternativep->marpaESLIFValueResult.type != MARPAESLIF_VALUE_TYPE_ARRAY) {
        continue;
      }

      sizel = alternativep->marpaESLIFValueResult.u.a.sizel;
      if (sizel < maxMatchedl) {
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp,
                                    funcs,
                                    "Alternative %s is skipped (length %ld < max length %ld)",
                                    alternativep->symbolp->descp->asciis,
                                    (unsigned long) sizel,
                                    (unsigned long) maxMatchedl);
        /* No need to set it to NULL, we use the alternativep->usedb flag */
        alternativep->usedb = 0;
      }
    }
  }

  /* It is a non-sense to have lexemes of length maxMatchedl and a discard rule that would be greater. */
  /* In this case, :discard have precedence. */
  if ((! marpaESLIFRecognizerp->discardb) /* Done only if we are not already in discard mode */
      &&
      marpaESLIFRecognizerp->discardOnOffb /* :discard is enabled */
      &&
      (grammarp->discardi >= 0) /* And if there is a :discard entry point */
      ) {
    /* Always reset this shallow pointer */
    marpaESLIFRecognizerp->discardEvents  = NULL;
    marpaESLIFRecognizerp->discardSymbolp = NULL;
    MARPAESLIFRECOGNIZER_GRAMMARDISCARD_INITIALIZER();
    if (_marpaESLIFGrammar_parseb(&(marpaESLIFRecognizerp->marpaESLIFGrammarDiscard),
                                  &(marpaESLIFRecognizerp->marpaESLIFRecognizerOptionDiscard),
                                  &(marpaESLIFRecognizerp->marpaESLIFValueOptionDiscard),
                                  1, /* discardb */
                                  marpaESLIFRecognizerp->noEventb, /* This will select marpaWrapperGrammarDiscardNoEventp or marpaWrapperGrammarDiscardp */
                                  1, /* silentb - and we want that to be absolutely silent */
                                  marpaESLIFRecognizerp, /* marpaESLIFRecognizerParentp */
                                  NULL, /* isExhaustedbp */
                                  &marpaESLIFValueResult,
                                  0, /* maxStartCompletionsi */
                                  NULL, /* lastSizeBeforeCompletionlp */
                                  NULL /* numberOfStartCompletionsip */,
                                  0 /* grammarIsOnStackb - because marpaESLIFRecognizerp itself is not on the stack */)) {
#ifndef MARPAESLIF_NTRACE
      MARPAESLIF_VALUECHECK_IF_LEXEME_MODE(marpaESLIFp, marpaESLIFValueResult);
#endif
      if (marpaESLIFValueResult.u.a.sizel > maxMatchedl) {
        /* New line processing, etc... */
        if (! _marpaESLIFRecognizer_matchPostProcessingb(marpaESLIFRecognizerp, marpaESLIFValueResult.u.a.sizel)) {
          goto err;
        }
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Discard match is %ld bytes > %ld bytes (longest lexeme): advancing stream position %p by %ld bytes",
                                    (unsigned long) marpaESLIFValueResult.u.a.sizel,
                                    (unsigned long) maxMatchedl,
                                    marpaESLIF_streamp->inputs,
                                    (unsigned long) marpaESLIFValueResult.u.a.sizel);
        marpaESLIF_streamp->inputs += marpaESLIFValueResult.u.a.sizel;
        marpaESLIF_streamp->inputl -= marpaESLIFValueResult.u.a.sizel;
        /* We NEVER free because we called for parsing in lexeme mode */
        /* free(marpaESLIFValueResult.u.a.p); */
        /* These lines are important so that this specific retry is clean */
        alternativeStackSymboli = 0;
        maxMatchedl = 0;

        /* We want to do as if we would have done a lexeme complete before */
        MARPAESLIFRECOGNIZER_RESET_EVENTS(marpaESLIFRecognizerp);
        if (! _marpaESLIFRecognizer_push_grammar_eventsb(marpaESLIFRecognizerp)) {
          goto err;
        }
        if ((marpaESLIFRecognizerp->discardEvents != NULL) && (marpaESLIFRecognizerp->discardSymbolp != NULL)) {
          /* Push discard event */
          if (! _marpaESLIFRecognizer_push_eventb(marpaESLIFRecognizerp, MARPAESLIF_EVENTTYPE_DISCARD, marpaESLIFRecognizerp->discardSymbolp, marpaESLIFRecognizerp->discardEvents, &(marpaESLIFValueResult.u.a))) {
            goto err;
          }
        }

        /* If there is an event, get out of this method */
        if (marpaESLIFRecognizerp->eventArrayl > 0) {
          rcb = 1;
          goto done;
        } else {
          goto retry;
        }
      } else {
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Discard match is of %ld bytes < %ld bytes (longest lexeme): ignored",
                                    (unsigned long) marpaESLIFValueResult.u.a.sizel,
                                    (unsigned long) maxMatchedl);
        /* We NEVER free because we called for parsing in lexeme mode */
        /*
        free(marpaESLIFValueResult.u.a.p);
        */
      }
    }
  }

  /* Here we have all the alternatives the recognizer got - remember that this recognizer have seen at least one lexeme in its whole life */
  marpaESLIFRecognizerp->haveLexemeb = 1;

  /* Determine if we have pause before events - only for the top-level recognizer */
  for (alternativei = 0; alternativei < alternativeStackSymboli; alternativei++) {
    alternativep = (marpaESLIF_alternative_t *) GENERICSTACK_GET_PTR(alternativeStackSymbolp, alternativei);
    if (! alternativep->usedb) {
      continue;
    }
    /* By definition here we should handle only the internal alternatives, that are ALWAYS of type */
    /* MARPAESLIF_VALUE_TYPE_ARRAY */
    if (alternativep->marpaESLIFValueResult.type != MARPAESLIF_VALUE_TYPE_ARRAY) {
      continue;
    }

    symbolp = alternativep->symbolp;
    if ((symbolp->eventBefores != NULL) && marpaESLIFRecognizerp->beforeEventStatebp[symbolp->idi]) {
      if (! _marpaESLIFRecognizer_set_pauseb(marpaESLIFRecognizerp, grammarp, symbolp, alternativep->marpaESLIFValueResult.u.a.p, alternativep->marpaESLIFValueResult.u.a.sizel)) {
        goto err;
      }
      if (! _marpaESLIFRecognizer_push_eventb(marpaESLIFRecognizerp, MARPAESLIF_EVENTTYPE_BEFORE, symbolp, symbolp->eventBefores, NULL /* discardArrayp */)) {
        goto err;
      }
    }
  }
  if (marpaESLIFRecognizerp->eventArrayl > 0) {
    rcb = 1;
    goto done;
  }

  /* And push alternatives */
  for (alternativei = 0; alternativei < alternativeStackSymboli; alternativei++) {
    alternativep = (marpaESLIF_alternative_t *) GENERICSTACK_GET_PTR(alternativeStackSymbolp, alternativei);
    if (! alternativep->usedb) {
      continue;
    }

    if (! _marpaESLIFRecognizer_lexeme_alternativeb(marpaESLIFRecognizerp, alternativep)) {
      goto err;
    }
  }

  /* Commit - this will increment inputs and decrement inputl */
  if (! _marpaESLIFRecognizer_lexeme_completeb(marpaESLIFRecognizerp, maxMatchedl)) {
#ifndef MARPAESLIF_NTRACE
    marpaESLIFRecognizer_progressLogb(marpaESLIFRecognizerp, -1, -1, GENERICLOGGER_LOGLEVEL_TRACE);
#endif
    goto err;
  }

  /* Is there a limit on start symbol completions ? Note that the value -1 is used to trigger the call to _marpaESLIF_recognizer_start_is_completeb() */
  if (maxStartCompletionsi != 0) {
    /* This will force a call to _marpaESLIF_recognizer_start_is_completeb */
    if (! _marpaESLIF_recognizer_start_is_completeb(marpaESLIFRecognizerp, &completeb)) {
      goto err;
    }
    if (completeb) {
      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "lastSizeBeforeCompletionl %ld -> %ld", (unsigned long) marpaESLIFRecognizerp->lastSizeBeforeCompletionl, (unsigned long) marpaESLIFRecognizerp->lastSizel);
      marpaESLIFRecognizerp->lastSizeBeforeCompletionl = marpaESLIFRecognizerp->lastSizel;
      previousNumberOfStartCompletionsi = marpaESLIFRecognizerp->numberOfStartCompletionsi++;
      if (marpaESLIFRecognizerp->numberOfStartCompletionsi < previousNumberOfStartCompletionsi) {
        MARPAESLIF_ERROR(marpaESLIFp, "int turnaround when computing numberOfStartCompletionsi");
        goto err;
      }
      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Number of start completions is %d", marpaESLIFRecognizerp->numberOfStartCompletionsi);
      if (maxStartCompletionsi > 0) {
        if (marpaESLIFRecognizerp->numberOfStartCompletionsi >= maxStartCompletionsi) {
          MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Maximum number of start completions is %d and reached", maxStartCompletionsi);
        } else {
          /* If grammar is exhausted and we did not reach wanted number of completions, then it is a failure */
          if (! _marpaESLIFRecognizer_isExhaustedb(marpaESLIFRecognizerp, &isExhaustedb)) {
            goto err;
          }
          if (isExhaustedb) {
            MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Maximum number of start completions is %d and current number is %d, but parse is exhausted", marpaESLIFRecognizerp->maxStartCompletionsi, marpaESLIFRecognizerp->numberOfStartCompletionsi);
            rcb = 0;
            goto done;
          }
        }
      }
    }
  }
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  if (! _marpaESLIFRecognizer_isCanContinueb(marpaESLIFRecognizerp, &canContinueb, NULL /* eofbp */, &isExhaustedb)) {
    goto err;
  }
#ifndef MARPAESLIF_NTRACE
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "rcb = %d (canContinueb=%d, isExhausted=%d)", (int) rcb, (int) canContinueb, (int) isExhaustedb);
#endif
    /* In discard mode, if successful, per definition we have fetched the events. An eventual completion event will be our parent's discard event */
    /* If there is a completion it is unique per def because discard mode is always launched with ambiguity turned off. */
  if (rcb && marpaESLIFRecognizerp->discardb && (! canContinueb) && (marpaESLIFRecognizerp->lastCompletionEvents != NULL) && (marpaESLIFRecognizerp->lastCompletionSymbolp != NULL)) {
    /* In theory it is not possible to not have a parent recognizer here */
    if (marpaESLIFRecognizerp->parentRecognizerp != NULL) {
      marpaESLIFRecognizerp->parentRecognizerp->discardEvents  = marpaESLIFRecognizerp->lastCompletionEvents;
      marpaESLIFRecognizerp->parentRecognizerp->discardSymbolp = marpaESLIFRecognizerp->lastCompletionSymbolp;
    }
  }
  /* At level 0, this is the final value - we generate error information if there is input unless discard or exception mode */
  if (! rcb) {
    if (! marpaESLIFRecognizerp->silentb) {
      MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "--------------------------------------------");
      MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "Recognizer failure. Current state:");
      marpaESLIFRecognizer_progressLogb(marpaESLIFRecognizerp, -1, -1, GENERICLOGGER_LOGLEVEL_ERROR);
      MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "--------------------------------------------");
      if (nSymboll <= 0) {
        MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "There was no expected terminal");
      } else {
        for (symboll = 0; symboll < nSymboll; symboll++) {
          symboli = symbolArrayp[symboll];
          MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "Expected terminal: %s", symbolp->descp->asciis);
        }
      }
      /* If there is some information before, show it */
      if ((marpaESLIF_streamp->inputs != NULL) && (marpaESLIF_streamp->buffers != NULL) && (marpaESLIF_streamp->inputs > marpaESLIF_streamp->buffers)) {
        char  *dumps;
        size_t dumpl;

        if ((marpaESLIF_streamp->inputs - marpaESLIF_streamp->buffers) > 128) {
          dumps = marpaESLIF_streamp->inputs - 128;
          dumpl = 128;
        } else {
          dumps = marpaESLIF_streamp->buffers;
          dumpl = marpaESLIF_streamp->inputs - marpaESLIF_streamp->buffers;
        }
        MARPAESLIF_HEXDUMPV(marpaESLIFRecognizerp,
                            "",
                            marpaESLIF_streamp->utfb ? "UTF-8 converted data before the failure" : "Raw data before the failure",
                            dumps,
                            dumpl,
                            0 /* traceb */);
      }
      if (marpaESLIF_streamp->utfb && marpaESLIFRecognizerp->marpaESLIFRecognizerOption.newlineb) {
        if (marpaESLIF_streamp->columnl > 0) {
          /* Column is known (in terms of character count) */
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "<<<<<< RECOGNIZER FAILURE AT LINE No %ld COLUMN No %ld, HERE: >>>>>>", (unsigned long) marpaESLIF_streamp->linel, (unsigned long) marpaESLIF_streamp->columnl);
        } else {
          /* Column is not known */
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "<<<<<< RECOGNIZER FAILURE AT LINE No %ld, HERE: >>>>>>", (unsigned long) marpaESLIF_streamp->linel);
        }
      } else {
        MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "<<<<<< RECOGNIZER FAILURE HERE: >>>>>>");
      }
      /* If there is some information after, show it */
      if ((marpaESLIF_streamp->inputs != NULL) && (marpaESLIF_streamp->inputl > 0)) {
        char  *dumps;
        size_t dumpl;

        dumps = marpaESLIF_streamp->inputs;
        dumpl = marpaESLIF_streamp->inputl > 128 ? 128 : marpaESLIF_streamp->inputl;
        MARPAESLIF_HEXDUMPV(marpaESLIFRecognizerp,
                            "",
                            marpaESLIF_streamp->utfb ? "UTF-8 converted data after the failure" : "Raw data after the failure",
                            dumps,
                            dumpl,
                            0 /* traceb */);
      }
    }
  }

  /* if (canContinuebp != NULL) { */
  *canContinuebp = canContinueb; /* We know it is never NULL */
  /* } */
  /* if (isExhaustedbp != NULL) { */
  *isExhaustedbp = isExhaustedb; /* We know it is never NULL */
  /* } */
  
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d (canContinueb=%d, isExhaustedb=%d)", (int) rcb, (int) canContinueb, (int) isExhaustedb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIF_recognizer_start_is_completeb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short *completebp)
/*****************************************************************************/
{
  static const char                *funcs                   = "_marpaESLIF_recognizer_start_is_completeb";
#ifndef MARPAESLIF_NTRACE
  marpaESLIF_t                     *marpaESLIFp             = marpaESLIFRecognizerp->marpaESLIFp;
#endif
  marpaESLIFGrammar_t              *marpaESLIFGrammarp      = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  marpaESLIF_grammar_t             *grammarp                = marpaESLIFGrammarp->grammarp;
  genericStack_t                   *ruleStackp              = grammarp->ruleStackp;
  marpaWrapperRecognizer_t         *marpaWrapperRecognizerp = marpaESLIFRecognizerp->marpaWrapperRecognizerp;
  marpaESLIF_rule_t                *rulep;
  short                             completeb;
  short                             rcb;
  marpaWrapperRecognizerProgress_t *progressp;
  size_t                            nProgressl;
  size_t                            progressl;
  int                               rulei;
  int                               positioni;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (! marpaWrapperRecognizer_progressb(marpaWrapperRecognizerp, -1, -1, &nProgressl, &progressp)) {
    goto err;
  }

  completeb = 0;
  for (progressl = 0; progressl < nProgressl; progressl++) {
    rulei     = progressp[progressl].rulei;
    positioni = progressp[progressl].positioni;

    if (positioni != -1) {
      continue;
    }

    /* Rule completion - get the LHS symbol */
    MARPAESLIF_INTERNAL_GET_RULE_FROM_STACK(marpaESLIFp, rulep, ruleStackp, rulei);
    if (rulep->lhsp->idi == grammarp->starti) {
      MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "Start symbol completion detected");
      completeb = 1;
      break;
    }
  }

  if (completebp != NULL) {
    *completebp = completeb;
  }
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_lexeme_alternativeb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_alternative_t *alternativep)
/*****************************************************************************/
{
  static const char    *funcs              = "_marpaESLIFRecognizer_lexeme_alternativeb";
  genericStack_t       *lexemeInputStackp  = marpaESLIFRecognizerp->lexemeInputStackp;
  size_t                lastSizel;
  short                 rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (! _marpaESLIFRecognizer_lexemeStack_i_setb(marpaESLIFRecognizerp, lexemeInputStackp, GENERICSTACK_USED(lexemeInputStackp), &(alternativep->marpaESLIFValueResult))) {
    goto err;
  }
  /* alternative is now in the lexemeStack - remember that */
  MARPAESLIF_MAKE_MARPAESLIFVALUERESULT_SHALLOW(alternativep->marpaESLIFValueResult);

  if (! _marpaESLIFRecognizer_alternative_and_valueb(marpaESLIFRecognizerp, alternativep, GENERICSTACK_USED(lexemeInputStackp) - 1)) {
    goto err;
  }

  if (marpaESLIFRecognizerp->pristineb) {
    /* Remember that this recognizer is not pristine anymore */
    MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "Remembering that recognizer is not pristine");
    marpaESLIFRecognizerp->pristineb = 0;
  } else {
    /* Increment lastSizeBeforeCompletionl */
    lastSizel = marpaESLIFRecognizerp->lastSizel;
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "lastSizel %ld -> %ld", (unsigned long) lastSizel, (unsigned long) (lastSizel + marpaESLIFRecognizerp->previousMaxMatchedl));
    if ((marpaESLIFRecognizerp->lastSizel += marpaESLIFRecognizerp->previousMaxMatchedl) < lastSizel) {
      /* Paranoid case */
      MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "size_t turnaround when computing lastSizel");
      goto err;
    }
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
short marpaESLIFRecognizer_lexeme_alternativeb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIFAlternative_t *marpaESLIFAlternativep)
/*****************************************************************************/
{
  static const char       *funcs = "marpaESLIFRecognizer_lexeme_alternativeb";
  marpaESLIF_t            *marpaESLIFp;
  marpaESLIFGrammar_t     *marpaESLIFGrammarp;
  marpaESLIF_grammar_t    *grammarp;
  marpaESLIFSymbol_t      *symbolp;
  char                    *lexemes;
  marpaESLIF_alternative_t alternative;
  short                    rcb;

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }
  marpaESLIFp        = marpaESLIFRecognizerp->marpaESLIFp;
  marpaESLIFGrammarp = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  grammarp           = marpaESLIFGrammarp->grammarp;

  /* It is very important to do that NOW because _marpaESLIFRecognizer_lexeme_alternativeb() */
  /* is not an atomic operation, and replaced the alternative's value to indicate it is ok. */
  alternative.marpaESLIFValueResult = marpaESLIFValueResultUndef;
  
  if (marpaESLIFAlternativep == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Alternative is NULL");
    goto err;
  }

  if (marpaESLIFAlternativep->value.contextp == NULL) {
    /* Context from the external world must be set */
    MARPAESLIF_ERRORF(marpaESLIFp, "%s must be called with a context != NULL", funcs);
    goto err;
  }

  lexemes = marpaESLIFAlternativep->lexemes;
  if (lexemes == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Lexeme name is NULL");
    goto err;
  }

  if (marpaESLIFAlternativep->grammarLengthl <= 0) {
    MARPAESLIF_ERROR(marpaESLIFp, "grammarLengthl cannot be <= 0");
    goto err;
  }

  symbolp = _marpaESLIF_symbol_findp(marpaESLIFp, grammarp, lexemes, -1, NULL /* symbolip */);
  if (symbolp == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "Failed to find symbol <%s>", lexemes);
    goto err;
  }

  if (! MARPAESLIF_IS_LEXEME(symbolp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "Symbol <%s> is not a lexeme", lexemes);
    goto err;
  }
  
  alternative.symbolp               = symbolp;
  alternative.marpaESLIFValueResult = marpaESLIFAlternativep->value;
  alternative.grammarLengthi        = (int) marpaESLIFAlternativep->grammarLengthl;
  alternative.usedb                 = 1;

  if (! _marpaESLIFRecognizer_lexeme_alternativeb(marpaESLIFRecognizerp, &alternative)) {
    goto err;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_alternative_and_valueb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_alternative_t *alternativep, int valuei)
/*****************************************************************************/
{
  static const char   *funcs                           = "_marpaESLIFRecognizer_alternative_and_valueb";
  marpaESLIF_t        *marpaESLIFp                     = marpaESLIFRecognizerp->marpaESLIFp;
  genericStack_t      *commitedAlternativeStackSymbolp = marpaESLIFRecognizerp->commitedAlternativeStackSymbolp;
  marpaESLIF_symbol_t *symbolp                         = alternativep->symbolp;
  short                rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

#ifndef MARPAESLIF_NTRACE
  if (symbolp->type == MARPAESLIF_SYMBOL_TYPE_TERMINAL) {
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Committing terminal alternative %s at input stack %d", symbolp->descp->asciis, valuei);
  } else {
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Committing meta alternative <%s> at input stack %d", symbolp->descp->asciis, valuei);
  }
#endif

  if (! marpaWrapperRecognizer_alternativeb(marpaESLIFRecognizerp->marpaWrapperRecognizerp, symbolp->idi, valuei, alternativep->grammarLengthi)) {
    goto err;
  }
  GENERICSTACK_PUSH_PTR(commitedAlternativeStackSymbolp, symbolp);
  if (GENERICSTACK_ERROR(commitedAlternativeStackSymbolp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "commitedAlternativeStackSymbolp push failure, %s", strerror(errno));
    goto err;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_isEofb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short *eofbp)
/*****************************************************************************/
{
  /* This method never fails */
  static const char *funcs = "_marpaESLIFRecognizer_isEofb";

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "isEofb=%d", (int) marpaESLIFRecognizerp->marpaESLIF_streamp->eofb);
  if (eofbp != NULL) {
    *eofbp = marpaESLIFRecognizerp->marpaESLIF_streamp->eofb;
  }

  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "return 1");
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;

  return 1;
}

/*****************************************************************************/
short marpaESLIFRecognizer_isEofb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short *eofbp)
/*****************************************************************************/
{
  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return _marpaESLIFRecognizer_isEofb(marpaESLIFRecognizerp, eofbp);
}

/*****************************************************************************/
short marpaESLIFRecognizer_lexeme_completeb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, size_t lengthl)
/*****************************************************************************/
{
  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return _marpaESLIFRecognizer_lexeme_completeb(marpaESLIFRecognizerp, lengthl);
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_lexeme_completeb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, size_t lengthl)
/*****************************************************************************/
{
  static const char                *funcs                           = "_marpaESLIFRecognizer_lexeme_completeb";
  marpaESLIFGrammar_t              *marpaESLIFGrammarp              = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  marpaESLIF_grammar_t             *grammarp                        = marpaESLIFGrammarp->grammarp;
  genericStack_t                   *commitedAlternativeStackSymbolp = marpaESLIFRecognizerp->commitedAlternativeStackSymbolp;
  marpaESLIF_stream_t              *marpaESLIF_streamp              = marpaESLIFRecognizerp->marpaESLIF_streamp;
  char                             *inputs                          = marpaESLIF_streamp->inputs;
  genericStack_t                   *set2InputStackp;
  int                               commitedAlternativei;
  marpaESLIF_symbol_t              *symbolp;
  short                             rcb;
  int                               latestEarleySetIdi;
  GENERICSTACKITEMTYPE2TYPE_ARRAY   array;
  char                             *currentOffsetp;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  /* The user may give a length bigger than what we have */
  while (lengthl > marpaESLIF_streamp->inputl) {
    if (! marpaESLIF_streamp->eofb) {
      if (! _marpaESLIFRecognizer_readb(marpaESLIFRecognizerp)) {
        goto err;
      }
      /* We are caching inputs for performance, but this is dangerous because */
      /* _marpaESLIFRecognizer_read() can change it */
      inputs = marpaESLIF_streamp->inputs;
    } else {
      MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "Completion length is %ld but must be <= %ld (number of remaining bytes in the recognizer internal buffer)", (unsigned long) lengthl, (unsigned long) marpaESLIF_streamp->inputl);
      goto err;
    }
  }

#ifndef MARPAESLIF_NTRACE
  /* This should never happen in production */
  if (GENERICSTACK_USED(commitedAlternativeStackSymbolp) <= 0) {
    MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "commitedAlternativeStackSymbolp is empty");
    goto err;
  }
#endif

  /* set latest earleme set id mapping if trackb is true */
  if (marpaESLIFRecognizerp->marpaESLIFRecognizerOption.trackb) {
    /* Get latest earleme set id */
    if (!  marpaWrapperRecognizer_latestb(marpaESLIFRecognizerp->marpaWrapperRecognizerp, &latestEarleySetIdi)) {
      goto err;
    }

    /* Map latest earley set to an offset and a length to start of input */
    currentOffsetp  = (char *) (inputs - marpaESLIF_streamp->buffers);
    currentOffsetp += (size_t) marpaESLIF_streamp->globalOffsetp;

    GENERICSTACK_ARRAY_PTR(array)    = currentOffsetp;
    GENERICSTACK_ARRAY_LENGTH(array) = lengthl;

    set2InputStackp = marpaESLIFRecognizerp->set2InputStackp;
    GENERICSTACK_SET_ARRAY(set2InputStackp, array, latestEarleySetIdi);
    if (GENERICSTACK_ERROR(set2InputStackp)) {
      MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "set2InputStackp set failure, %s", strerror(errno));
      goto err;
    }
  }

  if (! marpaWrapperRecognizer_completeb(marpaESLIFRecognizerp->marpaWrapperRecognizerp)) {
    /* Regardless of failure or success, events should always be fetched as per the doc */
    _marpaESLIFRecognizer_push_grammar_eventsb(marpaESLIFRecognizerp);
    goto err;
  }

  /* New line processing, etc... */
  if (! _marpaESLIFRecognizer_matchPostProcessingb(marpaESLIFRecognizerp, lengthl)) {
    goto err;
  }

  /* Update internal position */
  if (lengthl > 0) {
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Lexeme complete: Advancing stream position %p by %ld bytes", marpaESLIF_streamp->inputs, (unsigned long) lengthl);
    marpaESLIF_streamp->inputs += lengthl;
    marpaESLIF_streamp->inputl -= lengthl;
  }

  /* Push grammar and eventual pause after events */
  MARPAESLIFRECOGNIZER_RESET_EVENTS(marpaESLIFRecognizerp);
  if (! _marpaESLIFRecognizer_push_grammar_eventsb(marpaESLIFRecognizerp)) {
    goto err;
  }
  for (commitedAlternativei = 0; commitedAlternativei < GENERICSTACK_USED(commitedAlternativeStackSymbolp); commitedAlternativei++) {
    symbolp = (marpaESLIF_symbol_t *) GENERICSTACK_GET_PTR(commitedAlternativeStackSymbolp, commitedAlternativei);
    if ((symbolp->eventAfters != NULL) && marpaESLIFRecognizerp->afterEventStatebp[symbolp->idi]) {
      if (! _marpaESLIFRecognizer_set_pauseb(marpaESLIFRecognizerp, grammarp, symbolp, inputs, lengthl)) {
        goto err;
      }
      if (! _marpaESLIFRecognizer_push_eventb(marpaESLIFRecognizerp, MARPAESLIF_EVENTTYPE_AFTER, symbolp, symbolp->eventAfters, NULL /* discardArrayp */)) {
        goto err;
      }
    }
  }

  /* We can reset commited alternatives */
  GENERICSTACK_USED(commitedAlternativeStackSymbolp) = 0;

  /* Reset any internal flag that prevent continutation */
  marpaESLIFRecognizerp->cannotcontinueb = 0;

  /* Remember the length */
  marpaESLIFRecognizerp->previousMaxMatchedl = lengthl;

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_lexeme_tryb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_grammar_t *grammarp, marpaESLIF_symbol_t *symbolp, short *matchbp)
/*****************************************************************************/
{
  static const char          *funcs  = "_marpaESLIFRecognizer_lexeme_tryb";
  short                       matchb = 0;
  char                       *valuep = NULL;
  size_t                      valuel;
  short                       rcb;
  marpaESLIF_matcher_value_t  rci;
  marpaESLIFValueResult_t     marpaESLIFValueResult;
  short                       rcMatcherb;

  rcMatcherb = _marpaESLIFRecognizer_symbol_matcherb(marpaESLIFRecognizerp,
                                                     symbolp,
                                                     &rci,
                                                     &marpaESLIFValueResult,
                                                     0, /* maxStartCompletionsi */
                                                     NULL, /* lastSizeBeforeCompletionlp */
                                                     NULL /* numberOfStartCompletionsip */);
  if (rcMatcherb < 0) {
    goto err;
  }
  if (rcMatcherb) {
    matchb = (rci == MARPAESLIF_MATCH_OK);
  } else {
    matchb = 0;
  }

  if (matchb) {
    /* Remember the data, NULL or not - per def a lexeme coming our from the recognizer is always an array -; */
    valuep = marpaESLIFValueResult.u.a.p;
    valuel = marpaESLIFValueResult.u.a.sizel;
    if (! _marpaESLIFRecognizer_set_tryb(marpaESLIFRecognizerp, grammarp, symbolp, valuep, valuel)) {
      goto err;
    }
  }
  
  if (matchbp != NULL) {
    *matchbp = matchb;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  /* We NEVER free because we called for parsing in lexeme mode */
  /*
  if (valuep != NULL) {
    free(valuep);
  }
  */
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_discard_tryb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_grammar_t *grammarp, marpaESLIF_symbol_t *symbolp, short *matchbp)
/*****************************************************************************/
{
  static const char           *funcs                             = "_marpaESLIFRecognizer_discard_tryb";
  marpaESLIFGrammar_t         *marpaESLIFGrammarp                = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  marpaESLIFGrammar_t          marpaESLIFGrammarDiscard          = *marpaESLIFGrammarp; /* Fake marpaESLIFGrammar with the grammar sent in the stack */
  marpaESLIFValueOption_t      marpaESLIFValueOptionDiscard      = marpaESLIFValueOption_default_template;
  marpaESLIF_grammar_t         grammarDiscard                    = *grammarp;
  marpaESLIFRecognizerOption_t marpaESLIFRecognizerOptionDiscard = marpaESLIFRecognizerp->marpaESLIFRecognizerOption; /* Things overwriten, see below */
  char                        *valuep = NULL;
  size_t                       valuel;
  short                        rcb;
  short                        matchb = 0;
  marpaESLIFValueResult_t      marpaESLIFValueResult;

  grammarDiscard.starti             = grammarDiscard.discardi;
  marpaESLIFGrammarDiscard.grammarp = &grammarDiscard;

  marpaESLIFRecognizerOptionDiscard.disableThresholdb = 1; /* If discard, prepare the option to disable threshold */
  marpaESLIFRecognizerOptionDiscard.exhaustedb        = 1; /* ... and have the exhausted event */
  marpaESLIFRecognizerOptionDiscard.newlineb          = 0; /* ... do not count line/column numbers */
  marpaESLIFRecognizerOptionDiscard.trackb            = 0; /* ... do not track absolute position */

  /* It is important to select marpaESLIFRecognizerp->noEventb because only this grammar can be cached */
  /* nevertheles event if the sub-grammar MAY generate an event, end-user will not see it because we */
  /* will never propagate it. This is why the two following lines have to remain commented: they have */
  /* a meaning only for the true recognizer (c.f. method resume_oneb) */
  /*
  marpaESLIFRecognizerp->discardEvents  = NULL;
  marpaESLIFRecognizerp->discardSymbolp = NULL;
  */
  matchb = _marpaESLIFGrammar_parseb(&marpaESLIFGrammarDiscard,
                                     &marpaESLIFRecognizerOptionDiscard,
                                     &marpaESLIFValueOptionDiscard,
                                     1, /* discardb */
                                     marpaESLIFRecognizerp->noEventb, /* This will select marpaWrapperGrammarDiscardNoEventp or marpaWrapperGrammarDiscardp */
                                     1, /* silentb */
                                     marpaESLIFRecognizerp, /* marpaESLIFRecognizerParentp */
                                     NULL, /* isExhaustedbp */
                                     &marpaESLIFValueResult,
                                     0, /* maxStartCompletionsi */
                                     NULL, /* lastSizeBeforeCompletionlp */
                                     NULL /* numberOfStartCompletionsip */,
                                     1 /* grammarIsOnStackb */);
  if (matchb) {
    /* Remember the data, NULL or not - per def a lexeme coming our from the recognizer is always an array -; */
    valuep = marpaESLIFValueResult.u.a.p;
    valuel = marpaESLIFValueResult.u.a.sizel;
    if (! _marpaESLIFRecognizer_set_tryb(marpaESLIFRecognizerp, grammarp, symbolp, valuep, valuel)) {
      goto err;
    }
  }
  
  if (matchbp != NULL) {
    *matchbp = matchb;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  /* We NEVER free because we called for parsing in lexeme mode */
  /*
  if (valuep != NULL) {
    free(valuep);
  }
  */
  return rcb;
}

/*****************************************************************************/
short marpaESLIFRecognizer_lexeme_readb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIFAlternative_t *marpaESLIFAlternativep, size_t lengthl)
/*****************************************************************************/
{
  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return
    marpaESLIFRecognizer_lexeme_alternativeb(marpaESLIFRecognizerp, marpaESLIFAlternativep) &&
    marpaESLIFRecognizer_lexeme_completeb(marpaESLIFRecognizerp, lengthl);
}

/*****************************************************************************/
short marpaESLIFRecognizer_lexeme_tryb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char *lexemes, short *matchbp)
/*****************************************************************************/
{
  marpaESLIF_t            *marpaESLIFp;
  marpaESLIFGrammar_t     *marpaESLIFGrammarp;
  marpaESLIF_grammar_t    *grammarp;
  marpaESLIFSymbol_t      *symbolp;
  short                    rcb;

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }
  marpaESLIFp        = marpaESLIFRecognizerp->marpaESLIFp;
  marpaESLIFGrammarp = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  grammarp           = marpaESLIFGrammarp->grammarp;

  if (lexemes == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Lexeme name is NULL");
    goto err;
  }

  symbolp = _marpaESLIF_symbol_findp(marpaESLIFp, grammarp, lexemes, -1, NULL /* symbolip */);
  if (symbolp == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "Failed to find symbol <%s>", lexemes);
    goto err;
  }

  if (! MARPAESLIF_IS_LEXEME(symbolp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "Symbol <%s> is not a lexeme", lexemes);
    goto err;
  }

  if (! _marpaESLIFRecognizer_lexeme_tryb(marpaESLIFRecognizerp, grammarp, symbolp, matchbp)) {
    goto err;
  }
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIFRecognizer_discard_tryb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short *matchbp)
/*****************************************************************************/
{
  marpaESLIF_t            *marpaESLIFp;
  marpaESLIFGrammar_t     *marpaESLIFGrammarp;
  marpaESLIF_grammar_t    *grammarp;
  short                    rcb;

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }
  marpaESLIFp        = marpaESLIFRecognizerp->marpaESLIFp;
  marpaESLIFGrammarp = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  grammarp           = marpaESLIFGrammarp->grammarp;

  if (grammarp->discardp == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Grammar has no <:discard>");
    goto err;
  }

  if (! _marpaESLIFRecognizer_discard_tryb(marpaESLIFRecognizerp, grammarp, grammarp->discardp, matchbp)) {
    goto err;
  }
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIFRecognizer_event_onoffb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char *symbols, marpaESLIFEventType_t eventSeti, short onoffb)
/*****************************************************************************/
{
  static const char    *funcs           = "marpaESLIFRecognizer_event_onoffb";
  static const int      nativeEventSeti = MARPAESLIF_EVENTTYPE_COMPLETED|MARPAESLIF_EVENTTYPE_NULLED|MARPAESLIF_EVENTTYPE_PREDICTED;
  marpaESLIF_t         *marpaESLIFp;
  marpaESLIFGrammar_t  *marpaESLIFGrammarp;
  marpaESLIF_grammar_t *grammarp;
  marpaESLIFSymbol_t   *symbolp;
  int                   seti;
  short                 rcb;

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }
  marpaESLIFp        = marpaESLIFRecognizerp->marpaESLIFp;
  marpaESLIFGrammarp = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  grammarp           = marpaESLIFGrammarp->grammarp;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (marpaESLIFRecognizerp == NULL) {
    goto err;
  }
  
  if (symbols == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Symbol name is NULL");
    goto err;
  }

  symbolp = _marpaESLIF_symbol_findp(marpaESLIFp, grammarp, symbols, -1, NULL /* symbolip */);
  if (symbolp == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "Failed to find symbol <%s>", symbols);
    goto err;
  }

  /* We are more than events than native marpa, split them */
  
  /* Of course, this part of marpaESLIFEventType_t is strictly equivalent to marpaWrapperGrammarEventType_t -; */
  seti = eventSeti;
  seti &= nativeEventSeti;
  if (seti != MARPAWRAPPERGRAMMAR_EVENTTYPE_NONE) {
    if (! marpaWrapperRecognizer_event_onoffb(marpaESLIFRecognizerp->marpaWrapperRecognizerp, symbolp->idi, (marpaWrapperGrammarEventType_t) eventSeti, (int) onoffb)) {
      goto err;
    }
  }

  /* Comes our specific parts: lexeme before, lexeme after, exhaustion and discard */
  if ((eventSeti & MARPAESLIF_EVENTTYPE_BEFORE) == MARPAESLIF_EVENTTYPE_BEFORE) {
    marpaESLIFRecognizerp->beforeEventStatebp[symbolp->idi] = onoffb;
  }
  if ((eventSeti & MARPAESLIF_EVENTTYPE_AFTER) == MARPAESLIF_EVENTTYPE_AFTER) {
    marpaESLIFRecognizerp->afterEventStatebp[symbolp->idi] = onoffb;
  }
  if ((eventSeti & MARPAESLIF_EVENTTYPE_DISCARD) == MARPAESLIF_EVENTTYPE_DISCARD) {
    marpaESLIFRecognizerp->discardEventStatebp[symbolp->idi] = onoffb;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
short marpaESLIFRecognizer_lexeme_expectedb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, size_t *nLexemelp, char ***lexemesArraypp)
/*****************************************************************************/
{
  static const char    *funcs = "marpaESLIFRecognizer_expectedb";
  size_t                nSymboll;
  int                  *symbolArrayp;
  short                 rcb;
  marpaESLIF_t         *marpaESLIFp;
  marpaESLIFGrammar_t  *marpaESLIFGrammarp;
  marpaESLIF_grammar_t *grammarp;
  int                   symboli;
  size_t                symboll;
  marpaESLIF_symbol_t  *symbolp;
  size_t                nLexemel;
  char                **lexemesArrayp;
  size_t                tmpl;
  char                **tmpsp;
  size_t                lexemesArrayAllocl; /* Current allocated size -; */

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    goto err;
  }

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (! marpaWrapperRecognizer_expectedb(marpaESLIFRecognizerp->marpaWrapperRecognizerp, &nSymboll, &symbolArrayp)) {
    goto err;
  }

  marpaESLIFp        = marpaESLIFRecognizerp->marpaESLIFp;
  marpaESLIFGrammarp = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  grammarp           = marpaESLIFGrammarp->grammarp; /* Current grammar */
  lexemesArrayp      = marpaESLIFRecognizerp->lexemesArrayp;
  lexemesArrayAllocl = marpaESLIFRecognizerp->lexemesArrayAllocl;

  /* We filter to lexemes only */
  nLexemel = 0;
  for (symboll = 0; symboll < nSymboll; symboll++) {
    symboli = symbolArrayp[symboll];
    symbolp = _marpaESLIF_symbol_findp(marpaESLIFp, grammarp, NULL /* asciis */, symboli, NULL /* symbolip */);
    if (symbolp == NULL) {
      goto err;
    }
    if (symbolp->type != MARPAESLIF_SYMBOL_TYPE_META) {
      continue;
    }
    nLexemel++;

    /* Prepare/use internal buffer */
    if (lexemesArrayAllocl <= 0) {
      tmpsp = (char **) malloc(sizeof(char **) * nLexemel);
      if (tmpsp == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
        goto err;
      }
      lexemesArrayAllocl = marpaESLIFRecognizerp->lexemesArrayAllocl = nLexemel;
      lexemesArrayp      = marpaESLIFRecognizerp->lexemesArrayp      = tmpsp;
    } else if (nLexemel > lexemesArrayAllocl) {
      tmpl  = lexemesArrayAllocl * 2;
      tmpsp = (char **) realloc(lexemesArrayp, sizeof(char **) * tmpl);
      if (tmpsp == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "realloc failure, %s", strerror(errno));
        goto err;
      }
      lexemesArrayAllocl = marpaESLIFRecognizerp->lexemesArrayAllocl = tmpl;
      lexemesArrayp      = marpaESLIFRecognizerp->lexemesArrayp       = tmpsp;
    }

    /* We use symbolp->u.metap->asciinames that is persisent */
    lexemesArrayp[nLexemel - 1] = symbolp->u.metap->asciinames;
  }

  /* Makes sure we reset the others - not needed but more beautiful from debugger perspective -; */
  for (symboll = nLexemel; symboll < lexemesArrayAllocl; symboll++) {
    lexemesArrayp[symboll] = NULL;
  }

  if (nLexemelp != NULL) {
    *nLexemelp = nLexemel;
  }
  if (lexemesArraypp != NULL) {
    *lexemesArraypp = lexemesArrayp;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
void marpaESLIFGrammar_freev(marpaESLIFGrammar_t *marpaESLIFGrammarp)
/*****************************************************************************/
{
  _marpaESLIFGrammar_freev(marpaESLIFGrammarp, 0 /* onStackb */);
}

/*****************************************************************************/
static inline void _marpaESLIFrecognizer_lexemeData_freev(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_lexeme_data_t **lexemeDatapp)
/*****************************************************************************/
{
  static const char        *funcs       = "_marpaESLIFrecognizer_lexemeData_freev";
  marpaESLIFGrammar_t      *marpaESLIFGrammarp;
  genericStack_t           *symbolStackp;
  marpaESLIF_grammar_t     *grammarp;
  int                       symboli;
  marpaESLIF_lexeme_data_t *lexemeDatap;

  if (lexemeDatapp != NULL) {
    marpaESLIFGrammarp = marpaESLIFRecognizerp->marpaESLIFGrammarp;
    grammarp = marpaESLIFGrammarp->grammarp;
    symbolStackp = grammarp->symbolStackp;

    for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
      lexemeDatap = lexemeDatapp[symboli];
      if (lexemeDatap != NULL) {
        if (lexemeDatap->bytes != NULL) {
          free(lexemeDatap->bytes);
        }
        free(lexemeDatap);
      }
    }
    free(lexemeDatapp);
  }
}

/*****************************************************************************/
static inline void _marpaESLIFrecognizer_lastPause_freev(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  _marpaESLIFrecognizer_lexemeData_freev(marpaESLIFRecognizerp, marpaESLIFRecognizerp->lastPausepp);
}

/*****************************************************************************/
static inline void _marpaESLIFrecognizer_lastTry_freev(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  _marpaESLIFrecognizer_lexemeData_freev(marpaESLIFRecognizerp, marpaESLIFRecognizerp->lastTrypp);
}

/*****************************************************************************/
void marpaESLIFRecognizer_freev(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  if (marpaESLIFRecognizerp != NULL) {
    _marpaESLIFRecognizer_freev(marpaESLIFRecognizerp, 0 /* forceb */);
  }
}

/*****************************************************************************/
short marpaESLIFGrammar_parseb(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaESLIFRecognizerOption_t *marpaESLIFRecognizerOptionp, marpaESLIFValueOption_t *marpaESLIFValueOptionp, short *isExhaustedbp)
/*****************************************************************************/
{
  return marpaESLIFGrammar_parse_by_levelb(marpaESLIFGrammarp, marpaESLIFRecognizerOptionp, marpaESLIFValueOptionp, isExhaustedbp, 0 /* leveli */, NULL /* descp */);
}

/*****************************************************************************/
short marpaESLIFGrammar_parse_by_levelb(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaESLIFRecognizerOption_t *marpaESLIFRecognizerOptionp, marpaESLIFValueOption_t *marpaESLIFValueOptionp, short *isExhaustedbp, int leveli, marpaESLIFString_t *descp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;
  short                 rcb;
  marpaESLIFGrammar_t   marpaESLIFGrammar;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = _marpaESLIFGrammar_grammar_findp(marpaESLIFGrammarp, leveli, descp);
  if (grammarp == NULL) {
    goto err;
  }

  /* Use a local marpaESLIFGrammar and change current grammar */
  marpaESLIFGrammar          = *marpaESLIFGrammarp;
  marpaESLIFGrammar.grammarp = grammarp;
  if (! _marpaESLIFGrammar_parseb(&marpaESLIFGrammar,
                                  marpaESLIFRecognizerOptionp,
                                  marpaESLIFValueOptionp,
                                  0, /* discardb */
                                  1, /* noEventb - this will make the recognizer use marpaWrapperGrammarStartNoEventp */
                                  0, /* silentb */
                                  NULL, /* marpaESLIFRecognizerParentp */
                                  isExhaustedbp,
                                  NULL, /* marpaESLIFValueResultp */
                                  0, /* maxStartCompletionsi */
                                  NULL, /* lastSizeBeforeCompletionlp */
                                  NULL /* numberOfStartCompletionsip */,
                                  1 /* grammarIsOnStackb */)) {
    goto err;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIFReader_grammarReader(void *userDatavp, char **inputsp, size_t *inputlp, short *eofbp, short *characterStreambp, char **encodingsp, size_t *encodinglp)
/*****************************************************************************/
{
  static const char          *funcs                     = "marpaESLIFReader_grammarReader";
  marpaESLIF_readerContext_t *marpaESLIF_readerContextp = (marpaESLIF_readerContext_t *) userDatavp;
#ifndef MARPAESLIF_NTRACE
  marpaESLIF_t               *marpaESLIFp               = marpaESLIF_readerContextp->marpaESLIFp;
#endif

  *inputsp              = (char *) marpaESLIF_readerContextp->marpaESLIFGrammarOptionp->bytep;
  *inputlp              = marpaESLIF_readerContextp->marpaESLIFGrammarOptionp->bytel;
  *eofbp                = 1;
  *characterStreambp    = 1; /* We say this is a stream of characters */
  *encodingsp           = marpaESLIF_readerContextp->marpaESLIFGrammarOptionp->encodings;
  *encodinglp           = marpaESLIF_readerContextp->marpaESLIFGrammarOptionp->encodingl;

#ifndef MARPAESLIF_NTRACE
  MARPAESLIF_TRACEF(marpaESLIFp, funcs, "return 1 (*inputsp=%p, *inputlp=%ld, *eofbp=%d, *characterStreambp=%d)", *inputsp, (unsigned long) *inputlp, (int) *eofbp, (int) *characterStreambp);
#endif

  return 1;
}

/*****************************************************************************/
static inline marpaESLIF_grammar_t *_marpaESLIFGrammar_grammar_findp(marpaESLIFGrammar_t *marpaESLIFGrammarp, int leveli, marpaESLIF_string_t *descp)
/*****************************************************************************/
{
  static const char    *funcs         = "_marpaESLIFGrammar_grammar_findp";
  genericStack_t       *grammarStackp = marpaESLIFGrammarp->grammarStackp;
  marpaESLIF_string_t  *utf8p         = NULL;
  marpaESLIF_grammar_t *rcp           = NULL;
  marpaESLIF_grammar_t *grammarp;
  int                   i;

  if (descp != NULL) {
    /* Search by description has precedence */
    if ((utf8p = _marpaESLIF_string2utf8p(marpaESLIFGrammarp->marpaESLIFp, descp, 0 /* tconvsilentb */)) != NULL) {
      for (i = 0; i < GENERICSTACK_USED(grammarStackp); i++) {
        if (! GENERICSTACK_IS_PTR(grammarStackp, i)) {
          /* Sparse array */
          continue;
        }
        grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, i);
        if (_marpaESLIF_string_eqb(grammarp->descp, descp)) {
          rcp = grammarp;
          break;
        }
      }
    }
  } else if (leveli >= 0) {
    if (GENERICSTACK_IS_PTR(grammarStackp, leveli)) {
      rcp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, leveli);
    }
  }

  if ((utf8p != NULL) && (utf8p != descp)) {
    _marpaESLIF_string_freev(utf8p, 0 /* onStackb */);
  }

  return rcp;
}
 
/*****************************************************************************/
static inline marpaESLIF_rule_t *_marpaESLIF_rule_findp(marpaESLIF_t *marpaESLIFp, marpaESLIF_grammar_t *grammarp, int rulei)
/*****************************************************************************/
{
  static const char    *funcs        = "_marpaESLIF_rule_findp";
  genericStack_t       *ruleStackp   = grammarp->ruleStackp;
  marpaESLIF_rule_t    *rulep;

  if (rulei < 0) {
    MARPAESLIF_ERRORF(marpaESLIFp, "Invalid rule ID %d", rulei);
    goto err;
  }

  MARPAESLIF_INTERNAL_GET_RULE_FROM_STACK(marpaESLIFp, rulep, ruleStackp, rulei);
  goto done;

 err:
  rulep = NULL;

 done:
  return rulep;
}

/*****************************************************************************/
static inline marpaESLIF_symbol_t *_marpaESLIF_symbol_findp(marpaESLIF_t *marpaESLIFp, marpaESLIF_grammar_t *grammarp, char *asciis, int symboli, int *symbolip)
/*****************************************************************************/
{
  static const char    *funcs        = "_marpaESLIF_symbol_findp";
  genericStack_t       *symbolStackp = grammarp->symbolStackp;
  marpaESLIF_symbol_t  *rcp;
  marpaESLIF_symbol_t  *symbolp;
  int                   i;

  /* Give precedence to symbol by name - which is possible only for meta symbols */
  if (asciis != NULL) {
    rcp = NULL;
    for (i = 0; i < GENERICSTACK_USED(symbolStackp); i++) {
      MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, i);
      if (symbolp->type == MARPAESLIF_SYMBOL_TYPE_META) {
        if (strcmp(asciis, symbolp->u.metap->asciinames) == 0) {
          rcp = symbolp;
          break;
        }
      }
    }
    if (rcp == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFp, "Unknown symbol <%s>", asciis);
      goto err;
    }
  } else if (symboli >= 0) {
    if (! GENERICSTACK_IS_PTR(symbolStackp, symboli)) {
      MARPAESLIF_ERRORF(marpaESLIFp, "Unknown symbol ID %d", symboli);
      goto err;
    }
    rcp = (marpaESLIF_symbol_t *) GENERICSTACK_GET_PTR(symbolStackp, symboli);
  } else {
    MARPAESLIF_ERRORF(marpaESLIFp, "Invalid symbol ID %d", symboli);
    goto err;
  }

  /* Here rcp is != NULL per def */
  if (symbolip != NULL) {
    *symbolip = rcp->idi;
  }
  goto done;

 err:
  rcp = NULL;
  errno = EINVAL;

 done:
  return rcp;
}

/*****************************************************************************/
short marpaESLIFRecognizer_eventb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, size_t *eventArraylp, marpaESLIFEvent_t **eventArraypp)
/*****************************************************************************/
{
  static const char *funcs = "marpaESLIFRecognizer_eventb";
  short              rcb;

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  /* We want to always make sure that grammar events are fetched */
  /* although without duplicates */
  _marpaESLIFRecognizer_clear_grammar_eventsb(marpaESLIFRecognizerp);
  if (! _marpaESLIFRecognizer_push_grammar_eventsb(marpaESLIFRecognizerp)) {
    goto err;
  }

  if (eventArraylp != NULL) {
    *eventArraylp = marpaESLIFRecognizerp->eventArrayl;
  }
  if (eventArraypp != NULL) {
    *eventArraypp = marpaESLIFRecognizerp->eventArrayp;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;

  return rcb;
}

/*****************************************************************************/
 static inline short _marpaESLIFRecognizer_push_eventb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIFEventType_t type, marpaESLIF_symbol_t *symbolp, char *events, marpaESLIFValueResultArray_t *discardArrayp)
/*****************************************************************************/
{
  static const char *funcs        = "_marpaESLIFRecognizer_push_eventb";
  marpaESLIFEvent_t *eventArrayp;
  size_t             eventArrayl;
  size_t             eventArraySizel;
  marpaESLIFEvent_t  eventArray;
  char              *tmpp;
  short              rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  /* As a general protection, we never push events when there is a parent recognizer

   because a push of event systematically involve a break of resuming.
   And in the two conditions listed upper we never want to break: the
   user space should have no control on the workflow in case of
   sub-reconizer or exception (which has no parent recognizer, 
   but remains an internal thingy)

   It is exactly for this reason that internal recognizers propagate the exception internal flag
   because they NEED this information anyway, regardless if they have a parent recognizer, if
   they are in the exception mode, or not.

   Memory management of the event array is done so that free/malloc/realloc are avoided as much as
   possible.
  */
  if (marpaESLIFRecognizerp->parentRecognizerp != NULL) {
    goto no_push;
  }

  /* Detect hook events and process them instead of pushing to the end-user */
  if ((events != NULL) && (events[0] == ':')) {
    if (strcmp(events, ":discard[on]") == 0) {
      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s: event %s: enabling discard", (symbolp != NULL) ? symbolp->descp->asciis : "??", events);
      if (! _marpaESLIFRecognizer_hook_discardb(marpaESLIFRecognizerp, 1)) {
        goto err;
      }
      goto no_push;
    } else if (strcmp(events, ":discard[off]") == 0) {
      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s: event %s: disabling discard", (symbolp != NULL) ? symbolp->descp->asciis : "??", events);
      if (! _marpaESLIFRecognizer_hook_discardb(marpaESLIFRecognizerp, 0)) {
        goto err;
      }
      goto no_push;
    } else if (strcmp(events, ":discard[switch]") == 0) {
      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s: event %s: switching discard", (symbolp != NULL) ? symbolp->descp->asciis : "??", events);
      if (! _marpaESLIFRecognizer_hook_discard_switchb(marpaESLIFRecognizerp)) {
        goto err;
      }
      goto no_push;
    }
  }

  /* Hook for MARPAESLIF_EVENTTYPE_DISCARD: the associated symbol is NOT pushed to the grammar */
  /* because a discard is a transversal thing. So if the end user wants to retreive the last */
  /* discarded data, when trackb is on, he cannot. */
  if ((discardArrayp != NULL) && (discardArrayp->p != NULL) && (discardArrayp->sizel > 0)) {
    if (marpaESLIFRecognizerp->lastDiscards == NULL) {
      marpaESLIFRecognizerp->lastDiscards = (char *) malloc(discardArrayp->sizel + 1); /* Hiden NUL byte */
      if (marpaESLIFRecognizerp->lastDiscards == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "malloc failure, %s", strerror(errno));
        goto err;
      }
    } else if (marpaESLIFRecognizerp->lastDiscardl < discardArrayp->sizel) {
      tmpp = (char *) realloc(marpaESLIFRecognizerp->lastDiscards, discardArrayp->sizel + 1); /* Hiden NUL byte */
      if (tmpp == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "malloc failure, %s", strerror(errno));
        goto err;
      }
      marpaESLIFRecognizerp->lastDiscards = tmpp;
    }
    memcpy(marpaESLIFRecognizerp->lastDiscards, discardArrayp->p, discardArrayp->sizel);
    marpaESLIFRecognizerp->lastDiscardl = discardArrayp->sizel;
    marpaESLIFRecognizerp->lastDiscards[marpaESLIFRecognizerp->lastDiscardl] = '\0';
  }

  /* These statements have a cost - execute them only if we really push the event */
  eventArrayp     = marpaESLIFRecognizerp->eventArrayp;
  eventArraySizel = marpaESLIFRecognizerp->eventArraySizel;
  eventArrayl     = marpaESLIFRecognizerp->eventArrayl;

  eventArray.type    = type;
  eventArray.symbols = (symbolp != NULL) ? symbolp->descp->asciis : NULL;
  /* Support of ":symbol" is coded here */
  eventArray.events  = ((events != NULL) && (strcmp(events, ":symbol") == 0)) ? eventArray.symbols : events;

  /* Extend of create the array */
  /* marpaESLIFRecognizerp->eventArrayl is always in the range [0..marpaESLIFRecognizerp->eventArraySizel] */
  /* and is set to 0 at every reset */
  if (eventArraySizel <= eventArrayl) {
    if (eventArrayp == NULL) {
      /* In theory, here, eventArrayl can only have the value 1 */
      eventArraySizel = eventArrayl + 1;
      eventArrayp = malloc(eventArraySizel * sizeof(marpaESLIFEvent_t));
      if (eventArrayp == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "malloc failure, %s", strerror(errno));
        goto err;
      }
    } else {
      eventArraySizel *= 2;
      eventArrayp = realloc(eventArrayp, eventArraySizel * sizeof(marpaESLIFEvent_t));
      if (eventArrayp == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "realloc failure, %s", strerror(errno));
        goto err;
      }
    }
    marpaESLIFRecognizerp->eventArrayp     = eventArrayp;
    marpaESLIFRecognizerp->eventArraySizel = eventArraySizel;
  }

  eventArrayp[eventArrayl] = eventArray;
  if ((marpaESLIFRecognizerp->eventArrayl = ++eventArrayl) > 1) {
    /* Sort the events if there is more than one */
    _marpaESLIFRecognizer_sort_eventsb(marpaESLIFRecognizerp);
  }

 no_push:
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_set_lexemeDatab(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_grammar_t *grammarp, marpaESLIF_symbol_t *symbolp, char *datas, size_t datal, marpaESLIF_lexeme_data_t **lexemeDatapp)
/*****************************************************************************/
{
  static const char        *funcs      = "_marpaESLIFRecognizer_set_lexemeDatab";
  marpaESLIF_lexeme_data_t *lexemeDatap = lexemeDatapp[symbolp->idi];
  char                     *bytes;
  size_t                    byteSizel;
  short                     rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  /* lexemeDatapp is guaranteed to have been allocated. But this is originally an array of NULL pointers */
  if (lexemeDatap == NULL) {
    lexemeDatap = malloc(sizeof(marpaESLIF_lexeme_data_t));
    if (lexemeDatap == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "malloc failure, %s", strerror(errno));
      goto err;
    }
    lexemeDatap->bytes     = NULL;
    lexemeDatap->bytel     = 0;
    lexemeDatap->byteSizel = 0;

    lexemeDatapp[symbolp->idi] = lexemeDatap;
  }
  
  /* In theory it is not possible to have a pause event if there is a parent recognizer.

   Memory management of the pause chunk is done so that free/malloc/realloc are avoided as much as
   possible.
  */
  if (marpaESLIFRecognizerp->parentRecognizerp != NULL) {
    goto no_set;
  }
  /* These statements have a cost - execute them only if we really set the pause */
  byteSizel = lexemeDatap->byteSizel;

  /* Extend or create the chunk (take care: datal can be zero for a zero-length lexeme) */
  if ((byteSizel <= 0) || (byteSizel < datal)) {
    bytes     = lexemeDatap->bytes;
    byteSizel = datal;

    if (bytes == NULL) {
      bytes = (char *) malloc(byteSizel + 1); /* We always add a NUL byte for convenience */
      if (bytes == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "malloc failure, %s", strerror(errno));
        goto err;
      }
    } else {
      bytes = realloc(bytes, byteSizel + 1); /* We always add a NUL byte for convenience */
      if (bytes == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "realloc failure, %s", strerror(errno));
        goto err;
      }
    }
    lexemeDatap->bytes     = bytes;
    lexemeDatap->byteSizel = byteSizel;
  } else {
    bytes = lexemeDatap->bytes;
  }

  if (datal > 0) {
    memcpy(bytes, datas, datal);
  }
  bytes[datal] = '\0'; /* Just to make the debuggers happy - this is hiden */
  lexemeDatap->bytel = datal;

 no_set:
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_set_pauseb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_grammar_t *grammarp, marpaESLIF_symbol_t *symbolp, char *bytes, size_t bytel)
/*****************************************************************************/
{
  return _marpaESLIFRecognizer_set_lexemeDatab(marpaESLIFRecognizerp, grammarp, symbolp, bytes, bytel, marpaESLIFRecognizerp->lastPausepp);
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_set_tryb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_grammar_t *grammarp, marpaESLIF_symbol_t *symbolp, char *bytes, size_t bytel)
/*****************************************************************************/
{
  return _marpaESLIFRecognizer_set_lexemeDatab(marpaESLIFRecognizerp, grammarp, symbolp, bytes, bytel, marpaESLIFRecognizerp->lastTrypp);
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_push_grammar_eventsb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  static const char            *funcs              = "_marpaESLIFRecognizer_push_grammar_eventsb";
  marpaESLIFGrammar_t          *marpaESLIFGrammarp = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  marpaESLIF_grammar_t         *grammarp           = marpaESLIFGrammarp->grammarp;
  marpaESLIF_stream_t          *marpaESLIF_streamp = marpaESLIFRecognizerp->marpaESLIF_streamp;
  genericStack_t               *symbolStackp       = grammarp->symbolStackp;
  short                         last_discard_loopb = 0;
  short                         isExhaustedb       = 0;
  marpaESLIF_symbol_t          *symbolp;
  int                           symboli;
  size_t                        grammarEventl;
  marpaWrapperGrammarEvent_t   *grammarEventp;
  short                         rcb;
  char                         *events;
  size_t                        i;
  marpaESLIFEventType_t         type;
  /* These variables are used for the eventual last discard */
  marpaESLIFGrammar_t           marpaESLIFGrammarDiscard;
  marpaESLIF_grammar_t          grammarDiscard;
  marpaESLIFRecognizerOption_t  marpaESLIFRecognizerOptionDiscard;
  marpaESLIFValueOption_t       marpaESLIFValueOptionDiscard      = marpaESLIFValueOption_default_template;
  marpaESLIFValueResult_t       marpaESLIFValueResult;
  short                         continue_last_discard_loopb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  marpaESLIFRecognizerp->lastCompletionEvents  = NULL;
  marpaESLIFRecognizerp->lastCompletionSymbolp = NULL;
  marpaESLIFRecognizerp->completedb            = 0;

  /* Collect grammar native events and push them in the events stack */
  if (! marpaWrapperGrammar_eventb(marpaESLIFRecognizerp->marpaWrapperGrammarp,
                                   &grammarEventl,
                                   &grammarEventp,
                                   1, /* exhaustedb */
                                   0 /* forceReloadb */)) {
    goto err;
  }

  if (grammarEventl > 0) {

    for (i = 0; i < grammarEventl; i++) {
      symboli = grammarEventp[i].symboli;
      type    = MARPAESLIF_EVENTTYPE_NONE;
      events  = NULL;
      if (symboli >= 0) {
        /* Look for the symbol */
        MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFRecognizerp->marpaESLIFp, symbolp, symbolStackp, symboli);
      } else {
        symbolp = NULL;
      }

      /* Our grammar made sure there can by only one named event per symbol */
      /* In addition, marpaWrapper guarantee there is a symbol associated to */
      /* completion, nulled or prediction events */
      switch (grammarEventp[i].eventType) {
      case MARPAWRAPPERGRAMMAR_EVENT_COMPLETED:
        type        = MARPAESLIF_EVENTTYPE_COMPLETED;
        if (symbolp != NULL) {
          /* The discard event is only possible on completion in discard mode */
          marpaESLIFRecognizerp->lastCompletionEvents  = events = marpaESLIFRecognizerp->discardb ? symbolp->discardEvents : symbolp->eventCompleteds;
          marpaESLIFRecognizerp->lastCompletionSymbolp = symbolp;
        }
        marpaESLIFRecognizerp->completedb = 1;
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s: completion event", (symbolp != NULL) ? symbolp->descp->asciis : "??");
        break;
      case MARPAWRAPPERGRAMMAR_EVENT_NULLED:
        type        = MARPAESLIF_EVENTTYPE_NULLED;
        if (symbolp != NULL) {
          events = symbolp->eventNulleds;
        }
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s: nullable event", (symbolp != NULL) ? symbolp->descp->asciis : "??");
        break;
      case MARPAWRAPPERGRAMMAR_EVENT_EXPECTED:
        type        = MARPAESLIF_EVENTTYPE_PREDICTED;
        if (symbolp != NULL) {
          events = symbolp->eventPredicteds;
        }
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s: prediction event", (symbolp != NULL) ? symbolp->descp->asciis : "??");
        break;
      case MARPAWRAPPERGRAMMAR_EVENT_EXHAUSTED:
        type         = MARPAESLIF_EVENTTYPE_EXHAUSTED;
        events       = MARPAESLIF_EVENTTYPE_EXHAUSTED_NAME;
        isExhaustedb = 1;
        MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "Exhausted event");
        /* Force discard of remaining data */
        last_discard_loopb = 1;
        /* symboli will be -1 as per marpaWrapper spec */
        break;
      default:
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s: unsupported event type %d", (symbolp != NULL) ? symbolp->descp->asciis : "??", grammarEventp[i].eventType);
        break;
      }
      if (! _marpaESLIFRecognizer_push_eventb(marpaESLIFRecognizerp, type, symbolp, events, NULL /* discardArrayp */)) {
        goto err;
      }
    }
  }

  if (! isExhaustedb) {
    /* The event on exhaustion only occurs if needed to provide a reason to return. */
    /* If not sent by the grammar, we check explicitely ourself.                    */
    if (! _marpaESLIFRecognizer_isExhaustedb(marpaESLIFRecognizerp, &isExhaustedb)) {
      goto err;
    }
    if (isExhaustedb) {
      if (! _marpaESLIFRecognizer_push_eventb(marpaESLIFRecognizerp, MARPAESLIF_EVENTTYPE_EXHAUSTED, NULL /* symbolp */, MARPAESLIF_EVENTTYPE_EXHAUSTED_NAME, NULL /* discardArrayp */)) {
        goto err;
      }
      /* Force discard of remaining data */
      last_discard_loopb = 1;
      MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "Exhausted event (check mode)");
    }
  }

  if (last_discard_loopb) {
    /* By definition, here exhaustedb is a true value. */
    /* If we are not already in the discard mode, try to discard if discardOnOffb is true */
    /* This is done only for the top-level recognizer */
    if (
        (marpaESLIFRecognizerp->parentRecognizerp == NULL) &&   /* Top-level recognizer */
        marpaESLIFRecognizerp->discardOnOffb               &&   /* ... is not disabling discard */
        (grammarp->discardi >= 0)                               /* ... and have discard indeed */
        ) {
      /* This is the end the parsing (we are called when parsing is exhausted) - we try to discard as much as possible */
      /* to avoid the eventual error message "Grammar is exhausted but lexeme remains" */

      /* We can set these variables once */
      marpaESLIFGrammarDiscard                            = *marpaESLIFGrammarp; /* Fake marpaESLIFGrammar with the grammar sent in the stack */
      grammarDiscard                                      = *grammarp;
      grammarDiscard.starti                               = grammarDiscard.discardi;
      marpaESLIFGrammarDiscard.grammarp                   = &grammarDiscard;

      marpaESLIFRecognizerOptionDiscard                   = marpaESLIFRecognizerp->marpaESLIFRecognizerOption; /* Things overwriten, see below */
      marpaESLIFRecognizerOptionDiscard.disableThresholdb = 1; /* If discard, prepare the option to disable threshold */
      marpaESLIFRecognizerOptionDiscard.exhaustedb        = 1; /* ... and have the exhausted event */
      marpaESLIFRecognizerOptionDiscard.newlineb          = 0; /* ... do not count line/column numbers */
      marpaESLIFRecognizerOptionDiscard.trackb            = 0; /* ... do not count track absolute position */

      marpaESLIFValueOptionDiscard                        = marpaESLIFValueOption_default_template;

      do {
        /* Always reset this shallow pointer */
        marpaESLIFRecognizerp->discardEvents  = NULL;
        marpaESLIFRecognizerp->discardSymbolp = NULL;
        if (_marpaESLIFGrammar_parseb(&marpaESLIFGrammarDiscard,
                                      &marpaESLIFRecognizerOptionDiscard,
                                      &marpaESLIFValueOptionDiscard,
                                      1, /* discardb */
                                      marpaESLIFRecognizerp->noEventb, /* This will select marpaWrapperGrammarDiscardNoEventp or marpaWrapperGrammarDiscardp */
                                      1, /* silentb */
                                      marpaESLIFRecognizerp, /* marpaESLIFRecognizerParentp */
                                      NULL, /* isExhaustedbp */
                                      &marpaESLIFValueResult,
                                      0, /* maxStartCompletionsi */
                                      NULL, /* lastSizeBeforeCompletionlp */
                                      NULL /* numberOfStartCompletionsip */,
                                      1 /* grammarIsOnStackb */)) {
#ifndef MARPAESLIF_NTRACE
          MARPAESLIF_VALUECHECK_IF_LEXEME_MODE(marpaESLIFRecognizerp->marpaESLIFp, marpaESLIFValueResult);
#endif
          /* New line processing, etc... */
          if (! _marpaESLIFRecognizer_matchPostProcessingb(marpaESLIFRecognizerp, marpaESLIFValueResult.u.a.sizel)) {
            goto err;
          }
          MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Last discard successful: advancing stream position %p by %ld bytes", marpaESLIF_streamp->inputs, (unsigned long) marpaESLIFValueResult.u.a.sizel);
          marpaESLIF_streamp->inputs += marpaESLIFValueResult.u.a.sizel;
          marpaESLIF_streamp->inputl -= marpaESLIFValueResult.u.a.sizel;
          /* We NEVER free because we called for parsing in lexeme mode */
          /* free(marpaESLIFValueResult.u.a.p); */
          /* If there is an event, push it */
          if ((marpaESLIFRecognizerp->discardEvents != NULL) && (marpaESLIFRecognizerp->discardSymbolp != NULL)) {
            /* Push discard event */
            if (! _marpaESLIFRecognizer_push_eventb(marpaESLIFRecognizerp, MARPAESLIF_EVENTTYPE_DISCARD, marpaESLIFRecognizerp->discardSymbolp, marpaESLIFRecognizerp->discardEvents, &(marpaESLIFValueResult.u.a))) {
              goto err;
            }
          }
          continue_last_discard_loopb = (! marpaESLIF_streamp->eofb) || (marpaESLIF_streamp->inputl > 0);
        } else {
          continue_last_discard_loopb = 0;
        }
      } while (continue_last_discard_loopb);
    }

    /* Trigger an error if data remains and recognizer do not have the exhausted event flag */
    if (! ((marpaESLIF_streamp->eofb && (marpaESLIF_streamp->inputl <= 0)) || marpaESLIFRecognizerp->marpaESLIFRecognizerOption.exhaustedb)) {
      MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "Grammar is exhausted but data remains");
      goto err;
    }
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline void _marpaESLIFRecognizer_clear_grammar_eventsb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  static const char *funcs           = "_marpaESLIFRecognizer_clear_grammar_eventsb";
  marpaESLIFEvent_t *eventArrayp     = marpaESLIFRecognizerp->eventArrayp;
  size_t             eventArrayl     = marpaESLIFRecognizerp->eventArrayl;
  size_t             i;
  size_t             okl             = 0;

  /* We put to NONE the grammar events, sort (NONE will be at the end), and change the length */
  for (i = 0; i < eventArrayl; i++) {
    switch (eventArrayp[i].type) {
    case MARPAESLIF_EVENTTYPE_PREDICTED:
    case MARPAESLIF_EVENTTYPE_NULLED:
    case MARPAESLIF_EVENTTYPE_COMPLETED:
    case MARPAESLIF_EVENTTYPE_EXHAUSTED:
      eventArrayp[i].type = MARPAESLIF_EVENTTYPE_NONE;
      break;
    default:
      okl++;
      break;
    }
  }

  if (okl > 0) {
    /* The sort will put eventual NONE events at the very end */
    _marpaESLIFRecognizer_sort_eventsb(marpaESLIFRecognizerp);
  }
  marpaESLIFRecognizerp->eventArrayl = okl;
}

/*****************************************************************************/
static inline void  _marpaESLIFRecognizer_sort_eventsb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  qsort(marpaESLIFRecognizerp->eventArrayp, marpaESLIFRecognizerp->eventArrayl, sizeof(marpaESLIFEvent_t), _marpaESLIF_event_sorti);
}

/*****************************************************************************/
static inline short _marpaESLIF_stream_initb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, size_t bufsizl, int buftriggerperci, short eofb, short utfb)
/*****************************************************************************/
{
  static const char *funcs  = "_marpaESLIF_stream_initb";

  if (bufsizl <= 0) {
    bufsizl = MARPAESLIF_BUFSIZ;
    /* Still ?! */
    if (bufsizl <= 0) {
      MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "Please recompile this project with a default buffer value MARPAESLIF_BUFSIZ > 0");
      return 0;
    }
  }

  marpaESLIFRecognizerp->_marpaESLIF_stream.buffers              = NULL;
  marpaESLIFRecognizerp->_marpaESLIF_stream.bufferl              = 0;
  marpaESLIFRecognizerp->_marpaESLIF_stream.bufferallocl         = 0;
  marpaESLIFRecognizerp->_marpaESLIF_stream.globalOffsetp        = NULL;
  marpaESLIFRecognizerp->_marpaESLIF_stream.eofb                 = eofb;
  marpaESLIFRecognizerp->_marpaESLIF_stream.utfb                 = utfb;
  marpaESLIFRecognizerp->_marpaESLIF_stream.charconvb            = 0;
  marpaESLIFRecognizerp->_marpaESLIF_stream.lastFroms            = NULL;
  marpaESLIFRecognizerp->_marpaESLIF_stream.lastFroml            = 0;
  marpaESLIFRecognizerp->_marpaESLIF_stream.bytelefts            = NULL;
  marpaESLIFRecognizerp->_marpaESLIF_stream.byteleftl            = 0;
  marpaESLIFRecognizerp->_marpaESLIF_stream.byteleftallocl       = 0;
  marpaESLIFRecognizerp->_marpaESLIF_stream.inputs               = NULL;
  marpaESLIFRecognizerp->_marpaESLIF_stream.inputl               = 0;
  marpaESLIFRecognizerp->_marpaESLIF_stream.bufsizl              = bufsizl;
  marpaESLIFRecognizerp->_marpaESLIF_stream.buftriggerl          = (bufsizl * (100 + buftriggerperci)) / 100;
  marpaESLIFRecognizerp->_marpaESLIF_stream.nextReadIsFirstReadb = 1;
  marpaESLIFRecognizerp->_marpaESLIF_stream.noAnchorIsOkb        = eofb;
  marpaESLIFRecognizerp->_marpaESLIF_stream.encodings            = NULL;
  marpaESLIFRecognizerp->_marpaESLIF_stream.encodingp            = NULL;
  marpaESLIFRecognizerp->_marpaESLIF_stream.tconvp               = NULL;
  marpaESLIFRecognizerp->_marpaESLIF_stream.linel                = 1;
  marpaESLIFRecognizerp->_marpaESLIF_stream.columnl              = 0;
  marpaESLIFRecognizerp->_marpaESLIF_stream.bomdoneb             = 0;

  return 1;
}

/*****************************************************************************/
static inline marpaESLIFRecognizer_t *_marpaESLIFRecognizer_newp(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaESLIFRecognizerOption_t *marpaESLIFRecognizerOptionp, short discardb, short noEventb, short silentb, marpaESLIFRecognizer_t *marpaESLIFRecognizerParentp, short fakeb, int maxStartCompletionsi, short utfb, short grammarIsOnStackb)
/*****************************************************************************/
/* For performance reasons, when fakeb is a false value, then please arrange you code to work with a false grammmarIsOnStackb value as well */
{
  static const char             *funcs                 = "_marpaESLIFRecognizer_newp";
  marpaESLIF_t                  *marpaESLIFp           = marpaESLIFGrammarp->marpaESLIFp;
  genericStack_t                *marpaESLIFRecognizerParentStackp;
  marpaESLIFRecognizer_t        *marpaESLIFRecognizerp = NULL;
  marpaWrapperRecognizerOption_t marpaWrapperRecognizerOption;
  marpaESLIF_grammar_t          *grammarp;
  genericStack_t                *symbolStackp;
  int                            symboli;
  marpaESLIF_symbol_t           *symbolp;
  short                          discardEventb;

  if (marpaESLIFGrammarp == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Null grammar pointer");
    goto err;
  }

  /* No option ? Use the default. */
  if (marpaESLIFRecognizerOptionp == NULL) {
    marpaESLIFRecognizerOptionp = &marpaESLIFRecognizerOption_default_template;
  }

  /* If this can be a reusable recognizer, so do we */
  marpaESLIFRecognizerp = _marpaESLIFRecognizer_getFromCachep(marpaESLIFp, marpaESLIFGrammarp, discardb, noEventb, silentb, marpaESLIFRecognizerParentp, fakeb, grammarIsOnStackb);
  if (marpaESLIFRecognizerp != NULL) {
    goto done;
  }

  /* Nope, this will be a fresh new thingy */
  /* In case there is a parent, we may have cached an already malloc()ed area */
  if (marpaESLIFRecognizerParentp != NULL) {
    marpaESLIFRecognizerParentStackp = marpaESLIFRecognizerParentp->marpaESLIFRecognizerStackp;
    if (GENERICSTACK_USED(marpaESLIFRecognizerParentStackp) > 0) {
      marpaESLIFRecognizerp = (marpaESLIFRecognizer_t *) GENERICSTACK_POP_PTR(marpaESLIFRecognizerParentStackp);
#ifndef MARPAESLIF_NTRACE
      /* This should never happen */
      if (marpaESLIFRecognizerp == NULL) {
        MARPAESLIF_WARN(marpaESLIFp, "marpaESLIFRecognizerParentp->marpaESLIFRecognizerStackp pop returned a null pointer");
      }
#endif
    }
  }
  if (marpaESLIFRecognizerp == NULL) {
    marpaESLIFRecognizerp = (marpaESLIFRecognizer_t *) malloc(sizeof(marpaESLIFRecognizer_t));
    if (marpaESLIFRecognizerp == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
      goto err;
    }
  }

  marpaESLIFRecognizerp->marpaESLIFp                  = marpaESLIFp;
  _marpaESLIFRecognizer_redoGrammarv(marpaESLIFRecognizerp, marpaESLIFGrammarp, fakeb, grammarIsOnStackb);
  marpaESLIFRecognizerp->marpaESLIFRecognizerOption   = *marpaESLIFRecognizerOptionp;
  marpaESLIFRecognizerp->marpaWrapperRecognizerp      = NULL;
  marpaESLIFRecognizerp->lexemeInputStackp            = NULL;  /* Take care, it is pointer to internal _lexemeInputstack if stack init is ok */
  marpaESLIFRecognizerp->eventArrayp                  = NULL;
  marpaESLIFRecognizerp->eventArrayl                  = 0;
  marpaESLIFRecognizerp->eventArraySizel              = 0;
  marpaESLIFRecognizerp->parentRecognizerp            = marpaESLIFRecognizerParentp;
  marpaESLIFRecognizerp->lastCompletionEvents         = NULL;
  marpaESLIFRecognizerp->lastCompletionSymbolp        = NULL;
  marpaESLIFRecognizerp->discardEvents                = NULL;
  marpaESLIFRecognizerp->discardSymbolp               = NULL;
  marpaESLIFRecognizerp->resumeCounteri               = 0;
  marpaESLIFRecognizerp->callstackCounteri            = 0;
  /* If there is a parent recognizer, we share quite a lot of information */
  if (marpaESLIFRecognizerParentp != NULL) {
    marpaESLIFRecognizerp->leveli                       = marpaESLIFRecognizerParentp->leveli + 1;
    marpaESLIFRecognizerp->marpaESLIFRecognizerHashp    = marpaESLIFRecognizerParentp->marpaESLIFRecognizerHashp;
    marpaESLIFRecognizerp->marpaESLIFRecognizerStackp   = marpaESLIFRecognizerParentp->marpaESLIFRecognizerStackp;
    marpaESLIFRecognizerp->marpaESLIF_streamp           = marpaESLIFRecognizerParentp->marpaESLIF_streamp;
    marpaESLIFRecognizerp->parentDeltal                 = marpaESLIFRecognizerParentp->marpaESLIF_streamp->inputs - marpaESLIFRecognizerParentp->marpaESLIF_streamp->buffers;
  } else {
    marpaESLIFRecognizerp->leveli                       = 0;
    marpaESLIFRecognizerp->marpaESLIFRecognizerHashp    = NULL; /* Pointer to a hash in the structure, initialized later */
    marpaESLIFRecognizerp->marpaESLIFRecognizerStackp   = NULL; /* Pointer to a stack in the structure, initialized later */
    marpaESLIFRecognizerp->marpaESLIF_streamp           = NULL; /* Initialized below */
    marpaESLIFRecognizerp->parentDeltal                 = 0;
  }
  marpaESLIFRecognizerp->scanb                           = 0;
  marpaESLIFRecognizerp->noEventb                        = noEventb;
  marpaESLIFRecognizerp->discardb                        = discardb;
  marpaESLIFRecognizerp->silentb                         = silentb;
  marpaESLIFRecognizerp->haveLexemeb                     = 0;
  /* These variables are resetted at every _resume_oneb() */
  marpaESLIFRecognizerp->completedb                      = 0;
  marpaESLIFRecognizerp->cannotcontinueb                 = 0;
  marpaESLIFRecognizerp->alternativeStackSymbolp         = NULL;  /* Take care, it is pointer to internal _alternativeStackSymbolp if stack init is ok */
  marpaESLIFRecognizerp->commitedAlternativeStackSymbolp = NULL;  /* Take care, it is pointer to internal _commitedAlternativeStackSymbolp if stack init is ok */
  marpaESLIFRecognizerp->lastPausepp                     = NULL;
  marpaESLIFRecognizerp->lastTrypp                       = NULL;
  marpaESLIFRecognizerp->set2InputStackp                 = NULL;  /* Take care, it is pointer to internal _set2InputStackp if stack init is ok */
  marpaESLIFRecognizerp->lexemesArrayp                   = NULL;
  marpaESLIFRecognizerp->lexemesArrayAllocl              = 0;
  marpaESLIFRecognizerp->discardEventStatebp             = NULL;
  marpaESLIFRecognizerp->beforeEventStatebp              = NULL;
  marpaESLIFRecognizerp->afterEventStatebp               = NULL;
  marpaESLIFRecognizerp->discardOnOffb                   = 1; /* By default :discard is enabled */
  marpaESLIFRecognizerp->pristineb                       = 1; /* Until at least one alternative was pushed */
  marpaESLIFRecognizerp->previousMaxMatchedl             = 0;
  marpaESLIFRecognizerp->lastSizel                       = 0;
  marpaESLIFRecognizerp->maxStartCompletionsi            = maxStartCompletionsi;
  marpaESLIFRecognizerp->numberOfStartCompletionsi       = 0;
  marpaESLIFRecognizerp->lastSizeBeforeCompletionl       = 0;
  marpaESLIFRecognizerp->beforePtrStackp                 = NULL;
  marpaESLIFRecognizerp->beforePtrHashp                  = NULL;
  marpaESLIFRecognizerp->afterPtrHashp                   = NULL;
  marpaESLIFRecognizerp->nSymbolPristinel                = 0;
  marpaESLIFRecognizerp->symbolArrayPristinep            = NULL;
  marpaESLIFRecognizerp->lastDiscardl                    = 0;
  marpaESLIFRecognizerp->lastDiscards                    = NULL;
  marpaESLIFRecognizerp->L                               = NULL;
  marpaESLIFRecognizerp->ifactions                       = NULL;

  marpaWrapperRecognizerOption.genericLoggerp            = silentb ? NULL : marpaESLIFp->marpaESLIFOption.genericLoggerp;
  marpaWrapperRecognizerOption.disableThresholdb         = marpaESLIFRecognizerOptionp->disableThresholdb;
  marpaWrapperRecognizerOption.exhaustionEventb          = marpaESLIFRecognizerOptionp->exhaustedb;

  if (! fakeb) {

    /* Call functions that initialize vital memory areas  */
    if ((! _marpaESLIFRecognizer_createDiscardStateb(marpaESLIFRecognizerp))
        ||
        (! _marpaESLIFRecognizer_createBeforeStateb(marpaESLIFRecognizerp))
        ||
        (! _marpaESLIFRecognizer_createAfterStateb(marpaESLIFRecognizerp))
        ||
        (! _marpaESLIFRecognizer_createLastPauseb(marpaESLIFRecognizerp))
        ||
        (! _marpaESLIFRecognizer_createLastTryb(marpaESLIFRecognizerp))) {
      goto err;
    }

    grammarp = marpaESLIFGrammarp->grammarp;
    
    if (discardb) {
      marpaESLIFRecognizerp->marpaWrapperGrammarp = noEventb ? grammarp->marpaWrapperGrammarDiscardNoEventp : grammarp->marpaWrapperGrammarDiscardp;
      marpaESLIFRecognizerp->nSymbolPristinel     = grammarp->nSymbolDiscardl;
      marpaESLIFRecognizerp->symbolArrayPristinep = grammarp->symbolArrayDiscardp;
    } else {
      marpaESLIFRecognizerp->marpaWrapperGrammarp = noEventb ? grammarp->marpaWrapperGrammarStartNoEventp : grammarp->marpaWrapperGrammarStartp;
      marpaESLIFRecognizerp->nSymbolPristinel     = grammarp->nSymbolStartl;
      marpaESLIFRecognizerp->symbolArrayPristinep = grammarp->symbolArrayStartp;
    }

    marpaESLIFRecognizerp->marpaWrapperRecognizerp = marpaWrapperRecognizer_newp(marpaESLIFRecognizerp->marpaWrapperGrammarp, &marpaWrapperRecognizerOption);
    if (marpaESLIFRecognizerp->marpaWrapperRecognizerp == NULL) {
      goto err;
    }
    /* Set-up initial event states - Inside Marpa this will be a no-op if the symbol was not set with support of the wanted event */
    /* We know this is a global no-op if we are in the no-event mode */
    symbolStackp = grammarp->symbolStackp;
    if (! noEventb) {

      for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
        MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);
        if (symbolp->eventPredicteds != NULL) {
          MARPAESLIF_TRACEF(marpaESLIFp, funcs,
                            "Setting prediction event state for symbol %d <%s> at grammar level %d (%s) to %s (recognizer discard mode: %d)",
                            symbolp->idi, symbolp->descp->asciis, grammarp->leveli, grammarp->descp->asciis, discardb ? "off" : (symbolp->eventPredictedb ? "on" : "off"), (int) discardb);
          if (! marpaWrapperRecognizer_event_onoffb(marpaESLIFRecognizerp->marpaWrapperRecognizerp, symbolp->idi, MARPAWRAPPERGRAMMAR_EVENTTYPE_PREDICTION, discardb ? 0 : symbolp->eventPredictedb)) {
            goto err;
          }
        }
        if (symbolp->eventNulleds != NULL) {
          MARPAESLIF_TRACEF(marpaESLIFp, funcs,
                            "Setting nulled event state for symbol %d <%s> at grammar level %d (%s) to %s (recognizer discard mode: %d)",
                            symbolp->idi, symbolp->descp->asciis, grammarp->leveli, grammarp->descp->asciis, discardb ? "off" : (symbolp->eventNulledb ? "on" : "off"), (int) discardb);
          if (! marpaWrapperRecognizer_event_onoffb(marpaESLIFRecognizerp->marpaWrapperRecognizerp, symbolp->idi, MARPAWRAPPERGRAMMAR_EVENTTYPE_NULLED, discardb ? 0 : symbolp->eventNulledb)) {
            goto err;
          }
        }
        if (discardb) {
          /* Discard event is a special beast: it is a sub-recognizer, but the event setting is done at the top recognizer level. */
          /* In addition the current grammar (grammarp pointer) of a discard grammar always have the exact same symbols than the */
          /* current grammar of its parent, because this is the same grammar, though precomputed with a different start symbol -; */

          /* It is a non-sense to have discardb to true without a parent recognizer */
          if (marpaESLIFRecognizerParentp == NULL) {
            MARPAESLIF_ERROR(marpaESLIFp, "Discard mode called without a parent recognizer");
            goto err;
          }
          if (symbolp->discardEvents != NULL) {
            discardEventb = marpaESLIFRecognizerParentp->discardEventStatebp[symbolp->idi];
            MARPAESLIF_TRACEF(marpaESLIFp, funcs,
                              "Setting :discard completion event state for symbol %d <%s> at grammar level %d (%s) to %s (recognizer discard mode: %d)",
                              symbolp->idi, symbolp->descp->asciis, grammarp->leveli, grammarp->descp->asciis, discardEventb ? "on" : "off", (int) discardb);
            if (! marpaWrapperRecognizer_event_onoffb(marpaESLIFRecognizerp->marpaWrapperRecognizerp, symbolp->idi, MARPAWRAPPERGRAMMAR_EVENTTYPE_COMPLETION, discardEventb)) {
              goto err;
            }
          }
        } else {
          if (symbolp->eventCompleteds != NULL) {
            MARPAESLIF_TRACEF(marpaESLIFp, funcs,
                              "Setting completion event state for symbol %d <%s> at grammar level %d (%s) to %s (recognizer discard mode: %d)",
                              symbolp->idi, symbolp->descp->asciis, grammarp->leveli, grammarp->descp->asciis, discardb ? "off" : (symbolp->eventCompletedb ? "on" : "off"), (int) discardb);
            if (! marpaWrapperRecognizer_event_onoffb(marpaESLIFRecognizerp->marpaWrapperRecognizerp, symbolp->idi, MARPAWRAPPERGRAMMAR_EVENTTYPE_COMPLETION, discardb ? 0 : symbolp->eventCompletedb)) {
              goto err;
            }
          }
        }
      }
    } else {
      /* Events outside of marpa that need to be switched off: "before" and "after", except for grammar hooks at the top level (i.e. :discard[on/off/switch]) that keep their initial state as per the grammar */
      for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
        MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFp, symbolp, symbolStackp, symboli);
        if (symbolp->eventBefores != NULL) {
          if ((marpaESLIFRecognizerParentp != NULL) || (strstr(symbolp->eventBefores, ":discard[") == NULL)) {
            MARPAESLIF_TRACEF(marpaESLIFp, funcs,
                              "Setting \"before\" event state for symbol %d <%s> at grammar level %d (%s) to off (recognizer discard mode: %d)",
                              symbolp->idi, symbolp->descp->asciis, grammarp->leveli, grammarp->descp->asciis, (int) discardb);
            marpaESLIFRecognizerp->beforeEventStatebp[symbolp->idi] = 0;
          }
        }
        if (symbolp->eventAfters != NULL) {
          if ((marpaESLIFRecognizerParentp != NULL) || (strstr(symbolp->eventAfters, ":discard[") == NULL)) {
            MARPAESLIF_TRACEF(marpaESLIFp, funcs,
                              "Setting \"after\" event state for symbol %d <%s> at grammar level %d (%s) to off (recognizer discard mode: %d)",
                              symbolp->idi, symbolp->descp->asciis, grammarp->leveli, grammarp->descp->asciis, (int) discardb);
            marpaESLIFRecognizerp->afterEventStatebp[symbolp->idi] = 0;
          }
        }
      }
    }
  } else {
    marpaESLIFRecognizerp->marpaWrapperGrammarp         = NULL;
    marpaESLIFRecognizerp->marpaWrapperRecognizerp      = NULL;
    marpaESLIFRecognizerp->discardEventStatebp          = NULL;
    marpaESLIFRecognizerp->beforeEventStatebp           = NULL;
    marpaESLIFRecognizerp->afterEventStatebp            = NULL;
    marpaESLIFRecognizerp->lastPausepp                  = NULL;
    marpaESLIFRecognizerp->lastTrypp                    = NULL;
  }

  marpaESLIFRecognizerp->alternativeStackSymbolp = &(marpaESLIFRecognizerp->_alternativeStackSymbol);
  GENERICSTACK_INIT(marpaESLIFRecognizerp->alternativeStackSymbolp);
  if (GENERICSTACK_ERROR(marpaESLIFRecognizerp->alternativeStackSymbolp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "alternativeStackSymbolp initialization failure, %s", strerror(errno));
    marpaESLIFRecognizerp->alternativeStackSymbolp = NULL;
    goto err;
  }

  marpaESLIFRecognizerp->commitedAlternativeStackSymbolp = &(marpaESLIFRecognizerp->_commitedAlternativeStackSymbol);
  GENERICSTACK_INIT(marpaESLIFRecognizerp->commitedAlternativeStackSymbolp);
  if (GENERICSTACK_ERROR(marpaESLIFRecognizerp->commitedAlternativeStackSymbolp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "commitedAlternativeStackSymbolp initialization failure, %s", strerror(errno));
    marpaESLIFRecognizerp->commitedAlternativeStackSymbolp = NULL;
    goto err;
  }

  /* The mapping of earley set to pointer and length in input is available only if trackb is true */
  if (marpaESLIFRecognizerp->marpaESLIFRecognizerOption.trackb) {
    marpaESLIFRecognizerp->set2InputStackp = &(marpaESLIFRecognizerp->_set2InputStack);
    GENERICSTACK_INIT(marpaESLIFRecognizerp->set2InputStackp);
    if (GENERICSTACK_ERROR(marpaESLIFRecognizerp->set2InputStackp)) {
      MARPAESLIF_ERRORF(marpaESLIFp, "set2InputStackp initialization failure, %s", strerror(errno));
      marpaESLIFRecognizerp->set2InputStackp = NULL;
      goto err;
    }
  }

  marpaESLIFRecognizerp->lexemeInputStackp = &(marpaESLIFRecognizerp->_lexemeInputStack);
  GENERICSTACK_INIT(marpaESLIFRecognizerp->lexemeInputStackp);
  if (GENERICSTACK_ERROR(marpaESLIFRecognizerp->lexemeInputStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "lexemeInputStackp initialization failure, %s", strerror(errno));
    marpaESLIFRecognizerp->lexemeInputStackp = NULL;
    goto err;
  }

  /* Marpa does not like the indice 0 */
  GENERICSTACK_PUSH_NA(marpaESLIFRecognizerp->lexemeInputStackp);
  if (GENERICSTACK_ERROR(marpaESLIFRecognizerp->lexemeInputStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "lexemeInputStackp push failure, %s", strerror(errno));
    goto err;
  }

  if (marpaESLIFRecognizerp->marpaESLIFRecognizerHashp == NULL) {
    marpaESLIFRecognizerp->marpaESLIFRecognizerHashp = &(marpaESLIFRecognizerp->_marpaESLIFRecognizerHash);
    GENERICHASH_INIT_ALL(marpaESLIFRecognizerp->marpaESLIFRecognizerHashp,
                         _marpaESLIF_ptrhashi,
                         NULL, /* keyCmpFunctionp */
                         NULL, /* keyCopyFunctionp */
                         NULL, /* keyFreeFunctionp */
                         NULL, /* valCopyFunctionp */
                         _marpaESLIFRecognizerHash_freev,
                         MARPAESLIF_HASH_SIZE,
                         0 /* wantedSubSize */);
    if (GENERICHASH_ERROR(marpaESLIFRecognizerp->marpaESLIFRecognizerHashp)) {
      MARPAESLIF_ERRORF(marpaESLIFp, "marpaESLIFRecognizerHashp init failure, %s", strerror(errno));
      marpaESLIFRecognizerp->marpaESLIFRecognizerHashp = NULL;
      goto err;
    }
  }

  if (marpaESLIFRecognizerp->marpaESLIFRecognizerStackp == NULL) {
    marpaESLIFRecognizerp->marpaESLIFRecognizerStackp = &(marpaESLIFRecognizerp->_marpaESLIFRecognizerStack);
    GENERICSTACK_INIT(marpaESLIFRecognizerp->marpaESLIFRecognizerStackp);
    if (GENERICSTACK_ERROR(marpaESLIFRecognizerp->marpaESLIFRecognizerStackp)) {
      MARPAESLIF_ERRORF(marpaESLIFp, "marpaESLIFRecognizerStackp initialization failure, %s", strerror(errno));
      marpaESLIFRecognizerp->marpaESLIFRecognizerStackp = NULL;
      goto err;
    }
  }

  if (marpaESLIFRecognizerp->marpaESLIF_streamp == NULL) {
    if (! _marpaESLIF_stream_initb(marpaESLIFRecognizerp, marpaESLIFRecognizerOptionp->bufsizl, marpaESLIFRecognizerOptionp->buftriggerperci, fakeb /* eofb */, utfb)) {
      goto err;
    }
    marpaESLIFRecognizerp->marpaESLIF_streamp = &(marpaESLIFRecognizerp->_marpaESLIF_stream);
  }

  /* When a recognizer needs to discard, we do lazy initialization via the grammarDiscardInitializedb flag */
  /* Then the marpaESLIFGrammarDiscard, grammarDiscard, marpaESLIFRecognizerOptionDiscard, and marpaESLIFValueOptionDiscard */
  /* are filled. */
  marpaESLIFRecognizerp->grammarDiscardInitializedb         = 0;
  /*
  marpaESLIFRecognizerp->marpaESLIFGrammarDiscard           = c.f. MARPAESLIFRECOGNIZER_GRAMMARDISCARD_INITIALIZER() macro
  marpaESLIFRecognizerp->grammarDiscard                     = c.f. MARPAESLIFRECOGNIZER_GRAMMARDISCARD_INITIALIZER() macro
  marpaESLIFRecognizerp->marpaESLIFRecognizerOptionDiscard  = c.f. MARPAESLIFRECOGNIZER_GRAMMARDISCARD_INITIALIZER() macro
  marpaESLIFRecognizerp->marpaESLIFValueOptionDiscard       = c.f. MARPAESLIFRECOGNIZER_GRAMMARDISCARD_INITIALIZER() macro
  */

  marpaESLIFRecognizerp->beforePtrStackp = &(marpaESLIFRecognizerp->_beforePtrStack);
  GENERICSTACK_INIT(marpaESLIFRecognizerp->beforePtrStackp);
  if (GENERICSTACK_ERROR(marpaESLIFRecognizerp->beforePtrStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "marpaESLIFRecognizerp->beforePtrStackp initialization failure, %s", strerror(errno));
    marpaESLIFRecognizerp->beforePtrStackp = NULL;
    goto err;
  }

  marpaESLIFRecognizerp->beforePtrHashp = &(marpaESLIFRecognizerp->_beforePtrHash);
  GENERICHASH_INIT_ALL(marpaESLIFRecognizerp->beforePtrHashp,
                       _marpaESLIF_ptrhashi,
                       NULL, /* keyCmpFunctionp */
                       NULL, /* keyCopyFunctionp */
                       NULL, /* keyFreeFunctionp */
                       NULL, /* valCopyFunctionp */
                       NULL, /* valFreeFunctionp */
                       MARPAESLIF_HASH_SIZE,
                       0 /* wantedSubSize */);
  if (GENERICHASH_ERROR(marpaESLIFRecognizerp->beforePtrHashp)) {
    MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "marpaESLIFRecognizerp->beforePtrHashp init failure, %s", strerror(errno));
    marpaESLIFRecognizerp->beforePtrHashp = NULL;
    goto err;
  }

  marpaESLIFRecognizerp->afterPtrHashp = &(marpaESLIFRecognizerp->_afterPtrHash);
  GENERICHASH_INIT_ALL(marpaESLIFRecognizerp->afterPtrHashp,
                       _marpaESLIF_ptrhashi,
                       NULL, /* keyCmpFunctionp */
                       NULL, /* keyCopyFunctionp */
                       NULL, /* keyFreeFunctionp */
                       NULL, /* valCopyFunctionp */
                       NULL, /* valFreeFunctionp */
                       MARPAESLIF_HASH_SIZE,
                       0 /* wantedSubSize */);
  if (GENERICHASH_ERROR(marpaESLIFRecognizerp->afterPtrHashp)) {
    MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "afterPtrHashp init failure, %s", strerror(errno));
    marpaESLIFRecognizerp->afterPtrHashp = NULL;
    goto err;
  }

#ifndef MARPAESLIF_NTRACE
  if (marpaESLIFRecognizerParentp != NULL) {
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Recognizer %p remembers that parent's %p deltal is %ld", marpaESLIFRecognizerp, marpaESLIFRecognizerParentp, marpaESLIFRecognizerp->parentDeltal);
  }
#endif
  goto done;

 err:
  marpaESLIFRecognizer_freev(marpaESLIFRecognizerp);
  marpaESLIFRecognizerp = NULL;

 done:
#ifndef MARPAESLIF_NTRACE
  /*
  if (marpaESLIFRecognizerParentp != NULL) {
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerParentp, funcs, "return %p", marpaESLIFRecognizerp);
  } else {
    MARPAESLIF_TRACEF(marpaESLIFp, funcs, "return %p", marpaESLIFRecognizerp);
  }
  */
#endif
  return marpaESLIFRecognizerp;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_shareb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIFRecognizer_t *marpaESLIFRecognizerSharedp)
/*****************************************************************************/
{
  if (marpaESLIFRecognizerSharedp == NULL) {
    /* This is a reset */
    marpaESLIFRecognizerp->marpaESLIF_streamp = &(marpaESLIFRecognizerp->_marpaESLIF_stream);
  } else {
    /* We share the stream */
    marpaESLIFRecognizerp->marpaESLIF_streamp = marpaESLIFRecognizerSharedp->marpaESLIF_streamp;
  }

  /* This function never fails */
  return 1;
}

/*****************************************************************************/
marpaESLIFRecognizer_t *marpaESLIFRecognizer_newFromp(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaESLIFRecognizer_t *marpaESLIFRecognizerSharedp)
/*****************************************************************************/
{
  marpaESLIFRecognizer_t *marpaESLIFRecognizerp;

  if (marpaESLIFRecognizerSharedp == NULL) {
    errno = EINVAL;
    return NULL;
  }

  marpaESLIFRecognizerp = marpaESLIFRecognizer_newp(marpaESLIFGrammarp, &(marpaESLIFRecognizerSharedp->marpaESLIFRecognizerOption));
  if (marpaESLIFRecognizerp == NULL) {
    return NULL;
  }

  if (! marpaESLIFRecognizer_shareb(marpaESLIFRecognizerp, marpaESLIFRecognizerSharedp)) {
    marpaESLIFRecognizer_freev(marpaESLIFRecognizerp);
    return NULL;
  }

  return marpaESLIFRecognizerp;
}

/*****************************************************************************/
short marpaESLIFRecognizer_set_exhausted_flagb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short exhaustedb)
/*****************************************************************************/
{
  static const char *funcs = "marpaESLIFRecognizer_set_exhausted_flagb";

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  marpaESLIFRecognizerp->marpaESLIFRecognizerOption.exhaustedb = exhaustedb;

  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "return 1");
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;

  return 1;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_isExhaustedb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short *isExhaustedbp)
/*****************************************************************************/
{
  static const char *funcs = "_marpaESLIFRecognizer_isExhaustedb";
  short              isExhaustedb;
  short              rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  /* Ask directly to the grammar */
  if (! marpaWrapperRecognizer_exhaustedb(marpaESLIFRecognizerp->marpaWrapperRecognizerp, &isExhaustedb)) {
    goto err;
  }

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "isExhaustedb=%d", (int) isExhaustedb);
  if (isExhaustedbp != NULL) {
    *isExhaustedbp = isExhaustedb;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_isCanContinueb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short *isCanContinuebp, short *eofbp, short *isExhaustedbp)
/*****************************************************************************/
{
  static const char *funcs          = "_marpaESLIFRecognizer_isCanContinueb";
  short              isExhaustedb   = -1; /* To detect if it was fetched */
  short              eofb           = -1; /* To detect if it was fetched */
  short              isCanContinueb;
  size_t             inputl;
  short              rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "marpaESLIFRecognizerp->cannotcontinueb=%d, marpaESLIFRecognizerp->maxStartCompletionsi=%d, marpaESLIFRecognizerp->numberOfStartCompletionsi=%d", (int) marpaESLIFRecognizerp->cannotcontinueb, marpaESLIFRecognizerp->maxStartCompletionsi, marpaESLIFRecognizerp->numberOfStartCompletionsi);

  /* For the internal cases that have absolute priority */
  if (marpaESLIFRecognizerp->cannotcontinueb) {
    /* Discard failed but lexemes were read */
    isCanContinueb = 0;
  } else if ((marpaESLIFRecognizerp->maxStartCompletionsi > 0) && (marpaESLIFRecognizerp->numberOfStartCompletionsi >= marpaESLIFRecognizerp->maxStartCompletionsi)) {
    isCanContinueb = 0;
  } else {
    /* We cannot continue if grammar is exhausted or (eof is reached and all data is consumed) */
    if (! _marpaESLIFRecognizer_isExhaustedb(marpaESLIFRecognizerp, &isExhaustedb)) {
      goto err;
    }
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "isExhaustedb=%d", (int) isExhaustedb);
    if (isExhaustedb) {
      isCanContinueb = 0;
    } else {
      if (! _marpaESLIFRecognizer_isEofb(marpaESLIFRecognizerp, &eofb)) {
        goto err;
      }
      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "eofb=%d", (int) eofb);
      if (! _marpaESLIFRecognizer_inputb(marpaESLIFRecognizerp, NULL, &inputl)) {
        goto err;
      }
      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "inputl=%ld", (unsigned long) inputl);
      if (eofb && (inputl <= 0)) {
        isCanContinueb = 0;
      } else {
        isCanContinueb = 1;
      }
    }
  }

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "isCanContinueb=%d", (int) isCanContinueb);
  if (isCanContinuebp != NULL) {
    *isCanContinuebp = isCanContinueb;
  }

  if (eofbp != NULL) {
    if (eofb == -1) {
      if (! _marpaESLIFRecognizer_isEofb(marpaESLIFRecognizerp, eofbp)) {
        goto err;
      }
    } else {
      *eofbp = eofb;
    }
  }

  if (isExhaustedbp != NULL) {
    if (isExhaustedb == -1) {
      if (! _marpaESLIFRecognizer_isExhaustedb(marpaESLIFRecognizerp, isExhaustedbp)) {
        goto err;
      }
    } else {
      *isExhaustedbp = isExhaustedb;
    }
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
short marpaESLIFRecognizer_isCanContinueb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short *isCanContinuebp)
/*****************************************************************************/
{
  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return _marpaESLIFRecognizer_isCanContinueb(marpaESLIFRecognizerp, isCanContinuebp, NULL /* eofbp */, NULL /* exhautedbp */);
}

/*****************************************************************************/
short marpaESLIFRecognizer_isExhaustedb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short *isExhaustedbp)
/*****************************************************************************/
{
  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return _marpaESLIFRecognizer_isExhaustedb(marpaESLIFRecognizerp, isExhaustedbp);
}

/*****************************************************************************/
static inline short _marpaESLIFGrammar_parseb(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaESLIFRecognizerOption_t *marpaESLIFRecognizerOptionp, marpaESLIFValueOption_t *marpaESLIFValueOptionp, short discardb, short noEventb, short silentb, marpaESLIFRecognizer_t *marpaESLIFRecognizerParentp, short *isExhaustedbp, marpaESLIFValueResult_t *marpaESLIFValueResultp, int maxStartCompletionsi, size_t *lastSizeBeforeCompletionlp, int *numberOfStartCompletionsip, short grammarIsOnStackb)
/*****************************************************************************/
{
  static const char      *funcs                 = "_marpaESLIFGrammar_parseb";
#ifndef MARPAESLIF_NTRACE
  marpaESLIF_t           *marpaESLIFp           = marpaESLIFGrammarp->marpaESLIFp;
#endif
  marpaESLIFRecognizer_t *marpaESLIFRecognizerp = NULL;
  marpaESLIFValueOption_t marpaESLIFValueOption = (marpaESLIFValueOptionp != NULL) ? *marpaESLIFValueOptionp : marpaESLIFValueOption_default_template;
  marpaESLIFValue_t      *marpaESLIFValuep      = NULL;
  short                   isExhaustedb;
  short                   canContinueb;
  short                   rcb;

  marpaESLIFRecognizerp = _marpaESLIFRecognizer_newp(marpaESLIFGrammarp,
                                                     marpaESLIFRecognizerOptionp,
                                                     discardb,
                                                     noEventb,
                                                     silentb,
                                                     marpaESLIFRecognizerParentp,
                                                     0, /* fakeb */
                                                     maxStartCompletionsi,
                                                     0, /* utfb - not used because inherited from parent*/
                                                     grammarIsOnStackb);
  if (marpaESLIFRecognizerp == NULL) {
    goto err;
  }
  if (! marpaESLIFRecognizer_scanb(marpaESLIFRecognizerp, 1 /* initialEventsb */, &canContinueb, &isExhaustedb)) {
    goto err;
  }
  while (canContinueb) {
    if (! marpaESLIFRecognizer_resumeb(marpaESLIFRecognizerp, 0, &canContinueb, &isExhaustedb)) {
      goto err;
    }
  }

  /* Force unambiguity */
  marpaESLIFValueOption.ambiguousb = 0;
  marpaESLIFValuep = _marpaESLIFValue_newp(marpaESLIFRecognizerp, &marpaESLIFValueOption, silentb, 0 /* fakeb */);
  if (marpaESLIFValuep == NULL) {
    goto err;
  }
  /* No loop because we ask for a non-ambigous parse tree value */
  if (_marpaESLIFValue_valueb(marpaESLIFValuep, marpaESLIFValueResultp) <= 0) {
    goto err;
  }

  rcb = 1;
  if (isExhaustedbp != NULL) {
    *isExhaustedbp = isExhaustedb;
  }
  if (lastSizeBeforeCompletionlp != NULL) {
    /* If we get here, then marpaESLIFRecognizerp is != NULL */
    *lastSizeBeforeCompletionlp = marpaESLIFRecognizerp->lastSizeBeforeCompletionl;
  }
  if (numberOfStartCompletionsip != NULL) {
    *numberOfStartCompletionsip = marpaESLIFRecognizerp->numberOfStartCompletionsi;
  }
  goto done;
  
 err:
  rcb = 0;

 done:
  marpaESLIFValue_freev(marpaESLIFValuep);
#ifndef MARPAESLIF_NTRACE
  if (marpaESLIFRecognizerp != NULL) {
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  } else {
    MARPAESLIF_TRACEF(marpaESLIFp, funcs, "return %d", (int) rcb);
  }
#endif
  marpaESLIFRecognizer_freev(marpaESLIFRecognizerp);
  if (rcb && (marpaESLIFRecognizerParentp != NULL) && (marpaESLIFValueResultp != NULL)) {
    /* In lexeme mode, the result is expressed as an offset and in addition the stream is shared */
    /* This has to be done AFTER child recognizer is freed, because it this free that restores parent stream position */
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Applying offset %p to result {%ld,%ld} => {%p,%ld}",
                                marpaESLIFRecognizerParentp->marpaESLIF_streamp->inputs,
                                (unsigned long) marpaESLIFValueResultp->u.a.p,
                                (unsigned long) marpaESLIFValueResultp->u.a.sizel,
                                ((char *) marpaESLIFValueResultp->u.a.p) + (size_t) marpaESLIFRecognizerParentp->marpaESLIF_streamp->inputs,
                                (unsigned long) marpaESLIFValueResultp->u.a.sizel);
    marpaESLIFValueResultp->u.a.p = ((char *) marpaESLIFValueResultp->u.a.p) + (size_t) marpaESLIFRecognizerParentp->marpaESLIF_streamp->inputs;
  }
  return rcb;
}

/*****************************************************************************/
static void _marpaESLIF_generateStringWithLoggerCallback(void *userDatavp, genericLoggerLevel_t logLeveli, const char *msgs)
/*****************************************************************************/
{
  _marpaESLIF_appendOpaqueDataToStringGenerator((marpaESLIF_stringGenerator_t *) userDatavp, (char *) msgs, strlen(msgs));
}

/*****************************************************************************/
static void _marpaESLIF_generateSeparatedStringWithLoggerCallback(void *userDatavp, genericLoggerLevel_t logLeveli, const char *msgs)
/*****************************************************************************/
{
  marpaESLIF_stringGenerator_t *contextp = (marpaESLIF_stringGenerator_t *) userDatavp;

  if (contextp->s == NULL) {
    /* First time */
    _marpaESLIF_appendOpaqueDataToStringGenerator((marpaESLIF_stringGenerator_t *) userDatavp, (char *) msgs, strlen(msgs));
  } else {
    if (_marpaESLIF_appendOpaqueDataToStringGenerator((marpaESLIF_stringGenerator_t *) userDatavp, (char *) "|", 1)) {
      _marpaESLIF_appendOpaqueDataToStringGenerator((marpaESLIF_stringGenerator_t *) userDatavp, (char *) msgs, strlen(msgs));
    }
  }
}

/*****************************************************************************/
static void _marpaESLIF_traceLoggerCallback(void *userDatavp, genericLoggerLevel_t logLeveli, const char *msgs)
/*****************************************************************************/
{
#ifndef MARPAESLIF_NTRACE
  static const char *funcs       = "_marpaESLIF_traceLoggerCallback";
  marpaESLIF_t      *marpaESLIFp = (marpaESLIF_t *) userDatavp;

  if (marpaESLIFp != NULL) {
    MARPAESLIF_TRACEF(marpaESLIFp, funcs, "%s", msgs);
  }
#endif
}

/*****************************************************************************/
static inline short _marpaESLIF_appendOpaqueDataToStringGenerator(marpaESLIF_stringGenerator_t *marpaESLIF_stringGeneratorp, char *p, size_t sizel)
/*****************************************************************************/
{
  static const char *funcs = "_marpaESLIF_appendOpaqueDataToStringGenerator";
  char              *tmpp;
  short              rcb;
  size_t             allocl;
  size_t             wantedl;

  /* Note: caller must guarantee that marpaESLIF_stringGeneratorp->marpaESLIFp, p != NULL and l > 0 */

  if (marpaESLIF_stringGeneratorp->s == NULL) {
    /* Get an allocl that is a multiple of 1024, taking into account the hiden NUL byte */
    /* 1023 -> 1024 */
    /* 1024 -> 2048 */
    /* 2047 -> 2048 */
    /* 2048 -> 3072 */
    /* ... */
    /* i.e. this is the upper multiple of 1024 and have space for the NUL byte */
    allocl = MARPAESLIF_CHUNKED_SIZE_UPPER(sizel, 1024);
    /* Check for turn-around, should never happen */
    if (allocl < sizel) {
      MARPAESLIF_ERROR(marpaESLIF_stringGeneratorp->marpaESLIFp, "size_t turnaround detected");
      goto err;
    }
    marpaESLIF_stringGeneratorp->s  = malloc(allocl);
    if (marpaESLIF_stringGeneratorp->s == NULL) {
      MARPAESLIF_ERRORF(marpaESLIF_stringGeneratorp->marpaESLIFp, "malloc failure, %s", strerror(errno));
      goto err;
    }
    memcpy(marpaESLIF_stringGeneratorp->s, p, sizel);
    marpaESLIF_stringGeneratorp->l      = sizel + 1;  /* NUL byte is set at exit of the routine */
    marpaESLIF_stringGeneratorp->allocl = allocl;
    marpaESLIF_stringGeneratorp->okb    = 1;
  } else if (marpaESLIF_stringGeneratorp->okb) {
    wantedl = marpaESLIF_stringGeneratorp->l + sizel; /* +1 for the NUL is already accounted in marpaESLIF_stringGeneratorp->l */
    allocl = MARPAESLIF_CHUNKED_SIZE_UPPER(wantedl, 1024);
    /* Check for turn-around, should never happen */
    if (allocl < wantedl) {
      MARPAESLIF_ERROR(marpaESLIF_stringGeneratorp->marpaESLIFp, "size_t turnaround detected");
      goto err;
    }
    if (allocl > marpaESLIF_stringGeneratorp->allocl) {
      tmpp = realloc(marpaESLIF_stringGeneratorp->s, allocl); /* The +1 for the NULL byte is already in */
      if (tmpp == NULL) {
        MARPAESLIF_ERRORF(marpaESLIF_stringGeneratorp->marpaESLIFp, "realloc failure, %s", strerror(errno));
        goto err;
      }
      marpaESLIF_stringGeneratorp->s      = tmpp;
      marpaESLIF_stringGeneratorp->allocl = allocl;
    }
    memcpy(marpaESLIF_stringGeneratorp->s + marpaESLIF_stringGeneratorp->l - 1, p, sizel);
    marpaESLIF_stringGeneratorp->l = wantedl; /* Already contains the +1 fir the NUL byte */
  } else {
    MARPAESLIF_ERRORF(marpaESLIF_stringGeneratorp->marpaESLIFp, "Invalid internal call to %s", funcs);
    goto err;
  }

  marpaESLIF_stringGeneratorp->s[marpaESLIF_stringGeneratorp->l - 1] = '\0';
  rcb = 1;
  goto done;

 err:
  if (marpaESLIF_stringGeneratorp->s != NULL) {
    free(marpaESLIF_stringGeneratorp->s);
    marpaESLIF_stringGeneratorp->s = NULL;
  }
  marpaESLIF_stringGeneratorp->okb    = 0;
  marpaESLIF_stringGeneratorp->l      = 0;
  marpaESLIF_stringGeneratorp->allocl = 0;
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
marpaESLIFValue_t *marpaESLIFValue_newp(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIFValueOption_t *marpaESLIFValueOptionp)
/*****************************************************************************/
{
  return _marpaESLIFValue_newp(marpaESLIFRecognizerp, marpaESLIFValueOptionp, 0 /* silentb */, 0 /* fakeb */);
}

/*****************************************************************************/
static inline short _marpaESLIFValue_valueb(marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp)
/*****************************************************************************/
{
  static const char       *funcs                 = "_marpaESLIFValue_valueb";
  static const int         indicei               = 0; /* By definition result is always at indice No 0 */
  marpaESLIFRecognizer_t  *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  short                    rcb;
  marpaESLIFValueResult_t  marpaESLIFValueResult;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (! _marpaESLIFValue_stack_newb(marpaESLIFValuep)) {
    goto err;
  }

  /* It is VERY important to remember that in lexeme mode:
     - No lexeme is allocated
     - rule    callback is ALWAYS _marpaESLIF_lexeme_concatb()
     - symbol  callback is ALWAYS _marpaESLIF_lexeme_transferb()
     - nulling callback is ALWAYS _marpaESLIF_lexeme_concatb()
  */

  rcb = marpaWrapperValue_valueb(marpaESLIFValuep->marpaWrapperValuep,
                                 (void *) marpaESLIFValuep,
                                 _marpaESLIFValue_ruleCallbackWrapperb,
                                 _marpaESLIFValue_symbolCallbackWrapperb,
                                 _marpaESLIFValue_nullingCallbackWrapperb);

  if (rcb > 0) {
    /* The output is at position indicei of valueResultStackp */
    /* This indice must be something we know about */
#ifndef MARPAESLIF_NTRACE
    /* Should never happen */
    if (! GENERICSTACK_IS_CUSTOM(marpaESLIFValuep->valueResultStackp, indicei)) {
      MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValuep->valueResultStackp at indice %d is not CUSTOM (got %s, value %d)", indicei, _marpaESLIF_genericStack_i_types(marpaESLIFValuep->valueResultStackp, indicei), GENERICSTACKITEMTYPE(marpaESLIFValuep->valueResultStackp, indicei));
      goto err;
    }
#endif
    marpaESLIFValueResult = GENERICSTACK_GET_CUSTOM(marpaESLIFValuep->valueResultStackp, indicei);

    /* Only internal calls set this variable: then we know by construction that we do not want to have an import */
    /* and we will manage the consequence of resetting the value at this stack indice. */
    if (marpaESLIFValueResultp != NULL) {
      GENERICSTACK_SET_NA(marpaESLIFValuep->valueResultStackp, indicei);
      if (GENERICSTACK_ERROR(marpaESLIFValuep->valueResultStackp)) {
        MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValuep->valueResultStackp set to NA failure at indice %d, %s", strerror(errno), indicei);
      }
      *marpaESLIFValueResultp = marpaESLIFValueResult;
    } else {
      /* Call the end-user importer */
      if (! _marpaESLIFValue_eslif2hostb(marpaESLIFValuep, &marpaESLIFValueResult, NULL /* marpaESLIFValueResultResolvedp */, NULL /* forcedUserDatavp */, NULL /* forcedImporterp */)) {
        goto err;
      }
    }
  }

  goto done;

 err:
  rcb = -1;

 done:
  if (! _marpaESLIFValue_stack_freeb(marpaESLIFValuep)) {
    rcb = -1;
  }
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
short marpaESLIFValue_valueb(marpaESLIFValue_t *marpaESLIFValuep)
/*****************************************************************************/
{
  if (marpaESLIFValuep == NULL) {
    errno = EINVAL;
    return 0;
  }

  return _marpaESLIFValue_valueb(marpaESLIFValuep, NULL /* marpaESLIFValueResultp */);
}

/*****************************************************************************/
void marpaESLIFValue_freev(marpaESLIFValue_t *marpaESLIFValuep)
/*****************************************************************************/
{
  static const char *funcs = "marpaESLIFValue_freev";

  if (marpaESLIFValuep != NULL) {
    /* Take care: last value is under the USER's responsibility */
    marpaESLIFRecognizer_t *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
    marpaWrapperValue_t    *marpaWrapperValuep    = marpaESLIFValuep->marpaWrapperValuep;

    MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
    MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

    if (marpaWrapperValuep != NULL) {
      marpaWrapperValue_freev(marpaWrapperValuep);
    }
    /* The stacks should never be something different than NULL at this stage. */
    /* The methods to use them are protected so that it is impossible */
    /* to use them outside of valuation mode. */
    /*
    GENERICSTACK_FREE(marpaESLIFValuep->valueResultStackp);
    */

    MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "return");
    MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;

    if (marpaESLIFValuep->afterPtrHashp != NULL) {
      GENERICHASH_RESET(marpaESLIFValuep->afterPtrHashp, NULL);
    }
    if (marpaESLIFValuep->beforePtrHashp != NULL) {
      GENERICHASH_RESET(marpaESLIFValuep->beforePtrHashp, NULL);
    }
    if (marpaESLIFValuep->beforePtrStackp != NULL) {
      GENERICSTACK_RESET(marpaESLIFValuep->beforePtrStackp);
    }

    /* Dispose lua if needed */
    _marpaESLIFValue_lua_freev(marpaESLIFValuep);

    free(marpaESLIFValuep);
  }
}

/*****************************************************************************/
static short _marpaESLIFValue_ruleCallbackWrapperb(void *userDatavp, int rulei, int arg0i, int argni, int resulti)
/*****************************************************************************/
{
  static const char                  *funcs                 = "_marpaESLIFValue_ruleCallbackWrapperb";
  marpaESLIFValue_t                  *marpaESLIFValuep      = (marpaESLIFValue_t *) userDatavp;
  marpaESLIFRecognizer_t             *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  marpaESLIFValueOption_t             marpaESLIFValueOption = marpaESLIFValuep->marpaESLIFValueOption;
  marpaESLIFGrammar_t                *marpaESLIFGrammarp    = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  marpaESLIF_grammar_t               *grammarp              = marpaESLIFGrammarp->grammarp;
  marpaESLIFValueRuleCallback_t       ruleCallbackp         = NULL;
  marpaESLIF_rule_t                  *rulep;
  short                               rcb;
  int                                 i;
  int                                 j;
  int                                 k;
  
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "start [%d] = [%d-%d]", resulti, arg0i, argni);

  rulep = _marpaESLIF_rule_findp(marpaESLIFValuep->marpaESLIFp, grammarp, rulei);
  if (rulep == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "No such rule No %d", rulei);
    goto err;
  }

  marpaESLIFValuep->inValuationb   =  1;
  marpaESLIFValuep->symbolp        = NULL;
  marpaESLIFValuep->rulep          = rulep;

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Grammar %d Rule %d %s", grammarp->leveli, rulep->idi, rulep->asciishows);

  /* Passthrough mode:

     This is a vicious case: we have created a rule that is passthough. This can happen only in
     prioritized rules and our internal rules made sure that this situation is unique in the whole grammar.
     That is, for example:

       <Expression> ::=
         Number
          | '(' Expression ')' assoc => group
         || Expression '**' Expression assoc => right
         || Expression '*' Expression 
          | Expression '/' Expression
         || Expression '+' Expression
          | Expression '-' Expression

     is internally converted to:

          Expression    ::= Expression[0]
          Expression[3] ::= '(' Expression[0] ')'
          Expression[2] ::= Expression[3] '**' Expression[2]
          Expression[1] ::= Expression[1] '*'  Expression[2]
                          | Expression[1] '/'  Expression[2]
          Expression[0] ::= Expression[0] '+'  Expression[1]
                          | Expression[0] '+'  Expression[1]

     i.e. the rule

          Expression    ::= Expression[0]

     is a passtrough. Now, again, we made sure that this rule that we call a "passthrough" can happen only
     once in the grammar. This mean that when we evaluate Expression[0] we are sure that the next rule to
     evaluate will be Expression.

     In Marpa native valuation methods, from stack point of view, we know that if the stack numbers for
     Expression[0] and Expression will be the same. In the ASF valuation mode, this will not be true.

     In Marpa, for instance, Expression[0] evaluates in stack number resulti, then Expression will also
     evaluate to the same numberi.

     In ASF mode, Expression[0] is likely to be one plus the stack number for Expression.

     A general implementation can just say the following:

     Expression[0] will evaluate stack [arg0i[0]..argni[0]] to resulti[0]
     Expression    will evaluate stack resulti[0] to resulti

     i.e. both are equivalent to: stack [arg0i[0]..argni[0]] evaluating to resulti.

     This mean that we can skip the passthrough valuation if we remember its stack input: [arg0i[0]..argn[0]].

     Implementation is:
     * If current rule is a passthrough, remember arg0i and argni, action and remember we have done a passthrough
     * Next pass will check if previous call was a passthrough, and if true, will reuse these remembered arg0i and argni.
  */
  if (rulep->passthroughb) {
    if (marpaESLIFValuep->previousPassWasPassthroughb) {
      /* Extra protection - this should never happen */
      MARPAESLIF_ERROR(marpaESLIFValuep->marpaESLIFp, "Passthrough rule but previous rule was already a passthrough");
      goto err;
    }
    marpaESLIFValuep->previousPassWasPassthroughb = 1;
    marpaESLIFValuep->previousArg0i               = arg0i;
    marpaESLIFValuep->previousArgni               = argni;

  } else {
    
    if (marpaESLIFValuep->previousPassWasPassthroughb) {
      /* Previous rule was a passthrough */
      arg0i   = marpaESLIFValuep->previousArg0i;
      argni   = marpaESLIFValuep->previousArgni;
      marpaESLIFValuep->previousPassWasPassthroughb = 0;
    }

    if (! _marpaESLIFValue_ruleActionCallbackb(marpaESLIFValuep, rulep->asciishows, rulep->actionp, &ruleCallbackp)) {
      goto err;
    }

    /* If the rule have a separator, eventually remove it */
    if ((rulep->separatorp != NULL) && rulep->hideseparatorb
        /* && (argni > arg0i) */             /* test not necessary in theory because we are not nullable */
        ) {
      for (i = arg0i, j = arg0i; i <= argni; i += 2) {
        if (i == j) {
          continue;
        }
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Hide separator: Copy [%d] to [%d]", i, 1 + j);
        if (! _marpaESLIF_generic_action_copyb(marpaESLIFValueOption.userDatavp, marpaESLIFValuep, arg0i, argni, i, ++j, 0 /* nullable */)) {
          goto err;
        }
      }
      argni = j;
    }
    
    /* If the rule have a skipped elements, eventually remove them (starting at the latest indice) */
    if (rulep->skipbp != NULL) {
      k = 0; /* k is the number of arguments to shift */
      for (i = argni; i >= arg0i; i--) {
        if (rulep->skipbp[i - arg0i]) {
          /* Shift remaining values - last element is naturally skipped with argni-- */
          if (k > 0) {
	    /* There are k unhiden values scanned and we want to shift them. Current argument being skipped is at indice i. */
            for (j = i; j < i + k; j++) {
              MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Hide value: Copy [%d] to [%d]", j + 1, j);
              if (! _marpaESLIF_generic_action_copyb(marpaESLIFValueOption.userDatavp, marpaESLIFValuep, arg0i, argni, j + 1, j, 0 /* nullable */)) {
                goto err;
              }
            }
	  }
	  /* Number of arguments to the callback decreases (note that the first part of the for (i = argni; ...) loop is NOT reevaluated) */
          argni--;
        } else {
	  k++; /* This element is not skipped */
	}
      }
      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Hide value changed stack indices: [%d] = [%d-%d]", resulti, arg0i, argni);
    }

    if (! ruleCallbackp(marpaESLIFValueOption.userDatavp, marpaESLIFValuep, arg0i, argni, resulti, 0 /* nullableb */)) {
      /* marpaWrapper logging will not give rule description, so do we */
      MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Action %s failed for rule: %s", marpaESLIFValuep->actions, rulep->asciishows);
      goto err;
    }
  }

  rcb = 1;
  goto done;

 err:
#if MARPAESLIF_VALUEERRORPROGRESSREPORT
  _marpaESLIFValueErrorProgressReportv(marpaESLIFValuep);
#endif
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  marpaESLIFValuep->inValuationb   =  0;
  marpaESLIFValuep->symbolp        = NULL;
  marpaESLIFValuep->rulep          = NULL;
  marpaESLIFValuep->actions        = NULL;
  marpaESLIFValuep->stringp        = NULL;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFValue_anySymbolCallbackWrapperb(void *userDatavp, int symboli, int argi, int resulti, short nullableb)
/*****************************************************************************/
{
  static const char                    *funcs                 = "_marpaESLIFValue_anySymbolCallbackWrapperb";
  marpaESLIFValue_t                    *marpaESLIFValuep      = (marpaESLIFValue_t *) userDatavp;
  marpaESLIFRecognizer_t               *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  marpaESLIFValueOption_t               marpaESLIFValueOption = marpaESLIFValuep->marpaESLIFValueOption;
  marpaESLIFGrammar_t                  *marpaESLIFGrammarp    = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  marpaESLIF_grammar_t                 *grammarp              = marpaESLIFGrammarp->grammarp;
  marpaESLIFValueSymbolCallback_t       symbolCallbackp       = NULL;
  marpaESLIFValueRuleCallback_t         ruleCallbackp         = NULL;
  marpaESLIF_symbol_t                  *symbolp;
  short                                 rcb;
  marpaESLIFValueResult_t              *marpaESLIFValueResultp;
  
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
#ifndef MARPAESLIF_NTRACE
  if (nullableb) {
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "start(nullable=%d) [%d] ::=", (int) nullableb, resulti);
  } else {
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "start(nullable=%d) [%d] ~ [%d]", (int) nullableb, resulti, argi);
  }
#endif

  symbolp = _marpaESLIF_symbol_findp(marpaESLIFValuep->marpaESLIFp, grammarp, NULL /* asciis */, symboli, NULL /* symbolip */);
  if (symbolp == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "No such symbol No %d", symboli);
    goto err;
  }

  marpaESLIFValuep->inValuationb   =  1;
  marpaESLIFValuep->symbolp        = symbolp;
  marpaESLIFValuep->rulep          = NULL;

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Grammar %d Symbol %d %s", grammarp->leveli, symbolp->idi, symbolp->descp->asciis);

  if (! _marpaESLIFValue_symbolActionCallbackb(marpaESLIFValuep, symbolp->descp->asciis, nullableb, symbolp->nullableActionp, &symbolCallbackp, &ruleCallbackp, symbolp->symbolActionp)) {
    goto err;
  }

  if (symbolCallbackp != NULL) {
    marpaESLIFValueResultp = _marpaESLIFRecognizer_lexemeStack_i_getp(marpaESLIFValuep->marpaESLIFRecognizerp, marpaESLIFRecognizerp->lexemeInputStackp, argi);
    if (marpaESLIFValueResultp == NULL) {
      goto err;
    }
    if (! symbolCallbackp(marpaESLIFValueOption.userDatavp, marpaESLIFValuep, marpaESLIFValueResultp, resulti)) {
      /* marpaWrapper logging will not give symbol description, so do we */
      MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Action %s failed for symbol: %s", marpaESLIFValuep->actions, symbolp->descp->asciis);
      goto err;
    }
  } else {
    if (! ruleCallbackp(marpaESLIFValueOption.userDatavp, marpaESLIFValuep, -1, -1, resulti, nullableb)) {
      /* marpaWrapper logging will not give symbol description, so do we */
      MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Action %s failed for symbol: %s", marpaESLIFValuep->actions, symbolp->descp->asciis);
      goto err;
    }
  }

  rcb = 1;
  goto done;

 err:
#if MARPAESLIF_VALUEERRORPROGRESSREPORT
  _marpaESLIFValueErrorProgressReportv(marpaESLIFValuep);
#endif
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  marpaESLIFValuep->inValuationb   =  0;
  marpaESLIFValuep->symbolp        = NULL;
  marpaESLIFValuep->rulep          = NULL;
  marpaESLIFValuep->actions        = NULL;
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIFValue_symbolCallbackWrapperb(void *userDatavp, int symboli, int argi, int resulti)
/*****************************************************************************/
{
  static const char      *funcs                 = "_marpaESLIFValue_symbolCallbackWrapperb";
  marpaESLIFValue_t      *marpaESLIFValuep      = (marpaESLIFValue_t *) userDatavp;
  marpaESLIFRecognizer_t *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  short                   rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  rcb = _marpaESLIFValue_anySymbolCallbackWrapperb(userDatavp, symboli, argi, resulti, 0 /* nullableb */);

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIFValue_nullingCallbackWrapperb(void *userDatavp, int symboli, int resulti)
/*****************************************************************************/
{
  static const char      *funcs                 = "_marpaESLIFValue_nullingCallbackWrapperb";
  marpaESLIFValue_t      *marpaESLIFValuep      = (marpaESLIFValue_t *) userDatavp;
  marpaESLIFRecognizer_t *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  short                   rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  rcb = _marpaESLIFValue_anySymbolCallbackWrapperb(userDatavp, symboli, -1 /* arg0i - not used when nullable is true */, resulti, 1 /* nullableb */);

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline void _marpaESLIFGrammar_freev(marpaESLIFGrammar_t *marpaESLIFGrammarp, short onStackb)
/*****************************************************************************/
{
  if (marpaESLIFGrammarp != NULL) {
    _marpaESLIFGrammar_grammarStack_freev(marpaESLIFGrammarp, marpaESLIFGrammarp->grammarStackp);
    _marpaESLIF_string_freev(marpaESLIFGrammarp->luadescp, 0 /* onStackb */);
    if (marpaESLIFGrammarp->luabytep != NULL) {
      free(marpaESLIFGrammarp->luabytep);
    }
    if (marpaESLIFGrammarp->luaprecompiledp != NULL) {
      free(marpaESLIFGrammarp->luaprecompiledp);
    }
    if (! onStackb) {
      free(marpaESLIFGrammarp);
    }
  }
}

/*****************************************************************************/
static inline void _marpaESLIFGrammar_grammarStack_freev(marpaESLIFGrammar_t *marpaESLIFGrammarp, genericStack_t *grammarStackp)
/*****************************************************************************/
{
  if (grammarStackp != NULL) {
    while (GENERICSTACK_USED(grammarStackp) > 0) {
      if (GENERICSTACK_IS_PTR(grammarStackp, GENERICSTACK_USED(grammarStackp) - 1)) {
        marpaESLIF_grammar_t *grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_POP_PTR(grammarStackp);
        _marpaESLIF_grammar_freev(grammarp);
      } else {
        GENERICSTACK_USED(grammarStackp)--;
      }
    }
    GENERICSTACK_RESET(grammarStackp); /* Take care, this a pointer to a stack inside Grammar structure */
  }
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_lexemeStack_i_p_and_sizeb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, genericStack_t *lexemeStackp, int i, char **pp, size_t *sizelp)
/*****************************************************************************/
{
  static const char       *funcs = "_marpaESLIFRecognizer_lexemeStack_i_p_and_sizeb";
  marpaESLIFValueResult_t *marpaESLIFValueResultp;
  short                    rcb;

  marpaESLIFValueResultp = _marpaESLIFRecognizer_lexemeStack_i_getp(marpaESLIFRecognizerp, lexemeStackp, i);
  if (marpaESLIFValueResultp == NULL) {
    goto err;
  }

  if (marpaESLIFValueResultp->type != MARPAESLIF_VALUE_TYPE_ARRAY) {
    MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "marpaESLIFValueResultp->type is not ARRAY (got %d, %s)", marpaESLIFValueResultp->type, _marpaESLIF_value_types(marpaESLIFValueResultp->type));
    goto err;
  }

  *pp     = marpaESLIFValueResultp->u.a.p;
  *sizelp = marpaESLIFValueResultp->u.a.sizel;

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static inline marpaESLIFValueResult_t *_marpaESLIFRecognizer_lexemeStack_i_getp(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, genericStack_t *lexemeStackp, int i)
/*****************************************************************************/
{
  static const char       *funcs = "_marpaESLIFRecognizer_lexemeStack_i_getp";
  marpaESLIFValueResult_t *rcp;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

#ifndef MARPAESLIF_NTRACE
  /* Should never happen */
  if (! GENERICSTACK_IS_CUSTOM(lexemeStackp, i)) {
    MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "No such indice %d in lexeme stack", i);
    goto err;
  }
#endif
  rcp = GENERICSTACK_GET_CUSTOMP(lexemeStackp, i);

#ifndef MARPAESLIF_NTRACE
  goto done;

 err:
  rcp = NULL;

 done:
#endif
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %p", rcp);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcp;
}

/*****************************************************************************/
static inline const char *_marpaESLIF_genericStack_i_types(genericStack_t *stackp, int i)
/*****************************************************************************/
{
  const char *s;

  switch (GENERICSTACKITEMTYPE(stackp, i)) {
  case GENERICSTACKITEMTYPE_NA:
    s = GENERICSTACKITEMTYPE_NA_STRING;
    break;
  case GENERICSTACKITEMTYPE_CHAR:
    s = GENERICSTACKITEMTYPE_CHAR_STRING;
    break;
  case GENERICSTACKITEMTYPE_SHORT:
    s = GENERICSTACKITEMTYPE_SHORT_STRING;
    break;
  case GENERICSTACKITEMTYPE_INT:
    s = GENERICSTACKITEMTYPE_INT_STRING;
    break;
  case GENERICSTACKITEMTYPE_LONG:
    s = GENERICSTACKITEMTYPE_LONG_STRING;
    break;
  case GENERICSTACKITEMTYPE_FLOAT:
    s = GENERICSTACKITEMTYPE_FLOAT_STRING;
    break;
  case GENERICSTACKITEMTYPE_DOUBLE:
    s = GENERICSTACKITEMTYPE_DOUBLE_STRING;
    break;
  case GENERICSTACKITEMTYPE_PTR:
    s = GENERICSTACKITEMTYPE_PTR_STRING;
    break;
  case GENERICSTACKITEMTYPE_ARRAY:
    s = GENERICSTACKITEMTYPE_ARRAY_STRING;
    break;
  case GENERICSTACKITEMTYPE_CUSTOM:
    s = GENERICSTACKITEMTYPE_CUSTOM_STRING;
    break;
  case GENERICSTACKITEMTYPE_LONG_DOUBLE:
    s = GENERICSTACKITEMTYPE_LONG_DOUBLE_STRING;
    break;
  default:
    s = GENERICSTACKITEMTYPE_UNKNOWN_STRING;
    break;
  }

  return s;
}

/*****************************************************************************/
static inline const char *_marpaESLIF_value_types(int typei)
/*****************************************************************************/
{
  const char *s;

  switch (typei) {
  case MARPAESLIF_VALUE_TYPE_UNDEF:
    s = MARPAESLIF_VALUE_TYPE_UNDEF_STRING;
    break;
  case MARPAESLIF_VALUE_TYPE_CHAR:
    s = MARPAESLIF_VALUE_TYPE_CHAR_STRING;
    break;
  case MARPAESLIF_VALUE_TYPE_SHORT:
    s = MARPAESLIF_VALUE_TYPE_SHORT_STRING;
    break;
  case MARPAESLIF_VALUE_TYPE_INT:
    s = MARPAESLIF_VALUE_TYPE_INT_STRING;
    break;
  case MARPAESLIF_VALUE_TYPE_LONG:
    s = MARPAESLIF_VALUE_TYPE_LONG_STRING;
    break;
  case MARPAESLIF_VALUE_TYPE_FLOAT:
    s = MARPAESLIF_VALUE_TYPE_FLOAT_STRING;
    break;
  case MARPAESLIF_VALUE_TYPE_DOUBLE:
    s = MARPAESLIF_VALUE_TYPE_DOUBLE_STRING;
    break;
  case MARPAESLIF_VALUE_TYPE_PTR:
    s = MARPAESLIF_VALUE_TYPE_PTR_STRING;
    break;
  case MARPAESLIF_VALUE_TYPE_ARRAY:
    s = MARPAESLIF_VALUE_TYPE_ARRAY_STRING;
    break;
  case MARPAESLIF_VALUE_TYPE_BOOL:
    s = MARPAESLIF_VALUE_TYPE_BOOL_STRING;
    break;
  case MARPAESLIF_VALUE_TYPE_STRING:
    s = MARPAESLIF_VALUE_TYPE_STRING_STRING;
    break;
  case MARPAESLIF_VALUE_TYPE_ROW:
    s = MARPAESLIF_VALUE_TYPE_ROW_STRING;
    break;
  case MARPAESLIF_VALUE_TYPE_TABLE:
    s = MARPAESLIF_VALUE_TYPE_TABLE_STRING;
    break;
  case MARPAESLIF_VALUE_TYPE_LONG_DOUBLE:
    s = MARPAESLIF_VALUE_TYPE_LONG_DOUBLE_STRING;
    break;
#ifdef MARPAESLIF_HAVE_LONG_LONG
  case MARPAESLIF_VALUE_TYPE_LONG_LONG:
    s = MARPAESLIF_VALUE_TYPE_LONG_LONG_STRING;
    break;
#endif
  default:
    s = MARPAESLIF_VALUE_TYPE_UNKNOWN_STRING;
    break;
  }

  return s;
}

/*****************************************************************************/
static char *_marpaESLIFGrammar_symbolDescriptionCallback(void *userDatavp, int symboli)
/*****************************************************************************/
{
  static const char    *funcs              = "_marpaESLIFGrammar_symbolDescriptionCallback";
  marpaESLIFGrammar_t  *marpaESLIFGrammarp = (marpaESLIFGrammar_t *) userDatavp;
  marpaESLIF_grammar_t *grammarp           = marpaESLIFGrammarp->grammarp;
  genericStack_t       *symbolStackp       = grammarp->symbolStackp;
  marpaESLIF_symbol_t  *symbolp;

#ifndef MARPAESLIF_NTRACE
  /* Should never happen */
  if (! GENERICSTACK_IS_PTR(symbolStackp, symboli)) {
    return NULL;
  }
#endif
  symbolp = GENERICSTACK_GET_PTR(symbolStackp, symboli);

  return symbolp->descp->asciis;
}

/*****************************************************************************/
 static short _marpaESLIFGrammar_symbolOptionSetterInitb(void *userDatavp, int symboli, marpaWrapperGrammarSymbolOption_t *marpaWrapperGrammarSymbolOptionp)
/*****************************************************************************/
{
  static const char         *funcs                    = "_marpaESLIFGrammar_symbolOptionSetterInit";
  marpaESLIF_cloneContext_t *marpaESLIF_cloneContextp = (marpaESLIF_cloneContext_t *) userDatavp;
  marpaESLIF_grammar_t      *grammarp                 = marpaESLIF_cloneContextp->grammarp;
  genericStack_t            *symbolStackp             = grammarp->symbolStackp;
  marpaESLIF_symbol_t       *symbolp;
  short                      rcb;

  MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIF_cloneContextp->marpaESLIFp, symbolp, symbolStackp, symboli);

  /* Consistenty check */
  if (symbolp->idi != symboli) {
    MARPAESLIF_ERRORF(marpaESLIF_cloneContextp->marpaESLIFp, "Clone symbol callback for symbol No %d while we have %d !?", symboli, symbolp->idi);
    goto err;
  }

  marpaWrapperGrammarSymbolOptionp->eventSeti = MARPAWRAPPERGRAMMAR_EVENTTYPE_NONE;

  /* Get event set */
  if (! symbolp->discardRhsb) {
    if (symbolp->eventPredicteds != NULL) {
      MARPAESLIF_TRACEF(marpaESLIF_cloneContextp->marpaESLIFp, funcs, "Setting prediction event for symbol %d <%s> at grammar level %d (%s)", symbolp->idi, symbolp->descp->asciis, grammarp->leveli, grammarp->descp->asciis);
      marpaWrapperGrammarSymbolOptionp->eventSeti |= MARPAWRAPPERGRAMMAR_EVENTTYPE_PREDICTION;
    }
    if (symbolp->eventNulleds != NULL) {
      MARPAESLIF_TRACEF(marpaESLIF_cloneContextp->marpaESLIFp, funcs, "Setting nulled event for symbol %d <%s> at grammar level %d (%s)", symbolp->idi, symbolp->descp->asciis, grammarp->leveli, grammarp->descp->asciis);
      marpaWrapperGrammarSymbolOptionp->eventSeti |= MARPAWRAPPERGRAMMAR_EVENTTYPE_NULLED;
    }
    if (symbolp->eventCompleteds != NULL) {
      MARPAESLIF_TRACEF(marpaESLIF_cloneContextp->marpaESLIFp, funcs, "Setting completion event for symbol %d <%s> at grammar level %d (%s)", symbolp->idi, symbolp->descp->asciis, grammarp->leveli, grammarp->descp->asciis);
      marpaWrapperGrammarSymbolOptionp->eventSeti |= MARPAWRAPPERGRAMMAR_EVENTTYPE_COMPLETION;
    }
  } else {
    if (symbolp->discardEvents != NULL) {
      MARPAESLIF_TRACEF(marpaESLIF_cloneContextp->marpaESLIFp, funcs, "Setting :discard completion event for symbol %d <%s> at grammar level %d (%s)", symbolp->idi, symbolp->descp->asciis, grammarp->leveli, grammarp->descp->asciis);
      marpaWrapperGrammarSymbolOptionp->eventSeti |= MARPAWRAPPERGRAMMAR_EVENTTYPE_COMPLETION;
    }
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
 static short _marpaESLIFGrammar_symbolOptionSetterInternalb(void *userDatavp, int symboli, marpaWrapperGrammarSymbolOption_t *marpaWrapperGrammarSymbolOptionp)
/*****************************************************************************/
{
  static const char         *funcs                    = "_marpaESLIFGrammar_symbolOptionSetterInternalb";
  marpaESLIF_cloneContext_t *marpaESLIF_cloneContextp = (marpaESLIF_cloneContext_t *) userDatavp;
  marpaESLIF_grammar_t      *grammarp                 = marpaESLIF_cloneContextp->grammarp;
  genericStack_t            *symbolStackp             = grammarp->symbolStackp;
  marpaESLIF_symbol_t       *symbolp;
  short                     rcb;

  MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIF_cloneContextp->marpaESLIFp, symbolp, symbolStackp, symboli);

  /* Consistenty check */
  if (symbolp->idi != symboli) {
    MARPAESLIF_ERRORF(marpaESLIF_cloneContextp->marpaESLIFp, "Clone symbol callback for symbol No %d while we have %d !?", symboli, symbolp->idi);
    goto err;
  }

  marpaWrapperGrammarSymbolOptionp->eventSeti = MARPAWRAPPERGRAMMAR_EVENTTYPE_NONE;

  if ((symbolp->eventNulleds != NULL) &&
      ((strcmp(symbolp->eventNulleds, ":discard[on]") == 0) || (strcmp(symbolp->eventNulleds, ":discard[off]") == 0) || (strcmp(symbolp->eventNulleds, ":discard[switch]") == 0))) {
    MARPAESLIF_TRACEF(marpaESLIF_cloneContextp->marpaESLIFp, funcs, "Setting nullabled event %s for symbol %d <%s> at grammar level %d (%s)", symbolp->eventNulleds, symbolp->idi, symbolp->descp->asciis, grammarp->leveli, grammarp->descp->asciis);
    marpaWrapperGrammarSymbolOptionp->eventSeti |= MARPAWRAPPERGRAMMAR_EVENTTYPE_NULLED;
  }
  if ((symbolp->eventPredicteds != NULL) &&
      ((strcmp(symbolp->eventPredicteds, ":discard[on]") == 0) || (strcmp(symbolp->eventPredicteds, ":discard[off]") == 0) || (strcmp(symbolp->eventPredicteds, ":discard[switch]") == 0))) {
    MARPAESLIF_TRACEF(marpaESLIF_cloneContextp->marpaESLIFp, funcs, "Setting prediction event %s for symbol %d <%s> at grammar level %d (%s)", symbolp->eventPredicteds, symbolp->idi, symbolp->descp->asciis, grammarp->leveli, grammarp->descp->asciis);
    marpaWrapperGrammarSymbolOptionp->eventSeti |= MARPAWRAPPERGRAMMAR_EVENTTYPE_PREDICTION;
  }
  if ((symbolp->eventCompleteds != NULL) &&
      ((strcmp(symbolp->eventCompleteds, ":discard[on]") == 0) || (strcmp(symbolp->eventCompleteds, ":discard[off]") == 0) || (strcmp(symbolp->eventCompleteds, ":discard[switch]") == 0))) {
    MARPAESLIF_TRACEF(marpaESLIF_cloneContextp->marpaESLIFp, funcs, "Setting completiong event %s for symbol %d <%s> at grammar level %d (%s)", symbolp->eventCompleteds, symbolp->idi, symbolp->descp->asciis, grammarp->leveli, grammarp->descp->asciis);
    marpaWrapperGrammarSymbolOptionp->eventSeti |= MARPAWRAPPERGRAMMAR_EVENTTYPE_COMPLETION;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
 static short _marpaESLIFGrammar_symbolOptionSetterInternalNoeventb(void *userDatavp, int symboli, marpaWrapperGrammarSymbolOption_t *marpaWrapperGrammarSymbolOptionp)
/*****************************************************************************/
{
  static const char         *funcs                    = "_marpaESLIFGrammar_symbolOptionSetterInternalNoeventb";
  marpaESLIF_cloneContext_t *marpaESLIF_cloneContextp = (marpaESLIF_cloneContext_t *) userDatavp;
  marpaESLIF_grammar_t      *grammarp                 = marpaESLIF_cloneContextp->grammarp;
  genericStack_t            *symbolStackp             = grammarp->symbolStackp;
  marpaESLIF_symbol_t       *symbolp;
  short                     rcb;

  MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIF_cloneContextp->marpaESLIFp, symbolp, symbolStackp, symboli);

  /* Consistenty check */
  if (symbolp->idi != symboli) {
    MARPAESLIF_ERRORF(marpaESLIF_cloneContextp->marpaESLIFp, "Clone symbol callback for symbol No %d while we have %d !?", symboli, symbolp->idi);
    goto err;
  }

  marpaWrapperGrammarSymbolOptionp->eventSeti = MARPAWRAPPERGRAMMAR_EVENTTYPE_NONE;

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIFGrammar_grammarOptionSetterNoLoggerb(void *userDatavp, marpaWrapperGrammarOption_t *marpaWrapperGrammarOptionp)
/*****************************************************************************/
{
  static const char         *funcs                    = "_marpaESLIFGrammar_grammarOptionSetterNoLoggerb";
  marpaESLIF_cloneContext_t *marpaESLIF_cloneContextp = (marpaESLIF_cloneContext_t *) userDatavp;
  marpaESLIF_grammar_t      *grammarp                 = marpaESLIF_cloneContextp->grammarp;

  MARPAESLIF_TRACEF(marpaESLIF_cloneContextp->marpaESLIFp, funcs, "Disabling generic logger at grammar level %d (%s)", grammarp->leveli, grammarp->descp->asciis);

  marpaWrapperGrammarOptionp->genericLoggerp = NULL;

  return 1;
}

/*****************************************************************************/
 static short _marpaESLIFGrammar_symbolOptionSetterDiscardb(void *userDatavp, int symboli, marpaWrapperGrammarSymbolOption_t *marpaWrapperGrammarSymbolOptionp)
/*****************************************************************************/
{
  static const char         *funcs                    = "_marpaESLIFGrammar_symbolOptionSetterDiscardb";
  marpaESLIF_cloneContext_t *marpaESLIF_cloneContextp = (marpaESLIF_cloneContext_t *) userDatavp;
  marpaESLIF_grammar_t      *grammarp                 = marpaESLIF_cloneContextp->grammarp;
  genericStack_t            *symbolStackp             = grammarp->symbolStackp;
  marpaESLIF_symbol_t       *symbolp;
  short                     rcb;

  MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIF_cloneContextp->marpaESLIFp, symbolp, symbolStackp, symboli);

  /* Consistenty check */
  if (symbolp->idi != symboli) {
    MARPAESLIF_ERRORF(marpaESLIF_cloneContextp->marpaESLIFp, "Clone symbol callback for symbol No %d while we have %d !?", symboli, symbolp->idi);
    goto err;
  }

  /* A "discard" event is possible only for symbols that are the RHS of a :discard in the current grammar */
  if (symbolp->discardRhsb && (symbolp->discardEvents != NULL)) {
    if (marpaWrapperGrammarSymbolOptionp->eventSeti != MARPAWRAPPERGRAMMAR_EVENTTYPE_COMPLETION) {
      MARPAESLIF_TRACEF(marpaESLIF_cloneContextp->marpaESLIFp, funcs, "Setting completion event for symbol %d <%s> at grammar level %d (%s) on completion", symbolp->idi, symbolp->descp->asciis, grammarp->leveli, grammarp->descp->asciis);
      marpaWrapperGrammarSymbolOptionp->eventSeti = MARPAWRAPPERGRAMMAR_EVENTTYPE_COMPLETION;
    }
    rcb = 1;
  } else {
    rcb = _marpaESLIFGrammar_symbolOptionSetterInternalNoeventb(userDatavp, symboli, marpaWrapperGrammarSymbolOptionp);
  }

  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_readb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
/*
  In the recognizer:
  - buffers is containing unparsed data, and its size can vary at any time. ALWAYS SHARED with all parents.
  - inputs is always a pointer within buffers.                              ALWAYS SPECIFIC to current recognizer.
  - eofb is when EOF is hit.                                                ALWAYS SHARED with all parents.

  Handled in regex match:
  - encodings is eventual encoding information as per the user              ALWAYS SHARED with all parents.
  - utf8s is the UTF-8 conversion of buffer. Handled in regex match.        ALWAYS SHARED with all parents.
  
  Remember the semantics: from our point of view, reader is reading NEW data. We always append.
*/
{
  static const char            *funcs                      = "_marpaESLIFRecognizer_readb";
  marpaESLIFRecognizerOption_t  marpaESLIFRecognizerOption = marpaESLIFRecognizerp->marpaESLIFRecognizerOption;
  marpaESLIF_t                 *marpaESLIFp                = marpaESLIFRecognizerp->marpaESLIFp;
  marpaESLIF_stream_t          *marpaESLIF_streamp         = marpaESLIFRecognizerp->marpaESLIF_streamp;
  char                         *inputs                     = NULL;
  char                         *encodings                  = NULL;
  size_t                        encodingl                  = 0;
  size_t                        inputl                     = 0;
  short                         eofb                       = 0;
  short                         characterStreamb           = 0;
  char                         *utf8s                      = NULL;
  size_t                        utf8l;
  short                         appendDatab;
  short                         charconvb;
  short                         rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (marpaESLIFRecognizerOption.readerCallbackp == NULL) {
    goto err;
    MARPAESLIF_ERROR(marpaESLIFp, "Null reader callback");
  }

 again:
  if (! marpaESLIFRecognizerOption.readerCallbackp(marpaESLIFRecognizerOption.userDatavp, &inputs, &inputl, &eofb, &characterStreamb, &encodings, &encodingl)) {
    MARPAESLIF_ERROR(marpaESLIFp, "reader failure");
    goto err;
  }

  if ((inputs != NULL) && (inputl > 0)) {
    if (characterStreamb) {
      /* ************************************************************************************************************************************************* */
      /* User say this is a stream of characters.                                                                                                          */
      /* ************************************************************************************************************************************************* */
      /* Here are the possible cases:                                                                                                                      */
      /* - Previous read was a stream of characters (marpaESLIF_streamp->charconvb is true).                                                               */
      /*   [We MUST have the current input encodings in marpaESLIF_streamp->encodings and a conversion engine in marpaESLIF_streamp->tconvp]               */
      /*   [We MUST have a fake terminal associated to input encoding]                                                                                     */
      /*   - user gave encoding (encodings != NULL)                                                                                                        */
      /*     - If encodings and marpaESLIF_streamp->encodings differ, current conversion engine is flushed, last state clean. A new one start.             */
      /*       >> Encoding aliases are not supported.                                                                                                      */
      /*       >> This mode does not support incomplete characters in the input streaming.                                                                 */
      /*     - If encodings and marpaESLIF_streamp->encodings are the same, current conversion engine continue.                                            */
      /*       >> Encoding aliases are not supported.                                                                                                      */
      /*       >> This mode support incomplete characters in the input streaming.                                                                          */
      /*   - user gave NO encoding (encodings == NULL)                                                                                                     */
      /*     - It is assumed that current conversion can continue.                                                                                         */
      /*       >> This mode support incomplete characters in the input streaming.                                                                          */
      /* - Previous read was NOT a stream of characters (marpaESLIF_streamp->charconvb is false).                                                          */
      /*   [Input encodings in marpaESLIF_streamp->encodings should be NULL and current conversion in marpaESLIF_streamp->tconvp as well.]                 */
      /*   - user gave encoding (encodings != NULL) or not                                                                                                 */
      /*     - This is used as-is in the call to _marpaESLIF_charconvb(). Current encoding and conversion engine are initialized.                          */
      /*                                                                                                                                                   */
      /* Input is systematically converted into UTF-8. If user said "UTF-8" it is equivalent to                                                            */
      /* an UTF-8 validation. The user MUST send a buffer information that contain full characters.                                                        */
      /* ************************************************************************************************************************************************* */
      if (marpaESLIF_streamp->charconvb) {
        /* ************************************************************************************************************************************************* */
        /* - Previous read was a stream of characters (marpaESLIF_streamp->charconvb is true).                                                               */
        /* ************************************************************************************************************************************************* */
        if (marpaESLIF_streamp->encodings == NULL) {
          MARPAESLIF_ERROR(marpaESLIFp, "Previous encoding is unknown");
          goto err;
        }
        if (marpaESLIF_streamp->tconvp == NULL) {
          MARPAESLIF_ERROR(marpaESLIFp, "Previous conversion engine is not set");
          goto err;
        }
        if (marpaESLIF_streamp->encodingp == NULL) {
          MARPAESLIF_ERROR(marpaESLIFp, "Previous encoding is not associated to a fake terminal");
          goto err;
        }
        if (encodings != NULL) {
          /* ************************************************************************************************************************************************* */
          /*   - user gave encoding (encodings != NULL)                                                                                                        */
          /* ************************************************************************************************************************************************* */
          if (! _marpaESLIFRecognizer_encoding_eqb(marpaESLIFRecognizerp, marpaESLIF_streamp->encodingp, encodings, encodingl)) {
            /* ************************************************************************************************************************************************* */
            /*     - If encodings and marpaESLIF_streamp->encodings differ, current conversion engine is flushed. A new one start.                               */
            /* ************************************************************************************************************************************************* */
            /* Flush current conversion engine */
            if (! _marpaESLIFRecognizer_flush_charconvb(marpaESLIFRecognizerp)) {
              goto err;
            }
            /* Start a new one */
            charconvb = _marpaESLIFRecognizer_start_charconvb(marpaESLIFRecognizerp, encodings, encodingl, inputs, inputl, eofb);
            if (! charconvb) {
              goto err;
            } else if (charconvb < 0) {
              /* EAGAIN case: charconv internally appends data, the later tried to remove BOM, this is not eof, and there are not enough bytes */
              goto again;
            }
          } else {
            /* ************************************************************************************************************************************************* */
            /*     - If encodings and marpaESLIF_streamp->encodings are the same, current conversion engine continue.                                            */
            /* ************************************************************************************************************************************************* */
            /* Continue with current conversion engine */
            utf8s = _marpaESLIF_charconvb(marpaESLIFp, (char *) MARPAESLIF_UTF8_STRING, marpaESLIF_streamp->encodings, inputs, inputl, &utf8l, NULL /* encodingsp */, &(marpaESLIF_streamp->tconvp), eofb, &(marpaESLIF_streamp->bytelefts), &(marpaESLIF_streamp->byteleftl), &(marpaESLIF_streamp->byteleftallocl), 0 /* tconvsilentb */);
            if (utf8s == NULL) {
              goto err;
            }
            appendDatab = _marpaESLIFRecognizer_appendDatab(marpaESLIFRecognizerp, utf8s, utf8l, eofb);
            if (! appendDatab) {
              goto err;
            } else if (appendDatab < 0) {
              /* EAGAIN case: appending data tried to remove BOM, this is not eof, and there are not enough bytes */
              goto again;
            }
          }
        } else {
          /* ************************************************************************************************************************************************* */
          /*   - user gave NO encoding (encodings == NULL)                                                                                                     */
          /* ************************************************************************************************************************************************* */
          /* Continue with current conversion engine */
          utf8s = _marpaESLIF_charconvb(marpaESLIFp, (char *) MARPAESLIF_UTF8_STRING, marpaESLIF_streamp->encodings, inputs, inputl, &utf8l, NULL /* encodingsp */, &(marpaESLIF_streamp->tconvp), eofb, &(marpaESLIF_streamp->bytelefts), &(marpaESLIF_streamp->byteleftl), &(marpaESLIF_streamp->byteleftallocl), 0 /* tconvsilentb */);
          if (utf8s == NULL) {
            goto err;
          }
          appendDatab = _marpaESLIFRecognizer_appendDatab(marpaESLIFRecognizerp, utf8s, utf8l, eofb);
          if (! appendDatab) {
            goto err;
          } else if (appendDatab < 0) {
            /* EAGAIN case: appending data tried to remove BOM, this is not eof, and there are not enough bytes */
            goto again;
          }
        }
      } else {
        /* ************************************************************************************************************************************************* */
        /* - Previous read was NOT a stream of characters (marpaESLIF_streamp->charconvb is false).                                                          */
        /* ************************************************************************************************************************************************* */
        /* Start a new conversion engine */
        charconvb = _marpaESLIFRecognizer_start_charconvb(marpaESLIFRecognizerp, encodings, encodingl, inputs, inputl, eofb);
        if (! charconvb) {
          goto err;
        } else if (charconvb < 0) {
          /* EAGAIN case: charconv internally appends data, the later tried to remove BOM, this is not eof, and there are not enough bytes */
          goto again;
        }
      }
    } else {
      /* ************************************************************************************************************************************************* */
      /* User say this is not a stream of characters.                                                                                                      */
      /* ************************************************************************************************************************************************* */
      /* Here are the possible cases:                                                                                                                      */
      /* - Previous read was a stream of characters (marpaESLIF_streamp->charconvb is true).                                                               */
      /*   [We MUST have the input encodings in marpaESLIF_streamp->encodings and a current conversion engine in marpaESLIF_streamp->tconvp]               */
      /*   - Current encoding is flushed.                                                                                                                  */
      /*   - Data is appended as-is.                                                                                                                       */
      /* - Previous read was NOT a stream of characters (marpaESLIF_streamp->charconvb is false).                                                          */
      /*   - Data is appended as-is.                                                                                                                       */
      /* ************************************************************************************************************************************************* */
      if (marpaESLIF_streamp->charconvb) {
        /* ************************************************************************************************************************************************* */
        /* - Previous read was a stream of characters (marpaESLIF_streamp->charconvb is true).                                                               */
        /* ************************************************************************************************************************************************* */
        /* Flush current conversion engine */
        if (! _marpaESLIFRecognizer_flush_charconvb(marpaESLIFRecognizerp)) {
          goto err;
        }
        /* Data is appended as-is */
        appendDatab = _marpaESLIFRecognizer_appendDatab(marpaESLIFRecognizerp, inputs, inputl, eofb);
        if (! appendDatab) {
          goto err;
        } else if (appendDatab < 0) {
          /* EAGAIN case: should never happen because we said this is not anymore a character stream */
          MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "Internal failure, appending data wants more data when it should not");
          goto err;
        }
      } else {
        /* ************************************************************************************************************************************************* */
        /* - Previous read was NOT a stream of characters (marpaESLIF_streamp->charconvb is false).                                                          */
        /* ************************************************************************************************************************************************* */
        /* Data is appended as-is */
        appendDatab = _marpaESLIFRecognizer_appendDatab(marpaESLIFRecognizerp, inputs, inputl, eofb);
        if (! appendDatab) {
          goto err;
        } else if (appendDatab < 0) {
          /* EAGAIN case: should never happen because we said this is not anymore a character stream */
          MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "Internal failure, appending data wants more data when it should not");
          goto err;
        }
      }
      /* We do not know UTF-8 correctness. */
      marpaESLIF_streamp->utfb = 0;
    }
  }

  rcb = 1;
  marpaESLIF_streamp->eofb = eofb;

  /* We maintain here a very special thing: if there is EOF at the very first read, this mean that the user gave the whole stream */
  /* in ONE step: then removing PCRE2_ANCHORED is allowed. */
  if (marpaESLIF_streamp->nextReadIsFirstReadb) {
    marpaESLIF_streamp->noAnchorIsOkb = eofb;
    marpaESLIF_streamp->nextReadIsFirstReadb = 0; /* Next read will not be the first read */
  }

  goto done;

 err:
  rcb = 0;

 done:
  if (utf8s != NULL) {
    free(utf8s);
  }

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

#define MARPAESLIF_LEVEL_CREATESHOW(grammarp, asciishowl, asciishows) do { \
    if (grammarp->leveli == 0) {                                        \
      asciishowl += 4;                                                  \
      if (asciishows != NULL) {                                         \
        strcat(asciishows, " ::=");                                     \
      }                                                                 \
    } else if (grammarp->leveli == 1) {                                 \
      asciishowl += 2;                                                  \
      if (asciishows != NULL) {                                         \
        strcat(asciishows, " ~");                                       \
      }                                                                 \
    } else {                                                            \
      asciishowl += 2;                                                  \
      if (asciishows != NULL) {                                         \
        strcat(asciishows, " :");                                       \
      }                                                                 \
      sprintf(tmps, "%d", grammarp->leveli);                            \
      asciishowl += 1 + strlen(tmps) + 1;                               \
      if (asciishows != NULL) {                                         \
        strcat(asciishows, "[");                                        \
        strcat(asciishows, tmps);                                       \
        strcat(asciishows, "]");                                        \
      }                                                                 \
      asciishowl += 2;                                                  \
      if (asciishows != NULL) {                                         \
        strcat(asciishows, ":=");                                       \
      }                                                                 \
    }                                                                   \
  } while (0)

#define MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, strings) do { \
    asciishowl += strlen(strings);                                      \
    if (asciishows != NULL) {                                           \
      strcat(asciishows, strings);                                      \
    }                                                                   \
  } while (0)

#define MARPAESLIF_STRING_CREATEQUOTE(quote, strings) do {      \
    if (strchr(strings, '\'') == NULL) {                        \
      strcpy(quote[0], "'");                                    \
      strcpy(quote[1], "'");                                    \
    } else if (strchr(strings, '"') == NULL) {                  \
      strcpy(quote[0], "\"");                                   \
      strcpy(quote[1], "\"");                                   \
    } else {                                                    \
      strcpy(quote[0], "");                                     \
      strcpy(quote[1], "");                                     \
    }                                                           \
  } while (0)

/*****************************************************************************/
static inline void _marpaESLIF_rule_createshowv(marpaESLIF_t *marpaESLIFp, marpaESLIF_grammar_t *grammarp, marpaESLIF_rule_t *rulep, char *asciishows, size_t *asciishowlp)
/*****************************************************************************/
{
  genericStack_t      *rhsStackp       = rulep->rhsStackp;
  marpaESLIF_symbol_t *symbolp;
  short                skipb;
  size_t               asciishowl = 0;
  int                  rhsi;
  char                 tmps[1024];
  char                 quote[2][2];

  /* Calculate the size needed to show the rule in ASCII form */

  /* There is a special case with :discard, that we want to be shown as-is */
  if (rulep->lhsp->discardb) {
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, rulep->lhsp->descp->asciis);
  } else {
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "<");
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, rulep->lhsp->descp->asciis);
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ">");
  }
  MARPAESLIF_LEVEL_CREATESHOW(grammarp, asciishowl, asciishows);
  for (rhsi = 0; rhsi < GENERICSTACK_USED(rhsStackp); rhsi++) {
#ifndef MARPAESLIF_NTRACE
    /* Should never happen */
    if (! GENERICSTACK_IS_PTR(rhsStackp, rhsi)) {
      continue;
    }
#endif
    symbolp = GENERICSTACK_GET_PTR(rhsStackp, rhsi);
    skipb = (rulep->skipbp != NULL) && rulep->skipbp[rhsi];
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " ");
    if (skipb) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "(- ");
    }
    switch (symbolp->type) {
    case MARPAESLIF_SYMBOL_TYPE_TERMINAL:
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->descp->asciis);
      break;
    case MARPAESLIF_SYMBOL_TYPE_META:
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "<");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, (symbolp->lookupMetas != NULL) ? symbolp->lookupMetas : symbolp->u.metap->asciinames);
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ">");
    default:
      break;
    }
    if (symbolp->lookupResolvedLeveli != grammarp->leveli) {
      /* Default lookup is grammarp->leveli + 1 : we output the @deltaLeveli information if this is not the case */
      if (symbolp->lookupResolvedLeveli != (grammarp->leveli + 1)) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "@");
        sprintf(tmps, "%+d", symbolp->lookupLevelDeltai);
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, tmps);
      }
    }
    if (skipb) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " -)");
    }
  }
  if (rulep->sequenceb) {
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, (rulep->minimumi == 0) ? "*" : "+");
    if (rulep->separatorp != NULL) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " separator => ");
      switch (rulep->separatorp->type) {
      case MARPAESLIF_SYMBOL_TYPE_TERMINAL:
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, rulep->separatorp->descp->asciis);
        break;
      case MARPAESLIF_SYMBOL_TYPE_META:
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "<");
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, (rulep->separatorp->lookupMetas != NULL) ? rulep->separatorp->lookupMetas : rulep->separatorp->u.metap->asciinames);
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ">");
      default:
        break;
      }
    }
    if (rulep->properb) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " proper => 1");
    }
    if (rulep->hideseparatorb) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " hide-separator => 1");
    }
  }
  if (rulep->exceptionp != NULL) {
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " - ");
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "<");
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, rulep->exceptionp->descp->asciis);
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ">");
  }
  if (rulep->ranki != 0) {
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " rank => ");
    sprintf(tmps, "%d", rulep->ranki);
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, tmps);
  }
  if (rulep->nullRanksHighb) {
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " null-ranking => high");
  }
  if (rulep->actionp != NULL) {
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " action => ");
    switch (rulep->actionp->type) {
    case MARPAESLIF_ACTION_TYPE_NAME:
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, rulep->actionp->u.names);
      break;
    case MARPAESLIF_ACTION_TYPE_STRING:
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "::u8\"");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, rulep->actionp->u.stringp->asciis); /* Best effort ASCII */
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\"");
      break;
    case MARPAESLIF_ACTION_TYPE_LUA:
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "::lua->");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, rulep->actionp->u.luas);
    default:
      break;
    }
  }
  if ((! rulep->descautob) && (rulep->descp != NULL) && (rulep->descp->asciis != NULL)) {
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " name => ");
    MARPAESLIF_STRING_CREATEQUOTE(quote, rulep->descp->asciis);
    if (strlen(quote[0]) > 0) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, quote[0]);
    }
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, rulep->descp->asciis);
    if (strlen(quote[1]) > 0) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, quote[1]);
    }
  }
  if (rulep->lhsp->discardb && rulep->discardEvents != NULL) {
    /* Please note that this is a shared with symbol's discardEvents, even if the show does not "show" it */
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " event => ");
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, rulep->discardEvents);
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, rulep->discardEventb ? "=on" : "=off");
  }
  asciishowl++; /* NUL byte */

  if (asciishowlp != NULL) {
    *asciishowlp = asciishowl;
  }
}

/*****************************************************************************/
static inline void _marpaESLIF_grammar_createshowv(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaESLIF_grammar_t *grammarp, char *asciishows, size_t *asciishowlp)
/*****************************************************************************/
{
  marpaESLIF_t                 *marpaESLIFp = marpaESLIFGrammarp->marpaESLIFp;
  size_t                        asciishowl = 0;
  char                          tmps[1024];
  int                          *ruleip;
  size_t                        rulel;
  char                         *ruleshows;
  int                           l;
  char                          quote[2][2];
  genericStack_t               *symbolStackp = grammarp->symbolStackp;
  marpaESLIF_symbol_t          *symbolp;
  int                           symboli;
  genericStack_t               *ruleStackp = grammarp->ruleStackp;
  marpaESLIF_rule_t            *rulep;
  int                           rulei;
  int                           npropertyi;
  int                           neventi;
  genericLogger_t              *genericLoggerp = NULL;
  marpaESLIF_stringGenerator_t  marpaESLIF_stringGenerator;
  marpaESLIF_uint32_t           pcre2Optioni = 0;
  int                           pcre2Errornumberi;
  short                         skipb;

  /* Calculate the size needed to show the grammar in ASCII form */

  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "/*\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " * **********************\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " * Meta-grammar settings:\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " * **********************\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " */\n");

  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ":start");
  MARPAESLIF_LEVEL_CREATESHOW(grammarp, asciishowl, asciishows);
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " ");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, grammarp->starts);
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");

  if ((! grammarp->descautob) && (grammarp->descp != NULL) && (grammarp->descp->asciis != NULL)) {
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ":desc");
    MARPAESLIF_LEVEL_CREATESHOW(grammarp, asciishowl, asciishows);
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " ");
    MARPAESLIF_STRING_CREATEQUOTE(quote, grammarp->descp->asciis);
    if (strlen(quote[0]) > 0) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, quote[0]);
    }
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, grammarp->descp->asciis);
    if (strlen(quote[1]) > 0) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, quote[1]);
    }
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
  }
  if ((grammarp->defaultRuleActionp != NULL)
      ||
      (grammarp->defaultSymbolActionp != NULL)
      ||
      (grammarp->latmb)
      ) {
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ":default");
    MARPAESLIF_LEVEL_CREATESHOW(grammarp, asciishowl, asciishows);
    if (grammarp->defaultRuleActionp != NULL) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " action => ");
      switch (grammarp->defaultRuleActionp->type) {
      case MARPAESLIF_ACTION_TYPE_NAME:
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, grammarp->defaultRuleActionp->u.names);
        break;
      case MARPAESLIF_ACTION_TYPE_STRING:
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "::u8\"");
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, grammarp->defaultRuleActionp->u.stringp->asciis); /* Best effort ASCII */
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\"");
        break;
      case MARPAESLIF_ACTION_TYPE_LUA:
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "::lua->");
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, grammarp->defaultRuleActionp->u.luas);
        break;
      default:
        break;
      }
    }
    if (grammarp->defaultSymbolActionp != NULL) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " symbol-action => ");
      switch (grammarp->defaultSymbolActionp->type) {
      case MARPAESLIF_ACTION_TYPE_NAME:
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, grammarp->defaultSymbolActionp->u.names);
        break;
      case MARPAESLIF_ACTION_TYPE_STRING:
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "::u8\"");
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, grammarp->defaultSymbolActionp->u.stringp->asciis); /* Best effort ASCII */
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\"");
        break;
      case MARPAESLIF_ACTION_TYPE_LUA:
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "::lua->");
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, grammarp->defaultSymbolActionp->u.luas);
        break;
      default:
        break;
      }
    }
    if (grammarp->latmb) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " latm => 1");
    }
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
  }

  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "/*\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " * ****************\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " * Symbol settings:\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " * ****************\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " */\n");

  /* Lexeme information - this is all about events */
  for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
#ifndef MARPAESLIF_NTRACE
    /* Should never happen */
    if (! GENERICSTACK_IS_PTR(symbolStackp, symboli)) {
      continue;
    }
#endif
    symbolp = (marpaESLIF_symbol_t *) GENERICSTACK_GET_PTR(symbolStackp, symboli);

    /* C.f. the validate(),, we made sure that eventBefores and eventAfters are mutually exclusive */
    if ((symbolp->eventBefores != NULL)
        ||
        (symbolp->eventAfters != NULL)
        ||
        (symbolp->priorityi != 0)
        ||
        (symbolp->symbolActionp != NULL)
        ||
        (symbolp->ifActionp != NULL)
        ) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, MARPAESLIF_IS_LEXEME(symbolp) ? ":lexeme" : ":terminal");
      MARPAESLIF_LEVEL_CREATESHOW(grammarp, asciishowl, asciishows);
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " <");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->descp->asciis);
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ">");
      if (symbolp->eventBefores != NULL) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " pause => before event => ");
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->eventBefores);
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->eventBeforeb ? "=on" : "=off");
      }
      if (symbolp->eventAfters != NULL) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " pause => after event => ");
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->eventAfters);
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->eventAfterb ? "=on" : "=off");
      }
      if (symbolp->priorityi != 0) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " priority => ");
        sprintf(tmps, "%d", symbolp->priorityi);
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, tmps);
      }
      if (symbolp->symbolActionp != NULL) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " symbol-action => ");
        switch (symbolp->symbolActionp->type) {
        case MARPAESLIF_ACTION_TYPE_NAME:
          MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->symbolActionp->u.names);
          break;
        case MARPAESLIF_ACTION_TYPE_STRING:
          MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "::u8\"");
          MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->symbolActionp->u.stringp->asciis); /* Best effort ASCII */
          MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\"");
          break;
        case MARPAESLIF_ACTION_TYPE_LUA:
          MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "::lua->");
          MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->symbolActionp->u.luas);
          break;
        default:
          break;
        }
      }
      if (symbolp->ifActionp != NULL) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " if-action => ");
        switch (symbolp->ifActionp->type) {
        case MARPAESLIF_ACTION_TYPE_NAME:
          MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->ifActionp->u.names);
          break;
        case MARPAESLIF_ACTION_TYPE_STRING:
          MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "::u8\"");
          MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->ifActionp->u.stringp->asciis); /* Best effort ASCII */
          MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\"");
          break;
        case MARPAESLIF_ACTION_TYPE_LUA:
          MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "::lua->");
          MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->ifActionp->u.luas);
          break;
        default:
          break;
        }
      }
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
    }
  }

  /* Event information */
  for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
#ifndef MARPAESLIF_NTRACE
    /* Should never happen */
    if (! GENERICSTACK_IS_PTR(symbolStackp, symboli)) {
      continue;
    }
#endif
    symbolp = (marpaESLIF_symbol_t *) GENERICSTACK_GET_PTR(symbolStackp, symboli);

    if (symbolp->eventPredicteds != NULL) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "event ");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->eventPredicteds);
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->eventPredictedb ? "=on" : "=off");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " = predicted ");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "<");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->descp->asciis);
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ">");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
    }

    if (symbolp->eventNulleds != NULL) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "event ");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->eventNulleds);
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->eventNulledb ? "=on" : "=off");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " = nulled ");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "<");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->descp->asciis);
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ">");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
    }

    if (symbolp->eventCompleteds != NULL) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "event ");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->eventCompleteds);
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->eventCompletedb ? "=on" : "=off");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " = completed ");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "<");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->descp->asciis);
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ">");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
    }

  }

  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "/*\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " * ******\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " * Rules:\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " * ******\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " */\n");

  /* Rules */
  if (marpaESLIFGrammar_rulearray_by_levelb(marpaESLIFGrammarp, &ruleip, &rulel, grammarp->leveli, NULL /* descp */)) {
    for (l = 0; l < rulel; l++) {
      if (marpaESLIFGrammar_ruleshowform_by_levelb(marpaESLIFGrammarp, l, &ruleshows, grammarp->leveli, NULL /* descp */)) {
        if (ruleshows == NULL) {
          continue;
        }
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ruleshows);
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
      }
    }
  }


  /* Give useful information:
     - meta symbols that are terminals (they refered to another grammar)
     - nullable symbols
  */
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "# The following is giving information on grammar components: lexemes, rules and symbols properties\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "# --------\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "# Lexemes:\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "# --------\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#\n");
  for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
#ifndef MARPAESLIF_NTRACE
    /* Should never happen */
    if (! GENERICSTACK_IS_PTR(symbolStackp, symboli)) {
      continue;
    }
#endif
    symbolp = (marpaESLIF_symbol_t *) GENERICSTACK_GET_PTR(symbolStackp, symboli);
    if (symbolp->lhsb) {
      continue;
    }
    if (symbolp->type != MARPAESLIF_SYMBOL_TYPE_META) {
      continue;
    }
    if ((symbolp->lookupMetas != NULL) && (symbolp->lookupResolvedLeveli != grammarp->leveli)) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "# ");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "<");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->descp->asciis);
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ">");
      MARPAESLIF_LEVEL_CREATESHOW(grammarp, asciishowl, asciishows);
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " ");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "<");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->lookupMetas);
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ">");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "@");
      sprintf(tmps, "%+d", symbolp->lookupLevelDeltai);
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, tmps);
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
    }
  }

  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "# -----------------\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "# Rules properties:\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "# -----------------\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#\n");

  for (rulei = 0; rulei < GENERICSTACK_USED(ruleStackp); rulei++) {
#ifndef MARPAESLIF_NTRACE
    /* Should never happen */
    if (! GENERICSTACK_IS_PTR(ruleStackp, rulei)) {
      continue;
    }
#endif
    rulep = (marpaESLIF_rule_t *) GENERICSTACK_GET_PTR(ruleStackp, rulei);
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "# Rule No ");
    sprintf(tmps, "%d", rulep->idi);
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, tmps);
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#   Properties: ");
    npropertyi = 0;
    if ((rulep->propertyBitSet & MARPAWRAPPER_RULE_IS_ACCESSIBLE) == MARPAWRAPPER_RULE_IS_ACCESSIBLE) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "ACCESSIBLE");
      npropertyi++;
    }
    if ((rulep->propertyBitSet & MARPAWRAPPER_RULE_IS_NULLABLE) == MARPAWRAPPER_RULE_IS_NULLABLE) {
      if (npropertyi++ > 0) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ", ");
      }
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "NULLABLE");
    }
    if ((rulep->propertyBitSet & MARPAWRAPPER_RULE_IS_NULLING) == MARPAWRAPPER_RULE_IS_NULLING) {
      if (npropertyi++ > 0) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ", ");
      }
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "NULLING");
    }
    if ((rulep->propertyBitSet & MARPAWRAPPER_RULE_IS_LOOP) == MARPAWRAPPER_RULE_IS_LOOP) {
      if (npropertyi++ > 0) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ", ");
      }
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "LOOP");
    }
    if ((rulep->propertyBitSet & MARPAWRAPPER_RULE_IS_PRODUCTIVE) == MARPAWRAPPER_RULE_IS_PRODUCTIVE) {
      if (npropertyi++ > 0) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ", ");
      }
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "PRODUCTIVE");
    }
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#   Definition: ");
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, rulep->asciishows);

    marpaESLIF_stringGenerator.marpaESLIFp = marpaESLIFp;
    marpaESLIF_stringGenerator.s           = NULL;
    marpaESLIF_stringGenerator.l           = 0;
    marpaESLIF_stringGenerator.okb         = 0;
    marpaESLIF_stringGenerator.allocl      = 0;

    genericLoggerp = GENERICLOGGER_CUSTOM(_marpaESLIF_generateStringWithLoggerCallback, (void *) &marpaESLIF_stringGenerator, GENERICLOGGER_LOGLEVEL_TRACE);
    if (genericLoggerp != NULL) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
      if (rulep->exceptionp != NULL) {
        GENERICLOGGER_TRACE (genericLoggerp, "#   Components:  LHS = RHS - EXCEPTION\n");
      } else {
        GENERICLOGGER_TRACE (genericLoggerp, "#   Components:  LHS = RHS[]\n");
      }
      GENERICLOGGER_TRACEF(genericLoggerp, "#               %4d", rulep->lhsp->idi);
      if (GENERICSTACK_USED(rulep->rhsStackp) > 0) {
        for (symboli = 0; symboli < GENERICSTACK_USED(rulep->rhsStackp); symboli++) {
          symbolp = (marpaESLIF_symbol_t *) GENERICSTACK_GET_PTR(rulep->rhsStackp, symboli);
          skipb = (rulep->skipbp != NULL) && rulep->skipbp[symboli];
          if (symboli == 0) {
            GENERICLOGGER_TRACEF(genericLoggerp, " = %s%d%s", skipb ? "(- " : "", symbolp->idi, skipb ? " -)" : "");
          } else {
            GENERICLOGGER_TRACEF(genericLoggerp, " %s%d%s", skipb ? "(- " : "", symbolp->idi, skipb ? " -)" : "");
          }
        }
      }
      if (rulep->exceptionp != NULL) {
        GENERICLOGGER_TRACEF(genericLoggerp, " - %d", rulep->exceptionp->idi);
      }
      if (marpaESLIF_stringGenerator.okb) {
        if (marpaESLIF_stringGenerator.s != NULL) {
          MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, marpaESLIF_stringGenerator.s);
        }
      }
      if (marpaESLIF_stringGenerator.s != NULL) {
        free(marpaESLIF_stringGenerator.s);
      }
      GENERICLOGGER_FREE(genericLoggerp);
    }
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
  }

  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "# -------------------\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "# Symbols properties:\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "# -------------------\n");
  MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#\n");

  for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
#ifndef MARPAESLIF_NTRACE
    /* Should never happen */
    if (! GENERICSTACK_IS_PTR(symbolStackp, symboli)) {
      continue;
    }
#endif
    symbolp = (marpaESLIF_symbol_t *) GENERICSTACK_GET_PTR(symbolStackp, symboli);

    if (symboli > 0) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#\n");
    }
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "# Symbol No ");
    sprintf(tmps, "%d\n", symbolp->idi);
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, tmps);
    switch (symbolp->type) {
    case MARPAESLIF_SYMBOL_TYPE_TERMINAL:
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#         Type: ESLIF TERMINAL\n");
      break;
    case MARPAESLIF_SYMBOL_TYPE_META:
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#         Type: ESLIF META\n");
      break;
    default:
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#         Type: ?\n");
      break;
    }
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#   Properties: ");
    npropertyi = 0;
    if ((symbolp->propertyBitSet & MARPAWRAPPER_SYMBOL_IS_ACCESSIBLE) == MARPAWRAPPER_SYMBOL_IS_ACCESSIBLE) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "ACCESSIBLE");
      npropertyi++;
    }
    if ((symbolp->propertyBitSet & MARPAWRAPPER_SYMBOL_IS_NULLABLE) == MARPAWRAPPER_SYMBOL_IS_NULLABLE) {
      if (npropertyi++ > 0) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ", ");
      }
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "NULLABLE");
    }
    if ((symbolp->propertyBitSet & MARPAWRAPPER_SYMBOL_IS_NULLING) == MARPAWRAPPER_SYMBOL_IS_NULLING) {
      if (npropertyi++ > 0) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ", ");
      }
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "NULLING");
    }
    if ((symbolp->propertyBitSet & MARPAWRAPPER_SYMBOL_IS_PRODUCTIVE) == MARPAWRAPPER_SYMBOL_IS_PRODUCTIVE) {
      if (npropertyi++ > 0) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ", ");
      }
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "PRODUCTIVE");
    }
    if ((symbolp->propertyBitSet & MARPAWRAPPER_SYMBOL_IS_START) == MARPAWRAPPER_SYMBOL_IS_START) {
      if (npropertyi++ > 0) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ", ");
      }
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "START");
    }
    if ((symbolp->propertyBitSet & MARPAWRAPPER_SYMBOL_IS_TERMINAL) == MARPAWRAPPER_SYMBOL_IS_TERMINAL) {
      if (npropertyi++ > 0) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ", ");
      }
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "TERMINAL");
    }
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#       Events: ");

    neventi = 0;
    if ((symbolp->eventBitSet & MARPAESLIF_SYMBOL_EVENT_COMPLETION) == MARPAESLIF_SYMBOL_EVENT_COMPLETION) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "COMPLETION");
      neventi++;
    }
    if ((symbolp->eventBitSet & MARPAESLIF_SYMBOL_EVENT_NULLED) == MARPAESLIF_SYMBOL_EVENT_NULLED) {
      if (neventi++ > 0) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ", ");
      }
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "NULLED");
    }
    if ((symbolp->eventBitSet & MARPAESLIF_SYMBOL_EVENT_PREDICTION) == MARPAESLIF_SYMBOL_EVENT_PREDICTION) {
      if (neventi++ > 0) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ", ");
      }
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "PREDICTION");
    }
    MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
    if (symbolp->type == MARPAESLIF_SYMBOL_TYPE_TERMINAL) {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#      Pattern:");
      if (symbolp->u.terminalp->type == MARPAESLIF_TERMINAL_TYPE_STRING) {
        /* We know we made a 100% ASCII compatible pattern when the original type is STRING */
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, " ");
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->u.terminalp->patterns);
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
      } else {
        /* We have to dump - this is an opaque UTF-8 pattern */
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
        marpaESLIF_stringGenerator.marpaESLIFp = marpaESLIFp;
        marpaESLIF_stringGenerator.s           = NULL;
        marpaESLIF_stringGenerator.l           = 0;
        marpaESLIF_stringGenerator.okb         = 0;
        marpaESLIF_stringGenerator.allocl      = 0;

        genericLoggerp = GENERICLOGGER_CUSTOM(_marpaESLIF_generateStringWithLoggerCallback, (void *) &marpaESLIF_stringGenerator, GENERICLOGGER_LOGLEVEL_TRACE);
        if (genericLoggerp != NULL) {
          size_t i;
          size_t j;
          size_t lengthl = symbolp->u.terminalp->patternl;
          char  *p = symbolp->u.terminalp->patterns;
          
          for (i = 0; i < lengthl + ((lengthl % MARPAESLIF_HEXDUMP_COLS) ? (MARPAESLIF_HEXDUMP_COLS - lengthl % MARPAESLIF_HEXDUMP_COLS) : 0); i++) {
            /* print offset */
            if (i % MARPAESLIF_HEXDUMP_COLS == 0) {
              GENERICLOGGER_TRACEF(genericLoggerp, "#     0x%06x: ", i);
            }
            /* print hex data */
            if (i < lengthl) {
              GENERICLOGGER_TRACEF(genericLoggerp, "%02x ", 0xFF & p[i]);
            } else { /* end of block, just aligning for ASCII dump */
              GENERICLOGGER_TRACE(genericLoggerp, "   ");
            }
            /* print ASCII dump */
            if (i % MARPAESLIF_HEXDUMP_COLS == (MARPAESLIF_HEXDUMP_COLS - 1)) {
              for (j = i - (MARPAESLIF_HEXDUMP_COLS - 1); j <= i; j++) {
                if (j >= lengthl) { /* end of block, not really printing */
                  GENERICLOGGER_TRACE(genericLoggerp, " ");
                }
                else if (isprint(0xFF & p[j])) { /* printable char */
                  GENERICLOGGER_TRACEF(genericLoggerp, "%c", 0xFF & p[j]);
                }
                else { /* other char */
                  GENERICLOGGER_TRACE(genericLoggerp, ".");
                }
              }
              if (marpaESLIF_stringGenerator.okb) {
                MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, marpaESLIF_stringGenerator.s);
                MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
              }
              if (marpaESLIF_stringGenerator.s != NULL) {
                free(marpaESLIF_stringGenerator.s);
              }
              marpaESLIF_stringGenerator.s = NULL;
              marpaESLIF_stringGenerator.okb = 0;
            }
          }
          GENERICLOGGER_FREE(genericLoggerp);
        }
      }
      /* Dump PCRE flags */
      marpaESLIF_stringGenerator.marpaESLIFp = marpaESLIFp;
      marpaESLIF_stringGenerator.s           = NULL;
      marpaESLIF_stringGenerator.l           = 0;
      marpaESLIF_stringGenerator.okb         = 0;
      marpaESLIF_stringGenerator.allocl      = 0;

      genericLoggerp = GENERICLOGGER_CUSTOM(_marpaESLIF_generateSeparatedStringWithLoggerCallback, (void *) &marpaESLIF_stringGenerator, GENERICLOGGER_LOGLEVEL_TRACE);
      if (genericLoggerp != NULL) {
        pcre2Errornumberi = pcre2_pattern_info(symbolp->u.terminalp->regex.patternp, PCRE2_INFO_ALLOPTIONS, &pcre2Optioni);
        if (pcre2Errornumberi == 0) {
          if ((pcre2Optioni & PCRE2_ANCHORED)            == PCRE2_ANCHORED)            { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_ANCHORED"); }
          if ((pcre2Optioni & PCRE2_ALLOW_EMPTY_CLASS)   == PCRE2_ALLOW_EMPTY_CLASS)   { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_ALLOW_EMPTY_CLASS"); }
          if ((pcre2Optioni & PCRE2_ALT_BSUX)            == PCRE2_ALT_BSUX)            { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_ALT_BSUX"); }
          if ((pcre2Optioni & PCRE2_ALT_CIRCUMFLEX)      == PCRE2_ALT_CIRCUMFLEX)      { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_ALT_CIRCUMFLEX"); }
          if ((pcre2Optioni & PCRE2_ALT_VERBNAMES)       == PCRE2_ALT_VERBNAMES)       { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_ALT_VERBNAMES"); }
          if ((pcre2Optioni & PCRE2_AUTO_CALLOUT)        == PCRE2_AUTO_CALLOUT)        { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_AUTO_CALLOUT"); }
          if ((pcre2Optioni & PCRE2_CASELESS)            == PCRE2_CASELESS)            { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_CASELESS"); }
          if ((pcre2Optioni & PCRE2_DOLLAR_ENDONLY)      == PCRE2_DOLLAR_ENDONLY)      { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_DOLLAR_ENDONLY"); }
          if ((pcre2Optioni & PCRE2_DOTALL)              == PCRE2_DOTALL)              { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_DOTALL"); }
          if ((pcre2Optioni & PCRE2_DUPNAMES)            == PCRE2_DUPNAMES)            { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_DUPNAMES"); }
          if ((pcre2Optioni & PCRE2_EXTENDED)            == PCRE2_EXTENDED)            { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_EXTENDED"); }
          if ((pcre2Optioni & PCRE2_FIRSTLINE)           == PCRE2_FIRSTLINE)           { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_FIRSTLINE"); }
          if ((pcre2Optioni & PCRE2_MATCH_UNSET_BACKREF) == PCRE2_MATCH_UNSET_BACKREF) { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_MATCH_UNSET_BACKREF"); }
          if ((pcre2Optioni & PCRE2_MULTILINE)           == PCRE2_MULTILINE)           { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_MULTILINE"); }
          if ((pcre2Optioni & PCRE2_NEVER_BACKSLASH_C)   == PCRE2_NEVER_BACKSLASH_C)   { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_NEVER_BACKSLASH_C"); }
          if ((pcre2Optioni & PCRE2_NEVER_UCP)           == PCRE2_NEVER_UCP)           { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_NEVER_UCP"); }
          if ((pcre2Optioni & PCRE2_NEVER_UTF)           == PCRE2_NEVER_UTF)           { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_NEVER_UTF"); }
          if ((pcre2Optioni & PCRE2_NO_AUTO_CAPTURE)     == PCRE2_NO_AUTO_CAPTURE)     { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_NO_AUTO_CAPTURE"); }
          if ((pcre2Optioni & PCRE2_NO_AUTO_POSSESS)     == PCRE2_NO_AUTO_POSSESS)     { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_NO_AUTO_POSSESS"); }
          if ((pcre2Optioni & PCRE2_NO_DOTSTAR_ANCHOR)   == PCRE2_NO_DOTSTAR_ANCHOR)   { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_NO_DOTSTAR_ANCHOR"); }
          if ((pcre2Optioni & PCRE2_NO_START_OPTIMIZE)   == PCRE2_NO_START_OPTIMIZE)   { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_NO_START_OPTIMIZE"); }
          if ((pcre2Optioni & PCRE2_NO_UTF_CHECK)        == PCRE2_NO_UTF_CHECK)        { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_NO_UTF_CHECK"); }
          if ((pcre2Optioni & PCRE2_UCP)                 == PCRE2_UCP)                 { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_UCP"); }
          if ((pcre2Optioni & PCRE2_UNGREEDY)            == PCRE2_UNGREEDY)            { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_UNGREEDY"); }
          if ((pcre2Optioni & PCRE2_USE_OFFSET_LIMIT)    == PCRE2_USE_OFFSET_LIMIT)    { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_USE_OFFSET_LIMIT"); }
          if ((pcre2Optioni & PCRE2_UTF)                 == PCRE2_UTF)                 { GENERICLOGGER_TRACE(genericLoggerp, "PCRE2_UTF"); }
          if (marpaESLIF_stringGenerator.okb) {
            MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#        Flags: ");
            MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, marpaESLIF_stringGenerator.s);
            MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
          }
          if (marpaESLIF_stringGenerator.s != NULL) {
            free(marpaESLIF_stringGenerator.s);
          }
        }
        GENERICLOGGER_FREE(genericLoggerp);
      }
#ifdef PCRE2_CONFIG_JIT
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#          JIT: ");
      if (symbolp->u.terminalp->regex.jitCompleteb) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "complete=yes");
      } else {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "complete=no");
      }
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ", ");
      if (symbolp->u.terminalp->regex.jitPartialb) {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "partial=yes");
      } else {
        MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "partial=no");
      }
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
#endif
    } else {
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "#         Name: ");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "<");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, symbolp->descp->asciis);
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, ">");
      MARPAESLIF_STRING_CREATESHOW(asciishowl, asciishows, "\n");
    }
  }

  asciishowl++; /* NUL byte */

  if (asciishowlp != NULL) {
    *asciishowlp = asciishowl;
  }
}

/*****************************************************************************/
static inline int _marpaESLIF_utf82ordi(PCRE2_SPTR8 utf8bytes, marpaESLIF_uint32_t *uint32p)
/*****************************************************************************/
/* This is a copy of utf2ord from pcre2test.c
-----------------------------------------------------------------------------
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    * Neither the name of the University of Cambridge nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------------
*/
/* This function reads one or more bytes that represent a UTF-8 character,
and returns the codepoint of that character. Note that the function supports
the original UTF-8 definition of RFC 2279, allowing for values in the range 0
to 0x7fffffff, up to 6 bytes long. This makes it possible to generate
codepoints greater than 0x10ffff which are useful for testing PCRE2's error
checking, and also for generating 32-bit non-UTF data values above the UTF
limit.

Argument:
  utf8bytes   a pointer to the byte vector
  vptr        a pointer to an int to receive the value

Returns:      >  0 => the number of bytes consumed
              -6 to 0 => malformed UTF-8 character at offset = (-return)
*/
/*****************************************************************************/
{
  marpaESLIF_uint32_t c = *utf8bytes++;
  marpaESLIF_uint32_t d = c;
  int i, j, s;
  const int utf8_table1[] = { 0x7f, 0x7ff, 0xffff, 0x1fffff, 0x3ffffff, 0x7fffffff};
  const int utf8_table3[] = { 0xff, 0x1f, 0x0f, 0x07, 0x03, 0x01};
  const int utf8_table1_size = sizeof(utf8_table1) / sizeof(int);

  for (i = -1; i < 6; i++) {               /* i is number of additional bytes */
    if ((d & 0x80) == 0) break;
    d <<= 1;
  }

  if (i == -1) {
    /* ascii character */
    *uint32p = c;
    return 1;
  }
  if (i == 0 || i == 6) {
    return 0;
  } /* invalid UTF-8 */

  /* i now has a value in the range 1-5 */

  s = 6*i;
  d = (c & utf8_table3[i]) << s;

  for (j = 0; j < i; j++) {
    c = *utf8bytes++;
    if ((c & 0xc0) != 0x80) {
      return -(j+1);
    }
    s -= 6;
    d |= (c & 0x3f) << s;
  }

  /* Check that encoding was the correct unique one */

  for (j = 0; j < utf8_table1_size; j++) {
    if (d <= (uint32_t)utf8_table1[j]) {
      break;
    }
  }
  if (j != i) {
    return -(i+1);
  }

  /* Valid value */

  *uint32p = d;
  return i+1;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_matchPostProcessingb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, size_t matchl)
/*****************************************************************************/
{
  static const char          *funcs              = "_marpaESLIFRecognizer_matchPostProcessingb";
  marpaESLIF_stream_t        *marpaESLIF_streamp = marpaESLIFRecognizerp->marpaESLIF_streamp;
  marpaESLIF_terminal_t      *newlinep;
  marpaESLIF_terminal_t      *anycharp;
  char                       *linep;
  size_t                      linel;
  size_t                      matchedLengthl;
  marpaESLIF_matcher_value_t  rci;
  short                       rcb;
    
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  /* If newline counting is on, so do we - only at first level */
  if (marpaESLIFRecognizerp->marpaESLIFRecognizerOption.newlineb && marpaESLIF_streamp->utfb && (marpaESLIFRecognizerp->leveli == 0)) {
    newlinep = marpaESLIFRecognizerp->marpaESLIFp->newlinep;
    anycharp = marpaESLIFRecognizerp->marpaESLIFp->anycharp;
    linep = marpaESLIF_streamp->inputs;
    linel = matchl;

    /* Check newline */
    while (1) {
      /* We count newlines only when a discard or a complete has happened. So by definition */
      /* character sequences are complete. This is why we fake EOF to true. */
      if (! _marpaESLIFRecognizer_terminal_matcherb(marpaESLIFRecognizerp,
                                                    newlinep,
                                                    linep,
                                                    linel,
                                                    1, /* eofb */
                                                    &rci,
                                                    NULL /* marpaESLIFValueResultp */,
                                                    &matchedLengthl)) {
        goto err;
      }
      if (rci != MARPAESLIF_MATCH_OK) {
        break;
      }
      linep += matchedLengthl;
      linel -= matchedLengthl;
      /* A new line, reset column count */
      marpaESLIF_streamp->linel++;
      marpaESLIF_streamp->columnl = 0;
    }

    if (linel > 0) {
      /* Count characters */
      while (1) {
        /* We count newlines only when a discard or a complete has happened. So by definition */
        /* character sequences are complete. This is why we fake EOF to true. */
        if (! _marpaESLIFRecognizer_terminal_matcherb(marpaESLIFRecognizerp,
                                                      anycharp,
                                                      linep,
                                                      linel,
                                                      1, /* eofb */
                                                      &rci,
                                                      NULL /* marpaESLIFValueResultp */,
                                                      &matchedLengthl)) {
          goto err;
        }
        if (rci != MARPAESLIF_MATCH_OK) {
          break;
        }
        linep += matchedLengthl;
        linel -= matchedLengthl;
        /* A new character */
        marpaESLIF_streamp->columnl++;
      }
    }
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
short marpaESLIFRecognizer_progressLogb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, int starti, int endi, genericLoggerLevel_t logleveli)
/*****************************************************************************/
{
  static const char *funcs = "marpaESLIFRecognizer_progressLogb";
  short              rcb;

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  rcb = marpaWrapperRecognizer_progressLogb(marpaESLIFRecognizerp->marpaWrapperRecognizerp,
                                            starti,
                                            endi,
                                            logleveli,
                                            marpaESLIFRecognizerp->marpaESLIFGrammarp,
                                            _marpaESLIFGrammar_symbolDescriptionCallback);

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
marpaESLIFRecognizer_t *marpaESLIFValue_recognizerp(marpaESLIFValue_t *marpaESLIFValuep)
/*****************************************************************************/
{
  static const char *funcs = "marpaESLIFValue_recognizerp";

  if (marpaESLIFValuep == NULL) {
    errno = EINVAL;
    return NULL;
  }

  return marpaESLIFValuep->marpaESLIFRecognizerp;
}

/*****************************************************************************/
marpaESLIFValueOption_t *marpaESLIFValue_optionp(marpaESLIFValue_t *marpaESLIFValuep)
/*****************************************************************************/
{
  static const char *funcs = "marpaESLIFValue_optionp";

  if (marpaESLIFValuep == NULL) {
    errno = EINVAL;
    return NULL;
  }

  return &(marpaESLIFValuep->marpaESLIFValueOption);
}

/*****************************************************************************/
marpaESLIFGrammar_t *marpaESLIFRecognizer_grammarp(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  static const char *funcs = "marpaESLIFRecognizer_grammarp";

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return NULL;
  }

  return marpaESLIFRecognizerp->marpaESLIFGrammarp;
}

/*****************************************************************************/
marpaESLIFRecognizerOption_t *marpaESLIFRecognizer_optionp(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  static const char *funcs = "marpaESLIFRecognizer_optionp";

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return NULL;
  }

  return &(marpaESLIFRecognizerp->marpaESLIFRecognizerOption);
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_appendDatab(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char *datas, size_t datal, short eofb)
/*****************************************************************************/
{
  static const char   *funcs              = "_marpaESLIFRecognizer_appendDatab";
  marpaESLIF_stream_t *marpaESLIF_streamp = marpaESLIFRecognizerp->marpaESLIF_streamp;
  char                *buffers            = marpaESLIF_streamp->buffers;
  size_t               bufferallocl       = marpaESLIF_streamp->bufferallocl;
  char                *globalOffsetp      = marpaESLIF_streamp->globalOffsetp;
  size_t               bufferl            = marpaESLIF_streamp->bufferl;
  size_t               inputl             = marpaESLIF_streamp->inputl;
  size_t               deltal             = marpaESLIF_streamp->inputs - buffers;
  size_t               bufsizl            = marpaESLIF_streamp->bufsizl;
  size_t               buftriggerl        = marpaESLIF_streamp->buftriggerl;
  unsigned int         bufaddperci        = marpaESLIFRecognizerp->marpaESLIFRecognizerOption.bufaddperci;
  short                removebomb;
  size_t               bomsizel;
  size_t               wantedl;
  size_t               minwantedl;
  char                *tmps;
  short                rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "start (datas=%p, datal=%ld)", datas, (unsigned long) datal);

  if (datal <= 0) {
    /* Nothing to do */
    rcb = 1;
    goto done;
  }

  if (marpaESLIFRecognizerp->parentRecognizerp == NULL) {
    /* We can crunch data at any time unless blocked because of a pending BOM check. */

    if (((marpaESLIF_streamp->tconvp == NULL) || marpaESLIF_streamp->bomdoneb) /* BOM check done or not needed ? */
        &&
        (bufferallocl > buftriggerl)          /* If we allocated more than the trigger */
        &&                                    /* and */
        (inputl > 0)                          /* some bytes were already processed */
        &&                                    /* and */
        (inputl < bufsizl)                    /* there is less remaining bytes to process than minimum buffer size */
        ) {
      if ((marpaESLIF_streamp->tconvp == NULL) || marpaESLIF_streamp->bomdoneb) {
        /* ... then we can realloc to minimum buffer size */

        /* Before reallocating, we need to move the remaining bytes at the beginning */
        memmove(buffers, marpaESLIF_streamp->inputs, inputl);
        /* Try to realloc */
        wantedl = bufsizl;
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Resizing internal buffer size from %ld bytes to %ld bytes", (unsigned long) bufferallocl, (unsigned long) wantedl);
        tmps = realloc(buffers, wantedl + 1); /* We always add a hiden NUL byte for convenience */
        if (tmps == NULL) {
          /* We COULD continue, this is not truely fatal - but we are in a bad shape anyway -; */
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "realloc failure, %s", strerror(errno));
          goto err;
        }
        buffers       = marpaESLIF_streamp->buffers      = tmps;        /* Buffer pointer */
        bufferallocl  = marpaESLIF_streamp->bufferallocl = wantedl;     /* Allocated size */
        bufferl       = marpaESLIF_streamp->bufferl      = inputl;      /* Number of valid bytes */
        globalOffsetp += inputl;                                        /* We "forget" inputl bytes: increase global offset (size_t turnaround not checked) */
        marpaESLIF_streamp->globalOffsetp = globalOffsetp;
        /* Pointer inside internal buffer is back to the beginning */
        marpaESLIF_streamp->inputs = buffers;
        tmps[wantedl] = '\0';
      }
    }
  }

  /* Append data */
  if (buffers == NULL) {
    /* First time we put in the buffer */
    wantedl = (bufsizl < datal) ? datal : bufsizl;
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Creating an internal buffer of %ld bytes", (unsigned long) wantedl);
    tmps = (char *) malloc(wantedl + 1); /* We always add a NUL byte for convenience */
    if (tmps == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "malloc failure, %s", strerror(errno));
      goto err;
    }
    buffers      = marpaESLIF_streamp->buffers      = tmps;        /* Buffer pointer */
    bufferallocl = marpaESLIF_streamp->bufferallocl = wantedl;     /* Allocated size */
    bufferl      = marpaESLIF_streamp->bufferl      = 0;           /* Number of valid bytes (increased below) */
    buffers[bufferl] = '\0';
    /* Pointer inside internal buffer is at the beginning */
    marpaESLIF_streamp->inputs = buffers;
  } else {
    wantedl = bufferl + datal;
    if (wantedl > bufferallocl) {
      /* We need more bytes than what has been allocated. Apply augment policy */
      minwantedl = (bufferallocl * (1 + bufaddperci)) / 100;
      if (wantedl < minwantedl) {
        wantedl = minwantedl;
      }
      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Resizing an internal buffer from %ld bytes to %ld bytes", (unsigned long) bufferl, (unsigned long) wantedl);
      tmps = realloc(buffers, wantedl + 1); /* We always add a NUL byte for convenience */
      if (tmps == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "realloc failure, %s", strerror(errno));
        goto err;
      }
      buffers      = marpaESLIF_streamp->buffers      = tmps;        /* Buffer pointer */
      bufferallocl = marpaESLIF_streamp->bufferallocl = wantedl;     /* Allocated size */
      /* Pointer inside internal buffer is moving */
      marpaESLIF_streamp->inputs = buffers + deltal;
      buffers[bufferl] = '\0';
    }
  }

  /* In any case, append data just after the valid bytes */
  memcpy(buffers + bufferl, datas, datal);

  /* Commit number of valid bytes, and number of remaining bytes to process */
  marpaESLIF_streamp->bufferl += datal;
  marpaESLIF_streamp->inputl  += datal;

  /* In character mode, process BOM if not already done - we test marpaESLIF_streamp->tconvp instead of marpaESLIF_streamp->charconvb because */
  /* the later is set to true only after append data is done */
  if ((marpaESLIF_streamp->tconvp != NULL) && (! marpaESLIF_streamp->bomdoneb)) {
    removebomb = _marpaESLIF_string_removebomb(marpaESLIFRecognizerp->marpaESLIFp, marpaESLIF_streamp->inputs, &(marpaESLIF_streamp->inputl), (char *) MARPAESLIF_UTF8_STRING, &bomsizel);
    if (! removebomb) {
      goto err;
    } else if (removebomb > 0) {
      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "BOM is %ld bytes", (unsigned long) bomsizel);
      /* BOM processed */
      marpaESLIF_streamp->bomdoneb = 1;
      /* It is guaranteed that buffer was never crunched because of this pending BOM check - _marpaESLIF_string_removebomb() did an internal memmove, decreasing inputl */
      marpaESLIF_streamp->bufferl = marpaESLIF_streamp->inputl;
    } else {
      if (! eofb) {
        MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "BOM must be checked at next read");
        rcb = -1;
        goto done;
      } else {
        MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "BOM cannot be checked and eof is reached");
      }
    }
  }

  /* Please see the free method for the impact on parent's current pointer in input   */
  /* This need to be done once only, at return, this is why it is done at free level. */
  /* Note that when we create a grand child we strip off ALL events, so the user can */
  /* never got control back until we are finished. I.e. until all the free methods of */
  /* all the children are executed -; */

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_createDiscardStateb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  static const char    *funcs                = "_marpaESLIFRecognizer_createDiscardStateb";
  short                *discardEventStatebp  = marpaESLIFRecognizerp->discardEventStatebp;
  marpaESLIFGrammar_t  *marpaESLIFGrammarp;
  marpaESLIF_grammar_t *grammarp;
  genericStack_t       *symbolStackp;
  short                 rcb;
  int                   symboli;
  marpaESLIF_symbol_t  *symbolp;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (discardEventStatebp == NULL) {
    /* First time */

    marpaESLIFGrammarp   = marpaESLIFRecognizerp->marpaESLIFGrammarp;
    grammarp             = marpaESLIFGrammarp->grammarp;
    symbolStackp         = grammarp->symbolStackp;

    discardEventStatebp = (short *) malloc(sizeof(short) * GENERICSTACK_USED(symbolStackp));
    if (discardEventStatebp == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "malloc failure, %s", strerror(errno));
      goto err;
    }
    for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
      MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFRecognizerp->marpaESLIFp, symbolp, symbolStackp, symboli);
      discardEventStatebp[symboli] = symbolp->discardEventb;
    }

    /* Initialization ok */
    marpaESLIFRecognizerp->discardEventStatebp = discardEventStatebp;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_createBeforeStateb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  static const char    *funcs               = "_marpaESLIFRecognizer_createBeforeStateb";
  short                *beforeEventStatebp  = marpaESLIFRecognizerp->beforeEventStatebp;
  marpaESLIFGrammar_t  *marpaESLIFGrammarp;
  marpaESLIF_grammar_t *grammarp;
  genericStack_t       *symbolStackp;
  short                 rcb;
  int                   symboli;
  marpaESLIF_symbol_t  *symbolp;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (beforeEventStatebp == NULL) {
    /* First time */

    marpaESLIFGrammarp  = marpaESLIFRecognizerp->marpaESLIFGrammarp;
    grammarp            = marpaESLIFGrammarp->grammarp;
    symbolStackp        = grammarp->symbolStackp;

    beforeEventStatebp = (short *) malloc(sizeof(short) * GENERICSTACK_USED(symbolStackp));
    if (beforeEventStatebp == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "malloc failure, %s", strerror(errno));
      goto err;
    }
    for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
      MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFRecognizerp->marpaESLIFp, symbolp, symbolStackp, symboli);
      beforeEventStatebp[symboli] = symbolp->eventBeforeb;
    }

    /* Initialization ok */
    marpaESLIFRecognizerp->beforeEventStatebp = beforeEventStatebp;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_createAfterStateb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  static const char    *funcs              = "_marpaESLIFRecognizer_createAfterStateb";
  short                *afterEventStatebp  = marpaESLIFRecognizerp->afterEventStatebp;
  marpaESLIFGrammar_t  *marpaESLIFGrammarp;
  marpaESLIF_grammar_t *grammarp;
  genericStack_t       *symbolStackp;
  short                 rcb;
  int                   symboli;
  marpaESLIF_symbol_t  *symbolp;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (afterEventStatebp == NULL) {
    /* First time */

    marpaESLIFGrammarp  = marpaESLIFRecognizerp->marpaESLIFGrammarp;
    grammarp            = marpaESLIFGrammarp->grammarp;
    symbolStackp        = grammarp->symbolStackp;

    afterEventStatebp = (short *) malloc(sizeof(short) * GENERICSTACK_USED(symbolStackp));
    if (afterEventStatebp == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "malloc failure, %s", strerror(errno));
      goto err;
    }
    for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
      MARPAESLIF_INTERNAL_GET_SYMBOL_FROM_STACK(marpaESLIFRecognizerp->marpaESLIFp, symbolp, symbolStackp, symboli);
      afterEventStatebp[symboli] = symbolp->eventAfterb;
    }

    /* Initialization ok */
    marpaESLIFRecognizerp->afterEventStatebp = afterEventStatebp;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_createLexemeDatab(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIF_lexeme_data_t ***lexemeDatappp, short forPauseb)
/*****************************************************************************/
{
  /* It assumed that lexemeDatappp is != NULL */
  static const char         *funcs        = "_marpaESLIFRecognizer_createLexemeDatab";
  marpaESLIF_t              *marpaESLIFp  = marpaESLIFRecognizerp->marpaESLIFp;
  marpaESLIF_lexeme_data_t **lexemeDatapp = *lexemeDatappp;
  marpaESLIFGrammar_t        *marpaESLIFGrammarp;
  marpaESLIF_grammar_t       *grammarp;
  genericStack_t             *symbolStackp;
  short                       rcb;
  int                         symboli;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (lexemeDatapp == NULL) {
    /* First time */

    marpaESLIFGrammarp  = marpaESLIFRecognizerp->marpaESLIFGrammarp;
    grammarp            = marpaESLIFGrammarp->grammarp;
    symbolStackp        = grammarp->symbolStackp;

    if (marpaESLIFp->NULLisZeroBytesb) {
      lexemeDatapp = (marpaESLIF_lexeme_data_t **) calloc(GENERICSTACK_USED(symbolStackp), sizeof(marpaESLIF_lexeme_data_t *));
      if (lexemeDatapp == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "calloc failure, %s", strerror(errno));
        goto err;
      }
    } else {
      lexemeDatapp = (marpaESLIF_lexeme_data_t **) malloc(sizeof(marpaESLIF_lexeme_data_t *) * GENERICSTACK_USED(symbolStackp));
      if (lexemeDatapp == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
        goto err;
      }
      for (symboli = 0; symboli < GENERICSTACK_USED(symbolStackp); symboli++) {
        lexemeDatapp[symboli] = NULL;
      }
    }

    /* Initialization ok */
    *lexemeDatappp = lexemeDatapp;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_createLastPauseb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  return _marpaESLIFRecognizer_createLexemeDatab(marpaESLIFRecognizerp, &(marpaESLIFRecognizerp->lastPausepp), 1 /* forPauseb */);
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_createLastTryb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  return _marpaESLIFRecognizer_createLexemeDatab(marpaESLIFRecognizerp, &(marpaESLIFRecognizerp->lastTrypp), 0 /* forPauseb */);
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_encoding_eqb(marpaESLIFRecognizer_t *marpaESLIFRecognizerParentp, marpaESLIF_terminal_t *terminalp, char *inputs, size_t inputl)
/*****************************************************************************/
{
  /* Note that we do not support aliases nor case insensitivity */
  static const char         *funcs                    = "_marpaESLIFRecognizer_encoding_eqb";
  marpaESLIF_t              *marpaESLIFp              = marpaESLIFRecognizerParentp->marpaESLIFp;
  marpaESLIF_stream_t       *marpaESLIF_parentStreamp = marpaESLIFRecognizerParentp->marpaESLIF_streamp;
  marpaESLIFRecognizer_t    *marpaESLIFRecognizerp    = NULL;
  char                      *utf8s                    = NULL;
  size_t                     utf8l;
  marpaESLIFGrammar_t        marpaESLIFGrammar;
  marpaESLIF_matcher_value_t rci;
  short                      rcb;

#ifndef MARPAESLIF_NTRACE
  marpaESLIFRecognizerParentp->callstackCounteri++;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerParentp, funcs, "start");
#endif

  /* Before launching the bazooka, the very typical use case if that the encoding */
  /* is always the same... So a simple and fast memcmp will do it. */
  if (marpaESLIF_parentStreamp->lastFroms != NULL) {
    if (marpaESLIF_parentStreamp->lastFroml == inputl) {
      if (memcmp(marpaESLIF_parentStreamp->lastFroms, inputs, inputl) == 0) {
        rcb = 1;
        goto done;
      }
    }
  }

  /* First we want to make sure that inputs is in UTF-8, input encoding is free though ASCII is recommended as per the documentation */
  utf8s = _marpaESLIF_charconvb(marpaESLIFRecognizerParentp->marpaESLIFp, (char *) MARPAESLIF_UTF8_STRING, NULL /* fromEncodings */, inputs, inputl, &utf8l, NULL /* fromEncodingsp */, NULL /* tconvpp */, 1 /* eofb */, NULL /* byteleftsp */, NULL /* byteleftlp */, NULL /* byteleftalloclp */, 0 /* tconvsilentb */);
  if (utf8s == NULL) {
    goto err;
  }

  /* terminalp points to a case-insensitive string match terminal */
  marpaESLIFGrammar.marpaESLIFp = marpaESLIFp;
  /* Fake a recognizer. EOF flag will be set automatically in fake mode */
  marpaESLIFRecognizerp = _marpaESLIFRecognizer_newp(&marpaESLIFGrammar,
                                                     NULL, /* marpaESLIFRecognizerOptionp */
                                                     0, /* discardb - no effect anyway because we are in fake mode */
                                                     1, /* noEventb - no effect anyway because we are in fake mode */
                                                     0, /* silentb */
                                                     marpaESLIFRecognizerParentp,
                                                     1, /* fakeb */
                                                     0, /* maxStartCompletionsi */
                                                     1, /* We made sure input is UTF-8 valid */
                                                     1 /* grammmarIsOnStackb */);
  if (marpaESLIFRecognizerp == NULL) {
    goto err;
  }
#ifndef MARPAESLIF_NTRACE
  MARPAESLIF_HEXDUMPV(marpaESLIFRecognizerParentp, "Trying to match", terminalp->descp->asciis, utf8s, utf8l, 1 /* traceb */);
#endif
  /* Try to match */
  if (! _marpaESLIFRecognizer_terminal_matcherb(marpaESLIFRecognizerp, terminalp, utf8s, utf8l, 1 /* eofb */, &rci, NULL /* marpaESLIFValueResult */, NULL /* matchedLengthlp */)) {
    goto err;
  }
  if (rci != MARPAESLIF_MATCH_OK) {
    MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerParentp, funcs, "Encodings do not match");
    goto err;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  if (utf8s != NULL) {
    free(utf8s);
  }
  marpaESLIFRecognizer_freev(marpaESLIFRecognizerp);

#ifndef MARPAESLIF_NTRACE
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerParentp, funcs, "return %d", (int) rcb);
  marpaESLIFRecognizerParentp->callstackCounteri--;
#endif
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_flush_charconvb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  static const char   *funcs              = "_marpaESLIFRecognizer_flush_charconvb";
  marpaESLIF_t        *marpaESLIFp        = marpaESLIFRecognizerp->marpaESLIFp;
  marpaESLIF_stream_t *marpaESLIF_streamp = marpaESLIFRecognizerp->marpaESLIF_streamp;
  char                *utf8s              = NULL;
  size_t               utf8l;
  short                rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  /* It is a non-sense to flush a character conversion engine if we were not already in this state */
  if (! marpaESLIF_streamp->charconvb) {
    MARPAESLIF_ERROR(marpaESLIFp, "Previous state says character conversion is off");
    goto err;
  }
  if (marpaESLIF_streamp->encodings == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Previous encoding is unknown");
    goto err;
  }
  if (marpaESLIF_streamp->tconvp == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Previous conversion engine is not set");
    goto err;
  }
  if (marpaESLIF_streamp->encodingp == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Previous encoding is not associated to a fake terminal");
    goto err;
  }

  utf8s = _marpaESLIF_charconvb(marpaESLIFp, NULL /* toEncodings, was MARPAESLIF_UTF8_STRING */, NULL /* fromEncodings */, NULL /* srcs */, 0 /* srcl */, &utf8l /* dstlp */, NULL /* fromEncodingsp */, &(marpaESLIF_streamp->tconvp), 1 /* eofb */, &(marpaESLIF_streamp->bytelefts), &(marpaESLIF_streamp->byteleftl), &(marpaESLIF_streamp->byteleftallocl), 0 /* tconvsilentb */);
  if (utf8s == NULL) {
    goto err;
  }
  if (! _marpaESLIFRecognizer_appendDatab(marpaESLIFRecognizerp, utf8s, utf8l, 1 /* eofb */)) {
    goto err;
  }

  /* last state is cleaned */
  free(marpaESLIF_streamp->encodings);
  marpaESLIF_streamp->encodings = NULL;

  _marpaESLIF_terminal_freev(marpaESLIF_streamp->encodingp);
  marpaESLIF_streamp->encodingp = NULL;
  
  if (tconv_close(marpaESLIF_streamp->tconvp) != 0) {
    MARPAESLIF_ERRORF(marpaESLIFp, "tconv_close failure, %s", strerror(errno));
    marpaESLIF_streamp->tconvp = NULL; /* A priori a retry is a bad idea, even during general cleanup... */
    goto err;
  }
  marpaESLIF_streamp->tconvp = NULL;

  /* Put global flag to off */
  marpaESLIF_streamp->charconvb = 0;

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  if (utf8s != NULL) {
    free(utf8s);
  }
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_start_charconvb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char *encodings, size_t encodingl, char *srcs, size_t srcl, short eofb)
/*****************************************************************************/
/* Take care: this CAN RETURN -1, meaning that it needs more data, the reason is BOM removal */
/*****************************************************************************/
{
  static const char          *funcs              = "_marpaESLIFRecognizer_start_charconvb";
  marpaESLIF_t               *marpaESLIFp        = marpaESLIFRecognizerp->marpaESLIFp;
  marpaESLIF_stream_t        *marpaESLIF_streamp = marpaESLIFRecognizerp->marpaESLIF_streamp;
  char                       *encodingasciis     = NULL;
  char                       *encodingutf8s      = NULL;
  char                       *utf8s              = NULL;
  size_t                      utf8l;
  short                       appendDatab;
  short                       rcb;
  size_t                      encodingutf8l;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  /* It is a non-sense to start a character conversion engine if we were already in this state */
  if (marpaESLIF_streamp->charconvb) {
    MARPAESLIF_ERROR(marpaESLIFp, "Previous state says character conversion is on");
    goto err;
  }
  if (marpaESLIF_streamp->encodings != NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Previous encoding is already known");
    goto err;
  }
  if (marpaESLIF_streamp->tconvp != NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Previous conversion engine is already set");
    goto err;
  }
  if (marpaESLIF_streamp->encodingp != NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Previous encoding is already associated to a fake terminal");
    goto err;
  }

  /* New char conversion is starting: we have to take care of the BOM */
  marpaESLIF_streamp->bomdoneb = 0;

  /* Get an eventual ASCII version of input encoding */
  if ((encodings != NULL) && (encodingl > 0)) {
    /* Encoding is in ASCII as per IANA - we do not not conform exactly to IANA in the sense they say that non ASCII should be translated to "_" if I remember well */
    /* We ignore non-ASCII characters. */
    encodingasciis = _marpaESLIF_charconvb(marpaESLIFp, "ASCII//IGNORE", NULL /* fromEncodings */, encodings, encodingl, NULL /* dstlp */, NULL /* fromEncodingsp */, NULL /* tconvpp */, 1 /* eofb */, NULL /* byteleftsp */, NULL /* byteleftlp */, NULL /* byteleftalloclp */, 0 /* tconvsilentb */);
    if (encodingasciis == NULL) {
      goto err;
    }
  }

  /* Convert input */
  utf8s = _marpaESLIF_charconvb(marpaESLIFp, (char *) MARPAESLIF_UTF8_STRING, encodingasciis, srcs, srcl, &utf8l, &(marpaESLIF_streamp->encodings), &(marpaESLIF_streamp->tconvp), eofb, &(marpaESLIF_streamp->bytelefts), &(marpaESLIF_streamp->byteleftl), &(marpaESLIF_streamp->byteleftallocl), 0 /* tconvsilentb */);
  if (utf8s == NULL) {
    goto err;
  }

  /* Verify information is set */
  if (marpaESLIF_streamp->encodings == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Encoding has not been set");
    goto err;
  }
  if (marpaESLIF_streamp->tconvp == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Conversion engine has not been set");
    goto err;
  }

  /* We hardcode conversion to UTF-8, tconv will certify UTF-8 correctness */
  marpaESLIF_streamp->utfb = 1;

  /* Get an UTF-8 version of encoding - always set at this stage in marpaESLIF_streamp->encodings */
  encodingutf8s = _marpaESLIF_charconvb(marpaESLIFp, (char *) MARPAESLIF_UTF8_STRING, "ASCII", marpaESLIF_streamp->encodings, strlen(marpaESLIF_streamp->encodings), &encodingutf8l, NULL /* fromEncodingsp */, NULL /* tconvpp */, 1 /* eofb */, NULL /* byteleftsp */, NULL /* byteleftlp */, NULL /* byteleftalloclp */, 0 /* tconvsilentb */);
  if (encodingutf8s == NULL) {
    goto err;
  }

  /* Create terminal regexp corresponding to the encoding */
  marpaESLIF_streamp->encodingp = _marpaESLIF_terminal_newp(marpaESLIFp,
							    NULL /* grammarp */,
							    MARPAESLIF_EVENTTYPE_NONE, /* eventSeti */
							    "ASCII", /* descEncodings */
							    marpaESLIF_streamp->encodings /* descs */,
							    strlen(marpaESLIF_streamp->encodings) /* descl */,
							    MARPAESLIF_TERMINAL_TYPE_REGEX, /* type */
							    "i", /* modifiers */
							    encodingutf8s, /* utf8s */
							    encodingutf8l, /* utf8l */
							    NULL, /* testFullMatchs */
							    NULL  /* testPartialMatchs */
							    );
  if (marpaESLIF_streamp->encodingp == NULL) {
    goto err;
  }

  appendDatab = _marpaESLIFRecognizer_appendDatab(marpaESLIFRecognizerp, utf8s, utf8l, eofb);
  /* Take care: appendDatab can be < 0 */
  if (! appendDatab) {
    goto err;
  }

  /* For the fast check */
  if (marpaESLIF_streamp->lastFroms != NULL) {
    free(marpaESLIF_streamp->lastFroms);
  }
  marpaESLIF_streamp->lastFroms = malloc(encodingl + 1); /* Add a NUL byte for convenience */
  if (marpaESLIF_streamp->lastFroms == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
    goto err;
  }
  memcpy(marpaESLIF_streamp->lastFroms, encodings, encodingl);
  marpaESLIF_streamp->lastFroms[encodingl] = '\0';
  marpaESLIF_streamp->lastFroml = encodingl;
  
  /* Put global flag to on */
  marpaESLIF_streamp->charconvb = 1;

  rcb = appendDatab;
  goto done;

 err:
  rcb = 0;

 done:
  if (encodingasciis != NULL) {
    free(encodingasciis);
  }
  if (encodingutf8s != NULL) {
    free(encodingutf8s);
  }
  if (utf8s != NULL) {
    free(utf8s);
  }
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
short marpaESLIFValue_value_startb(marpaESLIFValue_t *marpaESLIFValuep, int *startip)
/*****************************************************************************/
{
  if (marpaESLIFValuep == NULL) {
    errno = EINVAL;
    return 0;
  }

  return marpaWrapperValue_value_startb(marpaESLIFValuep->marpaWrapperValuep, startip);
}

/*****************************************************************************/
short marpaESLIFValue_value_lengthb(marpaESLIFValue_t *marpaESLIFValuep, int *lengthip)
/*****************************************************************************/
{
  if (marpaESLIFValuep == NULL) {
    errno = EINVAL;
    return 0;
  }

  return marpaWrapperValue_value_lengthb(marpaESLIFValuep->marpaWrapperValuep, lengthip);
}

/*****************************************************************************/
marpaESLIFGrammar_t *marpaESLIF_grammarp(marpaESLIF_t *marpaESLIFp)
/*****************************************************************************/
{
  if (marpaESLIFp == NULL) {
    errno = EINVAL;
    return NULL;
  }

  return marpaESLIFp->marpaESLIFGrammarp;
}

/*****************************************************************************/
short marpaESLIFGrammar_ngrammarib(marpaESLIFGrammar_t *marpaESLIFGrammarp, int *ngrammarip)
/*****************************************************************************/
{
  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  if (ngrammarip != NULL) {
    *ngrammarip = GENERICSTACK_USED(marpaESLIFGrammarp->grammarStackp);
  }

  return 1;
}

/*****************************************************************************/
short marpaESLIFGrammar_defaultsb(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaESLIFGrammarDefaults_t *marpaESLIFGrammarDefaultsp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t        *grammarp;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = marpaESLIFGrammarp->grammarp;

  if (grammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return marpaESLIFGrammar_defaults_by_levelb(marpaESLIFGrammarp, marpaESLIFGrammarDefaultsp, grammarp->leveli, NULL /* descp */);
}

/*****************************************************************************/
short marpaESLIFGrammar_defaults_by_levelb(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaESLIFGrammarDefaults_t *marpaESLIFGrammarDefaultsp, int leveli, marpaESLIFString_t *descp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t        *grammarp;
  marpaESLIFGrammarDefaults_t  marpaESLIFGrammarDefaults;
  short                        rcb;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    goto err;
  }

  grammarp = _marpaESLIFGrammar_grammar_findp(marpaESLIFGrammarp, leveli, descp);
  if (grammarp == NULL) {
    errno = EINVAL;
    goto err;
  }

  marpaESLIFGrammarDefaults.defaultRuleActionp   = grammarp->defaultRuleActionp;
  marpaESLIFGrammarDefaults.defaultSymbolActionp = grammarp->defaultSymbolActionp;

  if (marpaESLIFGrammarDefaultsp != NULL) {
    *marpaESLIFGrammarDefaultsp = marpaESLIFGrammarDefaults;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIFGrammar_defaults_setb(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaESLIFGrammarDefaults_t *marpaESLIFGrammarDefaultsp)
/*****************************************************************************/
{
  marpaESLIF_grammar_t        *grammarp;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  grammarp = marpaESLIFGrammarp->grammarp;

  if (grammarp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return marpaESLIFGrammar_defaults_by_level_setb(marpaESLIFGrammarp, marpaESLIFGrammarDefaultsp, grammarp->leveli, NULL /* descp */);
}

/*****************************************************************************/
short marpaESLIFGrammar_defaults_by_level_setb(marpaESLIFGrammar_t *marpaESLIFGrammarp, marpaESLIFGrammarDefaults_t *marpaESLIFGrammarDefaultsp, int leveli, marpaESLIFString_t *descp)
/*****************************************************************************/
{
  marpaESLIF_t         *marpaESLIFp;
  marpaESLIF_grammar_t *grammarp;
  marpaESLIF_action_t  *previousActionp;
  short                 rcb;

  if (marpaESLIFGrammarp == NULL) {
    errno = EINVAL;
    goto err;
  }
  marpaESLIFp = marpaESLIFGrammarp->marpaESLIFp;

  grammarp = _marpaESLIFGrammar_grammar_findp(marpaESLIFGrammarp, leveli, descp);
  if (grammarp == NULL) {
    errno = EINVAL;
    goto err;
  }

  if (marpaESLIFGrammarDefaultsp != NULL) {

    if (marpaESLIFGrammarDefaultsp->defaultRuleActionp != NULL) {
      if (! _marpaESLIF_action_validb(marpaESLIFp, marpaESLIFGrammarDefaultsp->defaultRuleActionp)) {
        goto err;
      }
      previousActionp = grammarp->defaultRuleActionp;
      grammarp->defaultRuleActionp = _marpaESLIF_action_clonep(marpaESLIFp, marpaESLIFGrammarDefaultsp->defaultRuleActionp);
      if (grammarp->defaultRuleActionp == NULL) {
        grammarp->defaultRuleActionp = previousActionp;
        goto err;
      }
      _marpaESLIF_action_freev(previousActionp);
    } else {
      _marpaESLIF_action_freev(grammarp->defaultRuleActionp);
      grammarp->defaultRuleActionp = NULL;
    }

    if (marpaESLIFGrammarDefaultsp->defaultSymbolActionp != NULL) {
      if (! _marpaESLIF_action_validb(marpaESLIFp, marpaESLIFGrammarDefaultsp->defaultSymbolActionp)) {
        goto err;
      }
      previousActionp = grammarp->defaultSymbolActionp;
      grammarp->defaultSymbolActionp = _marpaESLIF_action_clonep(marpaESLIFp, marpaESLIFGrammarDefaultsp->defaultSymbolActionp);
      if (grammarp->defaultSymbolActionp == NULL) {
        grammarp->defaultSymbolActionp = previousActionp;
        goto err;
      }
      _marpaESLIF_action_freev(previousActionp);
    } else {
      _marpaESLIF_action_freev(grammarp->defaultSymbolActionp);
      grammarp->defaultSymbolActionp = NULL;
    }
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

#if MARPAESLIF_VALUEERRORPROGRESSREPORT
/*****************************************************************************/
static inline void _marpaESLIFValueErrorProgressReportv(marpaESLIFValue_t *marpaESLIFValuep)
/*****************************************************************************/
{
  static const char      *funcs = "_marpaESLIFValueErrorProgressReportv";
  marpaESLIF_t           *marpaESLIFp                = marpaESLIFValuep->marpaESLIFp;
  marpaESLIFRecognizer_t *marpaESLIFRecognizerp      = marpaESLIFValuep->marpaESLIFRecognizerp;
  int                     starti;
  int                     lengthi;

  /* If we were generating the value of the top level recognizer, modulo discard that is also at same level, log the error */
  if (! marpaESLIFRecognizerp->silentb) {
    if (marpaESLIFValue_value_startb(marpaESLIFValuep, &starti) &&
        marpaESLIFValue_value_lengthb(marpaESLIFValuep, &lengthi)) {
      marpaESLIFRecognizer_progressLogb(marpaESLIFValuep->marpaESLIFRecognizerp,
                                        starti,
                                        /* lengthi is zero when this is a MARPA_STEP_NULLABLE_SYMBOL */
                                        (lengthi > 0) ? starti+lengthi-1 : starti,
                                        GENERICLOGGER_LOGLEVEL_ERROR);
    }
  }
}
#endif

/*****************************************************************************/
static inline marpaESLIF_symbol_t *_marpaESLIF_resolveSymbolp(marpaESLIF_t *marpaESLIFp, genericStack_t *grammarStackp, marpaESLIF_grammar_t *current_grammarp, char *asciis, int lookupLevelDeltai, marpaESLIF_string_t *lookupGrammarStringp, marpaESLIF_grammar_t **grammarpp)
/*****************************************************************************/
{
  static const char     *funcs   = "_marpaESLIF_resolveSymbolp";
  marpaESLIF_symbol_t   *symbolp;
  marpaESLIF_grammar_t  *thisGrammarp;
  marpaESLIF_grammar_t  *grammarp;
  int                    grammari;

  if ((grammarStackp == NULL)
      ||
      (current_grammarp == NULL)
      ||
      (asciis == NULL)) {
    goto err;
  }
  
  grammarp = NULL;
  /* First look for the grammar */
  if (lookupGrammarStringp != NULL) {
    /* Look for such a grammar description */
    for (grammari = 0; grammari < GENERICSTACK_USED(grammarStackp); grammari++) {
      if (! GENERICSTACK_IS_PTR(grammarStackp, grammari)) {
        /* Sparse array */
        continue;
      }
      thisGrammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, grammari);
      if (_marpaESLIF_string_utf8_eqb(thisGrammarp->descp, lookupGrammarStringp)) {
        grammarp = thisGrammarp;
        break;
      }
    }
  } else {
    /* RHS level is relative - if RHS level is 0 the we fall back to current grammar */
    grammari = current_grammarp->leveli + lookupLevelDeltai;
    if ((grammari >= 0) && GENERICSTACK_IS_PTR(grammarStackp, grammari)) {
      grammarp = (marpaESLIF_grammar_t *) GENERICSTACK_GET_PTR(grammarStackp, grammari);
    }
  }

  if (grammarp == NULL) {
    goto err;
  }

  /* Then look into this grammar */
  symbolp = _marpaESLIF_symbol_findp(marpaESLIFp, grammarp, asciis, -1 /* symboli */, NULL /* symbolip */);
  if (symbolp == NULL) {
    goto err;
  }

  if (grammarpp != NULL) {
    *grammarpp = grammarp;
  }
  goto done;

 err:
  symbolp = NULL;

 done:
  return symbolp;
}

/*****************************************************************************/
static inline char *_marpaESLIF_ascii2ids(marpaESLIF_t *marpaESLIFp, char *asciis)
/*****************************************************************************/
{
  /* Produces a C identifier-compatible version of ascii string */
  static const char   *funcs = "_marpaESLIF_ascii2ids";
  char                *rcs   = NULL;
  char                *p;

  if (asciis == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "asciis is NULL");
    goto err;
  }

  rcs = strdup(asciis);
  if (rcs == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "strdup failure, %s", strerror(errno));
    goto err;
  }

  p = rcs;
  while (*p != '\0') {
    if ((*p == '_')                  ||
        ((*p >= 'a') && (*p <= 'z')) ||
        ((*p >= 'A') && (*p <= 'Z')) ||
        ((*p >= '0') && (*p <= '9'))) {
      goto next;
    }
    *p  = '_';
  next:
    ++p;
  }

  goto done;

 err:
  if (rcs != NULL) {
    free(rcs);
    rcs = NULL;
  }

 done:
  return rcs;
}

/*****************************************************************************/
static inline short _marpaESLIFValue_stack_setb(marpaESLIFValue_t *marpaESLIFValuep, int indicei, marpaESLIFValueResult_t *marpaESLIFValueResultp)
/*****************************************************************************/
{
  static const char       *funcs = "_marpaESLIFValue_stack_setb";
  marpaESLIFRecognizer_t  *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  short                    rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "start indicei=%d", indicei);

  /* Validate the input */
  if (! _marpaESLIFRecognizer_value_validb(marpaESLIFRecognizerp, marpaESLIFValueResultp, NULL /* userDatavp */, NULL /* callbackp */)) {
    goto err;
  }
  
  if (! _marpaESLIFValueResult_stack_i_setb(marpaESLIFValuep->marpaESLIFp,
                                            marpaESLIFValuep->valueResultStackp,
                                            indicei,
                                            marpaESLIFValueResultp,
                                            0, /* forgetb */
                                            marpaESLIFValuep->beforePtrStackp,
                                            marpaESLIFValuep->beforePtrHashp,
                                            marpaESLIFValuep->afterPtrHashp,
                                            NULL /* marpaESLIFValueResultOrigp */)) {
    goto err;
  }
  GENERICSTACK_SET_CUSTOMP(marpaESLIFValuep->valueResultStackp, marpaESLIFValueResultp, indicei);
  if (GENERICSTACK_ERROR(marpaESLIFValuep->valueResultStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValuep->valueResultStackp set at indice %d failure, %s", indicei, strerror(errno));
    goto err;
  }

  MARPAESLIF_TRACEF(marpaESLIFValuep->marpaESLIFp, funcs, "Action %s success, result type is %s", marpaESLIFValuep->actions, _marpaESLIF_value_types(marpaESLIFValueResultp->type));
  rcb = 1;
  goto done;

 err:
    rcb = 0;
 done:
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
    MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
    return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFValue_stack_getb(marpaESLIFValue_t *marpaESLIFValuep, int indicei, marpaESLIFValueResult_t *marpaESLIFValueResultp)
/*****************************************************************************/
{
  static const char       *funcs = "_marpaESLIFValue_stack_getb";
  marpaESLIFRecognizer_t  *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  short                    rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "start indicei=%d", indicei);

#ifndef MARPAESLIF_NTRACE
  /* Should never happen */
  if (! GENERICSTACK_IS_CUSTOM(marpaESLIFValuep->valueResultStackp, indicei)) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "No such indice %d in value result stack", indicei);
    goto err;
  }
#endif
  *marpaESLIFValueResultp = GENERICSTACK_GET_CUSTOM(marpaESLIFValuep->valueResultStackp, indicei);

  rcb = 1;
#ifndef MARPAESLIF_NTRACE
  /* Remove valid warnings for unused label err -; */
  goto done;

 err:
    rcb = 0;
 done:
#endif
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
    MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
    return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFValue_stack_getAndForgetb(marpaESLIFValue_t *marpaESLIFValuep, int indicei, marpaESLIFValueResult_t *marpaESLIFValueResultp)
/*****************************************************************************/
{
  static const char       *funcs = "_marpaESLIFValue_stack_getAndForgetb";
  marpaESLIFRecognizer_t  *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  short                    rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "start indicei=%d", indicei);

  if (! _marpaESLIFValue_stack_getb(marpaESLIFValuep, indicei, marpaESLIFValueResultp)) {
    goto err;
  }
  if (! _marpaESLIFValue_stack_forgetb(marpaESLIFValuep, indicei)) {
    goto err;
  }

  rcb = 1;
  goto done;

 err:
    rcb = 0;
 done:
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
    MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
    return rcb;
}

/*****************************************************************************/
marpaESLIFValueResult_t *marpaESLIFValue_stack_getp(marpaESLIFValue_t *marpaESLIFValuep, int indicei)
/*****************************************************************************/
{
  static const char *funcs  = "marpaESLIFValue_stack_getp";

  if (marpaESLIFValuep == NULL) {
    errno = EINVAL;
    return 0;
  }
  if (indicei < 0) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Indice %d is negative", indicei);
    return 0;
  }

  if (! marpaESLIFValuep->inValuationb) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "%s must be called only in an action callback", funcs);
    return 0;
  }

  return _marpaESLIFValue_stack_getp(marpaESLIFValuep, indicei);
}

/*****************************************************************************/
static inline short _marpaESLIFValue_eslif2hostb(marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, marpaESLIFValueResult_t *marpaESLIFValueResultResolvedp, void *forcedUserDatavp, marpaESLIFValueResultImport_t forcedImporterp)
/*****************************************************************************/
{
  static const char                *funcs                 = "_marpaESLIFValueResult_eslif2hostb";
  marpaESLIFRecognizer_t           *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  marpaESLIFValueOption_t           marpaESLIFValueOption = marpaESLIFValuep->marpaESLIFValueOption;
  /* userDatavp is forced to forcedUserDatavp if forcedImporterp is set */
  void                             *userDatavp            = (forcedImporterp != NULL) ? forcedUserDatavp : marpaESLIFValueOption.userDatavp;
  marpaESLIFValueResultImport_t     importerp             = (forcedImporterp != NULL) ? forcedImporterp : marpaESLIFValueOption.importerp;
  genericStack_t                    marpaESLIFValueResultStack;
  genericStack_t                   *marpaESLIFValueResultStackp = &(marpaESLIFValueResultStack);
  short                             rcb;
  size_t                            i;
  size_t                            j;
  marpaESLIFValueResult_t           marpaESLIFValueResult;
  short                             lazyb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  GENERICSTACK_INIT(marpaESLIFValueResultStackp);
  if (GENERICSTACK_ERROR(marpaESLIFValueResultStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValueResultStackp initialization failure, %s", strerror(errno));
    marpaESLIFValueResultStackp = NULL;
    goto err;
  }

  if (importerp == NULL) {
    /* End user do not mind about the final value */
    rcb = 1;
    goto done;
  }

  GENERICSTACK_PUSH_CUSTOMP(marpaESLIFValueResultStackp, marpaESLIFValueResultp);
  if (GENERICSTACK_ERROR(marpaESLIFValueResultStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValueResultStackp push failure, %s", strerror(errno));
    goto err;
  }

  while (GENERICSTACK_USED(marpaESLIFValueResultStackp) > 0) {
    marpaESLIFValueResult = GENERICSTACK_POP_CUSTOM(marpaESLIFValueResultStackp);
    if (marpaESLIFValueResult.type == MARPAESLIF_VALUE_TYPE_LAZY) {
      /* Next item is a marpaESLIFValueResult that got lazy'ed */
      if (GENERICSTACK_USED(marpaESLIFValueResultStackp) <= 0) {
        MARPAESLIF_ERROR(marpaESLIFValuep->marpaESLIFp, "Internal error: lazy marker but nothing else in the work stack");
        errno = ENOENT;
        goto err;
      }
      marpaESLIFValueResult = GENERICSTACK_POP_CUSTOM(marpaESLIFValueResultStackp);
      lazyb = 1;
    } else {
      lazyb = 0;
    }

    if (marpaESLIFValueResultResolvedp != NULL) {
      *marpaESLIFValueResultResolvedp = marpaESLIFValueResult;
    }

    switch (marpaESLIFValueResult.type) {
    case MARPAESLIF_VALUE_TYPE_UNDEF:
    case MARPAESLIF_VALUE_TYPE_CHAR:
    case MARPAESLIF_VALUE_TYPE_SHORT:
    case MARPAESLIF_VALUE_TYPE_INT:
    case MARPAESLIF_VALUE_TYPE_LONG:
    case MARPAESLIF_VALUE_TYPE_FLOAT:
    case MARPAESLIF_VALUE_TYPE_DOUBLE:
    case MARPAESLIF_VALUE_TYPE_PTR:
    case MARPAESLIF_VALUE_TYPE_BOOL:
    case MARPAESLIF_VALUE_TYPE_STRING:
    case MARPAESLIF_VALUE_TYPE_LONG_DOUBLE:
#ifdef MARPAESLIF_HAVE_LONG_LONG
    case MARPAESLIF_VALUE_TYPE_LONG_LONG:
#endif
    case MARPAESLIF_VALUE_TYPE_ARRAY:
      if (! importerp(marpaESLIFValuep, userDatavp, &marpaESLIFValueResult)) {
        goto err;
      }
      break;
    case MARPAESLIF_VALUE_TYPE_ROW:
      if (lazyb) {
        if (! importerp(marpaESLIFValuep, userDatavp, &marpaESLIFValueResult)) {
          goto err;
        }
      } else {
        /* Push again current element */
        GENERICSTACK_PUSH_CUSTOM(marpaESLIFValueResultStackp, marpaESLIFValueResult);
        if (GENERICSTACK_ERROR(marpaESLIFValueResultStackp)) {
          MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValueResultStackp push failure, %s", strerror(errno));
          goto err;
        }
        /* Push lazy marker */
        GENERICSTACK_PUSH_CUSTOM(marpaESLIFValueResultStackp, marpaESLIFValueResultLazy);
        if (GENERICSTACK_ERROR(marpaESLIFValueResultStackp)) {
          MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValueResultStackp push failure, %s", strerror(errno));
          goto err;
        }
        /* Push inner elements in reverse order i.e. 3, 2, 1 so that inner elements are imported in order, i.e. 1, 2, 3 */
        if (marpaESLIFValueResult.u.r.sizel > 0) {
          for (i = 0, j = marpaESLIFValueResult.u.r.sizel - 1; i < marpaESLIFValueResult.u.r.sizel; i++, j--) {
            GENERICSTACK_PUSH_CUSTOM(marpaESLIFValueResultStackp, marpaESLIFValueResult.u.r.p[j]);
            if (GENERICSTACK_ERROR(marpaESLIFValueResultStackp)) {
              MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValueResultStackp push failure, %s", strerror(errno));
              goto err;
            }
          }
        }
      }
      break;
    case MARPAESLIF_VALUE_TYPE_TABLE:
      if (lazyb) {
        if (! importerp(marpaESLIFValuep, userDatavp, &marpaESLIFValueResult)) {
          goto err;
        }
      } else {
        /* Push again current element */
        GENERICSTACK_PUSH_CUSTOM(marpaESLIFValueResultStackp, marpaESLIFValueResult);
        if (GENERICSTACK_ERROR(marpaESLIFValueResultStackp)) {
          MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValueResultStackp push failure, %s", strerror(errno));
          goto err;
        }
        /* Push lazy marker */
        GENERICSTACK_PUSH_CUSTOM(marpaESLIFValueResultStackp, marpaESLIFValueResultLazy);
        if (GENERICSTACK_ERROR(marpaESLIFValueResultStackp)) {
          MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValueResultStackp push failure, %s", strerror(errno));
          goto err;
        }
        /* We push valn, keyn, ...., val0, key0, so that importer is called in this order: */
        /* key0, val0, ..., keyn, valn */
        if (marpaESLIFValueResult.u.t.sizel > 0) {
          for (i = 0, j = marpaESLIFValueResult.u.t.sizel - 1; i < marpaESLIFValueResult.u.t.sizel; i++, j--) {
            GENERICSTACK_PUSH_CUSTOM(marpaESLIFValueResultStackp, marpaESLIFValueResult.u.t.p[j].value);
            if (GENERICSTACK_ERROR(marpaESLIFValueResultStackp)) {
              MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValueResultStackp push failure, %s", strerror(errno));
              goto err;
            }
            GENERICSTACK_PUSH_CUSTOM(marpaESLIFValueResultStackp, marpaESLIFValueResult.u.t.p[j].key);
            if (GENERICSTACK_ERROR(marpaESLIFValueResultStackp)) {
              MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValueResultStackp push failure, %s", strerror(errno));
              goto err;
            }
          }
        }
      }
      break;
    default:
      errno = ENOSYS;
      goto err;
    }
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  GENERICSTACK_RESET(marpaESLIFValueResultStackp);

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
short marpaESLIFValue_importb(marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, marpaESLIFValueResult_t *marpaESLIFValueResultResolvedp)
/*****************************************************************************/
{
  static const char *funcs = "marpaESLIFValue_importb";

  /* Generic importation helper of a marpaESLIFValueResult during valuation */
  if (marpaESLIFValuep == NULL) {
    errno = EINVAL;
    return 0;
  }

  return _marpaESLIFValue_eslif2hostb(marpaESLIFValuep, marpaESLIFValueResultp, marpaESLIFValueResultResolvedp, NULL /* forcedUserDatavp */, NULL /* forcedImporterp */);
}

/*****************************************************************************/
static inline marpaESLIFValueResult_t *_marpaESLIFValue_stack_getp(marpaESLIFValue_t *marpaESLIFValuep, int indicei)
/*****************************************************************************/
{
  /* Special internal version of _marpaESLIFValue_stack_getp that returns the direct pointer into the stack */
  static const char       *funcs                 = "_marpaESLIFValue_stack_getp";
  marpaESLIFRecognizer_t  *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  marpaESLIFValueResult_t *marpaESLIFValueResultp;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "start indicei=%d", indicei);

#ifndef MARPAESLIF_NTRACE
  /* Should never happen */
  if (! GENERICSTACK_IS_CUSTOM(marpaESLIFValuep->valueResultStackp, indicei)) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "No such indice %d in value result stack", indicei);
    goto err;
  }
#endif
  marpaESLIFValueResultp = GENERICSTACK_GET_CUSTOMP(marpaESLIFValuep->valueResultStackp, indicei);

#ifndef MARPAESLIF_NTRACE
  /* Remove valid warnings for unused label err -; */
  goto done;

 err:
  marpaESLIFValueResultp = NULL;

 done:
#endif
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %p", marpaESLIFValueResultp);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return marpaESLIFValueResultp;
}

/*****************************************************************************/
short marpaESLIFValue_stack_forgetb(marpaESLIFValue_t *marpaESLIFValuep, int indicei)
/*****************************************************************************/
{
  static const char *funcs  = "marpaESLIFValue_stack_forgetb";

  if (marpaESLIFValuep == NULL) {
    errno = EINVAL;
    return 0;
  }
  if (indicei < 0) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Indice %d is negative", indicei);
    return 0;
  }

  if (! marpaESLIFValuep->inValuationb) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "%s must be called only in an action callback", funcs);
    return 0;
  }

  return _marpaESLIFValue_stack_forgetb(marpaESLIFValuep, indicei);
}

/*****************************************************************************/
static inline short _marpaESLIFValue_stack_forgetb(marpaESLIFValue_t *marpaESLIFValuep, int indicei)
/*****************************************************************************/
{
  static const char       *funcs = "_marpaESLIFValue_stack_forgetb";
  marpaESLIFRecognizer_t  *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  short                    rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (! _marpaESLIFValueResult_stack_i_setb(marpaESLIFValuep->marpaESLIFp,
                                            marpaESLIFValuep->valueResultStackp,
                                            indicei,
                                            NULL, /* marpaESLIFValueResultp */
                                            1, /* forgetb */
                                            marpaESLIFValuep->beforePtrStackp,
                                            marpaESLIFValuep->beforePtrHashp,
                                            marpaESLIFValuep->afterPtrHashp,
                                            NULL /* marpaESLIFValueResultOrigp */)) {
    goto err;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;
 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFValueResult_stack_i_setb(marpaESLIF_t *marpaESLIFp, genericStack_t *valueResultStackp, int indicei, marpaESLIFValueResult_t *marpaESLIFValueResultp, short forgetb, genericStack_t *beforePtrStackp, genericHash_t *beforePtrHashp, genericHash_t *afterPtrHashp, marpaESLIFValueResult_t *marpaESLIFValueResultOrigp)
/*****************************************************************************/
/* If marpaESLIFValueResultOrigp then valueResultStackp and indicei must be set */
/*****************************************************************************/
{
  static const char        *funcs    = "_marpaESLIFValueResult_stack_i_setb";
  short                     restoreb = 0; /* If true, then marpaESLIFValueResultOrig can be used to restore original value */
  marpaESLIFValueResult_t   marpaESLIFValueResultOrig;
  marpaESLIFValueResult_t  *marpaESLIFValueResultNewp;
  short                     rcb;
  short                     findResultb;
  void                     *p;
  marpaESLIFFreeCallback_t  freeCallbackp;
  void                     *freeUserDatavp;
  int                       hashindexi;

  if (marpaESLIFValueResultOrigp == NULL) {
    /* Look at original value */
  /* ---------------------- */
    if (indicei >= GENERICSTACK_USED(valueResultStackp)) {
      /* Replacement on something that does not yet exist - this is not illegal, we set UNDEF */
      GENERICSTACK_SET_CUSTOM(valueResultStackp, marpaESLIFValueResultUndef, indicei);
      if (GENERICSTACK_ERROR(valueResultStackp)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "valueResultStackp set failure at indice %d, %s", indicei, strerror(errno));
        goto err;
      }
    } else if (! GENERICSTACK_IS_CUSTOM(valueResultStackp, indicei)) {
      /* Then it is must be NA */
      if (GENERICSTACK_IS_NA(valueResultStackp, indicei)) {
        /* Replacement on something that is NA - this is not illegal, we set UNDEF */
        GENERICSTACK_SET_CUSTOM(valueResultStackp, marpaESLIFValueResultUndef, indicei);
        if (GENERICSTACK_ERROR(valueResultStackp)) {
          MARPAESLIF_ERRORF(marpaESLIFp, "valueResultStackp set failure at indice %d, %s", indicei, strerror(errno));
          goto err;
        }
      } else {
        /* At indicei, this is must be a CUSTOM or a NA value */
        MARPAESLIF_ERRORF(marpaESLIFp, "valueResultStackp at indice %d is not CUSTOM nor NA (got %s, value %d)", indicei, _marpaESLIF_genericStack_i_types(valueResultStackp, indicei), GENERICSTACKITEMTYPE(valueResultStackp, indicei));
        goto err;
      }
    }

    /* Get original value */
    /* ------------------ */
    marpaESLIFValueResultOrigp = GENERICSTACK_GET_CUSTOMP(valueResultStackp, indicei);
    if (GENERICSTACK_ERROR(valueResultStackp)) {
      MARPAESLIF_ERRORF(marpaESLIFp, "GENERICSTACK_GET_CUSTOMP on valueResultStackp failure, %s", strerror(errno));
      goto err;
    }
  }

  /* Get replacement value */
  /* --------------------- */
  if (marpaESLIFValueResultp == NULL) {
    marpaESLIFValueResultp = (marpaESLIFValueResult_t *) &marpaESLIFValueResultUndef;
  }

  /* -------------------------------------------------------------------------------------------------- */
  /* Here it is guaranteed that both marpaESLIFValueResultOrigp and marpaESLIFValueResultp are not NULL */
  /* -------------------------------------------------------------------------------------------------- */

  /* Keep a copy of what we are going to replace */
  marpaESLIFValueResultOrig = *marpaESLIFValueResultOrigp;
  restoreb = 1;

  /* Do the replacement */
  *marpaESLIFValueResultOrigp = *marpaESLIFValueResultp;

  if (forgetb) {
    /* The caller said to definitely forget about it : we do nothing */
    rcb = 1;
    goto done;
  }

  /* ------------------ */
  /* Prepare work areas */
  /* ------------------ */
  GENERICSTACK_RESET(beforePtrStackp);
  if (GENERICSTACK_ERROR(beforePtrStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "beforePtrStackp reset failure, %s", strerror(errno));
    goto err;
  }

  GENERICHASH_RELAX(beforePtrHashp, NULL /* userDatavp */);
  if (GENERICHASH_ERROR(beforePtrHashp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "beforePtrHashp relax failure, %s", strerror(errno));
    goto err;
  }

  GENERICHASH_RELAX(afterPtrHashp, NULL /* userDatavp */);
  if (GENERICHASH_ERROR(afterPtrHashp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "afterPtrHashp relax failure, %s", strerror(errno));
    goto err;
  }

  /* Flatten view of all original pointers */
  if (! _marpaESLIF_flatten_pointers(marpaESLIFp, beforePtrStackp, beforePtrHashp, &marpaESLIFValueResultOrig, 1 /* noShallowb */)) {
    goto err;
  }

  /* Flatten view of all replacement pointers */
  if (! _marpaESLIF_flatten_pointers(marpaESLIFp, NULL /* afterPtrStackp */, afterPtrHashp, marpaESLIFValueResultp, 0 /* noShallowb */)) {
    goto err;
  }

  /* Loop on all original pointers and free them if they are not shallowed and if they do not exist in replacement pointers */
  while (GENERICSTACK_USED(beforePtrStackp) > 0) {
    p = GENERICSTACK_POP_PTR(beforePtrStackp);
    findResultb = 0;
    hashindexi = _marpaESLIF_ptrhashi(NULL /* userDatavp */, GENERICSTACKITEMTYPE_PTR, (void **) &p);
    GENERICHASH_FIND_BY_IND(beforePtrHashp,
                            NULL, /* userDatavp */
                            PTR,
                            p,
                            PTR,
                            &marpaESLIFValueResultOrigp,
                            findResultb,
                            hashindexi);
    if (GENERICHASH_ERROR(beforePtrHashp)) {
      MARPAESLIF_ERRORF(marpaESLIFp, "beforePtrHashp find failure, %s", strerror(errno));
      goto err;
    }
    if (! findResultb) {
      /* This should never happen */
      MARPAESLIF_WARNF(marpaESLIFp, "%p not found in beforePtrHashp", p);
      continue;
    }

    findResultb = 0;
    GENERICHASH_FIND_BY_IND(afterPtrHashp,
                            NULL, /* userDatavp */
                            PTR,
                            p,
                            PTR,
                            &marpaESLIFValueResultNewp,
                            findResultb,
                            hashindexi);
    if (GENERICHASH_ERROR(afterPtrHashp)) {
      MARPAESLIF_ERRORF(marpaESLIFp, "afterPtrHashp find failure, %s", strerror(errno));
      goto err;
    }
    if (findResultb) {
      /* Pointer in original also exist in replacement */
      continue;
    }

    /* We can free the original pointer */
    switch (marpaESLIFValueResultOrigp->type) {
    case MARPAESLIF_VALUE_TYPE_PTR:
      freeCallbackp  = marpaESLIFValueResultOrigp->u.p.freeCallbackp;
      freeUserDatavp = marpaESLIFValueResultOrigp->u.p.freeUserDatavp;
      break;
    case MARPAESLIF_VALUE_TYPE_ARRAY:
      freeCallbackp  = marpaESLIFValueResultOrigp->u.a.freeCallbackp;
      freeUserDatavp = marpaESLIFValueResultOrigp->u.a.freeUserDatavp;
      break;
    case MARPAESLIF_VALUE_TYPE_STRING:
      freeCallbackp  = marpaESLIFValueResultOrigp->u.s.freeCallbackp;
      freeUserDatavp = marpaESLIFValueResultOrigp->u.s.freeUserDatavp;
      break;
    case MARPAESLIF_VALUE_TYPE_ROW:
      freeCallbackp  = marpaESLIFValueResultOrigp->u.r.freeCallbackp;
      freeUserDatavp = marpaESLIFValueResultOrigp->u.r.freeUserDatavp;
      break;
    case MARPAESLIF_VALUE_TYPE_TABLE:
      freeCallbackp  = marpaESLIFValueResultOrigp->u.t.freeCallbackp;
      freeUserDatavp = marpaESLIFValueResultOrigp->u.t.freeUserDatavp;
      break;
    default:
      MARPAESLIF_ERRORF(marpaESLIFp, "Invalid case %d", marpaESLIFValueResultOrigp->type);
      goto err;
    }

    /* marpaESLIF made sure that freeCallbackp is always set, no need to check */
    freeCallbackp(freeUserDatavp, marpaESLIFValueResultOrigp);
  }

  rcb = 1;
  goto done;

 err:
  if (restoreb) {
    /* We can restore what was at indice indicei */
    *marpaESLIFValueResultOrigp = marpaESLIFValueResultOrig;
  }
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIFValue_contextb(marpaESLIFValue_t *marpaESLIFValuep, char **symbolsp, int *symbolip, char **rulesp, int *ruleip)
/*****************************************************************************/
{
  static const char *funcs  = "marpaESLIFValue_contextb";
  short              rcb;
  char              *symbols;
  int                symboli;
  char              *rules;
  int                rulei;

  if (marpaESLIFValuep == NULL) {
    errno = EINVAL;
    goto err;
  }

  if (! marpaESLIFValuep->inValuationb) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "%s must be called only in an action callback", funcs);
    goto err;
  }

  if (marpaESLIFValuep->symbolp != NULL) {
    symbols = marpaESLIFValuep->symbolp->descp->asciis;
    symboli = marpaESLIFValuep->symbolp->idi;
    rules = NULL;
    rulei = -1;
  } else if (marpaESLIFValuep->rulep != NULL) {
    symbols = NULL;
    symboli = -1;
    rules   = marpaESLIFValuep->rulep->descp->asciis;
    rulei   = marpaESLIFValuep->rulep->idi;
  } else {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "%s found no symbol nor rule in current context", funcs);
    goto err;
  }

  if (symbolsp != NULL) {
    *symbolsp = symbols;
  }
  if (symbolip != NULL) {
    *symbolip = symboli;
  }
  if (rulesp != NULL) {
    *rulesp = rules;
  }
  if (ruleip != NULL) {
    *ruleip = rulei;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIF_lexeme_transferb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti)
/*****************************************************************************/
/* This method is used ONLY in internal lexeme mode: everything is a MARPAESLIF_VALUE_TYPE_ARRAY where u.a.p is in reality an offset */
/*****************************************************************************/
{
  return _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, marpaESLIFValueResultp);
}

/*****************************************************************************/
static inline short _marpaESLIF_generic_literal_transferb(marpaESLIFValue_t *marpaESLIFValuep, marpaESLIF_string_t *stringp, int resulti)
/*****************************************************************************/
/* We are transfering a string that is in the grammar. So no allocatation.   */
/*****************************************************************************/
{
  static const char       *funcs = "_marpaESLIF_generic_literal_transferb";
  marpaESLIFValueResult_t  marpaESLIFValueResult;

  marpaESLIFValueResult.type               = MARPAESLIF_VALUE_TYPE_STRING;
  marpaESLIFValueResult.contextp           = NULL;
  marpaESLIFValueResult.representationp    = NULL;
  marpaESLIFValueResult.u.s.p              = (unsigned char *) stringp->bytep;
  marpaESLIFValueResult.u.s.sizel          = stringp->bytel;
  marpaESLIFValueResult.u.s.freeUserDatavp = NULL;
  marpaESLIFValueResult.u.s.freeCallbackp  = NULL;
  marpaESLIFValueResult.u.s.shallowb       = 1;
  marpaESLIFValueResult.u.s.encodingasciis = stringp->encodingasciis;
  
  return _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, &marpaESLIFValueResult);
}

/*****************************************************************************/
static short _marpaESLIF_symbol_literal_transferb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultNotUsedp, int resulti)
/*****************************************************************************/
{
  return _marpaESLIF_generic_literal_transferb(marpaESLIFValuep, marpaESLIFValuep->stringp, resulti);
}

/*****************************************************************************/
static short _marpaESLIF_rule_literal_transferb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb)
/*****************************************************************************/
{
  return _marpaESLIF_generic_literal_transferb(marpaESLIFValuep, marpaESLIFValuep->stringp, resulti);
}

/*****************************************************************************/
static short _marpaESLIF_lexeme_concatb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb)
/*****************************************************************************/
{
  static const char       *funcs                 = "_marpaESLIF_lexeme_concatb";
  /* It is very important to NOT depend of userDatavp here. This action bypasses any user action. Only marpaESLIFValuep is a known value */
  /* In addition, this method is ONLY called by sub-recognizers, i.e. user-defined data is =========> IMPOSSIBLE <============ at this stage. */
  /* It is always something that come from the physical input. */
  /* This method must ALWAYS return an array. Even if it is "invalid" */
  /* This method acceprt ONLY ARRAYS in the stack */
  /* How comes that this will not crash then... ? This is because at the end everything is a regular expression. */
  /* and if you look to regular expression implementation _marpaESLIFRecognizer_terminal_matcherb() you will see that it is ok with */
  /* an input of zero size: this will return MARPAESLIF_MATCH_FAILURE or MARPAESLIF_MATCH_AGAIN immediately. */
  /* i.e. the parent recognizer can never have a MARPAESLIF_MATCH_OK, so it can never recuperate a fake array of zero size. */

  marpaESLIFRecognizer_t  *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  short                    shallowb              = (marpaESLIFRecognizerp->parentRecognizerp != NULL) ? 1 : 0;
  void                    *p;
  size_t                   l;
  short                    rcb;
  char                    *tmpp;
  char                    *thisp;
  size_t                   thisl;
  int                      i;
  marpaESLIFValueResult_t *marpaESLIFValueResultp;
  marpaESLIFValueResult_t  marpaESLIFValueResult;

  /* Please note that this method is NOT subject on user's alternative data: it is called only by internal */
  /* recognizers, so it is guaranteed that no entry in the lexeme stack can be the tuple {ptr,0}. */
  
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "start [%d] = [%d-%d]", resulti, arg0i, argni);

  if (nullableb) {
    /* A very special internal case: in practice it is forbidden to create an array with a NULL pointer or
       a zero size. But here we are in the internal recognizers. Which at the end always finish into a
       regex match for its parent recognizer. And if you look into _marpaESLIFRecognizer_terminal_matcherb()
       you will see that it is testing the source pointer and sizes, and will NEVER return a successful
       match in this case. At worse it will say it failed, at best it will say it need more data -;
    */
    p = NULL;
    l = 0;
  } else {
    /* IF arg0i == argni == resulti, then this is a no-op (for example: LHS ~ lexeme) - guaranteed to be an ARRAY */
    if ((arg0i == argni) && (arg0i == resulti)) {
      MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "No-op");
      rcb = 1;
      goto done;
    }

    /* Compute the total size */
    p = NULL;
    l = 0;
    for (i = arg0i; i <= argni; i++) {
      marpaESLIFValueResultp = _marpaESLIFValue_stack_getp(marpaESLIFValuep, i);
      if (marpaESLIFValueResultp == NULL) {
        goto err;
      }
      if (marpaESLIFValueResultp->type == MARPAESLIF_VALUE_TYPE_ARRAY) {
        thisp = marpaESLIFValueResultp->u.a.p;
        thisl = marpaESLIFValueResultp->u.a.sizel;
        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Got lexeme to concat: {%p,%ld}", thisp, (unsigned long) thisl);
        if (((thisp != NULL) || shallowb) && (thisl > 0)) {
          l += thisl;
        }
      } else {
        MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValueResultp->type is not ARRAY (got %d, %s)", marpaESLIFValueResultp->type, _marpaESLIF_value_types(marpaESLIFValueResultp->type));
        goto err;
      }
    }
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Total size to concat: %ld", (unsigned long) l);
    if (l > 0) {
      if (shallowb) {
        /* Internal lexeme mode: we play with offsets. Only the first lexeme has the correct offset. */
        marpaESLIFValueResultp = _marpaESLIFValue_stack_getp(marpaESLIFValuep, arg0i);
        p = marpaESLIFValueResultp->u.a.p;
      } else {
        /* Allocate enough space */
        p = malloc(l + 1); /* We ALWAYS add a NUL byte for convenience */
        if (p == NULL) {
          MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "malloc failure, %s", strerror(errno));
          goto err;
        }
        /* Fill that */
        tmpp = (char *) p;
        for (i = arg0i; i <= argni; i++) {
          /* Same version as upper, without the check */
          marpaESLIFValueResultp = _marpaESLIFValue_stack_getp(marpaESLIFValuep, i);
          thisp = marpaESLIFValueResultp->u.a.p;
          thisl = marpaESLIFValueResultp->u.a.sizel;
          memcpy((void *) tmpp, (void *) thisp, thisl);
          tmpp += thisl;
        }
        /* Just to be nice with debuggers */
        ((char *) p)[l] = '\0';
      }
    }
  }

  /* We have a parent recognizer per construction. This mean that user's alternatives are IMPOSSIBLE at this level. */
  /* This is also why we can inject something that is impossible: an array of zero size. */
  marpaESLIFValueResult.type               = MARPAESLIF_VALUE_TYPE_ARRAY;
  marpaESLIFValueResult.contextp           = NULL;
  marpaESLIFValueResult.representationp    = NULL;
  marpaESLIFValueResult.u.a.shallowb       = shallowb;
  marpaESLIFValueResult.u.a.sizel          = l; /* Can be 0! */
  marpaESLIFValueResult.u.a.p              = p; /* Can be NULL! */
  marpaESLIFValueResult.u.a.freeUserDatavp = marpaESLIFRecognizerp;
  marpaESLIFValueResult.u.a.freeCallbackp  = _marpaESLIF_generic_freeCallbackv;
  if (! _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, &marpaESLIFValueResult)) {
    goto err;
  }

  rcb = 1;
  goto done;

 err:
  if (p != NULL) {
    free(p);
  }
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static void _marpaESLIF_lexeme_freeCallbackv(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp)
/*****************************************************************************/
{
  _marpaESLIF_generic_freeCallbackv((void *) marpaESLIFValuep->marpaESLIFRecognizerp, marpaESLIFValueResultp);
}

/*****************************************************************************/
static void _marpaESLIF_rule_freeCallbackv(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp)
/*****************************************************************************/
{
  _marpaESLIF_generic_freeCallbackv((void *) marpaESLIFValuep->marpaESLIFRecognizerp, marpaESLIFValueResultp);
}
 
/*****************************************************************************/
static void _marpaESLIF_generic_freeCallbackv(void *userDatavp, marpaESLIFValueResult_t *marpaESLIFValueResultp)
/*****************************************************************************/
{
  static const char      *funcs                 = "_marpaESLIF_generic_freeCallbackv";
  marpaESLIFRecognizer_t *marpaESLIFRecognizerp = (marpaESLIFRecognizer_t *) userDatavp;
  marpaESLIF_string_t     string;
  
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  switch (marpaESLIFValueResultp->type) {
  case MARPAESLIF_VALUE_TYPE_PTR:
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Freeing PTR {%p}", marpaESLIFValueResultp->u.p.p);
    free(marpaESLIFValueResultp->u.p.p);
    break;
  case MARPAESLIF_VALUE_TYPE_ARRAY:
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Freeing ARRAY {%p,%ld}", marpaESLIFValueResultp->u.a.p, (unsigned long) marpaESLIFValueResultp->u.a.sizel);
    free(marpaESLIFValueResultp->u.a.p);
    break;
  case MARPAESLIF_VALUE_TYPE_STRING:
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Freeing STRING {%p,%ld,encoding=%s}", marpaESLIFValueResultp->u.s.p, (unsigned long) marpaESLIFValueResultp->u.s.sizel, marpaESLIFValueResultp->u.s.encodingasciis != NULL ? marpaESLIFValueResultp->u.s.encodingasciis : "(null)");
    string.bytep          = (char *) marpaESLIFValueResultp->u.s.p;
    string.bytel          = marpaESLIFValueResultp->u.s.sizel;
    string.encodingasciis = marpaESLIFValueResultp->u.s.encodingasciis;
    string.asciis         = NULL;
    _marpaESLIF_string_freev(&string, 1 /* onStackb */);
    break;
  case MARPAESLIF_VALUE_TYPE_ROW:
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Freeing ROW {%p,%ld}", marpaESLIFValueResultp->u.r.p, (unsigned long) marpaESLIFValueResultp->u.r.sizel);
    free(marpaESLIFValueResultp->u.r.p);
    break;
  case MARPAESLIF_VALUE_TYPE_TABLE:
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Freeing TABLE {%p,%ld}", marpaESLIFValueResultp->u.t.p, (unsigned long) marpaESLIFValueResultp->u.t.sizel);
    free(marpaESLIFValueResultp->u.t.p);
    break;
  default:
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Skipping type %d %s", marpaESLIFValueResultp->type, _marpaESLIF_value_types(marpaESLIFValueResultp->type));
    break;
  }

  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "return");
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
}

/*****************************************************************************/
static inline marpaESLIFValue_t *_marpaESLIFValue_newp(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIFValueOption_t *marpaESLIFValueOptionp, short silentb, short fakeb)
/*****************************************************************************/
{
  static const char        *funcs                 = "marpaESLIFValue_newp";
  marpaESLIF_t             *marpaESLIFp           = marpaESLIFRecognizerp->marpaESLIFp;
  marpaESLIFValue_t        *marpaESLIFValuep      = NULL;
  marpaWrapperValue_t      *marpaWrapperValuep    = NULL;
  marpaWrapperValueOption_t marpaWrapperValueOption;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  /* MARPAESLIF_TRACE(marpaESLIFp, funcs, "Building Value"); */

  if (marpaESLIFValueOptionp == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Value option structure must not be NULL");
    goto err;
  }

  marpaESLIFValuep = (marpaESLIFValue_t *) malloc(sizeof(marpaESLIFValue_t));
  if (marpaESLIFValuep == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
    goto err;
  }

  marpaESLIFValuep->marpaESLIFp                 = marpaESLIFp;
  marpaESLIFValuep->marpaESLIFRecognizerp       = marpaESLIFRecognizerp;
  marpaESLIFValuep->marpaESLIFValueOption       = *marpaESLIFValueOptionp;
  marpaESLIFValuep->marpaWrapperValuep          = NULL;
  marpaESLIFValuep->previousPassWasPassthroughb = 0;
  marpaESLIFValuep->previousArg0i               = 0;
  marpaESLIFValuep->previousArgni               = 0;
  marpaESLIFValuep->valueResultStackp           = NULL;
  marpaESLIFValuep->inValuationb                = 0;
  marpaESLIFValuep->symbolp                     = NULL;
  marpaESLIFValuep->rulep                       = NULL;
  marpaESLIFValuep->actions                     = NULL;
  marpaESLIFValuep->stringp                     = NULL;
  marpaESLIFValuep->L                           = NULL;
  marpaESLIFValuep->marpaESLIFLuaValueContextp  = NULL; /* Shallow pointer */
  marpaESLIFValuep->beforePtrStackp             = NULL;
  marpaESLIFValuep->beforePtrHashp              = NULL;
  marpaESLIFValuep->afterPtrHashp               = NULL;

  if (! fakeb) {
    marpaWrapperValueOption.genericLoggerp = silentb ? marpaESLIFp->traceLoggerp : marpaESLIFp->marpaESLIFOption.genericLoggerp;
    marpaWrapperValueOption.highRankOnlyb  = marpaESLIFValueOptionp->highRankOnlyb;
    marpaWrapperValueOption.orderByRankb   = marpaESLIFValueOptionp->orderByRankb;
    marpaWrapperValueOption.ambiguousb     = marpaESLIFValueOptionp->ambiguousb;
    marpaWrapperValueOption.nullb          = marpaESLIFValueOptionp->nullb;
    marpaWrapperValueOption.maxParsesi     = marpaESLIFValueOptionp->maxParsesi;
    marpaWrapperValuep = marpaWrapperValue_newp(marpaESLIFRecognizerp->marpaWrapperRecognizerp, &marpaWrapperValueOption);
    if (marpaWrapperValuep == NULL) {
      goto err;
    }
    marpaESLIFValuep->marpaWrapperValuep = marpaWrapperValuep;
  }

  marpaESLIFValuep->beforePtrStackp = &(marpaESLIFValuep->_beforePtrStack);
  GENERICSTACK_INIT(marpaESLIFValuep->beforePtrStackp);
  if (GENERICSTACK_ERROR(marpaESLIFValuep->beforePtrStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValuep->beforePtrStackp initialization failure, %s", strerror(errno));
    marpaESLIFValuep->beforePtrStackp = NULL;
    goto err;
  }

  marpaESLIFValuep->beforePtrHashp = &(marpaESLIFValuep->_beforePtrHash);
  GENERICHASH_INIT_ALL(marpaESLIFValuep->beforePtrHashp,
                       _marpaESLIF_ptrhashi,
                       NULL, /* keyCmpFunctionp */
                       NULL, /* keyCopyFunctionp */
                       NULL, /* keyFreeFunctionp */
                       NULL, /* valCopyFunctionp */
                       NULL, /* valFreeFunctionp */
                       MARPAESLIF_HASH_SIZE,
                       0 /* wantedSubSize */);
  if (GENERICHASH_ERROR(marpaESLIFValuep->beforePtrHashp)) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValuep->beforePtrHashp init failure, %s", strerror(errno));
    marpaESLIFValuep->beforePtrHashp = NULL;
    goto err;
  }

  marpaESLIFValuep->afterPtrHashp = &(marpaESLIFValuep->_afterPtrHash);
  GENERICHASH_INIT_ALL(marpaESLIFValuep->afterPtrHashp,
                       _marpaESLIF_ptrhashi,
                       NULL, /* keyCmpFunctionp */
                       NULL, /* keyCopyFunctionp */
                       NULL, /* keyFreeFunctionp */
                       NULL, /* valCopyFunctionp */
                       NULL, /* valFreeFunctionp */
                       MARPAESLIF_HASH_SIZE,
                       0 /* wantedSubSize */);
  if (GENERICHASH_ERROR(marpaESLIFValuep->afterPtrHashp)) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "afterPtrHashp init failure, %s", strerror(errno));
    marpaESLIFValuep->afterPtrHashp = NULL;
    goto err;
  }

  goto done;

 err:
  marpaESLIFValue_freev(marpaESLIFValuep);
  marpaESLIFValuep = NULL;

 done:
  /* MARPAESLIF_TRACEF(marpaESLIFp, funcs, "return %p", marpaESLIFValuep); */
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %p", marpaESLIFValuep);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return marpaESLIFValuep;
}

/*****************************************************************************/
static inline short _marpaESLIFValue_stack_newb(marpaESLIFValue_t *marpaESLIFValuep)
/*****************************************************************************/
{
  short rcb;

  /* Initialize the stacks */
  GENERICSTACK_NEW(marpaESLIFValuep->valueResultStackp);
  if (GENERICSTACK_ERROR(marpaESLIFValuep->valueResultStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValuep->valueResultStackp initialization failure, %s", strerror(errno));
    goto err;
  }    

  rcb = 1;
  goto done;
 err:
  rcb = 0;
 done:
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFValue_stack_freeb(marpaESLIFValue_t *marpaESLIFValuep)
/*****************************************************************************/
{
  short           rcb;
  int             valueResultStacki;
  marpaESLIF_t   *marpaESLIFp;
  genericStack_t *valueResultStackp;
  genericStack_t *beforePtrStackp;
  genericHash_t  *beforePtrHashp;
  genericHash_t  *afterPtrHashp;

  if (marpaESLIFValuep != NULL) {
    /* Free the stacks */
    valueResultStackp = marpaESLIFValuep->valueResultStackp;
    if (valueResultStackp != NULL) {

      marpaESLIFp     = marpaESLIFValuep->marpaESLIFp;
      beforePtrStackp = marpaESLIFValuep->beforePtrStackp;
      beforePtrHashp  = marpaESLIFValuep->beforePtrHashp;
      afterPtrHashp   = marpaESLIFValuep->afterPtrHashp;

      for (valueResultStacki = 0; valueResultStacki < GENERICSTACK_USED(valueResultStackp); valueResultStacki++) {
        if (! _marpaESLIFValueResult_stack_i_setb(marpaESLIFp,
                                                  valueResultStackp,
                                                  valueResultStacki,
                                                  NULL, /* marpaESLIFValueResultp */
                                                  0, /* forgetb */
                                                  beforePtrStackp,
                                                  beforePtrHashp,
                                                  afterPtrHashp,
                                                  NULL /* marpaESLIFValueResultOrigp */)) {
          goto err;
        }
      }
      GENERICSTACK_FREE(valueResultStackp);
    }
  }
  rcb = 1;
  goto done;
 err:
  /* Makes sure all pointers are NULL anyway */
  if (marpaESLIFValuep != NULL) {
    marpaESLIFValuep->valueResultStackp = NULL;
  }
  rcb = 0;
 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIFValue_stack_setb(marpaESLIFValue_t *marpaESLIFValuep, int indicei, marpaESLIFValueResult_t *marpaESLIFValueResultp)
/*****************************************************************************/
{
  static const char *funcs  = "marpaESLIFValue_stack_setb";

  if (marpaESLIFValuep == NULL) {
    errno = EINVAL;
    return 0;
  }
  if (indicei < 0) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Indice %d is negative", indicei);
    return 0;
  }
  if (marpaESLIFValueResultp == NULL) {
    MARPAESLIF_ERROR(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValueResultp is NULL");
    return 0;
  }
  if (marpaESLIFValueResultp->contextp == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "%s must be called with a context != NULL", funcs);
    return 0;
  }
  if (! marpaESLIFValuep->inValuationb) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "%s must be called only in an action callback", funcs);
    return 0;
  }

  return _marpaESLIFValue_stack_setb(marpaESLIFValuep, indicei, marpaESLIFValueResultp);
}

/*****************************************************************************/
short marpaESLIFValue_stack_getb(marpaESLIFValue_t *marpaESLIFValuep, int indicei, marpaESLIFValueResult_t *marpaESLIFValueResultp)
/*****************************************************************************/
{
  static const char *funcs  = "marpaESLIFValue_stack_getb";

  if (marpaESLIFValuep == NULL) {
    errno = EINVAL;
    return 0;
  }
  if (indicei < 0) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Indice %d is negative", indicei);
    return 0;
  }
  if (marpaESLIFValueResultp == NULL) {
    MARPAESLIF_ERROR(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValueResultpp is NULL");
    return 0;
  }
  if (! marpaESLIFValuep->inValuationb) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "%s must be called only in an action callback", funcs);
    return 0;
  }

  return _marpaESLIFValue_stack_getb(marpaESLIFValuep, indicei, marpaESLIFValueResultp);
}

/*****************************************************************************/
static short _marpaESLIF_rule_action___shiftb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb)
/*****************************************************************************/
{
  static const char      *funcs                 = "_marpaESLIF_rule_action___shiftb";
  marpaESLIFRecognizer_t *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  short                   rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  /* ::shift is nothing else but an alias for ::copy[0] */
  rcb = _marpaESLIF_generic_action_copyb(userDatavp, marpaESLIFValuep, arg0i, argni, arg0i, resulti, nullableb);

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIFRecognizer_concat_valueResultCallbackb(void *userDatavp, marpaESLIFValueResult_t *marpaESLIFValueResultp)
/*****************************************************************************/
{
  static const char                      *funcs                       = "_marpaESLIFRecognizer_concat_valueResultCallbackb";
  marpaESLIF_concat_valueResultContext_t *contextp                    = (marpaESLIF_concat_valueResultContext_t *) userDatavp;
  marpaESLIFValue_t                      *marpaESLIFValuep            = contextp->marpaESLIFValuep;
  marpaESLIF_t                           *marpaESLIFp                 = marpaESLIFValuep->marpaESLIFp;
  marpaESLIF_stringGenerator_t           *marpaESLIF_stringGeneratorp = contextp->marpaESLIF_stringGeneratorp;
  marpaESLIFRecognizer_t                 *marpaESLIFRecognizerp       = marpaESLIFValuep->marpaESLIFRecognizerp;
  marpaESLIF_string_t                    *utf8p                       = NULL;
  genericLogger_t                        *genericLoggerp              = NULL;
  char                                   *srcs;
  size_t                                  srcl;
  genericStack_t                          todoStack;
  genericStack_t                         *todoStackp = &(todoStack);
  marpaESLIF_string_t                     string;
  marpaESLIFRepresentation_t              representationp;
  void                                   *representationUserDatavp;
  short                                   rcb;
  size_t                                  i;
  size_t                                  j;
  marpaESLIFValueResult_t                 marpaESLIFValueResult;
  short                                   continueb;
  marpaESLIF_uint32_t                     codepointi;
  char                                   *p;
  char                                   *maxp;
  int                                     lengthi;
  char                                   *encodingasciis;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  GENERICSTACK_INIT(todoStackp);
  if (GENERICSTACK_ERROR(todoStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "todoStackp initialization failure, %s", strerror(errno));
    todoStackp = NULL;
    goto err;
  }

  if (contextp->stringb) {
    genericLoggerp = GENERICLOGGER_CUSTOM(_marpaESLIF_generateStringWithLoggerCallback, marpaESLIF_stringGeneratorp, GENERICLOGGER_LOGLEVEL_TRACE);
    if (genericLoggerp == NULL) {
      goto err;
    }
    /* Start with an empty string */
    GENERICLOGGER_TRACE(genericLoggerp, "");
    if (! marpaESLIF_stringGeneratorp->okb) {
      goto err;
    }
  }

  /* Initialize the worklist */
  GENERICSTACK_PUSH_CUSTOMP(todoStackp, marpaESLIFValueResultp);
  if (GENERICSTACK_ERROR(todoStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "todoStackp push failure, %s", strerror(errno));
    goto err;
  }

  /* Iterate the worklist */
  while (GENERICSTACK_USED(todoStackp) > 0) {
    marpaESLIFValueResult = GENERICSTACK_POP_CUSTOM(todoStackp);
    continueb = 0; /* When we do not want to test stringification because we did not stringified */

    if ((marpaESLIFValueResult.type == MARPAESLIF_VALUE_TYPE_PTR) && (marpaESLIFValueResult.contextp == (void *) MARPAESLIF_EMBEDDED_CONTEXT_LUA)) {
      /* Specific to lua embedded: we use our proxy, then userDatavp is forced to be marpaESLIFValuep */
      representationp = _marpaESLIFValue_lua_representationb;
      representationUserDatavp = marpaESLIFValuep;
    } else {
      /* Origin representation, userDatavp is the one from original context */
      representationp = marpaESLIFValueResult.representationp;
      representationUserDatavp = contextp->userDatavp;
    }

    if (representationp != NULL) {
      srcs = NULL;
      srcl = 0;
      encodingasciis = NULL;
      if (! representationp(representationUserDatavp, &marpaESLIFValueResult, &srcs, &srcl, &encodingasciis)) {
        goto err;
      }
      if ((srcs != NULL) && (srcl > 0)) {
        if (encodingasciis != NULL) {
          marpaESLIFValueResult.type               = MARPAESLIF_VALUE_TYPE_STRING;
          marpaESLIFValueResult.contextp           = NULL;
          marpaESLIFValueResult.representationp    = NULL;
          marpaESLIFValueResult.u.s.p              = (unsigned char *) srcs;
          marpaESLIFValueResult.u.s.sizel          = srcl;
          marpaESLIFValueResult.u.s.encodingasciis = encodingasciis;
          marpaESLIFValueResult.u.s.shallowb       = 1;
          marpaESLIFValueResult.u.s.freeUserDatavp = NULL;
          marpaESLIFValueResult.u.s.freeCallbackp  = NULL;
        } else {
          marpaESLIFValueResult.type               = MARPAESLIF_VALUE_TYPE_ARRAY;
          marpaESLIFValueResult.contextp           = NULL;
          marpaESLIFValueResult.representationp    = NULL;
          marpaESLIFValueResult.u.a.p              = srcs;
          marpaESLIFValueResult.u.a.sizel          = srcl;
          marpaESLIFValueResult.u.a.shallowb       = 1;
          marpaESLIFValueResult.u.a.freeUserDatavp = NULL;
          marpaESLIFValueResult.u.a.freeCallbackp  = NULL;
        }
      } else {
        /* No op */
        continue;
      }
    }

    switch (marpaESLIFValueResult.type) {
    case MARPAESLIF_VALUE_TYPE_UNDEF:
      /* fprintf(stdout, "UNDEF\n"); fflush(stdout); */
      /* Undef default representation:
         - string mode: "", json: "null"
         - binary mode: N/A
      */
      if (contextp->stringb) {
        if (contextp->jsonb) {
          GENERICLOGGER_TRACE(genericLoggerp, "null");
        } else {
          GENERICLOGGER_TRACE(genericLoggerp, "");
        }
      } else {
        continueb = 1;
      }
      break;
    case MARPAESLIF_VALUE_TYPE_CHAR:
      /* fprintf(stdout, "CHAR %c\n", marpaESLIFValueResult.u.c); fflush(stdout); */
      /* Char default representation:
         - string mode: %c, json: "%c"
         - binary mode: content
      */
      if (contextp->stringb) {
        if (contextp->jsonb) {
          _marpaESLIF_codepoint_to_json((marpaESLIF_uint32_t) marpaESLIFValueResult.u.c, genericLoggerp);
        } else {
          GENERICLOGGER_TRACEF(genericLoggerp, "%c", marpaESLIFValueResult.u.c);
        }
      } else {
        _marpaESLIF_appendOpaqueDataToStringGenerator(marpaESLIF_stringGeneratorp, (char *) &(marpaESLIFValueResult.u.c), sizeof(marpaESLIFValueResultChar_t));
      }
      break;
    case MARPAESLIF_VALUE_TYPE_SHORT:
      /* fprintf(stdout, "SHORT %d\n", (int) marpaESLIFValueResult.u.b); fflush(stdout); */
      /* Char default representation:
         - string mode: %d, json: %d
         - binary mode: content
      */
      if (contextp->stringb) {
        GENERICLOGGER_TRACEF(genericLoggerp, "%d", (int) marpaESLIFValueResult.u.b);
      } else {
        _marpaESLIF_appendOpaqueDataToStringGenerator(marpaESLIF_stringGeneratorp, (char *) &(marpaESLIFValueResult.u.b), sizeof(marpaESLIFValueResultShort_t));
      }
      break;
    case MARPAESLIF_VALUE_TYPE_INT:
      /* fprintf(stdout, "INT %d\n", marpaESLIFValueResult.u.i); fflush(stdout); */
      /* Char default representation:
         - string mode: %d, json: %d
         - binary mode: content
      */
      if (contextp->stringb) {
        GENERICLOGGER_TRACEF(genericLoggerp, "%d", marpaESLIFValueResult.u.i);
      } else {
        _marpaESLIF_appendOpaqueDataToStringGenerator(marpaESLIF_stringGeneratorp, (char *) &(marpaESLIFValueResult.u.i), sizeof(marpaESLIFValueResultInt_t));
      }
      break;
    case MARPAESLIF_VALUE_TYPE_LONG:
      /* fprintf(stdout, "LONG %ld\n", marpaESLIFValueResult.u.l); fflush(stdout); */
      /* Long default representation:
         string mode: %ld, json: %ld
         binary mode: content
      */
      if (contextp->stringb) {
        GENERICLOGGER_TRACEF(genericLoggerp, "%ld", marpaESLIFValueResult.u.l);
      } else {
        _marpaESLIF_appendOpaqueDataToStringGenerator(marpaESLIF_stringGeneratorp, (char *) &(marpaESLIFValueResult.u.l), sizeof(marpaESLIFValueResultLong_t));
      }
      break;
    case MARPAESLIF_VALUE_TYPE_FLOAT:
      /* fprintf(stdout, "FLOAT %f\n", (double) marpaESLIFValueResult.u.f); fflush(stdout); */
      /* Float default representation:
         - string mode: marpaESLIFp->float_fmts, json: marpaESLIFp->float_fmts
         - binary mode: content
      */
      if (contextp->stringb) {
        GENERICLOGGER_TRACEF(genericLoggerp, marpaESLIFp->float_fmts, (double) marpaESLIFValueResult.u.f);
      } else {
        _marpaESLIF_appendOpaqueDataToStringGenerator(marpaESLIF_stringGeneratorp, (char *) &(marpaESLIFValueResult.u.f), sizeof(marpaESLIFValueResultFloat_t));
      }
      break;
    case MARPAESLIF_VALUE_TYPE_DOUBLE:
      /* fprintf(stdout, "DOUBLE %f\n", marpaESLIFValueResult.u.d); fflush(stdout); */
      /* Double default representation:
         - string mode: marpaESLIFp->double_fmts, json: marpaESLIFp->double_fmts
         - binary mode: content
      */
      if (contextp->stringb) {
        GENERICLOGGER_TRACEF(genericLoggerp, marpaESLIFp->double_fmts, marpaESLIFValueResult.u.d);
      } else {
        _marpaESLIF_appendOpaqueDataToStringGenerator(marpaESLIF_stringGeneratorp, (char *) &(marpaESLIFValueResult.u.d), sizeof(marpaESLIFValueResultDouble_t));
      }
      break;
    case MARPAESLIF_VALUE_TYPE_PTR:
      /* fprintf(stdout, "PTR %p\n", marpaESLIFValueResult.u.p.p); fflush(stdout); */
      /* Ptr default representation:
         - string mode: %p, json: %p
         - binary mode: content
      */
      if (contextp->stringb) {
        GENERICLOGGER_TRACEF(genericLoggerp, "%p", marpaESLIFValueResult.u.p.p);
      } else {
        _marpaESLIF_appendOpaqueDataToStringGenerator(marpaESLIF_stringGeneratorp, (char *) &(marpaESLIFValueResult.u.p.p), sizeof(void *));
      }
      break;
    case MARPAESLIF_VALUE_TYPE_ARRAY:
      /* fprintf(stdout, "ARRAY {%p,%ld}\n", marpaESLIFValueResult.u.a.p, (unsigned long) marpaESLIFValueResult.u.a.sizel); fflush(stdout); */
      /* Array default representation: bytes as-is */
      if ((marpaESLIFValueResult.u.a.p != NULL) && (marpaESLIFValueResult.u.a.sizel > 0)) {
        _marpaESLIF_appendOpaqueDataToStringGenerator(marpaESLIF_stringGeneratorp, marpaESLIFValueResult.u.a.p, marpaESLIFValueResult.u.a.sizel);
      }
      break;
    case MARPAESLIF_VALUE_TYPE_BOOL:
      /* fprintf(stdout, "BOOL %d\n", (int) marpaESLIFValueResult.u.y); fflush(stdout); */
      /* Bool default representation:
         - string mode: true or false, json: true or false
         - binary mode: content
      */
      if (contextp->stringb) {
        GENERICLOGGER_TRACEF(genericLoggerp, "%s", (marpaESLIFValueResult.u.y == MARPAESLIFVALUERESULTBOOL_TRUE) ? "true" : "false");
      } else {
        _marpaESLIF_appendOpaqueDataToStringGenerator(marpaESLIF_stringGeneratorp, (char *) &(marpaESLIFValueResult.u.y), sizeof(marpaESLIFValueResultBool_t));
      }
      break;
    case MARPAESLIF_VALUE_TYPE_STRING:
      /* fprintf(stdout, "STRING {%p,%ld,%s}\n", marpaESLIFValueResult.u.s.p, (unsigned long) marpaESLIFValueResult.u.s.sizel, marpaESLIFValueResult.u.s.encodingasciis); fflush(stdout); */
      /* String default representation:
         - string mode: UTF-8 bytes, json: escaped string
         - binary mode: content
      */
      if (contextp->stringb) {
        string.bytep          = (char *) marpaESLIFValueResult.u.s.p;
        string.bytel          = marpaESLIFValueResult.u.s.sizel;
        string.encodingasciis = marpaESLIFValueResult.u.s.encodingasciis;
        string.asciis         = NULL;
        if (utf8p != &string) {
          _marpaESLIF_string_freev(utf8p, 0 /* onStackb */);
        }
        utf8p = _marpaESLIF_string2utf8p(marpaESLIFp, &string, 0 /* tconvsilentb */);
        if (utf8p == NULL) {
          goto err;
        }
        if (contextp->jsonb) {
          GENERICLOGGER_TRACE(genericLoggerp, "\""); if (! marpaESLIF_stringGeneratorp->okb) goto err;
          p = utf8p->bytep;
          maxp = p + utf8p->bytel;
          while (p < maxp) {
            lengthi = _marpaESLIF_utf82ordi((PCRE2_SPTR8) p, &codepointi);
            if (lengthi < 0) {
              /* Well, this is a paranoid test: this should never happen since utf8p did not fail, so we do not bother to give any detail */
              MARPAESLIF_ERROR(marpaESLIFp, "Malformed UTF-8 byte");
              errno = EINVAL;
              goto err;
            }
            _marpaESLIF_codepoint_to_json(codepointi, genericLoggerp);
            if (! marpaESLIF_stringGeneratorp->okb) {
              goto err;
            }
            p += lengthi;
          }
          GENERICLOGGER_TRACE(genericLoggerp, "\"");
        } else {
          _marpaESLIF_appendOpaqueDataToStringGenerator(marpaESLIF_stringGeneratorp, utf8p->bytep, utf8p->bytel);
        }
      } else {
        _marpaESLIF_appendOpaqueDataToStringGenerator(marpaESLIF_stringGeneratorp, (char *) marpaESLIFValueResult.u.s.p, marpaESLIFValueResult.u.s.sizel);
      }
      break;
    case MARPAESLIF_VALUE_TYPE_ROW:
      /* String default representation: concatenation of sub-members representation, in reverse order for intuitive representation -; */
      /* fprintf(stdout, "ROW {%p,%ld}\n", marpaESLIFValueResult.u.r.p, (unsigned long) marpaESLIFValueResult.u.r.sizel); fflush(stdout); */
      GENERICSTACK_PUSH_CUSTOM(todoStackp, marpaESLIFValueResultRightSquare);
      if (GENERICSTACK_ERROR(todoStackp)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "todoStackp push failure, %s", strerror(errno));
        goto err;
      }
      if (marpaESLIFValueResult.u.r.sizel > 0) {
        for (i = 0, j = marpaESLIFValueResult.u.r.sizel - 1; i < marpaESLIFValueResult.u.r.sizel; i++, j--) {
          GENERICSTACK_PUSH_CUSTOM(todoStackp, marpaESLIFValueResult.u.r.p[j]);
          if (GENERICSTACK_ERROR(todoStackp)) {
            MARPAESLIF_ERRORF(marpaESLIFp, "todoStackp push failure, %s", strerror(errno));
            goto err;
          }
          if (j > 0) {
            /* , */
            GENERICSTACK_PUSH_CUSTOM(todoStackp, marpaESLIFValueResultComma);
            if (GENERICSTACK_ERROR(todoStackp)) {
              MARPAESLIF_ERRORF(marpaESLIFp, "todoStackp push failure, %s", strerror(errno));
              goto err;
            }
          }
        }
      }
      GENERICSTACK_PUSH_CUSTOM(todoStackp, marpaESLIFValueResultLeftSquare);
      if (GENERICSTACK_ERROR(todoStackp)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "todoStackp push failure, %s", strerror(errno));
        goto err;
      }
      continueb = 1;
      break;
    case MARPAESLIF_VALUE_TYPE_TABLE:
      /* Nothing else but a row with an even number of elements, in reverse order for intuitive representation -; */
      /* fprintf(stdout, "TABLE {%p,%ld}\n", marpaESLIFValueResult.u.t.p, (unsigned long) marpaESLIFValueResult.u.t.sizel); fflush(stdout); */
      GENERICSTACK_PUSH_CUSTOM(todoStackp, marpaESLIFValueResultRightBracket);
      if (GENERICSTACK_ERROR(todoStackp)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "todoStackp push failure, %s", strerror(errno));
        goto err;
      }
      if (marpaESLIFValueResult.u.t.sizel > 0) {
        for (i = 0, j = marpaESLIFValueResult.u.r.sizel - 1; i < marpaESLIFValueResult.u.r.sizel; i++, j--) {
          /* Value */
          GENERICSTACK_PUSH_CUSTOM(todoStackp, marpaESLIFValueResult.u.t.p[j].value);
          if (GENERICSTACK_ERROR(todoStackp)) {
            MARPAESLIF_ERRORF(marpaESLIFp, "todoStackp push failure, %s", strerror(errno));
            goto err;
          }
          /* : */
          GENERICSTACK_PUSH_CUSTOM(todoStackp, marpaESLIFValueResultColon);
          if (GENERICSTACK_ERROR(todoStackp)) {
            MARPAESLIF_ERRORF(marpaESLIFp, "todoStackp push failure, %s", strerror(errno));
            goto err;
          }
          /* Key */
          GENERICSTACK_PUSH_CUSTOM(todoStackp, marpaESLIFValueResult.u.t.p[j].key);
          if (GENERICSTACK_ERROR(todoStackp)) {
            MARPAESLIF_ERRORF(marpaESLIFp, "todoStackp push failure, %s", strerror(errno));
            goto err;
          }
          if (j > 0) {
            /* , */
            GENERICSTACK_PUSH_CUSTOM(todoStackp, marpaESLIFValueResultComma);
            if (GENERICSTACK_ERROR(todoStackp)) {
              MARPAESLIF_ERRORF(marpaESLIFp, "todoStackp push failure, %s", strerror(errno));
              goto err;
            }
          }
        }
      }
      GENERICSTACK_PUSH_CUSTOM(todoStackp, marpaESLIFValueResultLeftBracket);
      if (GENERICSTACK_ERROR(todoStackp)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "todoStackp push failure, %s", strerror(errno));
        goto err;
      }
      continueb = 1;
      break;
    case MARPAESLIF_VALUE_TYPE_LONG_DOUBLE:
      /* fprintf(stdout, "LONG_DOUBLE %Lf\n", marpaESLIFValueResult.u.ld); fflush(stdout); */
      /* Long double default representation:
         - string mode: marpaESLIFp->long_double_fmts, json: marpaESLIFp->long_double_fmts
         - binary mode: content
      */
      if (contextp->stringb) {
        GENERICLOGGER_TRACEF(genericLoggerp, marpaESLIFp->long_double_fmts, marpaESLIFValueResult.u.ld);
      } else {
        _marpaESLIF_appendOpaqueDataToStringGenerator(marpaESLIF_stringGeneratorp, (char *) &(marpaESLIFValueResult.u.ld), sizeof(marpaESLIFValueResultLongDouble_t));
      }
      break;
#ifdef MARPAESLIF_HAVE_LONG_LONG
    case MARPAESLIF_VALUE_TYPE_LONG_LONG:
      /* fprintf(stdout, "LONG_LONG " MARPAESLIF_LONG_LONG_FMT "\n", marpaESLIFValueResult.u.ll); fflush(stdout); */
      /* Long double default representation:
         - string mode: MARPAESLIF_LONG_LONG_FMT, json: MARPAESLIF_LONG_LONG_FMT
         - binary mode: content
      */
      if (contextp->stringb) {
        GENERICLOGGER_TRACEF(genericLoggerp, MARPAESLIF_LONG_LONG_FMT, marpaESLIFValueResult.u.ll);
      } else {
        _marpaESLIF_appendOpaqueDataToStringGenerator(marpaESLIF_stringGeneratorp, (char *) &(marpaESLIFValueResult.u.ll), sizeof(marpaESLIFValueResultLongLong_t));
      }
      break;
#endif
    default:
      break;
    }

    if (continueb) {
      continue;
    }
    if (! marpaESLIF_stringGeneratorp->okb) {
      goto err;
    }
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  GENERICSTACK_RESET(todoStackp);
  if (utf8p != &string) {
    _marpaESLIF_string_freev(utf8p, 0 /* onStackb */);
  }
  GENERICLOGGER_FREE(genericLoggerp);
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIF_generic_action___concatb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultLexemep, int arg0i, int argni, int resulti, short nullableb, char *toEncodings, short jsonb)
/*****************************************************************************/
{
  /* This method guarantees to push either UNDEF, or an ARRAY, or an ASCII NUL terminated buffer PTR (caller makes sure to set ptrb and toEncodings appropriately)  */
  
  static const char                      *funcs                 = "_marpaESLIF_generic_action___concatb";
  marpaESLIFRecognizer_t                 *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  marpaESLIF_t                           *marpaESLIFp           = marpaESLIFValuep->marpaESLIFp;
  char                                   *toEncodingDups        = NULL;
  size_t                                  toEncodingl;
  int                                     argi;
  marpaESLIF_stringGenerator_t            marpaESLIF_stringGenerator;
  marpaESLIF_concat_valueResultContext_t  context;
  marpaESLIFValueResult_t                 marpaESLIFValueResult;
  marpaESLIFValueResult_t                *marpaESLIFValueResultp;
  char                                   *converteds;
  size_t                                  convertedl;
  short                                   rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (nullableb) {
    /* No choice: result is undef */
    if (! _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, (marpaESLIFValueResult_t *) &marpaESLIFValueResultUndef)) {
      goto err;
    }
  } else {

    /* Prepare a string generator */
    marpaESLIF_stringGenerator             = marpaESLIF_stringGeneratorTemplate;
    marpaESLIF_stringGenerator.marpaESLIFp = marpaESLIFp;

    context.userDatavp                  = userDatavp;
    context.marpaESLIFValuep            = marpaESLIFValuep;
    context.marpaESLIF_stringGeneratorp = &marpaESLIF_stringGenerator;
    context.stringb                     = (toEncodings != NULL) ? 1 : 0;
    context.jsonb                       = jsonb;

    converteds = NULL;

    if (marpaESLIFValueResultLexemep != NULL) {
      /* Symbol action */
      marpaESLIFValueResult           = *marpaESLIFValueResultLexemep;
      /* If it is a pointer, make it shallow in any case */
      MARPAESLIF_MAKE_MARPAESLIFVALUERESULT_SHALLOW(marpaESLIFValueResult);
      if (! _marpaESLIFRecognizer_value_validb(marpaESLIFRecognizerp, &marpaESLIFValueResult, &context, _marpaESLIFRecognizer_concat_valueResultCallbackb)) {
        goto err;
      }
    } else {
      /* Rule action */
      for (argi = arg0i; argi <= argni; argi++) {
        marpaESLIFValueResultp = _marpaESLIFValue_stack_getp(marpaESLIFValuep, argi);
        if (marpaESLIFValueResultp == NULL) {
          goto err;
        }
        if (! _marpaESLIFRecognizer_value_validb(marpaESLIFRecognizerp, marpaESLIFValueResultp, &context, _marpaESLIFRecognizer_concat_valueResultCallbackb)) {
          goto err;
        }
      }
    }

    if (marpaESLIF_stringGenerator.l > 1) { /* Because of the implicit NULL byte */
      if (toEncodings != NULL) {
        toEncodingl = strlen(toEncodings);
	/* Call for conversion in any case, this is a way to validate UTF-8 correctness if the destination encoding is also UTF-8 */
	converteds = _marpaESLIF_charconvb(marpaESLIFp,
					   toEncodings,
					   (char *) MARPAESLIF_UTF8_STRING, /* We request that representations always produce UTF-8 strings */
					   marpaESLIF_stringGenerator.s,
					   marpaESLIF_stringGenerator.l - 1, /* Skip the automatic NUL byte in the source */
					   &convertedl,
					   NULL, /* fromEncodingsp */
					   NULL, /* tconvpp */
					   1, /* eofb */
					   NULL, /* byteleftsp */
					   NULL, /* byteleftlp */
					   NULL, /* byteleftalloclp */
					   0 /* tconvsilentb */);
	if (converteds == NULL) {
	  goto err;
	}
        /* We send the whole data in one go: we ignore the fact that _marpaESLIF_string_removebomb() may return -1 */
	if (! _marpaESLIF_string_removebomb(marpaESLIFp, converteds, &(convertedl), toEncodings, NULL /* bomsizelp */)) {
	  goto err;
	}
	free(marpaESLIF_stringGenerator.s);
	marpaESLIF_stringGenerator.s = NULL;

        /* Duplicate toEncodings */
        toEncodingDups = strdup(toEncodings);
        if (toEncodingDups == NULL) {
          MARPAESLIF_ERRORF(marpaESLIFp, "strdup failure, %s", strerror(errno));
          goto err;
        }
        marpaESLIFValueResult.type               = MARPAESLIF_VALUE_TYPE_STRING;
        marpaESLIFValueResult.contextp           = NULL;
        marpaESLIFValueResult.representationp    = NULL;
        marpaESLIFValueResult.u.s.p              = (unsigned char *) converteds;
        marpaESLIFValueResult.u.s.sizel          = convertedl;
        marpaESLIFValueResult.u.s.encodingasciis = toEncodingDups;
        marpaESLIFValueResult.u.s.shallowb       = 0;
        marpaESLIFValueResult.u.s.freeUserDatavp = marpaESLIFRecognizerp;
        marpaESLIFValueResult.u.s.freeCallbackp  = _marpaESLIF_generic_freeCallbackv;
        if (! _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, &marpaESLIFValueResult)) {
          goto err;
        }
        /* converteds is now in the stack */
        converteds = NULL;
        /* toEncodingDups as well */
        toEncodingDups = NULL;
      } else {
        marpaESLIFValueResult.type            = MARPAESLIF_VALUE_TYPE_ARRAY;
        marpaESLIFValueResult.contextp        = NULL;
        marpaESLIFValueResult.representationp = NULL;
        marpaESLIFValueResult.u.a.sizel       = marpaESLIF_stringGenerator.l - 1;
        marpaESLIFValueResult.u.a.shallowb    = 0;
        marpaESLIFValueResult.u.a.p           = marpaESLIF_stringGenerator.s;
        marpaESLIFValueResult.u.a.freeUserDatavp = marpaESLIFRecognizerp;
        marpaESLIFValueResult.u.a.freeCallbackp  = _marpaESLIF_generic_freeCallbackv;

        if (! _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, &marpaESLIFValueResult)) {
          goto err;
        }
        /* marpaESLIF_stringGenerator.s is now in the stack */
        marpaESLIF_stringGenerator.s = NULL;
      }
    } else {
      if (! _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, (marpaESLIFValueResult_t *) &marpaESLIFValueResultUndef)) {
        goto err;
      }
    }
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  /* No need to do any of these tests when this is a nullable */
  if (! nullableb) {
    if (marpaESLIF_stringGenerator.s != NULL) {
      free(marpaESLIF_stringGenerator.s);
    }
    if (converteds != NULL) {
      free(converteds);
    }
    if (toEncodingDups != NULL) {
      free(toEncodingDups);
    }
  }
  
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIF_generic_action_copyb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int argi, int resulti, short nullableb)
/*****************************************************************************/
{
  static const char         *funcs                 = "_marpaESLIF_generic_action_copyb";
  marpaESLIFRecognizer_t    *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  int                        argorigi              = argi;
  marpaESLIFValueResult_t    marpaESLIFValueResult;
  marpaESLIFValueResult_t   *marpaESLIFValueResultp;
  short                      forgetb;
  short                      rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (nullableb) {
    marpaESLIFValueResultp = (marpaESLIFValueResult_t *) &marpaESLIFValueResultUndef;
    forgetb = 0;
  } else {

    /* Use the perl formalism for negative indices */
    if (argi < 0) {
      argi = argni + 1 + argi;
    }
  
    if ((argi < arg0i) || (argi > argni)) {
      if (argorigi < 0) {
        MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Indice %d converted to %d is out of range [%d..%d]", argorigi, argi, arg0i, argni);
      } else {
        MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Indice %d is out of range [%d..%d]", argi, arg0i, argni);
      }
      goto err;
    }

    /* When argi is resulti, this is a no-op */
    if (argi == resulti) {
      MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "No-op");
      rcb = 1;
      goto done;
    }

    if (! _marpaESLIFValue_stack_getAndForgetb(marpaESLIFValuep, argi, &marpaESLIFValueResult)) {
      goto err;
    }
    marpaESLIFValueResultp = &marpaESLIFValueResult;
    forgetb = 1;
  }

  rcb = _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, marpaESLIFValueResultp);
  /*
   * If this failed, since we are not atomic, try to restore the original, ignoring failure or not
   */
  if ((! rcb) && forgetb) {
    if (! _marpaESLIFValue_stack_setb(marpaESLIFValuep, argi, marpaESLIFValueResultp)) {
      MARPAESLIF_WARNF(marpaESLIFValuep->marpaESLIFp, "Failure to restore original value at indice %d", argi);
    }
  }
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIF_rule_action___concatb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb)
/*****************************************************************************/
{
  return _marpaESLIF_generic_action___concatb(userDatavp, marpaESLIFValuep, NULL /* marpaESLIFValueResultLexemep */, arg0i, argni, resulti, nullableb, NULL /* toEncodings */, 0 /* jsonb */);
}

/*****************************************************************************/
static short _marpaESLIF_rule_action___copyb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb)
/*****************************************************************************/
{
  /* We want to copy in resulti the stack item at ::copy[THIS] */
  static const char      *funcs                 = "_marpaESLIF_rule_action___copyb";
  marpaESLIFRecognizer_t *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  char                   *actions               = marpaESLIFValuep->actions;
  int                     rhsi;
  short                   rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (actions == NULL) {
    MARPAESLIF_ERROR(marpaESLIFValuep->marpaESLIFp, "actions is NULL, cannot guess indice to copy");
    goto err;
  }

  rhsi = atoi(actions + copyl + 1); /* Because copyl is the length of "::copy" */

  rcb = _marpaESLIF_generic_action_copyb(userDatavp, marpaESLIFValuep, arg0i, argni, arg0i + rhsi, resulti, nullableb);
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIF_rule_action___undefb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb)
/*****************************************************************************/
{
  static const char      *funcs                 = "_marpaESLIF_rule_action___undefb";
  marpaESLIFRecognizer_t *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  short                   rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  rcb = _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, (marpaESLIFValueResult_t *) &marpaESLIFValueResultUndef);

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIF_rule_action___trueb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb)
/*****************************************************************************/
{
  static const char      *funcs                 = "_marpaESLIF_rule_action___trueb";
  marpaESLIFRecognizer_t *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  marpaESLIF_t           *marpaESLIFp           = marpaESLIFValuep->marpaESLIFp;
  short                   rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  rcb = _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, &(marpaESLIFp->marpaESLIFValueResultTrue));

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIF_rule_action___falseb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb)
/*****************************************************************************/
{
  static const char      *funcs                 = "_marpaESLIF_rule_action___trueb";
  marpaESLIFRecognizer_t *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  marpaESLIF_t           *marpaESLIFp           = marpaESLIFValuep->marpaESLIFp;
  short                   rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  rcb = _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, &(marpaESLIFp->marpaESLIFValueResultFalse));

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIF_rule_action___jsonb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb)
/*****************************************************************************/
{
  return _marpaESLIF_generic_action___concatb(userDatavp, marpaESLIFValuep, NULL /* marpaESLIFValueResultLexemep */, arg0i, argni, resulti, nullableb, "UTF-8", 1 /* jsonb */);
}

/*****************************************************************************/
static short _marpaESLIF_rule_action___rowb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb)
/*****************************************************************************/
{
  short                    rcb;
  size_t                   i;
  marpaESLIFValueResult_t  marpaESLIFValueResult;
  marpaESLIFValueResult_t *marpaESLIFValueResultp;
  short                   *origshallowbp;

  marpaESLIFValueResult.type               = MARPAESLIF_VALUE_TYPE_ROW;
  marpaESLIFValueResult.contextp           = NULL;
  marpaESLIFValueResult.representationp    = NULL;
  marpaESLIFValueResult.u.r.p              = NULL;
  marpaESLIFValueResult.u.r.sizel          = nullableb ? 0 : (argni - arg0i + 1);
  marpaESLIFValueResult.u.r.shallowb       = 0;
  marpaESLIFValueResult.u.r.freeUserDatavp = marpaESLIFValuep->marpaESLIFRecognizerp;
  marpaESLIFValueResult.u.r.freeCallbackp  = _marpaESLIF_generic_freeCallbackv;

  if (marpaESLIFValueResult.u.r.sizel > 0) {
    if ((marpaESLIFValueResult.u.r.p = (marpaESLIFValueResult_t *) malloc(marpaESLIFValueResult.u.r.sizel * sizeof(marpaESLIFValueResult_t))) == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "malloc failure, %s", strerror(errno));
      goto err;
    }
    for (i = 0; i < marpaESLIFValueResult.u.r.sizel; i++) {
      marpaESLIFValueResultp = _marpaESLIFValue_stack_getp(marpaESLIFValuep, (int) (arg0i + i));
      if (marpaESLIFValueResultp == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Failed to fetch value result at stack indice %d", resulti);
        goto err;
      }

      /* We have to take care of members's shallow status: the array becomes the owner in any case */
      origshallowbp = NULL;
      switch (marpaESLIFValueResultp->type) {
      case MARPAESLIF_VALUE_TYPE_PTR:
        origshallowbp = &(marpaESLIFValueResultp->u.p.shallowb);
        break;
      case MARPAESLIF_VALUE_TYPE_ARRAY:
        origshallowbp = &(marpaESLIFValueResultp->u.a.shallowb);
        break;
      case MARPAESLIF_VALUE_TYPE_STRING:
        origshallowbp = &(marpaESLIFValueResultp->u.s.shallowb);
        break;
      case MARPAESLIF_VALUE_TYPE_ROW:
        origshallowbp = &(marpaESLIFValueResultp->u.r.shallowb);
        break;
      case MARPAESLIF_VALUE_TYPE_TABLE:
        origshallowbp = &(marpaESLIFValueResultp->u.t.shallowb);
        break;
      default:
        break;
      }

      marpaESLIFValueResult.u.r.p[i] = *marpaESLIFValueResultp;

      if (origshallowbp != NULL) {
        *origshallowbp = 1;
      }

    }
  }

  /* If this fails, we will leak at most */
  if (! _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, &marpaESLIFValueResult)) {
    goto err;
  }

  rcb = 1;
  goto done;

 err:
  if (marpaESLIFValueResult.u.r.p != NULL) {
    free(marpaESLIFValueResult.u.r.p);
  }
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIF_rule_action___tableb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb)
/*****************************************************************************/
/* Exactly the same logic as for row except that the target is of type table and that we require an even number of elements */
/*****************************************************************************/
{
  short                    rcb;
  size_t                   i;
  marpaESLIFValueResult_t  marpaESLIFValueResult;
  marpaESLIFValueResult_t *marpaESLIFValueResultp;
  short                   *origshallowbp;
  short                    keyb;

  marpaESLIFValueResult.type               = MARPAESLIF_VALUE_TYPE_TABLE;
  marpaESLIFValueResult.contextp           = NULL;
  marpaESLIFValueResult.representationp    = NULL;
  marpaESLIFValueResult.u.t.p              = NULL;
  marpaESLIFValueResult.u.t.sizel          = nullableb ? 0 : (argni - arg0i + 1);
  marpaESLIFValueResult.u.t.shallowb       = 0;
  marpaESLIFValueResult.u.t.freeUserDatavp = marpaESLIFValuep->marpaESLIFRecognizerp;
  marpaESLIFValueResult.u.t.freeCallbackp  = _marpaESLIF_generic_freeCallbackv;

  if ((marpaESLIFValueResult.u.t.sizel % 2) != 0) {
    MARPAESLIF_ERROR(marpaESLIFValuep->marpaESLIFp, "::table rule action requires an even number of arguments");
    goto err;
  }

  if (marpaESLIFValueResult.u.t.sizel > 0) {
    if ((marpaESLIFValueResult.u.t.p = (marpaESLIFValueResultPair_t *) malloc(marpaESLIFValueResult.u.t.sizel * sizeof(marpaESLIFValueResultPair_t))) == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "malloc failure, %s", strerror(errno));
      goto err;
    }
    keyb = 1;
    for (i = 0; i < marpaESLIFValueResult.u.t.sizel; i++, keyb = ~keyb) {
      marpaESLIFValueResultp = _marpaESLIFValue_stack_getp(marpaESLIFValuep, (int) (arg0i + i));
      if (marpaESLIFValueResultp == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Failed to fetch value result at stack indice %d", resulti);
        goto err;
      }

      /* We have to take care of members's shallow status: the table becomes the owner in any case */
      origshallowbp = NULL;
      switch (marpaESLIFValueResultp->type) {
      case MARPAESLIF_VALUE_TYPE_PTR:
        origshallowbp = &(marpaESLIFValueResultp->u.p.shallowb);
        break;
      case MARPAESLIF_VALUE_TYPE_ARRAY:
        origshallowbp = &(marpaESLIFValueResultp->u.a.shallowb);
        break;
      case MARPAESLIF_VALUE_TYPE_STRING:
        origshallowbp = &(marpaESLIFValueResultp->u.s.shallowb);
        break;
      case MARPAESLIF_VALUE_TYPE_ROW:
        origshallowbp = &(marpaESLIFValueResultp->u.t.shallowb);
        break;
      case MARPAESLIF_VALUE_TYPE_TABLE:
        origshallowbp = &(marpaESLIFValueResultp->u.t.shallowb);
        break;
      default:
        break;
      }

      if (keyb) {
        marpaESLIFValueResult.u.t.p[i].key = *marpaESLIFValueResultp;
      } else {
        marpaESLIFValueResult.u.t.p[i].value = *marpaESLIFValueResultp;
      }

      if (origshallowbp != NULL) {
        *origshallowbp = 1;
      }

    }
  }

  /* If this fails, we will leak at most */
  if (! _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, &marpaESLIFValueResult)) {
    goto err;
  }

  rcb = 1;
  goto done;

 err:
  if (marpaESLIFValueResult.u.t.p != NULL) {
    free(marpaESLIFValueResult.u.t.p);
  }
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIF_rule_action___astb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb)
/*****************************************************************************/
/* almost like ::row, except that this is a table where single key is rule   */
/* name and single value is the list of values.                              */
/*****************************************************************************/
{
  short                    rcb;
  size_t                   i;
  marpaESLIFValueResult_t  marpaESLIFValueResult;
  marpaESLIFValueResult_t *marpaESLIFValueResultp;
  marpaESLIFValueResult_t *keyp;
  marpaESLIFValueResult_t *valuep;
  short                   *origshallowbp;
  marpaESLIF_symbol_t     *symbolp;
  size_t                   sizel;

  marpaESLIFValueResult.type               = MARPAESLIF_VALUE_TYPE_TABLE;
  marpaESLIFValueResult.contextp           = NULL;
  marpaESLIFValueResult.representationp    = NULL;
  marpaESLIFValueResult.u.t.p              = NULL;
  marpaESLIFValueResult.u.t.sizel          = 0;
  marpaESLIFValueResult.u.t.shallowb       = 0;
  marpaESLIFValueResult.u.t.freeUserDatavp = marpaESLIFValuep->marpaESLIFRecognizerp;
  marpaESLIFValueResult.u.t.freeCallbackp  = _marpaESLIF_generic_freeCallbackv;

  if ((marpaESLIFValueResult.u.t.p = (marpaESLIFValueResultPair_t *) malloc(sizeof(marpaESLIFValueResultPair_t))) == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "malloc failure, %s", strerror(errno));
    goto err;
  }

  keyp   = &(marpaESLIFValueResult.u.t.p[0].key);
  valuep = &(marpaESLIFValueResult.u.t.p[0].value);

  /* Per definition, current rule is in marpaESLIFValuep->rulep it is a rule action callback, symbolp otherwise (case of a nullable symbol action) */
  symbolp = marpaESLIFValuep->rulep != NULL ? marpaESLIFValuep->rulep->lhsp : marpaESLIFValuep->symbolp;
  keyp->type               = MARPAESLIF_VALUE_TYPE_STRING;
  keyp->contextp           = NULL;
  keyp->representationp    = NULL;
  keyp->u.s.p              = (unsigned char *) symbolp->descp->bytep;
  keyp->u.s.freeUserDatavp = NULL;
  keyp->u.s.freeCallbackp  = NULL;
  keyp->u.s.shallowb       = 1;
  keyp->u.s.sizel          = symbolp->descp->bytel;
  keyp->u.s.encodingasciis = symbolp->descp->encodingasciis;

  *valuep = marpaESLIFValueResultUndef;

  marpaESLIFValueResult.u.t.sizel   = 1;

  if (! nullableb) {
    sizel = argni - arg0i + 1;

    valuep->type               = MARPAESLIF_VALUE_TYPE_ROW;
    valuep->contextp           = NULL;
    valuep->representationp    = NULL;
    valuep->u.r.p              = NULL;
    valuep->u.r.sizel          = sizel;
    valuep->u.r.shallowb       = 0;
    valuep->u.r.freeUserDatavp = marpaESLIFValuep->marpaESLIFRecognizerp;
    valuep->u.r.freeCallbackp  = _marpaESLIF_generic_freeCallbackv;

    if (sizel > 0) {
      if ((valuep->u.r.p = (marpaESLIFValueResult_t *) malloc(sizel * sizeof(marpaESLIFValueResult_t))) == NULL) {
        MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "malloc failure, %s", strerror(errno));
        goto err;
      }
      for (i = 0; i < sizel; i++) {
        marpaESLIFValueResultp = _marpaESLIFValue_stack_getp(marpaESLIFValuep, (int) (arg0i + i));
        if (marpaESLIFValueResultp == NULL) {
          MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Failed to fetch value result at stack indice %d", resulti);
          goto err;
        }

        /* We have to take care of members's shallow status: the array becomes the owner in any case */
        origshallowbp = NULL;
        switch (marpaESLIFValueResultp->type) {
        case MARPAESLIF_VALUE_TYPE_PTR:
          origshallowbp = &(marpaESLIFValueResultp->u.p.shallowb);
          break;
        case MARPAESLIF_VALUE_TYPE_ARRAY:
          origshallowbp = &(marpaESLIFValueResultp->u.a.shallowb);
          break;
        case MARPAESLIF_VALUE_TYPE_STRING:
          origshallowbp = &(marpaESLIFValueResultp->u.s.shallowb);
          break;
        case MARPAESLIF_VALUE_TYPE_ROW:
          origshallowbp = &(marpaESLIFValueResultp->u.r.shallowb);
          break;
        case MARPAESLIF_VALUE_TYPE_TABLE:
          origshallowbp = &(marpaESLIFValueResultp->u.t.shallowb);
          break;
        default:
          break;
        }

        valuep->u.r.p[i] = *marpaESLIFValueResultp;

        if (origshallowbp != NULL) {
          *origshallowbp = 1;
        }
      }
    }
  }

  /* If this fails, we will leak at most */
  if (! _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, &marpaESLIFValueResult)) {
    goto err;
  }

  rcb = 1;
  goto done;

 err:
  if (marpaESLIFValueResult.u.t.p != NULL) {
    if ((valuep->type == MARPAESLIF_VALUE_TYPE_ROW) && (valuep->u.r.p != NULL)) {
      free(valuep->u.r.p);
    }
  }
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIF_rule_action___asciib(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb)
/*****************************************************************************/
{
  return _marpaESLIF_generic_action___concatb(userDatavp, marpaESLIFValuep, NULL /* marpaESLIFValueResultLexemep */, arg0i, argni, resulti, nullableb, "ASCII", 0 /* jsonb */);
}

/*****************************************************************************/
static short _marpaESLIF_rule_action___convertb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, int arg0i, int argni, int resulti, short nullableb)
/*****************************************************************************/
{
  /* We want to copy in resulti the result of ::convert[THIS] */
  static const char      *funcs                 = "_marpaESLIF_rule_action___convertb";
  marpaESLIFRecognizer_t *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  char                   *actions               = marpaESLIFValuep->actions;
  char                   *converts              = NULL;
  short                   rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (actions == NULL) {
    MARPAESLIF_ERROR(marpaESLIFValuep->marpaESLIFp, "actions is NULL, cannot guess convert encoding");
    goto err;
  }

  converts = strdup(actions);
  if (converts == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "strdup failure, %s", strerror(errno));
    goto err;
  }

  /* By definition, converts is "::convert[THIS]" */
  converts[strlen(converts) - 1] = '\0';  /* Remove the last "]" */

  rcb = _marpaESLIF_generic_action___concatb(userDatavp, marpaESLIFValuep, NULL /* marpaESLIFValueResultLexemep */, arg0i, argni, resulti, nullableb, converts + convertl + 1, 0 /* jsonb */);
  goto done;

 err:
  rcb = 0;

 done:
  if (converts != NULL) {
    free(converts);
  }
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIF_symbol_action___concatb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti)
/*****************************************************************************/
{
  return _marpaESLIF_generic_action___concatb(userDatavp, marpaESLIFValuep, marpaESLIFValueResultp, -1 /* arg0i */, -1 /* argni */, resulti, 0 /* nullableb */, NULL /* toEncodings */, 0 /* jsonb */);
}

/*****************************************************************************/
static short _marpaESLIF_symbol_action___trueb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti)
/*****************************************************************************/
{
  return _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, &(marpaESLIFValuep->marpaESLIFp->marpaESLIFValueResultTrue));
}

/*****************************************************************************/
static short _marpaESLIF_symbol_action___falseb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti)
/*****************************************************************************/
{
  return _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, &(marpaESLIFValuep->marpaESLIFp->marpaESLIFValueResultFalse));
}

/*****************************************************************************/
static short _marpaESLIF_symbol_action___jsonb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti)
/*****************************************************************************/
{
  return _marpaESLIF_generic_action___concatb(userDatavp, marpaESLIFValuep, marpaESLIFValueResultp, -1 /* arg0i */, -1 /* argni */, resulti, 0 /* nullableb */, "UTF-8", 1 /* jsonb */);
}

/*****************************************************************************/
static short _marpaESLIF_symbol_action___transferb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti)
/*****************************************************************************/
/* This method can be called in only one case: this is the top recognizer    */
/* that wants to put in its value stack a lexeme's marpaESLIFValueResultp.   */
/*****************************************************************************/
{
  static const char      *funcs                 = "_marpaESLIF_symbol_action___transferb";
  marpaESLIF_t           *marpaESLIFp           = marpaESLIFValuep->marpaESLIFp;
  marpaESLIFRecognizer_t *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  marpaESLIFValueResult_t marpaESLIFValueResult;
  short                   rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  /* Two cases: the lexeme comes from external, or from the grammar */
  /* It is easy to distinguish the two cases using the context:     */
  /* - Internal lexemes have a NULL context, are always of type ARRAY */
  /* - External lexemes have a non-NULL context */
  if (marpaESLIFValueResultp->contextp == NULL) {
    /* Must be ARRAY with a non-zero size - we duplicate it */
    if (marpaESLIFValueResultp->type != MARPAESLIF_VALUE_TYPE_ARRAY) {
      MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValueResultp->type is not ARRAY (got %d, %s)", marpaESLIFValueResultp->type, _marpaESLIF_value_types(marpaESLIFValueResultp->type));
      goto err;
    }
    if (marpaESLIFValueResultp->u.a.sizel <= 0) {
      MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValueResultp->type is ARRAY or size %ld", (unsigned long) marpaESLIFValueResultp->u.a.sizel);
      goto err;
    }
    /* Duplicate data */
    marpaESLIFValueResult.type               = MARPAESLIF_VALUE_TYPE_ARRAY;
    marpaESLIFValueResult.contextp           = NULL;
    marpaESLIFValueResult.representationp    = NULL;
    marpaESLIFValueResult.u.a.p              = malloc(marpaESLIFValueResultp->u.a.sizel + 1); /* Hiden NUL byte for convenience */
    if (marpaESLIFValueResult.u.a.p == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
      goto err;
    }
    memcpy(marpaESLIFValueResult.u.a.p, marpaESLIFValueResultp->u.a.p, marpaESLIFValueResultp->u.a.sizel);
    marpaESLIFValueResult.u.a.p[marpaESLIFValueResultp->u.a.sizel] = '\0';
    marpaESLIFValueResult.u.a.sizel          = marpaESLIFValueResultp->u.a.sizel;
    marpaESLIFValueResult.u.a.freeUserDatavp = marpaESLIFRecognizerp;
    marpaESLIFValueResult.u.a.freeCallbackp  = _marpaESLIF_generic_freeCallbackv;
    marpaESLIFValueResult.u.a.shallowb       = 0;
  } else {
    /* It is in lexemeInputStack : duplicate it and make the duplicate shallow */
    marpaESLIFValueResult = *marpaESLIFValueResultp;
    MARPAESLIF_MAKE_MARPAESLIFVALUERESULT_SHALLOW(marpaESLIFValueResult);
  }

  rcb = _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, &marpaESLIFValueResult);

  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static short _marpaESLIF_symbol_action___undefb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti)
/*****************************************************************************/
{
  return _marpaESLIFValue_stack_setb(marpaESLIFValuep, resulti, (marpaESLIFValueResult_t *) &marpaESLIFValueResultUndef);
}

/*****************************************************************************/
static short _marpaESLIF_symbol_action___asciib(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti)
/*****************************************************************************/
{
  return _marpaESLIF_generic_action___concatb(userDatavp, marpaESLIFValuep, marpaESLIFValueResultp, -1 /* arg0i */, -1 /* argni */, resulti, 0 /* nullableb */, "ASCII", 0 /* jsonb */);
}

/*****************************************************************************/
static short _marpaESLIF_symbol_action___convertb(void *userDatavp, marpaESLIFValue_t *marpaESLIFValuep, marpaESLIFValueResult_t *marpaESLIFValueResultp, int resulti)
/*****************************************************************************/
{
  /* We want to copy in resulti the result of ::convert[THIS] */
  static const char      *funcs                 = "_marpaESLIF_symbol_action___convertb";
  marpaESLIFRecognizer_t *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  char                   *actions               = marpaESLIFValuep->actions;
  char                   *converts              = NULL;
  short                   rcb;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (actions == NULL) {
    MARPAESLIF_ERROR(marpaESLIFValuep->marpaESLIFp, "actions is NULL, cannot guess convert encoding");
    goto err;
  }

  converts = strdup(actions);
  if (converts == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "strdup failure, %s", strerror(errno));
    goto err;
  }

  /* By definition, converts is "::convert[THIS]" */
  converts[strlen(converts) - 1] = '\0';  /* Remove the last "]" */

  rcb = _marpaESLIF_generic_action___concatb(userDatavp, marpaESLIFValuep, marpaESLIFValueResultp, -1 /* arg0i */, -1 /* argni */, resulti, 0 /* nullableb */, converts + convertl + 1, 0 /* jsonb */);
  goto done;

 err:
  rcb = 0;

 done:
  if (converts != NULL) {
    free(converts);
  }
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
short marpaESLIFValue_stack_getAndForgetb(marpaESLIFValue_t *marpaESLIFValuep, int indicei, marpaESLIFValueResult_t *marpaESLIFValueResultp)
/*****************************************************************************/
{
  static const char *funcs = "marpaESLIFValue_stack_getAndForgetb";
  
  if (marpaESLIFValuep == NULL) {
    errno = EINVAL;
    return 0;
  }
  if (indicei < 0) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Indice %d is negative", indicei);
    return 0;
  }
  if (marpaESLIFValueResultp == NULL) {
    MARPAESLIF_ERROR(marpaESLIFValuep->marpaESLIFp, "marpaESLIFValueResultpp is NULL");
    return 0;
  }
  if (! marpaESLIFValuep->inValuationb) {
    MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "%s must be called only in an action callback", funcs);
    return 0;
  }

  return _marpaESLIFValue_stack_getAndForgetb(marpaESLIFValuep, indicei, marpaESLIFValueResultp);
}

/*****************************************************************************/
short marpaESLIFRecognizer_readb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char **inputsp, size_t *inputlp)
/*****************************************************************************/
{
  static const char *funcs = "marpaESLIFRecognizer_readb";
  short              rcb;

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (! _marpaESLIFRecognizer_readb(marpaESLIFRecognizerp)) {
    goto err;
  }

  rcb = marpaESLIFRecognizer_inputb(marpaESLIFRecognizerp, inputsp, inputlp);
  goto done;

 err:
  rcb = 0;

 done:
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_inputb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char **inputsp, size_t *inputlp)
/*****************************************************************************/
{
  static const char *funcs = "_marpaESLIFRecognizer_inputb";

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  if (inputsp != NULL) {
    *inputsp = marpaESLIFRecognizerp->marpaESLIF_streamp->inputs;
  }
  if (inputlp != NULL) {
    *inputlp = marpaESLIFRecognizerp->marpaESLIF_streamp->inputl;
  }

  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "return 1");
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;

  return 1;
}

/*****************************************************************************/
short marpaESLIFRecognizer_inputb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char **inputsp, size_t *inputlp)
/*****************************************************************************/
{
  static const char *funcs = "marpaESLIFRecognizer_inputb";

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return _marpaESLIFRecognizer_inputb(marpaESLIFRecognizerp, inputsp, inputlp);
}

/*****************************************************************************/
short marpaESLIFRecognizer_locationb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, size_t *linelp, size_t *columnlp)
/*****************************************************************************/
{
  marpaESLIF_stream_t *marpaESLIF_streamp;

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  marpaESLIF_streamp = marpaESLIFRecognizerp->marpaESLIF_streamp;

  if (linelp != NULL) {
    *linelp = marpaESLIF_streamp->linel;
  }
  if (columnlp != NULL) {
    *columnlp = marpaESLIF_streamp->columnl;
  }

  return 1;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_last_lexemeDatab(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char *lexemes, char **bytesp, size_t *bytelp, marpaESLIF_lexeme_data_t **lexemeDatapp, short forPauseb)
/*****************************************************************************/
{
  marpaESLIF_t              *marpaESLIFp;
  marpaESLIFGrammar_t       *marpaESLIFGrammarp;
  marpaESLIF_grammar_t      *grammarp;
  marpaESLIFSymbol_t        *symbolp;
  marpaESLIF_lexeme_data_t  *lexemeDatap;
  short                      conditionb;
  char                      *bytes;
  size_t                     bytel;
  short                      rcb;

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    goto err;
  }

  marpaESLIFp        = marpaESLIFRecognizerp->marpaESLIFp;
  marpaESLIFGrammarp = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  grammarp           = marpaESLIFGrammarp->grammarp;

  if (lexemes == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Lexeme name is NULL");
    errno = EINVAL;
    goto err;
  }

  symbolp = _marpaESLIF_symbol_findp(marpaESLIFp, grammarp, lexemes, -1, NULL /* symbolip */);
  if (symbolp == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "Failed to find <%s>", lexemes);
    goto err;
  }
  if (forPauseb) {
    /* Any lexeme that as an event (grammar validation made sure that only lexemes can have such events) */
    conditionb = (symbolp->eventBefores != NULL) || (symbolp->eventAfters != NULL);
  } else {
    /* Any symbol that is a lexeme or the :discard entry */
    conditionb = MARPAESLIF_IS_LEXEME(symbolp) || MARPAESLIF_IS_DISCARD(symbolp);
  }

  lexemeDatap = lexemeDatapp[symbolp->idi];
  if (lexemeDatap == NULL) {
    /* This is an error unless conditionb is true - then it means it was not set */
    if (! conditionb) {
      MARPAESLIF_ERRORF(marpaESLIFp, "Symbol <%s> has no data setting", lexemes);
      errno = EINVAL;
      goto err;
    }
    bytes = NULL;
    bytel = 0;
  } else {
    bytes = lexemeDatap->bytes;
    bytel = lexemeDatap->bytel;
  }
  
  if (bytesp != NULL) {
    *bytesp = bytes;
  }
  if (bytelp != NULL) {
    *bytelp = bytel;
  }
  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_discard_lastb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char **bytesp, size_t *bytelp)
/*****************************************************************************/
{
  short rcb;

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    goto err;
  }

  if (bytesp != NULL) {
    *bytesp = marpaESLIFRecognizerp->lastDiscards;
  }
  if (bytelp != NULL) {
    *bytelp = marpaESLIFRecognizerp->lastDiscardl;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIFRecognizer_lexeme_last_pauseb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char *lexemes, char **pausesp, size_t *pauselp)
/*****************************************************************************/
{
  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return _marpaESLIFRecognizer_last_lexemeDatab(marpaESLIFRecognizerp, lexemes, pausesp, pauselp, marpaESLIFRecognizerp->lastPausepp, 1 /* forPauseb */);
}

/*****************************************************************************/
short marpaESLIFRecognizer_lexeme_last_tryb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char *lexemes, char **trysp, size_t *trylp)
/*****************************************************************************/
{
  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return _marpaESLIFRecognizer_last_lexemeDatab(marpaESLIFRecognizerp, lexemes, trysp, trylp, marpaESLIFRecognizerp->lastTrypp, 0 /* forPauseb */);
}

/*****************************************************************************/
short marpaESLIFRecognizer_discard_last_tryb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char **trysp, size_t *trylp)
/*****************************************************************************/
{
  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return _marpaESLIFRecognizer_last_lexemeDatab(marpaESLIFRecognizerp, ":discard", trysp, trylp, marpaESLIFRecognizerp->lastTrypp, 0 /* forPauseb */);
}

/*****************************************************************************/
short marpaESLIFRecognizer_discard_lastb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char **trysp, size_t *trylp)
/*****************************************************************************/
{
  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return _marpaESLIFRecognizer_discard_lastb(marpaESLIFRecognizerp, trysp, trylp);
}

/*****************************************************************************/
static int _marpaESLIF_event_sorti(const void *p1, const void *p2)
/*****************************************************************************/
{
  marpaESLIFEvent_t *event1p = (marpaESLIFEvent_t *) p1;
  marpaESLIFEvent_t *event2p = (marpaESLIFEvent_t *) p2;
  int                rci;
  /* The order is:

     MARPAESLIF_EVENTTYPE_PREDICTED
     MARPAESLIF_EVENTTYPE_BEFORE
     MARPAESLIF_EVENTTYPE_NULLED
     MARPAESLIF_EVENTTYPE_AFTER
     MARPAESLIF_EVENTTYPE_COMPLETED
     MARPAESLIF_EVENTTYPE_DISCARD
     MARPAESLIF_EVENTTYPE_EXHAUSTED
     else
       no order
  */

  switch (event1p->type) {
  case MARPAESLIF_EVENTTYPE_NONE: /* Happen when we clear grammar events and repush them */
    /* Not absolutely, this should be rci == 0 when event2p->type is NONE, but this has */
    /* no consequence: NONE is at the very end - by doing this we avoid a sub-switch -; */
    rci = 1;
    /*
    switch (event2p->type) {
    case MARPAESLIF_EVENTTYPE_PREDICTED:  rci =  1; break;
    case MARPAESLIF_EVENTTYPE_BEFORE:     rci =  1; break;
    case MARPAESLIF_EVENTTYPE_NULLED:     rci =  1; break;
    case MARPAESLIF_EVENTTYPE_AFTER:      rci =  1; break;
    case MARPAESLIF_EVENTTYPE_COMPLETED:  rci =  1; break;
    case MARPAESLIF_EVENTTYPE_DISCARD:    rci =  1; break;
    case MARPAESLIF_EVENTTYPE_EXHAUSTED:  rci =  1; break;
    case MARPAESLIF_EVENTTYPE_NONE:       rci =  0; break;
    default:                              rci =  0; break;
    }
    */
    break;
  case MARPAESLIF_EVENTTYPE_PREDICTED:
    switch (event2p->type) {
    case MARPAESLIF_EVENTTYPE_PREDICTED:  rci =  0; break;
    case MARPAESLIF_EVENTTYPE_BEFORE:     rci = -1; break;
    case MARPAESLIF_EVENTTYPE_NULLED:     rci = -1; break;
    case MARPAESLIF_EVENTTYPE_AFTER:      rci = -1; break;
    case MARPAESLIF_EVENTTYPE_COMPLETED:  rci = -1; break;
    case MARPAESLIF_EVENTTYPE_DISCARD:    rci = -1; break;
    case MARPAESLIF_EVENTTYPE_EXHAUSTED:  rci = -1; break;
    case MARPAESLIF_EVENTTYPE_NONE:       rci = -1; break;
    default:                              rci =  0; break; /* Should never happen */
    }
    break;
  case MARPAESLIF_EVENTTYPE_BEFORE:
    switch (event2p->type) {
    case MARPAESLIF_EVENTTYPE_PREDICTED:  rci =  1; break;
    case MARPAESLIF_EVENTTYPE_BEFORE:     rci =  0; break;
    case MARPAESLIF_EVENTTYPE_NULLED:     rci = -1; break;
    case MARPAESLIF_EVENTTYPE_AFTER:      rci = -1; break;
    case MARPAESLIF_EVENTTYPE_COMPLETED:  rci = -1; break;
    case MARPAESLIF_EVENTTYPE_DISCARD:    rci = -1; break;
    case MARPAESLIF_EVENTTYPE_EXHAUSTED:  rci = -1; break;
    case MARPAESLIF_EVENTTYPE_NONE:       rci = -1; break;
    default:                              rci =  0; break; /* Should never happen */
    }
    break;
  case MARPAESLIF_EVENTTYPE_NULLED:
    switch (event2p->type) {
    case MARPAESLIF_EVENTTYPE_PREDICTED:  rci =  1; break;
    case MARPAESLIF_EVENTTYPE_BEFORE:     rci =  1; break;
    case MARPAESLIF_EVENTTYPE_NULLED:     rci =  0; break;
    case MARPAESLIF_EVENTTYPE_AFTER:      rci = -1; break;
    case MARPAESLIF_EVENTTYPE_COMPLETED:  rci = -1; break;
    case MARPAESLIF_EVENTTYPE_DISCARD:    rci = -1; break;
    case MARPAESLIF_EVENTTYPE_EXHAUSTED:  rci = -1; break;
    case MARPAESLIF_EVENTTYPE_NONE:       rci = -1; break;
    default:                              rci =  0; break; /* Should never happen */
    }
    break;
  case MARPAESLIF_EVENTTYPE_AFTER:
    switch (event2p->type) {
    case MARPAESLIF_EVENTTYPE_PREDICTED:  rci =  1; break;
    case MARPAESLIF_EVENTTYPE_BEFORE:     rci =  1; break;
    case MARPAESLIF_EVENTTYPE_NULLED:     rci =  1; break;
    case MARPAESLIF_EVENTTYPE_AFTER:      rci =  0; break;
    case MARPAESLIF_EVENTTYPE_COMPLETED:  rci = -1; break;
    case MARPAESLIF_EVENTTYPE_DISCARD:    rci = -1; break;
    case MARPAESLIF_EVENTTYPE_EXHAUSTED:  rci = -1; break;
    case MARPAESLIF_EVENTTYPE_NONE:       rci = -1; break;
    default:                              rci =  0; break; /* Should never happen */
    }
    break;
  case MARPAESLIF_EVENTTYPE_COMPLETED:
    switch (event2p->type) {
    case MARPAESLIF_EVENTTYPE_PREDICTED:  rci =  1; break;
    case MARPAESLIF_EVENTTYPE_BEFORE:     rci =  1; break;
    case MARPAESLIF_EVENTTYPE_NULLED:     rci =  1; break;
    case MARPAESLIF_EVENTTYPE_AFTER:      rci =  1; break;
    case MARPAESLIF_EVENTTYPE_COMPLETED:  rci =  0; break;
    case MARPAESLIF_EVENTTYPE_DISCARD:    rci = -1; break;
    case MARPAESLIF_EVENTTYPE_EXHAUSTED:  rci = -1; break;
    case MARPAESLIF_EVENTTYPE_NONE:       rci = -1; break;
    default:                              rci =  0; break; /* Should never happen */
    }
    break;
  case MARPAESLIF_EVENTTYPE_DISCARD:
    switch (event2p->type) {
    case MARPAESLIF_EVENTTYPE_PREDICTED:  rci =  1; break;
    case MARPAESLIF_EVENTTYPE_BEFORE:     rci =  1; break;
    case MARPAESLIF_EVENTTYPE_NULLED:     rci =  1; break;
    case MARPAESLIF_EVENTTYPE_AFTER:      rci =  1; break;
    case MARPAESLIF_EVENTTYPE_COMPLETED:  rci =  1; break;
    case MARPAESLIF_EVENTTYPE_DISCARD:    rci =  0; break;
    case MARPAESLIF_EVENTTYPE_EXHAUSTED:  rci = -1; break;
    case MARPAESLIF_EVENTTYPE_NONE:       rci = -1; break;
    default:                              rci =  0; break; /* Should never happen */
    }
    break;
  case MARPAESLIF_EVENTTYPE_EXHAUSTED: /* Always at the very end, except if event2p->type is NONE */
    switch (event2p->type) {
    case MARPAESLIF_EVENTTYPE_PREDICTED:  rci =  1; break;
    case MARPAESLIF_EVENTTYPE_BEFORE:     rci =  1; break;
    case MARPAESLIF_EVENTTYPE_NULLED:     rci =  1; break;
    case MARPAESLIF_EVENTTYPE_AFTER:      rci =  1; break;
    case MARPAESLIF_EVENTTYPE_COMPLETED:  rci =  1; break;
    case MARPAESLIF_EVENTTYPE_DISCARD:    rci =  1; break;
    case MARPAESLIF_EVENTTYPE_EXHAUSTED:  rci =  0; break;
    case MARPAESLIF_EVENTTYPE_NONE:       rci = -1; break;
    default:                              rci =  0; break; /* Should never happen */
    }
    break;
  default: /* Should never happen */
    rci = 0;
    break;
  }

  return rci;
}

/*****************************************************************************/
short marpaESLIFRecognizer_last_completedb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char *names, char **offsetpp, size_t *lengthlp)
/*****************************************************************************/
{
  /* This method work only for the CURRENT grammar of CURRENT recognizer */
  marpaESLIF_t                     *marpaESLIFp;
  marpaWrapperRecognizer_t         *marpaWrapperRecognizerp;
  marpaESLIFGrammar_t              *marpaESLIFGrammarp;
  marpaESLIF_grammar_t             *grammarp;
  genericStack_t                   *set2InputStackp;
  marpaESLIF_symbol_t              *symbolp;
  short                             rcb;
  int                               latestEarleySetIdi;
  int                               earleySetIdi;
  marpaWrapperRecognizerProgress_t *progressp;
  size_t                            nProgressl;
  size_t                            progressl;
  int                               rulei;
  int                               positioni;
  int                               origini;
  int                               firstOrigini;
  int                               lhsRuleStacki;
  genericStack_t                   *lhsRuleStackp;
  short                             lhsRuleStackb;
  marpaESLIF_rule_t                *rulep;
  int                               starti;
  int                               lengthi;
  int                               endi;
  char                             *offsetp;
  size_t                            lengthl;
  GENERICSTACKITEMTYPE2TYPE_ARRAY   array[2];
  char                             *firstStartPositionp;
  char                             *lastStartPositionp;
  size_t                            lastLengthl;

  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }
  marpaESLIFp             = marpaESLIFRecognizerp->marpaESLIFp;
  marpaWrapperRecognizerp = marpaESLIFRecognizerp->marpaWrapperRecognizerp;
  if (! marpaESLIFRecognizerp->marpaESLIFRecognizerOption.trackb) {
    /* Is that information available ? */
    MARPAESLIF_ERROR(marpaESLIFp, "Last completion information is available only if recognizer is instanciated with the trackb option on");
    errno = ENOSYS;
    goto err;
  }
  
  marpaESLIFGrammarp      = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  grammarp                = marpaESLIFGrammarp->grammarp;
  set2InputStackp         = marpaESLIFRecognizerp->set2InputStackp;

  if (names == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "Symbol name is NULL");
    errno = EINVAL;
    goto err;
  }

  /* First look for this symbol */
  symbolp = _marpaESLIF_symbol_findp(marpaESLIFp, grammarp, names, -1, NULL /* symbolip */);
  if (symbolp == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "No such symbol <%s>", names);
    goto err;
  }
  lhsRuleStackp = symbolp->lhsRuleStackp;

  if (!  marpaWrapperRecognizer_latestb(marpaWrapperRecognizerp, &latestEarleySetIdi)) {
    goto err;
  }
  earleySetIdi = latestEarleySetIdi;

  /* Initialize to one past the end, so we can tell if there were no hits */
  firstOrigini = latestEarleySetIdi + 1;
  while (earleySetIdi >= 0) {
    if (! marpaWrapperRecognizer_progressb(marpaWrapperRecognizerp, earleySetIdi, -1, &nProgressl, &progressp)) {
      goto err;
    }
    for (progressl = 0; progressl < nProgressl; progressl++) {
      rulei     = progressp[progressl].rulei;
      positioni = progressp[progressl].positioni;
      origini   = progressp[progressl].earleySetOrigIdi;

      if (positioni != -1) {
        continue;
      }
      lhsRuleStackb = 0;
      for (lhsRuleStacki = 0; lhsRuleStacki < GENERICSTACK_USED(lhsRuleStackp); lhsRuleStacki++) {
        rulep = (marpaESLIF_rule_t *) GENERICSTACK_GET_PTR(lhsRuleStackp, lhsRuleStacki);
        if (rulep->idi == rulei) {
          lhsRuleStackb = 1;
          break;
        }
      }
      if (! lhsRuleStackb) {
        continue;
      }
      if (origini >= firstOrigini) {
        continue;
      }
      firstOrigini = origini;
    }
    if (firstOrigini <= latestEarleySetIdi) {
      break;
    }
    earleySetIdi--;
  }

  if (earleySetIdi < 0) {
    /* Not found */
    MARPAESLIF_ERRORF(marpaESLIFp, "No match for <%s> in input stack", names);
    errno = ENOENT;
    goto err;
  }

  starti  = firstOrigini;
  lengthi = earleySetIdi - firstOrigini;
  endi    = firstOrigini + lengthi - 1;

#ifndef MARPAESLIF_NTRACE
  /* Should never happen */
  if (! GENERICSTACK_IS_ARRAY(set2InputStackp, starti)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "No entry in set2InputStackp at indice %d", starti);
    goto err;
  }
  if (! GENERICSTACK_IS_ARRAY(set2InputStackp, endi)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "No entry in set2InputStackp at indice %d", endi);
    goto err;
  }
#endif

  array[0] = GENERICSTACK_GET_ARRAY(set2InputStackp, starti);
  array[1] = GENERICSTACK_GET_ARRAY(set2InputStackp, endi);

  firstStartPositionp = (char *) GENERICSTACK_ARRAY_PTR(array[0]);
  lastStartPositionp  = (char *) GENERICSTACK_ARRAY_PTR(array[1]);
  lastLengthl         =          GENERICSTACK_ARRAY_LENGTH(array[1]);

  offsetp = (char *) firstStartPositionp;
  lengthl = (size_t) (lastStartPositionp + lastLengthl - firstStartPositionp);

  if (offsetpp != NULL) {
    *offsetpp = offsetp;
  }
  if (lengthlp != NULL) {
    *lengthlp = lengthl;
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
short marpaESLIFRecognizer_hook_discardb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short discardOnOffb)
/*****************************************************************************/
{
  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return _marpaESLIFRecognizer_hook_discardb(marpaESLIFRecognizerp, discardOnOffb);
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_hook_discardb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short discardOnOffb)
/*****************************************************************************/
{
  static const char *funcs = "_marpaESLIFRecognizer_hook_discardb";

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Setting marpaESLIFRecognizerp->discardOnOffb to %d", (int) discardOnOffb ? 1 : 0);

  marpaESLIFRecognizerp->discardOnOffb = discardOnOffb ? 1 : 0;

  return 1;
}

/*****************************************************************************/
short marpaESLIFRecognizer_hook_discard_switchb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  if (marpaESLIFRecognizerp == NULL) {
    errno = EINVAL;
    return 0;
  }

  return _marpaESLIFRecognizer_hook_discard_switchb(marpaESLIFRecognizerp);
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_hook_discard_switchb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  static const char *funcs = "_marpaESLIFRecognizer_hook_discard_switchb";

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Setting marpaESLIFRecognizerp->discardOnOffb to %d", (int) marpaESLIFRecognizerp->discardOnOffb);

  marpaESLIFRecognizerp->discardOnOffb = marpaESLIFRecognizerp->discardOnOffb ? 0 : 1;

  return 1;
}

/*****************************************************************************/
static short _marpaESLIFRecognizer_value_validb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIFValueResult_t *marpaESLIFValueResultp, void *userDatavp, _marpaESLIFRecognizer_valueResultCallback_t callbackp)
/*****************************************************************************/
{
  static const char       *funcs                       = "_marpaESLIFRecognizer_value_validb";
  genericStack_t           todoStack;
  genericStack_t          *todoStackp = &(todoStack);
  short                    rcb;
  size_t                   i;
  marpaESLIFValueResult_t *marpaESLIFValueResultWorkp;

  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  GENERICSTACK_INIT(todoStackp);
  if (GENERICSTACK_ERROR(todoStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "todoStackp initialization failure, %s", strerror(errno));
    todoStackp = NULL;
    goto err;
  }

  /* - We want to make sure this is a known type */
  /* - We want to avoid recursion within the _marpaESLIF_generic_action___concatb() method */
  /*   that is working in both lexeme and value modes. */
  /* In any case, the important thing is: */
  /* - an alternative cannot refer to an alternative and so on. */

  /* Initialize the worklist */
  GENERICSTACK_PUSH_PTR(todoStackp, marpaESLIFValueResultp);
  if (GENERICSTACK_ERROR(todoStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "todoStackp push failure, %s", strerror(errno));
    goto err;
  }

  /* Iterate the worklist */
  while (GENERICSTACK_USED(todoStackp) > 0) {
    marpaESLIFValueResultWorkp = (marpaESLIFValueResult_t *) GENERICSTACK_POP_PTR(todoStackp);

    switch (marpaESLIFValueResultWorkp->type) {
    case MARPAESLIF_VALUE_TYPE_UNDEF:
    case MARPAESLIF_VALUE_TYPE_CHAR:
    case MARPAESLIF_VALUE_TYPE_SHORT:
    case MARPAESLIF_VALUE_TYPE_INT:
    case MARPAESLIF_VALUE_TYPE_LONG:
    case MARPAESLIF_VALUE_TYPE_FLOAT:
    case MARPAESLIF_VALUE_TYPE_DOUBLE:
    case MARPAESLIF_VALUE_TYPE_LONG_DOUBLE:
#ifdef MARPAESLIF_HAVE_LONG_LONG
    case MARPAESLIF_VALUE_TYPE_LONG_LONG:
#endif
      break;
    case MARPAESLIF_VALUE_TYPE_PTR:
      if (marpaESLIFValueResultWorkp->u.p.p != NULL) {
        if ((! marpaESLIFValueResultWorkp->u.p.shallowb) && (marpaESLIFValueResultWorkp->u.p.freeCallbackp == NULL)) {
          MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "MARPAESLIF_VALUE_TYPE_PTR: non-shallow pointer is set but free callback is not set");
          errno = EINVAL;
          goto err;
        }
      }
      break;
    case MARPAESLIF_VALUE_TYPE_ARRAY:
      if (marpaESLIFValueResultWorkp->u.a.p != NULL) {
        if ((! marpaESLIFValueResultWorkp->u.a.shallowb) && (marpaESLIFValueResultWorkp->u.a.freeCallbackp == NULL)) {
          MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "MARPAESLIF_VALUE_TYPE_ARRAY: non-shallow pointer is set but free callback is not set");
          errno = EINVAL;
          goto err;
        }
      } else {
        if (marpaESLIFValueResultWorkp->u.a.sizel > 0) {
          /* This is legal only when there is no parent recognizer: sub recognizers uses this illegal value */
          if (marpaESLIFRecognizerp->parentRecognizerp == NULL) {
            MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "MARPAESLIF_VALUE_TYPE_ARRAY: pointer is not set but array size is set to %ld", (unsigned long) marpaESLIFValueResultWorkp->u.a.sizel);
            errno = EINVAL;
            goto err;
          }
        }
      }
      break;
    case MARPAESLIF_VALUE_TYPE_BOOL:
      break;
    case MARPAESLIF_VALUE_TYPE_STRING:
      /* A string MUST have p and encodingasciis != NULL, even when this is an empty string */
      /* (in which case the caller can allocate a dummy one byte, or return a fixed adress) */
      if (marpaESLIFValueResultWorkp->u.s.p == NULL) {
        MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "MARPAESLIF_VALUE_TYPE_STRING: pointer is not set");
        errno = EINVAL;
        goto err;
      }
      if (marpaESLIFValueResultWorkp->u.s.encodingasciis == NULL) {
        MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "MARPAESLIF_VALUE_TYPE_STRING: encoding is not set");
        errno = EINVAL;
        goto err;
      }
      if ((! marpaESLIFValueResultWorkp->u.s.shallowb) && (marpaESLIFValueResultWorkp->u.s.freeCallbackp == NULL)) {
        MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "MARPAESLIF_VALUE_TYPE_STRING: non-shallow flag is set but free callback is not set");
        errno = EINVAL;
        goto err;
      }
      /* In any case, even for an empty string, encoding must be set */
      break;
    case MARPAESLIF_VALUE_TYPE_ROW:
      if (marpaESLIFValueResultWorkp->u.r.p != NULL) {
        if ((! marpaESLIFValueResultWorkp->u.r.shallowb) && (marpaESLIFValueResultWorkp->u.r.freeCallbackp == NULL)) {
          MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "MARPAESLIF_VALUE_TYPE_ROW: non-shallow pointer is set but free callback is not set");
          errno = EINVAL;
          goto err;
        }
        if (marpaESLIFValueResultWorkp->u.r.sizel <= 0) {
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "MARPAESLIF_VALUE_TYPE_ROW: pointer is set but size is %ld", (unsigned long) marpaESLIFValueResultWorkp->u.r.sizel);
        }
      } else {
        if (marpaESLIFValueResultWorkp->u.r.sizel > 0) {
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "MARPAESLIF_VALUE_TYPE_ROW: pointer is not set but size is %ld", (unsigned long) marpaESLIFValueResultWorkp->u.r.sizel);
        }
      }

      for (i = 0; i < marpaESLIFValueResultWorkp->u.r.sizel; i++) {
        GENERICSTACK_PUSH_PTR(todoStackp, &(marpaESLIFValueResultWorkp->u.r.p[i]));
        if (GENERICSTACK_ERROR(todoStackp)) {
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "todoStackp push failure, %s", strerror(errno));
          goto err;
        }
      }
      break;
    case MARPAESLIF_VALUE_TYPE_TABLE:
      if (marpaESLIFValueResultWorkp->u.t.p != NULL) {
        if ((! marpaESLIFValueResultWorkp->u.t.shallowb) && (marpaESLIFValueResultWorkp->u.t.freeCallbackp == NULL)) {
          MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "MARPAESLIF_VALUE_TYPE_TABLE: non-shallow pointer is set but free callback is not set");
          errno = EINVAL;
          goto err;
        }
        if (marpaESLIFValueResultWorkp->u.t.sizel <= 0) {
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "MARPAESLIF_VALUE_TYPE_TABLE: pointer is set but size is %ld", (unsigned long) marpaESLIFValueResultWorkp->u.t.sizel);
        }
      } else {
        if (marpaESLIFValueResultWorkp->u.t.sizel > 0) {
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "MARPAESLIF_VALUE_TYPE_TABLE: pointer is not set but size is %ld", (unsigned long) marpaESLIFValueResultWorkp->u.t.sizel);
        }
      }

      for (i = 0; i < marpaESLIFValueResultWorkp->u.t.sizel; i++) {
        GENERICSTACK_PUSH_PTR(todoStackp, &(marpaESLIFValueResultWorkp->u.t.p[i].key));
        if (GENERICSTACK_ERROR(todoStackp)) {
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "todoStackp push failure, %s", strerror(errno));
          goto err;
        }
        GENERICSTACK_PUSH_PTR(todoStackp, &(marpaESLIFValueResultWorkp->u.t.p[i].value));
        if (GENERICSTACK_ERROR(todoStackp)) {
          MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "todoStackp push failure, %s", strerror(errno));
          goto err;
        }
      }
      break;
    default:
      MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "marpaESLIFValueResultWorkp->type is not supported (got %d, %s)", marpaESLIFValueResultWorkp->type, _marpaESLIF_value_types(marpaESLIFValueResultWorkp->type));
      errno = EINVAL;
      goto err;
    }
  }

  rcb = (callbackp != NULL) ? callbackp(userDatavp, marpaESLIFValueResultp) : 1;
  goto done;

 err:
  rcb = 0;

 done:
  if (todoStackp != NULL) {
    GENERICSTACK_RESET(todoStackp);
  }
  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "return %d", (int) rcb);
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;
  return rcb;
}

/****************************************************************************/
static inline unsigned long _marpaESLIF_djb2_s(unsigned char *str, size_t lengthl)
/****************************************************************************/
{
  unsigned long hash = 5381;
  int           c;
  size_t        i;

  for (i = 0; i < lengthl; i++) {
    c = *str++;
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
  }

  return hash;
}

/****************************************************************************/
int _marpaESLIF_ptrhashi(void *userDatavp, genericStackItemType_t itemType, void **pp)
/****************************************************************************/
{
  /* We know what we are doing, i.e. that *pp is a void* */
  return (int) (_marpaESLIF_djb2_s((unsigned char *) pp, sizeof(void *)) % MARPAESLIF_HASH_SIZE);
}


/****************************************************************************/
int _marpaESLIF_string_hash_callbacki(void *userDatavp, genericStackItemType_t itemType, void **pp)
/****************************************************************************/
{
  /* We know what we are doing, i.e. that *pp is an array of bytes containing UTF-8 characters that are only ASCII [:print:] characters */
  /* i.e. we can use that array of bytes as if it is a ( char *) - the lexer made sure it ends with a hiden NUL byte */
  return (int) (_marpaESLIF_djb2_s((unsigned char *) *pp, strlen((char *) *pp)) % MARPAESLIF_HASH_SIZE);
}

/****************************************************************************/
short _marpaESLIF_string_cmp_callbackb(void *userDatavp, void **pp1, void **pp2)
/****************************************************************************/
{
  return (strcmp((char *) *pp1, (char *) *pp2) == 0) ? 1 : 0;
}

/****************************************************************************/
void *_marpaESLIF_string_copy_callbackp(void *userDatavp, void **pp)
/****************************************************************************/
{
  return strdup((char *)  *pp);
}

/****************************************************************************/
void _marpaESLIF_string_free_callbackv(void *userDatavp, void **pp)
/****************************************************************************/
{
  free(*pp);
}

/****************************************************************************/
static void _marpaESLIFRecognizerHash_freev(void *userDatavp, void **pp)
/****************************************************************************/
{
  genericStack_t         *marpaESLIFRecognizerStackp = * (genericStack_t **) pp;
  marpaESLIFRecognizer_t *marpaESLIFRecognizerp;

  if (marpaESLIFRecognizerStackp != NULL) {
    while (GENERICSTACK_USED(marpaESLIFRecognizerStackp) > 0) {
      if (GENERICSTACK_IS_PTR(marpaESLIFRecognizerStackp, GENERICSTACK_USED(marpaESLIFRecognizerStackp) - 1)) {
	marpaESLIFRecognizerp = (marpaESLIFRecognizer_t *) GENERICSTACK_POP_PTR(marpaESLIFRecognizerStackp);
        if (marpaESLIFRecognizerp != NULL) {
          /* MARPAESLIF_DEBUGF(marpaESLIFRecognizerp->marpaESLIFp, "Recognizer %p popped from reusable stack of recognizers for grammar %p (length %d)", marpaESLIFRecognizerp, marpaESLIFRecognizerp->marpaWrapperGrammarp, GENERICSTACK_USED(marpaESLIFRecognizerStackp)); */
          _marpaESLIFRecognizer_freev(marpaESLIFRecognizerp, 1 /* forceb */);
        }
      } else {
	GENERICSTACK_USED(marpaESLIFRecognizerStackp)--;
      }
    }
    GENERICSTACK_FREE(marpaESLIFRecognizerStackp);
  }
}

/*****************************************************************************/
static inline void _marpaESLIF_stream_disposev(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  marpaESLIF_stream_t *marpaESLIF_streamp;

  if (marpaESLIFRecognizerp != NULL) {
    marpaESLIF_streamp = marpaESLIFRecognizerp->marpaESLIF_streamp;
    if (marpaESLIF_streamp == &(marpaESLIFRecognizerp->_marpaESLIF_stream)) {

      if (marpaESLIF_streamp->buffers != NULL) {
        free(marpaESLIF_streamp->buffers);
      }
      if (marpaESLIF_streamp->lastFroms != NULL) {
        free(marpaESLIF_streamp->lastFroms);
      }
      if (marpaESLIF_streamp->bytelefts != NULL) {
        free(marpaESLIF_streamp->bytelefts);
      }
      if (marpaESLIF_streamp->encodings != NULL) {
        free(marpaESLIF_streamp->encodings);
      }
      _marpaESLIF_terminal_freev(marpaESLIF_streamp->encodingp);
      if (marpaESLIF_streamp->tconvp != NULL) {
        tconv_close(marpaESLIF_streamp->tconvp);
      }
    }
  }
}

/*****************************************************************************/
static inline void _marpaESLIFRecognizer_freev(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, short forceb)
/*****************************************************************************/
{
  static const char      *funcs                       = "_marpaESLIFRecognizer_freev";
  marpaESLIFRecognizer_t *marpaESLIFRecognizerParentp = marpaESLIFRecognizerp->parentRecognizerp;
  genericHash_t          *marpaESLIFRecognizerHashp   = marpaESLIFRecognizerp->marpaESLIFRecognizerHashp;
  genericStack_t         *marpaESLIFRecognizerStackp  = marpaESLIFRecognizerp->marpaESLIFRecognizerStackp;
  marpaESLIF_stream_t    *marpaESLIF_streamp          = marpaESLIFRecognizerp->marpaESLIF_streamp;
  short                  *discardEventStatebp;
  short                  *beforeEventStatebp;
  short                  *afterEventStatebp;
  int                     i;
  void                   *p;
    
  /* We may decide to not free but say we can be reused, unless caller definitely want us to get out */
  if (! forceb) {
    if (_marpaESLIFRecognizer_putToCacheb(marpaESLIFRecognizerp)) {
      return;
    }
  }

  /* This is a normal free -; */
  
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_INC;
  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "start");

  MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Freeing recognizer %p for grammar %p", marpaESLIFRecognizerp, marpaESLIFRecognizerp->marpaWrapperGrammarp);

  _marpaESLIFrecognizer_lexemeStack_freev(marpaESLIFRecognizerp, marpaESLIFRecognizerp->lexemeInputStackp);
  _marpaESLIFRecognizer_alternativeStackSymbol_freev(marpaESLIFRecognizerp, marpaESLIFRecognizerp->alternativeStackSymbolp);
  GENERICSTACK_RESET(marpaESLIFRecognizerp->commitedAlternativeStackSymbolp); /* Take care, this is a pointer to a stack inside recognizer's structure */
  if (marpaESLIFRecognizerp->marpaESLIFRecognizerOption.trackb) {
    GENERICSTACK_RESET(marpaESLIFRecognizerp->set2InputStackp); /* Take care, this is a pointer to a stack inside recognizer's structure */
  }
  if (marpaESLIFRecognizerp->lexemesArrayp != NULL) {
    free(marpaESLIFRecognizerp->lexemesArrayp);
  }
  if (marpaESLIFRecognizerp->marpaWrapperRecognizerp != NULL) {
    marpaWrapperRecognizer_freev(marpaESLIFRecognizerp->marpaWrapperRecognizerp);
  }
  if (marpaESLIFRecognizerp->eventArrayp != NULL) {
    free(marpaESLIFRecognizerp->eventArrayp);
  }

  _marpaESLIFrecognizer_lastPause_freev(marpaESLIFRecognizerp);
  _marpaESLIFrecognizer_lastTry_freev(marpaESLIFRecognizerp);

  discardEventStatebp = marpaESLIFRecognizerp->discardEventStatebp;
  if (discardEventStatebp != NULL) {
    free(discardEventStatebp);
  }

  beforeEventStatebp = marpaESLIFRecognizerp->beforeEventStatebp;
  if (beforeEventStatebp != NULL) {
    free(beforeEventStatebp);
  }

  afterEventStatebp = marpaESLIFRecognizerp->afterEventStatebp;
  if (afterEventStatebp != NULL) {
    free(afterEventStatebp);
  }

  _marpaESLIF_stream_disposev(marpaESLIFRecognizerp);

  if (marpaESLIFRecognizerParentp == NULL) {
    if (marpaESLIFRecognizerHashp != NULL) {
      /* This will free all cached recognizers in cascade -; */
      GENERICHASH_RESET(marpaESLIFRecognizerHashp, NULL);
    }
  } else {
    /* Parent's "current" position have to be updated */
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Restoring parent stream from {%p,%ld} to {%p,%ld}", marpaESLIFRecognizerParentp->marpaESLIF_streamp->inputs, marpaESLIFRecognizerParentp->marpaESLIF_streamp->inputl, marpaESLIFRecognizerParentp->marpaESLIF_streamp->buffers + marpaESLIFRecognizerp->parentDeltal, marpaESLIFRecognizerParentp->marpaESLIF_streamp->bufferl - marpaESLIFRecognizerp->parentDeltal);
    marpaESLIFRecognizerParentp->marpaESLIF_streamp->inputs = marpaESLIFRecognizerParentp->marpaESLIF_streamp->buffers + marpaESLIFRecognizerp->parentDeltal;
    marpaESLIFRecognizerParentp->marpaESLIF_streamp->inputl = marpaESLIFRecognizerParentp->marpaESLIF_streamp->bufferl - marpaESLIFRecognizerp->parentDeltal;
  }

  if (marpaESLIFRecognizerp->afterPtrHashp != NULL) {
    GENERICHASH_RESET(marpaESLIFRecognizerp->afterPtrHashp, NULL);
  }
  if (marpaESLIFRecognizerp->beforePtrHashp != NULL) {
    GENERICHASH_RESET(marpaESLIFRecognizerp->beforePtrHashp, NULL);
  }
  if (marpaESLIFRecognizerp->beforePtrStackp != NULL) {
    GENERICSTACK_RESET(marpaESLIFRecognizerp->beforePtrStackp);
  }
  if (marpaESLIFRecognizerp->lastDiscards != NULL) {
    free(marpaESLIFRecognizerp->lastDiscards);
  }

  /* Dispose lua if needed */
  _marpaESLIFRecognizer_lua_freev(marpaESLIFRecognizerp);

  MARPAESLIFRECOGNIZER_TRACE(marpaESLIFRecognizerp, funcs, "return");
  MARPAESLIFRECOGNIZER_CALLSTACKCOUNTER_DEC;

  if ((marpaESLIFRecognizerParentp == NULL) || (marpaESLIFRecognizerStackp == NULL)) {
    /* This is truely a top recognizer, or we are called on the top recognizer after a new() failure */
    if (marpaESLIFRecognizerStackp != NULL) {
      for (i = 0; i < GENERICSTACK_USED(marpaESLIFRecognizerStackp); i++) {
        p = GENERICSTACK_GET_PTR(marpaESLIFRecognizerStackp, i);
        if (p != NULL) {
          free(p);
        }
      }
      GENERICSTACK_RESET(marpaESLIFRecognizerStackp);
    }
    free(marpaESLIFRecognizerp);
  } else {
    /* Push ourself in the stack of already allocated children */
    GENERICSTACK_PUSH_PTR(marpaESLIFRecognizerStackp, marpaESLIFRecognizerp);
    if (GENERICSTACK_ERROR(marpaESLIFRecognizerStackp)) {
      MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "marpaESLIFRecognizerStackp push failure, %s", strerror(errno));
      /* We have to free anyway - take care we do not own marpaESLIFRecognizerStackp here */
      free(marpaESLIFRecognizerp);
    }
  }
}

/*****************************************************************************/
static inline marpaESLIFRecognizer_t *_marpaESLIFRecognizer_getFromCachep(marpaESLIF_t *marpaESLIFp, marpaESLIFGrammar_t *marpaESLIFGrammarp, short discardb, short noEventb, short silentb, marpaESLIFRecognizer_t *marpaESLIFRecognizerParentp, short fakeb, short grammarIsOnStackb)
/*****************************************************************************/
{
  static const char      *funcs                 = "_marpaESLIFRecognizer_getFromCachep";
  marpaESLIFRecognizer_t *marpaESLIFRecognizerp = NULL;
  marpaESLIF_grammar_t   *grammarp;
  marpaWrapperGrammar_t  *marpaWrapperGrammarp;
  genericHash_t          *marpaESLIFRecognizerHashp;
  genericStack_t         *marpaESLIFRecognizerStackp;
  short                   findResultb;

  /* Why is marpaESLIFRecognizerOptionp not in the parameters ? Because when marpaESLIFRecognizerParentp is != NULL */
  /* marpaESLIF always use the same marpaESLIFRecognizerOptionp: disableThresholdb is 1, and exhaustionEventb is 1. */
  /* genericLoggerp is per def the same as in marpaESLIFp. So *marpaESLIFRecognizerOptionp is a constant. */

  if ((! fakeb) && (marpaESLIFRecognizerParentp != NULL)) {
    grammarp = marpaESLIFGrammarp->grammarp;
    if (discardb) {
      marpaWrapperGrammarp = noEventb ? grammarp->marpaWrapperGrammarDiscardNoEventp : grammarp->marpaWrapperGrammarDiscardp;
    } else {
      marpaWrapperGrammarp = noEventb ? grammarp->marpaWrapperGrammarStartNoEventp : grammarp->marpaWrapperGrammarStartp;
    }

    /* Key is marpaWrapperGrammarp, value is a stack of reusable recognizers */
    /* The only problematic reuse of pristine recognizers would be those that are based */
    /* on a grammar that have INITIAL events. But this can happen only once: the very top grammar */
    /* which, by definition, is always used only once for the whole lifetime of the recognizer */
    findResultb = 0;
    marpaESLIFRecognizerHashp = marpaESLIFRecognizerParentp->marpaESLIFRecognizerHashp;
    marpaESLIFRecognizerStackp = NULL;
    GENERICHASH_FIND(marpaESLIFRecognizerHashp,
                     NULL, /* userDatavp */
                     PTR,
                     marpaWrapperGrammarp,
                     PTR,
                     &marpaESLIFRecognizerStackp,
                     findResultb);
#ifndef MARPAESLIF_NTRACE
    if (findResultb && (marpaESLIFRecognizerStackp == NULL)) {
      MARPAESLIF_ERROR(marpaESLIFp, "marpaESLIFRecognizerStackp is NULL");
      return NULL;
    }
#endif
    if (findResultb) {
      if (GENERICSTACK_USED(marpaESLIFRecognizerStackp) > 0) {
        marpaESLIFRecognizerp = (marpaESLIFRecognizer_t *) GENERICSTACK_POP_PTR(marpaESLIFRecognizerStackp);
#ifndef MARPAESLIF_NTRACE
        if (findResultb && (marpaESLIFRecognizerp == NULL)) {
          MARPAESLIF_ERROR(marpaESLIFp, "GENERICSTACK_POP_PTR(marpaESLIFRecognizerStackp) returned NULL");
          return NULL;
        }
#endif
        /* Re-associate needed members */
        /* marpaESLIFRecognizerp->marpaESLIFp                  = marpaESLIFp; */
        _marpaESLIFRecognizer_redoGrammarv(marpaESLIFRecognizerp, marpaESLIFGrammarp, fakeb, grammarIsOnStackb);
        /* marpaESLIFRecognizerp->marpaESLIFRecognizerOption   = *marpaESLIFRecognizerOptionp; */
        /* marpaESLIFRecognizerp->marpaWrapperRecognizerp      = NULL; */
        /* marpaESLIFRecognizerp->marpaWrapperGrammarp         = NULL; */
        /* marpaESLIFRecognizerp->lexemeInputStackp            = NULL; */
        /* marpaESLIFRecognizerp->eventArrayp                  = NULL; */
        /* marpaESLIFRecognizerp->eventArrayl                  = 0; */
        /* marpaESLIFRecognizerp->eventArraySizel              = 0; */
        marpaESLIFRecognizerp->parentRecognizerp            = marpaESLIFRecognizerParentp;
        /* marpaESLIFRecognizerp->lastCompletionEvents         = NULL; */
        /* marpaESLIFRecognizerp->lastCompletionSymbolp        = NULL; */
        /* marpaESLIFRecognizerp->discardEvents                = NULL; */
        /* marpaESLIFRecognizerp->discardSymbolp               = NULL; */
        marpaESLIFRecognizerp->resumeCounteri               = 0;
        /* marpaESLIFRecognizerp->callstackCounteri            = 0; */
        marpaESLIFRecognizerp->leveli                       = marpaESLIFRecognizerParentp->leveli + 1;
        marpaESLIFRecognizerp->marpaESLIFRecognizerHashp    = marpaESLIFRecognizerParentp->marpaESLIFRecognizerHashp;
        marpaESLIFRecognizerp->marpaESLIF_streamp           = marpaESLIFRecognizerParentp->marpaESLIF_streamp;
        marpaESLIFRecognizerp->parentDeltal                 = marpaESLIFRecognizerParentp->marpaESLIF_streamp->inputs - marpaESLIFRecognizerParentp->marpaESLIF_streamp->buffers;
        marpaESLIFRecognizerp->scanb                        = 0;
        /* marpaESLIFRecognizerp->noEventb                     = noEventb; */
        /* marpaESLIFRecognizerp->discardb                     = discardb; */
        marpaESLIFRecognizerp->silentb                      = silentb;
        /* marpaESLIFRecognizerp->haveLexemeb                  = 0; */
        /* These variables are resetted at every _resume_oneb() */
        /* marpaESLIFRecognizerp->completedb                   = 0; */
        /* marpaESLIFRecognizerp->cannotcontinueb                 = 0; */
        /* marpaESLIFRecognizerp->alternativeStackSymbolp         = NULL; */
        /* marpaESLIFRecognizerp->commitedAlternativeStackSymbolp = NULL; */
        /* marpaESLIFRecognizerp->lastPausepp                     = NULL; */
        /* marpaESLIFRecognizerp->lastTrypp                       = NULL; */
        /* marpaESLIFRecognizerp->set2InputStackp                 = NULL; */
        /* marpaESLIFRecognizerp->lexemesArrayp                   = NULL; */
        /* marpaESLIFRecognizerp->lexemesArrayAllocl              = 0; */
        /* marpaESLIFRecognizerp->discardEventStatebp             = NULL; */
        /* marpaESLIFRecognizerp->beforeEventStatebp              = NULL; */
        /* marpaESLIFRecognizerp->afterEventStatebp               = NULL; */
        /* marpaESLIFRecognizerp->discardOnOffb                   = 1; */
        /* marpaESLIFRecognizerp->pristineb                       = 1; */
        marpaESLIFRecognizerp->grammarDiscardInitializedb         = 0;
        /*
          marpaESLIFRecognizerp->marpaESLIFGrammarDiscard           = c.f. MARPAESLIFRECOGNIZER_GRAMMARDISCARD_INITIALIZER() macro
          marpaESLIFRecognizerp->grammarDiscard                     = c.f. MARPAESLIFRECOGNIZER_GRAMMARDISCARD_INITIALIZER() macro
          marpaESLIFRecognizerp->marpaESLIFRecognizerOptionDiscard  = c.f. MARPAESLIFRECOGNIZER_GRAMMARDISCARD_INITIALIZER() macro
          marpaESLIFRecognizerp->marpaESLIFValueOptionDiscard       = c.f. MARPAESLIFRECOGNIZER_GRAMMARDISCARD_INITIALIZER() macro
          marpaESLIFRecognizerp->L                                 = NULL;
        */

        MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Recognizer %p coming from reusable stack of recognizers for grammar %p (remains %d of them)", marpaESLIFRecognizerp, marpaWrapperGrammarp, GENERICSTACK_USED(marpaESLIFRecognizerStackp));
      }
    }
  }

  return marpaESLIFRecognizerp;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_putToCacheb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp)
/*****************************************************************************/
{
  static const char         *funcs                       = "_marpaESLIFRecognizer_putToCacheb";
  marpaESLIFRecognizer_t    *marpaESLIFRecognizerParentp = marpaESLIFRecognizerp->parentRecognizerp;
  marpaESLIF_stream_t       *marpaESLIF_streamp          = marpaESLIFRecognizerp->marpaESLIF_streamp;
  genericHash_t             *marpaESLIFRecognizerHashp   = marpaESLIFRecognizerp->marpaESLIFRecognizerHashp;
  marpaWrapperGrammar_t     *marpaWrapperGrammarp        = marpaESLIFRecognizerp->marpaWrapperGrammarp;
  genericStack_t            *marpaESLIFRecognizerStackp;
  short                      findResultb;
  short                      rcb;

  if ((marpaESLIFRecognizerParentp != NULL) &&
      (marpaWrapperGrammarp != NULL)        &&
      (marpaESLIFRecognizerHashp != NULL)   &&
      (marpaESLIFRecognizerp->pristineb == 1)) {

    /* Eventually create a stack if nothing yet exist in the hash */
    findResultb = 0;
    marpaESLIFRecognizerStackp = NULL;
    GENERICHASH_FIND(marpaESLIFRecognizerHashp,
                     NULL, /* userDatavp */
                     PTR,
                     marpaWrapperGrammarp,
                     PTR,
                     &marpaESLIFRecognizerStackp,
                     findResultb);
#ifndef MARPAESLIF_NTRACE
    if (findResultb && (marpaESLIFRecognizerStackp == NULL)) {
      MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "findResultb is true but marpaESLIFRecognizerStackp is NULL");
      goto err;
    }
    if ((! findResultb) && (marpaESLIFRecognizerStackp != NULL)) {
      MARPAESLIF_ERROR(marpaESLIFRecognizerp->marpaESLIFp, "findResultb is false but marpaESLIFRecognizerStackp is != NULL");
      goto err;
    }
#endif
    if (! findResultb) {
      GENERICSTACK_NEW(marpaESLIFRecognizerStackp);
      if (GENERICSTACK_ERROR(marpaESLIFRecognizerStackp)) {
        MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "marpaESLIFRecognizerStackp initialization failure, %s", strerror(errno));
        goto err;
      }
      GENERICHASH_SET(marpaESLIFRecognizerHashp, NULL, PTR, marpaWrapperGrammarp, PTR, marpaESLIFRecognizerStackp);
      if (GENERICHASH_ERROR(marpaESLIFRecognizerHashp)) {
        MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "marpaESLIFRecognizerHashp failure, %s", strerror(errno));
        GENERICSTACK_FREE(marpaESLIFRecognizerStackp);
        goto err;
      }
    }
    /* Here in any the entry in the hash exist and is a generic stack */
    GENERICSTACK_PUSH_PTR(marpaESLIFRecognizerStackp, marpaESLIFRecognizerp);
    if (GENERICSTACK_ERROR(marpaESLIFRecognizerStackp)) {
        MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "marpaESLIFRecognizerStackp push failure, %s", strerror(errno));
        goto err;
    }
    /* Before disconnecting from the parent, we update its "current" position */
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Restoring parent stream from {%p,%ld} to {%p,%ld}", marpaESLIFRecognizerParentp->marpaESLIF_streamp->inputs, marpaESLIFRecognizerParentp->marpaESLIF_streamp->inputl, marpaESLIFRecognizerParentp->marpaESLIF_streamp->buffers + marpaESLIFRecognizerp->parentDeltal, marpaESLIFRecognizerParentp->marpaESLIF_streamp->bufferl - marpaESLIFRecognizerp->parentDeltal);
    marpaESLIFRecognizerParentp->marpaESLIF_streamp->inputs = marpaESLIFRecognizerParentp->marpaESLIF_streamp->buffers + marpaESLIFRecognizerp->parentDeltal;
    marpaESLIFRecognizerParentp->marpaESLIF_streamp->inputl = marpaESLIFRecognizerParentp->marpaESLIF_streamp->bufferl - marpaESLIFRecognizerp->parentDeltal;
    /* Now we can disconnect */
    marpaESLIFRecognizerp->parentRecognizerp = NULL;
    marpaESLIFRecognizerp->marpaESLIF_streamp = NULL;
    /* And do not forget to disconnect also the shallow pointer of recognizer's cache */
    marpaESLIFRecognizerp->marpaESLIFRecognizerHashp = NULL;
    MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "Recognizer %p pushed to reusable stack of recognizers for grammar %p (length %d)", marpaESLIFRecognizerp, marpaWrapperGrammarp, GENERICSTACK_USED(marpaESLIFRecognizerStackp));
    rcb = 1;
  } else {
    rcb = 0;
  }

  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static inline void _marpaESLIFRecognizer_redoGrammarv(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, marpaESLIFGrammar_t *marpaESLIFGrammarp, short fakeb, short grammarIsOnStackb)
/*****************************************************************************/
{
  marpaESLIF_grammar_t *grammarp;

  /* I know (because I wrote it-;) that the grammar parameter MAY be */
  /* on the stack. In case the next usage of this recognizer is a free(), we MUST maintain a */
  /* valid marpaESLIFGrammarp and marpaESLIFGrammarp->grammarp */

  if (grammarIsOnStackb) {
    /* Note that marpaESLIFGrammarp cannot be NULL, c.f. the beginning of _marpaESLIFRecognizer_newp() */
    marpaESLIFRecognizerp->_marpaESLIFGrammar = *marpaESLIFGrammarp;
    marpaESLIFRecognizerp->marpaESLIFGrammarp = &(marpaESLIFRecognizerp->_marpaESLIFGrammar);
    if (fakeb) {
      marpaESLIFRecognizerp->_marpaESLIFGrammar.grammarp = NULL;
    } else {
      grammarp = marpaESLIFRecognizerp->_marpaESLIFGrammar.grammarp;
      if (grammarp != NULL) {
        marpaESLIFRecognizerp->_grammar = *grammarp;
        marpaESLIFRecognizerp->_marpaESLIFGrammar.grammarp = &(marpaESLIFRecognizerp->_grammar);
      } else {
        marpaESLIFRecognizerp->_marpaESLIFGrammar.grammarp = NULL;
      }
    }
  } else {
    marpaESLIFRecognizerp->marpaESLIFGrammarp = marpaESLIFGrammarp;
  }
}

/*****************************************************************************/
static inline char *_marpaESLIF_action2asciis(marpaESLIF_action_t *actionp)
/*****************************************************************************/
{
  /* Caller have to make sure we are NEVER called with actionp == NULL */
  switch (actionp->type) {
  case MARPAESLIF_ACTION_TYPE_NAME:
    return actionp->u.names;
  case MARPAESLIF_ACTION_TYPE_STRING:
    return actionp->u.stringp->asciis;
  case MARPAESLIF_ACTION_TYPE_LUA:
    return actionp->u.luas;
  default:
    return (char *) MARPAESLIF_UNKNOWN_STRING;
  }
}

/*****************************************************************************/
static inline short _marpaESLIF_action_validb(marpaESLIF_t *marpaESLIFp, marpaESLIF_action_t *actionp)
/*****************************************************************************/
{
  short rcb;

  if (actionp == NULL) {
    MARPAESLIF_ERROR(marpaESLIFp, "action is NULL");
    goto err;
  }

  switch (actionp->type) {
  case MARPAESLIF_ACTION_TYPE_NAME:
    if (actionp->u.names == NULL) {
      MARPAESLIF_ERROR(marpaESLIFp, "actionp->type is MARPAESLIF_ACTION_TYPE_NAME but actionp->u.names is NULL");
      goto err;    
    }
    break;
  case MARPAESLIF_ACTION_TYPE_STRING:
    /* This is invalid for a free action */
    if (actionp->u.stringp == NULL) {
      MARPAESLIF_ERROR(marpaESLIFp, "actionp->type is MARPAESLIF_ACTION_TYPE_STRING but actionp->u.stringp is NULL");
      goto err;    
    }
    break;
  case MARPAESLIF_ACTION_TYPE_LUA:
    if (actionp->u.luas == NULL) {
      MARPAESLIF_ERROR(marpaESLIFp, "actionp->type is MARPAESLIF_ACTION_TYPE_LUA but actionp->u.luas is NULL");
      goto err;    
    }
    break;
  default:
    MARPAESLIF_ERRORF(marpaESLIFp, "Invalid actionp->type %d", actionp->type);
    goto err;    
  }

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIF_action_eqb(marpaESLIF_action_t *action1p, marpaESLIF_action_t *action2p)
/*****************************************************************************/
{
  if (action1p == NULL) {
    return (action2p == NULL);
  } else {
    if (action2p == NULL) {
      return 0;
    }
  }

  /* Here both action1p and action2p are != NULL */
  /* Safety check */
  if (action1p->type != action2p->type) {
    return 0;
  }

  /* Here types are equal */
  switch (action1p->type) {
  case MARPAESLIF_ACTION_TYPE_NAME:
    return (strcmp(action1p->u.names, action2p->u.names) == 0);
  case MARPAESLIF_ACTION_TYPE_STRING:
    return _marpaESLIF_string_utf8_eqb(action1p->u.stringp, action2p->u.stringp);
  case MARPAESLIF_ACTION_TYPE_LUA:
    return (strcmp(action1p->u.luas, action2p->u.luas) == 0);
  default:
    return 0;
  }
}

/*****************************************************************************/
static inline marpaESLIF_action_t *_marpaESLIF_action_clonep(marpaESLIF_t *marpaESLIFp, marpaESLIF_action_t *actionp)
/*****************************************************************************/
{
  marpaESLIF_action_t *dup;

  dup = (marpaESLIF_action_t *) malloc(sizeof(marpaESLIF_action_t));
  if (dup == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
    goto err;
  }

  dup->type = actionp->type;

  switch (dup->type) {
  case MARPAESLIF_ACTION_TYPE_NAME:
    dup->u.names = strdup(actionp->u.names);
    if (dup->u.names == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFp, "strdup failure, %s", strerror(errno));
      goto err;
    }
    break;
  case MARPAESLIF_ACTION_TYPE_STRING:
    dup->u.stringp = _marpaESLIF_string_clonep(marpaESLIFp, actionp->u.stringp);
    if (dup->u.stringp == NULL) {
      goto err;
    }
    break;
  case MARPAESLIF_ACTION_TYPE_LUA:
    dup->u.luas = strdup(actionp->u.luas);
    if (dup->u.luas == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFp, "strdup failure, %s", strerror(errno));
      goto err;
    }
    break;
  default:
    MARPAESLIF_ERRORF(marpaESLIFp, "Invalid actionp->type %d", actionp->type);
    goto err;    
  }

  goto done;

 err:
  _marpaESLIF_action_freev(dup);
  dup = NULL;

 done:
  return dup;
}

/*****************************************************************************/
static inline void _marpaESLIF_action_freev(marpaESLIF_action_t *actionp)
/*****************************************************************************/
{
  if (actionp != NULL) {
    switch (actionp->type) {
    case MARPAESLIF_ACTION_TYPE_NAME:
      if (actionp->u.names != NULL) {
        free(actionp->u.names);
      }
      break;
    case MARPAESLIF_ACTION_TYPE_STRING:
      _marpaESLIF_string_freev(actionp->u.stringp, 0 /* onStackb */);
      break;
    case MARPAESLIF_ACTION_TYPE_LUA:
      if (actionp->u.luas != NULL) {
        free(actionp->u.luas);
      }
      break;
    default:
      break;
    }
    free(actionp);
  }
}

/*****************************************************************************/
static inline short _marpaESLIFValue_ruleActionCallbackb(marpaESLIFValue_t *marpaESLIFValuep, char *asciishows, marpaESLIF_action_t *actionp, marpaESLIFValueRuleCallback_t *ruleCallbackpp)
/*****************************************************************************/
{
  static const char                   *funcs                 = "_marpaESLIFValue_ruleActionCallbackb";
  marpaESLIFValueOption_t              marpaESLIFValueOption = marpaESLIFValuep->marpaESLIFValueOption;
  marpaESLIFValueRuleActionResolver_t  ruleActionResolverp   = marpaESLIFValueOption.ruleActionResolverp;
  marpaESLIFRecognizer_t              *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  marpaESLIFGrammar_t                 *marpaESLIFGrammarp    = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  marpaESLIF_grammar_t                *grammarp              = marpaESLIFGrammarp->grammarp;
  marpaESLIFValueRuleCallback_t        ruleCallbackp;
  short                                rcb;

  if (marpaESLIFRecognizerp->parentRecognizerp != NULL) {
    /* Lexeme mode: everything is fixed */
    ruleCallbackp             = _marpaESLIF_lexeme_concatb;
    marpaESLIFValuep->actions = (char *) MARPAESLIF_CONCAT_INTERNAL_STRING;
    marpaESLIFValuep->stringp = NULL;
  } else {

    if (actionp == NULL) {
      /* No action ? Then take the default. */
      actionp = grammarp->defaultRuleActionp;
    }
    if (actionp == NULL) {
      /* Still no action ? */
      MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "At grammar level %d (%s): %s requires: action => action_name, or that your grammar have: :default ::= action => action_name",
                        grammarp->leveli,
                        grammarp->descp->asciis,
                        asciishows);
      goto err;
    }

    switch (actionp->type) {
    case MARPAESLIF_ACTION_TYPE_NAME:
      {
        /* Action is a normal name */
        char *names;

        marpaESLIFValuep->actions = actionp->u.names;
        marpaESLIFValuep->stringp = NULL;

        /* Get the callback pointer */

        /* If this is a built-in action, we do not need the resolver */
        names = marpaESLIFValuep->actions;
        if (strcmp(names, "::shift") == 0) {
          ruleCallbackp = _marpaESLIF_rule_action___shiftb;
        } else if (strcmp(names, "::undef") == 0) {
          ruleCallbackp = _marpaESLIF_rule_action___undefb;
        } else if (strcmp(names, "::ascii") == 0) {
          ruleCallbackp = _marpaESLIF_rule_action___asciib;
        } else if (strncmp(names, "::convert", convertl) == 0) {
          ruleCallbackp = _marpaESLIF_rule_action___convertb;
        } else if (strcmp(names, "::concat") == 0) {
          ruleCallbackp = _marpaESLIF_rule_action___concatb;
        } else if (strncmp(names, "::copy", copyl) == 0) {
          ruleCallbackp = _marpaESLIF_rule_action___copyb;
        } else if (strcmp(names, "::true") == 0) {
          ruleCallbackp = _marpaESLIF_rule_action___trueb;
        } else if (strcmp(names, "::false") == 0) {
          ruleCallbackp = _marpaESLIF_rule_action___falseb;
        } else if (strcmp(names, "::json") == 0) {
          ruleCallbackp = _marpaESLIF_rule_action___jsonb;
        } else if (strcmp(names, "::row") == 0) {
          ruleCallbackp = _marpaESLIF_rule_action___rowb;
        } else if (strcmp(names, "::table") == 0) {
          ruleCallbackp = _marpaESLIF_rule_action___tableb;
        } else if (strcmp(names, "::ast") == 0) {
          ruleCallbackp = _marpaESLIF_rule_action___astb;
        } else {
          /* Not a built-in: ask to the resolver */
          if (ruleActionResolverp == NULL) {
            MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Cannot execute action \"%s\": no rule action resolver", names);
            goto err;
          }
          ruleCallbackp = ruleActionResolverp(marpaESLIFValueOption.userDatavp, marpaESLIFValuep, names);
        }
        if (ruleCallbackp == NULL) {
          MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "%s: action \"%s\" resolved to NULL", asciishows, names);
          goto err;
        } else {
          MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s: action \"%s\" resolved to %p", asciishows, names, ruleCallbackp);
        }
      }
      break;

    case MARPAESLIF_ACTION_TYPE_STRING:
      /* String literal: this is a built-in */
      /* Action name is the ASCII best-effort translation */
      ruleCallbackp             = _marpaESLIF_rule_literal_transferb;
      marpaESLIFValuep->actions = actionp->u.stringp->asciis;
      marpaESLIFValuep->stringp = actionp->u.stringp;
      break;

    case MARPAESLIF_ACTION_TYPE_LUA:
      /* Lua action: this is a built-in */
      ruleCallbackp             = _marpaESLIFValue_lua_actionb;
      marpaESLIFValuep->actions = actionp->u.luas;
      marpaESLIFValuep->stringp = NULL;
      break;

    default:
      MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Invalid action type %d", actionp->type);
      goto err;
    }
  }

  *ruleCallbackpp = ruleCallbackp; /* Never NULL */

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFValue_symbolActionCallbackb(marpaESLIFValue_t *marpaESLIFValuep, char *asciishows, short nullableb, marpaESLIF_action_t *nullableActionp, marpaESLIFValueSymbolCallback_t *symbolCallbackpp, marpaESLIFValueRuleCallback_t *ruleCallbackpp, marpaESLIF_action_t *symbolActionp)
/*****************************************************************************/
{
  /* In case of a nullable a symbol callback can fallback to a rule callback */
  static const char                    *funcs                 = "_marpaESLIFValue_symbolActionCallbackb";
  marpaESLIFValueOption_t               marpaESLIFValueOption = marpaESLIFValuep->marpaESLIFValueOption;
  marpaESLIFValueSymbolActionResolver_t symbolActionResolverp = marpaESLIFValueOption.symbolActionResolverp;
  marpaESLIFRecognizer_t               *marpaESLIFRecognizerp = marpaESLIFValuep->marpaESLIFRecognizerp;
  marpaESLIFGrammar_t                  *marpaESLIFGrammarp    = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  marpaESLIF_grammar_t                 *grammarp              = marpaESLIFGrammarp->grammarp;
  marpaESLIFValueSymbolCallback_t       symbolCallbackp;
  marpaESLIFValueRuleCallback_t         ruleCallbackp;
  marpaESLIF_action_t                  *actionp;
  short                                 rcb;

  if (marpaESLIFRecognizerp->parentRecognizerp != NULL) {
    /* Lexeme mode: everything is fixed */
    if (nullableb) {
      symbolCallbackp           = NULL;
      ruleCallbackp             = _marpaESLIF_lexeme_concatb;
      marpaESLIFValuep->actions = (char *) MARPAESLIF_CONCAT_INTERNAL_STRING;
      marpaESLIFValuep->stringp = NULL;
    } else {
      symbolCallbackp           = _marpaESLIF_lexeme_transferb;
      ruleCallbackp             = NULL;
      marpaESLIFValuep->actions = (char *) MARPAESLIF_TRANSFER_INTERNAL_STRING;
      marpaESLIFValuep->stringp = NULL;
    }
  } else {

    if (nullableb) {
      /* This will be in reality a rule callback */
      symbolCallbackp = NULL;
      if (! _marpaESLIFValue_ruleActionCallbackb(marpaESLIFValuep, asciishows, nullableActionp, &ruleCallbackp)) {
        goto err;
      }
    } else {
      /* This will truely be a symbol callback */
      ruleCallbackp   = NULL;
      /* Symbol action is a constant at the grammar level */
      actionp = (symbolActionp != NULL) ? symbolActionp : grammarp->defaultSymbolActionp;
      if (actionp == NULL) {
        /* Still no action ? */
        MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "At grammar level %d (%s): %s requires symbol-action => action_name",
                          grammarp->leveli,
                          grammarp->descp->asciis,
                          asciishows);
        goto err;
      }

      switch (actionp->type) {
      case MARPAESLIF_ACTION_TYPE_NAME:
        {
          /* Action is a normal name */
          char *names;

          marpaESLIFValuep->actions = actionp->u.names;
          marpaESLIFValuep->stringp = NULL;

          /* Get the callback pointer */

          /* If this is a built-in action, we do not need the resolver */
          names = marpaESLIFValuep->actions;
          if (strcmp(names, "::transfer") == 0) {
            symbolCallbackp = _marpaESLIF_symbol_action___transferb;
          } else if (strcmp(names, "::undef") == 0) {
            symbolCallbackp = _marpaESLIF_symbol_action___undefb;
          } else if (strcmp(names, "::ascii") == 0) {
            symbolCallbackp = _marpaESLIF_symbol_action___asciib;
          } else if (strncmp(names, "::convert", convertl) == 0) {
            symbolCallbackp = _marpaESLIF_symbol_action___convertb;
          } else if (strcmp(names, "::concat") == 0) {
            symbolCallbackp = _marpaESLIF_symbol_action___concatb;
          } else if (strcmp(names, "::true") == 0) {
            symbolCallbackp = _marpaESLIF_symbol_action___trueb;
          } else if (strcmp(names, "::false") == 0) {
            symbolCallbackp = _marpaESLIF_symbol_action___falseb;
          } else if (strcmp(names, "::json") == 0) {
            symbolCallbackp = _marpaESLIF_symbol_action___jsonb;
          } else {
            /* Not a built-in: ask to the resolver */
            if (symbolActionResolverp == NULL) {
              MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Cannot execute symbol action \"%s\": no symbol action resolver", names);
              goto err;
            }
            symbolCallbackp = symbolActionResolverp(marpaESLIFValueOption.userDatavp, marpaESLIFValuep, names);
          }
          if (symbolCallbackp == NULL) {
            MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "%s: action \"%s\" resolved to NULL", asciishows, names);
            goto err;
          } else {
            MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s: action \"%s\" resolved to %p", asciishows, names, symbolCallbackp);
          }
        }
        break;

      case MARPAESLIF_ACTION_TYPE_STRING:
        /* String literal: this is a built-in */
        /* Action name is the ASCII best-effort translation */
        symbolCallbackp           = _marpaESLIF_symbol_literal_transferb;
        marpaESLIFValuep->actions = actionp->u.stringp->asciis;
        marpaESLIFValuep->stringp = actionp->u.stringp;
        break;

      case MARPAESLIF_ACTION_TYPE_LUA:
        /* Lua action: this is a built-in */
        symbolCallbackp           = _marpaESLIFValue_lua_symbolb;
        marpaESLIFValuep->actions = actionp->u.luas;
        marpaESLIFValuep->stringp = NULL;
        break;

      default:
        MARPAESLIF_ERRORF(marpaESLIFValuep->marpaESLIFp, "Invalid action type %d", actionp->type);
        goto err;
      }
    }
  }

  *symbolCallbackpp = symbolCallbackp; /* Can be NULL */
  *ruleCallbackpp   = ruleCallbackp;   /* Can be NULL (but both cannot be NULL) */

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
static inline short _marpaESLIFRecognizer_recognizerIfActionCallbackb(marpaESLIFRecognizer_t *marpaESLIFRecognizerp, char *asciishows, marpaESLIF_action_t *ifActionp, marpaESLIFRecognizerIfCallback_t *ifCallbackpp)
/*****************************************************************************/
{
  static const char                      *funcs                      = "_marpaESLIFRecognizer_recognizerIfActionCallbackb";
  marpaESLIFRecognizerOption_t            marpaESLIFRecognizerOption = marpaESLIFRecognizerp->marpaESLIFRecognizerOption;
  marpaESLIFRecognizerIfActionResolver_t  ifActionResolverp          = marpaESLIFRecognizerOption.ifActionResolverp;
  marpaESLIFGrammar_t                    *marpaESLIFGrammarp         = marpaESLIFRecognizerp->marpaESLIFGrammarp;
  marpaESLIF_grammar_t                   *grammarp                   = marpaESLIFGrammarp->grammarp;
  marpaESLIFRecognizerIfCallback_t        ifCallbackp                = NULL;
  char                                   *ifactions;
  short                                   rcb;

  switch (ifActionp->type) {
  case MARPAESLIF_ACTION_TYPE_NAME:
    /* Action is a normal name */
    ifactions = ifActionp->u.names;
    /* Get the callback pointer */
    if (ifActionResolverp == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "Cannot execute if action \"%s\": no if action resolver", ifactions);
      goto err;
    }
    ifCallbackp = ifActionResolverp(marpaESLIFRecognizerOption.userDatavp, marpaESLIFRecognizerp, ifactions);
    if (ifCallbackp == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "%s: action \"%s\" resolved to NULL", asciishows, ifactions);
      goto err;
    } else {
      MARPAESLIFRECOGNIZER_TRACEF(marpaESLIFRecognizerp, funcs, "%s: action \"%s\" resolved to %p", asciishows, ifactions, ifCallbackp);
    }
    break;
  case MARPAESLIF_ACTION_TYPE_LUA:
    /* Lua action: this is a built-in */
    ifCallbackp                      = _marpaESLIFRecognizer_lua_ifactionb;
    marpaESLIFRecognizerp->ifactions = ifActionp->u.luas;
    break;
  default:
    MARPAESLIF_ERRORF(marpaESLIFRecognizerp->marpaESLIFp, "Invalid action type %d", ifActionp->type);
    goto err;
  }

  *ifCallbackpp = ifCallbackp;

  rcb = 1;
  goto done;

 err:
  rcb = 0;

 done:
  return rcb;
}

/*****************************************************************************/
char *marpaESLIF_charconvp(marpaESLIF_t *marpaESLIFp, char *toEncodings, char *fromEncodings, char *srcs, size_t srcl, size_t *dstlp)
/*****************************************************************************/
{
  return _marpaESLIF_charconvb(marpaESLIFp, toEncodings, fromEncodings, srcs, srcl, dstlp, NULL /* fromEncodingsp */, NULL /* tconvpp */, 1 /* eofb */, NULL /* byteleftsp */, NULL /* byteleftlp */, NULL /* byteleftalloclp */, 0 /* tconvsilentb */);
}

/*****************************************************************************/
static inline marpaESLIF_string_t *_marpaESLIF_string2utf8p(marpaESLIF_t *marpaESLIFp, marpaESLIF_string_t *stringp, short tconvsilentb)
/*****************************************************************************/
/* Take care: this method can return stringp */
/*****************************************************************************/
{
  static const char   *funcs = "_marpaESLIF_string2utf8p";
  marpaESLIF_string_t *rcp   = NULL;
  char                *fromencodingasciis;
  int                  utf82ordi;
  char                *maxp;
  char                *p;
  marpaESLIF_uint32_t  codepointi;
  short                utf8b;

  if (stringp == NULL) {
    errno = EINVAL;
    MARPAESLIF_ERRORF(marpaESLIFp, "%s failure, %s", funcs, strerror(errno));
    goto err;
  }

  /* When stringp->encodingasciis is MARPAESLIF_UTF8_STRING this is a string that marpaESLIF generated. */
  /* Then we know it is a valid UTF-8. No need to validate it. */
  if (stringp->encodingasciis == (char *) MARPAESLIF_UTF8_STRING) {
    rcp = stringp;
  } else {
    rcp = (marpaESLIF_string_t *) malloc(sizeof(marpaESLIF_string_t));
    if (rcp == NULL) {
      MARPAESLIF_ERRORF(marpaESLIFp, "malloc failure, %s", strerror(errno));
      goto err;
    }

    if ((stringp->bytep == NULL) || (stringp->bytel <= 0)) {
      /* Empty string */
      rcp->bytep          = (char *) MARPAESLIF_EMPTY_STRING;
      rcp->bytel          = 0;
      rcp->encodingasciis = (char *) MARPAESLIF_UTF8_STRING;
      rcp->asciis         = (char *) MARPAESLIF_EMPTY_STRING;
    } else {
      rcp->bytep          = NULL;
      rcp->bytel          = 0;
      rcp->encodingasciis = NULL;
      rcp->asciis         = NULL;

      /* When there is no encoding, we know that if the buffer is too small charset detection */
      /* can fail. We protect against this case in known situations. */
      fromencodingasciis = stringp->encodingasciis;
      if (fromencodingasciis == NULL) {
        p = stringp->bytep;
        maxp = p + stringp->bytel;
        utf8b = 1;
        while (p < maxp) {
          utf82ordi = _marpaESLIF_utf82ordi((PCRE2_SPTR8) p, &codepointi);
          if (utf82ordi <= 0) {
            utf8b = 0;
            break;
          }
          p += utf82ordi;
        }
        if (utf8b) {
          MARPAESLIF_TRACE(marpaESLIFp, funcs, "UTF-8 string detected using byte lookup");
          fromencodingasciis = (char *) MARPAESLIF_UTF8_STRING;
        }
      }

      /* No need of rcp->asciis, this is why we do not use _marpaESLIF_string_newp() */
      if ((rcp->bytep = _marpaESLIF_charconvb(marpaESLIFp, (char *) MARPAESLIF_UTF8_STRING, fromencodingasciis, stringp->bytep, stringp->bytel, &(rcp->bytel), &(rcp->encodingasciis), NULL /* tconvpp */, 1 /* eofb */, NULL /* byteleftsp */, NULL /* byteleftlp */, NULL /* byteleftalloclp */, tconvsilentb)) == NULL) {
        goto err;
      }

        /* We send the whole data in one go: we ignore the fact that _marpaESLIF_string_removebomb() may return -1 */
      if (! _marpaESLIF_string_removebomb(marpaESLIFp, rcp->bytep, &(rcp->bytel), (char *) MARPAESLIF_UTF8_STRING, NULL /* bomsizelp */)) {
        goto err;
      }
    }
  }

  goto done;

 err:
  if ((rcp != NULL) && (rcp != stringp)) {
    _marpaESLIF_string_freev(rcp, 0 /* onStackb */);
    rcp = NULL;
  }

 done:
  return rcp;
}

/*****************************************************************************/
static inline short _marpaESLIF_string_removebomb(marpaESLIF_t *marpaESLIFp, char *bytep, size_t *bytelp, char *encodingasciis, size_t *bomsizelp)
/*****************************************************************************/
/* Take care: it returns -1 if there is no failure BUT not enough bytes */
/*****************************************************************************/
{
  size_t  bytel                = (bytelp != NULL) ? *bytelp : 0;
  size_t  bomsizel             = 0;
  char   *encodingasciitofrees = NULL;
  char   *bytetofreep          = NULL;
  char   *tmpp;
  size_t  encodingasciil;
  short   rcb;

  if ((bytep != NULL) && (bytel > 0)) {
    if (encodingasciis == NULL) {
      /* Guess the encoding by converting to UTF-8 */
      if ((tmpp = _marpaESLIF_charconvb(marpaESLIFp, (char *) MARPAESLIF_UTF8_STRING /* toEncodings */, NULL /* fromEncodings */, bytep, bytel, NULL /* bytelp */, &(encodingasciitofrees) /* fromEncodingsp */, NULL /* tconvpp */, 1 /* eofb */, NULL /* byteleftsp */, NULL /* byteleftlp */, NULL /* byteleftalloclp */, 0 /* tconvsilentb */)) == NULL) {
	goto err;
      }
      /* Per def here encodingasciitofrees is != NULL */
      encodingasciis = encodingasciitofrees;
      free(tmpp);
    }

    encodingasciil = strlen(encodingasciis);
    if (MARPAESLIF_ENCODING_IS_UTF8(encodingasciis, encodingasciil)) {
      if (bytel >= 3) {
        if (((unsigned char) bytep[0] == (unsigned char) 0xEF) &&
            ((unsigned char) bytep[1] == (unsigned char) 0xBB) &&
            ((unsigned char) bytep[2] == (unsigned char) 0xBF)) {
          bomsizel = 3;
        }
        rcb = 1;
      } else {
        rcb = -1;
      }
    }
    else if (MARPAESLIF_ENCODING_IS_UTF16(encodingasciis, encodingasciil)) {
      if (bytel >= 2) {
        if (((unsigned char) bytep[0] == (unsigned char) 0xFE) &&
            ((unsigned char) bytep[1] == (unsigned char) 0xFF)) {
          bomsizel = 2;
        } else if (((unsigned char) bytep[0] == (unsigned char) 0xFF) &&
                   ((unsigned char) bytep[1] == (unsigned char) 0xFE)) {
          bomsizel = 2;
	}
        rcb = 1;
      } else {
        rcb = -1;
      }
    }
    else if (MARPAESLIF_ENCODING_IS_UTF32(encodingasciis, encodingasciil)) {
      if (bytel >= 4) {
        if (((unsigned char) bytep[0] == (unsigned char) 0x00) &&
            ((unsigned char) bytep[1] == (unsigned char) 0x00) &&
            ((unsigned char) bytep[2] == (unsigned char) 0xFE) &&
            ((unsigned char) bytep[3] == (unsigned char) 0xFF)) {
          bomsizel = 4;
        } else if (((unsigned char) bytep[0] == (unsigned char) 0xFF) &&
                   ((unsigned char) bytep[1] == (unsigned char) 0xFE) &&
                   ((unsigned char) bytep[2] == (unsigned char) 0x00) &&
                   ((unsigned char) bytep[3] == (unsigned char) 0x00)) {
          bomsizel = 4;
        }
        rcb = 1;
      } else {
        rcb = -1;
      }
    } else {
      rcb = 1;
    }
  
    if (bomsizel > 0) {
      memmove(bytep, bytep + bomsizel, bytel - bomsizel + 1); /* +1 for the hiden NUL byte */
      /* Per def bytelp is != NULL here */
      *bytelp -= bomsizel;
    }
  } else {
    rcb = -1;
  }

  if (rcb > 0) {
    if (bomsizelp != NULL) {
      *bomsizelp = bomsizel;
    }
  }
  goto done;

 err:
  rcb = 0;

 done:
  if (encodingasciitofrees != NULL) {
    free(encodingasciitofrees);
  }
  return rcb;
}

/*****************************************************************************/
static inline void _marpaESLIF_codepoint_to_json(marpaESLIF_uint32_t codepoint, genericLogger_t *genericLoggerp)
/*****************************************************************************/
  /* Adapted from https://github.com/nlohmann/json/blob/develop/include/nlohmann/detail/output/serializer.hpp */
/*****************************************************************************/
{
  /* fprintf(stdout, "_marpaESLIF_codepoint_to_json(0x%ld, genericLoggerp)\n", (unsigned long) codepoint); fflush(stdout); */
  switch (codepoint) {
  case 0x08: /* backspace */
    GENERICLOGGER_TRACE(genericLoggerp, "\\b");
    break;
  case 0x09: /* horizontal tab */
    GENERICLOGGER_TRACE(genericLoggerp, "\\t");
    break;
  case 0x0A: /* newline */
    GENERICLOGGER_TRACE(genericLoggerp, "\\n");
    break;
  case 0x0C: /* formfeed */
    GENERICLOGGER_TRACE(genericLoggerp, "\\f");
    break;
  case 0x0D: /* carriage return */
    GENERICLOGGER_TRACE(genericLoggerp, "\\r");
    break;
  case 0x22: /* quotation mark */
    GENERICLOGGER_TRACE(genericLoggerp, "\\\"");
    break;
  case 0x5C: /* reverse solidus */
    GENERICLOGGER_TRACE(genericLoggerp, "\\\\");
    break;
  default:
    /* escape control characters (0x00..0x1F) or non-ASCII characters */
    if ((codepoint <= 0x1F) || (codepoint >= 0x7F)) {
      if (codepoint <= 0xFFFF) {
        GENERICLOGGER_TRACEF(genericLoggerp, "\\u%04x", (int) codepoint);
      } else {
        GENERICLOGGER_TRACEF(genericLoggerp, "\\u%04x\\u%04x", (int) (0xD7C0 + (codepoint >> 10)), (int) (0xDC00 + (codepoint & 0x3FF)));
      }
    } else {
      GENERICLOGGER_TRACEF(genericLoggerp, "%c", (char) codepoint);
    }
    break;
  }
}

/*****************************************************************************/
char *marpaESLIF_encodings(marpaESLIF_t *marpaESLIFp, char *bytep, size_t bytel)
/*****************************************************************************/
{
  static const char   *funcs     = "marpaESLIF_encodings";
  char                *encodings = NULL;
  marpaESLIF_string_t *utf8p     = NULL;
  marpaESLIF_string_t  string;

  if (marpaESLIFp == NULL) {
    errno = EINVAL;
    goto err;
  }

  string.bytep          = bytep;
  string.bytel          = bytel;
  string.encodingasciis = NULL;
  string.asciis         = NULL;

  utf8p = _marpaESLIF_string2utf8p(marpaESLIFp, &string, 1 /* tconvsilentb */);
  if (utf8p == NULL) {
    goto err;
  }

  encodings = strdup(utf8p->encodingasciis);
  if (encodings == NULL) {
    MARPAESLIF_ERRORF(marpaESLIFp, "strdup failure, %s", strerror(errno));
    goto err;
  }

  goto done;

 err:
  if (encodings != NULL) {
    free(encodings);
    encodings = NULL;
  }

 done:
  _marpaESLIF_string_freev(utf8p, 0 /* onStackb */);
  return encodings;
}

/*****************************************************************************/
static inline short _marpaESLIF_flatten_pointers(marpaESLIF_t *marpaESLIFp, genericStack_t *flattenPtrStackp, genericHash_t *flattenPtrHashp, marpaESLIFValueResult_t *marpaESLIFValueResultp, short noShallowb)
/*****************************************************************************/
/* if marpaESLIFValueResultp != NULL, flattenPtrStackp may be NULL but flattenPtrHashp must never be NULL */
/*****************************************************************************/
{
  static const char       *funcs = "_marpaESLIF_flatten_pointers";
  genericStack_t           marpaESLIFValueResultStack;
  genericStack_t          *marpaESLIFValueResultStackp = NULL;
  marpaESLIFValueResult_t *marpaESLIFValueResultTmpp;
  short                    flattenPtrStackb = (flattenPtrStackp != NULL);
  size_t                   i;
  short                    shallowb;
  void                    *p;
  void                    *tmpp;
  short                    findResultb;
  short                    rcb;
  marpaESLIFValueType_t    type;
  int                      hashindexi;

  /* This method has a big cost - we know that we will have nothing by looking at the type marpaESLIFValueResultp */
  switch (marpaESLIFValueResultp->type) {
  case MARPAESLIF_VALUE_TYPE_PTR:
    p        = marpaESLIFValueResultp->u.p.p;
    shallowb = marpaESLIFValueResultp->u.p.shallowb;
    break;
  case MARPAESLIF_VALUE_TYPE_ARRAY:
    p        = marpaESLIFValueResultp->u.a.p;
    shallowb = marpaESLIFValueResultp->u.a.shallowb;
    break;
  case MARPAESLIF_VALUE_TYPE_STRING:
    p        = marpaESLIFValueResultp->u.s.p;
    shallowb = marpaESLIFValueResultp->u.s.shallowb;
    break;
  case MARPAESLIF_VALUE_TYPE_ROW:
    p        = marpaESLIFValueResultp->u.r.p;
    shallowb = marpaESLIFValueResultp->u.r.shallowb;
    break;
  case MARPAESLIF_VALUE_TYPE_TABLE:
    p        = marpaESLIFValueResultp->u.t.p;
    shallowb = marpaESLIFValueResultp->u.t.shallowb;
    break;
  default:
    p        = NULL;
    break;
  }
  if ((p == NULL) || (shallowb && noShallowb)) {
    rcb = 1;
    goto done;
  }

  marpaESLIFValueResultStackp = &marpaESLIFValueResultStack;
  GENERICSTACK_INIT(marpaESLIFValueResultStackp);
  if (GENERICSTACK_ERROR(marpaESLIFValueResultStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "marpaESLIFValueResultStackp initialization failure, %s", strerror(errno));
    marpaESLIFValueResultStackp = NULL;
    goto err;
  }

  GENERICSTACK_PUSH_PTR(marpaESLIFValueResultStackp, marpaESLIFValueResultp);
  if (GENERICSTACK_ERROR(marpaESLIFValueResultStackp)) {
    MARPAESLIF_ERRORF(marpaESLIFp, "marpaESLIFValueResultStackp push failure, %s", strerror(errno));
    goto err;
  }

  while (GENERICSTACK_USED(marpaESLIFValueResultStackp) > 0) {
    marpaESLIFValueResultTmpp = GENERICSTACK_POP_PTR(marpaESLIFValueResultStackp);

    switch (type = marpaESLIFValueResultTmpp->type) {
    case MARPAESLIF_VALUE_TYPE_PTR:
      p        = marpaESLIFValueResultTmpp->u.p.p;
      shallowb = marpaESLIFValueResultTmpp->u.p.shallowb;
      break;
    case MARPAESLIF_VALUE_TYPE_ARRAY:
      p        = marpaESLIFValueResultTmpp->u.a.p;
      shallowb = marpaESLIFValueResultTmpp->u.a.shallowb;
      break;
    case MARPAESLIF_VALUE_TYPE_STRING:
      p        = marpaESLIFValueResultTmpp->u.s.p;
      shallowb = marpaESLIFValueResultTmpp->u.s.shallowb;
      break;
    case MARPAESLIF_VALUE_TYPE_ROW:
      p        = marpaESLIFValueResultTmpp->u.r.p;
      shallowb = marpaESLIFValueResultTmpp->u.r.shallowb;
      break;
    case MARPAESLIF_VALUE_TYPE_TABLE:
      p        = marpaESLIFValueResultTmpp->u.t.p;
      shallowb = marpaESLIFValueResultTmpp->u.t.shallowb;
      break;
    default:
      p        = NULL;
      break;
    }
    if (p != NULL) {
      /* Per def shallowb is set */
      if (shallowb && noShallowb) {
        /* Note that if a container is shallowed, inner elements may NOT be shallow but must be skipped */
        continue;
      }
      /* Remember this pointer, value is the corresponding marpaESLIFValueResult pointer */
      findResultb = 0;
      hashindexi = _marpaESLIF_ptrhashi(NULL /* userDatavp */, GENERICSTACKITEMTYPE_PTR, (void **) &p);
      GENERICHASH_FIND_BY_IND(flattenPtrHashp,
                              NULL, /* userDatavp */
                              PTR,
                              p,
                              PTR,
                              &tmpp,
                              findResultb,
                              hashindexi);
      if (GENERICHASH_ERROR(flattenPtrHashp)) {
        MARPAESLIF_ERRORF(marpaESLIFp, "flattenPtrHashp find failure, %s", strerror(errno));
        goto err;
      }
      if (findResultb) {
        /* This is an error unless the marpaESLIFValueResult is shallow */
        if (! shallowb) {
          MARPAESLIF_ERRORF(marpaESLIFp, "Recursive marpaESLIFValueResult is not allowed, type: %s", _marpaESLIF_value_types(marpaESLIFValueResultTmpp->type));
          goto err;
        }
      } else {
        GENERICHASH_SET_BY_IND(flattenPtrHashp, NULL /* userDatavp */, PTR, p, PTR, marpaESLIFValueResultTmpp, hashindexi);
        if (GENERICHASH_ERROR(flattenPtrHashp)) {
          MARPAESLIF_ERRORF(marpaESLIFp, "flattenPtrHashp failure, %s", strerror(errno));
          goto err;
        }
      }
      if (flattenPtrStackb) {
        GENERICSTACK_PUSH_PTR(flattenPtrStackp, p);
        if (GENERICSTACK_ERROR(flattenPtrStackp)) {
          MARPAESLIF_ERRORF(marpaESLIFp, "flattenPtrStackp push failure, %s", strerror(errno));
          goto err;
        }
      }
      /* Push container content */
      switch (type) {
      case MARPAESLIF_VALUE_TYPE_ROW:
        /* We push a copy of the inner elements */
        for (i = 0; i < marpaESLIFValueResultTmpp->u.r.sizel; i++) {
          GENERICSTACK_PUSH_PTR(marpaESLIFValueResultStackp, &(marpaESLIFValueResultTmpp->u.r.p[i]));
          if (GENERICSTACK_ERROR(marpaESLIFValueResultStackp)) {
            MARPAESLIF_ERRORF(marpaESLIFp, "marpaESLIFValueResultStackp push failure, %s", strerror(errno));
            goto err;
          }
        }
        break;
      case MARPAESLIF_VALUE_TYPE_TABLE:
        /* We push a copy of the inner elements */
        for (i = 0; i < marpaESLIFValueResultTmpp->u.t.sizel; i++) {
          /* Key */
          GENERICSTACK_PUSH_PTR(marpaESLIFValueResultStackp, &(marpaESLIFValueResultTmpp->u.t.p[i].key));
          if (GENERICSTACK_ERROR(marpaESLIFValueResultStackp)) {
            MARPAESLIF_ERRORF(marpaESLIFp, "marpaESLIFValueResultStackp push failure, %s", strerror(errno));
            goto err;
          }
          /* Value */
          GENERICSTACK_PUSH_PTR(marpaESLIFValueResultStackp, &(marpaESLIFValueResultTmpp->u.t.p[i].value));
          if (GENERICSTACK_ERROR(marpaESLIFValueResultStackp)) {
            MARPAESLIF_ERRORF(marpaESLIFp, "marpaESLIFValueResultStackp push failure, %s", strerror(errno));
            goto err;
          }
        }
        break;
      default:
        break;
      }
    }
  }

  rcb = 1;
  goto done;

err:
  rcb = 0;

done:
  GENERICSTACK_RESET(marpaESLIFValueResultStackp); /* This is NULL protected */
  return rcb;
}

#include "bootstrap.c"
#include "lua.c"
