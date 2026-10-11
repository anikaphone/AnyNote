#include "SyncClient.h"
#include "TransferPipe.h"
#include "common/StringUtils.h"
#include <curl/curl.h>
#include <windows.h>
#include <filesystem>
#include <memory>
#include <set>
#include <thread>
#include <algorithm>
#include <cctype>

namespace anynote::sync {
namespace {
void Check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
struct Runtime { Runtime() { Check(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK, "TLS_INIT_FAILED"); } ~Runtime() { curl_global_cleanup(); } };
Runtime& Curl() { static Runtime runtime; return runtime; }
using Easy = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
struct Headers { curl_slist* value = nullptr; ~Headers() { curl_slist_free_all(value); }
    void Add(const std::string& text) { auto next = curl_slist_append(value, text.c_str()); Check(next != nullptr, "OUT_OF_MEMORY"); value = next; } };
nlohmann::json ControlResponse(std::istream& input, size_t maximum = 32 * 1024 * 1024) {
    std::string data; std::array<char, 16384> buffer{};
    while (input.read(buffer.data(), buffer.size()) || input.gcount()) {
        Check(data.size() + static_cast<size_t>(input.gcount()) <= maximum, "LIMIT_EXCEEDED"); data.append(buffer.data(), static_cast<size_t>(input.gcount()));
    }
    std::vector<std::set<std::string>> keys;
    auto result = nlohmann::json::parse(data, [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json& value) {
        Check(depth <= 12, "INVALID_RESPONSE");
        if (event == nlohmann::json::parse_event_t::object_start) keys.emplace_back();
        if (event == nlohmann::json::parse_event_t::key) Check(!keys.empty() && keys.back().insert(value.get<std::string>()).second, "DUPLICATE_FIELD");
        if (event == nlohmann::json::parse_event_t::object_end) keys.pop_back(); return true;
    });
    Check(IsUuid(result.at("request_id").get<std::string>()), "INVALID_RESPONSE");
    if (result.contains("error")) throw std::runtime_error(result.at("error").at("code").get<std::string>());
    return result.at("data");
}
}
std::string SyncClient::NormalizeServerUrl(const std::string& baseUrl) {
    Curl();
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(curl_url(), curl_url_cleanup);
    Check(url && curl_url_set(url.get(), CURLUPART_URL, baseUrl.c_str(), 0) == CURLUE_OK, "INVALID_SERVER_URL");
    char* value = nullptr;
    Check(curl_url_get(url.get(), CURLUPART_SCHEME, &value, 0) == CURLUE_OK, "INVALID_SERVER_URL");
    std::string scheme(value); curl_free(value); Check(scheme == "https", "HTTPS_REQUIRED");
    for (auto part : {CURLUPART_USER, CURLUPART_PASSWORD, CURLUPART_QUERY, CURLUPART_FRAGMENT}) {
        value = nullptr; auto status = curl_url_get(url.get(), part, &value, 0); curl_free(value);
        Check(status != CURLUE_OK, "INVALID_SERVER_URL");
    }
    value = nullptr;
    Check(curl_url_get(url.get(), CURLUPART_HOST, &value, 0) == CURLUE_OK, "INVALID_SERVER_URL");
    std::string host(value); curl_free(value);
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    Check(curl_url_set(url.get(), CURLUPART_HOST, host.c_str(), 0) == CURLUE_OK, "INVALID_SERVER_URL");
    value = nullptr;
    Check(curl_url_get(url.get(), CURLUPART_URL, &value, CURLU_NO_DEFAULT_PORT) == CURLUE_OK, "INVALID_SERVER_URL");
    std::string normalized(value); curl_free(value);
    while (!normalized.empty() && normalized.back() == '/') normalized.pop_back();
    return normalized;
}
SyncClient::SyncClient(std::string baseUrl, std::wstring caFile) : m_caFile(std::move(caFile)) {
    m_baseUrl = NormalizeServerUrl(baseUrl);
    Check(std::filesystem::is_regular_file(m_caFile), "CA_FILE_MISSING");
}
void SyncClient::ClearToken() { if (!m_token.empty()) SecureZeroMemory(m_token.data(), m_token.size()); m_token.clear(); }
void SyncClient::SetToken(std::string token) {
    Check(token.size() == 43 && token.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") == std::string::npos, "INVALID_TOKEN");
    ClearToken(); m_token = std::move(token);
}
void SyncClient::Transfer(const std::string& method, const std::string& path, TransferControl& control,
    const std::function<void(std::ostream&)>& upload, const std::function<void(std::istream&)>& download) {
    Check(path.starts_with('/') && !path.starts_with("//") && path.find_first_of("\r\n#") == std::string::npos, "INVALID_REQUEST_PATH");
    Easy curl(curl_easy_init(), curl_easy_cleanup); Check(curl != nullptr, "TLS_INIT_FAILED");
    auto set = [&](CURLoption option, auto value) { Check(curl_easy_setopt(curl.get(), option, value) == CURLE_OK, "TLS_OPTION_FAILED"); };
    Headers headers; headers.Add("Content-Type: application/json"); headers.Add("Accept: application/json"); headers.Add("Expect:");
    if (!m_token.empty()) headers.Add("Authorization: Bearer " + m_token);
    const auto url = m_baseUrl + "/api/v1" + path; const auto ca = utils::WideToUtf8(m_caFile);
    set(CURLOPT_URL, url.c_str()); set(CURLOPT_CAINFO, ca.c_str()); set(CURLOPT_SSL_VERIFYPEER, 1L); set(CURLOPT_SSL_VERIFYHOST, 2L);
    set(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2); set(CURLOPT_PROTOCOLS_STR, "https"); set(CURLOPT_REDIR_PROTOCOLS_STR, "https");
    set(CURLOPT_FOLLOWLOCATION, 0L); set(CURLOPT_CONNECTTIMEOUT, 10L); set(CURLOPT_TIMEOUT, 600L);
    set(CURLOPT_LOW_SPEED_LIMIT, 1L); set(CURLOPT_LOW_SPEED_TIME, 60L); set(CURLOPT_NOSIGNAL, 1L);
    set(CURLOPT_HTTPHEADER, headers.value); set(CURLOPT_NOPROGRESS, 0L); set(CURLOPT_XFERINFODATA, &control);
    set(CURLOPT_XFERINFOFUNCTION, +[](void* context, curl_off_t, curl_off_t received, curl_off_t, curl_off_t sent)->int {
        auto& state = *static_cast<TransferControl*>(context); state.sent = sent; state.received = received; return state.cancelled ? 1 : 0;
    });
    TransferPipe outgoing(control), incoming(control);
    set(CURLOPT_WRITEDATA, &incoming);
    set(CURLOPT_WRITEFUNCTION, +[](char* data, size_t size, size_t count, void* context)->size_t {
        try { return static_cast<TransferPipe*>(context)->Write(data, size * count); } catch (...) { return 0; }
    });
    if (upload) {
        set(CURLOPT_UPLOAD, 1L); set(CURLOPT_READDATA, &outgoing);
        set(CURLOPT_READFUNCTION, +[](char* data, size_t size, size_t count, void* context)->size_t {
            try { return static_cast<TransferPipe*>(context)->Read(data, size * count); } catch (...) { return CURL_READFUNC_ABORT; }
        });
    }
    set(CURLOPT_CUSTOMREQUEST, method.c_str());
    std::exception_ptr uploadError, downloadError;
    std::jthread writer;
    if (upload) writer = std::jthread([&] {
        try { PipeOutput buffer(outgoing); std::ostream output(&buffer); output.exceptions(std::ios::badbit | std::ios::failbit); upload(output); outgoing.Close(); }
        catch (...) { uploadError = std::current_exception(); outgoing.Abort(); }
    });
    std::jthread reader([&] {
        try { PipeInput buffer(incoming); std::istream input(&buffer); input.exceptions(std::ios::badbit); download(input); }
        catch (...) { downloadError = std::current_exception(); incoming.Abort(); }
    });
    const auto status = curl_easy_perform(curl.get());
    long httpStatus = 0; curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &httpStatus);
    outgoing.Abort(); incoming.Close(); if (writer.joinable()) writer.join(); reader.join();
    if (control.cancelled) throw std::runtime_error("CANCELLED");
    if (status == CURLE_PEER_FAILED_VERIFICATION || status == CURLE_SSL_CACERT_BADFILE) throw std::runtime_error("CERTIFICATE_REJECTED");
    if (status == CURLE_OPERATION_TIMEDOUT) throw std::runtime_error("TIMEOUT_RESULT_UNKNOWN");
    if (downloadError && (status == CURLE_OK || status == CURLE_WRITE_ERROR)) {
        try { std::rethrow_exception(downloadError); } catch (const std::runtime_error& error) { if (std::string(error.what()) == "UNAUTHORIZED") ClearToken(); throw; }
    }
    if (status != CURLE_OK) throw std::runtime_error("NETWORK_RESULT_UNKNOWN");
    if (uploadError) std::rethrow_exception(uploadError);
    Check(httpStatus >= 200 && httpStatus < 300, "HTTP_REQUEST_FAILED");
}
nlohmann::json SyncClient::Request(const std::string& method, const std::string& path, const nlohmann::json& body, TransferControl& control) {
    nlohmann::json result;
    std::function<void(std::ostream&)> upload;
    if (method != "GET") upload = [&](std::ostream& output) { output << body.dump(); };
    Transfer(method, path, control, upload, [&](std::istream& input) { result = ControlResponse(input, (path.ends_with("/sync-manifest") || path.find("/changes?") != std::string::npos) ? Limits{}.maxJsonBytes : 32 * 1024 * 1024); }); return result;
}
SnapshotResponse SyncClient::Download(const std::string& path, TransferControl& control) {
    SnapshotResponse result;
    Transfer("GET", path, control, {}, [&](std::istream& input) { result = ReadSnapshotResponse(input); }); return result;
}
nlohmann::json SyncClient::Upload(const std::string& path, const nlohmann::json& precondition, const Snapshot& snapshot, bool subtree, TransferControl& control) {
    nlohmann::json result;
    Transfer("PUT", path, control, [&](std::ostream& output) {
        output << "{\"precondition\":" << precondition.dump() << (subtree ? ",\"source\":" : ",\"snapshot\":");
        WriteSnapshot(output, snapshot); output << '}';
    }, [&](std::istream& input) { result = ControlResponse(input); }); return result;
}
std::wstring DescribeSyncError(const std::string& code) {
    if (code == "REALTIME_UPGRADE_REQUIRED") return L"请先升级云端服务以支持实时同步。";
    if (code == "REALTIME_PENDING_OTHER_ACCOUNT") return L"当前笔记库还有其他账户的待确认上传，请切回原账户确认结果。";
    if (code == "REALTIME_BUSY") return L"正在同步，请稍后处理冲突。";
    if (code == "UNAUTHORIZED") return L"登录已失效，请重新登录。";
    if (code == "VERSION_CONFLICT" || code == "LOCAL_CHANGED") return L"笔记库已发生变化，请重新比较并确认。";
    if (code == "CERTIFICATE_REJECTED") return L"服务器证书验证失败，请检查专用证书、有效期和服务器地址。";
    if (code == "NETWORK_RESULT_UNKNOWN" || code == "TIMEOUT_RESULT_UNKNOWN") return L"连接中断，服务端是否提交尚不确定。请查询操作结果后再继续。";
    if (code == "CANCELLED") return L"操作已取消；若已开始上传，仍需查询服务端结果。";
    if (code == "OUTSIDE_SUBTREE") return L"本次移动会影响所选子树之外的笔记，请同步更高层目录或整个库。";
    if (code == "NOT_FOUND") return L"远端笔记库或节点不存在。";
    return L"同步失败：" + utils::Utf8ToWide(code);
}
}
