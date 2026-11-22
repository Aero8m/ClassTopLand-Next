#include "CloudScreen.h"
#include "CloudEndpoint.h"

#ifdef CTX_SCREEN_PREVIEW
#include <QBuffer>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QOperatingSystemVersion>
#include <QRegularExpression>
#include <QScreen>

#include <windows.h>
#include <d3d11.h>
#include <roapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
#include <optional>

namespace {
// The WinRT ABI keeps this module compatible with both MSVC and clang-cl in C++17.
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;
using Microsoft::WRL::Wrappers::RoInitializeWrapper;
namespace Capture = ABI::Windows::Graphics::Capture;
namespace Direct3D = ABI::Windows::Graphics::DirectX::Direct3D11;
using ABI::Windows::Graphics::DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized;
constexpr int kMaxFrameWidth = 1920;
constexpr int kMaxFrameHeight = 1080;
constexpr qsizetype kMaxJpegBytes = 2 * 1024 * 1024;
constexpr int kFrameCheckIntervalMs = 50;
constexpr int kCaptureWaitMs = 4000;

void checkResult(HRESULT result)
{
    if (FAILED(result)) throw result;
}

template<typename Interface>
ComPtr<Interface> activationFactory(HSTRING name)
{
    ComPtr<Interface> factory;
    checkResult(RoGetActivationFactory(name, IID_PPV_ARGS(factory.GetAddressOf())));
    return factory;
}

template<typename Interface>
void closeCaptureObject(const ComPtr<Interface>& object)
{
    ComPtr<ABI::Windows::Foundation::IClosable> closable;
    if (object && SUCCEEDED(object.As(&closable))) closable->Close();
}

bool interactiveDesktop()
{
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session) || session != WTSGetActiveConsoleSessionId())
        return false;
    const HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desktop) return false;
    wchar_t name[64]{};
    DWORD length = 0;
    const bool available = GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &length) &&
                           QString::fromWCharArray(name).compare(QStringLiteral("Default"), Qt::CaseInsensitive) == 0;
    CloseDesktop(desktop);
    return available;
}

HMONITOR monitorByName(const QString& name)
{
    // Match the Qt screen name reported by CloudOperations, then resolve its Windows handle.
    for (const auto* screen : QGuiApplication::screens()) {
        if (screen->name().left(256) != name) continue;
        const auto* native = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
        return native ? native->handle() : nullptr;
    }
    return nullptr;
}

QJsonObject dataObject(const QByteArray& bytes)
{
    return QJsonDocument::fromJson(bytes).object().value(QStringLiteral("data")).toObject();
}
} // namespace

struct CloudScreen::Session
{
    std::optional<RoInitializeWrapper> apartment;
    QString id;
    QString name;
    HMONITOR monitor = nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<Direct3D::IDirect3DDevice> graphicsDevice;
    ComPtr<Capture::IGraphicsCaptureSession> capture;
    ComPtr<Capture::IDirect3D11CaptureFramePool> frames;
    ABI::Windows::Graphics::SizeInt32 size{};
    ComPtr<ID3D11Texture2D> staging;
    QElapsedTimer captureWait;

    ~Session()
    {
        closeCaptureObject(capture);
        closeCaptureObject(frames);
    }

