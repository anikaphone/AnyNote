#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

void Check(bool valid, const char* error) { if (!valid) throw std::runtime_error(error); }
size_t Discard(char*, size_t size, size_t count, void*) { return size * count; }
int Cancel(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) { return 1; }
CURLcode Request(const std::string& url, const std::filesystem::path& cert, bool cancel = false) {
    CURL* curl = curl_easy_init();
    Check(curl != nullptr, "curl init");
    auto path = cert.u8string();
    std::string utf8(path.begin(), path.end());
    auto set = [&](CURLoption option, auto value) {
        if (curl_easy_setopt(curl, option, value) != CURLE_OK) { curl_easy_cleanup(curl); throw std::runtime_error("curl option"); }
    };
    set(CURLOPT_URL, url.c_str());
    set(CURLOPT_CAINFO, utf8.c_str());
    set(CURLOPT_SSL_VERIFYPEER, 1L);
    set(CURLOPT_SSL_VERIFYHOST, 2L);
    set(CURLOPT_PROTOCOLS_STR, "https");
    set(CURLOPT_REDIR_PROTOCOLS_STR, "https");
    set(CURLOPT_FOLLOWLOCATION, 0L);
    set(CURLOPT_PROXY, ""); // Isolated loopback probe, no ambient proxy.
    set(CURLOPT_CONNECTTIMEOUT_MS, 2000L);
    set(CURLOPT_TIMEOUT_MS, 1000L);
    set(CURLOPT_WRITEFUNCTION, &Discard);
    if (cancel) { set(CURLOPT_NOPROGRESS, 0L); set(CURLOPT_XFERINFOFUNCTION, &Cancel); }
    auto result = curl_easy_perform(curl);
    long status = 0;
    if (result == CURLE_OK) {
        Check(curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK, "curl status");
        if (status != 200) result = CURLE_HTTP_RETURNED_ERROR;
    }
    curl_easy_cleanup(curl);
    return result;
}
int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 2, "TLS fixture directory required");
        Check(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK, "curl global init");
        const auto* info = curl_version_info(CURLVERSION_NOW);
        Check(info && info->ssl_version && std::string(info->ssl_version).starts_with("OpenSSL/"), "wrong TLS backend");
        std::cout << "curl=" << info->version << " tls=" << info->ssl_version << '\n';
        std::filesystem::path dir(argv[1]);
        std::ifstream input(dir / "ready.json"); nlohmann::json config; input >> config;
        const auto valid = config.at("valid").get<std::string>();
        Check(Request(valid + "/ok", dir/"valid.crt") == CURLE_OK, "100 year self-signed certificate rejected");
        Check(Request(valid + "/ok", dir/"wrong-host.crt") == CURLE_PEER_FAILED_VERIFICATION, "untrusted certificate accepted");
        Check(Request(config.at("wrong-host").get<std::string>() + "/ok", dir/"wrong-host.crt") == CURLE_PEER_FAILED_VERIFICATION, "SAN mismatch accepted");
        Check(Request(config.at("expired").get<std::string>() + "/ok", dir/"expired.crt") == CURLE_PEER_FAILED_VERIFICATION, "expired certificate accepted");
        Check(Request("http://127.0.0.1:1/", dir/"valid.crt") == CURLE_UNSUPPORTED_PROTOCOL, "plaintext HTTP accepted");
        Check(Request(valid + "/redirect", dir/"valid.crt") == CURLE_HTTP_RETURNED_ERROR, "redirect followed");
        Check(Request(valid + "/slow", dir/"valid.crt") == CURLE_OPERATION_TIMEDOUT, "timeout failed");
        Check(Request(valid + "/slow", dir/"valid.crt", true) == CURLE_ABORTED_BY_CALLBACK, "cancel failed");
        curl_global_cleanup();
        std::cout << "TLS trust/SAN/expiry/HTTPS/redirect/timeout/cancel passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
