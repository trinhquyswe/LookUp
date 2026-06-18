#pragma once

#include <string>

struct DictionaryEntry {
    std::wstring word;
    std::wstring phonetic;
    std::wstring definition;
    std::wstring audioUrl;
};

class DictionaryClient {
public:
    // Lookup a word, querying dictionaryapi.dev first, falling back to dict.minhqnd.com on failure/404
    static bool Lookup(const std::wstring& word, DictionaryEntry& outEntry);

private:
    // Internal synchronous HTTP request helper
    static std::string HttpGet(const std::wstring& host, const std::wstring& path, bool useHttps = true);
    
    // Helper to URL encode query parameters
    static std::wstring UrlEncode(const std::wstring& wstr);
    
    // String utility conversions
    static std::string WideToUtf8(const std::wstring& wstr);
    static std::wstring Utf8ToWide(const std::string& str);
};
