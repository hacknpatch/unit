#include <assert.h>
#include <stdlib.h>

#include "ut.h"

typedef struct _UtHashTableItem UtHashTableItem;

typedef struct {
  UtObject object;
  UtHashTableItem *items;
} UtHashTable;

struct _UtHashTableItem {
  UtObject object;
  UtObject *key;
  UtObject *value;
  UtHashTableItem *next;
};

static UtObject *ut_hash_table_item_get_key(UtObject *object) {
  UtHashTableItem *self = (UtHashTableItem *)object;
  return ut_object_ref(self->key);
}

static UtObject *ut_hash_table_item_get_value(UtObject *object) {
  UtHashTableItem *self = (UtHashTableItem *)object;
  return ut_object_ref(self->value);
}

static UtMapItemInterface map_item_interface = {
    .get_key = ut_hash_table_item_get_key,
    .get_value = ut_hash_table_item_get_value};

static void ut_hash_table_item_cleanup(UtObject *object) {
  UtHashTableItem *self = (UtHashTableItem *)object;
  ut_object_unref(self->key);
  ut_object_unref(self->value);
  self->next = NULL;
}

static UtObjectInterface item_object_interface = {
    .type_name = "UtHashTableItem",
    .cleanup = ut_hash_table_item_cleanup,
    .interfaces = {{&ut_map_item_id, &map_item_interface}, {NULL, NULL}}};

static UtHashTableItem *item_new(UtObject *key, UtObject *value) {
  UtHashTableItem *item = (UtHashTableItem *)ut_object_new(
      sizeof(UtHashTableItem), &item_object_interface);
  item->key = ut_object_ref(key);
  item->value = ut_object_ref(value);
  return item;
}

static UtHashTableItem *lookup(UtHashTable *self, UtObject *key,
                               UtHashTableItem **prev_item) {
  UtHashTableItem *prev_item_ = NULL;
  for (UtHashTableItem *item = self->items; item != NULL; item = item->next) {
    if (ut_object_equal(item->key, key)) {
      if (prev_item != NULL) {
        *prev_item = prev_item_;
      }

      return item;
    }
    prev_item_ = item;
  }
  return NULL;
}

size_t ut_hash_table_get_length(UtObject *object) {
  UtHashTable *self = (UtHashTable *)object;
  size_t length = 0;
  for (UtHashTableItem *item = self->items; item != NULL; item = item->next) {
    length++;
  }
  return length;
}

static void ut_hash_table_insert(UtObject *object, UtObject *key,
                                 UtObject *value) {
  UtHashTable *self = (UtHashTable *)object;

  UtHashTableItem *prev_item;
  UtHashTableItem *existing_item = lookup(self, key, &prev_item);

  UtHashTableItem *item = item_new(key, value);
  if (existing_item == NULL) {
    item->next = self->items;
    self->items = item;
  } else {
    item->next = existing_item->next;
    if (prev_item != NULL) {
      prev_item->next = item;
    } else {
      self->items = item;
    }

    existing_item->next = NULL;
    ut_object_unref((UtObject *)existing_item);
  }
}

static UtObject *ut_hash_table_lookup(UtObject *object, UtObject *key) {
  UtHashTable *self = (UtHashTable *)object;
  UtHashTableItem *item = lookup(self, key, NULL);
  return item != NULL ? item->value : NULL;
}

static void ut_hash_table_remove(UtObject *object, UtObject *key) {
  UtHashTable *self = (UtHashTable *)object;
  UtHashTableItem *prev_item;
  UtHashTableItem *item = lookup(self, key, &prev_item);
  if (item == NULL) {
    return;
  }
  if (prev_item != NULL) {
    prev_item->next = item->next;
  } else {
    self->items = item->next;
  }

  item->next = NULL;
  ut_object_unref((UtObject *)item);
}

static UtObject *ut_hash_table_get_items(UtObject *object) {
  UtHashTable *self = (UtHashTable *)object;
  UtObject *items = ut_object_array_new();
  for (UtHashTableItem *item = self->items; item != NULL; item = item->next) {
    ut_list_prepend(items, (UtObject *)item);
  }
  return items;
}

static UtObject *ut_hash_table_get_keys(UtObject *object) {
  UtHashTable *self = (UtHashTable *)object;
  UtObject *keys = ut_object_array_new();
  for (UtHashTableItem *item = self->items; item != NULL; item = item->next) {
    ut_list_prepend(keys, item->key);
  }
  return keys;
}

static UtObject *ut_hash_table_get_values(UtObject *object) {
  UtHashTable *self = (UtHashTable *)object;
  UtObject *values = ut_object_array_new();
  for (UtHashTableItem *item = self->items; item != NULL; item = item->next) {
    ut_list_prepend(values, item->value);
  }
  return values;
}

static UtMapInterface map_interface = {.get_length = ut_hash_table_get_length,
                                       .insert = ut_hash_table_insert,
                                       .lookup = ut_hash_table_lookup,
                                       .remove = ut_hash_table_remove,
                                       .get_items = ut_hash_table_get_items,
                                       .get_keys = ut_hash_table_get_keys,
                                       .get_values = ut_hash_table_get_values};

static void ut_hash_table_cleanup(UtObject *object) {
  UtHashTable *self = (UtHashTable *)object;
  UtHashTableItem *next_item;
  for (UtHashTableItem *item = self->items; item != NULL; item = next_item) {
    next_item = item->next;
    item->next = NULL;
    ut_object_unref((UtObject *)item);
  }
  self->items = NULL;
}

static UtObjectInterface object_interface = {
    .type_name = "UtHashTable",
    .to_string = ut_map_to_string,
    .cleanup = ut_hash_table_cleanup,
    .interfaces = {{&ut_map_id, &map_interface}, {NULL, NULL}}};

UtObject *ut_hash_table_new() {
  return ut_object_new(sizeof(UtHashTable), &object_interface);
}

bool ut_object_is_hash_table_string(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
