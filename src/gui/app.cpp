#include "gui/app.hpp"
#include "gui/theme.hpp"

#include "space/object/object_descriptor.hpp"
#include "space/object/object.hpp"
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

namespace gui {

// ── mat4 — column-major (OpenGL convention: m[col*4 + row]) ──────────────────

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

// eye → center, up = world-up
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

// ── Pressure colormaps ────────────────────────────────────────────────────────

struct RGB8 { uint8_t r,g,b; };

// Perceptually uniform: deep indigo → electric cyan → white
static RGB8 colormap_mag(double t) {
    static const RGB8 stops[6] = {
        {0x0A,0x0A,0x2E},  // 0.0  deep indigo
        {0x0D,0x2B,0x6B},  // 0.2  dark blue
        {0x00,0x66,0xCC},  // 0.4  mid blue
        {0x00,0xAA,0xDD},  // 0.6  cyan-blue
        {0x00,0xEE,0xFF},  // 0.8  electric cyan
        {0xFF,0xFF,0xFF},  // 1.0  white
    };
    t = std::clamp(t, 0.0, 1.0);
    double seg = t * 5.0;
    int    i   = std::min(static_cast<int>(seg), 4);
    float  f   = static_cast<float>(seg - i);
    const auto& a = stops[i]; const auto& b = stops[i+1];
    return {uint8_t(a.r + f*(b.r-a.r)),
            uint8_t(a.g + f*(b.g-a.g)),
            uint8_t(a.b + f*(b.b-a.b))};
}

// Circular hue colormap for phase
static RGB8 colormap_phase(double phi) {
    phi = std::fmod(phi, 2.0*std::numbers::pi);
    if(phi < 0) phi += 2.0*std::numbers::pi;
    float h = static_cast<float>(phi / (2.0*std::numbers::pi)) * 360.f;
    float c=1.f, x=c*(1.f-std::abs(std::fmod(h/60.f,2.f)-1.f));
    float r,g,b;
    if     (h<60)  {r=c;g=x;b=0;}
    else if(h<120) {r=x;g=c;b=0;}
    else if(h<180) {r=0;g=c;b=x;}
    else if(h<240) {r=0;g=x;b=c;}
    else if(h<300) {r=x;g=0;b=c;}
    else           {r=c;g=0;b=x;}
    return {uint8_t(r*255),uint8_t(g*255),uint8_t(b*255)};
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

// ── Constructor ────────────────────────────────────────────────────────────────

App::App() {
    if(!glfwInit()) throw std::runtime_error("glfwInit failed");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(win_w_, win_h_, "HolographySim", nullptr, nullptr);
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

    theme::apply();

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    init_gl();

    // Default scene: 64×64×32 air grid, cell_size = 5mm
    auto& g = model_.world.grid;
    g.grid      = {64, 64, 32};
    g.cell_size = 0.005;
    g.default_medium.name              = "air";
    g.default_medium.density           = 1.21;
    g.default_medium.stiffness         = 142000.0;
    g.default_medium.sound_speed       = 343.0;
    g.default_medium.acoustic_impedance= 415.0;
    g.default_medium.is_rigid          = false;

    slice_idx_ = g.grid.z / 2;

    // Initial camera distance
    float max_dim = std::max({g.grid.x * (float)g.cell_size,
                               g.grid.y * (float)g.cell_size,
                               g.grid.z * (float)g.cell_size});
    cam_.distance = max_dim * 1.8f;
}

// ── Destructor ────────────────────────────────────────────────────────────────

App::~App() {
    if(field_tex_)  glDeleteTextures(1, &field_tex_);
    if(fbo_color_)  glDeleteTextures(1, &fbo_color_);
    if(fbo_depth_)  glDeleteRenderbuffers(1, &fbo_depth_);
    if(fbo_)        glDeleteFramebuffers(1, &fbo_);
    if(floor_vao_) { glDeleteVertexArrays(1,&floor_vao_); glDeleteBuffers(1,&floor_vbo_); }
    if(axis_vao_)  { glDeleteVertexArrays(1,&axis_vao_);  glDeleteBuffers(1,&axis_vbo_); }
    if(slice_vao_) { glDeleteVertexArrays(1,&slice_vao_); glDeleteBuffers(1,&slice_vbo_);
                     glDeleteBuffers(1,&slice_ebo_); }
    if(tran_vao_)  { glDeleteVertexArrays(1,&tran_vao_);  glDeleteBuffers(1,&tran_vbo_); }
    if(line_prog_)  glDeleteProgram(line_prog_);
    if(quad_prog_)  glDeleteProgram(quad_prog_);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window_);
    glfwTerminate();
}

// ── GL resource init ──────────────────────────────────────────────────────────

void App::init_gl() {
    line_prog_ = link_program(LINE_VERT, LINE_FRAG);
    quad_prog_ = link_program(QUAD_VERT, QUAD_FRAG);

    glGenTextures(1, &field_tex_);
    glBindTexture(GL_TEXTURE_2D, field_tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // 1×1 deep-indigo placeholder
    uint8_t px[4] = {0x0A, 0x0A, 0x2E, 0xFF};
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

    const auto& g = model_.world.grid;
    float cs  = (float)g.cell_size;
    float gx  = g.grid.x * cs;
    float gy  = g.grid.y * cs;
    float pad = 0.2f;
    float x0  = -gx*pad,  x1 = gx*(1.f+pad);
    float y0  = -gy*pad,  y1 = gy*(1.f+pad);

    std::vector<float> verts;
    verts.reserve(((g.grid.x/10+2) + (g.grid.y/10+2)) * 6);

    for(int i = 0; i <= (int)g.grid.x; i += 10) {
        float x = i * cs;
        verts.insert(verts.end(), {x, y0, 0.f,  x, y1, 0.f});
    }
    for(int j = 0; j <= (int)g.grid.y; j += 10) {
        float y = j * cs;
        verts.insert(verts.end(), {x0, y, 0.f,  x1, y, 0.f});
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

void App::rebuild_axis_mesh() {
    if(axis_vao_) { glDeleteVertexArrays(1,&axis_vao_); glDeleteBuffers(1,&axis_vbo_); axis_vao_=0; }

    const auto& g = model_.world.grid;
    float cs = (float)g.cell_size;
    float hx = g.grid.x * cs * .5f;
    float hy = g.grid.y * cs * .5f;
    float hz = g.grid.z * cs * .5f;

    // Each axis line passes through the grid center
    float verts[] = {
        0,     hy, hz,   hx*2, hy,   hz,   // X: left→right
        hx, 0,     hz,   hx,   hy*2, hz,   // Y: front→back
        hx, hy, 0,       hx,   hy,   hz*2  // Z: bottom→top
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
        glDeleteVertexArrays(1,&slice_vao_);
        glDeleteBuffers(1,&slice_vbo_);
        glDeleteBuffers(1,&slice_ebo_);
        slice_vao_ = 0;
    }

    const auto& g = model_.world.grid;
    float cs = (float)g.cell_size;
    float nx = g.grid.x * cs;
    float ny = g.grid.y * cs;
    float nz = g.grid.z * cs;
    float idx = (float)slice_idx_ * cs;

    // 5 floats per vertex: xyz uv
    float verts[20];
    switch(slice_axis_) {
        case simulate::field_query::slice_axis::XY:
            verts[0]=0;  verts[1]=0;  verts[2]=idx; verts[3]=0; verts[4]=0;
            verts[5]=nx; verts[6]=0;  verts[7]=idx; verts[8]=1; verts[9]=0;
            verts[10]=nx;verts[11]=ny;verts[12]=idx;verts[13]=1;verts[14]=1;
            verts[15]=0; verts[16]=ny;verts[17]=idx;verts[18]=0;verts[19]=1;
            break;
        case simulate::field_query::slice_axis::XZ:
            verts[0]=0;  verts[1]=idx;verts[2]=0;  verts[3]=0; verts[4]=0;
            verts[5]=nx; verts[6]=idx;verts[7]=0;  verts[8]=1; verts[9]=0;
            verts[10]=nx;verts[11]=idx;verts[12]=nz;verts[13]=1;verts[14]=1;
            verts[15]=0; verts[16]=idx;verts[17]=nz;verts[18]=0;verts[19]=1;
            break;
        case simulate::field_query::slice_axis::YZ:
            verts[0]=idx;verts[1]=0;  verts[2]=0;  verts[3]=0; verts[4]=0;
            verts[5]=idx;verts[6]=ny; verts[7]=0;  verts[8]=1; verts[9]=0;
            verts[10]=idx;verts[11]=ny;verts[12]=nz;verts[13]=1;verts[14]=1;
            verts[15]=idx;verts[16]=0; verts[17]=nz;verts[18]=0;verts[19]=1;
            break;
    }
    unsigned int idx_data[] = {0,1,2, 0,2,3};

    glGenVertexArrays(1, &slice_vao_);
    glGenBuffers(1, &slice_vbo_);
    glGenBuffers(1, &slice_ebo_);
    glBindVertexArray(slice_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, slice_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, slice_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx_data), idx_data, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)(3*sizeof(float)));
    glBindVertexArray(0);
}

void App::rebuild_tran_mesh() {
    if(tran_vao_) { glDeleteVertexArrays(1,&tran_vao_); glDeleteBuffers(1,&tran_vbo_); tran_vao_=0; }

    std::vector<float> verts;
    for(const auto& arr : model_.world.transducers) {
        for(const auto& t : arr.tran_array) {
            if(!t.is_active) continue;
            verts.push_back((float)t.position.x);
            verts.push_back((float)t.position.y);
            verts.push_back((float)t.position.z);
        }
    }
    tran_vcount_ = (int)(verts.size() / 3);
    if(tran_vcount_ == 0) return;

    glGenVertexArrays(1, &tran_vao_);
    glGenBuffers(1, &tran_vbo_);
    glBindVertexArray(tran_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, tran_vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), nullptr);
    glBindVertexArray(0);
}

void App::upload_field_slice() {
    if(model_.state.pressure.empty()) {
        uint8_t px[4] = {0x0A, 0x0A, 0x2E, 0xFF};
        glBindTexture(GL_TEXTURE_2D, field_tex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glBindTexture(GL_TEXTURE_2D, 0);
        rebuild_slice_quad();
        return;
    }

    const auto& g = model_.world.grid;
    auto s = simulate::field_query::extract_slice(
        model_.state, g.grid, slice_axis_, (uint16_t)slice_idx_);

    if(s.data.empty()) return;

    std::vector<double> values;
    if     (overlay_mode_ == 0) values = simulate::field_query::magnitude(s);
    else if(overlay_mode_ == 1) values = simulate::field_query::phase(s);
    else                        values = simulate::field_query::intensity(s);

    double vmax = 1e-30;
    if(overlay_mode_ != 1) {
        for(auto v : values) vmax = std::max(vmax, v);
    }

    std::vector<uint8_t> pixels(s.width * s.height * 4);
    for(int i = 0; i < (int)(s.width * s.height); ++i) {
        RGB8 c = (overlay_mode_ == 1)
            ? colormap_phase(values[i])
            : colormap_mag(values[i] / vmax);
        pixels[i*4+0] = c.r;
        pixels[i*4+1] = c.g;
        pixels[i*4+2] = c.b;
        pixels[i*4+3] = 255;
    }

    glBindTexture(GL_TEXTURE_2D, field_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s.width, s.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    rebuild_slice_quad();
}

// ── Dispatch ──────────────────────────────────────────────────────────────────

void App::dispatch(simulate::sim_base::actions a) {
    bool is_world = std::holds_alternative<world::actions>(a);
    model_ = simulate::sim_base::update(std::move(model_), std::move(a));
    if(is_world) grid_dirty_ = true;
    if(model_.state.current == simulate::sim_state::VALID) field_dirty_ = true;
}

void App::run_forward_solver() {
    auto t0 = std::chrono::steady_clock::now();
    dispatch_state(simulate::sim_state::action::run_solver{model_.world});
    auto t1 = std::chrono::steady_clock::now();
    last_solve_ms_ = std::chrono::duration<double,std::milli>(t1-t0).count();
    field_dirty_ = true;
}

// ── Main loop ─────────────────────────────────────────────────────────────────

void App::run() {
    while(!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        glfwGetFramebufferSize(window_, &win_w_, &win_h_);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        handle_keyboard();
        render_frame();

        ImGui::Render();
        glViewport(0, 0, win_w_, win_h_);
        glClearColor(0.031f, 0.047f, 0.063f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window_);
    }
}

// ── Render frame — fixed manual layout ────────────────────────────────────────

void App::render_frame() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float header_h  = 38.0f;
    const float status_h  = 22.0f;
    const float panel_w   = 280.0f;

    ImVec2 full_pos  = vp->WorkPos;
    ImVec2 full_size = vp->WorkSize;
    float  mid_y     = full_pos.y + header_h;
    float  mid_h     = full_size.y - header_h - status_h;

    render_header_bar({full_pos.x,                  full_pos.y},
                      {full_size.x,                 header_h});
    render_status_bar({full_pos.x, full_pos.y + full_size.y - status_h},
                      {full_size.x, status_h});

    const auto fixed_flags =
        ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize    | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    // ── Control panel (left) ─────────────────────────────────────────────────
    ImGui::SetNextWindowPos ({full_pos.x,           mid_y});
    ImGui::SetNextWindowSize({panel_w,               mid_h});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::bg_panel);
    ImGui::Begin("Controls", nullptr, fixed_flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    render_control_panel();
    ImGui::End();

    // ── Visualization (right) ─────────────────────────────────────────────────
    float viz_x = full_pos.x + panel_w;
    float viz_w = full_size.x - panel_w;
    ImGui::SetNextWindowPos ({viz_x,  mid_y});
    ImGui::SetNextWindowSize({viz_w,  mid_h});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::bg_void);
    ImGui::Begin("Visualization", nullptr,
                 fixed_flags | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    render_visualization();
    ImGui::End();
}

// ── Header bar ────────────────────────────────────────────────────────────────

void App::render_header_bar(ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.f, 6.f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::bg_void);
    const auto flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("##Header", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    // Engine name
    ImGui::PushStyleColor(ImGuiCol_Text, theme::accent_primary);
    ImGui::Text("HOLOGRAPHYSIM");
    ImGui::PopStyleColor();
    ImGui::SameLine();

    // Separator
    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_dim);
    ImGui::Text("|");
    ImGui::PopStyleColor();
    ImGui::SameLine();

    // Scene name
    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_secondary);
    ImGui::Text("%s", scene_name_.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_dim);
    ImGui::Text("|");
    ImGui::PopStyleColor();
    ImGui::SameLine();

