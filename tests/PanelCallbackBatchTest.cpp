#include "PanelCallbackBatch.h"
#include "third_party/imgui/imgui_internal.h"

#include <cassert>
#include <cstdio>

namespace {

int captureCount = 0;
int paintCount = 0;

void Capture(const ImDrawList*, const ImDrawCmd*) { ++captureCount; }
void CapturePaint(const ImDrawList*, const ImDrawCmd*) {
    ++captureCount;
    ++paintCount;
}
void Other(const ImDrawList*, const ImDrawCmd*) {}

void ResetDrawList(ImDrawList& draw) {
    draw._ResetForNewFrame();
    draw.PushClipRect(ImVec2(0, 0), ImVec2(100, 100));
    draw.PushTextureID(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(7)));
    draw.AddCallback(&Capture, reinterpret_cast<void*>(static_cast<intptr_t>(3)));
    draw.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

}  // namespace

int main() {
    ImDrawListSharedData data;
    ImDrawList draw(&data);

    ResetDrawList(draw);
    assert(aurora::CombineAdjacentPanelCallbacks(draw, 3, &Capture, &CapturePaint));
    assert(draw.CmdBuffer.Size == 3);
    assert(draw.CmdBuffer[0].UserCallback == &CapturePaint);
    assert(draw.CmdBuffer[1].UserCallback == ImDrawCallback_ResetRenderState);
    assert(draw.CmdBuffer[2].UserCallback == nullptr);
    draw.CmdBuffer[0].UserCallback(&draw, &draw.CmdBuffer[0]);
    assert(captureCount == 1 && paintCount == 1);
    // AddCallback leaves the final unused draw command available for geometry.
    draw.AddRectFilled(ImVec2(10, 10), ImVec2(20, 20), IM_COL32_WHITE);
    assert(draw.CmdBuffer.back().ElemCount == 6);

    // Toggle's track must stay between capture and paint, including its reset.
    ResetDrawList(draw);
    draw.AddRectFilled(ImVec2(10, 10), ImVec2(20, 20), IM_COL32_WHITE);
    assert(!aurora::CombineAdjacentPanelCallbacks(draw, 3, &Capture, &CapturePaint));
    assert(draw.CmdBuffer[0].UserCallback == &Capture);

    ResetDrawList(draw);
    assert(!aurora::CombineAdjacentPanelCallbacks(draw, 4, &Capture, &CapturePaint));

    ResetDrawList(draw);
    draw.PushClipRect(ImVec2(0, 0), ImVec2(20, 20));
    assert(!aurora::CombineAdjacentPanelCallbacks(draw, 3, &Capture, &CapturePaint));

    ResetDrawList(draw);
    draw.PushTextureID(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(8)));
    assert(!aurora::CombineAdjacentPanelCallbacks(draw, 3, &Capture, &CapturePaint));

    ResetDrawList(draw);
    draw.AddCallback(&Other, nullptr);
    assert(!aurora::CombineAdjacentPanelCallbacks(draw, 3, &Capture, &CapturePaint));

    // Channel changes cannot silently pair callbacks from different streams.
    ResetDrawList(draw);
    draw.ChannelsSplit(2);
    draw.ChannelsSetCurrent(1);
    assert(!aurora::CombineAdjacentPanelCallbacks(draw, 3, &Capture, &CapturePaint));
    draw.ChannelsMerge();

    std::puts("PanelCallbackBatch: 7 real ImDrawList cases passed");
}