    void startCapture()
    {
        apartment.emplace(RO_INIT_SINGLETHREADED);
        checkResult(*apartment);
        const auto sessionFactory = activationFactory<Capture::IGraphicsCaptureSessionStatics>(
            HStringReference(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureSession).Get());
        boolean supported = false;
        checkResult(sessionFactory->IsSupported(&supported));
        if (!supported) throw E_NOTIMPL;

        checkResult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                     nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                                     context.GetAddressOf()));
        ComPtr<IDXGIDevice> dxgi;
        checkResult(device.As(&dxgi));
        ComPtr<IInspectable> graphics;
        checkResult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), graphics.GetAddressOf()));
        checkResult(graphics.As(&graphicsDevice));

        const auto itemFactory = activationFactory<IGraphicsCaptureItemInterop>(
            HStringReference(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem).Get());
        ComPtr<Capture::IGraphicsCaptureItem> item;
        checkResult(itemFactory->CreateForMonitor(monitor, IID_PPV_ARGS(item.GetAddressOf())));
        checkResult(item->get_Size(&size));
        const auto framesFactory = activationFactory<Capture::IDirect3D11CaptureFramePoolStatics2>(
            HStringReference(RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool).Get());
        checkResult(framesFactory->CreateFreeThreaded(graphicsDevice.Get(),
            DirectXPixelFormat_B8G8R8A8UIntNormalized, 2, size, frames.GetAddressOf()));
        checkResult(frames->CreateCaptureSession(item.Get(), capture.GetAddressOf()));
        checkResult(capture->StartCapture());
        captureWait.start();
    }

    QByteArray nextImage()
    {
        ComPtr<Capture::IDirect3D11CaptureFrame> frame;
        checkResult(frames->TryGetNextFrame(frame.GetAddressOf()));
        if (!frame) return {};
        ABI::Windows::Graphics::SizeInt32 received{};
        checkResult(frame->get_ContentSize(&received));
        if (received.Width != size.Width || received.Height != size.Height) {
            size = received;
            closeCaptureObject(frame);
            frame.Reset();
            staging.Reset();
            checkResult(frames->Recreate(graphicsDevice.Get(), DirectXPixelFormat_B8G8R8A8UIntNormalized, 2, size));
            return {};
        }
        ComPtr<Direct3D::IDirect3DSurface> surface;
        checkResult(frame->get_Surface(surface.GetAddressOf()));
        ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
        checkResult(surface.As(&access));
        ComPtr<ID3D11Texture2D> source;
        checkResult(access->GetInterface(IID_PPV_ARGS(source.GetAddressOf())));
        if (!staging) {
            D3D11_TEXTURE2D_DESC desc{};
            source->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.BindFlags = desc.MiscFlags = 0;
            checkResult(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
        }
        context->CopyResource(staging.Get(), source.Get());
        D3D11_MAPPED_SUBRESOURCE mapping{};
        checkResult(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapping));
        const QImage view(static_cast<const uchar*>(mapping.pData), size.Width, size.Height,
                          mapping.RowPitch, QImage::Format_RGB32);
        const QImage image = (view.width() > kMaxFrameWidth || view.height() > kMaxFrameHeight)
            ? view.scaled(kMaxFrameWidth, kMaxFrameHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            : view.copy();
        context->Unmap(staging.Get(), 0);
        for (const int quality : {92, 82, 72}) {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            if (!image.save(&buffer, "JPEG", quality)) throw E_FAIL;
            if (bytes.size() <= kMaxJpegBytes) return bytes;
        }
        throw E_FAIL;
    }
};
#else
struct CloudScreen::Session {};
#endif

CloudScreen::CloudScreen(QObject* parent) : QObject(parent)
{
    frameTimer_.setTimerType(Qt::PreciseTimer);
    connect(&pollTimer_, &QTimer::timeout, this, &CloudScreen::poll);
    connect(&frameTimer_, &QTimer::timeout, this, &CloudScreen::frame);
}

CloudScreen::~CloudScreen() { stop(); }

bool CloudScreen::supported()
{
#ifdef CTX_SCREEN_PREVIEW
    // CreateForMonitor is available starting with Windows 10 version 1903.
    return QOperatingSystemVersion::current() >= QOperatingSystemVersion(
        QOperatingSystemVersion::Windows, 10, 0, 18362);
#else
    return false;
#endif
}

void CloudScreen::configure(const QString& origin, const QByteArray& bearer)
{
    if (!supported() || bearer.isEmpty() || (origin_ == origin && bearer_ == bearer)) return;
    stop();
    origin_ = origin;
    bearer_ = bearer;
    pollTimer_.start(5000);
    poll();
}

