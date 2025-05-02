#include "AMTools.hpp"
#include <AMPath.hpp>
#include <CLI11.hpp>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <iostream>
#include <magic_enum/magic_enum.hpp>
#include <map>
#include <regex>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string>
#include <wil/com.h>
#include <windows.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32")
#pragma comment(lib, "oleaut32")
#pragma comment(lib, "uuid")
#pragma comment(lib, "shcore")
#pragma comment(lib, "fmt")
#pragma comment(lib, "Advapi32")

constexpr char *AMVERSION = "1.0";

constexpr char *AMCRITICAL = "CRITICAL";
constexpr char *AMERROR = "ERROR";
constexpr char *AMWARNING = "WARNING";
constexpr char *AMDEBUG = "DEBUG";
constexpr char *AMINFO = "INFO";

constexpr char *AMEND = "\033[0m";
constexpr char *AMRED = "\033[38;2;235;26;68m";
constexpr char *AMYELLOW = "\033[38;2;255;212;96m";
constexpr char *AMGREY = "\033[38;2;150;150;147m";
constexpr char *AMBLUE = "\033[38;2;44;196;203m";

constexpr char *AMSTOREENV = "AMIO_TEMPLETE_STORE";

constexpr int AMPYTRACEERROR = -198;
namespace fs = std::filesystem;

enum class FileOperationType
{
    COPY = 1,
    MOVE = 2,
    REMOVE = 3,
    RENAME = 4,
};

enum class FileOperationResult
{
    SUCCESS = 0,
    DstAlreadyExists = -1,
    PathNotExists = -2,
    NoOperationInstance = -3,
    FaileToCreOperationInstance = -4,
    FailToSetOperationFlags = -5,
    FailToCreSrcShellItem = -6,
    FailToCreDstShellItem = -7,
    FailToAddOperation = -8,
    FailToPerformOperation = -9,
    WrongOperationType = -10,
    FailToConfig = -11,
    UpperDirNotExists = -12,
    DstIsNotDir = -13,
    DstIsNotExists = -14,
    UnknownError = -15,
    FailToInitCOM = -16,
    NoIFileOperationInstance = -17,
    FailToCreateIFileOperationInstance = -18,
    OperationAborted = -19,
    COMInitFailed = -20,
    PyTraceError = -21,
    FailToCreateDir = -22,
    InvalidArgument = -23,
};

enum class FileOperationStatus
{
    Perfect = 0,
    PartialSuccess = 1,
    NoOperation = 2,
    Aborted = 3,
    FinalError = -1,
    AllErrors = -2,
    Uninitialized = -3,

};

std::string GetECName(FileOperationResult error_code)
{
    return std::string(magic_enum::enum_name(error_code));
}

struct FileOperationSet
{
    bool NoProgressUI;
    bool AlwaysYes;
    bool NoErrorUI;
    bool NoMkdirInfo;
    bool DeleteWarning;
    bool RenameOnCollision;
    bool AllowAdmin;
    bool AllowUndo;
    bool Hardlink;
    bool ToRecycleBin;
    FileOperationSet()
        : NoProgressUI(false),
          AlwaysYes(false),
          NoErrorUI(false),
          NoMkdirInfo(true),
          DeleteWarning(false),
          RenameOnCollision(false),
          AllowAdmin(true),
          AllowUndo(true),
          Hardlink(false),
          ToRecycleBin(true)
    {
    }

    FileOperationSet(
        bool DeleteWarning,
        bool NoProgressUI = false,
        bool AlwaysYes = false,
        bool NoErrorUI = false,
        bool NoMkdirInfo = true,
        bool RenameOnCollision = false,
        bool AllowAdmin = true,
        bool AllowUndo = true,
        bool Hardlink = false,
        bool ToRecycleBin = false)
        : NoProgressUI(NoProgressUI),
          AlwaysYes(AlwaysYes),
          NoErrorUI(NoErrorUI),
          NoMkdirInfo(NoMkdirInfo),
          DeleteWarning(DeleteWarning),
          RenameOnCollision(RenameOnCollision),
          AllowAdmin(AllowAdmin),
          AllowUndo(AllowUndo),
          Hardlink(Hardlink),
          ToRecycleBin(ToRecycleBin)
    {
    }
};

using FOR = FileOperationResult;
using ECM = std::pair<FOR, std::string>;
using PECM = std::pair<std::string, ECM>;
using TOR = std::pair<FileOperationStatus, std::vector<PECM>>;
using sptr = std::shared_ptr<FileOperationSet>;

struct SingleFileOperation
{
    FileOperationType action;
    std::string src;
    std::string dst_dir;
    std::string dst_name;
    bool mkdir;
    SingleFileOperation(FileOperationType action, std::string src, std::string dst_dir, std::string dst_name, bool mkdir)
        : action(action), src(src), dst_dir(dst_dir), dst_name(dst_name), mkdir(mkdir)
    {
    }
};

class ExplorerAPI
{
private:
    IFileOperation *pFileOp;
    FOR status;
    std::string g_error_msg = "";
    FileOperationSet settings;

    bool IsFileNameValid(const std::string &name)
    {
        std::regex illegal_chars("[\\/:*?\"<>|]");
        return !std::regex_search(name, illegal_chars);
    }

    void trace(std::string level, FOR error_code, std::string target, std::string action, std::string message)
    {
    }

    std::string GetErrorMsg(HRESULT hr)
    {
        LPWSTR errorMessage = nullptr;
        std::string msg;
        DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                      FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS;
        DWORD dwError = HRESULT_CODE(hr);
        FormatMessageW(
            flags,
            nullptr,
            dwError,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPWSTR)&errorMessage,
            0,
            nullptr);
        if (errorMessage == nullptr)
        {
            msg = "";
            goto end;
        }
        msg = AMPathTools::AMstr(errorMessage);

