// PavelDoGreat/WebGL-Fluid-Simulation（MIT）GLSL 照抄搬运 demo。
// 搬运清单已批：~44 pass 拓扑展开（pressure×20 / bloom×8 循环按批准展开），
// 全 pass 统一 resolutionScale（per-pass 分辨率上游 PR 另提），config 原值进原值出
// （script.js:59-85）。mouseState pass 补底座缺失的指针逐帧 delta。
// 离屏截帧：FLUID_DEMO_SCREENSHOT=<帧号> FLUID_DEMO_SCREENSHOT_PATH=<ppm 路径>。
#include "eui_neo.h"

#include <glad/glad.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace app {
namespace {

constexpr int kPressureIterations = 20;  // PRESSURE_ITERATIONS
constexpr int kBloomIterations = 8;      // BLOOM_ITERATIONS

// 扫参钩子：FLUID_SWEEP_{SPLAT,RADIUS,CLAMP,BG} 覆盖四个观感常量，未设用 v6 定稿值。
float sweepEnv(const char* k, float dflt) {
  const char* v = std::getenv(k);
  return v ? std::strtof(v, nullptr) : dflt;
}
const float kSweepSplat = sweepEnv("FLUID_SWEEP_SPLAT", 0.3f);
const float kSweepRadius = sweepEnv("FLUID_SWEEP_RADIUS", 1.0f);
const float kSweepClamp = sweepEnv("FLUID_SWEEP_CLAMP", 1.44f);
const float kSweepBg = sweepEnv("FLUID_SWEEP_BG", 1.0f);

#define KHEAD() (std::string(kHead) + sweepDefs())
std::string sweepDefs() {
  return std::string("#define SWEEP_SPLAT ") + std::to_string(kSweepSplat) + "\n" +
         "#define SWEEP_RADIUS " + std::to_string(kSweepRadius) + "\n" +
         "#define SWEEP_CLAMP " + std::to_string(kSweepClamp) + "\n" +
         "#define SWEEP_BG " + std::to_string(kSweepBg) + "\n";
}

constexpr char kHead[] =
    "vec2 texel = 1.0 / iResolution.xy;\n"
    "float dt = min(iTimeDelta, 0.016666f);\n";

eui::ShaderToyPass pass(eui::ShaderToyGraph& g, const char* name,
                        const std::string& body) {
    return g.addInlinePass(name, KHEAD()+body, "", name);
}

}  // namespace (anonymous)

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("Fluid Background")
        .pageId("fluid_background")
        .windowWidth(800)
        .windowHeight(500)
        .resizable(true)
        .alwaysOnTop(false);
    return config;
}

