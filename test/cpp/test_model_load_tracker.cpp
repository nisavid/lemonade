#include "lemon/model_load_tracker.h"

#include <cassert>

int main() {
    lemon::ModelLoadTracker tracker;

    assert(!tracker.is_loading("model-a"));
    assert(!tracker.set_pin_override_if_loading("model-a", true));

    tracker.begin("model-a");
    assert(tracker.is_loading("model-a"));
    assert(tracker.set_pin_override_if_loading("model-a", true));

    auto pinned = tracker.finish("model-a");
    assert(pinned.has_value());
    assert(*pinned);
    assert(!tracker.is_loading("model-a"));

    tracker.begin("model-b");
    tracker.begin("model-b");
    assert(tracker.set_pin_override_if_loading("model-b", true));
    assert(tracker.set_pin_override_if_loading("model-b", false));

    assert(!tracker.finish("model-b").has_value());
    assert(tracker.is_loading("model-b"));

    auto unpinned = tracker.finish("model-b");
    assert(unpinned.has_value());
    assert(!*unpinned);
    assert(!tracker.is_loading("model-b"));

    return 0;
}