    end:
        LocalFree(errorMessage);
        if (msg.empty())
        {
            return "Unknown Error";
        }
        return msg;
    }

    DWORD GetFlags(FileOperationSet settings)
    {
        DWORD flags = 0;
        if (settings.NoProgressUI)
            flags |= FOF_SILENT;
        if (settings.AlwaysYes)
            flags |= FOF_NOCONFIRMATION;
        if (settings.NoErrorUI)
            flags |= FOF_NOERRORUI;
        if (settings.NoMkdirInfo)
            flags |= FOF_NOCONFIRMMKDIR;
        if (settings.DeleteWarning)
            flags |= FOF_WANTNUKEWARNING;
        if (settings.RenameOnCollision)
            flags |= FOF_RENAMEONCOLLISION;
        if (settings.AllowAdmin)
            flags |= FOFX_SHOWELEVATIONPROMPT;
        if (settings.AllowUndo)
            flags |= FOFX_ADDUNDORECORD;
        if (settings.Hardlink)
            flags |= FOFX_PREFERHARDLINK;
        if (settings.ToRecycleBin)
            flags |= FOFX_RECYCLEONDELETE;
        return flags;
    }

    ECM Base1OP(FileOperationType action, std::string src, std::string dst_dir, std::string dst_name, bool mkdir, sptr tmp_set = nullptr)
    {
        ECM ecm = PendOperation(action, src, dst_dir, dst_name, mkdir);
        if (ecm.first != FOR::SUCCESS)
        {
            return ecm;
        }
        HRESULT hr;

        if (tmp_set)
        {
            FileOperationSet ori_set = settings;
            Config(*tmp_set);
            hr = pFileOp->PerformOperations();
            Config(ori_set);
        }
        else
        {
            hr = pFileOp->PerformOperations();
        }
        if (FAILED(hr))
        {
            return ECM(FOR::FailToPerformOperation, GetErrorMsg(hr));
        }
        return ECM(FOR::SUCCESS, "");
    }

    TOR BaseMultiOP(std::vector<SingleFileOperation> &operations, sptr tmp_set = nullptr)
    {
        if (!pFileOp)
        {
            return {FileOperationStatus::Uninitialized, {PECM("", ECM(FOR::NoIFileOperationInstance, "No IFileOperation instance"))}};
        }
        if (operations.empty())
        {
            return {FileOperationStatus::NoOperation, {PECM("", ECM(FOR::InvalidArgument, "No operations"))}};
        }
        std::vector<PECM> results;
        std::string msg;
        bool no_task = true;
        for (auto operation : operations)
        {
            ECM ecm = PendOperation(operation);
            if (ecm.first != FOR::SUCCESS)
            {
                results.emplace_back(PECM(operation.src, ecm));
            }
            no_task = false;
        }
        if (no_task)
        {
            return {FileOperationStatus::AllErrors, results};
        }
        HRESULT hr;
        if (tmp_set)
        {
            FileOperationSet ori_set = settings;
            Config(*tmp_set);
            hr = pFileOp->PerformOperations();
            Config(ori_set);
        }
        else
        {
            hr = pFileOp->PerformOperations();
        }
        if (FAILED(hr))
        {
            hr = hr & 0xFFFFFFFF;

            if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) || hr == COPYENGINE_E_USER_CANCELLED)
            {
                results.push_back(PECM("", ECM(FOR::OperationAborted, "User Aborted the operation")));
                return TOR{FileOperationStatus::Aborted, results};
            }
            msg = GetErrorMsg(hr);
            this->trace(AMERROR, FOR::FailToPerformOperation, "ExplorerAPI", "PerformOperations", msg);
            results.push_back(PECM("", ECM(FOR::FailToPerformOperation, msg)));
            return TOR{FileOperationStatus::FinalError, results};
        }
        return {results.empty() ? FileOperationStatus::Perfect : FileOperationStatus::PartialSuccess, results};
    }

