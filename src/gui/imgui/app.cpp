#include "gui/imgui/app.hpp"
#include "gui/theme.hpp"

#include "space/object/object_descriptor.hpp"
#include "space/object/object.hpp"
#include "space/medium.hpp"
#include "transducer/transducer.hpp"
#include "transducer/transducer_array.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <variant>
#include <chrono>

namespace gui::imgui_app {

// ── mat4 (column-major, OpenGL convention) ────────────────────────────────────

struct mat4 {
    float m[16] = {};
    static mat4 identity() {
        mat4 r; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1.f; return r;
    }
};

static mat4 mul(const mat4& a, const mat4& b) {
    mat4 r;
    for(int c=0;c<4;++c) for(int row=0;row<4;++row) for(int k=0;k<4;++k)
        r.m[c*4+row] += a.m[k*4+row] * b.m[c*4+k];
    return r;
}

static mat4 perspective(float fovy, float aspect, float znear, float zfar) {
    float f = 1.f / std::tan(fovy * .5f);
    mat4 r{};
    r.m[0]  = f/aspect;
    r.m[5]  = f;
    r.m[10] = (zfar+znear)/(znear-zfar);
    r.m[11] = -1.f;
    r.m[14] = 2.f*zfar*znear/(znear-zfar);
    return r;
}

static mat4 look_at(float ex,float ey,float ez,
                    float cx,float cy,float cz,
                    float ux,float uy,float uz) {
    float fx=cx-ex,fy=cy-ey,fz=cz-ez;
    float fl=std::sqrt(fx*fx+fy*fy+fz*fz); if(fl<1e-6f) fl=1e-6f;
    fx/=fl; fy/=fl; fz/=fl;
    float rx=fy*uz-fz*uy, ry=fz*ux-fx*uz, rz=fx*uy-fy*ux;
    float rl=std::sqrt(rx*rx+ry*ry+rz*rz); if(rl<1e-6f) rl=1e-6f;
    rx/=rl; ry/=rl; rz/=rl;
    float vx=ry*fz-rz*fy, vy=rz*fx-rx*fz, vz=rx*fy-ry*fx;
    mat4 r{};
    r.m[0]=rx;  r.m[1]=vx;  r.m[2]=-fx; r.m[3]=0;
    r.m[4]=ry;  r.m[5]=vy;  r.m[6]=-fy; r.m[7]=0;
    r.m[8]=rz;  r.m[9]=vz;  r.m[10]=-fz;r.m[11]=0;
    r.m[12]=-(rx*ex+ry*ey+rz*ez);
    r.m[13]=-(vx*ex+vy*ey+vz*ez);
    r.m[14]= (fx*ex+fy*ey+fz*ez);
    r.m[15]=1;
    return r;
}

// ── GLSL shaders ──────────────────────────────────────────────────────────────

static const char* LINE_VERT = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
void main(){ gl_Position = uMVP * vec4(aPos, 1.0); }
)";
static const char* LINE_FRAG = R"(
#version 330 core
uniform vec4 uColor;
out vec4 FragColor;
void main(){ FragColor = uColor; }
)";
static const char* QUAD_VERT = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
uniform mat4 uMVP;
void main(){ vUV = aUV; gl_Position = uMVP * vec4(aPos, 1.0); }
)";
static const char* QUAD_FRAG = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 FragColor;
void main(){ FragColor = texture(uTex, vUV); }
)";

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if(!ok) {
        char log[512]; glGetShaderInfoLog(sh, 512, nullptr, log);
        throw std::runtime_error(std::string("shader compile: ") + log);
    }
    return sh;
}

static GLuint link_program(const char* vs, const char* fs) {
    GLuint v=compile_shader(GL_VERTEX_SHADER,vs);
    GLuint f=compile_shader(GL_FRAGMENT_SHADER,fs);
    GLuint p=glCreateProgram();
    glAttachShader(p,v); glAttachShader(p,f);
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok) {
        char log[512]; glGetProgramInfoLog(p, 512, nullptr, log);
        throw std::runtime_error(std::string("program link: ") + log);
    }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

// ── Constructor ───────────────────────────────────────────────────────────────

App::App() {
    if(!glfwInit()) throw std::runtime_error("glfwInit failed");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(win_w_, win_h_,
                               "ACOUSTIC HOLOGRAPHY ENGINE", nullptr, nullptr);
    if(!window_) { glfwTerminate(); throw std::runtime_error("glfwCreateWindow failed"); }

    glfwSetWindowUserPointer(window_, this);
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        throw std::runtime_error("gladLoadGL failed");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;   // don't persist layout — we manage it ourselves

    // Load JetBrains Mono at 13px (body) and 15px (values)
#if defined(JETBRAINS_MONO_REGULAR)
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    font_body_  = io.Fonts->AddFontFromFileTTF(JETBRAINS_MONO_REGULAR, 13.f, &cfg);
    font_value_ = io.Fonts->AddFontFromFileTTF(JETBRAINS_MONO_REGULAR, 15.f, &cfg);
    if(!font_body_ || !font_value_) {
        // Fallback: ImGui's built-in font
        font_body_  = io.Fonts->AddFontDefault();
        font_value_ = font_body_;
    }
#else
    font_body_  = io.Fonts->AddFontDefault();
    font_value_ = font_body_;
#endif

    theme::apply();

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    init_gl();

    // Default scene: 64×64×32 air grid
    auto& g = model_.world.grid;
    g.grid      = {64, 64, 32};
    g.cell_size = 0.005;
    g.default_medium.density            = 1.21;
    g.default_medium.stiffness          = 142000.0;
    g.default_medium.sound_speed        = 343.0;
    g.default_medium.acoustic_impedance = 415.0;
    g.default_medium.is_rigid           = false;

    slice_idx_ = g.grid.z / 2;

    float gx = g.grid.x * (float)g.cell_size;
    float gy = g.grid.y * (float)g.cell_size;
    float gz = g.grid.z * (float)g.cell_size;
    float diag = std::sqrt(gx*gx + gy*gy + gz*gz);
    cam_.distance = diag * 2.0f;
    cam_.min_dist = diag * 0.5f;
    cam_.max_dist = diag * 5.0f;

    inv_params_.gs_iterations = 100;
}

// ── Destructor ────────────────────────────────────────────────────────────────

App::~App() {
    if(field_tex_)  glDeleteTextures(1, &field_tex_);
    if(fbo_color_)  glDeleteTextures(1, &fbo_color_);
    if(fbo_depth_)  glDeleteRenderbuffers(1, &fbo_depth_);
    if(fbo_)        glDeleteFramebuffers(1, &fbo_);
    if(floor_vao_) { glDeleteVertexArrays(1,&floor_vao_); glDeleteBuffers(1,&floor_vbo_); }
    if(floor_major_vao_) { glDeleteVertexArrays(1,&floor_major_vao_); glDeleteBuffers(1,&floor_major_vbo_); }
    if(axis_vao_)  { glDeleteVertexArrays(1,&axis_vao_);  glDeleteBuffers(1,&axis_vbo_); }
    if(slice_vao_) { glDeleteVertexArrays(1,&slice_vao_); glDeleteBuffers(1,&slice_vbo_);
                     glDeleteBuffers(1,&slice_ebo_); }
    if(tran_vao_)  { glDeleteVertexArrays(1,&tran_vao_);  glDeleteBuffers(1,&tran_vbo_); }
    if(box_vao_)   { glDeleteVertexArrays(1,&box_vao_);   glDeleteBuffers(1,&box_vbo_);
                     glDeleteBuffers(1,&box_ebo_); }
    if(obj_vao_)   { glDeleteVertexArrays(1,&obj_vao_);   glDeleteBuffers(1,&obj_vbo_); }
    if(line_prog_)  glDeleteProgram(line_prog_);
    if(quad_prog_)  glDeleteProgram(quad_prog_);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window_);
    glfwTerminate();
}

// ── Dispatch ──────────────────────────────────────────────────────────────────

void App::dispatch(simulate::sim_base::actions a) {
    model_ = simulate::sim_base::update(model_, std::move(a));
    // Any world change makes the field stale
    if(!std::holds_alternative<simulate::sim_state::actions>(a)) {
        if(model_.state.current == simulate::sim_state::VALID)
            model_.state.current = simulate::sim_state::OLD;
        grid_dirty_ = true;
    }
}

void App::run_forward_solver() {
    anim_.sweeping = true;
    anim_.sweep    = 0.f;

    auto t0 = std::chrono::steady_clock::now();
    dispatch_state(simulate::sim_state::action::run_solver{ model_.world });
    auto t1 = std::chrono::steady_clock::now();

    last_solve_ms_ = std::chrono::duration<double,std::milli>(t1-t0).count();
    field_dirty_   = true;
    anim_.valid_flash = 1.f;
}

void App::run_inverse_solver() {
    if(inv_targets_.empty()) return;
    inv_params_.solver_method = (inv_method_ == 0)
        ? simulate::inverse_solver::method::backprop
        : simulate::inverse_solver::method::gerchberg_saxton;
    auto res = simulate::inverse_solver::solve(model_.world, inv_targets_, inv_params_);
    for(size_t ai = 0; ai < res.phases.size(); ++ai) {
        for(size_t ti = 0; ti < res.phases[ai].size(); ++ti) {
            dispatch_world(world::action::mod_array{
                ai, transducer::tran_array::action::single_adjust{
                    (int)ti,
                    transducer::single::action::new_phase{res.phases[ai][ti]}}});
        }
    }
    field_dirty_ = true;
}

// ── GL resource init ──────────────────────────────────────────────────────────

