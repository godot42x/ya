#ifndef GL_INTERNAL_VERTEX_H
#define GL_INTERNAL_VERTEX_H

namespace glinternal {

struct Vertex
{
    float x, y, z;

    Vertex(float x, float y, float z) : x(x), y(y), z(z) {}
    Vertex(Vertex &other) : x(other.x), y(other.y), z(other.z) {}
};

} // namespace glinternal

#endif
