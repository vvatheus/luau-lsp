#include <iostream>
#include <optional>
#include "LSP/Client.hpp"

void Client::sendRequest(const id_type& id, const std::string& method, std::optional<json> params, std::optional<ResponseHandler> handler)
{
    json msg{
        {"jsonrpc", "2.0"},
        {"method", method},
        {"id", id},
        {"params", params},
    };

    // Register a response handler
    if (handler)
        responseHandler.emplace(id, *handler);

    sendRawMessage(msg);
}

void Client::sendResponse(const id_type& id, const json& result)
{
    json msg{
        {"jsonrpc", "2.0"},
        {"result", result},
        {"id", id},
    };

    sendRawMessage(msg);
}

void Client::sendError(const std::optional<id_type>& id, const JsonRpcException& e)
{
    json msg{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {{"code", e.code}, {"message", e.message}, {"data", e.data}}},
    };

    sendRawMessage(msg);
}

void Client::sendNotification(const std::string& method, std::optional<json> params)
{
    json msg{
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", params},
    };

    sendRawMessage(msg);
}

void Client::sendLogMessage(lsp::MessageType type, std::string message)
{
    json params{
        {"type", type},
        {"message", message},
    };
    sendNotification("window/logMessage", params);
}

void Client::sendTrace(std::string message, std::optional<std::string> verbose)
{
    if (traceMode == lsp::TraceValue::Off)
        return;
    json params{{"message", message}};
    if (verbose && traceMode == lsp::TraceValue::Verbose)
        params["verbose"] = verbose.value();
    sendNotification("$/logTrace", params);
};

void Client::sendWindowMessage(lsp::MessageType type, std::string message)
{
    lsp::ShowMessageParams params{type, message};
    sendNotification("window/showMessage", params);
}

void Client::registerCapability(std::string method, json registerOptions)
{
    lsp::Registration registration{"TODO-I-NEED-TO-FIX-THIS", method, registerOptions}; // TODO: registration ID
    // TODO: handle responses?
    sendRequest(nextRequestId++, "client/registerCapability", lsp::RegistrationParams{{registration}});
}

const ClientConfiguration Client::getConfiguration(const lsp::DocumentUri& uri)
{
    auto key = uri.toString();
    if (configStore.find(key) != configStore.end())
    {
        return configStore.at(key);
    }
    return globalConfig;
}

void Client::removeConfiguration(const lsp::DocumentUri& uri)
{
    configStore.erase(uri.toString());
}

void Client::requestConfiguration(const std::vector<lsp::DocumentUri>& uris)
{
    if (uris.empty())
        return;

    std::vector<lsp::ConfigurationItem> items;
    for (auto& uri : uris)
    {
        items.emplace_back(lsp::ConfigurationItem{uri, "luau-lsp"});
    }

    ResponseHandler handler = [uris, this](const JsonRpcMessage& message)
    {
        if (auto result = message.result)
        {
            if (result->is_array())
            {
                lsp::GetConfigurationResponse configs = *result;

                auto workspaceIt = uris.begin();
                auto configIt = configs.begin();

                while (workspaceIt != uris.end() && configIt != configs.end())
                {
                    auto uri = *workspaceIt;
                    ClientConfiguration config = *configIt;
                    configStore.insert_or_assign(uri.toString(), config);
                    sendLogMessage(lsp::MessageType::Info, "loaded configuration for " + uri.toString());
                    ++workspaceIt;
                    ++configIt;
                }

                // See if we have some remainders
                if (workspaceIt != uris.end() || configIt != configs.end())
                {
                    sendLogMessage(lsp::MessageType::Warning, "erroneuous config provided");
                }
            }
        }
    };

    sendRequest(nextRequestId++, "workspace/configuration", lsp::ConfigurationParams{items}, handler);
}

void Client::applyEdit(const lsp::ApplyWorkspaceEditParams& params, std::optional<ResponseHandler> handler)
{
    sendRequest(nextRequestId++, "workspace/applyEdit", params, handler);
}

void Client::refreshWorkspaceDiagnostics()
{
    if (capabilities.workspace && capabilities.workspace->diagnostics && capabilities.workspace->diagnostics->refreshSupport)
    {
        sendRequest(nextRequestId++, "workspace/diagnostics/refresh", nullptr);
    }
}

void Client::setTrace(const lsp::SetTraceParams& params)
{
    traceMode = params.value;
}

bool Client::readRawMessage(std::string& output)
{
    return json_rpc::readRawMessage(std::cin, output);
}

void Client::handleResponse(const JsonRpcMessage& message)
{
    // We run our own exception catcher here because we don't want an exception escaping
    // and then the main loop handler returning that. Since the client has sent a response
    // they no longer have knowledge about this. If we fail, we should just log it and move on.
    try
    {
        if (message.id)
        {
            // Check if a response handler was registered for this response
            if (responseHandler.find(*message.id) == responseHandler.end())
                return;

            // Call the handler on the message
            auto handler = responseHandler.at(*message.id);
            handler(message);

            // Deregister the handler
            responseHandler.erase(*message.id);
        }
    }
    catch (const std::exception& e)
    {
        sendLogMessage(lsp::MessageType::Error, "failed to process response " + json(message.id).dump() + " - " + e.what());
    }
}

void Client::sendRawMessage(const json& message)
{
    json_rpc::sendRawMessage(std::cout, message);
}