void App::init_gl() {
    line_prog_ = link_program(LINE_VERT, LINE_FRAG);
    quad_prog_ = link_program(QUAD_VERT, QUAD_FRAG);

    glGenTextures(1, &field_tex_);
    glBindTexture(GL_TEXTURE_2D, field_tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // 1×1 grid-mid placeholder
    uint8_t px[4] = {0x0A, 0x15, 0x20, 0xFF};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void App::rebuild_fbo(int w, int h) {
    if(fbo_w_ == w && fbo_h_ == h) return;
    fbo_w_ = w; fbo_h_ = h;

    if(fbo_)       glDeleteFramebuffers(1,  &fbo_);
    if(fbo_color_) glDeleteTextures(1,      &fbo_color_);
    if(fbo_depth_) glDeleteRenderbuffers(1, &fbo_depth_);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &fbo_color_);
    glBindTexture(GL_TEXTURE_2D, fbo_color_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_color_, 0);

    glGenRenderbuffers(1, &fbo_depth_);
    glBindRenderbuffer(GL_RENDERBUFFER, fbo_depth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo_depth_);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void App::rebuild_floor_mesh() {
    if(floor_vao_) { glDeleteVertexArrays(1,&floor_vao_); glDeleteBuffers(1,&floor_vbo_); floor_vao_=0; }
    if(floor_major_vao_) { glDeleteVertexArrays(1,&floor_major_vao_); glDeleteBuffers(1,&floor_major_vbo_); floor_major_vao_=0; }

    const auto& g = model_.world.grid;
    float cs  = (float)g.cell_size;
    float gx  = g.grid.x * cs;
    float gy  = g.grid.y * cs;
    float pad = 0.2f;
    float x0  = -gx*pad,  x1 = gx*(1.f+pad);
    float y0  = -gy*pad,  y1 = gy*(1.f+pad);

    // Secondary grid — every 10 cells (dim lines)
    {
        std::vector<float> verts;
        for(int i = 0; i <= (int)g.grid.x; i += 10) {
            float x = i * cs;
            verts.insert(verts.end(), {x, y0, 0.f, x, y1, 0.f});
        }
        for(int j = 0; j <= (int)g.grid.y; j += 10) {
            float y = j * cs;
            verts.insert(verts.end(), {x0, y, 0.f, x1, y, 0.f});
        }
        floor_vcount_ = (int)(verts.size() / 3);

        glGenVertexArrays(1, &floor_vao_);
        glGenBuffers(1, &floor_vbo_);
        glBindVertexArray(floor_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, floor_vbo_);
        glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), nullptr);
        glBindVertexArray(0);
    }

    // Major grid — every 50 cells (cyan at 30% alpha, with glow feel via brighter color)
    {
        std::vector<float> verts;
        for(int i = 0; i <= (int)g.grid.x; i += 50) {
            float x = i * cs;
            verts.insert(verts.end(), {x, y0, 0.f, x, y1, 0.f});
        }
        for(int j = 0; j <= (int)g.grid.y; j += 50) {
            float y = j * cs;
            verts.insert(verts.end(), {x0, y, 0.f, x1, y, 0.f});
        }
        floor_major_vcount_ = (int)(verts.size() / 3);

        glGenVertexArrays(1, &floor_major_vao_);
        glGenBuffers(1, &floor_major_vbo_);
        glBindVertexArray(floor_major_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, floor_major_vbo_);
        glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), nullptr);
        glBindVertexArray(0);
    }
}

void App::rebuild_axis_mesh() {
    if(axis_vao_) { glDeleteVertexArrays(1,&axis_vao_); glDeleteBuffers(1,&axis_vbo_); axis_vao_=0; }

    const auto& g = model_.world.grid;
    float cs = (float)g.cell_size;
    float cx = g.grid.x * cs * .5f;
    float cy = g.grid.y * cs * .5f;
    float cz = g.grid.z * cs * .5f;

    float verts[] = {
        0,    cy,   cz,   g.grid.x*cs, cy,           cz,   // X
        cx,   0,    cz,   cx,          g.grid.y*cs,  cz,   // Y
        cx,   cy,   0,    cx,          cy,           g.grid.z*cs  // Z
    };

    glGenVertexArrays(1, &axis_vao_);
    glGenBuffers(1, &axis_vbo_);
    glBindVertexArray(axis_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, axis_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), nullptr);
    glBindVertexArray(0);
}

void App::rebuild_slice_quad() {
    if(slice_vao_) {
        glDeleteVertexArrays(1,&slice_vao_); glDeleteBuffers(1,&slice_vbo_);
        glDeleteBuffers(1,&slice_ebo_); slice_vao_=0;
    }

    const auto& g = model_.world.grid;
    float cs = (float)g.cell_size;
    float gx = g.grid.x * cs;
    float gy = g.grid.y * cs;
    float gz = g.grid.z * cs;
    float si = slice_idx_ * cs;

    float verts[20]; // 4 × (xyz + uv)
    if(slice_axis_ == simulate::field_query::slice_axis::XY) {
        float z = si;
        verts[0]=0;  verts[1]=0;  verts[2]=z; verts[3]=0; verts[4]=0;
        verts[5]=gx; verts[6]=0;  verts[7]=z; verts[8]=1; verts[9]=0;
        verts[10]=gx;verts[11]=gy;verts[12]=z;verts[13]=1;verts[14]=1;
        verts[15]=0; verts[16]=gy;verts[17]=z;verts[18]=0;verts[19]=1;
    } else if(slice_axis_ == simulate::field_query::slice_axis::XZ) {
        float y = si;
        verts[0]=0;  verts[1]=y; verts[2]=0;  verts[3]=0; verts[4]=0;
        verts[5]=gx; verts[6]=y; verts[7]=0;  verts[8]=1; verts[9]=0;
        verts[10]=gx;verts[11]=y;verts[12]=gz;verts[13]=1;verts[14]=1;
        verts[15]=0; verts[16]=y;verts[17]=gz;verts[18]=0;verts[19]=1;
    } else { // YZ
        float x = si;
        verts[0]=x; verts[1]=0;  verts[2]=0;  verts[3]=0; verts[4]=0;
        verts[5]=x; verts[6]=gy; verts[7]=0;  verts[8]=1; verts[9]=0;
        verts[10]=x;verts[11]=gy;verts[12]=gz;verts[13]=1;verts[14]=1;
        verts[15]=x;verts[16]=0; verts[17]=gz;verts[18]=0;verts[19]=1;
    }
    uint16_t idx[] = {0,1,2, 0,2,3};

    glGenVertexArrays(1, &slice_vao_);
    glGenBuffers(1, &slice_vbo_);
    glGenBuffers(1, &slice_ebo_);
    glBindVertexArray(slice_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, slice_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, slice_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)(3*sizeof(float)));
    glBindVertexArray(0);
}

void App::rebuild_tran_mesh() {
    if(tran_vao_) { glDeleteVertexArrays(1,&tran_vao_); glDeleteBuffers(1,&tran_vbo_); tran_vao_=0; }

    std::vector<float> verts;
    float sz = (float)model_.world.grid.cell_size * 2.f;

    for(const auto& arr : model_.world.transducers) {
        for(const auto& t : arr.tran_array) {
            float x = (float)t.position.x;
            float y = (float)t.position.y;
            float z = (float)t.position.z;
            // Diamond (◆): 4 verts at ±sz
            verts.insert(verts.end(), {x,    y+sz, z});
            verts.insert(verts.end(), {x+sz, y,    z});
            verts.insert(verts.end(), {x,    y-sz, z});
            verts.insert(verts.end(), {x-sz, y,    z});
            verts.insert(verts.end(), {x,    y+sz, z}); // close
        }
    }
    tran_vcount_ = (int)(verts.size() / 3);
    if(tran_vcount_ == 0) return;

    glGenVertexArrays(1, &tran_vao_);
    glGenBuffers(1, &tran_vbo_);
    glBindVertexArray(tran_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, tran_vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), nullptr);
    glBindVertexArray(0);
}

void App::rebuild_box_mesh() {
    if(box_vao_) { glDeleteVertexArrays(1,&box_vao_); glDeleteBuffers(1,&box_vbo_);
                   glDeleteBuffers(1,&box_ebo_); box_vao_=0; }

    const auto& g = model_.world.grid;
    float gx = g.grid.x * (float)g.cell_size;
    float gy = g.grid.y * (float)g.cell_size;
    float gz = g.grid.z * (float)g.cell_size;

    float v[8][3] = {
        {0,  0,  0 }, {gx, 0,  0 }, {gx, gy, 0 }, {0,  gy, 0 },
        {0,  0,  gz}, {gx, 0,  gz}, {gx, gy, gz}, {0,  gy, gz}
    };
    // 12 edges
    uint16_t e[] = {
        0,1, 1,2, 2,3, 3,0,  // bottom
        4,5, 5,6, 6,7, 7,4,  // top
        0,4, 1,5, 2,6, 3,7   // verticals
    };

    glGenVertexArrays(1, &box_vao_);
    glGenBuffers(1, &box_vbo_);
    glGenBuffers(1, &box_ebo_);
    glBindVertexArray(box_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, box_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, box_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(e), e, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), nullptr);
    glBindVertexArray(0);
}

// ── Object wireframe mesh ─────────────────────────────────────────────────────

static void build_obj_verts(const space::grid::grid_model& g, std::vector<float>& verts) {
    constexpr int SEGS = 32;
    const float PI2 = 2.f * (float)std::numbers::pi;
    for(const auto& obj : g.objects) {
        std::visit([&](const auto& s) {
            using T = std::decay_t<decltype(s)>;
            if constexpr(std::is_same_v<T, space::object::shapes::sphere_model>) {
                float cx = (float)s.world_position.x;
                float cy = (float)s.world_position.y;
                float cz = (float)s.world_position.z;
                float r  = (float)s.scale;
                for(int i = 0; i < SEGS; ++i) {
                    float a0 = i * PI2 / SEGS, a1 = (i+1) * PI2 / SEGS;
                    // XY circle
                    verts.insert(verts.end(), {cx+r*std::cos(a0), cy+r*std::sin(a0), cz,
                                               cx+r*std::cos(a1), cy+r*std::sin(a1), cz});
                    // XZ circle
                    verts.insert(verts.end(), {cx+r*std::cos(a0), cy, cz+r*std::sin(a0),
                                               cx+r*std::cos(a1), cy, cz+r*std::sin(a1)});
                    // YZ circle
                    verts.insert(verts.end(), {cx, cy+r*std::cos(a0), cz+r*std::sin(a0),
                                               cx, cy+r*std::cos(a1), cz+r*std::sin(a1)});
                }
            } else {
                float cx = (float)s.world_position.x;
                float cy = (float)s.world_position.y;
                float cz = (float)s.world_position.z;
                float hx = (float)s.scale.i * 0.5f;
                float hy = (float)s.scale.j * 0.5f;
                float hz = (float)s.scale.k * 0.5f;
                float v[8][3] = {
                    {cx-hx,cy-hy,cz-hz},{cx+hx,cy-hy,cz-hz},
                    {cx+hx,cy+hy,cz-hz},{cx-hx,cy+hy,cz-hz},
                    {cx-hx,cy-hy,cz+hz},{cx+hx,cy-hy,cz+hz},
                    {cx+hx,cy+hy,cz+hz},{cx-hx,cy+hy,cz+hz},
                };
                static const int edges[12][2] = {
                    {0,1},{1,2},{2,3},{3,0},
                    {4,5},{5,6},{6,7},{7,4},
                    {0,4},{1,5},{2,6},{3,7},
                };
                for(auto& e : edges)
                    verts.insert(verts.end(), {v[e[0]][0],v[e[0]][1],v[e[0]][2],
                                               v[e[1]][0],v[e[1]][1],v[e[1]][2]});
            }
        }, obj.shape);
    }
}

// Compute the grid-cell offset of the SDF bounding-box lower corner.
// Must be dispatched as transform_mod any time world_position or scale changes.
static space::utility::cell_point sdf_transform(
    const space::object::shapes::shape_model& shape, double cell_size)
{
    return std::visit([cell_size](const auto& s) -> space::utility::cell_point {
        using T = std::decay_t<decltype(s)>;
        double ox, oy, oz;
        if constexpr(std::is_same_v<T, space::object::shapes::sphere_model>) {
            ox = s.world_position.x - s.scale;
            oy = s.world_position.y - s.scale;
            oz = s.world_position.z - s.scale;
        } else {
            ox = s.world_position.x - s.scale.i * 0.5;
            oy = s.world_position.y - s.scale.j * 0.5;
            oz = s.world_position.z - s.scale.k * 0.5;
        }
        return {
            (uint16_t)std::max(0, (int)std::floor(ox / cell_size)),
            (uint16_t)std::max(0, (int)std::floor(oy / cell_size)),
            (uint16_t)std::max(0, (int)std::floor(oz / cell_size)),
        };
    }, shape);
}

