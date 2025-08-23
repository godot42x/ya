//
// Created by nono on 10/14/23.
//

#include "Instrumentor.h"

namespace ya
{



void Instrumentor::BeginSession(const std::string &name, const std::string &filepath /*= "results.json"*/)
{
    auto path = std::filesystem::path(filepath);
    if (!std::filesystem::exists(path.parent_path())) {
        if (auto res = std::filesystem::create_directories(path.parent_path())) {
            YA_CORE_ASSERT(res, "No such dir, and then failed when creating");
        }
    }

    m_OutputStream.open(filepath, std::ios::trunc);
    YA_CORE_ASSERT(m_OutputStream.is_open() && !m_OutputStream.fail(), "Opening result file failed");
    m_CurrentSession = new InstrumentationSession{name};
    WriteHeader();
}
void Instrumentor::EndSession()
{
    WriteFooter();
    m_OutputStream.close();
    delete m_CurrentSession;
    m_CurrentSession = nullptr;
    m_ProfileCount   = 0;
}

void Instrumentor::WriteProfile(const ProfileResult &result)
{
    if (m_ProfileCount++ > 0) {
        m_OutputStream << ",";
    }

    std::string name = result.Name;
    std::replace(name.begin(), name.end(), '"', '\'');

    m_OutputStream << "{";
    m_OutputStream << R"("cat":"function",)";
    m_OutputStream << "\"dur\":" << (result.End - result.Start) << ',';
    m_OutputStream << R"("name":")" << name << "\",";
    m_OutputStream << R"("ph":"X",)";
    m_OutputStream << "\"pid\":0,";
    m_OutputStream << "\"tid\":" << result.ThreadID << ",";
    m_OutputStream << "\"ts\":" << result.Start;
    m_OutputStream << "}";

    m_OutputStream.flush();
}
void Instrumentor::WriteHeader()
{
    m_OutputStream << "{\"otherData\": {},\"traceEvents\":[";
    m_OutputStream.flush();
}
void Instrumentor::WriteFooter()
{
    m_OutputStream << "]}";
    m_OutputStream.flush();
}


} // namespace ya