    // Field status
    switch(model_.state.current) {
        case simulate::sim_state::VALID:
            ImGui::PushStyleColor(ImGuiCol_Text, theme::accent_valid);
            ImGui::Text("● VALID");
            break;
        case simulate::sim_state::OLD:
            ImGui::PushStyleColor(ImGuiCol_Text, theme::accent_warm);
            ImGui::Text("● STALE");
            break;
        default:
            ImGui::PushStyleColor(ImGuiCol_Text, theme::text_dim);
            ImGui::Text("○ UNCOMPUTED");
            break;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();

    // Solve button (right-aligned)
    float btn_w = 80.f;
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - btn_w - 12.f);
    ImGui::PushStyleColor(ImGuiCol_Button,        theme::hex(0x00D4FF, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::hex(0x00D4FF, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  theme::hex(0x00D4FF, 0.50f));
    ImGui::PushStyleColor(ImGuiCol_Text,          theme::accent_primary);
    if(ImGui::Button("SOLVE", {btn_w, 0})) run_forward_solver();
    ImGui::PopStyleColor(4);

    ImGui::End();
}

// ── Status bar ────────────────────────────────────────────────────────────────

void App::render_status_bar(ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.f, 3.f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::bg_void);
    const auto flags =
        ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize    | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("##Status", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_secondary);

    const auto& g = model_.world.grid;
    size_t total_tran = 0;
    for(const auto& arr : model_.world.transducers)
        total_tran += arr.tran_array.size();

    const char* field_str = "UNCOMPUTED";
    if(model_.state.current == simulate::sim_state::VALID)      field_str = "VALID";
    else if(model_.state.current == simulate::sim_state::OLD)   field_str = "STALE";

    ImGui::Text("Grid: %dx%dx%d  |  Cell: %.4fm  |  Objects: %d  |  Arrays: %d  |  "
                "Transducers: %zu  |  Field: %s",
                (int)g.grid.x, (int)g.grid.y, (int)g.grid.z,
                g.cell_size,
                (int)g.objects.size(),
                (int)model_.world.transducers.size(),
                total_tran,
                field_str);

    if(last_solve_ms_ > 0.0) {
        ImGui::SameLine();
        ImGui::Text("|  Last solve: %.2fms", last_solve_ms_);
    }

    ImGui::PopStyleColor();
    ImGui::End();
}

// ── Control panel ─────────────────────────────────────────────────────────────

void App::render_control_panel() {
    // All six sections — collapsed by default except Transducers and Solver

    auto collapsing = [](const char* label, bool open_default) -> bool {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::accent_primary);
        ImGui::SetNextItemOpen(open_default, ImGuiCond_FirstUseEver);
        bool open = ImGui::CollapsingHeader(label);
        ImGui::PopStyleColor();
        return open;
    };

