#include "ShaderParser.h"
#include "Debug/Debug.h"
#include <fstream>
#include <sstream>
#include <iostream>

ParsedShader ParseShaderFile(const std::string& filepath)
{
    std::ifstream stream(filepath);
    if (!stream.is_open())
    {
        Error_Core("Failed to open shader file at path: " + filepath);
        return {};
    }

    enum class Section { NONE = -1, VERTEX = 0, FRAGMENT = 1, COMPUTE = 2 };

    std::string line;
    std::stringstream ss[3];
    Section section = Section::NONE;
    bool inSystemBlock = false;

    ParsedShader result;

    while (std::getline(stream, line))
    {
        if (line.find("#shader") != std::string::npos)
        {
            if (line.find("vertex") != std::string::npos)
                section = Section::VERTEX;
            else if (line.find("fragment") != std::string::npos)
                section = Section::FRAGMENT;
            else if (line.find("compute") != std::string::npos)
                section = Section::COMPUTE;
        }
        else if (line.find("[Header") != std::string::npos)
        {
            std::string label;
            int index = line.find("[Header") + 8;
            label = line.substr(index, line.length() - index - 2);
            result.uniforms.push_back({ label, "Head" });
        }
        else if (line.find("[System]") != std::string::npos)
        {
            inSystemBlock = !inSystemBlock;
        }
        else
        {
            if (section != Section::NONE)
                ss[(int)section] << line << '\n';

            // Extract uniform declarations
            int uidx = line.find("uniform");
            if (uidx != std::string::npos)
            {
                int endpos;
                while (line[uidx] != ' ')
                    uidx++;
                endpos = uidx + 1;
                while (line[endpos] != ' ')
                    endpos++;
                std::string typeName = line.substr(uidx + 1, endpos - uidx - 1);

                uidx = endpos;
                while (line[uidx] == ' ')
                    uidx++;
                std::string rawName = line.substr(uidx, line.length() - uidx);
                size_t semiPos = rawName.find(';');
                if (semiPos != std::string::npos)
                    rawName = rawName.substr(0, semiPos);
                while (!rawName.empty() && rawName.back() == ' ')
                    rawName.pop_back();

                if (!inSystemBlock)
                    result.uniforms.push_back({ rawName, typeName });
            }
        }
    }

    // Map section index → ShaderStage and store non-empty sources
    if (!ss[0].str().empty())
        result.sources[ShaderStage::Vertex] = ss[0].str();
    if (!ss[1].str().empty())
        result.sources[ShaderStage::Fragment] = ss[1].str();
    if (!ss[2].str().empty())
        result.sources[ShaderStage::Compute] = ss[2].str();

    return result;
}
