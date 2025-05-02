#pragma once
#include "AMTools.hpp"
#include <aclapi.h>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fmt/format.h>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <sddl.h>
#include <shlwapi.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <variant>
#include <windows.h>

namespace AMPathTools
{
    namespace fs = std::filesystem;
    using CB = std::shared_ptr<std::function<void(std::string, std::string, std::string)>>;
    constexpr char *exc1 = "am1sp2exg6";
    constexpr char *exc2 = "amtmpexc5";
    constexpr char *exc3 = "atmasdapxch";
    constexpr char *exc4 = "amtmasdapaexch";
    constexpr char *exc5 = "amtessixhxch";
    constexpr char *exc6 = "amtasdmpexsch";
    namespace ENUMS
    {
        enum class PathType
        {
            BlockDevice = -1,
            CharacterDevice = -2,
            Socket = -3,
            FIFO = -4,
            Unknown = -5,
            DIR = 0,
            FILE = 1,
            SYMLINK = 2
        };

        enum class PathFormat
        {
            Absolute = 1,
            Relative = 2,
            User = 3
        };

        enum class SearchType
        {
            All = 0,
            File = 1,
            Directory = 2
        };
    }

    std::string regex_escape(const std::string &input)
    {
        std::string escaped;
        escaped.reserve(input.size() * 2); // 预分配内存优化性能

        for (char c : input)
        {
            // 匹配需要转义的正则元字符
            switch (c)
            {
            case '\\':
            case '^':
            case '$':
            case '.':
            case '|':
            case '?':
            case '*':
            case '+':
            case '(':
            case ')':
            case '[':
            case ']':
            case '{':
            case '}':
                escaped += '\\'; // 添加转义符
                break;
            default:
                break;
            }
            escaped += c; // 追加当前字符
        }

        return escaped;
    }

    bool is_valid_utf8(const std::string &str)
    {
        int remaining = 0;
        for (unsigned char c : str)
        {
            if (remaining > 0)
            {
                if ((c & 0xC0) != 0x80)
                    return false;
                --remaining;
            }
            else
            {
                if ((c & 0x80) == 0x00)
                    continue; // 单字节 0xxxxxxx
                else if ((c & 0xE0) == 0xC0)
                    remaining = 1; // 双字节 110xxxxx
                else if ((c & 0xF0) == 0xE0)
                    remaining = 2; // 三字节 1110xxxx
                else if ((c & 0xF8) == 0xF0)
                    remaining = 3; // 四字节 11110xxx
                else
                    return false;
            }
        }
        return (remaining == 0);
    }

    std::string AMstr(const std::wstring &wstr)
    {
        int codePage = GetACP();
        int len = WideCharToMultiByte(codePage, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0)
            return "";
        std::string result(len - 1, 0);
        WideCharToMultiByte(codePage, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);
        return result;
    };

    std::string AMstr(const wchar_t *wstr)
    {
        int codePage = GetACP();
        int len = WideCharToMultiByte(codePage, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0)
            return "";
        std::string result(len - 1, 0);
        WideCharToMultiByte(codePage, 0, wstr, -1, &result[0], len, nullptr, nullptr);
        return result;
    }

