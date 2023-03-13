#pragma once


#include <GL/glew.h>

namespace glinternal {

class VertexBuffer
{
  public:
    VertexBuffer(GLenum buffertype, size_t datasize, float *data, GLuint drawType);

    GLuint Id;
};

}; // namespace glinternal



namespace glinternal {

inline VertexBuffer::VertexBuffer(GLenum buffertype, size_t datasize, float *data, GLuint drawType)
{
    glGenBuffers(1, &Id);
    glBindBuffer(buffertype, Id);
    glBufferData(buffertype, datasize, data, drawType);
};

} // namespace glinternal