public:
    ExplorerAPI()
    {
    }

    ~ExplorerAPI()
    {
        if (pFileOp != nullptr)
        {
            pFileOp->Release();
            pFileOp = nullptr;
        }
        CoUninitialize();
    }

    FileOperationSet GetSettings()
    {
        return settings;
    }

    ECM Config(FileOperationSet set)
    {
        if (pFileOp == nullptr)
        {
            return ECM(status, g_error_msg);
        }
        DWORD flags = GetFlags(set);
        HRESULT hr = pFileOp->SetOperationFlags(flags);
        if (FAILED(hr))
        {
            status = FOR::FailToConfig;
            g_error_msg = fmt::format("Failed to set operation flags: {}", GetErrorMsg(hr));
            this->trace(AMCRITICAL, FOR::FailToConfig, "ExplorerAPI", "SetOperationFlags", g_error_msg);
            return ECM(FOR::FailToConfig, g_error_msg);
        }
        this->settings = set;
        status = FOR::SUCCESS;
        g_error_msg = "";
        return ECM(FOR::SUCCESS, "");
    }

    ECM Init(FileOperationSet set)
    {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(hr))
        {
            g_error_msg = fmt::format("Failed to create IFileOperation instance: {}", GetErrorMsg(hr));
            this->trace(AMCRITICAL, FOR::FailToCreateIFileOperationInstance, "COMInstance", "CoCreateInstance", g_error_msg);
            return {FOR::FailToInitCOM, g_error_msg};
        }
        SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE); // 设置 DPI 感知

        hr = CoCreateInstance(
            CLSID_FileOperation, // CLSID_FileOperation 是 IFileOperation 的类标识符
            nullptr,
            CLSCTX_ALL,            // 在所有上下文中创建
            IID_PPV_ARGS(&pFileOp) // 获取 IFileOperation 接口
        );

        if (FAILED(hr))
        {
            g_error_msg = fmt::format("Failed to create IFileOperation instance: {}", GetErrorMsg(hr));
            this->trace(AMCRITICAL, FOR::FailToCreateIFileOperationInstance, "COMInstance", "CoCreateInstance", g_error_msg);
            return ECM(FOR::FailToCreateIFileOperationInstance, g_error_msg);
        }
        if (!pFileOp)
        {
            g_error_msg = "Failed to create IFileOperation instance";
            this->trace(AMCRITICAL, FOR::NoIFileOperationInstance, "COMInstance", "CoCreateInstance", g_error_msg);
            return ECM(FOR::NoIFileOperationInstance, g_error_msg);
        }
        status = FOR::SUCCESS;
        g_error_msg = "";

        return Config(set);
    }

    ECM PendOperation(SingleFileOperation &operation)
    {
        return PendOperation(operation.action, operation.src, operation.dst_dir, operation.dst_name, operation.mkdir);
    }

    ECM PendOperation(FileOperationType action, std::string src, std::string dst_dir, std::string dst_name, bool mkdir)
    {
        std::string srcf = AMPath::realpath(src, false, "\\");
        if (!std::filesystem::exists(srcf))
        {
            this->trace(AMWARNING, FOR::PathNotExists, src, "CheckArguments", "Source path does not exist");
            return ECM(FOR::PathNotExists, fmt::format("Source path does not exist -> {}", srcf));
        }

        std::string dstf;
        if (dst_dir.empty())
        {
            if (action != FileOperationType::REMOVE && action != FileOperationType::RENAME)
            {
                this->trace(AMWARNING, FOR::InvalidArgument, src, "CheckArguments", "Destination name is empty and operation is not remove");
                return ECM(FOR::InvalidArgument, "Destination Director is empty");
            }
        }
        else
        {
            dstf = AMPath::realpath(dst_dir, false, "\\");
            if (!std::filesystem::exists(dstf))
            {
                if (!mkdir)
                {
                    this->trace(AMWARNING, FOR::PathNotExists, dst_dir, "CheckArguments", "Destination path does not exist");
                    return ECM(FOR::PathNotExists, fmt::format("Dst Directory does not exist -> {}", dstf));
                }
                else
                {
                    try
                    {
                        std::filesystem::create_directories(dstf);
                    }
                    catch (const std::exception &e)
                    {
                        return ECM(FOR::FailToCreateDir, fmt::format("Mkdir \"{}\" encounters {}", dstf, e.what()));
                    }
                }
            }
            else if (!std::filesystem::is_directory(dstf))
            {
                return ECM(FOR::DstIsNotDir, fmt::format("Dst is not a dir -> {}", dstf));
            }
        }
        std::wstring src_wstr;
        std::wstring dst_dir_wstr;
        std::string msg;
        HRESULT hr;
        if (action == FileOperationType::REMOVE || action == FileOperationType::RENAME)
        {
            src_wstr = AMPathTools::AMstr(srcf);
            wil::com_ptr<IShellItem> pItem;
            HRESULT hr = SHCreateItemFromParsingName(src_wstr.c_str(), nullptr, IID_PPV_ARGS(&pItem));
            if (FAILED(hr))
            {
                msg = fmt::format("Cre shell item encounters:{} -> {}", GetErrorMsg(hr), src);
                this->trace(AMERROR, FOR::FailToCreSrcShellItem, src, "PerformOperations", msg);
                return ECM(FOR::FailToCreSrcShellItem, msg);
            }

            if (action == FileOperationType::REMOVE)
            {
                hr = pFileOp->DeleteItem(pItem.get(), nullptr);
                if (FAILED(hr))
                {
                    msg = fmt::format("Failed to add Remove operation: {}", GetErrorMsg(hr));
                    this->trace(AMERROR, FOR::FailToAddOperation, src, "AddOperation", msg);
                    return ECM(FOR::FailToAddOperation, msg);
                }
                return ECM(FOR::SUCCESS, "");
            }
            else if (action == FileOperationType::RENAME)
            {
                if (!IsFileNameValid(dst_name))
                {
                    this->trace(AMWARNING, FOR::InvalidArgument, src, "CheckArguments", "Destination name contains invalid characters");
                    return ECM(FOR::InvalidArgument, "Destination name contains invalid characters");
                }
                if (dst_name.empty())
                {
                    this->trace(AMWARNING, FOR::InvalidArgument, src, "CheckArguments", "Destination name is empty");
                    return ECM(FOR::InvalidArgument, "Destination name is empty");
                }
                std::wstring dst_name_wstr = AMPathTools::AMstr(dst_name);
                hr = pFileOp->RenameItem(pItem.get(), dst_name_wstr.c_str(), nullptr);
                if (FAILED(hr))
                {
                    msg = fmt::format("Failed to add Rename operation: {}", GetErrorMsg(hr));
                    this->trace(AMERROR, FOR::FailToAddOperation, src, "AddOperation", msg);
                    return ECM(FOR::FailToAddOperation, msg);
                }
                return ECM(FOR::SUCCESS, "");
            }
            else
            {
                return ECM(FOR::InvalidArgument, fmt::format("Unsupported Operation Type: {}", magic_enum::enum_name(action)));
            }
        }
        else
        {
            src_wstr = AMPathTools::AMstr(srcf);
            dst_dir_wstr = AMPathTools::AMstr(dstf);
            if (!IsFileNameValid(dst_name))
            {
                this->trace(AMWARNING, FOR::InvalidArgument, src, "CheckArguments", "Destination name contains invalid characters");
                return ECM(FOR::InvalidArgument, fmt::format("Destination name contains invalid characters -> {}", dst_name));
            }
            if (dst_name.empty() && action == FileOperationType::RENAME)
            {
                this->trace(AMWARNING, FOR::InvalidArgument, src, "CheckArguments", "Destination name is empty and operation is rename");
                return ECM(FOR::InvalidArgument, "Rename Action recieves empty New name");
            }
            std::wstring dst_name_wstr = dst_name.empty() ? L"" : AMPathTools::AMstr(dst_name);
            wil::com_ptr<IShellItem> pSrcItem;
            hr = SHCreateItemFromParsingName(src_wstr.c_str(), nullptr, IID_PPV_ARGS(&pSrcItem));
            if (FAILED(hr))
            {
                msg = fmt::format("Cre shell item encounters:{} -> {}", GetErrorMsg(hr), src);
                this->trace(AMERROR, FOR::FailToCreSrcShellItem, src, "PerformOperations", msg);
                return ECM(FOR::FailToCreSrcShellItem, msg);
            }
            wil::com_ptr<IShellItem> pDstItem;
            hr = SHCreateItemFromParsingName(dst_dir_wstr.c_str(), nullptr, IID_PPV_ARGS(&pDstItem));
            if (FAILED(hr))
            {
                msg = fmt::format("Cre shell item encounters:{} -> {}", GetErrorMsg(hr), src);
                this->trace(AMERROR, FOR::FailToCreDstShellItem, dst_dir, "PerformOperations", msg);
                return ECM(FOR::FailToCreDstShellItem, msg);
            }

            if (action == FileOperationType::MOVE)
            {
                hr = pFileOp->MoveItem(pSrcItem.get(), pDstItem.get(), dst_name_wstr.c_str(), nullptr);
                if (FAILED(hr))
                {
                    msg = fmt::format("Failed to add Move operation: {}", GetErrorMsg(hr));
                    this->trace(AMERROR, FOR::FailToAddOperation, src, "AddOperation", msg);
                    return ECM(FOR::FailToAddOperation, msg);
                }
            }
            else if (action == FileOperationType::COPY)
            {
                hr = pFileOp->CopyItem(pSrcItem.get(), pDstItem.get(), dst_name_wstr.c_str(), nullptr);
                if (FAILED(hr))
                {
                    msg = fmt::format("Failed to add Copy operation: {}", GetErrorMsg(hr));
                    this->trace(AMERROR, FOR::FailToAddOperation, src, "AddOperation", msg);
                    return ECM(FOR::FailToAddOperation, msg);
                }
            }

            return ECM(FOR::SUCCESS, "");
        }
    }

    ECM Copy(std::string src, std::string dst_dir, bool mkdir = true, sptr tmp_set = nullptr)
    {
        return Base1OP(FileOperationType::COPY, src, dst_dir, "", mkdir, tmp_set);
    }

    TOR Copy(std::vector<std::string> &srcs, std::string dst, bool mkdir = true, sptr tmp_set = nullptr)
    {
        std::vector<SingleFileOperation> operations;
        for (auto src : srcs)
        {
            operations.emplace_back(SingleFileOperation(FileOperationType::COPY, src, dst, "", mkdir));
        }
        return BaseMultiOP(operations);
    }

    ECM Clone(std::string src, std::string dst, bool mkdir = true, sptr tmp_set = nullptr)
    {
        dst = AMPath::realpath(dst, false, "\\");
        std::string dst_dir = fs::path(dst).parent_path().string();
        std::string dst_name = fs::path(dst).filename().string();
        return Base1OP(FileOperationType::COPY, src, dst_dir, dst_name, mkdir, tmp_set);
    }

    TOR Clone(std::map<std::string, std::string> &srcs_dst, bool mkdir = true, sptr tmp_set = nullptr)
    {
        std::vector<SingleFileOperation> operations;
        std::string src_path;
        std::string dst_dir;
        std::string dst_name;
        for (auto [src, dst] : srcs_dst)
        {
            src_path = AMPath::realpath(src, false, "\\");
            dst_dir = fs::path(dst).parent_path().string();
            dst_name = fs::path(dst).filename().string();
            operations.emplace_back(SingleFileOperation(FileOperationType::COPY, src_path, dst_dir, dst_name, mkdir));
        }
        return BaseMultiOP(operations);
    }

    ECM Move(std::string src, std::string dst_dir, bool mkdir = true, sptr tmp_set = nullptr)
    {
        return Base1OP(FileOperationType::MOVE, src, dst_dir, "", mkdir, tmp_set);
    }

    TOR Move(std::vector<std::string> &srcs, std::string dst, bool mkdir = true, sptr tmp_set = nullptr)
    {
        std::vector<SingleFileOperation> operations;
        for (auto src : srcs)
        {
            operations.emplace_back(SingleFileOperation(FileOperationType::MOVE, src, dst, "", mkdir));
        }
        return BaseMultiOP(operations);
    }

    ECM Remove(std::string path, sptr tmp_set = nullptr)
    {
        return Base1OP(FileOperationType::REMOVE, path, "", "", false, tmp_set);
    }

    TOR Remove(std::vector<std::string> &paths, sptr tmp_set = nullptr)
    {
        std::vector<SingleFileOperation> operations;
        for (auto path : paths)
        {
            operations.emplace_back(SingleFileOperation(FileOperationType::REMOVE, path, "", "", false));
        }
        return BaseMultiOP(operations);
    }

    ECM Rename(std::string src, std::string new_name, sptr tmp_set = nullptr)
    {
        return Base1OP(FileOperationType::RENAME, src, src, new_name, false, tmp_set);
    }

    TOR Rename(std::map<std::string, std::string> &srcs_new_names, sptr tmp_set = nullptr)
    {
        std::vector<SingleFileOperation> operations;
        for (auto [src, new_name] : srcs_new_names)
        {
            operations.emplace_back(SingleFileOperation(FileOperationType::RENAME, src, src, new_name, false));
        }
        return BaseMultiOP(operations);
    }

    ECM Replace(std::string src, std::string dst, sptr tmp_set = nullptr)
    {
        std::string dst_dir = fs::path(AMPath::realpath(dst, false, "\\")).parent_path().string();
        std::string dst_name = fs::path(AMPath::realpath(dst, false, "\\")).filename().string();
        return Base1OP(FileOperationType::MOVE, src, dst_dir, dst_name, true, tmp_set);
    }

    TOR Replace(std::map<std::string, std::string> &srcs_dsts, sptr tmp_set = nullptr)
    {
        std::vector<SingleFileOperation> operations;
        for (auto [src, dst] : srcs_dsts)
        {
            std::string dst_dir = fs::path(AMPath::realpath(dst, false, "\\")).parent_path().string();
            std::string dst_name = fs::path(AMPath::realpath(dst, false, "\\")).filename().string();
            operations.emplace_back(SingleFileOperation(FileOperationType::MOVE, src, dst_dir, dst_name, true));
        }
        return BaseMultiOP(operations);
    }

    ECM Conduct(FileOperationType action, std::string src, std::string dst_dir = "", std::string dst_name = "", bool mkdir = false, sptr tmp_set = nullptr)
    {
        return Base1OP(action, src, dst_dir, dst_name, mkdir, tmp_set);
    }

    ECM Conduct(SingleFileOperation &operation, sptr tmp_set = nullptr)
    {
        return Base1OP(operation.action, operation.src, operation.dst_dir, operation.dst_name, operation.mkdir, tmp_set);
    }

    TOR Conduct(std::vector<SingleFileOperation> &operations, sptr tmp_set = nullptr)
    {
        return BaseMultiOP(operations, tmp_set);
    }

    ECM Conduct(sptr tmp_set = nullptr)
    {
        HRESULT hr;
        if (tmp_set)
        {
            FileOperationSet ori_set = settings;
            Config(*tmp_set);
            hr = pFileOp->PerformOperations();
            Config(ori_set);
        }
        else
        {
            hr = pFileOp->PerformOperations();
        }

        if (FAILED(hr))
        {
            hr = hr & 0xFFFFFFFF;

            if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) || hr == COPYENGINE_E_USER_CANCELLED)
            {
                return ECM(FOR::OperationAborted, "User Aborted the operation");
            }
            return ECM(FOR::FailToPerformOperation, GetErrorMsg(hr));
        }
        return ECM(FOR::SUCCESS, "");
    }
};