    if(collapsing("SCENE", false))       section_scene();
    if(collapsing("WORLD", false))       section_world();
    if(collapsing("OBJECTS", false))     section_objects();
    if(collapsing("TRANSDUCERS", true))  section_transducers();
    if(collapsing("SOLVER", true))       section_solver();
    if(collapsing("VIEW", false))        section_view();
}

// ── Section: Scene ────────────────────────────────────────────────────────────

void App::section_scene() {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_secondary);
    ImGui::Text("Scene name");
    ImGui::PopStyleColor();
    ImGui::SameLine();

    char buf[128];
    std::strncpy(buf, scene_name_.c_str(), sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    ImGui::SetNextItemWidth(-1);
    if(ImGui::InputText("##scene_name", buf, sizeof(buf)))
        scene_name_ = buf;
}

// ── Section: World ────────────────────────────────────────────────────────────

void App::section_world() {
    const auto& g = model_.world.grid;
    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_secondary);

    // Grid dimensions
    ImGui::Text("Dimensions");
    ImGui::PopStyleColor();

    int nx = g.grid.x, ny = g.grid.y, nz = g.grid.z;
    bool changed = false;
    ImGui::SetNextItemWidth(60); if(ImGui::InputInt("##gx", &nx, 0)) changed=true; ImGui::SameLine();
    ImGui::SetNextItemWidth(60); if(ImGui::InputInt("##gy", &ny, 0)) changed=true; ImGui::SameLine();
    ImGui::SetNextItemWidth(60); if(ImGui::InputInt("##gz", &nz, 0)) changed=true;
    if(changed) {
        nx = std::max(1, nx); ny = std::max(1, ny); nz = std::max(1, nz);
        dispatch_grid(space::grid::action::grid_dimensions{
            (uint16_t)nx, (uint16_t)ny, (uint16_t)nz});
        slice_idx_ = std::min(slice_idx_, nz-1);
    }

    // Cell size
    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_secondary);
    ImGui::Text("Cell size (m)");
    ImGui::PopStyleColor();
    double cs = g.cell_size;
    ImGui::SetNextItemWidth(-1);
    if(ImGui::InputDouble("##cs", &cs, 0.0001, 0.001, "%.5f"))
        if(cs > 0) dispatch_grid(space::grid::action::cell_size{cs});

    // Default medium
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_secondary);
    ImGui::Text("Default medium");
    ImGui::PopStyleColor();
    const auto& med = g.default_medium;

    double density = med.density;
    ImGui::SetNextItemWidth(-1);
    if(ImGui::InputDouble("density##dm", &density, 0, 0, "%.3f"))
        dispatch_grid(space::grid::action::default_medium_action{space::medium::action::density{density}});

    double stiff = med.stiffness;
    ImGui::SetNextItemWidth(-1);
    if(ImGui::InputDouble("stiffness##dm", &stiff, 0, 0, "%.1f"))
        dispatch_grid(space::grid::action::default_medium_action{space::medium::action::stiffness{stiff}});

    double absorb = med.absorption;
    ImGui::SetNextItemWidth(-1);
    if(ImGui::InputDouble("absorption##dm", &absorb, 0, 0, "%.5f"))
        dispatch_grid(space::grid::action::default_medium_action{space::medium::action::absorption{absorb}});

    bool rigid = med.is_rigid;
    if(ImGui::Checkbox("rigid##dm", &rigid))
        dispatch_grid(space::grid::action::default_medium_action{space::medium::action::rigid{rigid}});

    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_dim);
    ImGui::Text("c=%.1f m/s  Z=%.0f Pa·s/m", med.sound_speed, med.acoustic_impedance);
    ImGui::PopStyleColor();
}