    std::wstring AMstr(const std::string &str)
    {
        int codePage = CP_ACP;
        if (is_valid_utf8(str))
        {
            codePage = CP_UTF8;
        }
        int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, nullptr, 0);
        if (len <= 0)
            return L"";
        std::wstring result(len - 1, 0);
        MultiByteToWideChar(codePage, 0, str.c_str(), -1, &result[0], len);
        return result;
    };

    std::wstring AMstr(const char *str)
    {
        int codePage = CP_ACP;
        if (is_valid_utf8(str))
        {
            codePage = CP_UTF8;
        }
        int len = MultiByteToWideChar(CP_ACP, 0, str, -1, nullptr, 0);
        if (len <= 0)
            return L"";
        std::wstring result(len - 1, 0);
        MultiByteToWideChar(codePage, 0, str, -1, &result[0], len);
        return result;
    }

    namespace WinAPI
    {
        uint64_t FileTimeToUnixTime(const FILETIME &ft)
        {
            ULARGE_INTEGER uli;
            uli.LowPart = ft.dwLowDateTime;
            uli.HighPart = ft.dwHighDateTime;
            return (uli.QuadPart / 10000000ULL) - 11644473600ULL;
        }

        std::string GetFileOwner(const std::wstring &path)
        {
            PSID pSidOwner = NULL;
            PSECURITY_DESCRIPTOR pSD = NULL;
            DWORD dwRtnCode = GetNamedSecurityInfoW(
                path.c_str(),
                SE_FILE_OBJECT,
                OWNER_SECURITY_INFORMATION,
                &pSidOwner,
                NULL,
                NULL,
                NULL,
                &pSD);

            std::wstring owner = L"unknown";
            if (dwRtnCode == ERROR_SUCCESS)
            {
                wchar_t szOwnerName[256];
                wchar_t szDomainName[256];
                DWORD dwNameLen = 256;
                DWORD dwDomainLen = 256;
                SID_NAME_USE eUse;

                if (LookupAccountSidW(
                        NULL,
                        pSidOwner,
                        szOwnerName,
                        &dwNameLen,
                        szDomainName,
                        &dwDomainLen,
                        &eUse))
                {
                    owner = szOwnerName;
                }
            }

            if (pSD)
            {
                LocalFree(pSD);
            }

            return AMPathTools::AMstr(owner);
        }

        std::pair<uint64_t, uint64_t> GetTime(const std::wstring &path)
        {
            HANDLE hFile = CreateFileW(
                path.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ,
                NULL,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS,
                NULL);
            if (hFile == INVALID_HANDLE_VALUE)
            {
                return std::make_pair(0, 0);
            }
            FILETIME ftAccess, ftModify;
            GetFileTime(hFile, NULL, &ftAccess, &ftModify);
            CloseHandle(hFile); // 立即释放句柄
            ULARGE_INTEGER uliAccess{
                ftAccess.dwLowDateTime,
                ftAccess.dwHighDateTime};
            ULARGE_INTEGER uliModify{
                ftModify.dwLowDateTime,
                ftModify.dwHighDateTime};
            return std::make_pair((uliAccess.QuadPart - 116444736000000000ULL) / 1e7, (uliModify.QuadPart - 116444736000000000ULL) / 1e7);
        }

        bool is_readonly(const std::wstring &path)
        {
            DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && attributes & FILE_ATTRIBUTE_READONLY)
            {
                return true;
            }
            return false;
        }
    }

    bool _match(const std::string &name, const std::string &pattern_f, const bool &use_regex)
    {
        std::string pattern = pattern_f;
        if ((!use_regex) || (pattern.front() != '<'))
        {
            pattern = std::regex_replace(pattern, std::regex("\\*"), std::string(exc1));
            pattern = regex_escape(pattern);
            pattern = std::regex_replace(pattern, std::regex(exc1), ".*");

            try
            {
                std::wstring p_w = AMPathTools::AMstr(pattern);
                std::wstring n_w = AMPathTools::AMstr(name);
                return std::regex_match(n_w, std::wregex(p_w));
            }
            catch (std::exception e)
            {

                return false;
            }
        }
        else
        {
            if (pattern.size() < 2)
            {
                return false;
            }
            pattern = pattern.substr(1);
            try
            {

                std::wregex pattern_f(AMPathTools::AMstr(pattern));
                bool result = std::regex_search(AMPathTools::AMstr(name), pattern_f);
                return result;
            }
            catch (const std::exception &e)
            {

                return false;
            }
        }
    }

    std::variant<bool, std::string> isPatternsValid(const std::vector<std::string> &patterns)
    {
        std::regex pattern_f;
        for (auto pattern : patterns)
        {
            if (pattern.empty())
            {
                return "Recive an empty pattern";
            }
            if (pattern[0] != '<')
            {
                continue;
            }
            if (pattern.size() == 1)
            {
                return "Recive a single <";
            };

            try
            {
                pattern_f = std::regex(pattern.substr(1));
            }
            catch (std::exception &e)
            {
                return fmt::format("Pattern \" {} \" encounters {}", pattern, e.what());
            }
        }
        return true;
    }

    std::vector<std::string> UniqueVector(const std::vector<std::string> &ori_vec)
    {
        size_t length = ori_vec.size();
        if (length < 2)
            return ori_vec;

        std::vector<std::string> new_vec;
        new_vec.reserve(length);
        std::unordered_set<std::string> seen;

        for (const auto &item : ori_vec)
        {
            if (seen.insert(item).second)
            {
                new_vec.push_back(item);
            }
        }
        return new_vec;
    }
}