void App::rebuild_obj_mesh() {
    if(obj_vao_) { glDeleteVertexArrays(1,&obj_vao_); glDeleteBuffers(1,&obj_vbo_); obj_vao_=0; }
    std::vector<float> verts;
    build_obj_verts(model_.world.grid, verts);
    obj_vcount_ = (int)(verts.size() / 3);
    if(!obj_vcount_) return;
    glGenVertexArrays(1, &obj_vao_); glGenBuffers(1, &obj_vbo_);
    glBindVertexArray(obj_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, obj_vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), nullptr);
    glBindVertexArray(0);
}

void App::upload_field_slice() {
    const auto& g = model_.world.grid;
    auto sl = simulate::field_query::extract_slice(
        model_.state, g.grid, slice_axis_, (uint16_t)slice_idx_);

    std::vector<double> values;
    if(overlay_mode_ == 0)      values = simulate::field_query::magnitude(sl);
    else if(overlay_mode_ == 1) values = simulate::field_query::phase(sl);
    else                        values = simulate::field_query::intensity(sl);

    // Normalise
    double vmax = 0;
    for(auto v : values) vmax = std::max(vmax, std::abs(v));
    if(vmax < 1e-12) vmax = 1.0;

    std::vector<uint8_t> pixels(sl.width * sl.height * 4);
    for(size_t i = 0; i < values.size(); ++i) {
        double t = values[i] / vmax;
        theme::RGB8 c;
        if(overlay_mode_ == 1)
            c = theme::colormap_phase(values[i]);
        else
            c = theme::colormap_mag(t);
        pixels[i*4+0] = c.r;
        pixels[i*4+1] = c.g;
        pixels[i*4+2] = c.b;
        pixels[i*4+3] = 0xFF;
    }

    glBindTexture(GL_TEXTURE_2D, field_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sl.width, sl.height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    field_dirty_ = false;
}

// ── 3D Scene ──────────────────────────────────────────────────────────────────

void App::render_3d_scene(int w, int h) {
    if(w <= 0 || h <= 0) return;

    if(grid_dirty_) {
        rebuild_floor_mesh();
        rebuild_axis_mesh();
        rebuild_slice_quad();
        rebuild_tran_mesh();
        rebuild_box_mesh();
        rebuild_obj_mesh();
        grid_dirty_ = false;
    }
    if(field_dirty_ && model_.state.current != simulate::sim_state::UNCOMPUTED)
        upload_field_slice();

    rebuild_fbo(w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w, h);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    const auto& g = model_.world.grid;
    float gx = g.grid.x * (float)g.cell_size;
    float gy = g.grid.y * (float)g.cell_size;
    float gz = g.grid.z * (float)g.cell_size;
    float cx = gx * .5f, cy = gy * .5f, cz = gz * .5f;

    float az = cam_.azimuth * (float)std::numbers::pi / 180.f;
    float el = cam_.elevation * (float)std::numbers::pi / 180.f;
    float ex = cx + cam_.distance * std::cos(el) * std::cos(az);
    float ey = cy + cam_.distance * std::cos(el) * std::sin(az);
    float ez = cz + cam_.distance * std::sin(el);

    mat4 view = look_at(ex,ey,ez, cx,cy,cz, 0,0,1);
    mat4 proj = perspective(45.f * (float)std::numbers::pi/180.f,
                            (float)w/(float)h, 0.001f, 1000.f);
    mat4 mvp  = mul(proj, view);

    glUseProgram(line_prog_);
    GLint mvp_loc   = glGetUniformLocation(line_prog_, "uMVP");
    GLint color_loc = glGetUniformLocation(line_prog_, "uColor");
    glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, mvp.m);

    // Floor secondary grid
    if(show_floor_ && floor_vao_) {
        glUniform4f(color_loc, 0.053f, 0.125f, 0.212f, 1.f); // #0D2035
        glBindVertexArray(floor_vao_);
        glDrawArrays(GL_LINES, 0, floor_vcount_);
    }

    // Floor major grid (line-active at 30% opacity)
    if(show_floor_ && floor_major_vao_ && floor_major_vcount_ > 0) {
        glUniform4f(color_loc, 0.f, 0.784f, 1.f, 0.30f);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(floor_major_vao_);
        glDrawArrays(GL_LINES, 0, floor_major_vcount_);
        glDisable(GL_BLEND);
    }

    // Bounding box edges (line-dim, verticals slightly brighter at line-mid)
    if(box_vao_) {
        // First 8 edges (bottom + top) in line-dim
        glUniform4f(color_loc, 0.053f, 0.125f, 0.212f, 1.f); // line-dim
        glBindVertexArray(box_vao_);
        glDrawElements(GL_LINES, 16, GL_UNSIGNED_SHORT, nullptr);         // bottom+top
        // Last 4 edges (verticals) in line-mid
        glUniform4f(color_loc, 0.102f, 0.251f, 0.376f, 1.f); // line-mid
        glDrawElements(GL_LINES, 8, GL_UNSIGNED_SHORT, (void*)(16*sizeof(uint16_t))); // verticals
    }

    // World axis lines
    if(show_axes_ && axis_vao_) {
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // X — cyan 60%
        glUniform4f(color_loc, 0.f, 0.784f, 1.f, 0.6f);
        glBindVertexArray(axis_vao_);
        glDrawArrays(GL_LINES, 0, 2);
        // Y — amber 60%
        glUniform4f(color_loc, 1.f, 0.549f, 0.f, 0.6f);
        glDrawArrays(GL_LINES, 2, 2);
        // Z — white 60%
        glUniform4f(color_loc, 1.f, 1.f, 1.f, 0.6f);
        glDrawArrays(GL_LINES, 4, 2);
        glDisable(GL_BLEND);
    }

    // Pressure slice
    if(slice_vao_) {
        glUseProgram(quad_prog_);
        GLint q_mvp = glGetUniformLocation(quad_prog_, "uMVP");
        glUniformMatrix4fv(q_mvp, 1, GL_FALSE, mvp.m);
        glUniform1i(glGetUniformLocation(quad_prog_, "uTex"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, field_tex_);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(slice_vao_);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
        glDisable(GL_BLEND);
    }

    // Transducer diamonds
    if(tran_vao_ && tran_vcount_ > 0) {
        glUseProgram(line_prog_);
        glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, mvp.m);
        glUniform4f(color_loc, 0.f, 0.784f, 1.f, 1.f); // line-active cyan
        glBindVertexArray(tran_vao_);
        int ntrans = tran_vcount_ / 5;
        for(int i = 0; i < ntrans; ++i)
            glDrawArrays(GL_LINE_STRIP, i*5, 5);
    }

    // Object wireframes (spheres = 3 great circles, cubes = box edges)
    if(obj_vao_ && obj_vcount_ > 0) {
        glUseProgram(line_prog_);
        glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, mvp.m);
        glUniform4f(color_loc, 1.f, 0.549f, 0.f, 0.85f); // amber — object colour
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(obj_vao_);
        glDrawArrays(GL_LINES, 0, obj_vcount_);
        glDisable(GL_BLEND);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ── Main loop ─────────────────────────────────────────────────────────────────

void App::run() {
    auto last = std::chrono::steady_clock::now();

    while(!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        dt = std::min(dt, 0.05f); // clamp to 50ms

        anim_.time += dt;
        anim_.tl_ctrl += dt * 30.f;
        anim_.tl_viz  += dt * 30.f;

        if(anim_.sweeping) {
            anim_.sweep += dt / 0.3f;
            if(anim_.sweep >= 1.f) {
                anim_.sweep    = 1.f;
                anim_.sweeping = false;
            }
        }
        if(anim_.valid_flash > 0.f)
            anim_.valid_flash = std::max(0.f, anim_.valid_flash - dt * 3.f);

        glfwGetWindowSize(window_, &win_w_, &win_h_);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        handle_keyboard();
        render_frame(dt);

        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, win_w_, win_h_);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // DockSpace viewport update
        if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup);
        }

        glfwSwapBuffers(window_);
    }
}

// ── Render frame ─────────────────────────────────────────────────────────────

void App::render_frame(float /*dt*/) {
    constexpr float HEADER_H = 48.f;
    constexpr float STATUS_H = 24.f;
    constexpr float PANEL_W  = 290.f;

    float fw = (float)win_w_;
    float fh = (float)win_h_;

    // Full-window DockSpace backdrop (invisible window covering everything)
    ImGuiWindowFlags bg_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({fw, fh});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0});
    ImGui::Begin("##bg", nullptr, bg_flags);
    ImGuiID dock_id = ImGui::GetID("##dockspace");
    ImGui::DockSpace(dock_id, {0,0},
        ImGuiDockNodeFlags_PassthruCentralNode |
        ImGuiDockNodeFlags_NoDockingInCentralNode);
    ImGui::End();
    ImGui::PopStyleVar();

    panel_header(HEADER_H);
    panel_status(STATUS_H);
    panel_control();
    panel_visualization();
}

// ── Header bar ────────────────────────────────────────────────────────────────

