#include "Core/MaterialResourcePath.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image/stb_image.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Expected project root");
        namespace fs = std::filesystem;
        const auto root = fs::absolute(argv[1]);
        int decoded = 0;
        for (const auto& cwd : {root, root / "bin", fs::temp_directory_path()}) {
            fs::current_path(cwd);
            for (const auto& entry : fs::directory_iterator(root / "Assets/Sponza/Materials")) {
                if (entry.path().extension() != ".tmat") continue;
                std::ifstream file(entry.path()); std::string line;
                while (std::getline(file, line)) {
                    const std::string prefix = "texture_diffuse1 = texture2d ";
                    if (line.rfind(prefix, 0) != 0) continue;
                    auto ref = line.substr(prefix.size());
                    if (!ref.empty() && ref.back() == '\r') ref.pop_back();
                    const auto resolved = Engine::ResolveMaterialResourcePath(entry.path().string(), ref);
                    if (!fs::path(resolved).is_absolute()) throw std::runtime_error("Unresolved texture: " + resolved);
                    int w=0,h=0,c=0; auto* pixels=stbi_load(resolved.c_str(), &w,&h,&c,4);
                    if (!pixels || w <= 0 || h <= 0) throw std::runtime_error("Decode failed: " + resolved);
                    stbi_image_free(pixels); ++decoded;
                    auto local = fs::relative(resolved, entry.path().parent_path()).string();
                    if (Engine::ResolveMaterialResourcePath(entry.path().string(),local) != resolved)
                        throw std::runtime_error("Material-relative resolution differs");
                    if (Engine::ResolveMaterialResourcePath(entry.path().string(),resolved) != resolved)
                        throw std::runtime_error("Absolute resolution differs");
                }
                auto shader=Engine::ResolveMaterialResourcePath(entry.path().string(),"Assets/Sponza/SponzaLambert.shader");
                if(!fs::is_regular_file(shader)) throw std::runtime_error("Missing shader");
            }
        }
        if(decoded != 72) throw std::runtime_error("Expected 24 textures in each of 3 working directories");
        std::cout << "PASS: 24 Sponza textures decoded from project, bin and temp working directories; relative/absolute references agree\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
