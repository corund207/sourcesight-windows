#include "FullMapRenderer.hpp"
#include "core/engine/classes/MapRaytrace.hpp"
#include <GL/glew.h>

namespace {
GLuint program=0,vao=0,buffer=0;
std::shared_ptr<const std::vector<MapRaytrace::Triangle>> uploaded;

// This pass shares the context with ImGui. Do not leak depth, blending or
// clipping state into its renderer (including on an upload failure).
struct RenderState {
    GLint previous_program, previous_vao, previous_buffer, depth_func;
    GLint polygon_mode[2], blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha;
    GLint blend_equation_rgb, blend_equation_alpha;
    GLboolean depth_mask, color_mask[4];
    GLfloat line_width, offset_factor, offset_units;
    GLdouble clear_depth, depth_range[2];
    static constexpr GLenum caps[] = {GL_DEPTH_TEST, GL_BLEND, GL_SCISSOR_TEST,
        GL_CULL_FACE, GL_POLYGON_OFFSET_FILL, GL_LINE_SMOOTH, GL_STENCIL_TEST};
    GLboolean enabled[std::size(caps)];

    RenderState() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_buffer);
        glGetIntegerv(GL_DEPTH_FUNC, &depth_func);
        glGetIntegerv(GL_POLYGON_MODE, polygon_mode);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_alpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &blend_equation_rgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blend_equation_alpha);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
        glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
        glGetFloatv(GL_LINE_WIDTH, &line_width);
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &offset_factor);
        glGetFloatv(GL_POLYGON_OFFSET_UNITS, &offset_units);
        glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clear_depth);
        glGetDoublev(GL_DEPTH_RANGE, depth_range);
        for (size_t i = 0; i < std::size(caps); ++i) enabled[i] = glIsEnabled(caps[i]);
    }
    ~RenderState() {
        glUseProgram(previous_program);
        glBindVertexArray(previous_vao);
        glBindBuffer(GL_ARRAY_BUFFER, previous_buffer);
        glDepthFunc(depth_func);
        glDepthMask(depth_mask);
        glDepthRange(depth_range[0], depth_range[1]);
        glClearDepth(clear_depth);
        glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
        if (polygon_mode[0] == polygon_mode[1])
            glPolygonMode(GL_FRONT_AND_BACK, polygon_mode[0]);
        else {
            glPolygonMode(GL_FRONT, polygon_mode[0]);
            glPolygonMode(GL_BACK, polygon_mode[1]);
        }
        glPolygonOffset(offset_factor, offset_units);
        glLineWidth(line_width);
        glBlendFuncSeparate(blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha);
        glBlendEquationSeparate(blend_equation_rgb, blend_equation_alpha);
        for (size_t i = 0; i < std::size(caps); ++i) {
            if (enabled[i]) glEnable(caps[i]); else glDisable(caps[i]);
        }
    }
};
GLuint Shader(GLenum type,const char* source) {
    GLuint shader=glCreateShader(type);
    glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
    GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok) {glDeleteShader(shader);return 0;}
    return shader;
}
bool Init() {
    if(program) return true;
    const auto vertex=Shader(GL_VERTEX_SHADER,R"(#version 130
in vec3 position;
uniform mat4 view;
void main() {
    vec4 p=view*vec4(position,1.0);
    // Preserve the overlay's XY/FOV and row-3 W convention. A one-unit
    // near plane gives 100x more depth precision than the old 0.01 plane.
    gl_Position=vec4(p.xy,p.w-2.0,p.w);
})");
    const auto fragment=Shader(GL_FRAGMENT_SHADER,R"(#version 130
uniform vec4 tint;
out vec4 color;
void main() { color=tint; }
)");
    if(!vertex||!fragment) {if(vertex)glDeleteShader(vertex);if(fragment)glDeleteShader(fragment);return false;}
    program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);
    glBindAttribLocation(program,0,"position");glLinkProgram(program);
    glDeleteShader(vertex);glDeleteShader(fragment);
    GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){glDeleteProgram(program);program=0;return false;}
    glGenVertexArrays(1,&vao);glGenBuffers(1,&buffer);
    return true;
}
}

bool FullMapRenderer::Render(const view_matrix_t& matrix) {
    auto mesh=MapRaytrace::MeshSnapshot();
    if(!mesh) {Destroy();return false;}
    for(const auto& row:matrix.matrix) for(float value:row) if(!std::isfinite(value)) return false;
    if(mesh->empty() || mesh->size() > static_cast<size_t>(std::numeric_limits<GLsizei>::max()/3)) return false;
    GLint depth_bits=0;
    glGetIntegerv(GL_DEPTH_BITS,&depth_bits);
    if(depth_bits == 0) return false; // Never draw unoccluded fills without a depth attachment.
    const RenderState saved_state;
    if(!Init()) return false; // Leave CS2 visible on renderer failure.
    glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    if(mesh.get()!=uploaded.get()) {
        static_assert(sizeof(MapRaytrace::Triangle)==9*sizeof(float));
        glBufferData(GL_ARRAY_BUFFER,mesh->size()*sizeof(MapRaytrace::Triangle),mesh->data(),GL_STATIC_DRAW);
        if(glGetError()!=GL_NO_ERROR) {glBindVertexArray(0);Destroy();return false;}
        uploaded=mesh;
    }
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),nullptr);
    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program,"view"),1,GL_TRUE,&matrix.matrix[0][0]);
    const auto color=cfg::esp::wireframe_color;
    glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);glDisable(GL_LINE_SMOOTH);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);glDepthRange(0.0,1.0);glClearDepth(1.0);
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
    glClear(GL_DEPTH_BUFFER_BIT);
    const auto count=static_cast<GLsizei>(mesh->size()*3);
    // Establish the nearest surface BEFORE writing any color. Blending while
    // building depth let rear panels accumulate according to mesh draw order.
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(1.f,1.f);
    glDrawArrays(GL_TRIANGLES,0,count);

    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glDepthMask(GL_FALSE);glDepthFunc(GL_EQUAL);
    const float line_alpha=std::clamp(cfg::esp::wireframe_opacity,0.f,1.f);
    const float panel_alpha=std::clamp(cfg::esp::wireframe_panel_opacity,0.f,.35f);
    if(panel_alpha > 0.f) {
        // First color pass on the transparent overlay: write premultiplied
        // graphite directly, so even duplicate coplanar triangles stay at 10%.
        // The wire color is deliberately independent of the surface tint.
        glUniform4f(glGetUniformLocation(program,"tint"),
            (24.f/255.f)*panel_alpha,(26.f/255.f)*panel_alpha,(30.f/255.f)*panel_alpha,panel_alpha);
        glDrawArrays(GL_TRIANGLES,0,count);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthFunc(GL_LEQUAL);
    // X-ray affects edges only; panels always retain nearest-surface depth.
    if(cfg::esp::wireframe_full_xray) glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    glUniform4f(glGetUniformLocation(program,"tint"),color.r,color.g,color.b,line_alpha);
    glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);glLineWidth(1.f);
    if(line_alpha > 0.f) glDrawArrays(GL_TRIANGLES,0,count);
    return true;
}
void FullMapRenderer::Destroy() {
    if(buffer)glDeleteBuffers(1,&buffer);
    if(vao)glDeleteVertexArrays(1,&vao);
    if(program)glDeleteProgram(program);
    program=vao=buffer=0;uploaded.reset();
}
