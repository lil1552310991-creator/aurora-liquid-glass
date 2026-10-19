#include "../root-host/elf/AndroidStrongReference.h"
#include <cassert>
#include <cstdio>

int main() {
    int object = 42;
    int refs = 1; // Overlay's independent owner.
    int acquired = 0, released = 0;
    void* transaction = nullptr;
    auto acquire = [&](void**) { ++refs; ++acquired; return true; };
    auto release = [&](void** slot) {
        assert(*slot == &object);
        --refs; ++released;
    };
    // The real setInputWindowInfo uses sp move assignment. Each apply clears
    // the transaction owner, while the Overlay's independent +1 stays alive.
    for (int i = 0; i < 100; ++i) {
        assert(aospglue::detail::WithOwnedStrongSlot(&object, acquire, release,
            [&](void** slot) { transaction = *slot; *slot = nullptr; }));
        assert(refs == 2 && transaction == &object);
        release(&transaction); transaction = nullptr;
        assert(refs == 1);
    }
    assert(acquired == 100 && released == 100);
    // If getLayerState fails, the callee leaves the argument intact. Its
    // equivalent caller-side sp destructor must still balance the parameter.
    assert(aospglue::detail::WithOwnedStrongSlot(&object, acquire, release,
        [](void**) {}));
    assert(refs == 1 && acquired == 101 && released == 101);
    // Failed acquisition and null objects never submit or release anything.
    bool submitted = false;
    assert(!aospglue::detail::WithOwnedStrongSlot(&object,
        [](void**) { return false; }, release, [&](void**) { submitted = true; }));
    assert(!submitted && refs == 1);
    assert(!aospglue::detail::WithOwnedStrongSlot(nullptr,
        acquire, release, [&](void**) { submitted = true; }));
    assert(!submitted && refs == 1 && acquired == 101 && released == 101);
    std::puts("AndroidStrongReferenceTest: 4 ownership regression groups passed");
}