// ── Section: Objects ──────────────────────────────────────────────────────────

void App::section_objects() {
    const auto& objects = model_.world.grid.objects;

    // Add buttons
    ImGui::PushStyleColor(ImGuiCol_Text, theme::accent_primary);
    if(ImGui::SmallButton("+ Sphere"))
        dispatch_grid(space::grid::action::new_sphere{});
    ImGui::SameLine();
    if(ImGui::SmallButton("+ Cube"))
        dispatch_grid(space::grid::action::new_cube{});
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // Object list
    for(int i = 0; i < (int)objects.size(); ++i) {
        const auto& obj = objects[i];
        bool selected = (sel_obj_ == i);
        if(ImGui::Selectable(obj.name.c_str(), selected)) sel_obj_ = i;
    }

    if(sel_obj_ >= 0 && sel_obj_ < (int)objects.size()) {
        const auto& obj = objects[sel_obj_];
        ImGui::Separator();

        // Name
        char buf[128]; std::strncpy(buf, obj.name.c_str(), sizeof(buf)-1);
        ImGui::SetNextItemWidth(-1);
        if(ImGui::InputText("name##obj", buf, sizeof(buf)))
            dispatch_grid(space::grid::action::object_action{sel_obj_, space::object::action::name{buf}});

        // Priority
        int prio = obj.object_prio;
        ImGui::SetNextItemWidth(-1);
        if(ImGui::InputInt("priority##obj", &prio))
            dispatch_grid(space::grid::action::object_action{sel_obj_, space::object::action::priority{prio}});

        // Shape parameters
        if(std::holds_alternative<space::object::shapes::sphere_model>(obj.shape)) {
            const auto& sp = std::get<space::object::shapes::sphere_model>(obj.shape);
            ImGui::PushStyleColor(ImGuiCol_Text, theme::text_secondary);
            ImGui::Text("SPHERE");
            ImGui::PopStyleColor();

            double px=sp.world_position.x, py=sp.world_position.y, pz=sp.world_position.z;
            bool moved = false;
            ImGui::SetNextItemWidth(60); if(ImGui::InputDouble("##spx",&px,0,0,"%.4f")) moved=true; ImGui::SameLine();
            ImGui::SetNextItemWidth(60); if(ImGui::InputDouble("##spy",&py,0,0,"%.4f")) moved=true; ImGui::SameLine();
            ImGui::SetNextItemWidth(60); if(ImGui::InputDouble("##spz",&pz,0,0,"%.4f")) moved=true;
            if(moved)
                dispatch_grid(space::grid::action::object_action{sel_obj_,
                    space::object::shapes::action::edit_position{{px,py,pz}}});

            double scale = sp.scale;
            ImGui::SetNextItemWidth(-1);
            if(ImGui::InputDouble("radius##sp", &scale, 0, 0, "%.4f"))
                dispatch_grid(space::grid::action::object_action{sel_obj_,
                    space::object::shapes::action::edit_sphere{scale}});
        } else if(std::holds_alternative<space::object::shapes::cube_model>(obj.shape)) {
            const auto& cb = std::get<space::object::shapes::cube_model>(obj.shape);
            ImGui::PushStyleColor(ImGuiCol_Text, theme::text_secondary);
            ImGui::Text("CUBE");
            ImGui::PopStyleColor();

            double px=cb.world_position.x, py=cb.world_position.y, pz=cb.world_position.z;
            bool moved = false;
            ImGui::SetNextItemWidth(60); if(ImGui::InputDouble("##cbx",&px,0,0,"%.4f")) moved=true; ImGui::SameLine();
            ImGui::SetNextItemWidth(60); if(ImGui::InputDouble("##cby",&py,0,0,"%.4f")) moved=true; ImGui::SameLine();
            ImGui::SetNextItemWidth(60); if(ImGui::InputDouble("##cbz",&pz,0,0,"%.4f")) moved=true;
            if(moved)
                dispatch_grid(space::grid::action::object_action{sel_obj_,
                    space::object::shapes::action::edit_position{{px,py,pz}}});

            double sx=cb.scale.i, sy=cb.scale.j, sz=cb.scale.k;
            double rx=cb.rotation.i, ry=cb.rotation.j, rz=cb.rotation.k;
            bool scaled = false, rotated = false;
            ImGui::SetNextItemWidth(60); if(ImGui::InputDouble("##csx",&sx,0,0,"%.4f")) scaled=true; ImGui::SameLine();
            ImGui::SetNextItemWidth(60); if(ImGui::InputDouble("##csy",&sy,0,0,"%.4f")) scaled=true; ImGui::SameLine();
            ImGui::SetNextItemWidth(60); if(ImGui::InputDouble("##csz",&sz,0,0,"%.4f")) scaled=true;
            if(scaled)
                dispatch_grid(space::grid::action::object_action{sel_obj_,
                    space::object::shapes::action::edit_cube{space::utility::vector3{sx,sy,sz}, {}}});

            ImGui::SetNextItemWidth(60); if(ImGui::InputDouble("##crx",&rx,0,0,"%.3f")) rotated=true; ImGui::SameLine();
            ImGui::SetNextItemWidth(60); if(ImGui::InputDouble("##cry",&ry,0,0,"%.3f")) rotated=true; ImGui::SameLine();
            ImGui::SetNextItemWidth(60); if(ImGui::InputDouble("##crz",&rz,0,0,"%.3f")) rotated=true;
            if(rotated)
                dispatch_grid(space::grid::action::object_action{sel_obj_,
                    space::object::shapes::action::edit_cube{{}, space::utility::vector3{rx,ry,rz}}});
        }

        // Regenerate buttons
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::text_secondary);
        if(ImGui::SmallButton("Regen SDF"))
            dispatch_grid(space::grid::action::update_object_sdf{sel_obj_});
        ImGui::SameLine();
        if(ImGui::SmallButton("Regen Volume"))
            dispatch_grid(space::grid::action::update_object_volume{sel_obj_});
        ImGui::SameLine();
        if(ImGui::SmallButton("Stamp Grid"))
            dispatch_grid(space::grid::action::update_grid{});
        ImGui::PopStyleColor();
    }
}