namespace AMPath
{
    namespace fs = std::filesystem;
    using CB = std::shared_ptr<std::function<void(std::string, std::string, std::string)>>;
    constexpr char *AMERROR = "ERROR";
    constexpr char *AMWARNING = "WARNING";

    struct PathInfo
    {
    public:
        std::string name;
        std::string path;
        std::string dir;
        std::string uname;
        uint64_t size = 0;
        uint64_t atime = 0;
        uint64_t mtime = 0;
        AMPathTools::ENUMS::PathType type = AMPathTools::ENUMS::PathType::FILE;
        uint64_t mode_int = 0777;
        std::string mode_str = "rwxrwxrwx";
        PathInfo() : name(""), path(""), dir(""), uname("") {}
        PathInfo(std::string name, std::string path, std::string dir, std::string uname, uint64_t size = 0, uint64_t atime = 0, uint64_t mtime = 0, AMPathTools::ENUMS::PathType type = AMPathTools::ENUMS::PathType::FILE, uint64_t mode_int = 0777, std::string mode_str = "rwxrwxrwx") : name(name), path(path), dir(dir), uname(uname), size(size), atime(atime), mtime(mtime), type(type), mode_int(mode_int), mode_str(mode_str) {}
        std::string FormatTime(const uint64_t &time, const std::string &format = "%Y-%m-%d %H:%M:%S") const
        {
            time_t timeT = static_cast<time_t>(time);

            struct tm timeInfo;

#ifdef _WIN32

            localtime_s(&timeInfo, &timeT);
#else
            localtime_r(&timeT, &timeInfo);
#endif

            std::ostringstream oss;
            oss << std::put_time(&timeInfo, format.c_str());

            return oss.str();
        }
    };

    std::string ModeTrans(uint64_t mode_int)
    {
        // 把mode_int转换为8进制字符串, 长度为9
        if (mode_int > 0777 || mode_int == 0777)
        {
            return "rwxrwxrwx";
        }
        std::string out = "";
        uint64_t tmp_int;
        uint64_t start = 8 * 8 * 8;
        for (int i = 3; i > 0; i--)
        {
            tmp_int = (mode_int % start) / (start / 8);
            start /= 8;
            switch (tmp_int)
            {
            case 1:
                out += "--x";
                break;
            case 2:
                out += "-w-";
                break;
            case 3:
                out += "-wx";
                break;
            case 4:
                out += "r--";
                break;
            case 5:
                out += "r-x";
                break;
            case 6:
                out += "rw-";
                break;
            case 7:
                out += "rwx";
                break;
            default:
                out += "---";
            }
        }
        return out;
    }

    uint64_t ModeTrans(std::string mode_str)
    {
        std::regex pattern("^[r?\\-][w?\\-][x?\\-][r?\\-][w?\\-][x?\\-][r?\\-][w?\\-][x?\\-]$");
        if (!std::regex_match(mode_str, pattern))
        {
            throw std::invalid_argument(fmt::format("Invalid mode string: {}", mode_str));
        }
        uint64_t mode_int = 0;
        for (int i = 0; i < 9; i++)
        {
            if (mode_str[i] != '?' && mode_str[i] != '-')
            {
                mode_int += (1ULL << (8 - i));
            }
        }
        return mode_int;
    }

    std::string MergeModeStr(const std::string &base_mode_str, const std::string &new_mode_str)
    {
        std::string pattern_f = "^[r?\\-][w?\\-][x?\\-][r?\\-][w?\\-][x?\\-][r?\\-][w?\\-][x?\\-]$";
        std::regex pattern(pattern_f);

        if (!std::regex_match(base_mode_str, pattern))
        {
            throw std::invalid_argument(fmt::format("Invalid base mode string: {}", base_mode_str));
        }

        if (!std::regex_match(new_mode_str, pattern))
        {
            throw std::invalid_argument(fmt::format("Invalid new mode string: {}", new_mode_str));
        }

        std::string mode_str = "";
        for (int i = 0; i < 9; i++)
        {
            mode_str += (new_mode_str[i] == '?' ? base_mode_str[i] : new_mode_str[i]);
        }
        return mode_str;
    }

