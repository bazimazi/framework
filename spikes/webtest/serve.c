// Spike P0-07: minimal HTTP test server that launches headless Edge and waits for
// the page to POST /result. Throwaway. Uses the CRT (host tool, amendment A-01).
// Usage: serve <root_dir> <page_path> <timeout_ms> [extra browser flags...]
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma comment(lib, "ws2_32.lib")

static const char* mime(const char* p) {
    const char* e = strrchr(p, '.');
    if (!e) return "application/octet-stream";
    if (!strcmp(e, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(e, ".js")) return "text/javascript";
    if (!strcmp(e, ".wasm")) return "application/wasm";
    return "application/octet-stream";
}

static int recv_all(SOCKET s, char* buf, int cap, int* out_len, char** body, int* body_len) {
    int n = 0;
    for (;;) {
        int r = recv(s, buf + n, cap - n - 1, 0);
        if (r <= 0) return 0;
        n += r; buf[n] = 0;
        char* h = strstr(buf, "\r\n\r\n");
        if (h) {
            int clen = 0; char* cl = strstr(buf, "Content-Length:"); if (!cl) cl = strstr(buf, "content-length:");
            if (cl) clen = atoi(cl + 15);
            int have = n - (int)(h + 4 - buf);
            while (have < clen && n < cap - 1) { r = recv(s, buf + n, cap - n - 1, 0); if (r <= 0) break; n += r; have += r; }
            buf[n] = 0; *out_len = n; *body = h + 4; *body_len = clen; return 1;
        }
        if (n >= cap - 1) return 0;
    }
}

static void respond(SOCKET c, int code, const char* type, const char* data, int len) {
    char hdr[512];
    int h = snprintf(hdr, sizeof hdr, "HTTP/1.1 %d X\r\nContent-Type: %s\r\nContent-Length: %d\r\nCache-Control: no-store\r\n"
        "Cross-Origin-Opener-Policy: same-origin\r\nCross-Origin-Embedder-Policy: require-corp\r\nConnection: close\r\n\r\n", code, type, len);
    send(c, hdr, h, 0); if (len) send(c, data, len, 0);
}

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: serve root page timeout_ms [flags]\n"); return 2; }
    const char* root = argv[1]; const char* page = argv[2]; int timeout_ms = atoi(argv[3]);
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    bind(ls, (struct sockaddr*)&a, sizeof a); listen(ls, 16);
    int alen = sizeof a; getsockname(ls, (struct sockaddr*)&a, &alen); int port = ntohs(a.sin_port);

    char tmpdir[MAX_PATH]; GetTempPathA(MAX_PATH, tmpdir); char profile[MAX_PATH]; snprintf(profile, sizeof profile, "%sbmf_edge_%lu", tmpdir, GetCurrentProcessId());
    char cmd[4096]; int cl = snprintf(cmd, sizeof cmd, "\"C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe\" --headless=new --user-data-dir=\"%s\" --no-first-run --no-default-browser-check --disable-extensions", profile);
    for (int i = 4; i < argc; i++) cl += snprintf(cmd + cl, sizeof cmd - cl, " %s", argv[i]);
    snprintf(cmd + cl, sizeof cmd - cl, " http://127.0.0.1:%d/%s", port, page);
    fprintf(stderr, "{\"serve\":\"launch\",\"port\":%d}\n", port);
    HANDLE job = CreateJobObjectA(0, 0);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {0}; li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    STARTUPINFOA si = {sizeof si}; PROCESS_INFORMATION pi;
    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    if (!CreateProcessA(0, cmd, 0, 0, FALSE, CREATE_SUSPENDED, 0, 0, &si, &pi)) { fprintf(stderr, "launch failed %lu\n", GetLastError()); return 2; }
    AssignProcessToJobObject(job, pi.hProcess); ResumeThread(pi.hThread);

    static char buf[64 << 20];
    int result = -1; DWORD start = GetTickCount();
    while (result < 0) {
        fd_set rs; FD_ZERO(&rs); FD_SET(ls, &rs); struct timeval tv = {0, 200000};
        if ((int)(GetTickCount() - start) > timeout_ms) { fprintf(stderr, "{\"serve\":\"timeout\"}\n"); result = 3; break; }
        if (select(0, &rs, 0, 0, &tv) <= 0) continue;
        SOCKET c = accept(ls, 0, 0); if (c == INVALID_SOCKET) continue;
        int n, blen; char* body;
        if (!recv_all(c, buf, sizeof buf, &n, &body, &blen)) { closesocket(c); continue; }
        char method[8] = {0}, path[1024] = {0}; sscanf(buf, "%7s %1023s", method, path);
        if (!strcmp(method, "POST")) {
            if (!strncmp(path, "/result", 7)) {
                QueryPerformanceCounter(&t1);
                fwrite(body, 1, blen, stdout); fputc('\n', stdout); fflush(stdout);
                fprintf(stderr, "{\"serve\":\"result\",\"ms\":%.1f}\n", (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart);
                result = strstr(body, "\"ok\":1") ? 0 : 1;
            } else if (!strncmp(path, "/log", 4)) {
                fprintf(stderr, "page: %.*s\n", blen, body);
            } else if (!strncmp(path, "/file/", 6)) {
                char fp[2048]; snprintf(fp, sizeof fp, "%s/out/%s", root, path + 6);
                FILE* o = fopen(fp, "wb"); if (o) { fwrite(body, 1, blen, o); fclose(o); }
                fprintf(stderr, "{\"serve\":\"file\",\"path\":\"%s\",\"bytes\":%d}\n", fp, blen);
            }
            respond(c, 200, "text/plain", "ok", 2);
        } else {
            char fp[2048]; char* q = strchr(path, '?'); if (q) *q = 0;
            snprintf(fp, sizeof fp, "%s%s", root, path);
            FILE* in = fopen(fp, "rb");
            if (!in) { respond(c, 404, "text/plain", "nf", 2); }
            else { static char fb[32 << 20]; int len = (int)fread(fb, 1, sizeof fb, in); fclose(in); respond(c, 200, mime(fp), fb, len); }
        }
        closesocket(c);
    }
    CloseHandle(job);   // kills Edge and all its children
    return result;
}
