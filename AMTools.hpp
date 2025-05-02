#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

namespace _AMInternalTools
{
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
}

template <typename... Args>
void amprint(Args &&...args)
{
    std::string out;
    auto process_arg = [&](auto &&arg)
    {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::filesystem::path>)
        {
            if (!arg.empty())
            {
                out = out + arg.string() + " ";
            }
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            if (!arg.empty())
            {
                out = out + arg + " ";
            }
        }
        else if constexpr (std::is_same_v<T, std::vector<std::string>>)
        {
            for (auto &seg : arg)
            {
                if (!seg.empty())
                {
                    out = out + seg + " ";
                }
            }
        }
        else if constexpr (std::is_same_v<T, const char *>)
        {
            std::string s = std::string(arg);
            if (!s.empty())
            {
                out = out + s + " ";
                ;
            }
        }
        else if constexpr (std::is_same_v<T, std::wstring>)
        {
            std::string s = _AMInternalTools::AMstr(arg);
            if (!s.empty())
            {
                out = out + s + " ";
            }
        }
        else
        {
            try
            {
                std::string s = fmt::format("{}", arg);
                out = out + s + " ";
            }
            catch (std::exception)
            {
            }
        }
    };

    (process_arg(std::forward<Args>(args)), ...);
    std::cout << out << std::endl;
}
