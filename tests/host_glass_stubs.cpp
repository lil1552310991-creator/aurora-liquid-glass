// =============================================================================
//  宿主机回归用的替身（**只在 PC 上编译，永远不参与设备产物**）
//
//  为什么需要：几条离线回归要链接 ytbl 的组件实现（Toggle/Slider/PillButton/
//  TabStrip/PopupMenu/Dialog）与它们的配方，而 ytbl 里真正碰 GL 的部分住在
//  GlassPanel.cpp / Context.cpp —— 那两个文件包含 <GLES3/gl3.h>，PC 上根本编不了。
//
//  这些替身满足"链接通过"这一个条件，**不会被执行**：
//    · 所有测试只调用"参数 → PanelStyle"的纯函数（GlassRecipes.cpp），
//      不调用任何 draw()；
//    · submitPanel 返回 -1（= 没有可用面板），即使被误调用也不会去碰 GL 状态；
//    · glStartup/glReady 返回 false，误调用时只会让面板绘制静默跳过。
//
//  ★ 这些替身**不能**复制任何生产逻辑（例如 px()/saturate()/lerp() 必须来自
//    Runtime.cpp 的真实现），否则测的就不是真代码了。这里只有 GL 侧的空壳。
// =============================================================================
#include "third_party/liquidglass/ytbl/GlassContext.h"

namespace lgx {

int submitPanel(const PanelStyle& style, const Box& box) {
  (void)style;
  (void)box;
  return -1;   // 宿主机上没有 GL 上下文，一律拒绝提交
}

namespace detail {

float pixelScaleXOf() { return 1.0f; }
float pixelScaleYOf() { return 1.0f; }

void bindBackdrop(unsigned int texture, int width, int height) {
  (void)texture;
  (void)width;
  (void)height;
}
void releaseBackdrop() {}
void setSceneComposerHook(SceneComposer hook, void* user) {
  (void)hook;
  (void)user;
}
void resetJobs() {}

bool glStartup() { return false; }
void glShutdown() {}
bool glReady() { return false; }
void glBeginFrame() {}
bool glGatherReady() { return false; }
bool glCaptureRegion(int x, int y, int w, int h, int viewW, int viewH, float& originX,
                     float& originY, float& sizeX, float& sizeY, float& strideX,
                     float& strideY) {
  (void)x; (void)y; (void)w; (void)h; (void)viewW; (void)viewH;
  (void)originX; (void)originY; (void)sizeX; (void)sizeY; (void)strideX; (void)strideY;
  return false;
}
void glReleaseCapture() {}
unsigned int glCaptureTexture() { return 0u; }
int glCaptureWidth() { return 0; }
int glCaptureHeight() { return 0; }
bool glRunGather(int regionW, int regionH, unsigned int sourceTexture, int sourceW,
                 int sourceH, float blurRadius) {
  (void)regionW; (void)regionH; (void)sourceTexture; (void)sourceW; (void)sourceH;
  (void)blurRadius;
  return false;
}
GatherInfo glGatherInfo() { return GatherInfo(); }
void glPaintPanel(const PanelJob& job, const Box& clip, unsigned int sceneTexture,
                  const GatherInfo& gather) {
  (void)job; (void)clip; (void)sceneTexture; (void)gather;
}
const char* quadVertexSource() { return ""; }
const char* gatherFragmentSource() { return ""; }
const char* panelFragmentSource() { return ""; }

}  // namespace detail
}  // namespace lgx