namespace CliPara
{
    const std::unordered_map<std::string, std::vector<char>> AvailableFuntions =
        {{"cp", {'r', 'm', 'f', 'q'}},
         {"cl", {'f', 'm', 'q'}},
         {"mv", {'r', 'm', 'q'}},
         {"rp", {'f', 'm', 'q'}},
         {"rn", {'f', 'q'}},
         {"rm", {'p', 'r', 'q'}},
         {"new", {'m', 'f', 'q'}}};

    // std::pair<std::string, std::vector<char>> ParsingArg(int argc, char **argv)
    // {
    //     std::string func = "";
    //     std::vector<char> options{};
    //     std::vector<std::string> position_args{};
    // }

    struct Options
    {
        bool only_file;
        bool only_dir;
        bool force;
        bool quiet;
        bool regex;
        bool mkdir;
        bool permanent;
        bool newname;
        bool is_recursive = false;
        AMPathTools::ENUMS::SearchType srh;
        FileOperationSet set;
        std::shared_ptr<std::function<void(std::string, std::string, std::string, std::string)>> cb_ptr;
        Options(bool only_file, bool only_dir, bool force, bool quiet, bool regex, bool mkdir, bool permanent, bool newname) : only_file(only_file), only_dir(only_dir), force(force), quiet(quiet), regex(regex), mkdir(mkdir), permanent(permanent), newname(newname)
        {
            if (quiet)
            {
                this->set.NoErrorUI = true;
                this->set.NoProgressUI = true;
                this->set.DeleteWarning = false;
            }
            if (permanent)
            {
                this->set.ToRecycleBin = false;
            }
            if (newname)
            {
                this->set.RenameOnCollision = true;
            }
            if (force)
            {
                this->set.AlwaysYes = true;
            }

            if (only_file && !only_dir)
            {
                this->srh = AMPathTools::ENUMS::SearchType::File;
            }
            else if (!only_file && only_dir)
            {
                this->srh = AMPathTools::ENUMS::SearchType::Directory;
            }
            else
            {
                this->srh = AMPathTools::ENUMS::SearchType::All;
            }
        }