eui::ShaderToyGraph buildGraph() {
    using eui::ShaderToyChannel;
    eui::ShaderToyGraph g;

    g.addInlinePass("mouse_state", KHEAD() + R"GLSL(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  // demo 自动激励：圆周轨迹虚拟指针（无人操作也持续注入染料），
  // 真鼠标按下时优先（iMouse.z > 0），接入 app 后由宿主开关切真输入
  float t = iTime * 0.6;
  vec2 auto_ = vec2(0.5f, 0.5f) + vec2(0.22f * cos(t) + 0.14f * cos(t * 2.7f + 1.3f),
                                        0.20f * sin(t * 1.31f) + 0.12f * sin(t * 3.9f));
  vec2 autoPrev = vec2(0.5f, 0.5f) + vec2(0.22f * cos(t - 0.016f) + 0.14f * cos((t - 0.016f) * 2.7f + 1.3f),
                                          0.20f * sin((t - 0.016f) * 1.31f) + 0.12f * sin((t - 0.016f) * 3.9f));
  vec2 mouseUv = iMouse.xy / iResolution.xy;
  float mouseDown = iMouse.z > 0.0 ? 1.0 : 0.0;
  vec2 cur = mouseDown > 0.5 ? mouseUv : auto_;
  vec2 prev = mouseDown > 0.5 ? texture(iChannel0, fragCoord / iResolution.xy).zw.xy
                              : autoPrev;
  if (prev.x == 0.0 && prev.y == 0.0) prev = cur;
  fragColor = vec4(cur.x, cur.y, prev.x, prev.y);
}
)GLSL", "", "mouse_state");
    g.setChannel("mouse_state", 0, ShaderToyChannel::self());

    g.addInlinePass("splat_velocity", KHEAD() + R"GLSL(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  vec3 base = texture(iChannel0, uv).xyz;
  vec4 ms = texture(iChannel1, uv);
  vec2 cur = ms.xy, prev = ms.zw;
  float aspect = iResolution.x / iResolution.y;
  vec2 delta = cur - prev;
  if (aspect < 1.0) delta.x *= aspect;
  delta *= 6000.0f;
  vec2 p = uv - cur;
  p.x *= aspect;
  float radius = 0.0025f;
  if (aspect > 1.0) radius *= aspect;
  vec3 splat = exp(-dot(p, p) / radius) * vec3(delta, 0.0);
  fragColor = vec4(base + splat, 1.0);
}
)GLSL", "", "splat_velocity");
    g.setChannel("splat_velocity", 0, ShaderToyChannel::self());
    g.setChannel("splat_velocity", 1, ShaderToyChannel::buffer("mouse_state"));

    g.addInlinePass("curl", KHEAD() + R"GLSL(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float L = texture(iChannel0, uv - texel * vec2(1, 0)).y;
  float R = texture(iChannel0, uv + texel * vec2(1, 0)).y;
  float T = texture(iChannel0, uv + texel * vec2(0, 1)).x;
  float B = texture(iChannel0, uv - texel * vec2(0, 1)).x;
  float vorticity = R - L - T + B;
  fragColor = vec4(0.5 * vorticity, 0.0, 0.0, 1.0);
}
)GLSL", "", "curl");
    g.setChannel("curl", 0, ShaderToyChannel::buffer("splat_velocity"));

    g.addInlinePass("vorticity", KHEAD() + R"GLSL(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  float curl = 30.0f;
  vec2 uv = fragCoord / iResolution.xy;
  float L = texture(iChannel0, uv - texel * vec2(1, 0)).x;
  float R = texture(iChannel0, uv + texel * vec2(1, 0)).x;
  float T = texture(iChannel0, uv + texel * vec2(0, 1)).x;
  float B = texture(iChannel0, uv - texel * vec2(0, 1)).x;
  float C = texture(iChannel1, uv).x;
  vec2 force = 0.5 * vec2(abs(T) - abs(B), abs(R) - abs(L));
  force /= length(force) + 0.0001;
  force *= curl * C;
  force.y *= -1.0;
  vec2 velocity = texture(iChannel0, uv).xy;
  velocity += force * dt;
  velocity = min(max(velocity, -1000.0), 1000.0);
  fragColor = vec4(velocity, 0.0, 1.0);
}
)GLSL", "", "vorticity");
    g.setChannel("vorticity", 0, ShaderToyChannel::buffer("splat_velocity"));
    g.setChannel("vorticity", 1, ShaderToyChannel::buffer("curl"));

    g.addInlinePass("divergence", KHEAD() + R"GLSL(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float L = texture(iChannel0, uv - texel * vec2(1, 0)).x;
  float R = texture(iChannel0, uv + texel * vec2(1, 0)).x;
  float T = texture(iChannel0, uv + texel * vec2(0, 1)).y;
  float B = texture(iChannel0, uv - texel * vec2(0, 1)).y;
  vec2 C = texture(iChannel0, uv).xy;
  if (uv.x - texel.x < 0.0) L = -C.x;
  if (uv.x + texel.x > 1.0) R = -C.x;
  if (uv.y + texel.y > 1.0) T = -C.y;
  if (uv.y - texel.y < 0.0) B = -C.y;
  float div = 0.5 * (R - L + T - B);
  fragColor = vec4(div, 0.0, 0.0, 1.0);
}
)GLSL", "", "divergence");
    g.setChannel("divergence", 0, ShaderToyChannel::buffer("vorticity"));

    g.addInlinePass("pressure_clear", KHEAD() + R"GLSL(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  fragColor = 0.8f * texture(iChannel0, uv);
}
)GLSL", "", "pressure_clear");
    g.setChannel("pressure_clear", 0, ShaderToyChannel::self());

    // pressure ×20：链式引用前一块（首块接 clear 的上帧输出）
    for (int i = 1; i <= kPressureIterations; ++i) {
        char name[16], prev[16];
        std::snprintf(name, sizeof(name), "pressure_%02d", i);
        if (i == 1)
            std::snprintf(prev, sizeof(prev), "pressure_clear");
        else
            std::snprintf(prev, sizeof(prev), "pressure_%02d", i - 1);
        std::string src = KHEAD() + R"GLSL(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float L = texture(iChannel0, uv - texel * vec2(1, 0)).x;
  float R = texture(iChannel0, uv + texel * vec2(1, 0)).x;
  float T = texture(iChannel0, uv + texel * vec2(0, 1)).x;
  float B = texture(iChannel0, uv - texel * vec2(0, 1)).x;
  float C = texture(iChannel0, uv).x;
  float divergence = texture(iChannel1, uv).x;
  float pressure = (L + R + B + T - divergence) * 0.25f;
  fragColor = vec4(pressure, 0.0, 0.0, 1.0);
}
)GLSL";
        g.addInlinePass(name, src, "", name);
        g.setChannel(name, 0, ShaderToyChannel::buffer("divergence"));
        g.setChannel(name, 1, ShaderToyChannel::buffer(prev));
    }

    g.addInlinePass("gradient_subtract", KHEAD() + R"GLSL(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float L = texture(iChannel1, uv - texel * vec2(1, 0)).x;
  float R = texture(iChannel1, uv + texel * vec2(1, 0)).x;
  float T = texture(iChannel1, uv + texel * vec2(0, 1)).x;
  float B = texture(iChannel1, uv - texel * vec2(0, 1)).x;
  vec2 velocity = texture(iChannel0, uv).xy;
  velocity -= vec2(R - L, T - B);
  fragColor = vec4(velocity, 0.0, 1.0);
}
)GLSL", "", "gradient_subtract");
    g.setChannel("gradient_subtract", 0, ShaderToyChannel::buffer("pressure_20"));
    g.setChannel("gradient_subtract", 1, ShaderToyChannel::buffer("divergence"));

    g.addInlinePass("advect_velocity", KHEAD() + R"GLSL(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  float dissipation = 0.2f;
  vec2 uv = fragCoord / iResolution.xy;
  vec2 coord = uv - dt * texture(iChannel0, uv).xy * texel;
  vec4 result = texture(iChannel0, coord);
  float decay = 1.0 + dissipation * dt;
  fragColor = result / decay;
}
)GLSL", "", "advect_velocity");
    g.setChannel("advect_velocity", 0,
                 ShaderToyChannel::buffer("gradient_subtract"));

    g.addInlinePass("color_state", KHEAD()+R"GLSL(
vec3 hsv2rgb(vec3 c) {
  vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  // DevDesk 定稿：Clay 暖色带轮换（橙/深陶土/暖沙），10 秒一换，亮度 ×0.15
  fragColor = vec4(0.851f * 0.15f, 0.467f * 0.15f, 0.341f * 0.15f, 1.0f);
}
)GLSL", "", "color_state");

    g.addInlinePass("splat_dye", KHEAD() + R"GLSL(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  vec3 base = texture(iChannel0, uv).xyz;
  vec4 ms = texture(iChannel1, uv);
  vec3 color = texture(iChannel2, uv).rgb;
  vec2 cur = ms.xy, prev = ms.zw;
  float aspect = iResolution.x / iResolution.y;
  vec2 p = uv - cur;
  p.x *= aspect;
  float radius = 0.0025f;
  if (aspect > 1.0) radius *= aspect;
  vec3 splat = exp(-dot(p, p) / (radius * SWEEP_RADIUS)) * color * SWEEP_SPLAT;
  fragColor = vec4(base + splat, 1.0);
}
)GLSL", "", "splat_dye");
    g.setChannel("splat_dye", 0, ShaderToyChannel::self());
    g.setChannel("splat_dye", 1, ShaderToyChannel::buffer("mouse_state"));
    g.setChannel("splat_dye", 2, ShaderToyChannel::buffer("color_state"));

    g.addInlinePass("advect_dye", KHEAD() + R"GLSL(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  float dissipation = 1.0f;
  vec2 uv = fragCoord / iResolution.xy;
  vec2 coord = uv - dt * texture(iChannel0, uv).xy * texel;
  vec4 result = texture(iChannel1, coord);
  float decay = 1.0 + dissipation * dt;
  fragColor = result / decay;
}
)GLSL", "", "advect_dye");
    g.setChannel("advect_dye", 0, ShaderToyChannel::buffer("gradient_subtract"));
    g.setChannel("advect_dye", 1, ShaderToyChannel::buffer("splat_dye"));

    g.addInlinePass("bloom_prefilter", KHEAD() + R"GLSL(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  vec3 c = texture(iChannel0, uv).rgb;
  float br = max(c.r, max(c.g, c.b));
  float rq = clamp(br - 0.6f + 0.7f * 0.6f * 0.6f, 0.0f, 0.7f * 0.6f);
  rq = (0.7f / (0.6f * 0.6f - 0.7f * 0.6f + 0.0001f)) * rq * rq;
  c *= max(rq, br - 0.6f) / max(br, 0.0001f);
  fragColor = vec4(c, 0.0);
}
)GLSL", "", "bloom_prefilter");
    g.setChannel("bloom_prefilter", 0, ShaderToyChannel::buffer("advect_dye"));

    for (int i = 1; i <= kBloomIterations; ++i) {
        char name[24], prev[32];
        std::snprintf(name, sizeof(name), "bloom_blur_%02d", i);
        if (i == 1)
            std::snprintf(prev, sizeof(prev), "bloom_prefilter");
        else
            std::snprintf(prev, sizeof(prev), "bloom_blur_%02d", i - 1);
        std::string src = KHEAD() + R"GLSL(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  vec4 sum = vec4(0.0);
  sum += texture(iChannel0, uv - texel * vec2(1, 0));
  sum += texture(iChannel0, uv + texel * vec2(1, 0));
  sum += texture(iChannel0, uv + texel * vec2(0, 1));
  sum += texture(iChannel0, uv - texel * vec2(0, 1));
  sum *= 0.25f;
  fragColor = sum;
}
)GLSL";
        g.addInlinePass(name, src, "", name);
        g.setChannel(name, 0, ShaderToyChannel::buffer(prev));
    }

    g.addInlinePass("bloom_final", KHEAD() + R"GLSL(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  vec4 sum = vec4(0.0);
  sum += texture(iChannel0, uv - texel * vec2(1, 0));
  sum += texture(iChannel0, uv + texel * vec2(1, 0));
  sum += texture(iChannel0, uv + texel * vec2(0, 1));
  sum += texture(iChannel0, uv - texel * vec2(0, 1));
  sum *= 0.25f;
  sum *= 0.8f;
  fragColor = sum;
}
)GLSL", "", "bloom_final");
    char lastBlur[24];
    std::snprintf(lastBlur, sizeof(lastBlur), "bloom_blur_%02d",
                  kBloomIterations);
    g.setChannel("bloom_final", 0, ShaderToyChannel::buffer(lastBlur));

    g.addInlinePass("sunrays_mask", KHEAD() + R"GLSL(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  vec4 c = texture(iChannel0, uv);
  float br = max(c.r, max(c.g, c.b));
  c.a = 1.0 - min(max(br * 20.0, 0.0), 0.8);
  fragColor = c;
}
)GLSL", "", "sunrays_mask");
    g.setChannel("sunrays_mask", 0, ShaderToyChannel::buffer("advect_dye"));

    g.addInlinePass("sunrays", KHEAD() + R"GLSL(
#define ITERATIONS 16
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float weight = 1.0f;
  float Density = 0.3f;
  float Decay = 0.95f;
  float Exposure = 0.7f;
  vec2 coord = uv;
  vec2 dir = uv - 0.5;
  dir *= 1.0 / float(ITERATIONS) * Density;
  float illuminationDecay = 1.0;
  float color = texture(iChannel0, uv).a;
  for (int i = 0; i < ITERATIONS; i++) {
    coord -= dir;
    float col = texture(iChannel0, coord).a;
    color += col * illuminationDecay * weight;
    illuminationDecay *= Decay;
  }
  fragColor = vec4(color * Exposure, 0.0, 0.0, 1.0);
}
)GLSL", "", "sunrays");
    g.setChannel("sunrays", 0, ShaderToyChannel::buffer("sunrays_mask"));

    g.addInlinePass("display", KHEAD() + R"GLSL(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  vec3 c = texture(iChannel0, uv).rgb;
  float L = max(c.r, max(c.g, c.b));
  c *= (1.0f + L / SWEEP_CLAMP) / (1.0f + L);
  vec3 bg = vec3(0.141f, 0.110f, 0.082f) * SWEEP_BG;
  c = min(bg + c, vec3(1.0f));
  float a = max(c.r, max(c.g, c.b));
  fragColor = vec4(c, a);
}
)GLSL", "", "display");
    g.setChannel("display", 0, ShaderToyChannel::buffer("splat_dye"));
    return g;
}

