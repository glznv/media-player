#include <render/gl_renderer.hpp>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace render {

namespace {

#define RENDER_GL_FUNCTIONS(X)                                              \
  X(GetIntegerv) X(Viewport) X(ClearColor) X(Clear) X(DrawArrays)           \
  X(PixelStorei) X(GenTextures) X(DeleteTextures) X(BindTexture)            \
  X(ActiveTexture) X(TexParameteri) X(TexImage2D) X(TexSubImage2D)          \
  X(CreateShader) X(ShaderSource) X(CompileShader) X(GetShaderiv)           \
  X(GetShaderInfoLog) X(DeleteShader) X(CreateProgram) X(AttachShader)      \
  X(LinkProgram) X(GetProgramiv) X(GetProgramInfoLog) X(DeleteProgram)      \
  X(UseProgram) X(GetUniformLocation) X(Uniform1i) X(GenVertexArrays)       \
  X(BindVertexArray) X(DeleteVertexArrays)

struct GL {
#define RENDER_DECLARE(name) decltype(&::gl##name) name = nullptr;
  RENDER_GL_FUNCTIONS(RENDER_DECLARE)
#undef RENDER_DECLARE

  explicit GL(GLProcLoader loader) {
#define RENDER_LOAD(name)                                          \
  name = reinterpret_cast<decltype(name)>(loader("gl" #name));     \
  if (name == nullptr) {                                           \
    throw std::runtime_error("failed to load gl" #name);          \
  }
    RENDER_GL_FUNCTIONS(RENDER_LOAD)
#undef RENDER_LOAD
  }
};

#undef RENDER_GL_FUNCTIONS

constexpr const char* kVertexShader = R"(#version 330 core
out vec2 v_uv;
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  v_uv = vec2(p.x, 1.0 - p.y);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(#version 330 core
in vec2 v_uv;
out vec4 o_color;
uniform sampler2D u_tex0;
uniform sampler2D u_tex1;
uniform sampler2D u_tex2;
void main() {
  float y = (texture(u_tex0, v_uv).r - 16.0 / 255.0) * (255.0 / 219.0);
  float u = (texture(u_tex1, v_uv).r - 128.0 / 255.0) * (255.0 / 224.0);
  float v = (texture(u_tex2, v_uv).r - 128.0 / 255.0) * (255.0 / 224.0);
  vec3 rgb = vec3(y + 1.5748 * v,
                  y - 0.1873 * u - 0.4681 * v,
                  y + 1.8556 * u);
  o_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
)";

}

struct GLRenderer::Impl {
  GL gl;
  GLuint m_vao = 0;
  GLuint m_program = 0;
  GLuint m_textures[3] = {};

  bool m_has_frame = false;
  int m_width = 0;
  int m_height = 0;

  explicit Impl(GLProcLoader loader) : gl(loader) {
    GLint major = 0;
    GLint minor = 0;
    gl.GetIntegerv(GL_MAJOR_VERSION, &major);
    gl.GetIntegerv(GL_MINOR_VERSION, &minor);
    if (major * 10 + minor < 33) {
      throw std::runtime_error("OpenGL 3.3 required");
    }

    m_program = link_program();

    gl.GenVertexArrays(1, &m_vao);
    gl.GenTextures(3, m_textures);
    for (GLuint tex : m_textures) {
      gl.BindTexture(GL_TEXTURE_2D, tex);
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
  }

  ~Impl() { release(); }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  void release() noexcept {
    if (m_program != 0) gl.DeleteProgram(m_program);
    if (m_textures[0] != 0) gl.DeleteTextures(3, m_textures);
    if (m_vao != 0) gl.DeleteVertexArrays(1, &m_vao);
  }

  GLuint compile_shader(GLenum type, const char* source) {
    const GLuint shader = gl.CreateShader(type);
    gl.ShaderSource(shader, 1, &source, nullptr);
    gl.CompileShader(shader);

    GLint ok = GL_FALSE;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE) {
      char log[1024] = {};
      gl.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
      gl.DeleteShader(shader);
      throw std::runtime_error(std::string("shader compilation failed: ") +
                               log);
    }
    return shader;
  }

  GLuint link_program() {
    const GLuint vs = compile_shader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = 0;
    try {
      fs = compile_shader(GL_FRAGMENT_SHADER, kFragmentShader);
    } catch (...) {
      gl.DeleteShader(vs);
      throw;
    }

    const GLuint program = gl.CreateProgram();
    gl.AttachShader(program, vs);
    gl.AttachShader(program, fs);
    gl.LinkProgram(program);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);

    GLint ok = GL_FALSE;
    gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
      char log[1024] = {};
      gl.GetProgramInfoLog(program, sizeof(log), nullptr, log);
      gl.DeleteProgram(program);
      throw std::runtime_error(std::string("program link failed: ") + log);
    }

    gl.UseProgram(program);
    for (int i = 0; i < 3; ++i) {
      const std::string name = "u_tex" + std::to_string(i);
      gl.Uniform1i(gl.GetUniformLocation(program, name.c_str()), i);
    }
    gl.UseProgram(0);
    return program;
  }

  void upload_plane(GLuint texture, bool realloc, int width, int height,
                    const std::uint8_t* data, int stride) {
    gl.BindTexture(GL_TEXTURE_2D, texture);
    if (realloc) {
      gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED,
                    GL_UNSIGNED_BYTE, nullptr);
    }
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, stride);
    gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED,
                     GL_UNSIGNED_BYTE, data);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  }

  void upload(const Frame& frame) {
    if (frame.width <= 0 || frame.height <= 0) {
      throw std::invalid_argument("invalid frame size");
    }
    for (int i = 0; i < 3; ++i) {
      if (frame.planes[i] == nullptr || frame.strides[i] <= 0) {
        throw std::invalid_argument("invalid frame plane");
      }
    }

    const bool realloc =
        !m_has_frame || frame.width != m_width || frame.height != m_height;

    for (int i = 0; i < 3; ++i) {
      const int w = i > 0 ? (frame.width + 1) / 2 : frame.width;
      const int h = i > 0 ? (frame.height + 1) / 2 : frame.height;
      upload_plane(m_textures[i], realloc, w, h, frame.planes[i],
                   frame.strides[i]);
    }

    m_has_frame = true;
    m_width = frame.width;
    m_height = frame.height;
  }

  void render(int viewport_width, int viewport_height) const noexcept {
    gl.Viewport(0, 0, viewport_width, viewport_height);
    gl.ClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    gl.Clear(GL_COLOR_BUFFER_BIT);

    if (!m_has_frame || viewport_width <= 0 || viewport_height <= 0) {
      return;
    }

    const double scale =
        std::min(static_cast<double>(viewport_width) / m_width,
                 static_cast<double>(viewport_height) / m_height);
    const int w = static_cast<int>(m_width * scale);
    const int h = static_cast<int>(m_height * scale);
    gl.Viewport((viewport_width - w) / 2, (viewport_height - h) / 2, w, h);

    gl.UseProgram(m_program);
    for (int i = 0; i < 3; ++i) {
      gl.ActiveTexture(GL_TEXTURE0 + i);
      gl.BindTexture(GL_TEXTURE_2D, m_textures[i]);
    }

    gl.BindVertexArray(m_vao);
    gl.DrawArrays(GL_TRIANGLES, 0, 3);
    gl.BindVertexArray(0);
    gl.ActiveTexture(GL_TEXTURE0);
  }
};

GLRenderer::GLRenderer(GLProcLoader loader)
    : m_impl(std::make_unique<Impl>(loader)) {}

GLRenderer::~GLRenderer() = default;
GLRenderer::GLRenderer(GLRenderer&&) noexcept = default;
GLRenderer& GLRenderer::operator=(GLRenderer&&) noexcept = default;

void GLRenderer::upload(const Frame& frame) { m_impl->upload(frame); }

void GLRenderer::render(int viewport_width, int viewport_height)
    const noexcept {
  m_impl->render(viewport_width, viewport_height);
}

}