        void MsgRecord(std::string src, std::string error_name, std::string error_msg) {}
    };

    enum class FuntionType
    {
        Unknown = 0,
        COPY = 1,
        CLONE = 2,
        MOVE = 3,
        REPLACE = 4,
        REMOVE = 5,
        NEW = 6,
        RENAME = 7,
    };
}

namespace CliFunc
{
    void OriCallback(std::string src, std::string error, std::string msg)
    {
        amprint(fmt::format("{}⚠️{}{}: {}{}{}", AMYELLOW, error, AMEND, AMGREY, msg, AMEND));
    }

    bool IsUseMatch(const std::string &path, bool use_regex)
    {
        if (use_regex)
        {
            return path.find("*") != std::string::npos || path.find("<") != std::string::npos;
        }
        else
        {
            return path.find("*") != std::string::npos;
        }
    }
    void CopyMove(FileOperationType oper, std::vector<std::string> &srcs, std::string dst, CliPara::Options &opt, std::vector<SingleFileOperation> &tasks, std::shared_ptr<std::function<void(std::string, std::string, std::string)>> cb)
    {
        dst = AMPath::realpath(dst);
        if (!fs::exists(dst))
        {
            if (!opt.mkdir)
            {
                amprint(fmt::format("❌{}PathNotExists{}: Unexisting Dst Dir -> {}", AMRED, AMEND, dst));
                exit(static_cast<int>(FileOperationResult::PathNotExists));
            }
            else
            {
                try
                {
                    fs::create_directories(dst);
                }
                catch (const std::filesystem::filesystem_error &e)
                {
                    auto ec = e.code();
                    amprint(fmt::format("❌MkdirError: cre dir encounters:{} -> {}", ec.message(), dst));
                }
                catch (const std::exception &e)
                {
                    amprint(fmt::format("❌MkdirError: cre dir encounters:{} -> {}", e.what(), dst));
                }
            }
        }
        else
        {
            if (!fs::is_directory(dst))
            {
                amprint(fmt::format("❌{}DstIsNotDir{}: Dst is not a dir -> {}", AMRED, AMEND, dst));
                exit(static_cast<int>(FileOperationResult::DstIsNotDir));
            }
        }
        std::vector<std::string> search_res;
        for (auto src : srcs)
        {
            if (IsUseMatch(src, opt.regex))
            {
                for (auto path : AMPath::find(src, opt.is_recursive, opt.srh, opt.regex, opt.quiet, cb))
                {
                    search_res.push_back(path);
                }
            }
            else
            {
                search_res.push_back(src);
            }
        }
        search_res = AMPathTools::UniqueVector(search_res);
        for (auto &item : search_res)
        {
            tasks.emplace_back(SingleFileOperation(oper, item, dst, "", opt.mkdir));
        }
    }

