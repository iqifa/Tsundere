#pragma once
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>

namespace SkyboxSettings
{
	inline std::filesystem::path ConfigPath()
	{
		wchar_t executable[32768]{};
		const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
		if (length == 0 || length == 32768)
			throw std::runtime_error("Cannot resolve executable path");
		auto directory = std::filesystem::path(executable).parent_path();
		for (;;)
		{
			if (std::filesystem::is_directory(directory / "res"))
				return directory / "ProjectSettings" / "Skybox.ini";
			const auto parent = directory.parent_path();
			if (parent == directory || parent.empty())
				throw std::runtime_error("Cannot locate project root for skybox settings");
			directory = parent;
		}
	}

	inline uint64_t Load(const std::filesystem::path& config)
	{
		if (!std::filesystem::exists(config))
			return {};
		std::ifstream file(config);
		std::string key;
		uint64_t uid = 0;
		if (!(file >> key >> uid) || key != "cubemap_uid") throw std::runtime_error("Invalid skybox UUID settings");
		file >> std::ws;
		if (!file.eof()) throw std::runtime_error("Unexpected skybox settings content");
		return uid;
	}

	inline void Save(const std::filesystem::path& config, uint64_t uid)
	{
		std::filesystem::create_directories(config.parent_path());
		const auto temporary = std::filesystem::path(config.wstring() + L"." +
			std::to_wstring(GetCurrentProcessId()) + L".tmp");
		try
		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			file << "cubemap_uid " << uid << '\n';
			file.close();
			if (!file || !MoveFileExW(temporary.c_str(), config.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				throw std::runtime_error("Cannot save skybox settings");
		}
		catch (...)
		{
			std::error_code error;
			std::filesystem::remove(temporary, error);
			throw;
		}
	}
}
