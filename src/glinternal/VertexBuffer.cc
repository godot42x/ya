#include <VertexBuffer.h>

glinternal::VertexBuffer::VertexBuffer(GLenum buffertype, size_t datasize, float *data, GLuint drawType)
{
    glGenBuffers(1, &Id);
    glBindBuffer(buffertype, Id);
    glBufferData(buffertype, datasize, data, drawType);
};
