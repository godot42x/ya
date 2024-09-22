#include <Gloria/glinternal/VertexBuffer.h>

glinternal::VertexBuffer::VertexBuffer(GLenum buffertype, size_t datasize, float *data, GLuint drawType)
{
    glGenBuffers(1, &ID);
    glBindBuffer(buffertype, ID);
    glBufferData(buffertype, datasize, data, drawType);
};