// ── Section: Transducers ──────────────────────────────────────────────────────

void App::section_transducers() {
    const auto& arrays = model_.world.transducers;

    // Array-level controls
    ImGui::PushStyleColor(ImGuiCol_Text, theme::accent_primary);
    if(ImGui::SmallButton("+ Array"))
        dispatch_world(world::action::add_array{});
    if(sel_array_ >= 0 && (size_t)sel_array_ < arrays.size()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::accent_danger);
        if(ImGui::SmallButton("- Remove Array")) {
            dispatch_world(world::action::remove_array{(size_t)sel_array_});
            sel_array_ = -1; sel_tran_ = -1;
        }
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // Array list
    for(int ai = 0; ai < (int)arrays.size(); ++ai) {
        const auto& arr = arrays[ai];
        std::string arr_label = arr.name.value_or("Array " + std::to_string(ai));
        bool sel = (sel_array_ == ai);
        ImGui::PushID(ai);
        if(ImGui::Selectable(arr_label.c_str(), sel)) {
            sel_array_ = ai;
            sel_tran_  = -1;
        }
        ImGui::PopID();
    }

    // Per-array controls
    if(sel_array_ >= 0 && (size_t)sel_array_ < arrays.size()) {
        const auto& arr = arrays[sel_array_];
        ImGui::Separator();

        // Array name
        char nbuf[128];
        std::string cur_name = arr.name.value_or("");
        std::strncpy(nbuf, cur_name.c_str(), sizeof(nbuf)-1);
        ImGui::SetNextItemWidth(-1);
        if(ImGui::InputText("##arr_name", nbuf, sizeof(nbuf)))
            dispatch_world(world::action::mod_array{(size_t)sel_array_,
                transducer::tran_array::action::new_name{nbuf}});

        // Add/remove transducer
        ImGui::PushStyleColor(ImGuiCol_Text, theme::accent_primary);
        if(ImGui::SmallButton("+ Tran"))
            dispatch_world(world::action::mod_array{(size_t)sel_array_,
                transducer::tran_array::action::add_tran{}});
        if((int)arr.tran_array.size() > 0) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, theme::accent_danger);
            if(ImGui::SmallButton("- Tran")) {
                dispatch_world(world::action::mod_array{(size_t)sel_array_,
                    transducer::tran_array::action::remove_tran{}});
                sel_tran_ = -1;
            }
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // Transducer list
        for(int ti = 0; ti < (int)arr.tran_array.size(); ++ti) {
            const auto& t = arr.tran_array[ti];
            std::string t_label = t.name.value_or("T" + std::to_string(ti));
            bool tsel = (sel_tran_ == ti);
            ImGui::PushID(ti * 1000 + sel_array_);
            // Active dot indicator
            ImGui::PushStyleColor(ImGuiCol_Text,
                t.is_active ? theme::accent_valid : theme::text_dim);
            ImGui::Text(t.is_active ? "●" : "○");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if(ImGui::Selectable(t_label.c_str(), tsel)) sel_tran_ = ti;
            ImGui::PopID();
        }

        // Per-transducer editing
        if(sel_tran_ >= 0 && sel_tran_ < (int)arr.tran_array.size()) {
            const auto& t = arr.tran_array[sel_tran_];
            ImGui::Separator();

            // Name
            char tbuf[128];
            std::strncpy(tbuf, t.name.value_or("").c_str(), sizeof(tbuf)-1);
            ImGui::SetNextItemWidth(-1);
            if(ImGui::InputText("##t_name", tbuf, sizeof(tbuf)))
                dispatch_world(world::action::mod_array{(size_t)sel_array_,
                    transducer::tran_array::action::single_adjust{sel_tran_,
                        transducer::single::action::new_name{tbuf}}});

            // Active toggle
            bool active = t.is_active;
            if(ImGui::Checkbox("active", &active))
                dispatch_world(world::action::mod_array{(size_t)sel_array_,
                    transducer::tran_array::action::single_adjust{sel_tran_,
                        transducer::single::action::toggle_active{}}});

            // Position
            ImGui::PushStyleColor(ImGuiCol_Text, theme::text_secondary);
            ImGui::Text("position");
            ImGui::PopStyleColor();
            double px=t.position.x, py=t.position.y, pz=t.position.z;
            bool moved = false;
            ImGui::SetNextItemWidth(70); if(ImGui::InputDouble("##tx",&px,0,0,"%.4f")) moved=true; ImGui::SameLine();
            ImGui::SetNextItemWidth(70); if(ImGui::InputDouble("##ty",&py,0,0,"%.4f")) moved=true; ImGui::SameLine();
            ImGui::SetNextItemWidth(70); if(ImGui::InputDouble("##tz",&pz,0,0,"%.4f")) moved=true;
            if(moved)
                dispatch_world(world::action::mod_array{(size_t)sel_array_,
                    transducer::tran_array::action::single_adjust{sel_tran_,
                        transducer::single::action::new_position{px, py, pz}}});

            // Frequency
            double freq = t.frequency;
            ImGui::SetNextItemWidth(-1);
            if(ImGui::InputDouble("freq (Hz)", &freq, 0, 0, "%.1f"))
                dispatch_world(world::action::mod_array{(size_t)sel_array_,
                    transducer::tran_array::action::single_adjust{sel_tran_,
                        transducer::single::action::new_frequency{freq}}});

            // Amplitude
            double amp = t.amplitude;
            ImGui::SetNextItemWidth(-1);
            if(ImGui::InputDouble("amplitude", &amp, 0, 0, "%.4f"))
                dispatch_world(world::action::mod_array{(size_t)sel_array_,
                    transducer::tran_array::action::single_adjust{sel_tran_,
                        transducer::single::action::new_amplitude{amp}}});

            // Phase
            double phase = t.phase;
            ImGui::SetNextItemWidth(-1);
            if(ImGui::InputDouble("phase (rad)", &phase, 0, 0, "%.4f"))
                dispatch_world(world::action::mod_array{(size_t)sel_array_,
                    transducer::tran_array::action::single_adjust{sel_tran_,
                        transducer::single::action::new_phase{phase}}});
        }
    }
}

