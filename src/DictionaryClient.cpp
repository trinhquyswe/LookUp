#include "DictionaryClient.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <vector>
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

bool DictionaryClient::Lookup(const std::wstring& word, DictionaryEntry& outEntry) {
    if (word.empty()) return false;

    std::wstring encodedWord = UrlEncode(word);
    
    // 1. Query dictionaryapi.dev (English dictionary)
    std::wstring dictHost = L"api.dictionaryapi.dev";
    std::wstring dictPath = L"/api/v2/entries/en/" + encodedWord;
    
    std::string response = HttpGet(dictHost, dictPath, true);
    
    if (!response.empty()) {
        try {
            json j = json::parse(response);
            if (j.is_array() && !j.empty()) {
                outEntry.word = word;
                
                // Get phonetic
                std::wstring phonetic;
                if (j[0].contains("phonetic") && j[0]["phonetic"].is_string()) {
                    phonetic = Utf8ToWide(j[0]["phonetic"].get<std::string>());
                }
                
                // Get audio url & fallback phonetic from phonetics list
                std::wstring audioUrl;
                if (j[0].contains("phonetics") && j[0]["phonetics"].is_array()) {
                    for (const auto& ph : j[0]["phonetics"]) {
                        if (phonetic.empty() && ph.contains("text") && ph["text"].is_string()) {
                            phonetic = Utf8ToWide(ph["text"].get<std::string>());
                        }
                        if (audioUrl.empty() && ph.contains("audio") && ph["audio"].is_string()) {
                            std::string audioStr = ph["audio"].get<std::string>();
                            if (!audioStr.empty()) {
                                audioUrl = Utf8ToWide(audioStr);
                            }
                        }
                    }
                }
                outEntry.phonetic = phonetic;
                outEntry.audioUrl = audioUrl;

                // Format definitions list (limit to 3 for clean card UI)
                std::wstring definition;
                if (j[0].contains("meanings") && j[0]["meanings"].is_array()) {
                    int count = 0;
                    for (const auto& meaning : j[0]["meanings"]) {
                        std::string pos = meaning.value("partOfSpeech", "");
                        if (meaning.contains("definitions") && meaning["definitions"].is_array() && !meaning["definitions"].empty()) {
                            std::string defText = meaning["definitions"][0].value("definition", "");
                            if (!defText.empty()) {
                                if (!definition.empty()) definition += L"\n";
                                definition += Utf8ToWide(pos) + L": " + Utf8ToWide(defText);
                                if (++count >= 3) break;
                            }
                        }
                    }
                }
                outEntry.definition = definition;
                return true;
            }
        } catch (...) {
            // JSON parsing failed, try fallback dictionary
        }
    }

    // 2. Fallback: Query dict.minhqnd.com (Vietnamese bilingual translations)
    std::wstring fallbackHost = L"dict.minhqnd.com";
    std::wstring fallbackPath = L"/api/v1/lookup?word=" + encodedWord;

    std::string fallbackResponse = HttpGet(fallbackHost, fallbackPath, true);

    if (!fallbackResponse.empty()) {
        try {
            json j = json::parse(fallbackResponse);
            if (j.value("exists", false) && j.contains("results") && j["results"].is_array() && !j["results"].empty()) {
                const auto& res = j["results"][0];
                outEntry.word = word;

                // Get phonetic (IPA)
                std::wstring phonetic;
                if (res.contains("pronunciations") && res["pronunciations"].is_array() && !res["pronunciations"].empty()) {
                    phonetic = Utf8ToWide(res["pronunciations"][0].value("ipa", ""));
                }
                outEntry.phonetic = phonetic;

                // Get relative TTS audio url and map to absolute domain
                std::wstring audioUrl;
                if (res.contains("audio") && res["audio"].is_string()) {
                    std::string audioPath = res["audio"].get<std::string>();
                    if (!audioPath.empty()) {
                        audioUrl = L"https://dict.minhqnd.com" + Utf8ToWide(audioPath);
                    }
                }
                outEntry.audioUrl = audioUrl;

                // Format definitions list (Vietnamese translation)
                std::wstring definition;
                if (res.contains("meanings") && res["meanings"].is_array()) {
                    int count = 0;
                    for (const auto& meaning : res["meanings"]) {
                        std::string pos = meaning.value("pos", "");
                        std::string defText = meaning.value("definition", "");
                        if (!defText.empty()) {
                            if (!definition.empty()) definition += L"\n";
                            definition += Utf8ToWide(pos) + L": " + Utf8ToWide(defText);
                            if (++count >= 3) break;
                        }
                    }
                }
                outEntry.definition = definition;
                return true;
            }
        } catch (...) {
            // Failed fallback parsing
        }
    }

    return false;
}

std::string DictionaryClient::HttpGet(const std::wstring& host, const std::wstring& path, bool useHttps) {
    std::string response;
    HINTERNET hSession = WinHttpOpen(
        L"LookUp/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (hSession) {
        // Set connection/receive timeouts (3 seconds)
        DWORD timeout = 3000;
        WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

        INTERNET_PORT port = useHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);

        if (hConnect) {
            DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
            HINTERNET hRequest = WinHttpOpenRequest(
                hConnect,
                L"GET",
                path.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                flags
            );

            if (hRequest) {
                BOOL bResults = WinHttpSendRequest(
                    hRequest,
                    WINHTTP_NO_ADDITIONAL_HEADERS,
                    0,
                    WINHTTP_NO_REQUEST_DATA,
                    0,
                    0,
                    0
                );

                if (bResults) {
                    bResults = WinHttpReceiveResponse(hRequest, nullptr);
                }

                if (bResults) {
                    DWORD dwSize = 0;
                    do {
                        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                        if (dwSize == 0) break;

                        std::vector<char> buffer(dwSize);
                        DWORD dwRead = 0;
                        if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwRead)) {
                            response.append(buffer.data(), dwRead);
                        }
                    } while (dwSize > 0);
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
    }
    return response;
}

std::wstring DictionaryClient::UrlEncode(const std::wstring& wstr) {
    std::string utf8 = WideToUtf8(wstr);
    std::wostringstream escaped;
    escaped << std::hex << std::uppercase;
    for (char c : utf8) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << static_cast<wchar_t>(c);
        } else {
            escaped << L'%' << std::setfill(L'0') << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
    return escaped.str();
}

std::string DictionaryClient::WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string str(sizeNeeded - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], sizeNeeded, nullptr, nullptr);
    return str;
}

std::wstring DictionaryClient::Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring wstr(sizeNeeded - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], sizeNeeded);
    return wstr;
}
