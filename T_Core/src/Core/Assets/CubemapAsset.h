#pragma once
#include "Debug/Debug.h"
#include "Core/Assets/AssetDatabase.h"
#include "stb_image/stb_image.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <iomanip>
#include <stdexcept>
#include <vector>

namespace Engine
{
	// Six texture UUIDs in +X,-X,+Y,-Y,+Z,-Z order; no paths or image payload.
	struct CubemapAsset
	{
		uint32_t size = 0;
		std::vector<unsigned char> pixels;

		static size_t ByteCount(uint32_t size)
		{
			if (size == 0 || size > 4096)
				throw std::runtime_error("Cubemap face size must be between 1 and 4096");
			return size_t(size) * size * 4 * 6;
		}

		// Faces contain the stable IDs used by the project's .meta files.
		static std::array<std::string, 6> References(const std::string& path)
		{
			std::ifstream file(std::filesystem::path(path), std::ios::binary);
			std::string magic;
			if (!(file >> magic) || magic != "TCUBEMAP_UID") throw std::runtime_error("Invalid UUID cubemap: " + path);
			std::array<std::string, 6> faces;
			for (auto& face : faces)
			{
				uint64_t uid = 0;
				if (!(file >> uid) || uid == 0) throw std::runtime_error("Invalid cubemap texture UUID: " + path);
				face = AssetDatabase::Resolve(uid, AssetKind::Texture);
				if (face.empty()) throw std::filesystem::filesystem_error("Unresolved cubemap texture UUID " + std::to_string(uid), std::filesystem::path(path), std::make_error_code(std::errc::no_such_file_or_directory));
			}
			file >> std::ws;
			if (!file.eof()) throw std::runtime_error("Unexpected cubemap data: " + path);
			return faces;
		}

		static CubemapAsset Load(const std::string& path)
		{
			return DecodeFaces(References(path), true);
		}
		static CubemapAsset DecodeFaces(const std::array<std::string, 6>& faces, bool retainPixels)
		{
			CubemapAsset asset;
			for (size_t face = 0; face < faces.size(); ++face)
			{
				if (!std::filesystem::is_regular_file(std::filesystem::u8path(faces[face])))
					throw std::filesystem::filesystem_error("Cubemap face missing", std::filesystem::u8path(faces[face]), std::make_error_code(std::errc::no_such_file_or_directory));
				int width = 0, height = 0, channels = 0;
				std::unique_ptr<unsigned char, decltype(&stbi_image_free)> image(
					LoadImageSTB(faces[face].c_str(), &width, &height, &channels, 4, false),
					stbi_image_free);
				if (!image || width != height || width <= 0 ||
					(face > 0 && uint32_t(width) != asset.size))
					throw std::runtime_error("Missing, non-square or mismatched cubemap face: " + faces[face]);
				if (face == 0)
				{
					asset.size = uint32_t(width);
					if (retainPixels) asset.pixels.reserve(ByteCount(asset.size));
					else ByteCount(asset.size);
				}
				if (retainPixels) asset.pixels.insert(asset.pixels.end(), image.get(), image.get() + size_t(width) * height * 4);
			}
			return asset;
		}

		static void Import(const std::array<std::string, 6>& faces, const std::string& destination)
		{
			DecodeFaces(faces, false);
			std::array<uint64_t, 6> ids{};
			for (size_t i = 0; i < faces.size(); ++i)
			{
				ids[i] = AssetDatabase::UidForPath(faces[i], AssetKind::Texture);
				if (!ids[i]) throw std::runtime_error("Import texture into project Assets first: " + faces[i]);
			}
			if (std::filesystem::exists(destination))
				throw std::runtime_error("Cubemap already exists: " + destination);
			const std::string temporary = destination + ".importing";
			if (std::filesystem::exists(temporary))
				throw std::runtime_error("Cubemap import already in progress: " + temporary);
			try
			{
				std::ofstream file(std::filesystem::path(temporary), std::ios::binary);
				file << "TCUBEMAP_UID\n";
				for (const auto uid : ids) file << uid << '\n';
				file.close();
				if (!file)
					throw std::runtime_error("Cannot write cubemap: " + destination);
				std::filesystem::rename(temporary, destination);
			}
			catch (...)
			{
				std::error_code error;
				std::filesystem::remove(temporary, error);
				throw;
			}
		}
	};
}
