#include <SDL.h>
#include <glad/glad.h>

const GLchar *vertex_src = R"glsl(
#version 430 core 

layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
out vec4 Color;

uniform vec4 scale;

void main()
{
    Color = color;
    vec4 scaled = scale * vec4(position, 0.0, 1.0);
    vec4 translated = scaled - vec4(1.0, 1.0, 0.0, 0.0);
    vec4 flipped = vec4(1.0, -1.0, 1.0, 1.0) * translated;
    gl_Position = flipped;
}
)glsl";

const char *fragment_src = R"glsl(
#version 430 core

in vec4 Color;
out vec4 out_color;

void main()
{
    out_color = Color;
}
)glsl";

GLuint compile_shader(GLuint type, const GLchar *src) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, NULL);
  glCompileShader(shader);

  GLint compiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled == GL_FALSE)
    SDL_Log("shader compile failed\n");

  return shader;
}

GLuint make_shader_program(GLuint vertex_shader, GLuint fragment_shader) {
  GLuint shader_program = glCreateProgram();
  glAttachShader(shader_program, vertex_shader);
  glAttachShader(shader_program, fragment_shader);
  glLinkProgram(shader_program);
  return shader_program;
}

void setup_layout(GLuint w, GLuint h, int cells_x, int cells_y,
                  float *_cell_width, float *_cell_height, float *_pad_x, float *_pad_y,
                  float *vertex_buffer)
{
  float play_area = (float)(w > h ? h : w);
  float play_x = 0;
  float play_y = 0;
  float play_w = play_area;
  float play_h = play_area;

  float pad_x = 5.0f;
  float pad_y = 5.0f;
  float play_x1 = play_w - (pad_x * (cells_x - 1));
  float play_y1 = play_h - (pad_y * (cells_y - 1));
                                    
  float cell_width = play_x1 / (float)cells_x;
  float cell_height = play_y1 / (float)cells_y;

  *_cell_width = cell_width;
  *_cell_height = cell_height;
  *_pad_x = pad_x;
  *_pad_y = pad_y;

  for (int i = 0; i < cells_x; i++) {
    for (int j = 0; j < cells_y; j++) {
      float x = (cell_width + pad_x) * (float)i;
      float y = (cell_height + pad_y) * (float)j;
      float w = cell_width;
      float h = cell_height;

      // 2 dims * 6 vertices
      unsigned int vsi = (i * cells_y * 2 * 6) + (j * 2 * 6);
      // write vertices for cell's rectangle
      float vs[12] =
      {
       x    , y + h, // bl
       x + w, y + h, // br
       x + w, y    , // tr
       x    , y + h, // bl
       x + w, y    , // tr
       x    , y    , // tl
      };
      SDL_memcpy(&vertex_buffer[vsi], vs, sizeof(vs));
    }
  }
}

void setup_game_state(int cells_x, int cells_y, unsigned char **state,
                      unsigned int **connections)
{
  // game state
  int num_cells = cells_x * cells_y;
  *state = (unsigned char *)SDL_calloc(1, sizeof(unsigned char) * num_cells);
  unsigned int *csptr = (unsigned int *)SDL_calloc(1, sizeof(unsigned int) * num_cells * 4);
  *connections = csptr;

  // set up connections between cells
  for (int x = 0; x < cells_x; x++) {
    for (int y = 0; y < cells_y; y++) {
      int s = x * cells_y + y;
      int i = 0;
      unsigned int *cs = &csptr[s * 4];
      for (int dx = -1; dx < 2; dx++) {
        for (int dy = -1; dy < 2; dy++) {
          if (abs(dx) + abs(dy) != 1)
            continue;
          int nx = x + dx;
          int ny = y + dy;
          unsigned int ns = (nx * cells_y + ny);
          if (nx >= 0 && nx < cells_x && ny >= 0 && ny < cells_y)
            cs[i++] = ns << 1 | 1u;
        }
      }
    }
  }
}

void poke(int i, unsigned char *state, unsigned int *connections) {
  unsigned int *cs = &connections[i * 4];
  state[i] ^= 1;
  for (int n = 0; n < 4; n++)
    state[cs[n] >> 1] ^= 1 & cs[n];
}

