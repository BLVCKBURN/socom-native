#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>
#include <gl/GLU.h>

#include "runtime_data.hpp"
#include "player_data.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using socom::Vec3;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kPlayerRadius = 0.32f;
constexpr float kPlayerHeight = 1.78f;
constexpr float kEyeHeight = 1.62f;
constexpr float kWalkSpeed = 3.4f;
constexpr float kSprintSpeed = 6.2f;
constexpr float kJumpSpeed = 5.0f;
constexpr float kGravity = 9.81f;
constexpr float kMouseSensitivity = 0.0022f;
constexpr double kFixedStep = 1.0 / 120.0;

struct AppState {
    HWND hwnd{};
    HDC dc{};
    HGLRC glrc{};
    int width{1280};
    int height{720};
    bool running{true};
    bool active{true};
    bool mouseCaptured{true};
    bool drawCollision{false};
    bool wireframe{false};
    bool noclip{false};
    bool texturesEnabled{true};
    bool flipTextureV{true};
    bool thirdPerson{true};
    bool playerVisible{true};
    socom::RuntimePack pack;
    socom::PlayerPack playerPack;
    bool hasPlayerPack{false};
    std::vector<GLuint> glTextures;
    std::vector<GLuint> glPlayerTextures;
    std::vector<socom::Mat4f> playerSkinMatrices;
    int standClip{-1};
    int walkClip{-1};
    int runClip{-1};
    int currentClip{-1};
    float animationTime{0.0f};
    float facingYaw{0.0f};
    float movementAmount{0.0f};
    bool sprinting{false};
    Vec3 player{};
    Vec3 velocity{};
    bool grounded{false};
    float yaw{0.0f};
    float pitch{-0.08f};
    std::array<bool, 256> previousKeys{};
};

AppState* gApp = nullptr;

bool KeyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool KeyPressed(AppState& app, int vk) {
    const bool now = KeyDown(vk);
    const bool pressed = now && !app.previousKeys[static_cast<std::size_t>(vk)];
    app.previousKeys[static_cast<std::size_t>(vk)] = now;
    return pressed;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_ACTIVATEAPP:
        if (gApp) gApp->active = (wparam != 0);
        return 0;
    case WM_SIZE:
        if (gApp) {
            gApp->width = std::max(1, static_cast<int>(LOWORD(lparam)));
            gApp->height = std::max(1, static_cast<int>(HIWORD(lparam)));
            if (gApp->glrc) glViewport(0, 0, gApp->width, gApp->height);
        }
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }
}

void SetMouseCapture(AppState& app, bool capture) {
    app.mouseCaptured = capture;
    ShowCursor(capture ? FALSE : TRUE);
    if (capture) SetCapture(app.hwnd);
    else ReleaseCapture();
}

void CenterCursor(const AppState& app) {
    POINT p{app.width / 2, app.height / 2};
    ClientToScreen(app.hwnd, &p);
    SetCursorPos(p.x, p.y);
}

void UpdateMouseLook(AppState& app) {
    if (!app.active || !app.mouseCaptured) return;

    POINT center{app.width / 2, app.height / 2};
    ClientToScreen(app.hwnd, &center);
    POINT cursor{};
    GetCursorPos(&cursor);
    const int dx = cursor.x - center.x;
    const int dy = cursor.y - center.y;
    if (dx || dy) {
        app.yaw += static_cast<float>(dx) * kMouseSensitivity;
        app.pitch -= static_cast<float>(dy) * kMouseSensitivity;
        app.pitch = std::clamp(app.pitch, -1.52f, 1.52f);
        SetCursorPos(center.x, center.y);
    }
}

Vec3 Forward(float yaw, float pitch) {
    const float cp = std::cos(pitch);
    return {
        std::sin(yaw) * cp,
        std::sin(pitch),
        -std::cos(yaw) * cp
    };
}

Vec3 FlatForward(float yaw) {
    return {std::sin(yaw), 0.0f, -std::cos(yaw)};
}

Vec3 FlatRight(float yaw) {
    return {std::cos(yaw), 0.0f, std::sin(yaw)};
}

