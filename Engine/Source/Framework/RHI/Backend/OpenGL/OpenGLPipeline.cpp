// #include "OpenGLPipeline.h"
// #include "Core/Log.h"
// #include "OpenGLRender.h"
// #include "OpenGLRenderPass.h"

// #include <spirv_cross/spirv_cross.hpp>
// #include <spirv_cross/spirv_glsl.hpp>

// namespace ya
// {

// void OpenGLPipelineLayout::create(const std::vector<PushConstantRange>                     &pushConstants,
//                                    const std::vector<std::shared_ptr<IDescriptorSetLayout>> &layouts)
// {
//     _pushConstants = pushConstants;
//     // OpenGL doesn't need explicit pipeline layout creation
//     // Uniforms are bound directly to the program
// }

// OpenGLPipeline::~OpenGLPipeline()
// {
//     cleanup();
// }

// void OpenGLPipeline::cleanup()
// {
//     if (_program != 0) {
//         glDeleteProgram(_program);
//         _program = 0;
//     }
// }

// bool OpenGLPipeline::recreate(const GraphicsPipelineCreateInfo &ci)
// {
//     _ci = ci;

//     // Clean up existing program
//     cleanup();

//     // Compile and link shaders
//     std::vector<GLuint> shaders;

//     // Vertex shader
//     if (!ci.vertexShader.spirv.empty()) {
//         GLuint vertShader = createShaderModule(GL_VERTEX_SHADER, ci.vertexShader.spirv);
//         if (vertShader == 0) {
//             YA_CORE_ERROR("Failed to create vertex shader");
//             return false;
//         }
//         shaders.push_back(vertShader);
//     }

//     // Fragment shader
//     if (!ci.fragmentShader.spirv.empty()) {
//         GLuint fragShader = createShaderModule(GL_FRAGMENT_SHADER, ci.fragmentShader.spirv);
//         if (fragShader == 0) {
//             YA_CORE_ERROR("Failed to create fragment shader");
//             // Clean up vertex shader
//             for (auto shader : shaders) {
//                 glDeleteShader(shader);
//             }
//             return false;
//         }
//         shaders.push_back(fragShader);
//     }

//     // Link program
//     _program = linkProgram(shaders);

//     // Clean up shader objects (they're no longer needed after linking)
//     for (auto shader : shaders) {
//         glDeleteShader(shader);
//     }

//     if (_program == 0) {
//         YA_CORE_ERROR("Failed to link shader program");
//         return false;
//     }

//     // Cache uniform locations
//     _uniformLocations.clear();

//     // Store pipeline state
//     _state.cullMode          = ci.rasterizationState.cullMode;
//     _state.depthTestEnabled  = ci.depthStencilState.depthTestEnable;
//     _state.depthWriteEnabled = ci.depthStencilState.depthWriteEnable;
//     _state.depthCompareOp    = ci.depthStencilState.depthCompareOp;
//     _state.blendEnabled      = ci.colorBlendState.attachments.empty() ? false : ci.colorBlendState.attachments[0].blendEnable;

//     YA_CORE_TRACE("Created OpenGL pipeline: program={}", _program);
//     return true;
// }

// void OpenGLPipeline::bind(CommandBufferHandle commandBuffer)
// {
//     glUseProgram(_program);
//     applyPipelineState();
// }

// GLint OpenGLPipeline::getUniformLocation(const std::string &name)
// {
//     auto it = _uniformLocations.find(name);
//     if (it != _uniformLocations.end()) {
//         return it->second;
//     }

//     GLint location = glGetUniformLocation(_program, name.c_str());
//     _uniformLocations[name] = location;
//     return location;
// }

// GLuint OpenGLPipeline::createShaderModule(GLenum type, const std::vector<uint32_t> &spv_binary)
// {
//     try {
//         // Use SPIRV-Cross to convert SPIR-V to GLSL
//         spirv_cross::CompilerGLSL glsl(spv_binary);

//         // Set options
//         spirv_cross::CompilerGLSL::Options options;
//         options.version = 450;  // GLSL 4.50
//         options.es      = false;
//         glsl.set_common_options(options);

