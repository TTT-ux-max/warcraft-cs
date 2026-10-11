#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace HudHints {
// Pack whole shortcut clauses into rows; wrap long clauses at spaces on narrow windows.
inline std::vector<std::string> Wrap(const std::string& text,int columns) {
    columns=std::max(1,columns);std::vector<std::string> rows;std::string row;
    size_t start=0;
    while (start<text.size()) {
        size_t end=text.find(" | ",start);if (end==std::string::npos) end=text.size();
        std::string clause=text.substr(start,end-start);
        if (!row.empty() && row.size()+3+clause.size()>size_t(columns)) {rows.push_back(row);row.clear();}
        while (clause.size()>size_t(columns)) {
            size_t split=clause.rfind(' ',size_t(columns));if (!split) split=std::string::npos;
            if (split==std::string::npos) split=size_t(columns);
            rows.push_back(clause.substr(0,split));clause.erase(0,split);
            if (!clause.empty() && clause[0]==' ') clause.erase(0,1);
        }
        if (!row.empty()) row+=" | ";row+=clause;
        start=end==text.size() ? end : end+3;
    }
    if (!row.empty()) rows.push_back(row);return rows;
}
}