int main(int argc, char *argv[]) {
  GLuint window_width = 600;
  GLuint window_height = 600;

  SDL_Init(SDL_INIT_VIDEO);
  
  SDL_Window *window = SDL_CreateWindow("lights out", SDL_WINDOWPOS_UNDEFINED,
                                        SDL_WINDOWPOS_UNDEFINED, window_width,
                                        window_height, SDL_WINDOW_OPENGL);
  Uint32 windowID = SDL_GetWindowID(window);
  SDL_SetWindowResizable(window, SDL_TRUE);

  SDL_GLContext context = SDL_GL_CreateContext(window);
  SDL_GL_SetSwapInterval(1); // vsync on

  if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
    SDL_Log("Failed to initialize the OpenGL context.");
    exit(1);
  }

  GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_src);
  GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_src);
  GLuint shader_program = make_shader_program(vertex_shader, fragment_shader);

  // shader parameters
  glUseProgram(shader_program);
  GLint uni_scale = glGetUniformLocation(shader_program, "scale");
  glBindFragDataLocation(shader_program, 0, "out_color");

  GLuint vao;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  GLuint vbo, cbo;
  glGenBuffers(1, &vbo);
  glGenBuffers(1, &cbo);

  // attribute buffer layouts
  int vertex_dims = 2;
  GLsizei vertex_size = sizeof(float) * vertex_dims;
  GLint pos_attrib = 0;
  GLint pos_index = 0;
  glBindVertexBuffer(pos_index, vbo, 0, vertex_size);
  glEnableVertexAttribArray(pos_attrib);
  glVertexAttribFormat(pos_attrib, vertex_dims, GL_FLOAT, GL_FALSE, 0);
  glVertexAttribBinding(pos_attrib, pos_index);

  GLsizei color_size = sizeof(GLubyte) * 4;
  GLint color_attrib = 1;
  GLint color_index = 1;
  glBindVertexBuffer(color_index, cbo, 0, color_size);
  glEnableVertexAttribArray(color_attrib);
  glVertexAttribFormat(color_attrib, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0);
  glVertexAttribBinding(color_attrib, color_index);
  glBindVertexArray(0);

  // general opengl init
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glViewport(0, 0, window_width, window_height);
  glClearColor(0.0, 0.0, 0.0, 1.0);

  int cells_x = 5;
  int cells_y = 5;
  int num_cells = cells_x * cells_y;
  int num_vertices = num_cells * 6;

  GLsizei vbo_size = (GLsizei)num_vertices * vertex_size;
  float* vertex_buffer = (float*)SDL_malloc(vbo_size);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, vbo_size, vertex_buffer, GL_DYNAMIC_DRAW);

  GLsizei cbo_size = (GLsizei)num_vertices * color_size;
  GLubyte *color_buffer = (GLubyte *)SDL_malloc(cbo_size);
  glBindBuffer(GL_ARRAY_BUFFER, cbo);
  glBufferData(GL_ARRAY_BUFFER, cbo_size, color_buffer, GL_DYNAMIC_DRAW);

  unsigned char *board_state;
  unsigned int *connections;
  setup_game_state(cells_x, cells_y, &board_state, &connections);
  float cell_width, cell_height, pad_x, pad_y;
  setup_layout(window_width, window_height, cells_x, cells_y, &cell_width, &cell_height, &pad_x, &pad_y, vertex_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, vbo_size, vertex_buffer);

  // main loop
  SDL_Event window_event;
  int quit = 0;
  int fullscreen = 0; // 0 = windowed, 1 = fullscreen
  int mouse_x = 0, mouse_y = 0, mouse_down = 0, mouse_pressed = 0;
  while (!quit) {
    glViewport(0, 0, window_width, window_height);
    // clear screen
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // update scene; just colors
    for (int i = 0; i < cells_x; i++) {
      for (int j = 0; j < cells_y; j++) {
        unsigned int si = i * cells_y + j;

        float x = (cell_width + pad_x) * (float)i;
        float y = (cell_height + pad_y) * (float)j;
        float w = cell_width;
        float h = cell_height;

        unsigned char hovered = 0;
        if (mouse_x >= x && mouse_x <= x + w &&
            mouse_y >= y && mouse_y <= y + h)
          {
            hovered = 1;
            if (mouse_pressed) {
              mouse_pressed = 0;
              poke(si, board_state, connections);
            }
          }

        GLubyte color[4];
        color[0] = hovered ? 155 : 0;
        color[1] = board_state[si] ? 255 : 0;
        color[2] = hovered ? 0 : 155;
        color[3] = 255;

        // replicate color for each vertex
        for (int v = 0; v < 6; v++) {
          SDL_memcpy(&color_buffer[(si * 6 * 4) + (v * 4)], &color, 4);
        }
      }
    }
    // transfer color data
    glBindBuffer(GL_ARRAY_BUFFER, cbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, cbo_size, color_buffer);

    // recalculate scale in case of window resize
    float scale[4] = { 2.0f / (float)window_width, 2.0f / (float)window_height, 1.0f, 1.0f };

    // draw
    glUseProgram(shader_program);
    glBindVertexArray(vao);
    glUniform4fv(uni_scale, 1, scale);
    glDrawArrays(GL_TRIANGLES, 0, num_vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // show what was drawn
    SDL_GL_SwapWindow(window);

    SDL_PumpEvents();
    SDL_GetMouseState(&mouse_x, &mouse_y);

    while (SDL_PollEvent(&window_event)) {
      switch (window_event.type) {
      case SDL_QUIT:
        quit = 1;
        break;
      case SDL_MOUSEBUTTONDOWN:
        mouse_down = 1;
        break;
      case SDL_MOUSEBUTTONUP:
        mouse_pressed = 1;
        mouse_down = 0;
        break;
      case SDL_KEYDOWN:
        if (window_event.key.keysym.sym == SDLK_f) {
          if (fullscreen) {
            SDL_SetWindowFullscreen(window, 0);
            fullscreen = 0;
          } else {
            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
            fullscreen = 1;
          }
        }
        break;
      case SDL_WINDOWEVENT:
        if (window_event.window.windowID == windowID) {
            switch (window_event.window.event) {
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                window_width = window_event.window.data1;
                window_height = window_event.window.data2;
                
                setup_layout(window_width, window_height, cells_x, cells_y, &cell_width, &cell_height, &pad_x, &pad_y, vertex_buffer);
                glBindBuffer(GL_ARRAY_BUFFER, vbo);
                glBufferSubData(GL_ARRAY_BUFFER, 0, vbo_size, vertex_buffer);
                break;

            case SDL_WINDOWEVENT_CLOSE:
                window_event.type = SDL_QUIT;
                SDL_PushEvent(&window_event);
                break;
            }
        }
      default:
        // SDL_Log("Unhandled Input %d", window_event.type);
        break;
      }
    }
  }

  // cleanup
  glDeleteBuffers(1, &vbo);
  glDeleteBuffers(1, &cbo);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
  glDeleteProgram(shader_program);

  SDL_GL_DeleteContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
