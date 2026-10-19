#pragma once

namespace aospglue::detail {

// An Android sp<T> passed by value is an addressable 8-byte owning slot on
// AArch64. A POD copy does not acquire ownership. Acquire one reference for
// this parameter, let the real callee move it (and null the slot), and release
// it here if the callee did not move it. The caller's original owner survives.
template <class Acquire, class Release, class Consume>
bool WithOwnedStrongSlot(void* object, Acquire acquire, Release release, Consume consume) {
    if (!object) return false;
    void* slot = object;
    if (!acquire(&slot)) return false;
    struct Cleanup {
        void*& slot;
        Release& release;
        ~Cleanup() { if (slot) release(&slot); }
    } cleanup{slot, release};
    consume(&slot);
    return true;
}

} // namespace aospglue::detail