void Respawn(AppState& app) {
    app.player = app.pack.spawn;
    app.velocity = {};
    app.grounded = false;
    std::cout << "[runtime] respawn at "
              << app.player.x << ", " << app.player.y << ", " << app.player.z << "\n";
}

void SelectLocomotionClip(AppState& app, int clip) {
    if (!app.hasPlayerPack || clip < 0) return;
    if (app.currentClip != clip) {
        app.currentClip = clip;
        app.animationTime = 0.0f;
    }
}

void UpdatePlayer(AppState& app, float dt, bool jumpPressed) {
    app.sprinting = KeyDown(VK_SHIFT);
    const float speed = app.sprinting ? kSprintSpeed : kWalkSpeed;
    Vec3 move{};
    const Vec3 f = FlatForward(app.yaw);
    const Vec3 r = FlatRight(app.yaw);

    if (KeyDown('W')) move += f;
    if (KeyDown('S')) move -= f;
    if (KeyDown('D')) move += r;
    if (KeyDown('A')) move -= r;

    const float moveLen = socom::Length(move);
    app.movementAmount = std::min(1.0f, moveLen);
    if (moveLen > 0.001f) {
        move = move / moveLen;
        app.facingYaw = std::atan2(move.x, -move.z);
        SelectLocomotionClip(app, app.sprinting ? app.runClip : app.walkClip);
    } else {
        SelectLocomotionClip(app, app.standClip);
    }
    if (app.currentClip >= 0) app.animationTime += dt;

    if (app.noclip) {
        Vec3 delta = move * speed;
        if (KeyDown(VK_SPACE)) delta.y += speed;
        if (KeyDown(VK_CONTROL)) delta.y -= speed;
        app.player += delta * dt;
        app.velocity = {};
        app.grounded = false;
        return;
    }

    app.velocity.x = move.x * speed;
    app.velocity.z = move.z * speed;

    if (jumpPressed && app.grounded) {
        app.velocity.y = kJumpSpeed;
        app.grounded = false;
    }

    app.velocity.y -= kGravity * dt;
    app.player += app.velocity * dt;

    const auto resolved = socom::ResolvePlayerCapsule(
        app.player,
        kPlayerRadius,
        kPlayerHeight,
        app.pack.collisionTriangles,
        4);

    app.player = resolved.position;
    app.grounded = resolved.grounded;
    if (app.grounded && app.velocity.y < 0.0f) app.velocity.y = 0.0f;

    if (app.player.y < app.pack.boundsMin.y - 25.0f) Respawn(app);
}

void SetupProjection(const AppState& app) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const double aspect = static_cast<double>(app.width) / static_cast<double>(std::max(1, app.height));
    gluPerspective(72.0, aspect, 0.08, 1800.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    if (app.thirdPerson && app.hasPlayerPack) {
        const Vec3 target = app.player + Vec3{0.0f, 1.28f, 0.0f};
        const Vec3 view = Forward(app.yaw, app.pitch * 0.70f);
        const Vec3 eye = target - view * 4.2f + Vec3{0.0f, 0.35f, 0.0f};
        gluLookAt(
            eye.x, eye.y, eye.z,
            target.x, target.y, target.z,
            0.0, 1.0, 0.0);
    } else {
        const Vec3 eye = app.player + Vec3{0.0f, kEyeHeight, 0.0f};
        const Vec3 forward = Forward(app.yaw, app.pitch);
        const Vec3 target = eye + forward;
        gluLookAt(
            eye.x, eye.y, eye.z,
            target.x, target.y, target.z,
            0.0, 1.0, 0.0);
    }
}

GLubyte LitChannel(std::uint8_t c, float lighting) {
    const int value =
        static_cast<int>(static_cast<float>(c) * lighting);
    return static_cast<GLubyte>(
        std::clamp(value, 8, 255));
}

