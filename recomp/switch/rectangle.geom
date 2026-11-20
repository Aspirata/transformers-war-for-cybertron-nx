#version 460

layout(triangles) in;
layout(triangle_strip, max_vertices = 4) out;

// The WFC vertex pack reserves locations 0-15 for TEXCOORD and 16-21
// for COLOR. Keep the complete interface through the geometry stage.
layout(location = 0) in vec4 vertex_varyings[][22];
layout(location = 0) out vec4 raster_varyings[22];

void emit_corner(int vertex) {
  gl_Position = gl_in[vertex].gl_Position;
  for (int i = 0; i < 22; ++i) {
    raster_varyings[i] = vertex_varyings[vertex][i];
  }
  EmitVertex();
}

float edge_length_squared(int a, int b) {
  vec2 delta = gl_in[a].gl_Position.xy - gl_in[b].gl_Position.xy;
  return dot(delta, delta);
}

void main() {
  // The Xenos supplies three corners. Rotate them so the longest edge
  // is the shared diagonal of the two triangles in the output strip.
  float edge12 = edge_length_squared(1, 2);
  float edge20 = edge_length_squared(2, 0);
  float edge01 = edge_length_squared(0, 1);
  int corner = 0;
  if (edge20 > edge12 && edge20 >= edge01) {
    corner = 1;
  } else if (edge01 > edge12 && edge01 > edge20) {
    corner = 2;
  }
  int a = (corner + 1) % 3;
  int b = (corner + 2) % 3;

  emit_corner(corner);
  emit_corner(a);
  emit_corner(b);
  gl_Position = gl_in[a].gl_Position + gl_in[b].gl_Position - gl_in[corner].gl_Position;
  for (int i = 0; i < 22; ++i) {
    raster_varyings[i] = vertex_varyings[a][i] + vertex_varyings[b][i] - vertex_varyings[corner][i];
  }
  EmitVertex();
  EndPrimitive();
}