void App::panel_header(float header_h) {
    float fw = (float)win_w_;
    ImGuiWindowFlags fl =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({fw, header_h});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::v4(theme::GRID_DARK));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##header", nullptr, fl);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    // Bottom border glow
    theme::glow::line(dl,
        {wp.x, wp.y + header_h - 1},
        {wp.x + fw, wp.y + header_h - 1},
        theme::LINE_ACTIVE, 1.f);

    // Vertical center
    float mid_y = header_h * 0.5f;
    ImGui::SetCursorPos({12.f, mid_y - ImGui::GetTextLineHeight()*0.5f});

    // Engine name
    ImGui::PushFont(font_value_);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_PRIMARY));
    ImGui::Text("ACOUSTIC HOLOGRAPHY ENGINE");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    // Scene name field (center)
    float name_w = 200.f;
    float name_x = fw * 0.35f;
    ImGui::SameLine(name_x);
    ImGui::SetNextItemWidth(name_w);

    char scene_buf[256];
    std::strncpy(scene_buf, scene_name_.c_str(), 255);
    scene_buf[255] = '\0';

    ImGui::PushStyleColor(ImGuiCol_FrameBg,  theme::v4(theme::VOID));
    ImGui::PushStyleColor(ImGuiCol_Border,   theme::v4(theme::LINE_DIM));
    ImGui::PushStyleColor(ImGuiCol_Text,     theme::v4(theme::TEXT_PRIMARY));
    if(ImGui::InputText("##scenename", scene_buf, 256))
        scene_name_ = scene_buf;
    ImGui::PopStyleColor(3);

    // Status indicator
    const auto status = model_.state.current;
    float status_x = fw - 300.f;
    ImGui::SameLine(status_x);

    if(status == simulate::sim_state::VALID) {
        float flash = anim_.valid_flash;
        ImU32 col = flash > 0.f
            ? theme::with_alpha(theme::STATUS_VALID, (uint8_t)(0x7F + flash * 0x80))
            : theme::STATUS_VALID;
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(col));
        ImGui::Text("● VALID");
        ImGui::PopStyleColor();
    } else if(status == simulate::sim_state::OLD) {
        // Pulse at 0.5 Hz
        float pulse = 0.5f + 0.5f * std::sin(anim_.time * (float)std::numbers::pi);
        ImU32 col = theme::with_alpha(theme::STATUS_STALE, (uint8_t)(180 + pulse * 75));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(col));
        ImGui::Text("● STALE");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
        ImGui::Text("○ UNCOMPUTED");
        ImGui::PopStyleColor();
    }

    // SOLVE button — fixed 28px height, centred in the 48px header
    float btn_w   = 110.f;
    constexpr float btn_h = 28.f;
    bool is_solving = anim_.sweeping;

    ImGui::SetCursorPos({fw - btn_w - 12.f, (header_h - btn_h) * 0.5f});
    ImGui::PushStyleColor(ImGuiCol_Button,        theme::v4(theme::VOID));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::v4(theme::GRID_MID));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  theme::v4(theme::LINE_ACTIVE));
    ImGui::PushStyleColor(ImGuiCol_Border,        theme::v4(theme::LINE_ACTIVE));
    ImGui::PushStyleColor(ImGuiCol_Text,          theme::v4(theme::LINE_BRIGHT));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);

    const char* btn_label = is_solving ? "SOLVING..." : "SOLVE";
    if(ImGui::Button(btn_label, {btn_w, btn_h}) && !is_solving) {
        run_forward_solver();
    }
    // Glow on the button rect
    ImVec2 btn_min = ImGui::GetItemRectMin();
    ImVec2 btn_max = ImGui::GetItemRectMax();
    theme::glow::rect(dl, btn_min, btn_max, theme::LINE_ACTIVE, 0.f);

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ── Status bar ────────────────────────────────────────────────────────────────

void App::panel_status(float status_h) {
    float fw = (float)win_w_;
    float fh = (float)win_h_;

    ImGuiWindowFlags fl =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::SetNextWindowPos({0, fh - status_h});
    ImGui::SetNextWindowSize({fw, status_h});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::v4(theme::GRID_DARK));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##status", nullptr, fl);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    // Top border glow
    theme::glow::line(dl,
        {wp.x, wp.y},
        {wp.x + fw, wp.y},
        theme::LINE_ACTIVE, 1.f);

    float row_y = status_h * 0.5f - ImGui::GetTextLineHeight() * 0.5f;
    ImGui::SetCursorPos({12.f, row_y});

    const auto& g = model_.world.grid;

    // Helper to write label+value pairs
    auto stat_item = [&](const char* label, const char* val) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
        ImGui::Text("%s", label);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 2);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_SECONDARY));
        ImGui::Text("%s", val);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 0);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::LINE_DIM));
        ImGui::Text("  │  ");
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 0);
    };

    char buf[64];

    snprintf(buf, sizeof(buf), "%u×%u×%u", g.grid.x, g.grid.y, g.grid.z);
    stat_item("GRID:", buf);

    snprintf(buf, sizeof(buf), "%.4fm", g.cell_size);
    stat_item("CELL:", buf);

    snprintf(buf, sizeof(buf), "%zu", g.objects.size());
    stat_item("OBJECTS:", buf);

    snprintf(buf, sizeof(buf), "%zu", model_.world.transducers.size());
    stat_item("ARRAYS:", buf);

    size_t total_t = 0;
    for(const auto& a : model_.world.transducers) total_t += a.tran_array.size();
    snprintf(buf, sizeof(buf), "%zu", total_t);
    stat_item("TRANS:", buf);

    // FIELD — semantic color
    const auto status = model_.state.current;
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
    ImGui::Text("FIELD:");
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 2);
    if(status == simulate::sim_state::VALID) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::STATUS_VALID));
        ImGui::Text("VALID");
    } else if(status == simulate::sim_state::OLD) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::STATUS_STALE));
        ImGui::Text("STALE");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
        ImGui::Text("UNCOMPUTED");
    }
    ImGui::PopStyleColor();

    if(last_solve_ms_ > 0.0) {
        ImGui::SameLine(0, 0);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::LINE_DIM));
        ImGui::Text("  │  ");
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 0);
        snprintf(buf, sizeof(buf), "%.2fms", last_solve_ms_);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
        ImGui::Text("LAST SOLVE:");
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 2);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_SECONDARY));
        ImGui::Text("%s", buf);
        ImGui::PopStyleColor();
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ── Control panel ─────────────────────────────────────────────────────────────

void App::panel_control() {
    constexpr float HEADER_H = 48.f;
    constexpr float STATUS_H = 24.f;
    constexpr float PANEL_W  = 290.f;
    float fh = (float)win_h_;
    float panel_h = fh - HEADER_H - STATUS_H;

    ImGuiWindowFlags fl =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav;

    ImGui::SetNextWindowPos({0, HEADER_H});
    ImGui::SetNextWindowSize({PANEL_W, panel_h});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::v4(theme::GRID_DARK));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##control", nullptr, fl);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();

    // Right border glow
    theme::glow::line(dl,
        {wp.x + PANEL_W, wp.y},
        {wp.x + PANEL_W, wp.y + panel_h},
        theme::LINE_ACTIVE, 1.f);

    // Traveling border light along right edge
    theme::traveling_light(dl,
        {wp.x, wp.y}, {wp.x + PANEL_W - 1, wp.y + panel_h - 1},
        anim_.tl_ctrl);

    ImGui::SetCursorPos({0, 8.f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.f, 0.f});

    section_scene();
    section_world();
    section_objects();
    section_transducers();
    section_solver();
    section_view();

    ImGui::PopStyleVar();
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ── Section header helper ─────────────────────────────────────────────────────

bool App::section_header(const char* label, bool& open) {
    constexpr float PANEL_W = 320.f;
    constexpr float ROW_H   = 28.f;

    ImDrawList* dl  = ImGui::GetWindowDrawList();
    ImVec2 pos      = ImGui::GetCursorScreenPos();

    // Background strip
    dl->AddRectFilled(pos, {pos.x + PANEL_W, pos.y + ROW_H},
                      theme::with_alpha(theme::GRID_MID, 200));

    // Triangle + label painted on draw list (colour control)
    const char* tri = open ? "▼" : "▶";
    float ty = pos.y + (ROW_H - ImGui::GetTextLineHeight()) * 0.5f;
    dl->AddText({pos.x + 10.f, ty}, theme::LINE_ACTIVE,    tri);
    dl->AddText({pos.x + 28.f, ty}, theme::TEXT_SECONDARY, label);

    // Decorative rule to right edge
    float rule_x0 = pos.x + 28.f + ImGui::CalcTextSize(label).x + 8.f;
    float rule_x1 = pos.x + PANEL_W - 8.f;
    if(rule_x1 > rule_x0)
        dl->AddLine({rule_x0, pos.y + ROW_H * 0.5f},
                    {rule_x1, pos.y + ROW_H * 0.5f},
                    theme::LINE_DIM, 1.f);
    if(open)
        dl->AddLine({pos.x, pos.y + ROW_H - 1.f},
                    {pos.x + PANEL_W, pos.y + ROW_H - 1.f},
                    theme::LINE_DIM, 1.f);

    // Transparent Selectable — registers the item with the layout (no more warning)
    ImGui::PushStyleColor(ImGuiCol_Header,        {0,0,0,0});
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme::v4(theme::with_alpha(theme::LINE_ACTIVE, 15)));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  theme::v4(theme::with_alpha(theme::LINE_ACTIVE, 30)));
    char id[64]; snprintf(id, sizeof(id), "##sec_%s", label);
    if(ImGui::Selectable(id, false, ImGuiSelectableFlags_AllowOverlap, {PANEL_W, ROW_H}))
        open = !open;
    ImGui::PopStyleColor(3);

    return open;
}

// ── SCENE section ─────────────────────────────────────────────────────────────

void App::section_scene() {
    if(!section_header("SCENE", sec_scene_open_)) return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.f, 6.f});
    ImGui::SetCursorPosX(12.f);
    ImGui::Dummy({0.f, 6.f});

    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
    ImGui::SetWindowFontScale(0.85f);
    ImGui::Text("NAME");
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();

    ImGui::SetNextItemWidth(266.f);
    ImGui::SetCursorPosX(12.f);
    char buf[256];
    std::strncpy(buf, scene_name_.c_str(), 255); buf[255]='\0';
    ImGui::PushStyleColor(ImGuiCol_FrameBg,    theme::v4(theme::GRID_MID));
    ImGui::PushStyleColor(ImGuiCol_Border,     theme::v4(theme::LINE_DIM));
    ImGui::PushStyleColor(ImGuiCol_Text,       theme::v4(theme::TEXT_PRIMARY));
    if(ImGui::InputText("##sname", buf, 256)) scene_name_ = buf;
    ImGui::PopStyleColor(3);

    ImGui::Dummy({0.f, 6.f});
    ImGui::PopStyleVar();
}

// ── WORLD section ─────────────────────────────────────────────────────────────