    void Remove(FileOperationType oper, std::vector<std::string> &paths, CliPara::Options &opt, std::vector<SingleFileOperation> &tasks, std::shared_ptr<std::function<void(std::string, std::string, std::string)>> cb)
    {
        std::vector<std::string> search_res;
        for (auto &src : paths)
        {
            for (auto &path : AMPath::find(src, opt.is_recursive, opt.srh, opt.regex, opt.quiet, cb))
            {
                search_res.push_back(path);
            }
        }
        search_res = AMPathTools::UniqueVector(search_res);
        for (auto &item : search_res)
        {
            tasks.emplace_back(SingleFileOperation(FileOperationType::REMOVE, item, "", "", opt.mkdir));
        }
    }

    void CheckStore(int &status, std::string &error_msg, std::string &path, std::unordered_map<std::string, std::string> &templetes)
    {

        char *templete_store_ptr = std::getenv(AMSTOREENV);
        if (!templete_store_ptr)
        {
            path = "";
            status = -1;
            error_msg = fmt::format("{}❌EnvVarNotSetError{}:  EnvVar not set -> ${}", AMRED, AMEND, AMSTOREENV);
            return;
        }
        else
        {
            path = std::string(templete_store_ptr);
        }

        if (path.empty())
        {
            status = -2;
            error_msg = fmt::format("{}❌ValueError{}: Empty Value -> ${}", AMRED, AMEND, AMSTOREENV);
            return;
        }
        else if (!fs::exists(path))
        {
            status = -3;
            error_msg = fmt::format("{}❌PathNotFound{}: Templete store path not exists -> {}", AMRED, AMEND, path);
            return;
        }
        else if (!fs::is_directory(path))
        {
            status = -4;
            error_msg = fmt::format("{}❌InvalidPath{}: Templete store is not a directory -> {}", AMRED, AMEND, path);
            return;
        }
        else if (fs::is_empty(path))
        {
            status = -5;
            error_msg = fmt::format("{}⚠️EmptyStore:{} Templete store is empty -> {}", AMYELLOW, AMEND, path);
            return;
        }
        for (auto path_i : fs::directory_iterator(path))
        {
            if (!fs::is_directory(path_i))
            {
                templetes[path_i.path().extension().string()] = path_i.path().string();
            }
        }
    }

    void New(std::vector<std::string> &paths, CliPara::Options &opt, std::vector<SingleFileOperation> &tasks)
    {
        int status;
        std::string msg;
        std::string path;
        std::unordered_map<std::string, std::string> templetes{};
        CliFunc::CheckStore(status, msg, path, templetes);
        if (status != 0)
        {
            std::cerr << AMRED << msg << AMEND << std::endl;
            exit(status);
        }
        std::string t_ext;
        for (auto path : paths)
        {
            path = AMPath::realpath(path, false, "\\");
            t_ext = AMPath::extension(path);
            if (templetes.find(t_ext) != templetes.end())
            {
                tasks.emplace_back(SingleFileOperation(FileOperationType::COPY, templetes[t_ext], AMPath::dirname(path), AMPath::basename(path), opt.mkdir));
            }
            else if (templetes.find(".txt") != templetes.end())
            {
                tasks.emplace_back(SingleFileOperation(FileOperationType::COPY, templetes[".txt"], AMPath::dirname(path), AMPath::basename(path), opt.mkdir));
            }
        }
    }

    void StoreView()
    {
        int status = 0;
        std::string msg = "";
        std::string path = "";
        std::unordered_map<std::string, std::string> templetes{};
        CliFunc::CheckStore(status, msg, path, templetes);
        std::cout << "Environment Variable Name: $" << AMSTOREENV << std::endl;
        if (status != -1)
        {
            std::cout << "Store Path: " << path << std::endl;
        }
        if (status != 0)
        {
            std::cerr << msg << std::endl;
            exit(status);
        }
        std::string avb_ext = "";
        if (!templetes.empty())
        {
            for (auto &i : templetes)
            {
                avb_ext += (i.first + ", ");
            }
            avb_ext = AMPath::Strip(avb_ext);
            avb_ext.pop_back();
            std::cout << "Available Formats: " << avb_ext;
            exit(0);
        }
        std::cout << "Available Formats: None" << std::endl;
        exit(0);
    }
}

std::string GetAPPName(const std::string &exc_path)
{
    if (AMPath::extension(exc_path) != ".exe")
    {
        return "AMIO";
    }
    else
    {
        std::string exc_name = AMPath::basename(exc_path);
        int pos = exc_name.find_last_of('.');
        if (pos != std::string::npos && pos > 0)
        {
            return exc_name.substr(0, pos);
        }
        else
        {
            return "AMIO";
        }
    }
}

