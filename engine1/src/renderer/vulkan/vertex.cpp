extern uint32_t VkFormat2Size(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R32_SFLOAT:
        return 4;
    case VK_FORMAT_R32G32_SFLOAT:
        return 8;
    case VK_FORMAT_R32G32B32_SFLOAT:
        return 12;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16;
    default:
        break;
    }

    NE_ASSERT(false, "Unsupported format: {}", int(format));
    return 0;
}
