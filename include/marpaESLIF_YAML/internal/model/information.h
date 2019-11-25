#ifndef MARPAESLIF_YAML_INTERNAL_MODEL_INFORMATION_H
#define MARPAESLIF_YAML_INTERNAL_MODEL_INFORMATION_H

#include <stddef.h>
#include <genericStack.h>
#include <genericHash.h>

typedef struct marpaESLIF_YAML_Model_Tag_NonSpecific marpaESLIF_YAML_Model_Tag_NonSpecific_t;
typedef struct marpaESLIF_YAML_Model_Tag_Scalar      marpaESLIF_YAML_Model_Tag_Scalar_t;
typedef enum   marpaESLIF_YAML_Model_Tag_Kind        marpaESLIF_YAML_Model_Tag_Kind_e;
typedef struct marpaESLIF_YAML_Model_Tag             marpaESLIF_YAML_Model_Tag_t;
typedef struct marpaESLIF_YAML_Model_Node            marpaESLIF_YAML_Model_Node_t;

struct marpaESLIF_YAML_Model_Tag_NonSpecific {
  void *tagp;
};

struct marpaESLIF_YAML_Model_Tag_Scalar {
  char   *canonicals;
  size_t  canonicall;
};

enum marpaESLIF_YAML_Model_Tag_Kind {
  MARPAESLIF_YAML_MODEL_TAG_KIND_NONSPECIFIC = 0,
  MARPAESLIF_YAML_MODEL_TAG_KIND_SCALAR
};

struct marpaESLIF_YAML_Model_Tag {
  marpaESLIF_YAML_Model_Tag_Kind_e kinde;
  union {
    marpaESLIF_YAML_Model_Tag_NonSpecific_t *nonSpecificp;
    marpaESLIF_YAML_Model_Tag_Scalar_t      *scalarp;
  } u;
};

marpaESLIF_YAML_Model_Tag_t *marpaESLIF_YAML_Model_Tag_newp(marpaESLIF_YAML_Model_Tag_Kind_e kinde, void *tagkindp);
void                         marpaESLIF_YAML_Model_Tag_freev(marpaESLIF_YAML_Model_Tag_t *tagp);

struct marpaESLIF_YAML_Model_Node {
  genericStack_t  _tagStack;    /* For performance, tag stack is hiden inside the structure */
  genericStack_t *tagStackp;
  genericHash_t  _keyValueHash; /* For performance, keyValue hash is hiden inside the structure */
  genericHash_t  *keyValueHashp;
};

#endif /* MARPAESLIF_YAML_INTERNAL_MODEL_INFORMATION_H */