void App::section_world() {
    if(!section_header("WORLD", sec_world_open_)) return;

    auto& g = model_.world.grid;
    ImGui::SetCursorPosX(12.f);
    ImGui::Dummy({0.f, 6.f});

    // Dimensions sliders
    ImGui::PushStyleColor(ImGuiCol_Text,          theme::v4(theme::TEXT_GHOST));
    ImGui::SetWindowFontScale(0.85f);
    ImGui::Text("DIMENSIONS");
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();

    auto dim_slider = [&](const char* lbl, uint16_t& val, ImU32 col) {
        ImGui::SetCursorPosX(12.f);
        ImGui::PushStyleColor(ImGuiCol_Text,        theme::v4(col));
        ImGui::Text("%s", lbl);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        int iv = val;
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,       theme::v4(col));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, theme::v4(theme::LINE_BRIGHT));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,          theme::v4(theme::GRID_MID));
        ImGui::SetNextItemWidth(200.f);
        char id[8]; snprintf(id, sizeof(id), "##d%s", lbl);
        if(ImGui::SliderInt(id, &iv, 4, 256)) {
            val = (uint16_t)iv;
            dispatch_grid(space::grid::action::update_grid{});
        }
        ImGui::PopStyleColor(3);
    };
    dim_slider("X", g.grid.x, theme::AXIS_X);
    dim_slider("Y", g.grid.y, theme::AXIS_Y);
    dim_slider("Z", g.grid.z, theme::AXIS_Z);

    // Cell size
    ImGui::SetCursorPosX(12.f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
    ImGui::SetWindowFontScale(0.85f);
    ImGui::Text("CELL SIZE (m)");
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();

    ImGui::SetCursorPosX(12.f);
    float cs = (float)g.cell_size;
    ImGui::SetNextItemWidth(266.f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::v4(theme::GRID_MID));
    if(ImGui::InputFloat("##cs", &cs, 0.001f, 0.01f, "%.6f"))
        dispatch_grid(space::grid::action::cell_size{(double)cs});
    ImGui::PopStyleColor();

    // ── Default medium ────────────────────────────────────────────────────────
    ImGui::Dummy({0.f, 4.f});
    ImGui::SetCursorPosX(12.f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_SECONDARY));
    ImGui::Text("DEFAULT MEDIUM ─────────────────────");
    ImGui::PopStyleColor();

    auto def_med_float = [&](const char* lbl, double cur, auto mk_action) {
        ImGui::SetCursorPosX(12.f);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
        ImGui::SetWindowFontScale(0.85f);
        ImGui::Text("%s", lbl);
        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleColor();
        float fv = (float)cur;
        char id[32]; snprintf(id, sizeof(id), "##dmed_%s", lbl);
        ImGui::SetCursorPosX(12.f);
        ImGui::SetNextItemWidth(266.f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::v4(theme::GRID_MID));
        if(ImGui::InputFloat(id, &fv, 0.f, 0.f, "%.4g"))
            dispatch_grid(space::grid::action::default_medium_action{mk_action((double)fv)});
        ImGui::PopStyleColor();
    };

    const auto& dm = g.default_medium;
    def_med_float("DENSITY (kg/m³)",   dm.density,     [](double v) -> space::medium::actions { return space::medium::action::density{v}; });
    def_med_float("STIFFNESS (Pa)",    dm.stiffness,   [](double v) -> space::medium::actions { return space::medium::action::stiffness{v}; });
    def_med_float("ABSORPTION",        dm.absorption,  [](double v) -> space::medium::actions { return space::medium::action::absorption{v}; });
    def_med_float("TEMPERATURE (°C)",  dm.temperature, [](double v) -> space::medium::actions { return space::medium::action::temperature{v}; });

    // IS RIGID toggle
    ImGui::SetCursorPosX(12.f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
    ImGui::SetWindowFontScale(0.85f);
    ImGui::Text("IS RIGID");
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    bool dm_rigid = dm.is_rigid;
    ImGui::PushStyleColor(ImGuiCol_Button,
        dm_rigid ? theme::v4(theme::with_alpha(theme::STATUS_VALID, 50)) : theme::v4(theme::GRID_MID));
    ImGui::PushStyleColor(ImGuiCol_Text,
        dm_rigid ? theme::v4(theme::STATUS_VALID) : theme::v4(theme::TEXT_GHOST));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    char dm_rid[32]; snprintf(dm_rid, sizeof(dm_rid), "%s##dmed_rigid", dm_rigid ? "[ ON  ]" : "[ OFF ]");
    if(ImGui::Button(dm_rid, {72.f, 18.f}))
        dispatch_grid(space::grid::action::default_medium_action{space::medium::action::rigid{!dm_rigid}});
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImGui::Dummy({0.f, 6.f});
}

// ── OBJECTS section ───────────────────────────────────────────────────────────

void App::section_objects() {
    if(!section_header("OBJECTS", sec_obj_open_)) return;

    auto& g = model_.world.grid;
    ImGui::SetCursorPosX(12.f);
    ImGui::Dummy({0.f, 4.f});

    // Add buttons
    ImGui::PushStyleColor(ImGuiCol_Button,        theme::v4(theme::GRID_MID));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::v4(theme::with_alpha(theme::LINE_ACTIVE, 40)));
    ImGui::PushStyleColor(ImGuiCol_Text,          theme::v4(theme::TEXT_SECONDARY));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    if(ImGui::Button("+ SPHERE", {90,18}))
        dispatch_grid(space::grid::action::new_sphere{});
    ImGui::SameLine(0, 4);
    if(ImGui::Button("+ CUBE", {76,18}))
        dispatch_grid(space::grid::action::new_cube{});
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    ImGui::SetCursorPosX(12.f);
    ImGui::Dummy({0.f, 4.f});

    // Object list
    for(int oi = 0; oi < (int)g.objects.size(); ++oi) {
        const auto& obj = g.objects[oi];
        bool selected = (oi == sel_obj_);

        ImGui::SetCursorPosX(12.f);
        ImVec2 item_pos = ImGui::GetCursorScreenPos();
        ImDrawList* dl  = ImGui::GetWindowDrawList();

        // Border
        ImU32 border_col = selected ? theme::LINE_ACTIVE : theme::LINE_DIM;
        dl->AddRect({item_pos.x, item_pos.y},
                    {item_pos.x + 256.f, item_pos.y + 22.f},
                    border_col, 0.f, 0, 1.f);
        if(selected)
            theme::glow::rect(dl, {item_pos.x, item_pos.y},
                                  {item_pos.x+256.f, item_pos.y+22.f},
                                  theme::LINE_ACTIVE, 0.f);

        // Indicator
        ImGui::PushStyleColor(ImGuiCol_Text,
            theme::v4(selected ? theme::LINE_ACTIVE : theme::TEXT_GHOST));
        ImGui::Text("%s", selected ? "●" : "○");
        ImGui::PopStyleColor();
        ImGui::SameLine();

        // Name + type
        const char* shape_str = std::visit([](auto& s) -> const char* {
            using T = std::decay_t<decltype(s)>;
            if constexpr(std::is_same_v<T, space::object::shapes::sphere_model>) return "sphere";
            else return "cube";
        }, obj.shape);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_PRIMARY));
        ImGui::Text("%-16s  %-8s  prio:%-2d", obj.name.c_str(), shape_str, obj.object_prio);
        ImGui::PopStyleColor();

        // Click to select
        ImGui::SetCursorScreenPos(item_pos);
        ImGui::PushID(oi);
        if(ImGui::InvisibleButton("##objsel", {266.f, 22.f}))
            sel_obj_ = (sel_obj_ == oi) ? -1 : oi;
        ImGui::PopID();

        ImGui::Dummy({0.f, 2.f});
    }

    // Edit selected object
    if(sel_obj_ >= 0 && sel_obj_ < (int)g.objects.size()) {
        auto& obj = const_cast<space::object::object_model&>(g.objects[sel_obj_]);
        ImGui::SetCursorPosX(12.f);
        ImGui::Dummy({0.f, 6.f});
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_SECONDARY));
        ImGui::Text("— %s ─────────────", obj.name.c_str());
        ImGui::PopStyleColor();

        // Position
        auto edit_pt3 = [&](const char* label, space::utility::point3& p) {
            ImGui::SetCursorPosX(12.f);
            ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
            ImGui::SetWindowFontScale(0.85f);
            ImGui::Text("%s", label);
            ImGui::SetWindowFontScale(1.f);
            ImGui::PopStyleColor();
            float v[3] = {(float)p.x, (float)p.y, (float)p.z};
            ImGui::SetCursorPosX(12.f);
            ImGui::SetNextItemWidth(266.f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::v4(theme::GRID_MID));
            char id[16]; snprintf(id, sizeof(id), "##%s%d", label, sel_obj_);
            if(ImGui::InputFloat3(id, v)) {
                dispatch_grid(space::grid::action::object_action{
                    sel_obj_,
                    space::object::shapes::action::edit_position{{v[0],v[1],v[2]}}
                });
                dispatch_grid(space::grid::action::object_action{sel_obj_,
                    space::object::action::transform_mod{sdf_transform(
                        model_.world.grid.objects[sel_obj_].shape,
                        model_.world.grid.cell_size)}});
                dispatch_grid(space::grid::action::update_object_sdf{sel_obj_});
            }
            ImGui::PopStyleColor();
        };
        edit_pt3("POSITION", const_cast<space::utility::point3&>(
            std::visit([](auto& s) -> space::utility::point3& {
                return s.world_position;
            }, obj.shape)));

        // Shape-specific
        std::visit([&](auto& s) {
            using T = std::decay_t<decltype(s)>;
            if constexpr(std::is_same_v<T, space::object::shapes::sphere_model>) {
                ImGui::SetCursorPosX(12.f);
                ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
                ImGui::SetWindowFontScale(0.85f);
                ImGui::Text("SCALE (radius m)");
                ImGui::SetWindowFontScale(1.f);
                ImGui::PopStyleColor();
                float sc = (float)s.scale;
                ImGui::SetCursorPosX(12.f);
                ImGui::SetNextItemWidth(266.f);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::v4(theme::GRID_MID));
                if(ImGui::InputFloat("##sphsc", &sc, 0.001f, 0.01f)) {
                    dispatch_grid(space::grid::action::object_action{
                        sel_obj_, space::object::shapes::action::edit_sphere{(double)sc}});
                    dispatch_grid(space::grid::action::object_action{sel_obj_,
                        space::object::action::transform_mod{sdf_transform(
                            model_.world.grid.objects[sel_obj_].shape,
                            model_.world.grid.cell_size)}});
                    dispatch_grid(space::grid::action::update_object_sdf{sel_obj_});
                }
                ImGui::PopStyleColor();
            } else if constexpr(std::is_same_v<T, space::object::shapes::cube_model>) {
                float sv[3] = {(float)s.scale.i, (float)s.scale.j, (float)s.scale.k};
                ImGui::SetCursorPosX(12.f);
                ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
                ImGui::SetWindowFontScale(0.85f);
                ImGui::Text("SCALE");
                ImGui::SetWindowFontScale(1.f);
                ImGui::PopStyleColor();
                ImGui::SetCursorPosX(12.f);
                ImGui::SetNextItemWidth(266.f);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::v4(theme::GRID_MID));
                if(ImGui::InputFloat3("##cubsc", sv)) {
                    dispatch_grid(space::grid::action::object_action{
                        sel_obj_, space::object::shapes::action::edit_cube{
                            space::utility::vector3{sv[0],sv[1],sv[2]},
                            std::nullopt}});
                    dispatch_grid(space::grid::action::object_action{sel_obj_,
                        space::object::action::transform_mod{sdf_transform(
                            model_.world.grid.objects[sel_obj_].shape,
                            model_.world.grid.cell_size)}});
                    dispatch_grid(space::grid::action::update_object_sdf{sel_obj_});
                }
                ImGui::PopStyleColor();
            }
        }, obj.shape);

        // ── Medium edit ───────────────────────────────────────────────────
        ImGui::Dummy({0.f, 4.f});
        ImGui::SetCursorPosX(12.f);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_SECONDARY));
        ImGui::Text("MEDIUM ─────────────────────────────");
        ImGui::PopStyleColor();

        auto med_float = [&](const char* lbl, double cur, auto mk_action) {
            ImGui::SetCursorPosX(12.f);
            ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
            ImGui::SetWindowFontScale(0.85f);
            ImGui::Text("%s", lbl);
            ImGui::SetWindowFontScale(1.f);
            ImGui::PopStyleColor();
            float fv = (float)cur;
            char id[32]; snprintf(id, sizeof(id), "##med%s%d", lbl, sel_obj_);
            ImGui::SetCursorPosX(12.f);
            ImGui::SetNextItemWidth(266.f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::v4(theme::GRID_MID));
            if(ImGui::InputFloat(id, &fv, 0.f, 0.f, "%.4g")) {
                dispatch_grid(space::grid::action::object_action{
                    sel_obj_, mk_action((double)fv)});
            }
            ImGui::PopStyleColor();
        };

        med_float("DENSITY (kg/m³)", obj.medium.density,
            [](double v) -> space::object::actions {
                return space::medium::action::density{v}; });
        med_float("STIFFNESS (Pa)", obj.medium.stiffness,
            [](double v) -> space::object::actions {
                return space::medium::action::stiffness{v}; });
        med_float("ABSORPTION (1/m)", obj.medium.absorption,
            [](double v) -> space::object::actions {
                return space::medium::action::absorption{v}; });
        med_float("TEMPERATURE (°C)", obj.medium.temperature,
            [](double v) -> space::object::actions {
                return space::medium::action::temperature{v}; });

        // IS RIGID toggle
        ImGui::SetCursorPosX(12.f);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
        ImGui::SetWindowFontScale(0.85f);
        ImGui::Text("IS RIGID");
        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        {
            bool rigid = obj.medium.is_rigid;
            ImGui::PushStyleColor(ImGuiCol_Button,
                rigid ? theme::v4(theme::with_alpha(theme::STATUS_VALID,50))
                      : theme::v4(theme::GRID_MID));
            ImGui::PushStyleColor(ImGuiCol_Text,
                rigid ? theme::v4(theme::STATUS_VALID) : theme::v4(theme::TEXT_GHOST));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
            char rid[32]; snprintf(rid, sizeof(rid), "%s##rigid%d", rigid ? "[ ON  ]" : "[ OFF ]", sel_obj_);
            if(ImGui::Button(rid, {72.f, 18.f}))
                dispatch_grid(space::grid::action::object_action{
                    sel_obj_, space::medium::action::rigid{!rigid}});
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        }

        // ── Stamp / regen buttons ─────────────────────────────────────────
        ImGui::Dummy({0.f, 4.f});
        ImGui::SetCursorPosX(12.f);
        ImGui::PushStyleColor(ImGuiCol_Button,  theme::v4(theme::GRID_MID));
        ImGui::PushStyleColor(ImGuiCol_Text,    theme::v4(theme::TEXT_SECONDARY));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
        if(ImGui::Button("REGEN SDF", {84.f,18}))
            dispatch_grid(space::grid::action::update_object_sdf{sel_obj_});
        ImGui::SameLine(0,4);
        if(ImGui::Button("REGEN VOLUME", {94.f,18}))
            dispatch_grid(space::grid::action::update_object_volume{sel_obj_});
        ImGui::SameLine(0,4);
        ImGui::PushStyleColor(ImGuiCol_Button,        theme::v4(theme::VOID));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::v4(theme::with_alpha(theme::LINE_ACTIVE,30)));
        ImGui::PushStyleColor(ImGuiCol_Border,        theme::v4(theme::LINE_ACTIVE));
        ImGui::PushStyleColor(ImGuiCol_Text,          theme::v4(theme::LINE_BRIGHT));
        if(ImGui::Button("STAMP##stamp", {72.f,18}))
            dispatch_grid(space::grid::action::update_grid{});
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    // STAMP ALL (always visible at bottom of section)
    ImGui::Dummy({0.f, 4.f});
    ImGui::SetCursorPosX(12.f);
    ImGui::PushStyleColor(ImGuiCol_Button,        theme::v4(theme::VOID));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::v4(theme::with_alpha(theme::LINE_ACTIVE,30)));
    ImGui::PushStyleColor(ImGuiCol_Border,        theme::v4(theme::LINE_ACTIVE));
    ImGui::PushStyleColor(ImGuiCol_Text,          theme::v4(theme::LINE_BRIGHT));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    if(ImGui::Button("STAMP ALL INTO GRID", {266.f, 20.f}))
        dispatch_grid(space::grid::action::update_grid{});
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    ImGui::Dummy({0.f, 6.f});
}