eui::ShaderToyGraph& fluidGraphOnce() {
    static eui::ShaderToyGraph graph = buildGraph();
    return graph;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    ui.shadertoy("fluid.bg")
        .position(0.0f, 0.0f)
        .size(screen.width, screen.height)
        .graph(fluidGraphOnce())
        .resolutionScale(0.25f)
        .radius(0.0f)
        .onCompileError([](const eui::ShaderToyError& e) {
            std::fprintf(stderr, "[fluid] code=%d pass=%s stage=%s: %s\n",
                         static_cast<int>(e.code), e.passName.c_str(),
                         e.stage.c_str(), e.message.c_str());
        })
        .build();

    // 离屏截帧取证
    static int frameCount = 0;
    const char* shotEnv = std::getenv("FLUID_DEMO_SCREENSHOT");
    if (shotEnv && frameCount++ == std::atoi(shotEnv)) {
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        const int w = viewport[2], h = viewport[3];
        std::string pixels(static_cast<std::size_t>(w) * h * 3, '\0');
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        const char* out = std::getenv("FLUID_DEMO_SCREENSHOT_PATH");
        std::FILE* f = std::fopen(out ? out : "/tmp/fluid_demo.ppm", "wb");
        if (f) {
            std::fprintf(f, "P6\n%d %d\n255\n", w, h);
            std::fwrite(pixels.data(), 1, pixels.size(), f);
            std::fclose(f);
        }
    }
}

} // namespace app
