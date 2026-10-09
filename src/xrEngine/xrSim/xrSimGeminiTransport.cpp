#include "xrSim/xrSimGeminiTransport.h"

#include <atomic>
#include <chrono>

#if defined(XRAY_AGENT_HTTP_CURL)
#include <curl/curl.h>
#endif

namespace xrSim
{
namespace
{
#if defined(XRAY_AGENT_HTTP_CURL)
size_t GeminiWriteBody(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    const size_t bytes = size * nmemb;
    std::string* body = static_cast<std::string*>(userdata);
    body->append(ptr, bytes);
    return bytes;
}

int GeminiCheckCancelled(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    const auto* cancelled = static_cast<const std::atomic_bool*>(userdata);
    return cancelled->load(std::memory_order_acquire) ? 1 : 0;
}

bool GeminiEnsureCurlGlobalInitialized(std::string& error)
{
    static const CURLcode initCode = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initCode != CURLE_OK)
    {
        error = curl_easy_strerror(initCode);
        return false;
    }
    return true;
}

bool GeminiAppendHeader(curl_slist*& headers, const std::string& header)
{
    curl_slist* next = curl_slist_append(headers, header.c_str());
    if (!next)
        return false;
    headers = next;
    return true;
}

class CurlGeminiTransport final : public IGeminiTransport
{
public:
    GeminiTransportResult Send(
        const AgentProviderConfig& config, const GeminiGenerateContentRequest& request) override
    {
        const auto start = std::chrono::steady_clock::now();

        GeminiTransportResult result;
        std::string initError;
        if (!GeminiEnsureCurlGlobalInitialized(initError))
        {
            result.error = "curl_global_init_failed: " + initError;
            return result;
        }
        if (request.method != "POST")
        {
            result.error = "unsupported_http_method";
            return result;
        }

        CURL* curl = curl_easy_init();
        if (!curl)
        {
            result.error = "curl_easy_init_failed";
            return result;
        }

        curl_slist* headers = nullptr;
        const std::string apiKeyHeader = "x-goog-api-key: " + config.apiKey;
        const std::string contentTypeHeader = "content-type: " + request.contentType;
        const bool headersOk = GeminiAppendHeader(headers, apiKeyHeader) &&
            GeminiAppendHeader(headers, contentTypeHeader);
        if (!headersOk)
        {
            if (headers)
                curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            result.error = "curl_header_allocation_failed";
            return result;
        }

        std::string body;
        char errorBuffer[CURL_ERROR_SIZE]{};
        const std::string path =
            (request.path.empty() || request.path.front() != '/') ? ("/" + request.path) : request.path;
        const std::string url = "https://generativelanguage.googleapis.com" + path;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, GeminiWriteBody);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, long(config.timeoutMs));
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "OpenXVibeRay-xrSim/1");
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, GeminiCheckCancelled);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &m_cancelled);

        const CURLcode rc = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

        const auto end = std::chrono::steady_clock::now();
        result.latencyMs = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
        result.status = status < 0 ? 0 : uint32_t(status);
        result.body = body;

        if (rc == CURLE_OK)
        {
            result.ok = true;
        }
        else
        {
            result.ok = false;
            result.error = rc == CURLE_ABORTED_BY_CALLBACK ? "cancelled"
                                                           : (errorBuffer[0] ? errorBuffer : curl_easy_strerror(rc));
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return result;
    }

    void Cancel() override { m_cancelled.store(true, std::memory_order_release); }

private:
    std::atomic_bool m_cancelled{false};
};
#endif
} // namespace

std::unique_ptr<IGeminiTransport> CreateGeminiHttpTransport()
{
#if defined(XRAY_AGENT_HTTP_CURL)
    return std::make_unique<CurlGeminiTransport>();
#else
    return nullptr;
#endif
}

bool IsGeminiHttpTransportAvailable()
{
#if defined(XRAY_AGENT_HTTP_CURL)
    return true;
#else
    return false;
#endif
}

std::string DescribeGeminiHttpTransport()
{
    return IsGeminiHttpTransportAvailable() ? "curl" : "unavailable";
}
} // namespace xrSim
