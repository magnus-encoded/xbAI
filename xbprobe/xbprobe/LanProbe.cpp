#include "pch.h"
#include "Probe.h"

#include <atomic>
#include <iomanip>
#include <mutex>
#include <string>
#include <ppltasks.h>

// ---------------------------------------------------------------------------
// T3 gate: can a Game-classified UWP app accept inbound LAN TCP from the dev PC?
//
// Pure-WinRT serving path per ADR-0002: a `StreamSocketListener` bound to a fixed
// port + a hand-rolled HTTP/1.1 200 response. No native HTTP lib (so no extra
// app-container forbidden-API risk). The manifest must declare
// `privateNetworkClientServer` for the inbound bind to be permitted.
//
// The listener outlives this function (kept in a global), and the
// ConnectionReceived handler rewrites the report file each time a request lands —
// so the `curl` from the dev PC, which happens AFTER the initial report write,
// still leaves a pullable breadcrumb in LocalState.
// ---------------------------------------------------------------------------

using namespace Windows::Networking;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;
using namespace Windows::Foundation;
using namespace concurrency;

namespace
{
    StreamSocketListener^ g_listener = nullptr;   // kept alive for the app lifetime
    std::mutex            g_lanMutex;
    std::wstring          g_lanBase;               // full report up to the conn log
    std::wstring          g_lanLog;                // accumulated per-connection lines
    int                   g_connCount = 0;
    std::atomic<bool>     g_served{ false };        // flipped once a real request is served

    std::wstring HrHex(HRESULT hr)
    {
        std::wstringstream s;
        s << L"0x" << std::hex << std::uppercase << std::setw(8) << std::setfill(L'0') << (uint32_t)hr;
        return s.str();
    }

    // Fires on a threadpool thread when an inbound TCP connection is accepted.
    // Reads the request line, answers HTTP/1.1 200, and live-appends to the report.
    void OnConnection(StreamSocketListener^, StreamSocketListenerConnectionReceivedEventArgs^ args)
    {
        std::wstring line;
        try
        {
            StreamSocket^ socket = args->Socket;

            std::wstring remote = L"?";
            try
            {
                remote = std::wstring(socket->Information->RemoteAddress->DisplayName->Data())
                       + L":" + std::wstring(socket->Information->RemotePort->Data());
            }
            catch (...) {}

            // --- read the request (partial: we only need the request line) ---
            DataReader^ reader = ref new DataReader(socket->InputStream);
            reader->InputStreamOptions = InputStreamOptions::Partial;
            unsigned int got = create_task(reader->LoadAsync(2048)).get();
            std::wstring reqLine;
            if (got > 0)
            {
                Platform::String^ reqText = reader->ReadString(got);
                reqLine.assign(reqText->Data());
                auto nl = reqLine.find_first_of(L"\r\n");
                if (nl != std::wstring::npos) reqLine = reqLine.substr(0, nl);
            }
            reader->DetachStream();

            // --- write a trivial HTTP/1.1 200 response ---
            std::string body = "xbAI T3 LAN serve OK\n";
            std::string resp = "HTTP/1.1 200 OK\r\n"
                               "Content-Type: text/plain; charset=utf-8\r\n"
                               "Content-Length: " + std::to_string(body.size()) + "\r\n"
                               "Connection: close\r\n"
                               "\r\n" + body;

            DataWriter^ writer = ref new DataWriter(socket->OutputStream);
            auto bytes = ref new Platform::Array<unsigned char>((unsigned int)resp.size());
            memcpy(bytes->Data, resp.data(), resp.size());
            writer->WriteBytes(bytes);
            create_task(writer->StoreAsync()).get();
            create_task(writer->FlushAsync()).get();
            writer->DetachStream();

            delete socket; // close the connection (Connection: close)

            std::wstringstream s;
            s << L"[conn #" << (++g_connCount) << L"] from " << remote
              << L"  req=\"" << reqLine << L"\"  -> 200 OK (" << resp.size() << L" bytes)\n";
            line = s.str();
            g_served.store(true);
        }
        catch (Platform::Exception^ e)
        {
            std::wstringstream s;
            s << L"[conn #" << (++g_connCount) << L"] EXCEPTION hr=" << HrHex(e->HResult)
              << L"  msg=" << std::wstring(e->Message->Data()) << L"\n";
            line = s.str();
        }
        catch (...)
        {
            line = L"[conn] non-WinRT exception while serving\n";
        }

        std::lock_guard<std::mutex> lk(g_lanMutex);
        g_lanLog += line;
        WriteReport(g_lanBase + g_lanLog);
    }
}

// Polled by the render loop so the TV color can flip once a real inbound request
// has been served (PROBE_LAN_SERVED), distinct from merely binding the port.
bool LanServedAtLeastOnce()
{
    return g_served.load();
}

std::wstring RunLanServeProbe(unsigned int& statusInOut, const std::wstring& reportSoFar)
{
    std::wstringstream r;
    const wchar_t* kPort = L"8080";

    r << L"\n================ T3: LAN inbound serving reachability ================\n";
    r << L"[T3] manifest capability declared: privateNetworkClientServer\n";

    try
    {
        g_listener = ref new StreamSocketListener();
        g_listener->ConnectionReceived +=
            ref new TypedEventHandler<StreamSocketListener^, StreamSocketListenerConnectionReceivedEventArgs^>(&OnConnection);
        r << L"[T3] StreamSocketListener created\n";

        // BindServiceNameAsync is the inbound-listen step; it is what the
        // privateNetworkClientServer capability gates. Block on it (MTA thread).
        create_task(g_listener->BindServiceNameAsync(ref new Platform::String(kPort))).get();

        Platform::String^ localPort = g_listener->Information->LocalPort;
        r << L"[T3] BindServiceNameAsync(\"" << kPort << L"\"): OK  (LocalPort="
          << std::wstring(localPort->Data()) << L")\n";
        statusInOut |= PROBE_LAN_BOUND;
        r << L"[T3] inbound listener UP. From the dev PC: curl http://192.168.1.233:" << kPort << L"/\n";
    }
    catch (Platform::Exception^ e)
    {
        r << L"[T3] BIND/LISTEN FAILED  hr=" << HrHex(e->HResult)
          << L"  msg=" << std::wstring(e->Message->Data()) << L"\n";
        r << L"[T3] => inbound LAN serving NOT available under current manifest/profile\n";
    }
    catch (...)
    {
        r << L"[T3] BIND/LISTEN FAILED (non-WinRT exception)\n";
    }

    r << L"--- connection log (live-updated as inbound requests arrive) ---\n";

    // Seed the global base text so the ConnectionReceived handler can rewrite the
    // whole file (capability report + this T3 section) when a request lands.
    {
        std::lock_guard<std::mutex> lk(g_lanMutex);
        g_lanBase = reportSoFar + r.str();
    }
    return r.str();
}