// ── TRANSDUCERS section ───────────────────────────────────────────────────────

void App::section_transducers() {
    if(!section_header("TRANSDUCERS", sec_tran_open_)) return;

    ImGui::SetCursorPosX(12.f);
    ImGui::Dummy({0.f, 4.f});

    // Add buttons
    ImGui::PushStyleColor(ImGuiCol_Button,        theme::v4(theme::GRID_MID));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::v4(theme::with_alpha(theme::LINE_ACTIVE, 40)));
    ImGui::PushStyleColor(ImGuiCol_Text,          theme::v4(theme::TEXT_SECONDARY));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    if(ImGui::Button("+ ARRAY", {82,18}))
        dispatch_world(world::action::add_array{});
    ImGui::SameLine(0,4);
    if(ImGui::Button("+ SINGLE", {82,18})) {
        dispatch_world(world::action::add_array{});
        size_t last = model_.world.transducers.size()-1;
        dispatch_world(world::action::mod_array{
            last, transducer::tran_array::action::add_tran{}});
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    ImGui::SetCursorPosX(12.f);
    ImGui::Dummy({0.f, 4.f});

    static std::vector<uint8_t> arr_open; // per-array expand state (uint8 avoids vector<bool> proxy)
    arr_open.resize(model_.world.transducers.size(), 0);

    for(size_t ai = 0; ai < model_.world.transducers.size(); ++ai) {
        const auto& arr = model_.world.transducers[ai];
        bool ao = (arr_open[ai] != 0);

        std::string arr_name = arr.name.value_or("ARRAY_" + std::to_string(ai));

        // Array row
        ImGui::SetCursorPosX(12.f);
        ImVec2 row_pos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        bool arr_sel = (sel_array_ == (int)ai && sel_tran_ == -1);
        ImU32 arr_col = arr_sel ? theme::LINE_ACTIVE : theme::LINE_DIM;
        dl->AddRect({row_pos.x, row_pos.y}, {row_pos.x+256.f, row_pos.y+22.f}, arr_col);

        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::LINE_ACTIVE));
        ImGui::Text("%s", ao ? "▼" : "▶");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(arr_sel ? theme::TEXT_BRIGHT : theme::TEXT_PRIMARY));
        ImGui::Text("%-18s  %2zu trans", arr_name.c_str(), arr.tran_array.size());
        ImGui::PopStyleColor();

        ImGui::SetCursorScreenPos(row_pos);
        ImGui::PushID((int)ai * 1000);
        if(ImGui::InvisibleButton("##arrsel", {266.f, 22.f})) {
            if(sel_array_ == (int)ai && sel_tran_ == -1) arr_open[ai] = ao ? 0 : 1;
            else { sel_array_ = (int)ai; sel_tran_ = -1; arr_open[ai] = 1; ao = true; }
        }
        ImGui::PopID();
        ImGui::Dummy({0.f, 2.f});

        if(!ao) continue;

        // Individual transducers
        for(size_t ti = 0; ti < arr.tran_array.size(); ++ti) {
            const auto& t = arr.tran_array[ti];
            bool tsel = (sel_array_ == (int)ai && sel_tran_ == (int)ti);

            ImGui::SetCursorPosX(24.f);
            ImVec2 tp = ImGui::GetCursorScreenPos();
            ImU32 tc = tsel ? theme::LINE_ACTIVE : theme::LINE_DIM;
            dl->AddRect({tp.x, tp.y}, {tp.x+244.f, tp.y+20.f}, tc);
            if(tsel) theme::glow::rect(dl, {tp.x,tp.y},{tp.x+244.f,tp.y+20.f},theme::LINE_ACTIVE,0.f);

            std::string tname = t.name.value_or("T_" + std::to_string(ti));
            ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(tsel ? theme::LINE_ACTIVE : theme::TEXT_GHOST));
            ImGui::Text("%s", tsel ? "●" : "○");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_PRIMARY));
            ImGui::Text("%-10s φ:%.3f A:%.3f %s",
                tname.c_str(), t.phase, t.amplitude,
                t.is_active ? "✓" : "✗");
            ImGui::PopStyleColor();

            ImGui::SetCursorScreenPos(tp);
            ImGui::PushID((int)ai * 1000 + (int)ti + 1);
            if(ImGui::InvisibleButton("##tsel", {254.f, 20.f}))
                sel_array_ = (int)ai, sel_tran_ = (int)ti;
            ImGui::PopID();
            ImGui::Dummy({0.f, 2.f});
        }
    }

    // Edit selected transducer
    if(sel_array_ >= 0 && sel_array_ < (int)model_.world.transducers.size()
    && sel_tran_  >= 0 && sel_tran_  < (int)model_.world.transducers[sel_array_].tran_array.size()) {

        const auto& t = model_.world.transducers[sel_array_].tran_array[sel_tran_];

        auto dispatch_tran = [&](transducer::single::actions a) {
            dispatch_world(world::action::mod_array{
                (size_t)sel_array_,
                transducer::tran_array::action::single_adjust{sel_tran_, std::move(a)}});
        };

        ImGui::SetCursorPosX(12.f);
        ImGui::Dummy({0.f, 6.f});
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_SECONDARY));
        std::string tname = t.name.value_or("T_" + std::to_string(sel_tran_));
        ImGui::Text("— %s ─────────────", tname.c_str());
        ImGui::PopStyleColor();

        // Active toggle
        ImGui::SetCursorPosX(12.f);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
        ImGui::SetWindowFontScale(0.85f);
        ImGui::Text("ACTIVE");
        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
            t.is_active ? theme::v4(theme::with_alpha(theme::STATUS_VALID,50))
                        : theme::v4(theme::GRID_MID));
        ImGui::PushStyleColor(ImGuiCol_Text,
            t.is_active ? theme::v4(theme::STATUS_VALID) : theme::v4(theme::TEXT_GHOST));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
        char act_id[32]; snprintf(act_id, sizeof(act_id), "%s##act%d_%d",
            t.is_active ? "[ ON  ]" : "[ OFF ]", sel_array_, sel_tran_);
        if(ImGui::Button(act_id, {72, 18}))
            dispatch_tran(transducer::single::action::toggle_active{});
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        auto tran_float = [&](const char* lbl, double cur, double step,
                               auto new_act, auto mod_act) {
            ImGui::SetCursorPosX(12.f);
            ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
            ImGui::SetWindowFontScale(0.85f);
            ImGui::Text("%s", lbl);
            ImGui::SetWindowFontScale(1.f);
            ImGui::PopStyleColor();
            float v = (float)cur;
            ImGui::SetCursorPosX(12.f);
            ImGui::SetNextItemWidth(220.f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::v4(theme::GRID_MID));
            char id[32]; snprintf(id, sizeof(id), "##%s", lbl);
            if(ImGui::InputFloat(id, &v, (float)step, (float)step*10.f))
                dispatch_tran(new_act((double)v));
            ImGui::PopStyleColor();
            (void)mod_act;
        };

        tran_float("FREQUENCY (Hz)", t.frequency, 100.0,
            [](double v){ return transducer::single::action::new_frequency{v}; },
            [](double v){ return transducer::single::action::mod_frequency{v}; });
        tran_float("AMPLITUDE", t.amplitude, 0.01,
            [](double v){ return transducer::single::action::new_amplitude{v}; },
            [](double v){ return transducer::single::action::mod_amplitude{v}; });
        tran_float("PHASE (rad)", t.phase, 0.01,
            [](double v){ return transducer::single::action::new_phase{v}; },
            [](double v){ return transducer::single::action::mod_phase{v}; });

        // Position X/Y/Z
        ImGui::SetCursorPosX(12.f);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
        ImGui::SetWindowFontScale(0.85f);
        ImGui::Text("POSITION");
        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleColor();
        float pos[3] = {(float)t.position.x,(float)t.position.y,(float)t.position.z};
        ImGui::SetCursorPosX(12.f);
        ImGui::SetNextItemWidth(266.f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::v4(theme::GRID_MID));
        if(ImGui::InputFloat3("##tpos", pos))
            dispatch_tran(transducer::single::action::new_position{pos[0],pos[1],pos[2]});
        ImGui::PopStyleColor();
    }

    ImGui::Dummy({0.f, 6.f});
}

