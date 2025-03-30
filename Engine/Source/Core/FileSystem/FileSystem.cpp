#include "FileSystem.h"


FileSystem *FileSystem::instance = nullptr;

void FileSystem::init()
{
    instance = new FileSystem();
}
