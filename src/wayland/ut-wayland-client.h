#include <stdbool.h>

#include "ut-object.h"
#include "ut-wayland-callback.h"
#include "ut-wayland-registry.h"

#pragma once

/// Creates a new Wayland client.
///
/// !return-ref
/// !return-type UtWaylandClient
UtObject *ut_wayland_client_new();

/// Connect the [object] to the Wayland server.
void ut_wayland_client_connect(UtObject *object);

/// Makes a sync request to the server.
///
/// !return-ref
/// !return-type UtWaylandCallback
UtObject *ut_wayland_client_sync(UtObject *object, UtObject *callback_object,
                                 UtWaylandCallbackDoneCallback done_callback);

/// Returns the registry for this client.
///
/// !return-ref
/// !return-type UtWaylandRegistry
UtObject *
ut_wayland_client_get_registry(UtObject *object, UtObject *callback_object,
                               const UtWaylandRegistryCallbacks *callbacks);

/// Returns [true] if [object] is a [UtWaylandClient].
bool ut_object_is_wayland_client(UtObject *object);