void DrawRenderBatch(
    const AppState& app,
    const socom::RenderBatch& batch,
    bool textured)
{
    const Vec3 lightDir =
        socom::Normalize(Vec3{0.35f, 0.82f, 0.44f});

    glBegin(GL_TRIANGLES);

    const std::size_t first =
        static_cast<std::size_t>(batch.firstVertex);
    const std::size_t end =
        first + static_cast<std::size_t>(batch.vertexCount);

    for (std::size_t i = first; i < end; ++i) {
        const auto& v = app.pack.renderVertices[i];
        const float ndotl =
            std::max(
                0.0f,
                socom::Dot(
                    socom::Normalize(v.normal),
                    lightDir));
        const float lighting =
            0.42f + ndotl * 0.58f;

        glColor4ub(
            LitChannel(v.color[0], lighting),
            LitChannel(v.color[1], lighting),
            LitChannel(v.color[2], lighting),
            v.color[3]);

        if (textured) {
            const float tv =
                app.flipTextureV ? (1.0f - v.uv.y) : v.uv.y;
            glTexCoord2f(v.uv.x, tv);
        }

        glVertex3f(
            v.position.x,
            v.position.y,
            v.position.z);
    }

    glEnd();
}

void RenderWorld(const AppState& app) {
    glPolygonMode(
        GL_FRONT_AND_BACK,
        app.wireframe ? GL_LINE : GL_FILL);

    const bool hasTextures =
        app.texturesEnabled &&
        !app.pack.textures.empty() &&
        app.glTextures.size() == app.pack.textures.size();

    if (!hasTextures) {
        glDisable(GL_TEXTURE_2D);
        for (const auto& batch : app.pack.renderBatches)
            DrawRenderBatch(app, batch, false);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        return;
    }

    glEnable(GL_TEXTURE_2D);
    glTexEnvi(
        GL_TEXTURE_ENV,
        GL_TEXTURE_ENV_MODE,
        GL_MODULATE);

    // Opaque pass.
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDepthMask(GL_TRUE);

    for (const auto& batch : app.pack.renderBatches) {
        if (batch.material >= app.pack.textures.size())
            continue;

        const auto& material =
            app.pack.textures[batch.material];

        if (material.hasTransparency())
            continue;

        glBindTexture(
            GL_TEXTURE_2D,
            app.glTextures[batch.material]);

        DrawRenderBatch(app, batch, true);
    }

    // Transparent/cutout pass. This is intentionally simple for the
    // first native renderer; material-specific PS2 blend modes come later.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.02f);
    glDepthMask(GL_FALSE);

    for (const auto& batch : app.pack.renderBatches) {
        if (batch.material >= app.pack.textures.size())
            continue;

        const auto& material =
            app.pack.textures[batch.material];

        if (!material.hasTransparency())
            continue;

        glBindTexture(
            GL_TEXTURE_2D,
            app.glTextures[batch.material]);

        DrawRenderBatch(app, batch, true);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

Vec3 RotateYaw(Vec3 v, float yaw) {
    const float c = std::cos(yaw);
    const float sn = std::sin(yaw);
    return {c*v.x + sn*v.z, v.y, -sn*v.x + c*v.z};
}

void DrawPlayerBatch(
    AppState& app,
    const socom::PlayerBatch& batch,
    bool textured)
{
    const Vec3 lightDir = socom::Normalize(Vec3{0.35f, 0.82f, 0.44f});
    glBegin(GL_TRIANGLES);
    const std::size_t end = static_cast<std::size_t>(batch.firstVertex) + batch.vertexCount;
    for (std::size_t i=batch.firstVertex; i<end; ++i) {
        const auto& v=app.playerPack.vertices[i];
        Vec3 p{};
        Vec3 n{};
        for (int k=0;k<4;++k) {
            const float w=v.weights[static_cast<std::size_t>(k)];
            if (w <= 0.000001f) continue;
            const auto& m=app.playerSkinMatrices[v.joints[static_cast<std::size_t>(k)]];
            p += socom::TransformPoint(m,v.position)*w;
            n += socom::TransformVector(m,v.normal)*w;
        }
        n=socom::Normalize(n);
        const Vec3 worldN=RotateYaw(n,app.facingYaw);
        const float ndotl=std::max(0.0f,socom::Dot(worldN,lightDir));
        const float lighting=0.45f+ndotl*0.55f;
        const GLubyte shade=static_cast<GLubyte>(std::clamp(static_cast<int>(255.0f*lighting),16,255));
        glColor4ub(shade,shade,shade,255);
        if (textured) {
            const float tv=app.flipTextureV ? (1.0f-v.uv.y) : v.uv.y;
            glTexCoord2f(v.uv.x,tv);
        }
        glVertex3f(p.x,p.y,p.z);
    }
    glEnd();
}

void RenderPlayer(AppState& app) {
    if (!app.hasPlayerPack || !app.playerVisible || !app.thirdPerson || app.currentClip < 0) return;
    socom::EvaluatePlayerSkin(app.playerPack,app.currentClip,app.animationTime,app.playerSkinMatrices);
    if (app.playerSkinMatrices.empty()) return;

    glPushMatrix();
    glTranslatef(app.player.x,app.player.y,app.player.z);
    glRotatef(app.facingYaw * 180.0f / kPi,0.0f,1.0f,0.0f);

    const bool hasTextures = app.texturesEnabled &&
        app.glPlayerTextures.size()==app.playerPack.textures.size() &&
        !app.playerPack.textures.empty();

    if (!hasTextures) {
        glDisable(GL_TEXTURE_2D);
        for (const auto& b:app.playerPack.batches) DrawPlayerBatch(app,b,false);
        glPopMatrix();
        return;
    }

    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDepthMask(GL_TRUE);
    for (const auto& b:app.playerPack.batches) {
        if (b.material>=app.playerPack.textures.size()) continue;
        if (app.playerPack.textures[b.material].hasTransparency()) continue;
        glBindTexture(GL_TEXTURE_2D,app.glPlayerTextures[b.material]);
        DrawPlayerBatch(app,b,true);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER,0.02f);
    glDepthMask(GL_FALSE);
    for (const auto& b:app.playerPack.batches) {
        if (b.material>=app.playerPack.textures.size()) continue;
        if (!app.playerPack.textures[b.material].hasTransparency()) continue;
        glBindTexture(GL_TEXTURE_2D,app.glPlayerTextures[b.material]);
        DrawPlayerBatch(app,b,true);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,0);
    glPopMatrix();
}

void RenderCollision(const AppState& app) {
    if (!app.drawCollision) return;
    glDisable(GL_DEPTH_TEST);
    glColor3ub(40, 255, 80);
    glBegin(GL_LINES);
    for (const auto& t : app.pack.collisionTriangles) {
        glVertex3f(t.a.x,t.a.y,t.a.z); glVertex3f(t.b.x,t.b.y,t.b.z);
        glVertex3f(t.b.x,t.b.y,t.b.z); glVertex3f(t.c.x,t.c.y,t.c.z);
        glVertex3f(t.c.x,t.c.y,t.c.z); glVertex3f(t.a.x,t.a.y,t.a.z);
    }
    glEnd();
    glEnable(GL_DEPTH_TEST);
}

void RenderFrame(AppState& app) {
    glViewport(0,0,app.width,app.height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    SetupProjection(app);
    RenderWorld(app);
    RenderPlayer(app);
    RenderCollision(app);
    SwapBuffers(app.dc);
}

void UpdateTitle(const AppState& app) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1)
       << "SOCOM Native - M8 SP Prototype  |  "
       << "pos " << app.player.x << ", " << app.player.y << ", " << app.player.z
       << "  |  " << (app.grounded ? "ground" : "air")
       << (app.noclip ? "  |  NOCLIP" : "")
       << "  |  TEX " << (app.texturesEnabled ? "ON" : "OFF")
       << (app.flipTextureV ? " VFLIP" : "")
       << (app.hasPlayerPack ? (app.thirdPerson ? "  |  3P" : "  |  1P") : "")
       << (app.hasPlayerPack && app.currentClip >= 0 ? ("  |  " + app.playerPack.clips[static_cast<std::size_t>(app.currentClip)].name) : "")
       << "  |  F1 controls";
    SetWindowTextA(app.hwnd, ss.str().c_str());
}

void PrintControls() {
    std::cout
        << "\nSOCOM Native - Single Player Runtime Milestone 1\n"
        << "------------------------------------------------\n"
        << "WASD        Move\n"
        << "Mouse       Look\n"
        << "Shift       Sprint\n"
        << "Space       Jump\n"
        << "F2          Toggle world wireframe\n"
        << "F3          Toggle collision debug\n"
        << "F4          Toggle noclip (Space up / Ctrl down)\n"
        << "F5          Respawn\n"
        << "F6          Toggle SOCOM textures\n"
        << "F7          Flip texture V orientation\n"
        << "F8          Toggle first/third-person camera\n"
        << "F9          Toggle player model\n"
        << "Tab         Release/capture mouse\n"
        << "F1          Print controls\n"
        << "Esc         Quit\n\n";
}

void UploadTextures(AppState& app) {
    if (app.pack.textures.empty()) {
        std::cout << "[runtime] pack has no textures; using vertex colors\n";
        return;
    }

    app.glTextures.resize(app.pack.textures.size());
    glGenTextures(
        static_cast<GLsizei>(app.glTextures.size()),
        app.glTextures.data());

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for (std::size_t i = 0; i < app.pack.textures.size(); ++i) {
        const auto& source = app.pack.textures[i];

        glBindTexture(
            GL_TEXTURE_2D,
            app.glTextures[i]);

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MIN_FILTER,
            GL_LINEAR);
        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MAG_FILTER,
            GL_LINEAR);
        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_S,
            GL_REPEAT);
        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_T,
            GL_REPEAT);

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            static_cast<GLsizei>(source.width),
            static_cast<GLsizei>(source.height),
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            source.rgba.data());

        const GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            throw std::runtime_error(
                "OpenGL texture upload failed for " +
                source.name +
                " (error " +
                std::to_string(
                    static_cast<unsigned int>(error)) +
                ")");
        }
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    std::cout
        << "[runtime] uploaded "
        << app.pack.textures.size()
        << " SOCOM textures\n";
}

