#include "io/StyledJson.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace mtfui::io {
    namespace {
        constexpr std::size_t kRightMargin = 74;

        std::string Quote(const std::string& s) {
            std::string out = "\"";
            for (unsigned char c : s) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b"; break;
                    case '\f': out += "\\f"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (c < 0x20) {
                            char buf[8];
                            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                            out += buf;
                        } else {
                            out += static_cast<char>(c);
                        }
                }
            }
            out += '"';
            return out;
        }

        class Writer {
        public:
            std::string Write(const nlohmann::json& root) {
                indented_ = true;
                WriteValue(root);
                return doc_;
            }

        private:
            std::string doc_;
            std::string indent_;
            bool indented_ = false;
            bool addChildValues_ = false;
            std::vector<std::string> childValues_;

            void PushValue(const std::string& v) {
                if (addChildValues_) {
                    childValues_.push_back(v);
                } else {
                    doc_ += v;
                }
            }

            void WriteIndent() {
                doc_ += "\r\n";
                doc_ += indent_;
            }

            void WriteWithIndent(const std::string& v) {
                if (!indented_) {
                    WriteIndent();
                }
                doc_ += v;
                indented_ = false;
            }

            void Indent() { indent_ += '\t'; }
            void Unindent() { indent_.pop_back(); }

            void WriteValue(const nlohmann::json& v) {
                switch (v.type()) {
                    case nlohmann::json::value_t::null: PushValue("null"); break;
                    case nlohmann::json::value_t::boolean: PushValue(v.get<bool>() ? "true" : "false"); break;
                    case nlohmann::json::value_t::number_integer: PushValue(std::to_string(v.get<std::int64_t>())); break;
                    case nlohmann::json::value_t::number_unsigned: PushValue(std::to_string(v.get<std::uint64_t>())); break;
                    case nlohmann::json::value_t::number_float: PushValue(FormatDouble(v.get<double>())); break;
                    case nlohmann::json::value_t::string: PushValue(Quote(v.get<std::string>())); break;
                    case nlohmann::json::value_t::array: WriteArray(v); break;
                    case nlohmann::json::value_t::object: WriteObject(v); break;
                    default: PushValue("null"); break;
                }
            }

            void WriteObject(const nlohmann::json& v) {
                if (v.empty()) {
                    PushValue("{}");
                    return;
                }
                WriteWithIndent("{");
                Indent();
                std::size_t i = 0;
                for (auto it = v.begin(); it != v.end(); ++it, ++i) {
                    WriteWithIndent(Quote(it.key()));
                    doc_ += " : ";
                    WriteValue(it.value());
                    if (i + 1 < v.size()) {
                        doc_ += ",";
                    }
                }
                Unindent();
                WriteWithIndent("}");
            }

            bool IsMultilineArray(const nlohmann::json& v) {
                const std::size_t size = v.size();
                bool multi = size * 3 >= kRightMargin;
                childValues_.clear();
                for (std::size_t i = 0; i < size && !multi; ++i) {
                    const auto& c = v[i];
                    multi = (c.is_array() || c.is_object()) && !c.empty();
                }
                if (!multi) {
                    childValues_.reserve(size);
                    addChildValues_ = true;
                    std::size_t lineLength = 4 + (size - 1) * 2;
                    for (std::size_t i = 0; i < size; ++i) {
                        WriteValue(v[i]);
                        lineLength += childValues_[i].length();
                    }
                    addChildValues_ = false;
                    multi = lineLength >= kRightMargin;
                }
                return multi;
            }

            void WriteArray(const nlohmann::json& v) {
                if (v.empty()) {
                    PushValue("[]");
                    return;
                }
                if (IsMultilineArray(v)) {
                    WriteWithIndent("[");
                    Indent();
                    const bool hasChildValues = !childValues_.empty();
                    for (std::size_t i = 0; i < v.size(); ++i) {
                        if (hasChildValues) {
                            WriteWithIndent(childValues_[i]);
                        } else {
                            if (!indented_) {
                                WriteIndent();
                            }
                            indented_ = true;
                            WriteValue(v[i]);
                            indented_ = false;
                        }
                        if (i + 1 < v.size()) {
                            doc_ += ",";
                        }
                    }
                    Unindent();
                    WriteWithIndent("]");
                } else {
                    std::string line = "[ ";
                    for (std::size_t i = 0; i < childValues_.size(); ++i) {
                        if (i > 0) {
                            line += ", ";
                        }
                        line += childValues_[i];
                    }
                    line += " ]";
                    doc_ += line;
                }
            }
        };
    }

    std::string FormatDouble(double value) {
        if (std::isnan(value) || std::isinf(value)) {
            return "0.0";
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", value);
        std::string s = buf;
        if (s.find_first_of(".e") == std::string::npos) {
            s += ".0";
        }
        return s;
    }

    std::string WriteStyled(const nlohmann::json& value) {
        Writer w;
        return w.Write(value);
    }
}
