#pragma once
#include <string>
#include <vector>

class DirectoryScanner
{
public:
	static std::vector<std::string> GetFiles(const std::string& path);
    static std::vector<std::string> scan(const std::string& path);
private:
    DirectoryScanner() = delete;
};