void UploadPlayerTextures(AppState& app) {
    if (!app.hasPlayerPack || app.playerPack.textures.empty()) return;
    app.glPlayerTextures.resize(app.playerPack.textures.size());
    glGenTextures(static_cast<GLsizei>(app.glPlayerTextures.size()),app.glPlayerTextures.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for (std::size_t i=0;i<app.playerPack.textures.size();++i) {
        const auto& source=app.playerPack.textures[i];
        glBindTexture(GL_TEXTURE_2D,app.glPlayerTextures[i]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,static_cast<GLsizei>(source.width),static_cast<GLsizei>(source.height),0,GL_RGBA,GL_UNSIGNED_BYTE,source.rgba.data());
        if (glGetError()!=GL_NO_ERROR) throw std::runtime_error("OpenGL player texture upload failed: "+source.name);
    }
    glBindTexture(GL_TEXTURE_2D,0);
    std::cout << "[runtime] uploaded " << app.playerPack.textures.size() << " SEAL textures\\n";
}

void ShutdownTextures(AppState& app) {
    if (!app.glPlayerTextures.empty()) {
        glDeleteTextures(static_cast<GLsizei>(app.glPlayerTextures.size()),app.glPlayerTextures.data());
        app.glPlayerTextures.clear();
    }
    if (!app.glTextures.empty()) {
        glDeleteTextures(
            static_cast<GLsizei>(app.glTextures.size()),
            app.glTextures.data());
        app.glTextures.clear();
    }
}

void InitOpenGL(AppState& app) {
    app.dc = GetDC(app.hwnd);
    if (!app.dc) throw std::runtime_error("GetDC failed");

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    const int pf = ChoosePixelFormat(app.dc, &pfd);
    if (!pf || !SetPixelFormat(app.dc, pf, &pfd))
        throw std::runtime_error("failed to choose/set OpenGL pixel format");

    app.glrc = wglCreateContext(app.dc);
    if (!app.glrc || !wglMakeCurrent(app.dc, app.glrc))
        throw std::runtime_error("failed to create OpenGL context");

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE); // Original content can contain mixed/double-sided geometry.
    glShadeModel(GL_SMOOTH);
    glClearColor(0.34f, 0.45f, 0.56f, 1.0f);

    glEnable(GL_FOG);
    GLfloat fogColor[4] = {0.34f,0.45f,0.56f,1.0f};
    glFogfv(GL_FOG_COLOR, fogColor);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 300.0f);
    glFogf(GL_FOG_END, 950.0f);
}