// ── Section: Solver ───────────────────────────────────────────────────────────

void App::section_solver() {
    // Forward solver
    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_secondary);
    ImGui::Text("FORWARD");
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Button,        theme::hex(0x00D4FF, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::hex(0x00D4FF, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  theme::hex(0x00D4FF, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_Text,          theme::accent_primary);
    if(ImGui::Button("SOLVE", {-1, 0})) run_forward_solver();
    ImGui::PopStyleColor(4);

    if(last_solve_ms_ > 0.0) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::text_dim);
        ImGui::Text("last: %.2f ms", last_solve_ms_);
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Inverse solver
    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_secondary);
    ImGui::Text("INVERSE");
    ImGui::PopStyleColor();

    // Method selector
    const char* methods[] = {"backpropagation", "Gerchberg-Saxton"};
    int method_idx = (inv_params_.solver_method == simulate::inverse_solver::method::gerchberg_saxton) ? 1 : 0;
    ImGui::SetNextItemWidth(-1);
    if(ImGui::Combo("method", &method_idx, methods, 2))
        inv_params_.solver_method = (method_idx == 1)
            ? simulate::inverse_solver::method::gerchberg_saxton
            : simulate::inverse_solver::method::backprop;

    if(inv_params_.solver_method == simulate::inverse_solver::method::gerchberg_saxton) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("GS iterations", &inv_params_.gs_iterations);
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_secondary);
    ImGui::Text("Target points");
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Text, theme::accent_primary);
    if(ImGui::SmallButton("+ Target")) {
        simulate::inverse_solver::target_point tp;
        tp.position        = {0.0, 0.0, 0.0};
        tp.desired_pressure = {1.0, 0.0};
        inv_targets_.push_back(tp);
    }
    ImGui::PopStyleColor();

    for(int i = 0; i < (int)inv_targets_.size(); ++i) {
        ImGui::PushID(i);
        auto& tp = inv_targets_[i];

        double px=tp.position.x, py=tp.position.y, pz=tp.position.z;
        bool moved = false;
        ImGui::SetNextItemWidth(55); if(ImGui::InputDouble("##ipx",&px,0,0,"%.4f")) moved=true; ImGui::SameLine();
        ImGui::SetNextItemWidth(55); if(ImGui::InputDouble("##ipy",&py,0,0,"%.4f")) moved=true; ImGui::SameLine();
        ImGui::SetNextItemWidth(55); if(ImGui::InputDouble("##ipz",&pz,0,0,"%.4f")) moved=true;
        if(moved) tp.position = {px, py, pz};

        double mag = std::abs(tp.desired_pressure);
        ImGui::SetNextItemWidth(-20);
        if(ImGui::InputDouble("##imag",&mag,0,0,"%.3f")) {
            double ph = std::arg(tp.desired_pressure);
            tp.desired_pressure = std::polar(mag, ph);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::accent_danger);
        if(ImGui::SmallButton("x")) {
            inv_targets_.erase(inv_targets_.begin() + i);
            ImGui::PopStyleColor();
            ImGui::PopID();
            break;
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    if(!inv_targets_.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button,        theme::hex(0xFF8C42, 0.12f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::hex(0xFF8C42, 0.25f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  theme::hex(0xFF8C42, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_Text,          theme::accent_warm);
        if(ImGui::Button("SOLVE INVERSE", {-1, 0})) {
            dispatch(simulate::sim_base::action::run_inverse{inv_targets_, inv_params_});
        }
        ImGui::PopStyleColor(4);
    }
}

// ── Section: View ─────────────────────────────────────────────────────────────

void App::section_view() {
    // Slice axis
    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_secondary);
    ImGui::Text("Slice axis");
    ImGui::PopStyleColor();

    const char* axes[] = {"XY", "XZ", "YZ"};
    int ax = (int)slice_axis_;
    ImGui::SetNextItemWidth(-1);
    if(ImGui::Combo("##axis", &ax, axes, 3)) {
        slice_axis_ = (simulate::field_query::slice_axis)ax;
        const auto& g = model_.world.grid;
        int max_idx = (ax == 0) ? g.grid.z : (ax == 1) ? g.grid.y : g.grid.x;
        slice_idx_ = std::clamp(slice_idx_, 0, max_idx-1);
        field_dirty_ = true;
    }

    // Slice index
    const auto& g = model_.world.grid;
    int max_idx = ((int)slice_axis_ == 0) ? g.grid.z
                : ((int)slice_axis_ == 1) ? g.grid.y
                :                           g.grid.x;
    ImGui::SetNextItemWidth(-1);
    if(ImGui::SliderInt("##slice", &slice_idx_, 0, max_idx-1))
        field_dirty_ = true;

    ImGui::Spacing();

    // Overlay mode
    ImGui::PushStyleColor(ImGuiCol_Text, theme::text_secondary);
    ImGui::Text("Overlay");
    ImGui::PopStyleColor();
    const char* modes[] = {"Magnitude", "Phase", "Intensity"};
    ImGui::SetNextItemWidth(-1);
    if(ImGui::Combo("##overlay", &overlay_mode_, modes, 3))
        field_dirty_ = true;

    ImGui::Spacing();

    // Toggles
    if(ImGui::Checkbox("Show floor",      &show_floor_)) {}
    if(ImGui::Checkbox("Show axis lines", &show_axes_))  {}
}

// ── Visualization ─────────────────────────────────────────────────────────────

void App::render_visualization() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = (int)avail.x, h = (int)avail.y;
    if(w <= 0 || h <= 0) return;

    // Geometry rebuild
    if(grid_dirty_) {
        rebuild_floor_mesh();
        rebuild_axis_mesh();
        rebuild_tran_mesh();
        grid_dirty_ = false;
    }

    // Field texture upload
    if(field_dirty_) {
        upload_field_slice();
        field_dirty_ = false;
    }

    rebuild_fbo(w, h);
    render_3d_scene(w, h);

    // Display the FBO — flip Y (OpenGL origin is bottom-left)
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(intptr_t)fbo_color_, avail, {0,1}, {1,0});

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Field status indicator — top right of the panel
    draw_field_status(dl, cursor, avail);

    // Axis gizmo — bottom left
    draw_gizmo(dl, {cursor.x + 60.f, cursor.y + h - 60.f});

    // Mouse interaction (only when hovered)
    if(ImGui::IsItemHovered()) handle_viz_input();
}