    bool IsModeValid(std::string mode_str)
    {
        return std::regex_match(mode_str, std::regex("^[r?\\-][w?\\-][x?\\-][r?\\-][w?\\-][x?\\-][r?\\-][w?\\-][x?\\-]$"));
    }

    bool IsModeValid(uint64_t mode_int)
    {
        return mode_int <= 0777;
    }

    bool is_absolute(const std::string &path)
    {
        std::regex merged_regex("^(?:[A-Za-z]:[/\\\\]?|/|\\\\\\\\|~[\\\\/])");
        return std::regex_search(path, merged_regex);
    }

    std::string Strip(std::string path)
    {
        const std::string trim_chars = " \t\n\r\"'";

        size_t start = path.find_first_not_of(trim_chars);

        if (start == std::string::npos)
        {
            return "";
        }

        size_t end = path.find_last_not_of(trim_chars);
        return path.substr(start, end - start + 1);
    }

    void VStrip(std::string &path)
    {
        const std::string trim_chars = " \t\n\r\"'";

        size_t start = path.find_first_not_of(trim_chars);
        if (start == std::string::npos)
        {
            path = "";
        }

        size_t end = path.find_last_not_of(trim_chars);
        path = path.substr(start, end - start + 1);
    }

    std::string extension(const std::string &path)
    {
        return fs::path(path).extension().string();
    }

    std::string GetPathSep(const std::string &path)
    {
        int slash_count = 0;
        int anti_slash_count = 0;
        for (auto &c : path)
        {
            if (c == '/')
            {
                slash_count++;
            }
            else if (c == '\\')
            {
                anti_slash_count++;
            }
        }
        return slash_count > anti_slash_count ? "/" : "\\";
    }

    std::string HomePath()
    {
        char *home = std::getenv("HOMEPROFILE");
        if (home)
        {
            return home;
        }
        else
        {
            return "C:\\Users\\Administrator";
        }
    }

    std::string ShapePath(std::string path, std::string sep = "")
    {
        path = AMPath::Strip(path);
        if (path.size() < 2)
            return path;
        if (sep.empty())
        {
            sep = AMPath::GetPathSep(path);
        }
        std::string head = path.substr(0, 2);

        std::regex slash_pt("[\\\\/]+");
        path = head + std::regex_replace(path.substr(2), slash_pt, sep);
        return path;
    }

    std::vector<std::string> split(std::string &path_f)
    {
        // Window style path supported only!
        std::string path = AMPath::ShapePath(path_f);

        if (path.size() < 2)
        {
            return {path};
        }
        std::vector<std::string> result{};
        std::string head = path.substr(0, 2);
        if (head == "//" || head == "\\\\")
        {
            path = path.substr(2);
        }
        else
        {
            head.clear();
        }

        std::string cur = "";
        for (auto c : path)
        {
            if (c == '\\' || c == '/')
            {
                if (!cur.empty())
                {
                    result.push_back(cur);
                    cur = "";
                }
            }
            else
            {
                cur += c;
            }
        }
        if (!cur.empty())
        {
            result.push_back(cur);
        }
        if (!head.empty())
        {
            result[0] = "\\\\" + result[0];
        }

        return result;
    }

    std::vector<std::string> resplit(std::string path, char front_esc, char back_esc, std::string head = "")
    {
        if (path.size() < 2)
        {
            return {path};
        }
        std::vector<std::string> parts{};
        // for network mount path
        std::string head_two = path.substr(0, 2);
        if (head == "//" || head == "\\\\")
        {
            path = path.substr(2);
        }
        else
        {
            head_two.clear();
        }
        std::string tmp_str = "";
        bool in_brackets = false;
        for (auto &sig : path)
        {
            if (in_brackets)
            {
                if (sig == back_esc)
                {
                    in_brackets = false;
                    if (!tmp_str.empty())
                    {
                        parts.push_back(head + tmp_str);
                        tmp_str.clear();
                    }
                }
                else
                {
                    tmp_str += sig;
                }
            }
            else
            {
                if (sig == front_esc)
                {
                    in_brackets = true;
                }
                else if (sig == '/' || sig == '\\')
                {
                    if (!tmp_str.empty())
                    {
                        parts.push_back(tmp_str);
                        tmp_str.clear();
                    }
                }
                else
                {
                    tmp_str += sig;
                }
            }
        }
        if (!tmp_str.empty())
        {
            parts.push_back(tmp_str);
        }
        if (!head_two.empty())
        {
            parts[0] = "\\\\" + parts[0];
        }
        return parts;
    }

