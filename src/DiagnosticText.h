#pragma once
#include <string>
#include <cctype>
#include <utility>
// Chatmail's connectivity page contains IMAP AND SMTP details. Keep it native,
// remove the stylesheet, preserve headings, and decode escaped server errors.
inline std::string ConnectivityText(const std::string& html) {
    auto begin = html.find("<body>");
    if (begin == std::string::npos) return "Connectivity details unavailable";
    std::string out;
    bool space = false;
    for (size_t i = begin + 6; i < html.size();) {
        if (html[i] == '<') {
            auto end = html.find('>',i); if (end == std::string::npos) break;
            auto tag = html.substr(i + 1,end - i - 1);
            if ((tag == "/h3" || tag == "/li" || tag == "/div" || tag.compare(0,2,"br") == 0)
                && !out.empty() && out.back() != '\n') out += '\n';
            i = end + 1; space = false;
        } else if (std::isspace(static_cast<unsigned char>(html[i]))) { space = true; ++i; }
        else {
            if (space && !out.empty() && out.back() != '\n') out += ' ';
            space = false; out += html[i++];
        }
    }
    for (auto pair : {std::pair<const char*,const char*>("&lt;","<"), {"&gt;",">"},
            {"&quot;","\""}, {"&#39;","'"}, {"&#x27;","'"}, {"&nbsp;"," "}, {"&amp;","&"}}) {
        size_t pos = 0; std::string from(pair.first), to(pair.second);
        while ((pos = out.find(from,pos)) != std::string::npos) { out.replace(pos,from.size(),to); pos += to.size(); }
    }
    return out;
}
