#pragma once

#ifndef LEMONADE_RESIDENCY_DURABLE_TESTING
#error "durable local-overlay test factory is confined to test targets"
#endif

#include "lemon/residency/durable_local_overlay.h"

#include "platform/durable_file_adapter.h"

#include <memory>

namespace lemon::residency::detail {

class LocalOverlayStoreTestFactory {
public:
    static LocalOverlayStore
    make(std::unique_ptr<DurableFileAdapter> adapter,
         LocalOverlayStoreLimits limits);
    static LocalOverlayActivation
    resolve_qualification(LocalOverlayActivation activation);
};

LocalOverlayStore
make_local_overlay_store_for_test(std::unique_ptr<DurableFileAdapter> adapter,
                                  LocalOverlayStoreLimits limits);

} // namespace lemon::residency::detail