// ── SOLVER section ────────────────────────────────────────────────────────────

void App::section_solver() {
    if(!section_header("SOLVER", sec_solver_open_)) return;

    ImGui::SetCursorPosX(12.f);
    ImGui::Dummy({0.f, 6.f});

    // Forward frequency (read from first transducer if any)
    double freq = 40000.0;
    if(!model_.world.transducers.empty() && !model_.world.transducers[0].tran_array.empty())
        freq = model_.world.transducers[0].tran_array[0].frequency;

    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_SECONDARY));
    ImGui::Text("FORWARD ─────────────────────────────");
    ImGui::PopStyleColor();

    ImGui::SetCursorPosX(12.f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
    ImGui::SetWindowFontScale(0.85f);
    ImGui::Text("FREQUENCY (Hz)");
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();

    float fv = (float)freq;
    ImGui::SetCursorPosX(12.f);
    ImGui::SetNextItemWidth(266.f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::v4(theme::GRID_MID));
    if(ImGui::InputFloat("##solfreq", &fv, 100.f, 1000.f, "%.1f")) {
        for(size_t ai = 0; ai < model_.world.transducers.size(); ++ai)
            for(size_t ti = 0; ti < model_.world.transducers[ai].tran_array.size(); ++ti)
                dispatch_world(world::action::mod_array{
                    ai, transducer::tran_array::action::single_adjust{
                        (int)ti, transducer::single::action::new_frequency{(double)fv}}});
    }
    ImGui::PopStyleColor();

    // SOLVE button — full width
    ImGui::SetCursorPosX(12.f);
    ImGui::Dummy({0.f, 4.f});
    bool solving = anim_.sweeping;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::PushStyleColor(ImGuiCol_Button,        theme::v4(theme::VOID));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::v4(theme::GRID_MID));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  theme::v4(theme::with_alpha(theme::LINE_ACTIVE, 70)));
    ImGui::PushStyleColor(ImGuiCol_Border,        theme::v4(theme::LINE_ACTIVE));
    ImGui::PushStyleColor(ImGuiCol_Text,          theme::v4(theme::TEXT_BRIGHT));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    if(ImGui::Button(solving ? "SOLVING..." : "SOLVE", {266.f, 28.f}) && !solving)
        run_forward_solver();
    ImVec2 bmin = ImGui::GetItemRectMin();
    ImVec2 bmax = ImGui::GetItemRectMax();
    theme::glow::rect(dl, bmin, bmax, theme::LINE_ACTIVE, 0.f);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);

    ImGui::Dummy({0.f, 8.f});
    ImGui::SetCursorPosX(12.f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_SECONDARY));
    ImGui::Text("INVERSE ─────────────────────────────");
    ImGui::PopStyleColor();

    // Method selector
    ImGui::SetCursorPosX(12.f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
    ImGui::SetWindowFontScale(0.85f);
    ImGui::Text("METHOD");
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    for(int mi = 0; mi < 2; ++mi) {
        const char* labels[] = {"BACKPROP","GERCHBERG-SAXTON"};
        bool active = (inv_method_ == mi);
        ImGui::PushStyleColor(ImGuiCol_Button,
            active ? theme::v4(theme::with_alpha(theme::LINE_ACTIVE,70))
                   : theme::v4(theme::GRID_MID));
        ImGui::PushStyleColor(ImGuiCol_Border,
            active ? theme::v4(theme::LINE_ACTIVE) : theme::v4(theme::LINE_DIM));
        ImGui::PushStyleColor(ImGuiCol_Text,
            active ? theme::v4(theme::TEXT_BRIGHT) : theme::v4(theme::TEXT_SECONDARY));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
        ImGui::PushID(100+mi);
        if(ImGui::Button(labels[mi])) inv_method_ = mi;
        ImGui::PopID();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if(mi==0) ImGui::SameLine(0,4);
    }

    if(inv_method_ == 1) {
        ImGui::SetCursorPosX(12.f);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
        ImGui::SetWindowFontScale(0.85f);
        ImGui::Text("GS ITERATIONS");
        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleColor();
        int gi = inv_params_.gs_iterations;
        ImGui::SetCursorPosX(12.f);
        ImGui::SetNextItemWidth(266.f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::v4(theme::GRID_MID));
        if(ImGui::SliderInt("##gsi", &gi, 1, 500))
            inv_params_.gs_iterations = gi;
        ImGui::PopStyleColor();
    }

    // Target points
    ImGui::SetCursorPosX(12.f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
    ImGui::SetWindowFontScale(0.85f);
    ImGui::Text("TARGET POINTS");
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();

    for(int ti = 0; ti < (int)inv_targets_.size(); ++ti) {
        auto& tp = inv_targets_[ti];
        bool tsel = (sel_target_ == ti);
        ImGui::SetCursorPosX(12.f);
        ImVec2 tp_pos = ImGui::GetCursorScreenPos();
        ImDrawList* tdl = ImGui::GetWindowDrawList();
        tdl->AddRect({tp_pos.x, tp_pos.y}, {tp_pos.x+256.f, tp_pos.y+20.f},
                     tsel ? theme::LINE_ACTIVE : theme::LINE_DIM);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(tsel ? theme::TEXT_BRIGHT : theme::TEXT_PRIMARY));
        ImGui::Text("%s TARGET_%02d  (%.2f,%.2f,%.2f)",
            tsel ? "●" : "○", ti,
            tp.position.x, tp.position.y, tp.position.z);
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(tp_pos);
        ImGui::PushID(200+ti);
        if(ImGui::InvisibleButton("##tpsel", {266.f, 20.f}))
            sel_target_ = (sel_target_ == ti) ? -1 : ti;
        ImGui::PopID();
        ImGui::Dummy({0.f, 2.f});
    }

    // + / - TARGET
    ImGui::SetCursorPosX(12.f);
    ImGui::PushStyleColor(ImGuiCol_Button, theme::v4(theme::GRID_MID));
    ImGui::PushStyleColor(ImGuiCol_Text,   theme::v4(theme::TEXT_SECONDARY));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    if(ImGui::Button("+ TARGET", {110,18})) {
        inv_targets_.push_back({
            {0.05 + inv_targets_.size()*0.05, 0.05, 0.05},
            std::complex<double>(1.0, 0.0)
        });
    }
    ImGui::SameLine(0,4);
    if(ImGui::Button("- TARGET", {110,18}) && sel_target_ >= 0 && sel_target_ < (int)inv_targets_.size()) {
        inv_targets_.erase(inv_targets_.begin() + sel_target_);
        sel_target_ = -1;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    // RUN INVERSE
    ImGui::SetCursorPosX(12.f);
    ImGui::Dummy({0.f, 4.f});
    ImGui::PushStyleColor(ImGuiCol_Button,        theme::v4(theme::VOID));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::v4(theme::GRID_MID));
    ImGui::PushStyleColor(ImGuiCol_Border,        theme::v4(theme::STATUS_STALE));
    ImGui::PushStyleColor(ImGuiCol_Text,          theme::v4(theme::TEXT_BRIGHT));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    if(ImGui::Button("RUN INVERSE", {266.f, 28.f}))
        run_inverse_solver();
    ImVec2 ib_min = ImGui::GetItemRectMin();
    ImVec2 ib_max = ImGui::GetItemRectMax();
    theme::glow::rect(dl, ib_min, ib_max, theme::STATUS_STALE, 0.f);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    ImGui::Dummy({0.f, 6.f});
}

// ── VIEW section ──────────────────────────────────────────────────────────────

void App::section_view() {
    if(!section_header("VIEW", sec_view_open_)) return;

    const auto& g = model_.world.grid;
    ImGui::SetCursorPosX(12.f);
    ImGui::Dummy({0.f, 6.f});

    // Slice axis
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
    ImGui::SetWindowFontScale(0.85f);
    ImGui::Text("SLICE AXIS");
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    for(int ai = 0; ai < 3; ++ai) {
        const char* lbls[] = {"XY","XZ","YZ"};
        bool act = ((int)slice_axis_ == ai);
        ImGui::PushStyleColor(ImGuiCol_Button,
            act ? theme::v4(theme::with_alpha(theme::LINE_ACTIVE,70)) : theme::v4(theme::GRID_MID));
        ImGui::PushStyleColor(ImGuiCol_Border,
            act ? theme::v4(theme::LINE_ACTIVE) : theme::v4(theme::LINE_DIM));
        ImGui::PushStyleColor(ImGuiCol_Text,
            act ? theme::v4(theme::TEXT_BRIGHT) : theme::v4(theme::TEXT_SECONDARY));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
        ImGui::PushID(300+ai);
        if(ImGui::Button(lbls[ai], {44.f, 20.f})) {
            slice_axis_ = (simulate::field_query::slice_axis)ai;
            int max_idx = (ai==0) ? g.grid.z-1 : (ai==1) ? g.grid.y-1 : g.grid.x-1;
            slice_idx_ = std::clamp(slice_idx_, 0, max_idx);
            rebuild_slice_quad();
            if(model_.state.current != simulate::sim_state::UNCOMPUTED) field_dirty_ = true;
        }
        ImGui::PopID();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if(ai < 2) ImGui::SameLine(0,4);
    }

    // Slice index
    ImGui::SetCursorPosX(12.f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
    ImGui::SetWindowFontScale(0.85f);
    ImGui::Text("SLICE INDEX");
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();
    int max_idx = (slice_axis_==simulate::field_query::slice_axis::XY) ? g.grid.z-1
                : (slice_axis_==simulate::field_query::slice_axis::XZ) ? g.grid.y-1
                : g.grid.x-1;
    ImGui::SetCursorPosX(12.f);
    ImGui::SetNextItemWidth(266.f);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,  theme::v4(theme::LINE_ACTIVE));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,     theme::v4(theme::GRID_MID));
    if(ImGui::SliderInt("##si", &slice_idx_, 0, max_idx)) {
        rebuild_slice_quad();
        if(model_.state.current != simulate::sim_state::UNCOMPUTED) field_dirty_ = true;
    }
    ImGui::PopStyleColor(2);

    // Overlay mode
    ImGui::SetCursorPosX(12.f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
    ImGui::SetWindowFontScale(0.85f);
    ImGui::Text("OVERLAY");
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    for(int oi = 0; oi < 3; ++oi) {
        const char* lbls[] = {"MAGNITUDE","PHASE","INTENSITY"};
        bool act = (overlay_mode_ == oi);
        ImGui::PushStyleColor(ImGuiCol_Button,
            act ? theme::v4(theme::with_alpha(theme::LINE_ACTIVE,70)) : theme::v4(theme::GRID_MID));
        ImGui::PushStyleColor(ImGuiCol_Border,
            act ? theme::v4(theme::LINE_ACTIVE) : theme::v4(theme::LINE_DIM));
        ImGui::PushStyleColor(ImGuiCol_Text,
            act ? theme::v4(theme::TEXT_BRIGHT) : theme::v4(theme::TEXT_SECONDARY));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
        ImGui::PushID(400+oi);
        if(ImGui::Button(lbls[oi])) {
            overlay_mode_ = oi;
            if(model_.state.current != simulate::sim_state::UNCOMPUTED) field_dirty_ = true;
        }
        ImGui::PopID();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if(oi < 2) ImGui::SameLine(0,4);
    }

    // Toggles
    auto toggle_btn = [&](const char* lbl, bool& val) {
        ImGui::SetCursorPosX(12.f);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT_GHOST));
        ImGui::SetWindowFontScale(0.85f);
        ImGui::Text("%s", lbl);
        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
            val ? theme::v4(theme::with_alpha(theme::STATUS_VALID,50)) : theme::v4(theme::GRID_MID));
        ImGui::PushStyleColor(ImGuiCol_Text,
            val ? theme::v4(theme::STATUS_VALID) : theme::v4(theme::TEXT_GHOST));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
        char id[48]; snprintf(id, sizeof(id), "%s##tgl%s", val ? "[ ON  ]" : "[ OFF ]", lbl);
        if(ImGui::Button(id, {72.f, 18.f})) val = !val;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    };
    toggle_btn("FLOOR",      show_floor_);
    toggle_btn("AXIS LINES", show_axes_);

    ImGui::Dummy({0.f, 6.f});
}