void App::render_3d_scene(int w, int h) {
    const auto& g = model_.world.grid;
    float cs = (float)g.cell_size;
    float nx = g.grid.x * cs;
    float ny = g.grid.y * cs;
    float nz = g.grid.z * cs;
    float cx = nx*.5f, cy = ny*.5f, cz = nz*.5f;

    // Eye position from orbit angles
    float az = cam_.azimuth   * (float)std::numbers::pi / 180.f;
    float el = cam_.elevation * (float)std::numbers::pi / 180.f;
    float eye_x = cx + cam_.distance * std::cos(el) * std::cos(az);
    float eye_y = cy + cam_.distance * std::cos(el) * std::sin(az);
    float eye_z = cz + cam_.distance * std::sin(el);

    mat4 view = look_at(eye_x, eye_y, eye_z, cx, cy, cz, 0, 0, 1);
    mat4 proj = perspective((float)std::numbers::pi/4.f, (float)w/h, 0.001f, 1000.f);
    mat4 mvp  = mul(proj, view);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w, h);

    glClearColor(0.031f, 0.047f, 0.063f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ── Line geometry (floor, axes) ──────────────────────────────────────────
    glUseProgram(line_prog_);
    GLint mvp_loc = glGetUniformLocation(line_prog_, "uMVP");
    GLint col_loc = glGetUniformLocation(line_prog_, "uColor");
    glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, mvp.m);

    if(show_floor_ && floor_vao_) {
        auto c = theme::border_subtle;
        glUniform4f(col_loc, c.x, c.y, c.z, 0.55f);
        glBindVertexArray(floor_vao_);
        glDrawArrays(GL_LINES, 0, floor_vcount_);
    }

    if(show_axes_ && axis_vao_) {
        glBindVertexArray(axis_vao_);
        auto xc = theme::axis_x; glUniform4f(col_loc, xc.x,xc.y,xc.z, 0.50f); glDrawArrays(GL_LINES, 0, 2);
        auto yc = theme::axis_y; glUniform4f(col_loc, yc.x,yc.y,yc.z, 0.50f); glDrawArrays(GL_LINES, 2, 2);
        auto zc = theme::axis_z; glUniform4f(col_loc, zc.x,zc.y,zc.z, 0.50f); glDrawArrays(GL_LINES, 4, 2);
    }

    // ── Pressure slice quad ───────────────────────────────────────────────────
    if(slice_vao_ && !model_.state.pressure.empty()) {
        glUseProgram(quad_prog_);
        glUniformMatrix4fv(glGetUniformLocation(quad_prog_,"uMVP"), 1, GL_FALSE, mvp.m);
        glUniform1i(glGetUniformLocation(quad_prog_,"uTex"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, field_tex_);
        glBindVertexArray(slice_vao_);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    }

    // ── Transducer point markers ──────────────────────────────────────────────
    if(tran_vao_ && tran_vcount_ > 0) {
        glUseProgram(line_prog_);
        glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, mvp.m);
        auto tc = theme::accent_primary;
        glUniform4f(col_loc, tc.x, tc.y, tc.z, 1.f);
        glPointSize(5.f);
        glBindVertexArray(tran_vao_);
        glDrawArrays(GL_POINTS, 0, tran_vcount_);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, win_w_, win_h_);
}