//         // Compile to GLSL
//         std::string source = glsl.compile();

//         // Compile the GLSL shader
//         return compileShader(type, source);
//     }
//     catch (const std::exception &e) {
//         YA_CORE_ERROR("Failed to convert SPIR-V to GLSL: {}", e.what());
//         return 0;
//     }
// }

// GLuint OpenGLPipeline::compileShader(GLenum type, const std::string &source)
// {
//     GLuint shader = glCreateShader(type);
//     if (shader == 0) {
//         YA_CORE_ERROR("Failed to create shader object");
//         return 0;
//     }

//     const char *sourceCStr = source.c_str();
//     glShaderSource(shader, 1, &sourceCStr, nullptr);
//     glCompileShader(shader);

//     // Check compilation status
//     GLint success;
//     glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
//     if (!success) {
//         GLchar infoLog[1024];
//         glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
//         YA_CORE_ERROR("Shader compilation failed:\n{}", infoLog);
//         glDeleteShader(shader);
//         return 0;
//     }

//     return shader;
// }

// GLuint OpenGLPipeline::linkProgram(const std::vector<GLuint> &shaders)
// {
//     GLuint program = glCreateProgram();
//     if (program == 0) {
//         YA_CORE_ERROR("Failed to create program object");
//         return 0;
//     }

//     // Attach shaders
//     for (auto shader : shaders) {
//         glAttachShader(program, shader);
//     }

//     // Link program
//     glLinkProgram(program);

//     // Check link status
//     GLint success;
//     glGetProgramiv(program, GL_LINK_STATUS, &success);
//     if (!success) {
//         GLchar infoLog[1024];
//         glGetProgramInfoLog(program, 1024, nullptr, infoLog);
//         YA_CORE_ERROR("Program linking failed:\n{}", infoLog);
//         glDeleteProgram(program);
//         return 0;
//     }

//     return program;
// }

// void OpenGLPipeline::applyPipelineState()
// {
//     // Cull mode
//     switch (_state.cullMode) {
//     case ECullMode::None:
//         glDisable(GL_CULL_FACE);
//         break;
//     case ECullMode::Front:
//         glEnable(GL_CULL_FACE);
//         glCullFace(GL_FRONT);
//         break;
//     case ECullMode::Back:
//         glEnable(GL_CULL_FACE);
//         glCullFace(GL_BACK);
//         break;
//     case ECullMode::FrontAndBack:
//         glEnable(GL_CULL_FACE);
//         glCullFace(GL_FRONT_AND_BACK);
//         break;
//     }

//     // Depth test
//     if (_state.depthTestEnabled) {
//         glEnable(GL_DEPTH_TEST);
//         glDepthMask(_state.depthWriteEnabled ? GL_TRUE : GL_FALSE);

//         // Convert compare op to GL enum
//         GLenum depthFunc = GL_LESS;
//         switch (_state.depthCompareOp) {
//         case ECompareOp::Never:
//             depthFunc = GL_NEVER;
//             break;
//         case ECompareOp::Less:
//             depthFunc = GL_LESS;
//             break;
//         case ECompareOp::Equal:
//             depthFunc = GL_EQUAL;
//             break;
//         case ECompareOp::LessOrEqual:
//             depthFunc = GL_LEQUAL;
//             break;
//         case ECompareOp::Greater:
//             depthFunc = GL_GREATER;
//             break;
//         case ECompareOp::NotEqual:
//             depthFunc = GL_NOTEQUAL;
//             break;
//         case ECompareOp::GreaterOrEqual:
//             depthFunc = GL_GEQUAL;
//             break;
//         case ECompareOp::Always:
//             depthFunc = GL_ALWAYS;
//             break;
//         }
//         glDepthFunc(depthFunc);
//     }
//     else {
//         glDisable(GL_DEPTH_TEST);
//     }

//     // Blending
//     if (_state.blendEnabled) {
//         glEnable(GL_BLEND);
//         // Default blend function (can be extended based on CI)
//         glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//     }
//     else {
//         glDisable(GL_BLEND);
//     }
// }

// } // namespace ya
