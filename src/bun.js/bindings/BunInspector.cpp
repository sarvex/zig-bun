#include "BunInspector.h"
#include <JavaScriptCore/Heap.h>
#include <JavaScriptCore/JSGlobalObject.h>
#include "JSGlobalObjectInspectorController.h"

namespace Zig {

WTF_MAKE_ISO_ALLOCATED_IMPL(BunInspector);

void BunInspector::sendMessageToFrontend(const String& message)
{

    String out = message;
    auto jsonObject = WTF::JSONImpl::Value::parseJSON(message);
    if (jsonObject) {
        if (auto object = jsonObject->asObject()) {
            auto method = object->getString("method"_s);

            if (method == "Debugger.scriptParsed"_s) {
                if (auto params = object->getObject("params"_s)) {
                    params->setInteger("executionContextId"_s, 1);

                    params->setString("url"_s, makeString("file://"_s, params->getString("url"_s)));
                }

                out = object->toJSONString();
            }
        }
    }

    auto utf8Message = out.utf8();
    std::string_view view { utf8Message.data(), utf8Message.length() };
    this->server->publish("BunInspectorConnection", view, uWS::OpCode::TEXT, false);
}

RefPtr<BunInspector> BunInspector::startWebSocketServer(
    WebCore::ScriptExecutionContext& context,
    WTF::String hostname,
    uint16_t port,
    WTF::Function<void(RefPtr<BunInspector>, bool success)>&& callback)
{
    context.ensureURL();
    auto url = context.url();
    auto identifier = url.fileSystemPath();

    auto title = makeString(
        url.fileSystemPath(),
        " (Bun "_s, Bun__version, ")"_s);

    auto* globalObject = context.jsGlobalObject();

    uWS::App* app = new uWS::App();
    RefPtr<BunInspector> inspector = adoptRef(*new BunInspector(&context, app, WTFMove(identifier)));
    auto host = hostname.utf8();

    // https://chromedevtools.github.io/devtools-protocol/  GET /json or /json/list
    app->get("/json", [hostname, port, url, title = title, inspector](auto* res, auto* /*req*/) {
           auto identifier = inspector->identifier();
           auto jsonString = makeString(
               "[ {\"description\": \"\", \"devtoolsFrontendUrl\": \"devtools://devtools/bundled/js_app.html?experiments=false&v8only=true&ws="_s,
               hostname,
               ":"_s,
               port,
               "/devtools/page/"_s,
               identifier,
               "\","_s
               "  \"id\": \"6e99c4f9-6bb6-4f45-9749-5772545b2371\","_s,
               "  \"title\": \""_s,
               title,
               "\","
               "  \"type\": \"node\","_s,
               "  \"url\": \"file://"_s,
               identifier,
               "\","_s
               "  \"webSocketDebuggerUrl\": \"ws://"_s,
               hostname,
               ":"_s,
               port,
               "/devtools/page/"_s,
               identifier,
               "\"} ]"_s);
           auto utf8 = jsonString.utf8();
           res->writeStatus("200 OK");
           res->writeHeader("Content-Type", "application/json");
           res->end(utf8.data(), utf8.length());
       })
        .get("/json/version", [](auto* res, auto* req) {
            auto out = makeString("{\"Browser\": \"node.js/19.6.0\", \"Protocol-Version\": \"1.1\"}"_s);
            auto utf8 = out.utf8();
            res->writeStatus("200 OK");
            res->writeHeader("Content-Type", "application/json");
            res->end({ utf8.data(), utf8.length() });
        })
        .ws<BunInspector*>("/*", { /* Settings */
                                     .compression = uWS::DISABLED,
                                     .maxPayloadLength = 1024 * 1024 * 1024,
                                     .idleTimeout = 512,
                                     .maxBackpressure = 64 * 1024 * 1024,
                                     .closeOnBackpressureLimit = false,
                                     .resetIdleTimeoutOnSend = false,
                                     .sendPingsAutomatically = true,
                                     /* Handlers */
                                     .upgrade = nullptr,
                                     .open = [inspector](auto* ws) {
                                                                   *ws->getUserData() = inspector.get();
                                                                   ws->subscribe("BunInspectorConnection");
                                                                   inspector->connect(Inspector::FrontendChannel::ConnectionType::Local); },
                                     .message = [inspector](auto* ws, std::string_view message, uWS::OpCode opCode) {
                                                                       if (opCode == uWS::OpCode::TEXT) {
                                                                           if (!inspector) {
                                                                               ws->close();
                                                                               return;
                                                                           }
                                                                           


                                                                           inspector->dispatchToBackend(message);
                                                                       } },
                                     .drain = [](auto* /*ws*/) {
    /* Check ws->getBufferedAmount() here */ },
                                     .ping = [](auto* /*ws*/, std::string_view) {
    /* Not implemented yet */ },
                                     .pong = [](auto* /*ws*/, std::string_view) {
    /* Not implemented yet */ },
                                     .close = [](auto* ws, int /*code*/, std::string_view /*message*/) { 
                                                                    
                                                                    auto* connection = *ws->getUserData();
                                                                    if (connection) {
                                                                        connection->disconnect();
                                                                    } } })
        .any("/*", [](auto* res, auto* req) {
            res->writeStatus("404 Not Found");
            res->writeHeader("Content-Type", "text/plain");
            res->write(req->getUrl());
            res->end(" was not found");
        })
        .listen(std::string(host.data(), host.length()), port, [inspector, callback = WTFMove(callback)](auto* listen_socket) {
            if (listen_socket) {
                callback(inspector, true);
            } else {
                callback(inspector, false);
            }
        });
    ;

    return inspector;
}

void BunInspector::dispatchToBackend(std::string_view message)
{
    WTF::CString data { message.data(), message.length() };
    WTF::String msg = WTF::String::fromUTF8(data.data(), data.length());
    auto jsonObject = WTF::JSONImpl::Value::parseJSON(msg);
    // if (auto object = jsonObject->asObject()) {
    //     auto method = object->getString("method"_s);

    //     if (method == "Profiler.enable"_s || method == "Runtime.runIfWaitingForDebugger"_s || method == "Debugger.setAsyncCallStackDepth"_s || method == "Debugger.setBlackboxPatterns"_s) {

    //         if (auto id = object.get()->getInteger("id"_s)) {
    //             auto response = makeString(
    //                 "{\"id\":"_s,
    //                 id.value(),
    //                 "\"result\":{}}"_s);

    //             sendMessageToFrontend(response);
    //             return;
    //         }
    //     } else if (method == "Runtime.getHeapUsage"_s) {

    //         if (auto id = object.get()->getInteger("id"_s)) {
    //             auto& heap = globalObject()->vm().heap;
    //             int usedSize = heap.size();
    //             int totalSize = heap.capacity();

    //             auto response = makeString(
    //                 "{\"id\":"_s,
    //                 id.value(),
    //                 "\"result\":{ "_s,
    //                 "\"usedSize\": "_s, usedSize, "\"totalSize\":"_s, totalSize, "}}"_s);

    //             sendMessageToFrontend(response);
    //             return;
    //         }
    //     } else if (method == "Runtime.getIsolateId"_s) {

    //         if (auto id = object.get()->getInteger("id"_s)) {
    //             auto& heap = globalObject()->vm().heap;
    //             int usedSize = heap.size();
    //             int totalSize = heap.capacity();

    //             auto response = makeString(
    //                 "{\"id\":"_s,
    //                 id.value(),
    //                 "\"result\": \"123\"}"_s);

    //             sendMessageToFrontend(response);
    //             return;
    //         }
    //     }
    // }
    globalObject()->inspectorController().backendDispatcher().dispatch(msg);
}

void BunInspector::sendMessageToTargetBackend(const WTF::String& message)
{
    globalObject()->inspectorController().dispatchMessageFromFrontend(message);
}

void BunInspector::connect(Inspector::FrontendChannel::ConnectionType connectionType)
{
    globalObject()->inspectorController().connectFrontend(*this, false, false);
}

void BunInspector::disconnect()
{
    globalObject()->inspectorController().disconnectFrontend(*this);
}

} // namespace Zig