void CloudScreen::stop()
{
    pollTimer_.stop();
    frameTimer_.stop();
    api_.abortAll();
    session_.reset();
    origin_.clear();
    bearer_.clear();
    lastRequestId_.clear();
    polling_ = false;
}

void CloudScreen::poll()
{
#ifdef CTX_SCREEN_PREVIEW
    if (polling_ || bearer_.isEmpty()) return;
    polling_ = true;
    api_.postJson(Cloud::apiUrl(origin_, QStringLiteral("/device/screen/poll")), bearer_, {},
                  [this](const CloudApi::Reply& response) {
        polling_ = false;
        if (!response.ok()) {
            if (response.status == 401) stop();
            else if (response.status == 404) {
                end();
                pollTimer_.start(60000);
            }
            return;
        }
        if (pollTimer_.interval() != 5000) pollTimer_.start(5000);
        const auto target = dataObject(response.body).value(QStringLiteral("session")).toObject();
        const auto id = target.value(QStringLiteral("id")).toString();
        static const QRegularExpression pattern(QStringLiteral("^[a-f0-9]{32}$"));
        if (!pattern.match(id).hasMatch()) {
            end();
            return;
        }
        if (lastRequestId_ == id) return;
        end();
        lastRequestId_ = id;
        begin(target);
    });
#endif
}

void CloudScreen::begin(const QJsonObject& target)
{
#ifdef CTX_SCREEN_PREVIEW
    session_ = std::make_unique<Session>();
    auto& view = *session_;
    view.id = target.value(QStringLiteral("id")).toString();
    view.name = target.value(QStringLiteral("monitor")).toString();
    view.monitor = monitorByName(view.name);
    if (!interactiveDesktop()) return end(QStringLiteral("desktop_unavailable"));
    if (!view.monitor) return end(QStringLiteral("monitor_unavailable"));
    try {
        view.startCapture();
    } catch (HRESULT) {
        return end(QStringLiteral("capture_failed"));
    }
    frameTimer_.start(kFrameCheckIntervalMs);
#else
    Q_UNUSED(target);
#endif
}

void CloudScreen::frame()
{
#ifdef CTX_SCREEN_PREVIEW
    if (!session_) return;
    auto& view = *session_;
    if (!interactiveDesktop()) return end(QStringLiteral("desktop_unavailable"));
    if (monitorByName(view.name) != view.monitor) return end(QStringLiteral("monitor_unavailable"));
    if (view.captureWait.hasExpired(kCaptureWaitMs)) return end(QStringLiteral("capture_failed"));
    QByteArray image;
    try { image = view.nextImage(); }
    catch (HRESULT) { return end(QStringLiteral("capture_failed")); }
    if (image.isEmpty()) return;
    if (!interactiveDesktop()) return end(QStringLiteral("desktop_unavailable"));
    const auto id = view.id;
    end();
    api_.postJpeg(Cloud::apiUrl(origin_, QStringLiteral("/device/screen/%1/frame").arg(id)), bearer_, image,
                  [this, id](const CloudApi::Reply& reply) {
        if (reply.status == 401) return stop();
        if (!reply.ok() && reply.status != 404 && reply.status != 409)
            reportFailure(id, QStringLiteral("upload_failed"));
    });
#endif
}

void CloudScreen::end(const QString& reason)
{
#ifdef CTX_SCREEN_PREVIEW
    frameTimer_.stop();
    if (!session_) return;
    const QString id = session_->id;
    session_.reset();
    if (!reason.isEmpty()) reportFailure(id, reason);
#else
    Q_UNUSED(reason);
#endif
}

#ifdef CTX_SCREEN_PREVIEW
void CloudScreen::reportFailure(const QString& id, const QString& reason)
{
    if (!bearer_.isEmpty())
        api_.postJson(Cloud::apiUrl(origin_, QStringLiteral("/device/screen/%1/stop").arg(id)), bearer_,
                      {{QStringLiteral("reason"), reason}}, [](const CloudApi::Reply&) {});
}
#endif