int main(int argc, char **argv)
{
    try
    {
        UINT codepage = GetConsoleOutputCP();
        if (codepage != 65001)
        {
            std::cout << AMYELLOW << "Warning: Terminal not support UTF-8, some characters may be missing, use \"chcp 65001\" to enable it!" << AMEND << std::endl;
        }

        using task = SingleFileOperation;
        using op = FileOperationType;
        using EC = FileOperationResult;
        using CB = std::function<void(std::string, std::string, std::string)>;

        std::string name_f = AMPath::basename(argv[0]);

        CLI::App app{fmt::format("AM FileManager CLI\nAuthor: Vaccummer\nVersion: {}", AMVERSION), name_f};

        bool use_regex = false;
        bool mkdir = false;
        bool conflict_newname = false;
        bool force_overlap = false;
        bool quiet = false;
        bool permanent_delete = false;
        bool only_file = false;
        bool only_dir = false;

        std::vector<std::string> cp_srcs;
        std::string cp_dst;
        CLI::App *copy_cmd = app.add_subcommand("cp", "Copy paths to a certain directory");
        copy_cmd->add_option("Sources", cp_srcs, "Source Paths to be copied")
            ->expected(1, -1);

        copy_cmd->add_option("DstDir", cp_dst, "Destination Dir to paste path")
            ->expected(1);

        copy_cmd->add_flag("-m,--mkdir", mkdir, "Make dir when dst dir not exists");
        copy_cmd->add_flag("-f,--file", only_file, "Only Match Files when search path(Won't exclude exact directory path in input)");
        copy_cmd->add_flag("-d,--dir", only_file, "Only Match Directories when search path(Won't exclude exact directory path in input)");
        copy_cmd->add_flag("-o,--overlap", force_overlap, "Overlap path when dst path already exists");
        copy_cmd->add_flag("-n,--new", conflict_newname, "Create new name when dst path already exists ");
        copy_cmd->add_flag("-r,--regex", use_regex, "User regex to find paths, use <> to wrap your pattern");
        copy_cmd->add_flag("-q,--quiet", quiet, "No UI, auto cre new name when conflict");

        copy_cmd->callback([&]()
                           {
                               if (cp_srcs.size() < 2)
                               {
                                   throw CLI::ValidationError("Copy Function Need at least one source and one destination");
                               }

                               cp_dst = cp_srcs.back();
                               cp_srcs.pop_back(); });

        std::vector<std::string> sr_paths;
        std::string cl_src;
        std::string cl_dst;
        CLI::App *clone_cmd = app.add_subcommand("cl", "Clone src to dst");
        clone_cmd->add_option("Source", cl_src, "Source Path")
            ->required()
            ->expected(1);
        clone_cmd->add_option("Destination", cl_dst, "Full Path of Destination")
            ->required()
            ->expected(1);
        clone_cmd->add_flag("-m,--mkdir", mkdir, "Make dir when dst dir not exists");
        clone_cmd->add_flag("-o,--overlap", force_overlap, "Overlap path when dst path already exists");
        clone_cmd->add_flag("-n,--new", conflict_newname, "Create new name when dst path already exists");
        clone_cmd->add_flag("-q,--quiet", quiet, "No UI, auto cre new name when conflict");

        std::vector<std::string> mv_srcs;
        std::string mv_dst = "";
        CLI::App *move_cmd = app.add_subcommand("mv", "Move path to a certain directory");
        move_cmd->add_option("Sources", mv_srcs, "Source Paths to be moved")
            ->expected(1, -1);
        move_cmd->add_option("DstDir", mv_dst, "Destination Dir to paste path")
            ->expected(1);
        move_cmd->add_flag("-m,--mkdir", mkdir, "Make dir when dst dir not exists");
        move_cmd->add_flag("-f,--file", only_file, "Only Match Files when search path(Won't exclude exact directory path in input)");
        move_cmd->add_flag("-d,--dir", only_file, "Only Match Directories when search path(Won't exclude exact directory path in input)");
        move_cmd->add_flag("-o,--overlap", force_overlap, "Overlap path when dst path already exists");
        move_cmd->add_flag("-n,--new", conflict_newname, "Create new name when dst path already exists ");
        move_cmd->add_flag("-r,--regex", use_regex, "User regex to find paths, use <> to wrap your pattern");
        move_cmd->add_flag("-q,--quiet", quiet, "No UI, auto cre new name when conflict");
        move_cmd->callback([&]()
                           {
                               if (mv_srcs.size() < 2)
                               {
                                   throw CLI::ValidationError("Move Function Needs at least one source and one destination");
                               }
                               mv_dst = mv_srcs.back();
                               mv_srcs.pop_back(); });

        std::string mr_src;
        std::string mr_dst;
        CLI::App *replace_cmd = app.add_subcommand("mr", "Move and Replace");
        replace_cmd->add_option("Source", mr_src, "Source Path to be moved")
            ->required()
            ->expected(1);
        replace_cmd->add_option("Destination", mr_src, "Full Destination Path")
            ->required()
            ->expected(1);
        replace_cmd->add_flag("-m, --mkdir", mkdir, "Make dir when dst dir not exists");
        replace_cmd->add_flag("-o,--overlap", force_overlap, "Overlap path when dst path already exists");
        replace_cmd->add_flag("-n,--new", conflict_newname, "Create new name when dst path already exists");
        replace_cmd->add_flag("-q,--quiet", quiet, "No UI, auto cre new name when conflict");

        std::vector<std::string> rm_paths;
        CLI::App *remove_cmd = app.add_subcommand("rm", "Remove paths");
        remove_cmd->add_option("Paths", rm_paths, "Paths to be removed")
            ->expected(1, -1);
        remove_cmd->add_flag("-r,--regex", use_regex, "Use regex to find paths, use <> to wrap your pattern");
        remove_cmd->add_flag("-q,--quite", quiet, "Use regex to find paths, use <> to wrap your pattern");
        remove_cmd->add_flag("-p,--permanent", use_regex, "Directly delete path rather than move to Recycle Bin (But UNDO is still available)");

        std::string rn_src;
        std::string rn_dst_name;
        CLI::App *rename_cmd = app.add_subcommand("rn", "Rename path to a new name");
        rename_cmd->add_option("Source", rn_src, "Source Path you want to rename")
            ->required()
            ->expected(1);
        rename_cmd->add_option("Newname", rn_dst_name, "New name of the Path")
            ->required()
            ->expected(1);
        rename_cmd->add_flag("-o,--overlap", force_overlap, "Overlap path when dst path already exists");

        std::vector<std::string> new_paths;
        CLI::App *new_cmd = app.add_subcommand("new", "Create new file(s)");
        new_cmd->add_option("Destinations", new_paths, "The Files to be created")
            ->required();
        new_cmd->add_flag("-m,--mkdir", mkdir, "Make dir when dst dir not exists");
        new_cmd->add_flag("-q,--quiet", quiet, "No UI, auto cre new name when conflict");
        new_cmd->add_flag("-o,--overlap", force_overlap, "Overlap path when dst path already exists");

        CLI::App *amstore_cmd = app.add_subcommand("store", "View Store File Types");

        try
        {
            CLI11_PARSE(app, argc, argv);
        }
        catch (const CLI::ParseError &e)
        {
            return app.exit(e);
        }
        catch (const std::exception &e)
        {
            std::cerr << fmt::format("❌{}ArgParseError{}: {}", AMRED, AMEND, e.what()) << std::endl;
            exit(-1);
        }

        CliPara::Options opt{only_file, only_dir, force_overlap, quiet, use_regex, mkdir, permanent_delete, conflict_newname};
        std::shared_ptr<CB> call_ptr = nullptr;
        if (!opt.quiet)
        {
            call_ptr = std::make_shared<CB>(CliFunc::OriCallback);
        }

        std::vector<task> TASKS;
        if (copy_cmd->parsed())
        {
            CliFunc::CopyMove(op::COPY, cp_srcs, cp_dst, opt, TASKS, call_ptr);
        }
        else if (clone_cmd->parsed())
        {
            TASKS.emplace_back(task(op::COPY, cl_src, AMPath::dirname(cl_dst), AMPath::basename(cl_dst), opt.mkdir));
        }
        else if (move_cmd->parsed())
        {
            CliFunc::CopyMove(op::MOVE, mv_srcs, mv_dst, opt, TASKS, call_ptr);
        }
        else if (replace_cmd->parsed())
        {
            TASKS.emplace_back(task(op::MOVE, mr_src, AMPath::dirname(mr_dst), AMPath::basename(mr_dst), opt.mkdir));
        }
        else if (remove_cmd->parsed())
        {
            CliFunc::Remove(op::REMOVE, rm_paths, opt, TASKS, call_ptr);
        }
        else if (rename_cmd->parsed())
        {
            TASKS.emplace_back(task(op::RENAME, rn_src, "", rn_dst_name, opt.mkdir));
        }
        else if (new_cmd->parsed())
        {
            CliFunc::New(new_paths, opt, TASKS);
        }
        else if (amstore_cmd->parsed())
        {
            CliFunc::StoreView();
        }
        else
        {
            std::cerr << fmt::format("❌{}InvalidArgument{}: No valid Funtion name provided!", AMRED, AMEND) << std::endl;
            exit(-5);
        }

        if (TASKS.empty() && !opt.quiet)
        {
            std::cout << "EmptyTasks: No tasks matched" << std::endl;
            exit(0);
        }
        auto exp = ExplorerAPI();
        ECM ecm = exp.Init(opt.set);
        if (ecm.first != EC::SUCCESS)
        {
            std::cerr << fmt::format("{}❌{}{}: {}", AMRED, GetECName(ecm.first), AMEND, ecm.second) << std::endl;
            exit(static_cast<int>(ecm.first));
        }

        if (opt.is_recursive)
        {
            if (!quiet)
            {
                std::string pmt;
                for (auto task_i : TASKS)
                {
                    switch (task_i.action)
                    {
                    case op::COPY:
                    case op::MOVE:
                    {
                        pmt = fmt::format("{}: {}", magic_enum::enum_name(task_i.action), task_i.src);
                        break;
                    }
                    case op::REMOVE:
                    {
                        if (opt.permanent)
                        {
                            pmt = fmt::format("{}: {} -> $null", magic_enum::enum_name(task_i.action), task_i.src);
                        }
                        else
                        {
                            pmt = fmt::format("{}: {} -> $RecycleBin", magic_enum::enum_name(task_i.action), task_i.src);
                        }
                        break;
                    }
                    case op::RENAME:
                    {
                        pmt = fmt::format("{}: {} -> {}", magic_enum::enum_name(task_i.action), task_i.src, task_i.dst_name);
                        break;
                    }
                    default:
                    {
                        pmt = "";
                        break;
                    }
                    }
                    if (!pmt.empty())
                    {
                        std::cout << pmt << std::endl;
                    }
                }

                if (!TASKS[0].dst_dir.empty())
                {
                    amprint(fmt::format("==>> {}{}{}", AMBLUE, TASKS[0].dst_dir, AMEND));
                }
            }

            char choice;
            std::cout << fmt::format("Are you sure to pend all {} tasks? (y/n): ", TASKS.size());
            std::cin >> choice;
            if (choice != 'y')
            {
                exit(0);
            }
        }

        bool has_task = false;
        for (auto task_i : TASKS)
        {

            ecm = exp.PendOperation(task_i);

            if (ecm.first != EC::SUCCESS)
            {
                amprint(fmt::format("{}⚠️{}{}: {}", AMYELLOW, GetECName(ecm.first), AMEND, ecm.second));
            }
            else
            {
                has_task = true;
            }
        }

        if (!has_task)
        {
            amprint(fmt::format("{}❌TaskPendError{}: All tasks pend failed!", AMRED, AMEND));
            exit(-5);
        }
        ecm = exp.Conduct();
        if (ecm.first != EC::SUCCESS)
        {
            amprint(fmt::format("{}❌{}{}: {}", AMRED, GetECName(ecm.first), AMEND, ecm.second));
            exit(static_cast<int>(ecm.first));
        }
    }
    catch (std::exception e)
    {
        amprint(fmt::format("{}❌UnepectedError{}: {}", AMRED, AMEND, e.what()));
        exit(-13);
    }
}