void ShutdownOpenGL(AppState& app) {
    if (app.mouseCaptured) SetMouseCapture(app, false);
    if (app.glrc) ShutdownTextures(app);
    if (app.glrc) {
        wglMakeCurrent(nullptr,nullptr);
        wglDeleteContext(app.glrc);
        app.glrc = nullptr;
    }
    if (app.dc && app.hwnd) {
        ReleaseDC(app.hwnd, app.dc);
        app.dc = nullptr;
    }
}

HWND CreateMainWindow(AppState& app, HINSTANCE instance) {
    const char* className = "SocomNativeRuntimeWindow";
    WNDCLASSA wc{};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = className;
    if (!RegisterClassA(&wc)) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("RegisterClass failed");
    }

    RECT rect{0,0,app.width,app.height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExA(
        0, className, "SOCOM Native - M8 SP Prototype",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right-rect.left, rect.bottom-rect.top,
        nullptr,nullptr,instance,nullptr);
    if (!hwnd) throw std::runtime_error("CreateWindow failed");
    return hwnd;
}

struct Args {
    std::string packPath;
    std::string playerPath;
    bool overrideSpawn{false};
    Vec3 spawn{};
};

Args ParseArgs(int argc, char** argv) {
    Args args;
    for (int i=1; i<argc; ++i) {
        const std::string a = argv[i];
        if (a == "--pack" && i+1 < argc) {
            args.packPath = argv[++i];
        } else if (a == "--player" && i+1 < argc) {
            args.playerPath = argv[++i];
        } else if (a == "--spawn" && i+3 < argc) {
            args.overrideSpawn = true;
            args.spawn.x = std::stof(argv[++i]);
            args.spawn.y = std::stof(argv[++i]);
            args.spawn.z = std::stof(argv[++i]);
        } else if (a == "--help" || a == "-h") {
            std::cout << "usage: socom-native --pack <m8_runtime.snr> [--player seal_A_des.spr] [--spawn X Y Z]\n";
            std::exit(0);
        } else if (args.packPath.empty() && !a.empty() && a[0] != '-') {
            args.packPath = a;
        } else {
            throw std::runtime_error("unknown/incomplete argument: " + a);
        }
    }
    if (args.packPath.empty())
        throw std::runtime_error("missing runtime pack; use --pack <m8_runtime.snr>");
    return args;
}

} // namespace