// ── Visualization panel ───────────────────────────────────────────────────────

void App::panel_visualization() {
    constexpr float HEADER_H = 48.f;
    constexpr float STATUS_H = 24.f;
    constexpr float PANEL_W  = 290.f;
    float fw = (float)win_w_;
    float fh = (float)win_h_;
    float viz_x = PANEL_W;
    float viz_y = HEADER_H;
    float viz_w = fw - PANEL_W;
    float viz_h = fh - HEADER_H - STATUS_H;

    ImGuiWindowFlags fl =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav;

    ImGui::SetNextWindowPos({viz_x, viz_y});
    ImGui::SetNextWindowSize({viz_w, viz_h});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::v4(theme::VOID));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##viz", nullptr, fl);

    ImDrawList* dl  = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();

    // Render 3D scene into FBO
    int iw = (int)viz_w, ih = (int)viz_h;
    render_3d_scene(iw, ih);

    // Display FBO as background image
    if(fbo_color_)
        dl->AddImage((ImTextureID)(intptr_t)fbo_color_,
                     {wp.x, wp.y}, {wp.x + viz_w, wp.y + viz_h},
                     {0,1}, {1,0}); // flip Y (OpenGL → ImGui)

    // Handle mouse drag for orbit
    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool in_viz = mouse.x >= wp.x && mouse.x < wp.x+viz_w
               && mouse.y >= wp.y && mouse.y < wp.y+viz_h;

    if(in_viz && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        dragging_  = true;
        drag_last_ = mouse;
    }
    if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)) dragging_ = false;
    if(dragging_) {
        ImVec2 delta = {mouse.x - drag_last_.x, mouse.y - drag_last_.y};
        drag_last_  = mouse;
        cam_.azimuth   -= delta.x * 0.3f;
        cam_.elevation += delta.y * 0.3f;
        cam_.elevation  = std::clamp(cam_.elevation, -89.f, 89.f);
    }
    if(in_viz) {
        float wheel = ImGui::GetIO().MouseWheel;
        if(std::abs(wheel) > 1e-4f) {
            cam_.distance *= (1.f - wheel * 0.1f);
            cam_.distance  = std::clamp(cam_.distance, cam_.min_dist, cam_.max_dist);
        }
    }

    // Solve sweep animation
    if(anim_.sweeping || anim_.sweep > 0.f)
        draw_solve_sweep(dl, wp, {viz_w, viz_h});

    // Field status indicator (top-right)
    draw_field_status(dl, wp, {viz_w, viz_h});

    // Axis gizmo (bottom-left)
    draw_gizmo(dl, {wp.x + 16.f + 40.f, wp.y + viz_h - 16.f - 40.f});

    // Traveling light along viz panel edges
    theme::traveling_light(dl, {wp.x+1, wp.y+1}, {wp.x+viz_w-1, wp.y+viz_h-1}, anim_.tl_viz);

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ── Overlays ──────────────────────────────────────────────────────────────────

void App::draw_field_status(ImDrawList* dl, ImVec2 pos, ImVec2 sz) {
    const char* text = nullptr;
    ImU32 col;

    auto status = model_.state.current;
    if(status == simulate::sim_state::VALID) {
        float f = anim_.valid_flash;
        col  = f > 0.f
            ? theme::with_alpha(theme::STATUS_VALID, (uint8_t)(180 + f*75))
            : theme::STATUS_VALID;
        text = "● VALID";
    } else if(status == simulate::sim_state::OLD) {
        float pulse = 0.5f + 0.5f * std::sin(anim_.time * (float)std::numbers::pi);
        col  = theme::with_alpha(theme::STATUS_STALE, (uint8_t)(150 + pulse * 105));
        text = "● STALE";
    } else {
        col  = theme::TEXT_GHOST;
        text = "○ UNCOMPUTED";
    }

    if(!text) return;
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 tp = {pos.x + sz.x - ts.x - 12.f, pos.y + 12.f};
    // Small bg
    dl->AddRectFilled({tp.x-6, tp.y-3}, {tp.x+ts.x+6, tp.y+ts.y+3},
                      theme::with_alpha(theme::GRID_DARK, 180));
    dl->AddText(tp, col, text);
}

void App::draw_gizmo(ImDrawList* dl, ImVec2 center) {
    float len = 30.f;
    constexpr float PI = (float)std::numbers::pi;

    // Camera orientation vectors
    float az = cam_.azimuth   * PI / 180.f;
    float el = cam_.elevation * PI / 180.f;

    // Forward vector (cam → grid center direction)
    float fx = std::cos(el)*std::cos(az);
    float fy = std::cos(el)*std::sin(az);
    float fz = std::sin(el);

    // Right vector
    float rx = std::sin(az);
    float ry = -std::cos(az);
    float rz = 0.f;

    // Up vector = forward × right
    float ux = fy*rz - fz*ry;
    float uy = fz*rx - fx*rz;
    float uz = fx*ry - fy*rx;

    auto project = [&](float wx, float wy, float wz) -> ImVec2 {
        float px = wx*rx + wy*ry + wz*rz;
        float py = -(wx*ux + wy*uy + wz*uz);
        return {center.x + px*len, center.y + py*len};
    };

    ImVec2 o = {center.x, center.y};
    ImVec2 px_tip = project(1,0,0);
    ImVec2 py_tip = project(0,1,0);
    ImVec2 pz_tip = project(0,0,1);

    // Background circle
    dl->AddCircleFilled(center, 46.f, theme::with_alpha(theme::GRID_DARK, 180));
    dl->AddCircle(center, 46.f, theme::LINE_DIM, 32, 1.f);

    // X axis
    theme::glow::line(dl, o, px_tip, theme::AXIS_X, 1.5f);
    dl->AddText({px_tip.x+2, px_tip.y-5}, theme::AXIS_X, "X");

    // Y axis
    theme::glow::line(dl, o, py_tip, theme::AXIS_Y, 1.5f);
    dl->AddText({py_tip.x+2, py_tip.y-5}, theme::AXIS_Y, "Y");

    // Z axis
    theme::glow::line(dl, o, pz_tip, theme::AXIS_Z, 1.5f);
    dl->AddText({pz_tip.x+2, pz_tip.y-5}, theme::AXIS_Z, "Z");
}

void App::draw_solve_sweep(ImDrawList* dl, ImVec2 pos, ImVec2 sz) {
    if(anim_.sweep < 0.f) return;
    float y = pos.y + anim_.sweep * sz.y;
    // Dim flash first: briefly darken the panel
    if(anim_.sweep < 0.05f) {
        float alpha = (0.05f - anim_.sweep) / 0.05f * 0.6f;
        dl->AddRectFilled(pos, {pos.x+sz.x, pos.y+sz.y},
                          theme::with_alpha(theme::GRID_DARK, (uint8_t)(alpha*255)));
    }
    // Sweep line
    dl->AddLine({pos.x, y}, {pos.x+sz.x, y},
                theme::with_alpha(theme::LINE_ACTIVE, 200), 1.5f);
    dl->AddLine({pos.x, y}, {pos.x+sz.x, y},
                theme::with_alpha(theme::LINE_ACTIVE, 80), 4.f);
}

// ── Keyboard ──────────────────────────────────────────────────────────────────

void App::handle_keyboard() {
    if(ImGui::GetIO().WantCaptureKeyboard) return;

    if(ImGui::IsKeyPressed(ImGuiKey_F))
    { cam_.azimuth = 45.f; cam_.elevation = 30.f; }

    if(ImGui::IsKeyPressed(ImGuiKey_X))
    { cam_.azimuth = 0.f;  cam_.elevation =  0.f; }

    if(ImGui::IsKeyPressed(ImGuiKey_Y))
    { cam_.azimuth = 90.f; cam_.elevation =  0.f; }

    if(ImGui::IsKeyPressed(ImGuiKey_Z))
    { cam_.azimuth = 45.f; cam_.elevation = 89.f; }

    if(ImGui::IsKeyDown(ImGuiKey_LeftArrow))  cam_.azimuth   -= 5.f;
    if(ImGui::IsKeyDown(ImGuiKey_RightArrow)) cam_.azimuth   += 5.f;
    if(ImGui::IsKeyDown(ImGuiKey_UpArrow))    cam_.elevation += 5.f;
    if(ImGui::IsKeyDown(ImGuiKey_DownArrow))  cam_.elevation -= 5.f;
    cam_.elevation = std::clamp(cam_.elevation, -89.f, 89.f);

    if(ImGui::IsKeyDown(ImGuiKey_Equal)) // + key
        cam_.distance = std::max(cam_.min_dist, cam_.distance * 0.9f);
    if(ImGui::IsKeyDown(ImGuiKey_Minus))
        cam_.distance = std::min(cam_.max_dist, cam_.distance * 1.1f);
}

} // namespace gui::imgui_app
