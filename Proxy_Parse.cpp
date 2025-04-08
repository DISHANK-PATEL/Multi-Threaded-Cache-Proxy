#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <cstdarg>

using namespace std;

#define DEBUG_MODE 0

void debug(const char *format, ...) {
    if (DEBUG_MODE) {
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
    }
}

class ParsedHeader {
public:
    string key;
    string value;

    ParsedHeader(const string &k, const string &v) : key(k), value(v) {}

    size_t lineLen() const {
        return key.size() + 2 + value.size() + 2; // "key: value\r\n"
    }
};

class ParsedRequest {
public:
    string buf;
    string method;
    string protocol;
    string host;
    string port;
    string path;
    string version;
    vector<ParsedHeader> headers;

    ParsedRequest() {}

    int parse(const string &input);
    int unparse(string &output);
    int unparse_headers(string &output);
    size_t totalLen() const;

private:
    size_t requestLineLen() const;
    int printRequestLine(ostringstream &oss) const;
    size_t headersLen() const;
};

int ParsedRequest::parse(const string &input) {
    const size_t MIN_REQ_LEN = 4, MAX_REQ_LEN = 65535;
    const string root_abs_path = "/";

    if (input.size() < MIN_REQ_LEN || input.size() > MAX_REQ_LEN)
        return -1;

    size_t header_end = input.find("\r\n\r\n");
    if (header_end == string::npos)
        return -1;

    size_t first_line_end = input.find("\r\n");
    if (first_line_end == string::npos)
        return -1;

    buf = input.substr(0, first_line_end);
    istringstream iss(buf);

    if (!(iss >> method)) return -1;
    if (method != "GET") return -1;

    string full_addr;
    if (!(iss >> full_addr)) return -1;
    if (!(iss >> version)) return -1;
    if (version.find("HTTP/") != 0) return -1;

    size_t pos = full_addr.find("://");
    if (pos == string::npos) return -1;

    protocol = full_addr.substr(0, pos);
    size_t start = pos + 3;
    size_t path_start = full_addr.find('/', start);
    if (path_start == string::npos) return -1;

    string host_port = full_addr.substr(start, path_start - start);
    size_t colon_pos = host_port.find(':');
    if (colon_pos != string::npos) {
        host = host_port.substr(0, colon_pos);
        port = host_port.substr(colon_pos + 1);
        try {
            int port_num = stoi(port);
            if (port_num == 0) return -1;
        } catch (exception &) {
            return -1;
        }
    } else {
        host = host_port;
    }

    path = full_addr.substr(path_start);
    if (path.empty()) {
        path = root_abs_path;
    } else if (path.size() >= 2 && path[0] == '/' && path[1] == '/') {
        return -1;
    } else if (path[0] != '/') {
        path = "/" + path;
    }

    size_t header_line_start = first_line_end + 2;
    while (header_line_start < header_end) {
        size_t header_line_end = input.find("\r\n", header_line_start);
        if (header_line_end == string::npos || header_line_end > header_end)
            break;

        string line = input.substr(header_line_start, header_line_end - header_line_start);
        if (line.empty()) break;

        size_t colon = line.find(':');
        if (colon == string::npos) return -1;

        string key = line.substr(0, colon);
        size_t value_start = colon + 1;
        if (value_start < line.size() && line[value_start] == ' ')
            value_start++;
        string value = line.substr(value_start);

        auto it = remove_if(headers.begin(), headers.end(),
                            [&key](const ParsedHeader &h) { return h.key == key; });
        if (it != headers.end()) headers.erase(it, headers.end());

        headers.push_back(ParsedHeader(key, value));
        header_line_start = header_line_end + 2;
    }

    return 0;
}

size_t ParsedRequest::requestLineLen() const {
    size_t len = method.size() + 1 + protocol.size() + 3 + host.size() + path.size() + 1 + version.size() + 2;
    if (!port.empty()) len += 1 + port.size();
    return len;
}

int ParsedRequest::printRequestLine(ostringstream &oss) const {
    oss << method << " " << protocol << "://" << host;
    if (!port.empty()) oss << ":" << port;
    oss << path << " " << version << "\r\n";
    return 0;
}

size_t ParsedRequest::headersLen() const {
    size_t len = 0;
    for (const auto &ph : headers)
        len += ph.lineLen();
    len += 2;
    return len;
}

int ParsedRequest::unparse(string &output) {
    ostringstream oss;
    if (printRequestLine(oss) < 0)
        return -1;
    for (const auto &ph : headers)
        oss << ph.key << ": " << ph.value << "\r\n";
    oss << "\r\n";
    output = oss.str();
    return 0;
}

int ParsedRequest::unparse_headers(string &output) {
    ostringstream oss;
    for (const auto &ph : headers)
        oss << ph.key << ": " << ph.value << "\r\n";
    oss << "\r\n";
    output = oss.str();
    return 0;
}

size_t ParsedRequest::totalLen() const {
    return requestLineLen() + headersLen();
}

int main() {
    string request =
        "GET http://example.com:8080/path/to/resource HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "User-Agent: TestAgent\r\n"
        "\r\n";

    ParsedRequest pr;
    if (pr.parse(request) != 0) {
        cerr << "Failed to parse request." << endl;
        return EXIT_FAILURE;
    }

    string unparsed;
    if (pr.unparse(unparsed) != 0) {
        cerr << "Failed to unparse request." << endl;
        return EXIT_FAILURE;
    }

    cout << "Unparsed request:\n" << unparsed << endl;
    return EXIT_SUCCESS;
}
