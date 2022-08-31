#include <assert.h>
#include <stdlib.h>

#include "ut.h"

typedef struct {
  UtObject object;
  char *name;
  UtObject *attributes;
  UtObject *content;
} UtXmlElement;

static void ut_xml_element_cleanup(UtObject *object) {
  UtXmlElement *self = (UtXmlElement *)object;
  free(self->name);
  ut_object_unref(self->attributes);
  ut_object_unref(self->content);
}

static UtObjectInterface object_interface = {.type_name = "UtXmlElement",
                                             .cleanup = ut_xml_element_cleanup};

UtObject *ut_xml_element_new(const char *name, UtObject *attributes,
                             UtObject *content) {
  UtObject *object = ut_object_new(sizeof(UtXmlElement), &object_interface);
  UtXmlElement *self = (UtXmlElement *)object;
  self->name = ut_cstring_new(name);
  self->attributes = ut_object_ref(attributes);
  self->content = ut_object_ref(content);
  return object;
}

const char *ut_xml_element_get_name(UtObject *object) {
  assert(ut_object_is_xml_element(object));
  UtXmlElement *self = (UtXmlElement *)object;
  return self->name;
}

UtObject *ut_xml_element_get_attributes(UtObject *object) {
  assert(ut_object_is_xml_element(object));
  UtXmlElement *self = (UtXmlElement *)object;
  return self->attributes;
}

UtObject *ut_xml_element_get_content(UtObject *object) {
  assert(ut_object_is_xml_element(object));
  UtXmlElement *self = (UtXmlElement *)object;
  return self->content;
}

bool ut_object_is_xml_element(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