    template <typename... Args>
    std::string join(Args &&...args)
    {
        std::vector<std::string> segments;
        std::string ori_str;
        fs::path combined;

        auto process_arg = [&](auto &&arg)
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::filesystem::path>)
            {
                if (!arg.empty())
                {
                    segments.push_back(arg.string());
                    ori_str += arg.string();
                }
            }
            else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, const std::string>)
            {
                if (!arg.empty())
                {
                    segments.push_back(arg);
                    ori_str += arg;
                }
            }
            else if constexpr (std::is_same_v<T, std::vector<std::string>> || std::is_same_v<T, const std::vector<std::string>>)
            {
                for (auto &seg : arg)
                {
                    if (!seg.empty())
                    {
                        segments.push_back(seg);
                        ori_str += seg;
                    }
                }
            }
            else if constexpr (std::is_same_v<T, std::wstring> || std::is_same_v<T, const std::wstring>)
            {
                std::string s = AMPathTools::AMstr(arg);
                if (!s.empty())
                {
                    segments.push_back(s);
                    ori_str += s;
                }
            }
            else if constexpr (std::is_same_v<T, const char *> || std::is_same_v<T, char *>)
            {
                std::string s = std::string(arg);
                if (!s.empty())
                {
                    segments.push_back(s);
                    ori_str += s;
                }
            }
            else if constexpr (std::is_same_v<T, const wchar_t *> || std::is_same_v<T, wchar_t *>)
            {
                std::string s = AMPathTools::AMstr(arg);
                if (!s.empty())
                {
                    segments.push_back(s);
                    ori_str += s;
                }
            }
        };

        (process_arg(std::forward<Args>(args)), ...);

        if (segments.empty())
            return "";
        else if (segments.size() == 1)
        {
            return segments.front();
        }

        std::string result;
        std::string sep = GetPathSep(ori_str);
        for (auto &seg : segments)
        {
            result += (seg + sep);
        }
        result.pop_back();
        return result;
    }

    std::string realpath(std::vector<std::string> &parts, std::string sep = "")
    {
        std::string new_sep = sep.empty() ? "\\" : sep;
        std::vector<std::string> new_parts{};

        if (parts.front() == "~")
        {
            std::string hm = HomePath();
            for (const auto seg : AMPath::split(hm))
            {
                new_parts.push_back(seg);
            }
            parts.erase(parts.begin());
        }

        std::string tmp_part;
        for (auto &tmp_part : parts)
        {
            if (tmp_part == ".")
            {
                continue;
            }
            else if (tmp_part == "..")
            {
                if (new_parts.size() > 1)
                {
                    new_parts.pop_back();
                }
            }
            else
            {
                new_parts.push_back(tmp_part);
            }
        }

        std::string result;
        for (const auto part : new_parts)
        {
            result += part + new_sep;
        }
        result.pop_back();

        return result;
    }

    std::string realpath(std::string path, bool force_absolute = false, std::string sep = "")
    {

        if (path.size() < 2)
        {
            if (path != "/")
            {
                fs::current_path().string() + "\\" + path;
            }
        }

        std::string new_sep = sep.empty() ? GetPathSep(path) : sep;

        if (!is_absolute(path) && !force_absolute)
        {
            path = fs::current_path().string() + new_sep + path;
        }

        std::vector<std::string> parts = AMPath::split(path);
        if (parts.empty())
        {
            return "";
        }

        return AMPath::realpath(parts, sep);
    }

    std::string dirname(const std::string &path)
    {
        fs::path p(realpath(path));
        if (p.parent_path().empty())
        {
            return "";
        }
        return p.parent_path().string();
    }

    std::string basename(const std::string &path)
    {
        fs::path p(path);
        return p.filename().string();
    }

    std::optional<std::pair<std::string, std::exception>> mkdirs(const std::string &path)
    {
        try
        {
            fs::path p(path);
            fs::create_directories(p);
            return std::nullopt;
        }
        catch (const std::exception e)
        {
            return std::make_pair("Mkdir error", e);
        }
    }

    std::variant<PathInfo, std::pair<std::string, std::exception>> stat(const std::string &path, bool trace_link = false)
    {
        PathInfo info;
        fs::path p(path);
        std::wstring wpath = AMPathTools::AMstr(path);
        info.name = p.filename().string();
        info.path = realpath(path);
        info.dir = p.parent_path().string();
        fs::file_status status;
        try
        {
            if (trace_link)
            {
                status = fs::status(p);
            }
            else
            {
                status = fs::symlink_status(p);
            }
        }
        catch (const std::exception &e)
        {
            return std::make_pair("Stat error: " + path, e);
        }

        switch (status.type())
        {
        case fs::file_type::directory:
            info.type = AMPathTools::ENUMS::PathType::DIR;
            break;
        case fs::file_type::symlink:
            info.type = AMPathTools::ENUMS::PathType::SYMLINK;
            break;
        case fs::file_type::regular:
            info.type = AMPathTools::ENUMS::PathType::FILE;
            break;
        case fs::file_type::block:
            info.type = AMPathTools::ENUMS::PathType::BlockDevice;
            break;
        case fs::file_type::character:
            info.type = AMPathTools::ENUMS::PathType::CharacterDevice;
            break;
        case fs::file_type::fifo:
            info.type = AMPathTools::ENUMS::PathType::FIFO;
            break;
        case fs::file_type::socket:
            info.type = AMPathTools::ENUMS::PathType::Socket;
            break;
        case fs::file_type::unknown:
            info.type = AMPathTools::ENUMS::PathType::Unknown;
            break;
        default:
            info.type = AMPathTools::ENUMS::PathType::Unknown;
            break;
        }

        if (info.type == AMPathTools::ENUMS::PathType::FILE)
        {
            try
            {
                info.size = fs::file_size(p);
            }
            catch (const std::exception)
            {
            }
        }

        if (AMPathTools::WinAPI::is_readonly(AMPathTools::AMstr(path)))
        {
            info.mode_int = 0333;
            info.mode_str = "r-xr-xr-x";
        }
        else
        {
            info.mode_int = 0666;
            info.mode_str = "rwxrwxrwx";
        }

        auto [atime, mtime] = AMPathTools::WinAPI::GetTime(wpath);
        info.atime = atime;
        info.mtime = mtime;
        info.uname = AMPathTools::WinAPI::GetFileOwner(wpath);
        return info;
    }

    std::vector<PathInfo> listdir(const std::string &path)
    {
        std::vector<PathInfo> result = {};
        fs::path p(path);
        if (!fs::exists(p))
        {
            return result;
        }
        if (!fs::is_directory(p))
        {
            return result;
        }
        std::variant<PathInfo, std::pair<std::string, std::exception>> sr;
        for (const auto &entry : fs::directory_iterator(p))
        {
            sr = stat(entry.path().string(), false);
            if (std::holds_alternative<PathInfo>(sr))
            {
                result.push_back(std::get<PathInfo>(sr));
            }
        }
        return result;
    }

    void _walk(std::string path, std::vector<PathInfo> &result, bool ignore_sepcial_file, bool trace_link)
    {
        fs::path p(path);
        fs::file_status status;
        if (!fs::exists(p))
        {
            return;
        }
        auto sr = stat(path, trace_link);
        if (std::holds_alternative<PathInfo>(sr))
        {
            result.push_back(std::get<PathInfo>(sr));
        }
    }

    std::vector<PathInfo> walk(const std::string &path, bool ignore_sepcial_file, bool trace_link)
    {
        std::vector<PathInfo> results = {};

        _walk(path, results, ignore_sepcial_file, trace_link);

        return results;
    }

    void _getsize(const std::string &path, uint64_t &result, bool trace_link)
    {
        fs::path p(path);
        if (!fs::exists(p))
        {
            return;
        }
        if (fs::is_directory(p))
        {
            for (const auto &entry : fs::directory_iterator(p))
            {
                _getsize(entry.path().string(), result, trace_link);
            }
        }
        else if (fs::is_symlink(p))
        {
            if (trace_link)
            {
                _getsize(fs::read_symlink(p).string(), result, trace_link);
            }
        }
        else if (fs::is_regular_file(p))
        {
            result += fs::file_size(p);
        }
    }

    uint64_t getsize(const std::string &path, bool trace_link = false)
    {
        uint64_t result = 0;
        _getsize(path, result, trace_link);
        return result;
    }

    std::tuple<std::vector<std::string>, std::string, bool> preprocess(std::string path, bool use_regex)
    {
        if (!is_absolute(path))
        {
            path = fs::current_path().string() + "\\" + path;
        }

        std::vector<std::string> ori_parts;
        if (use_regex)
        {
            ori_parts = AMPath::resplit(path, '<', '>', "<");
        }
        else
        {
            ori_parts = AMPath::split(path);
        }

        std::vector<std::string> path_parts = {};

        bool is2star = false;
        bool is_recursive = false;

        // delete consecutive "**"
        for (int i = 0; i < ori_parts.size(); i++)
        {
            auto part = ori_parts[i];
            if (part == "**")
            {
                is_recursive = true;
                if (is2star)
                {
                    continue;
                }
                else
                {
                    is2star = true;
                    path_parts.push_back(part);
                }
            }
            else
            {
                is2star = false;
                path_parts.push_back(part);
            }
        }

        std::vector<std::string> match_parts;
        std::vector<std::string> root_parts;

        int num_i = 0;
        bool is_match;
        bool is_end = false;
        for (auto part : path_parts)
        {
            if (is_end)
            {
                match_parts.push_back(part);
                continue;
            }

            if (use_regex)
            {
                is_match = (part[0] == '<') ? true : (part.find("*") != std::string::npos);
            }
            else
            {
                is_match = part.find("*") != std::string::npos;
            }

            if (is_match)
            {
                match_parts.push_back(part);
                is_end = true;
            }
            else
            {
                root_parts.push_back(part);
            }
        }

        return std::make_tuple(match_parts, AMPath::realpath(root_parts, "\\"), is_recursive);
    }

    void search(std::vector<std::string> &results, fs::path root, std::string name, std::vector<std::string> remains, AMPathTools::ENUMS::SearchType type, bool use_regex, bool silence, CB callback, bool star_matched = false)
    {
        if (!remains.empty())
        {
            if (!fs::is_directory(root))
            {

                return;
            }
            try
            {
                fs::path cur_path;
                std::string cur_name;
                std::string next_name = remains.front();
                bool is_dir;
                bool is_match;
                bool next_match;
                for (auto &entry : fs::directory_iterator(root))
                {

                    cur_name = entry.path().filename().string();
                    cur_path = AMPath::join(root, cur_name);
                    is_dir = fs::is_directory(cur_path);

                    if (name == "**")
                    {
                        if (is_dir)
                        {
                            // use ** to match current dir
                            search(results, cur_path, name, remains, type, use_regex, silence, callback, true);
                        }
                        next_match = AMPathTools::_match(cur_name, next_name, use_regex);
                        if (next_match && star_matched)
                        {
                            if (remains.size() == 1)
                            {
                                if (is_dir && type != AMPathTools::ENUMS::SearchType::File)
                                {
                                    results.push_back(cur_path.string());
                                }
                                else if (!is_dir && type != AMPathTools::ENUMS::SearchType::Directory)
                                {
                                    results.push_back(cur_path.string());
                                }
                            }
                            else
                            {
                                auto new_remains = remains;
                                new_remains.erase(new_remains.begin()); // erase the next_match name
                                auto new_name = new_remains.front();
                                new_remains.erase(new_remains.begin()); // move the new name to name
                                search(results, cur_path, new_name, new_remains, type, use_regex, silence, callback, false);
                            }
                        }
                        continue;
                    }

                    is_match = AMPathTools::_match(cur_name, name, use_regex);
                    if (is_match && is_dir)
                    {
                        auto new_remains = remains;
                        new_remains.erase(new_remains.begin());
                        search(results, cur_path, next_name, new_remains, type, use_regex, silence, callback, false);
                    }
                }
            }
            catch (const std::filesystem::filesystem_error &e)
            {
                if (!silence && callback)
                {
                    const std::error_code ec = e.code();
                    (*callback)(root.string(), "IterDirError", fmt::format("Listdir \"{}\" encounters {}", root.string(), ec.message()));
                }
            }
            catch (const std::exception &e)
            {
                if (!silence && callback)
                {
                    (*callback)(root.string(), "IterDirError", fmt::format("Listdir \"{}\" encounters {}", root.string(), e.what()));
                }
            }
        }
        else
        {
            if (!fs::is_directory(root))
            {
                return;
            }

            if (name == "**")
            {
                fs::path cur_path;
                std::string cur_name;
                bool is_dir;
                for (auto &entry : fs::directory_iterator(root))
                {
                    cur_name = entry.path().filename().string();
                    cur_path = AMPath::join(root, cur_name);
                    is_dir = fs::is_directory(cur_path);
                    if (!is_dir)
                    {
                        if (type != AMPathTools::ENUMS::SearchType::Directory)
                        {
                            results.push_back(cur_path.string());
                        }
                        continue;
                    }

                    if (type != AMPathTools::ENUMS::SearchType::File)
                    {
                        results.push_back(cur_path.string());
                        continue;
                    }
                    else
                    {
                        if (fs::is_empty(cur_path))
                        {
                            continue;
                        }
                        else
                        {
                            search(results, cur_path, "**", {}, type, use_regex, silence, callback);
                        }
                    }
                }
                return;
            }

            bool is_dir2;
            std::string cur_name;
            fs::path cur_path;
            try
            {
                for (auto &entry : fs::directory_iterator(root))
                {
                    cur_name = entry.path().filename().string();
                    if (!AMPathTools::_match(cur_name, name, use_regex))
                    {
                        continue;
                    }
                    cur_path = AMPath::join(root, cur_name);
                    amprint("cur_path", cur_path);
                    is_dir2 = fs::is_directory(cur_path);
                    if (is_dir2 && type != AMPathTools::ENUMS::SearchType::File)
                    {
                        results.push_back(cur_path.string());
                    }
                    else if (!is_dir2 && type != AMPathTools::ENUMS::SearchType::Directory)
                    {
                        results.push_back(cur_path.string());
                    }
                }
            }
            catch (const std::filesystem::filesystem_error &e)
            {
                if (!silence && callback)
                {
                    const std::error_code ec = e.code();
                    (*callback)(root.string(), "IterDirError", fmt::format("Listdir \"{}\" encounters {}", root.string(), ec.message()));
                }
            }
            catch (const std::exception &e)
            {
                if (!silence && callback)
                {
                    (*callback)(root.string(), "IterDirError", fmt::format("Listdir \"{}\" encounters {}", root.string(), e.what()));
                }
            }
        }
    }

    std::vector<std::string> find(const std::string &path_f, bool &is_recursive, AMPathTools::ENUMS::SearchType type = AMPathTools::ENUMS::SearchType::All, bool use_regex = false, bool silence = false, CB callback = nullptr)
    {
        std::string path_n = AMPath::Strip(path_f);
        if (path_n.empty())
        {
            if (callback)
            {
                (*callback)(path_f, "InvalidArgument", "Recive empty src path");
            }
            return {};
        }
        auto pre_result = preprocess(path_n, use_regex);
        auto match_parts = std::get<0>(pre_result);
        auto root_path = std::get<1>(pre_result);
        is_recursive = std::get<2>(pre_result);
        std::vector<std::string> results;

        if (root_path.empty())
        {
            if (callback)
            {
                (*callback)(path_f, "FailToParsingRoot", "Parsed Root is Empty");
            }
            return {};
        }

        if (!fs::exists(root_path))
        {
            return {};
        }

        if (match_parts.empty())
        {
            return std::vector<std::string>{root_path};
        }

        if (use_regex)
        {
            auto reg_check = AMPathTools::isPatternsValid(match_parts);
            if (std::holds_alternative<std::string>(reg_check))
            {
                if (callback)
                {
                    (*callback)(path_f, "RegexSytanxError", std::get<std::string>(reg_check));
                }
                return {};
            }
        }

        std::string name_first = match_parts.front();
        match_parts.erase(match_parts.begin());

        if (root_path.find("\\") == std::string::npos && root_path.find("/") == std::string::npos)
        {
            root_path = root_path + "\\";
        }

        AMPath::search(results, fs::path(root_path), name_first, match_parts, type, use_regex, silence, callback);

        return AMPathTools::UniqueVector(results);
    }
}