// ── Viz overlays ──────────────────────────────────────────────────────────────

void App::draw_field_status(ImDrawList* dl, ImVec2 panel_pos, ImVec2 panel_size) {
    const char* status_str;
    ImU32 status_color;
    switch(model_.state.current) {
        case simulate::sim_state::VALID:
            status_str = "● VALID";
            status_color = theme::status_valid32;
            break;
        case simulate::sim_state::OLD:
            status_str = "● STALE";
            status_color = theme::status_stale32;
            break;
        default:
            status_str = "○ UNCOMPUTED";
            status_color = theme::status_uncomputed32;
            break;
    }

    ImVec2 text_pos = {panel_pos.x + panel_size.x - 140.f, panel_pos.y + 10.f};
    dl->AddRectFilled({text_pos.x - 6, text_pos.y - 3},
                      {text_pos.x + 136, text_pos.y + 16},
                      IM_COL32(8, 12, 16, 180), 2.f);
    dl->AddText(text_pos, status_color, status_str);
}

void App::draw_gizmo(ImDrawList* dl, ImVec2 center) {
    // Compute view rotation for the gizmo (no translation)
    float az = cam_.azimuth   * (float)std::numbers::pi / 180.f;
    float el = cam_.elevation * (float)std::numbers::pi / 180.f;
    float eye_x = std::cos(el)*std::cos(az);
    float eye_y = std::cos(el)*std::sin(az);
    float eye_z = std::sin(el);
    mat4 view = look_at(eye_x, eye_y, eye_z, 0, 0, 0, 0, 0, 1);

    const float axes[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
    const ImU32 colors[3]  = {theme::axis_x32, theme::axis_y32, theme::axis_z32};
    const char* labels[3]  = {"X","Y","Z"};
    constexpr float scale  = 32.f;

    // Gizmo background circle
    dl->AddCircleFilled(center, scale + 8.f, IM_COL32(8, 12, 16, 160));

    for(int i = 0; i < 3; ++i) {
        // Project world axis to camera-space X,Y using view rotation
        float cx = view.m[0]*axes[i][0] + view.m[4]*axes[i][1] + view.m[8]*axes[i][2];
        float cy = view.m[1]*axes[i][0] + view.m[5]*axes[i][1] + view.m[9]*axes[i][2];
        ImVec2 end = {center.x + cx*scale, center.y - cy*scale};  // ImGui Y is down
        dl->AddLine(center, end, colors[i], 2.0f);
        dl->AddText({end.x + 2.f, end.y - 7.f}, colors[i], labels[i]);
    }
}

// ── Input handling ────────────────────────────────────────────────────────────

void App::handle_viz_input() {
    ImGuiIO& io = ImGui::GetIO();

    // Scroll = zoom
    if(io.MouseWheel != 0.f) {
        cam_.distance *= (1.f - io.MouseWheel * 0.08f);
        cam_.distance = std::clamp(cam_.distance, 0.001f, 1000.f);
    }

    // Left drag = orbit
    if(ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        cam_.azimuth   -= io.MouseDelta.x * 0.4f;
        cam_.elevation += io.MouseDelta.y * 0.4f;
        cam_.elevation  = std::clamp(cam_.elevation, -89.f, 89.f);
    }
}

void App::handle_keyboard() {
    // Camera snap (only when not typing)
    if(ImGui::GetIO().WantTextInput) return;

    if(ImGui::IsKeyPressed(ImGuiKey_F)) { cam_.azimuth=45.f; cam_.elevation=30.f; }
    if(ImGui::IsKeyPressed(ImGuiKey_X)) { cam_.azimuth= 0.f; cam_.elevation= 0.f; }
    if(ImGui::IsKeyPressed(ImGuiKey_Y)) { cam_.azimuth=90.f; cam_.elevation= 0.f; }
    if(ImGui::IsKeyPressed(ImGuiKey_Z)) { cam_.azimuth= 0.f; cam_.elevation=90.f; }

    if(ImGui::IsKeyDown(ImGuiKey_LeftArrow))  cam_.azimuth   -= 1.f;
    if(ImGui::IsKeyDown(ImGuiKey_RightArrow)) cam_.azimuth   += 1.f;
    if(ImGui::IsKeyDown(ImGuiKey_UpArrow))    cam_.elevation  = std::min(cam_.elevation + 1.f, 89.f);
    if(ImGui::IsKeyDown(ImGuiKey_DownArrow))  cam_.elevation  = std::max(cam_.elevation - 1.f,-89.f);
    if(ImGui::IsKeyDown(ImGuiKey_Equal))      cam_.distance  *= 0.98f;
    if(ImGui::IsKeyDown(ImGuiKey_Minus))      cam_.distance  *= 1.02f;
}

} // namespace gui