int main(int argc, char** argv) {
    try {
        SetProcessDPIAware();
        const Args args = ParseArgs(argc,argv);

        AppState app;
        gApp = &app;
        std::cout << "[runtime] loading " << args.packPath << "\n";
        app.pack = socom::LoadRuntimePack(args.packPath);
        if (args.overrideSpawn) app.pack.spawn = args.spawn;
        if (!args.playerPath.empty()) {
            std::cout << "[runtime] loading player " << args.playerPath << "\n";
            app.playerPack = socom::LoadPlayerPack(args.playerPath);
            app.hasPlayerPack = true;
            app.standClip = socom::FindPlayerClip(app.playerPack,"seal_stand");
            app.walkClip = socom::FindPlayerClip(app.playerPack,"seal_walk");
            app.runClip = socom::FindPlayerClip(app.playerPack,"seal_run");
            app.currentClip = app.standClip;
            if (app.standClip < 0 || app.walkClip < 0 || app.runClip < 0)
                throw std::runtime_error("player pack is missing stand/walk/run clips");
            std::cout << "[runtime] player triangles: " << app.playerPack.vertices.size()/3
                      << " joints: " << app.playerPack.joints.size()
                      << " clips: " << app.playerPack.clips.size() << "\n";
        }

        std::cout << "[runtime] render triangles: " << app.pack.renderVertices.size()/3 << "\n";
        std::cout << "[runtime] collision triangles: " << app.pack.collisionTriangles.size() << "\n";
        std::cout << "[runtime] render batches: " << app.pack.renderBatches.size() << "\n";
        std::cout << "[runtime] textures: " << app.pack.textures.size() << "\n";
        std::cout << "[runtime] map bounds: ("
                  << app.pack.boundsMin.x << ", " << app.pack.boundsMin.y << ", " << app.pack.boundsMin.z
                  << ") -> ("
                  << app.pack.boundsMax.x << ", " << app.pack.boundsMax.y << ", " << app.pack.boundsMax.z << ")\n";

        const HINSTANCE instance = GetModuleHandleA(nullptr);
        app.hwnd = CreateMainWindow(app,instance);
        InitOpenGL(app);
        UploadTextures(app);
        UploadPlayerTextures(app);
        Respawn(app);
        PrintControls();
        CenterCursor(app);
        SetMouseCapture(app,true);

        LARGE_INTEGER frequency{}, previous{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&previous);
        double accumulator = 0.0;
        double titleTimer = 0.0;
        bool jumpQueued = false;

        while (app.running) {
            MSG msg{};
            while (PeekMessageA(&msg,nullptr,0,0,PM_REMOVE)) {
                if (msg.message == WM_QUIT) app.running = false;
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            if (!app.running) break;

            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            double frameDt = static_cast<double>(now.QuadPart-previous.QuadPart) /
                             static_cast<double>(frequency.QuadPart);
            previous = now;
            frameDt = std::clamp(frameDt,0.0,0.05);
            accumulator += frameDt;
            titleTimer += frameDt;

            if (KeyPressed(app,VK_ESCAPE)) {
                app.running = false;
                continue;
            }
            if (KeyPressed(app,VK_TAB)) {
                SetMouseCapture(app,!app.mouseCaptured);
                if (app.mouseCaptured) CenterCursor(app);
            }
            if (KeyPressed(app,VK_F1)) PrintControls();
            if (KeyPressed(app,VK_F2)) app.wireframe = !app.wireframe;
            if (KeyPressed(app,VK_F3)) app.drawCollision = !app.drawCollision;
            if (KeyPressed(app,VK_F4)) {
                app.noclip = !app.noclip;
                app.velocity = {};
                std::cout << "[runtime] noclip " << (app.noclip ? "ON" : "OFF") << "\n";
            }
            if (KeyPressed(app,VK_F5)) Respawn(app);
            if (KeyPressed(app,VK_F6)) {
                app.texturesEnabled = !app.texturesEnabled;
                std::cout
                    << "[runtime] textures "
                    << (app.texturesEnabled ? "ON" : "OFF")
                    << "\n";
            }
            if (KeyPressed(app,VK_F7)) {
                app.flipTextureV = !app.flipTextureV;
                std::cout
                    << "[runtime] texture V flip "
                    << (app.flipTextureV ? "ON" : "OFF")
                    << "\n";
            }
            if (KeyPressed(app,VK_F8) && app.hasPlayerPack) {
                app.thirdPerson = !app.thirdPerson;
                std::cout << "[runtime] camera " << (app.thirdPerson ? "THIRD PERSON" : "FIRST PERSON") << "\n";
            }
            if (KeyPressed(app,VK_F9) && app.hasPlayerPack) {
                app.playerVisible = !app.playerVisible;
                std::cout << "[runtime] player model " << (app.playerVisible ? "ON" : "OFF") << "\n";
            }
            if (KeyPressed(app,VK_SPACE)) jumpQueued = true;

            UpdateMouseLook(app);

            int safetySteps = 0;
            while (accumulator >= kFixedStep && safetySteps < 8) {
                UpdatePlayer(app,static_cast<float>(kFixedStep),jumpQueued);
                jumpQueued = false;
                accumulator -= kFixedStep;
                ++safetySteps;
            }
            if (safetySteps == 8) accumulator = 0.0;

            if (app.active) RenderFrame(app);
            else Sleep(20);

            if (titleTimer >= 0.25) {
                UpdateTitle(app);
                titleTimer = 0.0;
            }
            Sleep(1);
        }

        ShutdownOpenGL(app);
        if (app.hwnd) DestroyWindow(app.hwnd);
        gApp = nullptr;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        MessageBoxA(nullptr,e.what(),"SOCOM Native Runtime Error",MB_OK|MB_ICONERROR);
        return 1;
    }
}
